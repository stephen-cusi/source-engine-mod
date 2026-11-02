//=============================================================================
//
// HL2SB: GMod-compatible sound.PlayFile / IGModAudioChannel runtime (CLIENT).
//
// Design (reference-behavior notes):
//
//  * One output device. Decoded PCM is summed into the engine paint buffer
//    through the HL2SBLuaStreamMix001 engine interface. The engine calls the
//    registered callback on its own mix thread; that callback only reads
//    buffered PCM and applies gain/pan, it never decodes or does file IO.
//
//  * Decoding happens on one worker thread owned by this file. Streaming
//    channels own an SPSC ring buffer (worker produces, mix thread consumes);
//    "noblock" channels are fully decoded into an in-memory block, which is
//    also what IGModAudioChannel:SetTime / EnableLooping require.
//
//  * Files are opened through the engine file system ("GAME" path), so files
//    inside mounted VPK/GMA archives behave the same as loose files.
//
//  * GMod interface behavior:
//      - sound.PlayFile( path, flags, callback ); path relative to the game
//        root; flags space separated: "3d", "mono", "noplay", "noblock".
//      - callback( channel, errorID, errorName ); errorID/errorName follow
//        the BASS error code table addons branch on.
//      - 41 IGModAudioChannel methods, client realm; GMOD_CHANNEL_* and
//        FFT_* are global constants.
//      - Collecting the channel stops the sound; addons keep a reference
//        while playing (PlayURL example comment).
//
//  * Documented differences (nothing is silently faked):
//      - Online streams (sound.PlayURL) are not implemented.
//      - GetTagsHTTP/Meta/MP4/WMA return nil (offline only). GetTagsOGG /
//        GetTagsVendor are a planned phase 2. GetTagsID3 reads ID3v1 only
//        (same as the reference contract).
//      - 3D audio is a distance/pan approximation of the reference 3D mixer,
//        not a bit-exact port. Cone attenuation is stored but not applied.
//      - GetAverageBitRate and GetBitsPerSample return 0 (the documented
//        "unknown" answer).
//
// Threading model:
//   main thread       - Lua API, Play/Pause/Stop, load dispatch, lifecycle
//   worker thread     - VFS open, miniaudio decode, ring/block filling
//   engine mix thread - the HL2SBLuaStreamMix callback only
// Cross-thread fields are tier0 interlocked or documented per field; the
// decoder itself is guarded by a per-channel mutex because restarts seek it
// from the main thread while the worker reads it.
//
//=============================================================================

#include "cbase.h"
#include "luamanager.h"
#include "luasrclib.h"
#include "mathlib/lvector.h"
#include "filesystem.h"
#include "tier0/threadtools.h"
#include "utlvector.h"
#include "engine/iluastreammix.h"
#include "engine/iluaaudiodecoder.h"
#include "view.h"
#include "lgmodaudio.h"
#include <atomic>
#include <cmath>

// lua_State *L (the game client realm state) comes from luamanager.h.

// memdbgon must be the last include file in a .cpp file.
#include "memdbgon.h"

//=============================================================================
// BASS error codes (reference contract; addons branch on these names/numbers)
//=============================================================================

enum
{
	BASS_ERROR_MEM      = 1,
	BASS_ERROR_FILEOPEN = 2,
	BASS_ERROR_FORMAT   = 6,
	BASS_ERROR_UNKNOWN  = -1,
	BASS_ERROR_NOTAVAIL = 37,
	BASS_ERROR_FILEFORM = 41,
};

struct HL2SBBassErrorName_t
{
	int nCode;
	const char *pName;
};

static const HL2SBBassErrorName_t g_BassErrorNames[] =
{
	{ BASS_ERROR_MEM,      "BASS_ERROR_MEM" },
	{ BASS_ERROR_FILEOPEN, "BASS_ERROR_FILEOPEN" },
	{ BASS_ERROR_FORMAT,   "BASS_ERROR_FORMAT" },
	{ BASS_ERROR_NOTAVAIL, "BASS_ERROR_NOTAVAIL" },
	{ BASS_ERROR_FILEFORM, "BASS_ERROR_FILEFORM" },
	{ BASS_ERROR_UNKNOWN,  "BASS_ERROR_UNKNOWN" },
};

static const char *BassErrorName( int nCode )
{
	for ( int i = 0; i < ( int )ARRAYSIZE( g_BassErrorNames ); ++i )
	{
		if ( g_BassErrorNames[ i ].nCode == nCode )
			return g_BassErrorNames[ i ].pName;
	}
	return "BASS_ERROR_UNKNOWN";
}

//=============================================================================
// Channel constants
//=============================================================================

// Everything decodes to this rate. The reference backend also plays through a
// fixed output rate; IGModAudioChannel:GetSamplingRate is documented as
// "always 44100".
static const uint32 AUDIO_RATE = 44100;

// Streaming ring capacity in frames (power of two). 32768 frames ~0.74s.
static const uint32 RING_FRAMES = 32768;
static const uint32 RING_MASK   = RING_FRAMES - 1;

// Worker decode chunk, frames.
static const uint64 DECODE_CHUNK_FRAMES = 2048;

// Frames buffered before the load callback fires for streaming channels
// (~46ms at 44.1k; the reference fires once the stream is creatable).
static const uint64 STREAM_PRIME_FRAMES = 2048;

// noblock decode cap: 512 MB of f32 PCM.
static const uint64 NOBLOCK_MAX_BYTES = 512ull * 1024ull * 1024ull;

static const float AUDIO_FADEMIN_DEFAULT = 200.0f;
static const float AUDIO_FADEMAX_DEFAULT = 1000000000.0f;

// channel state (matches GMOD_CHANNEL; LOADING is our own pre-callback state)
enum HL2SBChannelState_e
{
	CHANSTATE_LOADING = -1,
	CHANSTATE_STOPPED = 0,
	CHANSTATE_PLAYING = 1,
	CHANSTATE_PAUSED  = 2,
};

//=============================================================================
// CLuaAudioChannel
//
// Refcounting contract:
//   +1 initial "load task" reference (created with the channel; released by
//      the main thread at dispatch, or by the worker right after queueing a
//      failure result),
//   +1 per pending success queue entry (released at dispatch),
//   +1 by the Lua userdata (__gc releases it),
//   +1 for membership in the manager channel list (released by the mix thread
//      when a finished channel leaves the list, or on shutdown/level change).
// The worker takes a temporary reference around each unit of work so a
// channel cannot be deleted under it.
//=============================================================================

class CLuaAudioChannel
{
public:
	CLuaAudioChannel();
	~CLuaAudioChannel();

	void AddRef( void ) { ThreadInterlockedIncrement( &m_nRefCount ); }
	void ReleaseRef( void );

	// identity, set once by PlayFile on the main thread before the worker
	// ever sees the channel
	void InitLoad( const char *pPath, bool b3D, bool bMono, bool bNoPlay, bool bNoBlock );
	void SetCallbackRef( int nRef ) { m_nCallbackRef = nRef; }
	void CancelPendingLoad( lua_State *pLua );
	void CloseDecoder( void );

	// --- load path (worker thread) ------------------------------------
	bool OpenDecoder( void );		// opens the VFS file + miniaudio decoder
	void DecodeChunk( void );		// one unit of decode work
	bool NeedsLoadWork( void );
	bool NeedsDecodeWork( void );
	bool CheckLoadProgress( void );	// queues the load result when ready
	void QueueLoadResult( void );

	// --- playback control (main thread) -------------------------------
	void Play( void );
	void Pause( void );
	void Stop( void );
	void SetVolume( float flVolume ) { if ( std::isfinite( flVolume ) && flVolume >= 0.0f ) m_flVolume = flVolume; }
	void SetPan( float flPan ) { if ( std::isfinite( flPan ) && flPan >= -1.0f && flPan <= 1.0f ) m_flPan = flPan; }
	void SetPlaybackRate( float flRate );
	bool EnableLooping( bool bEnable );
	bool SetTime( double flSecs );
	void Set3DPos( const Vector &vecPos, const Vector &vecDirection )
	{
		if ( !m_b3D ) return;
		AUTO_LOCK( m_PosMutex );
		m_vecPos = vecPos;
		m_vecDirection = vecDirection;
	}
	void Set3DEnabled( bool bEnable );
	void Set3DFadeDistance( float flMin, float flMax );
	void Set3DCone( float flInner, float flOuter, float flOuterVolume );

	// --- mix thread ----------------------------------------------------
	bool MixInto( float *pMix, int nFrames, const Vector &vecListener, const QAngle &angListener );

	// --- queries (main thread) -----------------------------------------
	int   GetState( void );
	float GetTime( void );
	float GetLength( void ) const;
	float GetBufferedTime( void );
	bool  Is3D( void ) const { return m_b3D; }
	bool  Get3DEnabled( void ) const { return m_b3D && m_b3DEnabled; }
	bool  IsBlockStreamed( void ) const { return !m_bNoBlock; }
	bool  IsLooping( void ) const { return m_bLooping; }
	bool  IsOnline( void ) const { return false; }
	bool  IsValid( void ) const { return !m_bPendingRemoval; }
	const char *GetFileName( void ) const { return m_szPath; }
	int   GetLoadError( void ) const { return m_nLoadError; }
	bool  IsLoaded( void ) { return GetStateRaw() != CHANSTATE_LOADING; }
	void  GetLevel( float &flLeft, float &flRight ) const;
	int   FFT( float *pBins, int nBins );

	void Get3DFadeDistance( float &flMin, float &flMax ) const { flMin = m_flFadeMin; flMax = m_flFadeMax; }
	void Get3DCone( float &flInner, float &flOuter, float &flOuterVol ) const
	{
		flInner = m_flConeInner; flOuter = m_flConeOuter; flOuterVol = m_flConeOuterVol;
	}
	Vector GetPos( void ) { AUTO_LOCK( m_PosMutex ); return m_vecPos; }
	Vector GetDirection( void ) { AUTO_LOCK( m_PosMutex ); return m_vecDirection; }
	float GetVolume( void ) const { return m_flVolume; }
	float GetPan( void ) const { return m_flPan; }
	float GetPlaybackRate( void ) const { return m_flRate; }

	// --- tags -----------------------------------------------------------
	bool LoadID3Tag( void );	// worker side, right after OpenDecoder
	const char *GetTag( const char *pKey ) const;
	bool HasID3Tag( void ) const { return m_bHasID3Tag; }
	int GetID3Genre( void ) const { return m_nID3Genre; }

private:
	int GetStateRaw( void );
	void AppendFFTFrame( float flLeft, float flRight );

	// --- worker-only decoder state --------------------------------------
	FileHandle_t m_hFile;
	void *m_Decoder;
	bool m_bDecoderOpen;
	CThreadFastMutex m_DecoderMutex;	// decoder touched by worker AND main-thread restarts
	uint64 m_nDecodedFrames;			// frames decoded so far (worker view)
	uint64 m_nTotalFrames;				// stream length if known (0 = unknown)

	// --- sample storage ---------------------------------------------------
	float *m_pRing;						// streaming: RING_FRAMES * channels f32
	float *m_pBlock;					// noblock: fully decoded f32
	std::atomic<uint32> m_nRingWrite;	// frames written into the ring (worker, monotonic)

	// --- shared playback state -------------------------------------------
	int32 volatile m_nState;			// HL2SBChannelState_e, interlocked transitions
	int32 volatile m_nCursor;			// frames consumed (mix thread adds; SetTime/Stop/Play set)
	std::atomic<float> m_flVolume;
	std::atomic<float> m_flPan;
	std::atomic<float> m_flRate;
	std::atomic<bool> m_bLooping;
	std::atomic<bool> m_bEOF;
	Vector m_vecPos;
	Vector m_vecDirection;
	CThreadFastMutex m_PosMutex;
	std::atomic<bool> m_bPendingRemoval;	// Stop() marks the channel dead; the mix
										// thread drops it from the list (binary
										// contract: Stop invalidates immediately)

	// 3D attributes (distance/pan applied; cone stored only, documented gap)
	std::atomic<float> m_flFadeMin;
	std::atomic<float> m_flFadeMax;
	std::atomic<float> m_flConeInner;
	std::atomic<float> m_flConeOuter;
	std::atomic<float> m_flConeOuterVol;
	bool m_b3D;							// created with the "3d" flag (Is3D)
	std::atomic<bool> m_b3DEnabled;

	// level meters (mix thread writes the peaks it just consumed)
	std::atomic<float> m_flPeakL;
	std::atomic<float> m_flPeakR;

	// playback-rate fractional accumulator (mix thread only)
	double m_flRateAcc;

	// FFT history (allocated lazily on first FFT call; consumed-sample ring)
	float *m_pFFTHistory;
	CThreadFastMutex m_FFTMutex;
	uint32 m_nFFTSize;
	uint32 m_nFFTHead;
	uint32 m_nFFTCount;
	bool m_bFFTTrack;

	// --- identity ----------------------------------------------------------
	char m_szPath[ MAX_PATH ];
	char m_szTags[ 512 ];				// ID3v1 fields: "key\0value\0..." , empty key ends
	bool m_bHasID3Tag;
	int m_nID3Genre;
	int m_nLoadError;					// 0 = none
	int m_nCallbackRef;
	bool m_bMono;
	bool m_bNoPlay;
	bool m_bNoBlock;
	int m_nChannels;					// decoded channel count (1 or 2)
	std::atomic<bool> m_bLoadQueued;
	int32 volatile m_nRefCount;
};

//=============================================================================
// Manager state
//=============================================================================

struct QueuedCallback_t
{
	CLuaAudioChannel *pChannel;	// NULL = failure result
	int nCallbackRef;			// lua registry ref of the PlayFile callback
	int nErrorID;				// 0 = none
	char szErrorName[ 32 ];
};

static CThreadFastMutex g_ChannelListMutex;
static CUtlVector< CLuaAudioChannel * > g_Channels;	// membership holds a reference
static CUtlVector< CLuaAudioChannel * > g_RetiredChannels;

static CThreadFastMutex g_QueueMutex;
static CUtlVector< QueuedCallback_t > g_Queue;

static ILuaStreamMix *g_pStreamMix = NULL;
static ILuaAudioDecoder *g_pAudioDecoder = NULL;
static CSysModule *g_pAudioDecoderModule = NULL;
static bool g_bAudioConnected = false;
static bool g_bMixRegistered = false;

static std::atomic<bool> g_bWorkerKill( false );
static ThreadHandle_t g_hWorker = NULL;

// 3D listener, refreshed by the pump on the main thread, read by the mix
// thread. Guarded by its own mutex, held only for the copies.
static CThreadFastMutex g_ListenerMutex;
static Vector g_vecListener( vec3_origin );
static QAngle g_angListener( vec3_angle );

//=============================================================================
// CLuaAudioChannel implementation
//=============================================================================

CLuaAudioChannel::CLuaAudioChannel()
  : m_bDecoderOpen( false ),
	m_hFile( FILESYSTEM_INVALID_HANDLE ),
	m_Decoder( NULL ),
	m_nDecodedFrames( 0 ),
	m_nTotalFrames( 0 ),
	m_pRing( NULL ),
	m_pBlock( NULL ),
	m_nRingWrite( 0 ),
	m_nState( CHANSTATE_LOADING ),
	m_nCursor( 0 ),
	m_flVolume( 1.0f ),
	m_flPan( 0.0f ),
	m_flRate( 1.0f ),
	m_bLooping( false ),
	m_bEOF( false ),
	m_vecPos( vec3_origin ),
	m_vecDirection( vec3_origin ),
	m_bPendingRemoval( false ),
	m_flFadeMin( AUDIO_FADEMIN_DEFAULT ),
	m_flFadeMax( AUDIO_FADEMAX_DEFAULT ),
	m_flConeInner( 360.0f ),
	m_flConeOuter( 360.0f ),
	m_flConeOuterVol( 1.0f ),
	m_b3D( false ),
	m_b3DEnabled( true ),
	m_flPeakL( 0.0f ),
	m_flPeakR( 0.0f ),
	m_flRateAcc( 0.0 ),
	m_pFFTHistory( NULL ),
	m_nFFTSize( 0 ),
	m_nFFTHead( 0 ),
	m_nFFTCount( 0 ),
	m_bFFTTrack( false ),
	m_bHasID3Tag( false ),
	m_nID3Genre( 0 ),
	m_nLoadError( 0 ),
	m_nCallbackRef( LUA_NOREF ),
	m_bMono( false ),
	m_bNoPlay( false ),
	m_bNoBlock( false ),
	m_nChannels( 2 ),
	m_bLoadQueued( false ),
	m_nRefCount( 1 )
{
	m_szPath[ 0 ] = 0;
	m_szTags[ 0 ] = 0;
}

CLuaAudioChannel::~CLuaAudioChannel()
{
	CloseDecoder();
	free( m_pRing );
	free( m_pBlock );
	free( m_pFFTHistory );
}

void CLuaAudioChannel::CloseDecoder( void )
{
	if ( m_bDecoderOpen && g_pAudioDecoder )
		g_pAudioDecoder->Close( m_Decoder );
	m_Decoder = NULL;
	m_bDecoderOpen = false;
	if ( m_hFile != FILESYSTEM_INVALID_HANDLE )
	{
		g_pFullFileSystem->Close( m_hFile );
		m_hFile = FILESYSTEM_INVALID_HANDLE;
	}
}

void CLuaAudioChannel::ReleaseRef( void )
{
	if ( ThreadInterlockedDecrement( &m_nRefCount ) <= 0 )
		delete this;
}

void CLuaAudioChannel::InitLoad( const char *pPath, bool b3D, bool bMono, bool bNoPlay, bool bNoBlock )
{
	Q_strncpy( m_szPath, pPath, sizeof( m_szPath ) );
	m_b3D = b3D;
	m_bMono = bMono || b3D;
	m_bNoPlay = bNoPlay;
	m_bNoBlock = bNoBlock;
}

void CLuaAudioChannel::CancelPendingLoad( lua_State *pLua )
{
	if ( m_nCallbackRef == LUA_NOREF )
		return;
	if ( pLua )
		luaL_unref( pLua, LUA_REGISTRYINDEX, m_nCallbackRef );
	m_nCallbackRef = LUA_NOREF;
	ReleaseRef();
}

int CLuaAudioChannel::GetStateRaw( void )
{
	return ThreadInterlockedCompareExchange( &m_nState, 0, 0 );
}

//=============================================================================
// VFS IO callbacks for miniaudio (worker thread only)
//=============================================================================

static size_t AudioFileRead( void *pUser, void *pBuffer, size_t nBytes )
{
	FileHandle_t hFile = ( FileHandle_t )pUser;
	if ( hFile == FILESYSTEM_INVALID_HANDLE || nBytes == 0 )
		return 0;

	int nRead = g_pFullFileSystem->Read( pBuffer, ( int )nBytes, hFile );
	return nRead > 0 ? ( size_t )nRead : 0;
}

static bool AudioFileSeek( void *pUser, int64_t nOffset, bool relative )
{
	FileHandle_t hFile = ( FileHandle_t )pUser;
	if ( hFile == FILESYSTEM_INVALID_HANDLE || nOffset < INT_MIN || nOffset > INT_MAX )
		return false;

	FileSystemSeek_t eSeek = relative ? FILESYSTEM_SEEK_CURRENT : FILESYSTEM_SEEK_HEAD;
	g_pFullFileSystem->Seek( hFile, ( int )nOffset, eSeek );
	return true;
}

static uint32 AudioReadLE32( const unsigned char *p )
{
	return ( uint32 )p[0] | ( ( uint32 )p[1] << 8 ) | ( ( uint32 )p[2] << 16 ) | ( ( uint32 )p[3] << 24 );
}

// A seekable, single-stream Vorbis file exposes its final PCM position in its
// last Ogg page. Preserve the decoder's underlying file position while reading.
static uint64 AudioVorbisLengthFrames( FileHandle_t file )
{
	const unsigned int size = g_pFullFileSystem->Size( file );
	const unsigned int previous = g_pFullFileSystem->Tell( file );
	if ( size < 58 || size > INT_MAX || previous > INT_MAX )
		return 0;

	unsigned char first[58];
	g_pFullFileSystem->Seek( file, 0, FILESYSTEM_SEEK_HEAD );
	int count = g_pFullFileSystem->Read( first, sizeof( first ), file );
	if ( count != sizeof( first ) || memcmp( first, "OggS", 4 ) != 0 || first[4] != 0 ||
		 !( first[5] & 2 ) || first[26] != 1 || first[27] != 30 ||
		 first[28] != 1 || memcmp( first + 29, "vorbis", 6 ) != 0 )
	{
		g_pFullFileSystem->Seek( file, ( int )previous, FILESYSTEM_SEEK_HEAD );
		return 0;
	}

	const uint32 serial = AudioReadLE32( first + 14 );
	const uint32 sampleRate = AudioReadLE32( first + 40 );
	const unsigned int tailSize = size < 65536 ? size : 65536;
	unsigned char *tail = ( unsigned char * )malloc( tailSize );
	uint64 frames = 0;
	if ( tail && sampleRate != 0 )
	{
		g_pFullFileSystem->Seek( file, ( int )( size - tailSize ), FILESYSTEM_SEEK_HEAD );
		if ( g_pFullFileSystem->Read( tail, tailSize, file ) == ( int )tailSize )
		{
			for ( int i = ( int )tailSize - 27; i >= 0; --i )
			{
				if ( memcmp( tail + i, "OggS", 4 ) != 0 || tail[i + 4] != 0 ||
					 !( tail[i + 5] & 4 ) || AudioReadLE32( tail + i + 14 ) != serial )
					continue;
				unsigned int segments = tail[i + 26];
				if ( ( unsigned int )i + 27 + segments > tailSize ) continue;
				unsigned int pageSize = 27 + segments;
				for ( unsigned int j = 0; j < segments; ++j ) pageSize += tail[i + 27 + j];
				if ( ( unsigned int )i + pageSize != tailSize ) continue;
				uint64 granule = ( uint64 )AudioReadLE32( tail + i + 6 ) |
					( ( uint64 )AudioReadLE32( tail + i + 10 ) << 32 );
				if ( granule <= INT64_MAX / AUDIO_RATE )
					frames = ( granule * AUDIO_RATE + sampleRate / 2 ) / sampleRate;
				break;
			}
		}
	}
	free( tail );
	g_pFullFileSystem->Seek( file, ( int )previous, FILESYSTEM_SEEK_HEAD );
	return frames;
}

//=============================================================================
// Load path
//=============================================================================

bool CLuaAudioChannel::OpenDecoder( void )
{
	m_nChannels = m_bMono ? 1 : 2;

	if ( m_szPath[0] == ' ' || m_szPath[0] == '/' || strstr( m_szPath, ".." ) ||
		 strstr( m_szPath, "./" ) || strstr( m_szPath, ":" ) )
	{
		m_nLoadError = BASS_ERROR_FILEOPEN;
		return false;
	}

	m_hFile = g_pFullFileSystem->Open( m_szPath, "rb", "GAME" );
	if ( m_hFile == FILESYSTEM_INVALID_HANDLE )
	{
		m_nLoadError = BASS_ERROR_FILEOPEN;
		return false;
	}

	HL2SBAudioDecodeResult result;
	m_Decoder = g_pAudioDecoder->Open( ( void * )m_hFile, AudioFileRead, AudioFileSeek,
		( unsigned int )m_nChannels, AUDIO_RATE, &result );
	if ( !m_Decoder )
	{
		m_nLoadError = ( result == HL2SB_AUDIO_DECODE_MEMORY ) ? BASS_ERROR_MEM : BASS_ERROR_FILEFORM;
		g_pFullFileSystem->Close( m_hFile );
		m_hFile = FILESYSTEM_INVALID_HANDLE;
		return false;
	}

	m_bDecoderOpen = true;

	uint64_t nLength = 0;
	if ( g_pAudioDecoder->Length( m_Decoder, &nLength ) && nLength > 0 )
		m_nTotalFrames = nLength;
	else
		m_nTotalFrames = AudioVorbisLengthFrames( m_hFile );

	if ( m_bNoBlock )
	{
		uint64 nMaxFrames = NOBLOCK_MAX_BYTES / ( sizeof( float ) * ( uint64 )m_nChannels );
		if ( m_nTotalFrames > nMaxFrames )
		{
			// Known to exceed the memory guard: fail now rather than decode.
			m_nLoadError = BASS_ERROR_MEM;
			return false;
		}
		uint64 nReserve = ( m_nTotalFrames > 0 ) ? m_nTotalFrames : 65536;
		m_pBlock = ( float * )malloc( ( size_t )( nReserve * sizeof( float ) * ( uint64 )m_nChannels ) );
		if ( !m_pBlock )
		{
			m_nLoadError = BASS_ERROR_MEM;
			return false;
		}
		m_nTotalFrames = nReserve;	// grows while decoding
	}
	else
	{
		m_pRing = ( float * )malloc( ( size_t )( ( uint64 )RING_FRAMES * sizeof( float ) * ( uint64 )m_nChannels ) );
		if ( !m_pRing )
		{
			m_nLoadError = BASS_ERROR_MEM;
			return false;
		}
		memset( m_pRing, 0, ( size_t )( ( uint64 )RING_FRAMES * sizeof( float ) * ( uint64 )m_nChannels ) );
	}

	// The reference backend reads ID3v1 tags for mp3 files; do it now while
	// the file cursor is still at the head (we rewind afterwards).
	LoadID3Tag();
	return true;
}

bool CLuaAudioChannel::NeedsLoadWork( void )
{
	return !m_bDecoderOpen && m_nLoadError == 0 && !m_bLoadQueued && GetStateRaw() == CHANSTATE_LOADING;
}

bool CLuaAudioChannel::NeedsDecodeWork( void )
{
	if ( m_nLoadError != 0 || m_bEOF || !m_bDecoderOpen )
		return false;

	int nState = GetStateRaw();
	if ( nState == CHANSTATE_STOPPED || nState == CHANSTATE_PAUSED )
		return false;

	if ( m_bNoBlock )
		return m_pBlock != NULL;

	uint32 nWrite = m_nRingWrite.load();
	uint32 nRead = ( uint32 )ThreadInterlockedCompareExchange( &m_nCursor, 0, 0 );
	uint32 nBuffered = nWrite - nRead;
	return nBuffered < RING_FRAMES - ( uint32 )DECODE_CHUNK_FRAMES;
}

// Worker side: queue the load result once the contract trigger is hit.
bool CLuaAudioChannel::CheckLoadProgress( void )
{
	if ( m_bLoadQueued )
		return false;

	if ( m_nLoadError != 0 )
	{
		QueueLoadResult();
		return true;
	}

	if ( m_bNoBlock )
	{
		if ( m_bEOF )
		{
			QueueLoadResult();
			return true;
		}
		return false;
	}

	if ( m_bEOF || ( int32 )m_nRingWrite >= ( int32 )STREAM_PRIME_FRAMES )
	{
		QueueLoadResult();
		return true;
	}
	return false;
}

void CLuaAudioChannel::QueueLoadResult( void )
{
	m_bLoadQueued = true;

	QueuedCallback_t entry;
	entry.nCallbackRef = m_nCallbackRef;
	m_nCallbackRef = LUA_NOREF;
	entry.nErrorID = m_nLoadError;
	Q_strncpy( entry.szErrorName, m_nLoadError != 0 ? BassErrorName( m_nLoadError ) : "", sizeof( entry.szErrorName ) );
	entry.pChannel = this;	// transfer the load-task reference to the queue
	if ( m_nLoadError == 0 )
		ThreadInterlockedExchange( &m_nState, m_bNoPlay ? CHANSTATE_STOPPED : CHANSTATE_PLAYING );
	else
		m_bPendingRemoval = true;

	g_QueueMutex.Lock();
	g_Queue.AddToTail( entry );
	g_QueueMutex.Unlock();
}

//=============================================================================
// Decode (worker thread)
//=============================================================================

void CLuaAudioChannel::DecodeChunk( void )
{
	if ( m_bEOF || m_nLoadError != 0 || !m_bDecoderOpen )
		return;

	AUTO_LOCK( m_DecoderMutex );

	uint64_t nRead = 0;

	if ( m_bNoBlock )
	{
		uint64 nMaxFrames = NOBLOCK_MAX_BYTES / ( sizeof( float ) * ( uint64 )m_nChannels );
		if ( m_nDecodedFrames >= nMaxFrames )
		{
			m_nLoadError = BASS_ERROR_MEM;
			return;
		}

		// grow the block as needed
		uint64 nNeed = m_nDecodedFrames + DECODE_CHUNK_FRAMES;
		if ( nNeed > m_nTotalFrames )
		{
			uint64 nNew = m_nTotalFrames > 0 ? m_nTotalFrames * 2 : 65536;
			if ( nNew > nMaxFrames )
				nNew = nMaxFrames;
			if ( nNeed > nNew )
				nNew = nNeed;
			float *pNew = ( float * )realloc( m_pBlock, ( size_t )( nNew * sizeof( float ) * ( uint64 )m_nChannels ) );
			if ( !pNew )
			{
				m_nLoadError = BASS_ERROR_MEM;
				return;
			}
			m_pBlock = pNew;
			m_nTotalFrames = nNew;
		}

		float *pDst = m_pBlock + ( size_t )( m_nDecodedFrames * ( uint64 )m_nChannels );
		HL2SBAudioDecodeResult result = g_pAudioDecoder->Read( m_Decoder, pDst, DECODE_CHUNK_FRAMES, &nRead );
		if ( result == HL2SB_AUDIO_DECODE_ERROR && nRead == 0 )
		{
			// decoder error counts as a format failure
			m_nLoadError = BASS_ERROR_FILEFORM;
			return;
		}

		m_nDecodedFrames += nRead;
		if ( nRead < DECODE_CHUNK_FRAMES )
		{
			if ( m_nDecodedFrames == 0 )
			{
				// zero-length decode: unrecognized format
				m_nLoadError = BASS_ERROR_FILEFORM;
				return;
			}
			// decoded through the end: shrink the block to its final size
			float *pFinal = ( float * )realloc( m_pBlock, ( size_t )( m_nDecodedFrames * sizeof( float ) * ( uint64 )m_nChannels ) );
			if ( pFinal )
				m_pBlock = pFinal;
			m_nTotalFrames = m_nDecodedFrames;
			m_bEOF = true;
		}
		return;
	}

	// streaming: fill the ring. The producer must not overwrite unconsumed
	// data, so the buffered window limits how much is decoded.
	uint32 nWrite = m_nRingWrite.load();
	uint32 nReadPos = ( uint32 )ThreadInterlockedCompareExchange( &m_nCursor, 0, 0 );
	uint32 nBuffered = nWrite - nReadPos;
	if ( nBuffered > RING_FRAMES - ( uint32 )DECODE_CHUNK_FRAMES )
		return;

	// decode into a static scratch (single worker thread owns it), then copy
	// in rim-crossing halves
	static float s_pScratch[ DECODE_CHUNK_FRAMES * 2 ];
	HL2SBAudioDecodeResult result = g_pAudioDecoder->Read( m_Decoder, s_pScratch, DECODE_CHUNK_FRAMES, &nRead );
	if ( result == HL2SB_AUDIO_DECODE_ERROR && nRead == 0 )
	{
		m_nLoadError = BASS_ERROR_FILEFORM;
		return;
	}

	uint64 nBase = m_nRingWrite % RING_FRAMES;
	uint64 nFirst = RING_FRAMES - nBase;
	if ( nFirst > nRead )
		nFirst = nRead;

	memcpy( m_pRing + ( size_t )( nBase * ( uint64 )m_nChannels ), s_pScratch,
			( size_t )( nFirst * sizeof( float ) * ( uint64 )m_nChannels ) );
	if ( nRead > nFirst )
	{
		memcpy( m_pRing, s_pScratch + ( size_t )( nFirst * ( uint64 )m_nChannels ),
				( size_t )( ( nRead - nFirst ) * sizeof( float ) * ( uint64 )m_nChannels ) );
	}

	m_nRingWrite += ( uint32 )nRead;
	m_nDecodedFrames += nRead;

	if ( nRead < DECODE_CHUNK_FRAMES )
		m_bEOF = true;
}

//=============================================================================
// ID3v1 (reference contract: ID3 version 1 only, no v2)
//=============================================================================

bool CLuaAudioChannel::LoadID3Tag( void )
{
	if ( m_hFile == FILESYSTEM_INVALID_HANDLE )
		return false;

	int nSize = g_pFullFileSystem->Size( m_hFile );
	if ( nSize < 128 )
		return false;

	unsigned int nOldPosition = g_pFullFileSystem->Tell( m_hFile );
	g_pFullFileSystem->Seek( m_hFile, -128, FILESYSTEM_SEEK_TAIL );
	char buf[ 128 ];
	int nRead = g_pFullFileSystem->Read( buf, 128, m_hFile );
	g_pFullFileSystem->Seek( m_hFile, ( int )nOldPosition, FILESYSTEM_SEEK_HEAD );

	if ( nRead != 128 || Q_strncmp( buf, "TAG", 3 ) != 0 )
		return false;
	m_bHasID3Tag = true;
	m_nID3Genre = ( unsigned char )buf[ 127 ];

	static const char *pKeys[ 5 ] = { "title", "artist", "album", "year", "comment" };
	static const int nLens[ 5 ] = { 30, 30, 30, 4, 30 };

	char *pOut = m_szTags;
	char *pEnd = m_szTags + sizeof( m_szTags ) - 2;
	const char *pSrc = buf + 3;
	for ( int i = 0; i < 5 && pOut < pEnd; ++i )
	{
		char szValue[ 32 ];
		int nLen = nLens[ i ];
		Q_strncpy( szValue, pSrc, nLen + 1 );
		szValue[ nLen ] = 0;
		pSrc += nLen;

		char *pTrim = szValue + Q_strlen( szValue );
		while ( pTrim > szValue && pTrim[ -1 ] == ' ' )
			*--pTrim = 0;

		if ( szValue[ 0 ] == 0 )
			continue;

		size_t nKeyLen = Q_strlen( pKeys[ i ] );
		size_t nValLen = Q_strlen( szValue );
		if ( ( size_t )( pEnd - pOut ) < nKeyLen + 1 + nValLen + 1 )
			break;

		Q_strncpy( pOut, pKeys[ i ], nKeyLen + 1 );
		pOut += nKeyLen + 1;
		Q_strncpy( pOut, szValue, nValLen + 1 );
		pOut += nValLen + 1;
	}
	*pOut = 0;
	return true;
}

const char *CLuaAudioChannel::GetTag( const char *pKey ) const
{
	const char *p = m_szTags;
	while ( *p )
	{
		if ( Q_stricmp( p, pKey ) == 0 )
			return p + Q_strlen( p ) + 1;
		p += Q_strlen( p ) + 1;	// key
		p += Q_strlen( p ) + 1;	// value
	}
	return NULL;
}

//=============================================================================
// Playback control (main thread)
//=============================================================================

void CLuaAudioChannel::Play( void )
{
	int nState = GetStateRaw();
	if ( nState == CHANSTATE_LOADING || nState == CHANSTATE_PLAYING )
		return;

	if ( nState == CHANSTATE_STOPPED )
	{
		ThreadInterlockedExchange( &m_nCursor, 0 );
		if ( !m_bNoBlock && m_bDecoderOpen )
		{
			AUTO_LOCK( m_DecoderMutex );
			m_bEOF = false;
			m_nRingWrite = 0;
			g_pAudioDecoder->Seek( m_Decoder, 0 );
			m_nDecodedFrames = 0;
		}
	}

	ThreadInterlockedCompareExchange( &m_nState, CHANSTATE_PLAYING, nState );
}

void CLuaAudioChannel::Pause( void )
{
	ThreadInterlockedCompareExchange( &m_nState, CHANSTATE_PAUSED, CHANSTATE_PLAYING );
}

void CLuaAudioChannel::Stop( void )
{
	int nState = GetStateRaw();
	if ( nState == CHANSTATE_LOADING )
	{
		m_bPendingRemoval = true;
		return;
	}

	// Stop invalidates the Lua userdata and removes the channel from mixing.
	ThreadInterlockedCompareExchange( &m_nState, CHANSTATE_STOPPED, nState );
	ThreadInterlockedExchange( &m_nCursor, 0 );
	m_bPendingRemoval = true;
}

void CLuaAudioChannel::SetPlaybackRate( float flRate )
{
	// Reference backend maps the rate onto the BASS frequency attribute
	// (rate * 44100 Hz, accepted range 1..100000); out-of-range sets are
	// rejected silently and the previous rate stays readable.
	float flFreq = flRate * ( float )AUDIO_RATE;
	if ( !std::isfinite( flFreq ) || flFreq < 1.0f || flFreq > 100000.0f )
		return;
	m_flRate = flRate;
}

bool CLuaAudioChannel::EnableLooping( bool bEnable )
{
	// Reference contract: requires noblock. Without it this is a no-op.
	if ( !m_bNoBlock )
		return false;
	m_bLooping = bEnable;
	return true;
}

bool CLuaAudioChannel::SetTime( double flSecs )
{
	// Reference contract: only noblock channels can seek.
	// (dont_decode has no decode cost here: the block is already in memory.)
	if ( !m_bNoBlock || m_pBlock == NULL || m_nTotalFrames == 0 || !std::isfinite( flSecs ) )
		return false;

	double frame = flSecs * ( double )AUDIO_RATE;
	if ( frame < 0.0 ) frame = 0.0;
	if ( frame > ( double )m_nTotalFrames ) frame = ( double )m_nTotalFrames;
	int64 nFrame = ( int64 )frame;

	// The mix thread consumes the cursor with interlocked adds; publish the
	// new position the same way so one of the two writers always wins
	// atomically.
	ThreadInterlockedExchange( &m_nCursor, ( int32 )nFrame );
	return true;
}

void CLuaAudioChannel::Set3DEnabled( bool bEnable )
{
	// Reference contract: only channels created with the "3d" flag accept
	// this; anything else is a no-op.
	if ( m_b3D )
		m_b3DEnabled = bEnable;
}

void CLuaAudioChannel::Set3DFadeDistance( float flMin, float flMax )
{
	// Reference contract: zero or negative keeps the current value.
	if ( flMin > 0.0f )
		m_flFadeMin = flMin;
	if ( flMax > 0.0f )
		m_flFadeMax = flMax;
}

void CLuaAudioChannel::Set3DCone( float flInner, float flOuter, float flOuterVolume )
{
	// Reference contract: negative angles and outerVolume below zero keep the
	// current value. The cone is stored but not applied (documented gap).
	if ( flInner >= 0.0f && flInner <= 360.0f )
		m_flConeInner = flInner;
	if ( flOuter >= 0.0f && flOuter <= 360.0f )
		m_flConeOuter = flOuter;
	if ( flOuterVolume >= 0.0f && flOuterVolume <= 1.0f )
		m_flConeOuterVol = flOuterVolume;
}

int CLuaAudioChannel::GetState( void )
{
	int nState = GetStateRaw();
	if ( nState == CHANSTATE_LOADING )
		return CHANSTATE_STOPPED;
	return nState;
}

float CLuaAudioChannel::GetTime( void )
{
	int32 nCursor = ThreadInterlockedCompareExchange( &m_nCursor, 0, 0 );
	if ( m_bNoBlock && m_bLooping && m_nTotalFrames > 0 )
		return ( float )( ( double )( ( uint32 )nCursor % m_nTotalFrames ) / ( double )AUDIO_RATE );
	return ( float )( ( double )( uint32 )nCursor / ( double )AUDIO_RATE );
}

float CLuaAudioChannel::GetLength( void ) const
{
	if ( m_nTotalFrames > 0 )
		return ( float )( ( double )m_nTotalFrames / ( double )AUDIO_RATE );
	// Unknown length (and continuous streams in the reference) report <= 0;
	// ours reports 0 for unknown.
	return 0.0f;
}

float CLuaAudioChannel::GetBufferedTime( void )
{
	return GetLength();
}

void CLuaAudioChannel::GetLevel( float &flLeft, float &flRight ) const
{
	flLeft = m_flPeakL;
	flRight = m_flPeakR;
}

//=============================================================================
// FFT (Hann window, half spectrum; reference contract)
//=============================================================================

int CLuaAudioChannel::FFT( float *pBins, int nBins )
{
	if ( nBins <= 0 || !pBins )
		return 0;

	uint32 nNeed = ( uint32 )nBins * 2;
	if ( nNeed > RING_FRAMES )
		nNeed = RING_FRAMES;
	float *pReal = ( float * )malloc( sizeof( float ) * nNeed );
	float *pImag = ( float * )malloc( sizeof( float ) * nNeed );
	if ( !pReal || !pImag )
	{
		free( pReal );
		free( pImag );
		return 0;
	}

	m_FFTMutex.Lock();
	if ( !m_bFFTTrack )
	{
		m_nFFTSize = RING_FRAMES;
		m_pFFTHistory = ( float * )malloc( ( size_t )( ( uint64 )m_nFFTSize * sizeof( float ) * ( uint64 )m_nChannels ) );
		if ( !m_pFFTHistory )
		{
			m_FFTMutex.Unlock();
			free( pReal );
			free( pImag );
			return 0;
		}
		memset( m_pFFTHistory, 0, ( size_t )( ( uint64 )m_nFFTSize * sizeof( float ) * ( uint64 )m_nChannels ) );
		m_nFFTHead = 0;
		m_nFFTCount = 0;
		m_bFFTTrack = true;
	}

	// read-only snapshot of the consumed-sample history
	uint32 nAvail = m_nFFTCount < nNeed ? m_nFFTCount : nNeed;
	for ( uint32 i = 0; i < nNeed; ++i )
	{
		float flSample = 0.0f;
		if ( i < nAvail )
		{
			uint32 nIdx = ( m_nFFTHead + m_nFFTSize - nAvail + i ) % m_nFFTSize;
			flSample = m_pFFTHistory[ nIdx * m_nChannels ];
			if ( m_nChannels == 2 )
				flSample = ( flSample + m_pFFTHistory[ nIdx * m_nChannels + 1 ] ) * 0.5f;
		}
		float flWindow = 0.5f * ( 1.0f - cosf( 2.0f * M_PI_F * ( float )i / ( float )nNeed ) );
		pReal[ i ] = flSample * flWindow;
		pImag[ i ] = 0.0f;
	}
	m_FFTMutex.Unlock();

	// in-place radix-2 FFT
	for ( uint32 i = 1, j = 0; i < nNeed; ++i )
	{
		uint32 nBit = nNeed >> 1;
		for ( ; j & nBit; nBit >>= 1 )
			j ^= nBit;
		j ^= nBit;
		if ( i < j )
		{
			float flT = pReal[ i ]; pReal[ i ] = pReal[ j ]; pReal[ j ] = flT;
			flT = pImag[ i ]; pImag[ i ] = pImag[ j ]; pImag[ j ] = flT;
		}
	}
	for ( uint32 nLen = 2; nLen <= nNeed; nLen <<= 1 )
	{
		float flAng = -2.0f * M_PI_F / ( float )nLen;
		float flWr = cosf( flAng ), flWi = sinf( flAng );
		for ( uint32 i = 0; i < nNeed; i += nLen )
		{
			float flCr = 1.0f, flCi = 0.0f;
			for ( uint32 k = 0; k < nLen / 2; ++k )
			{
				uint32 nA = i + k, nB = i + k + nLen / 2;
				float flUr = pReal[ nA ], flUi = pImag[ nA ];
				float flVr = pReal[ nB ] * flCr - pImag[ nB ] * flCi;
				float flVi = pReal[ nB ] * flCi + pImag[ nB ] * flCr;
				pReal[ nA ] = flUr + flVr;
				pImag[ nA ] = flUi + flVi;
				pReal[ nB ] = flUr - flVr;
				pImag[ nB ] = flUi - flVi;
				float flNewCr = flCr * flWr - flCi * flWi;
				flCi = flCr * flWi + flCi * flWr;
				flCr = flNewCr;
			}
		}
	}

	// reference contract: half spectrum, starting at bin 1
	int nFilled = ( int )( nNeed / 2 );
	int nOut = nBins < nFilled ? nBins : nFilled;
	for ( int i = 0; i < nOut; ++i )
	{
		float flRe = pReal[ i ];
		float flIm = pImag[ i ];
		pBins[ i ] = sqrtf( flRe * flRe + flIm * flIm ) / ( float )( nNeed / 2 );
	}

	free( pReal );
	free( pImag );
	return nOut;
}

//=============================================================================
// Mix consumer (engine mix thread)
//=============================================================================

void CLuaAudioChannel::AppendFFTFrame( float flLeft, float flRight )
{
	float *pDst = m_pFFTHistory + ( size_t )( ( uint64 )m_nFFTHead * ( uint64 )m_nChannels );
	pDst[ 0 ] = flLeft;
	if ( m_nChannels == 2 )
		pDst[ 1 ] = flRight;
	m_nFFTHead = ( m_nFFTHead + 1 ) % m_nFFTSize;
	if ( m_nFFTCount < m_nFFTSize )
		m_nFFTCount++;
}

// Reference-style 3D: full volume inside the fade-in distance, linear inverse
// falloff past it, silent at the fade-out distance; pan from the listener
// right vector.
static void Audio3DGains( const Vector &vecSource, const Vector &vecListener, const QAngle &angListener,
						  float flFadeMin, float flFadeMax, float &flDistGain, float &fl3DPan )
{
	Vector vecDelta = vecSource - vecListener;
	float flDist = vecDelta.Length();

	Vector vecForward, vecRight, vecUp;
	AngleVectors( angListener, &vecForward, &vecRight, &vecUp );

	fl3DPan = 0.0f;
	if ( flDist > 0.01f )
	{
		Vector vecDir = vecDelta * ( 1.0f / flDist );
		fl3DPan = DotProduct( vecDir, vecRight );
		if ( fl3DPan > 1.0f ) fl3DPan = 1.0f;
		if ( fl3DPan < -1.0f ) fl3DPan = -1.0f;
	}

	if ( flDist <= flFadeMin )
		flDistGain = 1.0f;
	else
	{
		float flEffectiveDist = flDist < flFadeMax ? flDist : flFadeMax;
		flDistGain = flFadeMin / flEffectiveDist;
	}
}

bool CLuaAudioChannel::MixInto( float *pMix, int nFrames, const Vector &vecListener, const QAngle &angListener )
{
	// Drop channels invalidated by Stop or collection.
	if ( m_bPendingRemoval )
		return false;

	int nState = GetStateRaw();
	if ( nState != CHANSTATE_PLAYING )
	{
		m_flPeakL = 0.0f;
		m_flPeakR = 0.0f;
		return true;
	}

	float flRate = m_flRate;
	float flVolume = m_flVolume;
	float flPan = m_flPan;
	float flGainL = flVolume, flGainR = flVolume;

	Vector vecPos = GetPos();
	float fl3DPan = 0.0f;
	if ( m_b3D && m_b3DEnabled )
	{
		float flDistGain = 1.0f;
		Audio3DGains( vecPos, vecListener, angListener, m_flFadeMin, m_flFadeMax, flDistGain, fl3DPan );
		flGainL = flVolume * flDistGain * ( 1.0f - 0.5f * ( fl3DPan + flPan ) );
		flGainR = flVolume * flDistGain * ( 1.0f - 0.5f * ( flPan - fl3DPan ) );
	}
	else if ( flPan != 0.0f )
	{
		flGainL = flVolume * ( flPan <= 0.0f ? 1.0f : 1.0f - flPan );
		flGainR = flVolume * ( flPan >= 0.0f ? 1.0f : 1.0f + flPan );
	}

	float flPeakL = 0.0f, flPeakR = 0.0f;
	bool bEnded = false;

	//---------------------------------------------------------
	// noblock: linear-interpolated read from the decoded block
	//---------------------------------------------------------
	if ( m_bNoBlock )
	{
		uint64 nTotal = m_nTotalFrames;
		if ( nTotal == 0 )
			return true;

		int32 nCursor = ThreadInterlockedCompareExchange( &m_nCursor, 0, 0 );
		double flPos = ( double )( uint32 )nCursor + m_flRateAcc;

		m_FFTMutex.Lock();
		for ( int i = 0; i < nFrames; ++i )
		{
			if ( m_bLooping )
			{
				while ( flPos >= ( double )nTotal )
					flPos -= ( double )nTotal;
			}
			else if ( flPos >= ( double )nTotal - 1.0 )
			{
				bEnded = true;
				break;
			}

			uint64 nPos = ( uint64 )flPos;
			double flFrac = flPos - ( double )nPos;
			float flL0 = m_pBlock[ nPos * ( uint64 )m_nChannels ];
			float flR0 = ( m_nChannels == 2 ) ? m_pBlock[ nPos * ( uint64 )m_nChannels + 1 ] : flL0;
			uint64 nNext = nPos + 1;
			if ( m_bLooping && nNext >= nTotal )
				nNext = 0;
			else if ( nNext >= nTotal )
				nNext = nPos;
			float flL1 = m_pBlock[ nNext * ( uint64 )m_nChannels ];
			float flR1 = ( m_nChannels == 2 ) ? m_pBlock[ nNext * ( uint64 )m_nChannels + 1 ] : flL1;

			float flL = ( float )( flL0 + ( flL1 - flL0 ) * flFrac );
			float flR = ( float )( flR0 + ( flR1 - flR0 ) * flFrac );
			if ( m_bFFTTrack ) AppendFFTFrame( flL, flR );
			pMix[ i * 2 ] += flL * flGainL;
			pMix[ i * 2 + 1 ] += flR * flGainR;
			if ( fabsf( flL ) > flPeakL ) flPeakL = fabsf( flL );
			if ( fabsf( flR ) > flPeakR ) flPeakR = fabsf( flR );

			flPos += flRate;
		}
		m_FFTMutex.Unlock();

		// advance the time cursor by the exact fractional frame count
		double flAdvance = m_flRateAcc + ( double )nFrames * flRate;
		int32 nConsumed = ( int32 )flAdvance;
		m_flRateAcc = flAdvance - ( double )nConsumed;
		if ( !bEnded && nConsumed > 0 )
		{
			if ( m_bLooping )
				ThreadInterlockedExchange( &m_nCursor, ( int32 )( ( ( uint64 )( uint32 )nCursor + nConsumed ) % nTotal ) );
			else
				ThreadInterlockedExchangeAdd( &m_nCursor, nConsumed );
		}

		m_flPeakL = flPeakL;
		m_flPeakR = flPeakR;

		if ( bEnded )
		{
			ThreadInterlockedExchange( &m_nCursor, ( int32 )nTotal );
			ThreadInterlockedCompareExchange( &m_nState, CHANSTATE_STOPPED, CHANSTATE_PLAYING );
			return true;
		}
		return true;
	}

	//----------------------------------------------------
	// streaming: pull from the SPSC ring with underrun gaps
	//----------------------------------------------------
	uint32 nWrite = m_nRingWrite.load();
	uint32 nCursor = ( uint32 )ThreadInterlockedCompareExchange( &m_nCursor, 0, 0 );
	uint32 nBuffered = nWrite - nCursor;

	int32 nPullable = ( int32 )( ( double )nFrames * flRate ) + 1;
	if ( ( uint32 )nPullable > nBuffered )
		nPullable = ( int32 )nBuffered;
	if ( nPullable < 0 )
		nPullable = 0;

	// decoded samples for the FFT history (stable region of the ring)
	m_FFTMutex.Lock();
	if ( m_bFFTTrack && nPullable > 0 )
	{
		for ( int32 k = 0; k < nPullable; ++k )
		{
			uint32 pos = ( uint32 )( nCursor + k ) & RING_MASK;
			float left = m_pRing[ ( uint64 )pos * m_nChannels ];
			float right = m_nChannels == 2 ? m_pRing[ ( uint64 )pos * 2 + 1 ] : left;
			AppendFFTFrame( left, right );
		}
	}
	m_FFTMutex.Unlock();

	for ( int i = 0; i < nFrames; ++i )
	{
		double flOffset = m_flRateAcc + ( double )i * flRate;
		uint32 nRelativePos = ( uint32 )flOffset;
		if ( nRelativePos >= nBuffered )
			continue;	// underrun: leave silence

		uint32 nPosInt = nCursor + nRelativePos;
		uint32 nMasked = ( uint32 )nPosInt & RING_MASK;
		uint32 nMaskedNext = ( nRelativePos + 1 < nBuffered ? nPosInt + 1 : nPosInt ) & RING_MASK;
		double flFrac = flOffset - ( double )( int32 )flOffset;
		float flL0 = m_pRing[ ( uint64 )nMasked * ( uint64 )m_nChannels ];
		float flR0 = ( m_nChannels == 2 ) ? m_pRing[ ( uint64 )nMasked * ( uint64 )m_nChannels + 1 ] : flL0;
		float flL1 = m_pRing[ ( uint64 )nMaskedNext * ( uint64 )m_nChannels ];
		float flR1 = ( m_nChannels == 2 ) ? m_pRing[ ( uint64 )nMaskedNext * ( uint64 )m_nChannels + 1 ] : flL1;
		float flL = ( float )( flL0 + ( flL1 - flL0 ) * flFrac );
		float flR = ( float )( flR0 + ( flR1 - flR0 ) * flFrac );
		pMix[ i * 2 ] += flL * flGainL;
		pMix[ i * 2 + 1 ] += flR * flGainR;
		if ( fabsf( flL ) > flPeakL ) flPeakL = fabsf( flL );
		if ( fabsf( flR ) > flPeakR ) flPeakR = fabsf( flR );
	}

	// advance the time cursor by the exact fractional frame count
	double flAdvance = m_flRateAcc + ( double )nFrames * flRate;
	int32 nConsumed = ( int32 )flAdvance;
	m_flRateAcc = flAdvance - ( double )nConsumed;
	if ( ( uint32 )nConsumed > nBuffered )
		nConsumed = ( int32 )nBuffered;
	if ( nConsumed > 0 )
		ThreadInterlockedExchangeAdd( &m_nCursor, nConsumed );

	m_flPeakL = flPeakL;
	m_flPeakR = flPeakR;

	// end of stream: everything decoded has been consumed
	if ( m_bEOF && nBuffered - nConsumed <= 0 )
	{
		ThreadInterlockedCompareExchange( &m_nState, CHANSTATE_STOPPED, CHANSTATE_PLAYING );
		return true;
	}
	return true;
}

//=============================================================================
// Worker thread
//=============================================================================

static uintp AudioWorkerThread( void * )
{
	while ( !g_bWorkerKill )
	{
		bool bDidWork = false;

		// snapshot the list under the lock with temporary refs
		CUtlVector< CLuaAudioChannel * > snapshot;
		g_ChannelListMutex.Lock();
		for ( int i = 0; i < g_Channels.Count(); ++i )
		{
			CLuaAudioChannel *pChan = g_Channels[ i ];
			pChan->AddRef();
			snapshot.AddToTail( pChan );
		}
		g_ChannelListMutex.Unlock();

		for ( int i = 0; i < snapshot.Count(); ++i )
		{
			CLuaAudioChannel *pChan = snapshot[ i ];
			if ( g_bWorkerKill )
			{
				pChan->ReleaseRef();
				continue;
			}

			if ( pChan->NeedsLoadWork() )
			{
				if ( !pChan->OpenDecoder() )
				{
					pChan->QueueLoadResult();
					pChan->ReleaseRef(); // snapshot reference
					continue;
				}
				bDidWork = true;
			}

			if ( pChan->NeedsDecodeWork() )
			{
				pChan->DecodeChunk();
				bDidWork = true;
			}

			pChan->CheckLoadProgress();

			pChan->ReleaseRef();	// snapshot reference
		}

		if ( !bDidWork && !g_bWorkerKill )
			ThreadSleep( 10 );
	}

	return 0;
}

//=============================================================================
// Engine mix callback (engine mix thread)
//=============================================================================

static void LuaStreamMixCallback( float *pStereo, int nFrames, int nSampleRate, void * )
{
	if ( !pStereo || nFrames <= 0 )
		return;

	Vector vecListener;
	QAngle angListener;
	g_ListenerMutex.Lock();
	vecListener = g_vecListener;
	angListener = g_angListener;
	g_ListenerMutex.Unlock();

	g_ChannelListMutex.Lock();
	for ( int i = g_Channels.Count() - 1; i >= 0; --i )
	{
		CLuaAudioChannel *pChan = g_Channels[ i ];
		if ( !pChan->MixInto( pStereo, nFrames, vecListener, angListener ) )
		{
			g_RetiredChannels.AddToTail( pChan );
			g_Channels.Remove( i );
		}
	}
	g_ChannelListMutex.Unlock();

}

//=============================================================================
// Main-thread dispatch (pump)
//=============================================================================

static void PushAudioChannel( lua_State *L, CLuaAudioChannel *pChan )
{
	CLuaAudioChannel **pp = ( CLuaAudioChannel ** )lua_newuserdata( L, sizeof( CLuaAudioChannel * ) );
	*pp = pChan;
	pChan->AddRef();
	luaL_getmetatable( L, "IGModAudioChannel" );
	lua_setmetatable( L, -2 );
}

static void DispatchQueuedCallback( const QueuedCallback_t &entry )
{
	if ( !L )
	{
		if ( entry.pChannel )
			entry.pChannel->ReleaseRef();
		return;
	}

	lua_rawgeti( L, LUA_REGISTRYINDEX, entry.nCallbackRef );
	luaL_unref( L, LUA_REGISTRYINDEX, entry.nCallbackRef );

	if ( lua_isfunction( L, -1 ) )
	{
		if ( entry.nErrorID == 0 && entry.pChannel )
			PushAudioChannel( L, entry.pChannel );
		else
			lua_pushnil( L );

		if ( entry.nErrorID != 0 )
		{
			lua_pushinteger( L, entry.nErrorID );
			lua_pushstring( L, entry.szErrorName );
			luasrc_pcall( L, 3, 0, 0 );
		}
		else
		{
			luasrc_pcall( L, 1, 0, 0 );
		}
	}
	else
	{
		lua_pop( L, 1 );
	}

	if ( entry.pChannel )
		entry.pChannel->ReleaseRef();
}

//=============================================================================
// sound.PlayFile entry (called from the Sounds library binding)
//=============================================================================

int HL2SB_AudioPlayFile( lua_State *L )
{
	const char *pszPath = luaL_checkstring( L, 1 );
	const char *pszFlags = luaL_checkstring( L, 2 );
	luaL_checktype( L, 3, LUA_TFUNCTION );

	// flags: space separated (reference contract)
	bool b3D = false, bMono = false, bNoPlay = false, bNoBlock = false;
	{
		char szFlags[ 128 ];
		Q_strncpy( szFlags, pszFlags, sizeof( szFlags ) );
		char *p = szFlags;
		while ( *p )
		{
			while ( *p == ' ' ) ++p;
			if ( *p == 0 )
				break;
			char *pStart = p;
			while ( *p && *p != ' ' ) ++p;
			bool bLast = ( *p == 0 );
			*p = 0;
			if ( Q_stricmp( pStart, "3d" ) == 0 ) b3D = true;
			else if ( Q_stricmp( pStart, "mono" ) == 0 ) bMono = true;
			else if ( Q_stricmp( pStart, "noplay" ) == 0 ) bNoPlay = true;
			else if ( Q_stricmp( pStart, "noblock" ) == 0 ) bNoBlock = true;
			// unknown flags are ignored (reference behavior)
			if ( bLast )
				break;
			++p;
		}
	}

	// Normalize separators, remove trailing spaces and truncate at "//".
	char szPath[ MAX_PATH ];
	Q_strncpy( szPath, pszPath, sizeof( szPath ) );
	for ( int i = 0; szPath[ i ]; ++i )
	{
		if ( szPath[ i ] == '\\' )
			szPath[ i ] = '/';
	}
	{
		char *pCut = Q_stristr( szPath, "//" );
		if ( pCut )
			*pCut = 0;
		int nLen = Q_strlen( szPath );
		while ( nLen > 0 && szPath[ nLen - 1 ] == ' ' )
			szPath[ --nLen ] = 0;
	}

	if ( !g_bAudioConnected )
	{
		// backend unavailable: immediate failure callback with the reference
		// "not available" code
		lua_pushvalue( L, 3 );
		lua_pushnil( L );
		lua_pushinteger( L, BASS_ERROR_NOTAVAIL );
		lua_pushstring( L, BassErrorName( BASS_ERROR_NOTAVAIL ) );
		lua_call( L, 3, 0 );
		return 0;
	}

	CLuaAudioChannel *pChan = new CLuaAudioChannel();
	pChan->InitLoad( szPath, b3D, bMono, bNoPlay, bNoBlock );

	// registry ref of the callback (main thread)
	lua_pushvalue( L, 3 );
	int nRef = luaL_ref( L, LUA_REGISTRYINDEX );
	if ( nRef == -1 )
	{
		// could not reference the callback: nothing sensible to fire
		pChan->ReleaseRef();
		return 0;
	}
	pChan->SetCallbackRef( nRef );

	g_ChannelListMutex.Lock();
	g_RetiredChannels.EnsureCapacity( g_Channels.Count() + g_RetiredChannels.Count() + 1 );
	pChan->AddRef();	// list membership
	g_Channels.AddToTail( pChan );
	g_ChannelListMutex.Unlock();

	// the worker polls the list every 10ms; no explicit wake needed
	return 0;
}

//=============================================================================
// Public entry points
//=============================================================================

void HL2SB_AudioConnect( CreateInterfaceFn engineFactory )
{
	if ( g_bAudioConnected )
		return;

	g_pAudioDecoderModule = Sys_LoadModule( "miniaudio" );
	CreateInterfaceFn decoderFactory = g_pAudioDecoderModule ? Sys_GetFactory( g_pAudioDecoderModule ) : NULL;
	g_pAudioDecoder = decoderFactory ? ( ILuaAudioDecoder * )decoderFactory( HL2SB_AUDIO_DECODER_INTERFACE_VERSION, NULL ) : NULL;
	if ( !g_pAudioDecoder )
	{
		if ( g_pAudioDecoderModule ) Sys_UnloadModule( g_pAudioDecoderModule );
		g_pAudioDecoderModule = NULL;
		Warning( "Audio decoder module is unavailable\n" );
		return;
	}
	g_pStreamMix = ( ILuaStreamMix * )engineFactory( HL2SB_LUA_STREAM_MIX_INTERFACE_VERSION, NULL );
	if ( g_pStreamMix )
	{
		g_bMixRegistered = g_pStreamMix->RegisterCallback( LuaStreamMixCallback, NULL );
	}

	if ( !g_bMixRegistered )
	{
		Sys_UnloadModule( g_pAudioDecoderModule );
		g_pAudioDecoderModule = NULL;
		g_pAudioDecoder = NULL;
		return;
	}
	g_bWorkerKill = false;
	g_hWorker = CreateSimpleThread( AudioWorkerThread, NULL );
	if ( !g_hWorker )
	{
		g_pStreamMix->UnregisterCallback( LuaStreamMixCallback, NULL );
		g_bMixRegistered = false;
		g_pStreamMix = NULL;
		Sys_UnloadModule( g_pAudioDecoderModule );
		g_pAudioDecoderModule = NULL;
		g_pAudioDecoder = NULL;
		return;
	}
	g_bAudioConnected = true;
}

void HL2SB_AudioFramePump( void )
{
	if ( !g_bAudioConnected )
		return;

	// refresh the 3D listener
	{
		AUTO_LOCK( g_ListenerMutex );
		g_vecListener = MainViewOrigin();
		g_angListener = MainViewAngles();
	}
	for ( ;; )
	{
		g_ChannelListMutex.Lock();
		CLuaAudioChannel *retired = g_RetiredChannels.Count() ? g_RetiredChannels.Tail() : NULL;
		if ( retired ) g_RetiredChannels.Remove( g_RetiredChannels.Count() - 1 );
		g_ChannelListMutex.Unlock();
		if ( !retired ) break;
		retired->ReleaseRef();
	}

	// dispatch completed load callbacks
	for ( ;; )
	{
		QueuedCallback_t entry;
		bool bHave = false;

		g_QueueMutex.Lock();
		if ( g_Queue.Count() > 0 )
		{
			entry = g_Queue[ 0 ];
			g_Queue.Remove( 0 );
			bHave = true;
		}
		g_QueueMutex.Unlock();

		if ( !bHave )
			break;

		DispatchQueuedCallback( entry );
	}
}

static void AudioStopAllChannels( void )
{
	// main thread: stop everything and drop list memberships
	g_ChannelListMutex.Lock();
	for ( int i = 0; i < g_Channels.Count(); ++i )
	{
		CLuaAudioChannel *pChan = g_Channels[ i ];
		pChan->Stop();
		pChan->CloseDecoder();
		pChan->CancelPendingLoad( L );
		pChan->ReleaseRef();	// membership
	}
	g_Channels.RemoveAll();
	for ( int i = 0; i < g_RetiredChannels.Count(); ++i )
	{
		CLuaAudioChannel *pChan = g_RetiredChannels[ i ];
		pChan->CloseDecoder();
		pChan->CancelPendingLoad( L );
		pChan->ReleaseRef();
	}
	g_RetiredChannels.RemoveAll();
	g_ChannelListMutex.Unlock();
}

static void AudioDrainQueue( void )
{
	g_QueueMutex.Lock();
	for ( int i = 0; i < g_Queue.Count(); ++i )
	{
		QueuedCallback_t &entry = g_Queue[ i ];
		if ( L )
			luaL_unref( L, LUA_REGISTRYINDEX, entry.nCallbackRef );
		if ( entry.pChannel )
			entry.pChannel->ReleaseRef();	// queue entry ref
	}
	g_Queue.RemoveAll();
	g_QueueMutex.Unlock();
}

void HL2SB_AudioLevelShutdown( void )
{
	if ( !g_bAudioConnected )
		return;

	g_bWorkerKill = true;
	if ( g_hWorker )
	{
		ThreadJoin( g_hWorker );
		ReleaseThreadHandle( g_hWorker );
		g_hWorker = NULL;
	}
	AudioDrainQueue();
	AudioStopAllChannels();
	g_bWorkerKill = false;
	g_hWorker = CreateSimpleThread( AudioWorkerThread, NULL );
	if ( !g_hWorker )
	{
		g_pStreamMix->UnregisterCallback( LuaStreamMixCallback, NULL );
		g_bMixRegistered = false;
		g_pStreamMix = NULL;
		Sys_UnloadModule( g_pAudioDecoderModule );
		g_pAudioDecoderModule = NULL;
		g_pAudioDecoder = NULL;
		g_bAudioConnected = false;
	}
}

void HL2SB_AudioShutdown( void )
{
	if ( !g_bAudioConnected )
		return;
	g_bAudioConnected = false;

	// 1) stop the worker first: after the join, no worker references exist
	g_bWorkerKill = true;
	if ( g_hWorker )
	{
		ThreadJoin( g_hWorker );
		ReleaseThreadHandle( g_hWorker );
		g_hWorker = NULL;
	}

	// 2) unregister the engine mix callback WITHOUT holding the channel list
	//    lock (the engine waits for an in-flight callback, which takes the
	//    list lock; holding it here would deadlock)
	if ( g_pStreamMix )
	{
		if ( g_bMixRegistered )
			g_pStreamMix->UnregisterCallback( LuaStreamMixCallback, NULL );
		g_pStreamMix = NULL;
		g_bMixRegistered = false;
	}

	// 3) drop everything
	AudioDrainQueue();
	AudioStopAllChannels();
	if ( g_pAudioDecoderModule )
		Sys_UnloadModule( g_pAudioDecoderModule );
	g_pAudioDecoderModule = NULL;
	g_pAudioDecoder = NULL;
}

//=============================================================================
// IGModAudioChannel Lua bindings
//=============================================================================

LUA_REGISTRATION_INIT( AudioChannel )

static CLuaAudioChannel *AudioChannel_Check( lua_State *L, int nArg )
{
	CLuaAudioChannel **ppChan = ( CLuaAudioChannel ** )luaL_checkudata( L, nArg, "IGModAudioChannel" );
	// Stop nulls the userdata pointer; other methods reject the empty shell.
	if ( !*ppChan || !( *ppChan )->IsValid() )
		luaL_error( L, "Tried to use a NULL IGModAudioChannel!" );
	return *ppChan;
}

// Same as AudioChannel_Check, but the reference IsValid binding answers
// false for an emptied userdata instead of raising.
static CLuaAudioChannel *AudioChannel_CheckAllowNull( lua_State *L, int nArg, bool &bValid )
{
	CLuaAudioChannel **ppChan = ( CLuaAudioChannel ** )luaL_checkudata( L, nArg, "IGModAudioChannel" );
	bValid = ( *ppChan != NULL && ( *ppChan )->IsValid() );
	return *ppChan;
}

LUA_BINDING_BEGIN( AudioChannel, Play, "class", "Starts playing the audio channel." )
{
	AudioChannel_Check( L, 1 )->Play();
	return 0;
}
LUA_BINDING_END()

LUA_BINDING_BEGIN( AudioChannel, Pause, "class", "Pauses the audio channel." )
{
	AudioChannel_Check( L, 1 )->Pause();
	return 0;
}
LUA_BINDING_END()

LUA_BINDING_BEGIN( AudioChannel, Stop, "class", "Stops and invalidates the audio channel." )
{
	CLuaAudioChannel **ppChan = ( CLuaAudioChannel ** )luaL_checkudata( L, 1, "IGModAudioChannel" );
	if ( !*ppChan || !( *ppChan )->IsValid() )
		luaL_error( L, "Tried to use a NULL IGModAudioChannel!" );

	CLuaAudioChannel *pChan = *ppChan;
	pChan->Stop();

	// Empty the userdata shell and drop the Lua-held reference. The channel
	// object dies once the mix thread has removed it from the list; after
	// this point every method on this userdata raises the NULL-channel error.
	*ppChan = NULL;
	pChan->ReleaseRef();
	return 0;
}
LUA_BINDING_END()

LUA_BINDING_BEGIN( AudioChannel, SetVolume, "class", "Sets the volume. May be above 1 (reference example uses 3)." )
{
	AudioChannel_Check( L, 1 )->SetVolume( ( float )luaL_checknumber( L, 2 ) );
	return 0;
}
LUA_BINDING_END()

LUA_BINDING_BEGIN( AudioChannel, GetVolume, "class", "Returns the volume." )
{
	lua_pushnumber( L, AudioChannel_Check( L, 1 )->GetVolume() );
	return 1;
}
LUA_BINDING_END()

LUA_BINDING_BEGIN( AudioChannel, SetPan, "class", "Sets the pan: -1 left, 0 center, 1 right." )
{
	AudioChannel_Check( L, 1 )->SetPan( ( float )luaL_checknumber( L, 2 ) );
	return 0;
}
LUA_BINDING_END()

LUA_BINDING_BEGIN( AudioChannel, GetPan, "class", "Returns the pan." )
{
	lua_pushnumber( L, AudioChannel_Check( L, 1 )->GetPan() );
	return 1;
}
LUA_BINDING_END()

LUA_BINDING_BEGIN( AudioChannel, SetPlaybackRate, "class", "Sets the playback rate. 1 is normal speed." )
{
	AudioChannel_Check( L, 1 )->SetPlaybackRate( ( float )luaL_checknumber( L, 2 ) );
	return 0;
}
LUA_BINDING_END()

LUA_BINDING_BEGIN( AudioChannel, GetPlaybackRate, "class", "Returns the playback rate." )
{
	lua_pushnumber( L, AudioChannel_Check( L, 1 )->GetPlaybackRate() );
	return 1;
}
LUA_BINDING_END()

LUA_BINDING_BEGIN( AudioChannel, SetTime, "class", "Seeks; block streamed channels require the noblock flag." )
{
	CLuaAudioChannel *pChan = AudioChannel_Check( L, 1 );
	if ( pChan->IsBlockStreamed() )
		luaL_error( L, "Cannot use SetTime on a block streamed audio channel. Use the 'noblock' flag when creating the channel to disable block streaming." );
	pChan->SetTime( luaL_checknumber( L, 2 ) );
	return 0;
}
LUA_BINDING_END()

LUA_BINDING_BEGIN( AudioChannel, GetTime, "class", "Returns the current playback time in seconds." )
{
	lua_pushnumber( L, AudioChannel_Check( L, 1 )->GetTime() );
	return 1;
}
LUA_BINDING_END()

LUA_BINDING_BEGIN( AudioChannel, GetLength, "class", "Returns the sound length in seconds." )
{
	lua_pushnumber( L, AudioChannel_Check( L, 1 )->GetLength() );
	return 1;
}
LUA_BINDING_END()

LUA_BINDING_BEGIN( AudioChannel, GetBufferedTime, "class", "Returns the buffered duration; offline channels report the full length." )
{
	lua_pushnumber( L, AudioChannel_Check( L, 1 )->GetBufferedTime() );
	return 1;
}
LUA_BINDING_END()

LUA_BINDING_BEGIN( AudioChannel, GetFileName, "class", "Returns the file name the channel was loaded from." )
{
	lua_pushstring( L, AudioChannel_Check( L, 1 )->GetFileName() );
	return 1;
}
LUA_BINDING_END()

LUA_BINDING_BEGIN( AudioChannel, GetSamplingRate, "class", "Returns the sample rate; always 44100 (reference contract)." )
{
	AudioChannel_Check( L, 1 );
	lua_pushinteger( L, AUDIO_RATE );
	return 1;
}
LUA_BINDING_END()

LUA_BINDING_BEGIN( AudioChannel, GetBitsPerSample, "class", "Returns 0 (unknown; the reference reports 0 for mp3/ogg)." )
{
	AudioChannel_Check( L, 1 );
	lua_pushinteger( L, 0 );
	return 1;
}
LUA_BINDING_END()

LUA_BINDING_BEGIN( AudioChannel, GetAverageBitRate, "class", "Returns 0 (unknown)." )
{
	AudioChannel_Check( L, 1 );
	lua_pushinteger( L, 0 );
	return 1;
}
LUA_BINDING_END()

LUA_BINDING_BEGIN( AudioChannel, GetState, "class", "Returns a GMOD_CHANNEL enum value." )
{
	lua_pushinteger( L, AudioChannel_Check( L, 1 )->GetState() );
	return 1;
}
LUA_BINDING_END()

LUA_BINDING_BEGIN( AudioChannel, Is3D, "class", "True when the channel was created with the 3d flag." )
{
	lua_pushboolean( L, AudioChannel_Check( L, 1 )->Is3D() ? 1 : 0 );
	return 1;
}
LUA_BINDING_END()

LUA_BINDING_BEGIN( AudioChannel, Get3DEnabled, "class", "True when 3D processing is enabled for a 3d channel." )
{
	lua_pushboolean( L, AudioChannel_Check( L, 1 )->Get3DEnabled() ? 1 : 0 );
	return 1;
}
LUA_BINDING_END()

LUA_BINDING_BEGIN( AudioChannel, Set3DEnabled, "class", "Enables/disables 3D processing; only works on channels created with the 3d flag. The boolean argument is required." )
{
	CLuaAudioChannel *pChan = AudioChannel_Check( L, 1 );
	pChan->Set3DEnabled( lua_toboolean( L, 2 ) ? true : false );
	return 0;
}
LUA_BINDING_END()

LUA_BINDING_BEGIN( AudioChannel, Set3DCone, "class", "Sets the 3D projection cone angles; negative values keep the current ones." )
{
	CLuaAudioChannel *pChan = AudioChannel_Check( L, 1 );
	float flInner = ( float )luaL_checknumber( L, 2 );
	float flOuter = ( float )luaL_checknumber( L, 3 );
	float flOuterVol = ( float )luaL_checknumber( L, 4 );
	pChan->Set3DCone( flInner, flOuter, flOuterVol );
	return 0;
}
LUA_BINDING_END()

LUA_BINDING_BEGIN( AudioChannel, Get3DCone, "class", "Returns the 3D cone: inner angle, outer angle, outer volume." )
{
	float flInner, flOuter, flOuterVol;
	AudioChannel_Check( L, 1 )->Get3DCone( flInner, flOuter, flOuterVol );
	lua_pushnumber( L, flInner );
	lua_pushnumber( L, flOuter );
	lua_pushnumber( L, flOuterVol );
	return 3;
}
LUA_BINDING_END()

LUA_BINDING_BEGIN( AudioChannel, Set3DFadeDistance, "class", "Sets the 3D fade distances; zero or negative keeps the current ones." )
{
	CLuaAudioChannel *pChan = AudioChannel_Check( L, 1 );
	float flMin = ( float )luaL_checknumber( L, 2 );
	float flMax = ( float )luaL_checknumber( L, 3 );
	pChan->Set3DFadeDistance( flMin, flMax );
	return 0;
}
LUA_BINDING_END()

LUA_BINDING_BEGIN( AudioChannel, Get3DFadeDistance, "class", "Returns the 3D fade distances (min, max)." )
{
	float flMin, flMax;
	AudioChannel_Check( L, 1 )->Get3DFadeDistance( flMin, flMax );
	lua_pushnumber( L, flMin );
	lua_pushnumber( L, flMax );
	return 2;
}
LUA_BINDING_END()

LUA_BINDING_BEGIN( AudioChannel, SetPos, "class", "Sets the world position of a 3D channel." )
{
	CLuaAudioChannel *pChan = AudioChannel_Check( L, 1 );
	Vector vecPos = luaL_checkvector( L, 2 );
	Vector *pDirection = ( Vector * )luaL_testudata( L, 3, "Vector" );
	pChan->Set3DPos( vecPos, pDirection ? *pDirection : vec3_origin );
	return 0;
}
LUA_BINDING_END()

LUA_BINDING_BEGIN( AudioChannel, GetPos, "class", "Returns the 3D position and direction vectors." )
{
	CLuaAudioChannel *pChan = AudioChannel_Check( L, 1 );
	Vector vecPos = pChan->GetPos();
	lua_pushvector( L, vecPos );
	lua_pushvector( L, pChan->GetDirection() );
	return 2;
}
LUA_BINDING_END()

LUA_BINDING_BEGIN( AudioChannel, EnableLooping, "class", "Enables looping; block streamed channels require the noblock flag." )
{
	CLuaAudioChannel *pChan = AudioChannel_Check( L, 1 );
	bool bEnable = lua_toboolean( L, 2 ) != 0;
	if ( bEnable && pChan->IsBlockStreamed() )
		luaL_error( L, "Cannot use EnableLooping on a block streamed audio channel. Use the 'noblock' flag when creating the channel to disable block streaming." );
	pChan->EnableLooping( bEnable );
	return 0;
}
LUA_BINDING_END()

LUA_BINDING_BEGIN( AudioChannel, IsLooping, "class", "True when the channel is looping." )
{
	lua_pushboolean( L, AudioChannel_Check( L, 1 )->IsLooping() ? 1 : 0 );
	return 1;
}
LUA_BINDING_END()

LUA_BINDING_BEGIN( AudioChannel, IsBlockStreamed, "class", "False for noblock channels; SetTime needs this to be false." )
{
	lua_pushboolean( L, AudioChannel_Check( L, 1 )->IsBlockStreamed() ? 1 : 0 );
	return 1;
}
LUA_BINDING_END()

LUA_BINDING_BEGIN( AudioChannel, IsOnline, "class", "False: online streams are not implemented in this backend." )
{
	AudioChannel_Check( L, 1 );
	lua_pushboolean( L, 0 );
	return 1;
}
LUA_BINDING_END()

LUA_BINDING_BEGIN( AudioChannel, IsValid, "class", "False for a channel invalidated by Stop; true otherwise (reference answers false on the emptied userdata instead of raising)." )
{
	bool bValid = false;
	AudioChannel_CheckAllowNull( L, 1, bValid );
	lua_pushboolean( L, bValid ? 1 : 0 );
	return 1;
}
LUA_BINDING_END()

LUA_BINDING_BEGIN( AudioChannel, GetLevel, "class", "Returns the left and right peak levels of recently played audio." )
{
	float flLeft, flRight;
	AudioChannel_Check( L, 1 )->GetLevel( flLeft, flRight );
	lua_pushnumber( L, flLeft );
	lua_pushnumber( L, flRight );
	return 2;
}
LUA_BINDING_END()

LUA_BINDING_BEGIN( AudioChannel, FFT, "class", "Fills the table with Hann-windowed DFT magnitudes and returns the bin count." )
{
	CLuaAudioChannel *pChan = AudioChannel_Check( L, 1 );
	luaL_checktype( L, 2, LUA_TTABLE );
	int nSize = ( int )luaL_optnumber( L, 3, 2 );
	if ( nSize < 0 )
		nSize = 0;
	if ( nSize > 7 )
		nSize = 7;

	int nBins = 1 << ( 7 + nSize );	// FFT_256=0 -> 128 bins
	float *pBins = ( float * )malloc( sizeof( float ) * nBins );
	int nFilled = 0;
	if ( pBins )
	{
		nFilled = pChan->FFT( pBins, nBins );
		for ( int i = 0; i < nFilled; ++i )
		{
			lua_pushinteger( L, i + 1 );
			lua_pushnumber( L, pBins[ i ] );
			lua_settable( L, 2 );
		}
		free( pBins );
	}
	lua_pushnumber( L, nFilled );
	return 1;
}
LUA_BINDING_END()

static int AudioChannel_GetTagsID3( lua_State *L )
{
	CLuaAudioChannel *pChan = AudioChannel_Check( L, 1 );
	static const char *pKeys[ 5 ] = { "title", "artist", "album", "year", "comment" };
	if ( !pChan->HasID3Tag() )
	{
		lua_pushnil( L );
		return 1;
	}
	lua_newtable( L );
	// ID3v1 data includes its three-character identifier.
	lua_pushstring( L, "id" );
	lua_pushstring( L, "TAG" );
	lua_settable( L, -3 );
	for ( int i = 0; i < 5; ++i )
	{
		const char *pValue = pChan->GetTag( pKeys[ i ] );
		lua_pushstring( L, pKeys[ i ] );
		lua_pushstring( L, pValue ? pValue : "" );
		lua_settable( L, -3 );
	}
	lua_pushinteger( L, pChan->GetID3Genre() );
	lua_setfield( L, -2, "genre" );
	return 1;
}

// The reference contract: no online streams here, so the HTTP/Meta tag
// families have no source; OGG comments and vendor need a phase 2 decoder
// surface; MP4/WMA containers are not decoded at all.
static int AudioChannel_GetTagsNil( lua_State *L )
{
	AudioChannel_Check( L, 1 );
	lua_pushnil( L );
	return 1;
}

//=============================================================================
// luaopen
//=============================================================================

// Empty channel handles retain a printable representation.
static int AudioChannel_tostring( lua_State *L )
{
	bool bValid = false;
	CLuaAudioChannel *pChan = AudioChannel_CheckAllowNull( L, 1, bValid );
	if ( !bValid )
	{
		lua_pushstring( L, "IGModAudioChannel [NULL]" );
		return 1;
	}
	char szOut[ MAX_PATH + 32 ];
	Q_snprintf( szOut, sizeof( szOut ), "IGModAudioChannel [%s]", pChan->GetFileName() );
	lua_pushstring( L, szOut );
	return 1;
}

static int AudioChannel_gc( lua_State *L )
{
	CLuaAudioChannel **ppChan = ( CLuaAudioChannel ** )lua_touserdata( L, 1 );
	if ( ppChan && *ppChan )
	{
		// Collecting the channel stops the sound (reference contract): the
		// remaining references are the list membership and any pending queue
		// entry; the mix thread removes finished channels on its own.
		( *ppChan )->Stop();
		( *ppChan )->ReleaseRef();
		*ppChan = NULL;
	}
	return 0;
}

LUALIB_API int luaopen_AudioChannel( lua_State *L )
{
	LUA_PUSH_NEW_METATABLE( L, "IGModAudioChannel" );

	LUA_REGISTRATION_COMMIT( AudioChannel );

	// The tag methods have plain helper bodies, not binding-macro ones.
	static const LuaRegEntry s_TagEntries[] =
	{
		{ "GetTagsID3", AudioChannel_GetTagsID3 },
		{ "GetTagsOGG", AudioChannel_GetTagsNil },
		{ "GetTagsVendor", AudioChannel_GetTagsNil },
		{ "GetTagsHTTP", AudioChannel_GetTagsNil },
		{ "GetTagsMeta", AudioChannel_GetTagsNil },
		{ "GetTagsMP4", AudioChannel_GetTagsNil },
		{ "GetTagsWMA", AudioChannel_GetTagsNil },
	};
	for ( int i = 0; i < ( int )ARRAYSIZE( s_TagEntries ); ++i )
	{
		lua_pushcfunction( L, s_TagEntries[ i ].function );
		lua_setfield( L, -2, s_TagEntries[ i ].name );
	}

	lua_pushcfunction( L, AudioChannel_gc );
	lua_setfield( L, -2, "__gc" );

	lua_pushcfunction( L, AudioChannel_tostring );
	lua_setfield( L, -2, "__tostring" );

	lua_pushvalue( L, -1 );
	lua_setfield( L, -2, "__index" );

	lua_pushstring( L, "IGModAudioChannel" );
	lua_setfield( L, -2, "__type" );

	// GMOD_CHANNEL_* / FFT_* global constants (reference contract: globals).
	static const struct { const char *pName; int nValue; } s_Channels[] =
	{
		{ "GMOD_CHANNEL_STOPPED", 0 },
		{ "GMOD_CHANNEL_PLAYING", 1 },
		{ "GMOD_CHANNEL_PAUSED", 2 },
		{ "GMOD_CHANNEL_STALLED", 3 },
	};
	for ( int i = 0; i < ( int )ARRAYSIZE( s_Channels ); ++i )
	{
		lua_pushinteger( L, s_Channels[ i ].nValue );
		lua_setglobal( L, s_Channels[ i ].pName );
	}
	static const struct { const char *pName; int nValue; } s_FFT[] =
	{
		{ "FFT_256", 0 }, { "FFT_512", 1 }, { "FFT_1024", 2 }, { "FFT_2048", 3 },
		{ "FFT_4096", 4 }, { "FFT_8192", 5 }, { "FFT_16384", 6 }, { "FFT_32768", 7 },
	};
	for ( int i = 0; i < ( int )ARRAYSIZE( s_FFT ); ++i )
	{
		lua_pushinteger( L, s_FFT[ i ].nValue );
		lua_setglobal( L, s_FFT[ i ].pName );
	}

	return 0;
}
