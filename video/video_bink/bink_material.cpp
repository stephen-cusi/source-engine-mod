//========= Copyright Valve Corporation, All rights reserved. ============//
//
// Purpose: 
//
//=============================================================================


#include "filesystem.h"
#include "tier1/strtools.h"
#include "tier1/utllinkedlist.h"
#include "tier1/KeyValues.h"
#include "materialsystem/imaterial.h"
#include "materialsystem/imaterialsystem.h"
#include "materialsystem/MaterialSystemUtil.h"
#include "materialsystem/itexture.h"
#include "vtf/vtf.h"
#include "pixelwriter.h"
#include "tier3/tier3.h"
#include "platform.h"
#include "bink_material.h"
#include "tier0/memdbgon.h"

extern "C" {
#include "yuv_rgb.h"
}


// makes a copy of a string
char *COPY_STRING( const char *pString )
{
	if ( pString == nullptr )
		return nullptr;

	size_t strLen = V_strlen( pString );

	char *pNewStr = new char[ strLen+ 1 ];
	if ( strLen > 0 )
		V_memcpy( pNewStr, pString, strLen );

	pNewStr[strLen] = nullchar;

	return pNewStr;
}

int open_codec_context(int *stream_idx, AVCodecContext **dec_ctx, AVFormatContext *fmt_ctx, enum AVMediaType type)
{
	int ret, stream_index;
	AVStream *st;
	const AVCodec *dec = NULL;

	ret = av_find_best_stream(fmt_ctx, type, -1, -1, NULL, 0);
	if (ret < 0)
	{
		Warning("Could not find %s stream\n",
				av_get_media_type_string(type));
		return ret;
	}
	else
	{
		stream_index = ret;
		st = fmt_ctx->streams[stream_index];

		/* find decoder for the stream */
		dec = avcodec_find_decoder(st->codecpar->codec_id);
		if (!dec)
		{
			Warning("Failed to find %s codec\n",
					av_get_media_type_string(type));
			return AVERROR(EINVAL);
		}

		/* Allocate a codec context for the decoder */
		*dec_ctx = avcodec_alloc_context3(dec);
		if (!*dec_ctx)
		{
			Warning("Failed to allocate the %s codec context\n",
					av_get_media_type_string(type));
			return AVERROR(ENOMEM);
		}

		/* Copy codec parameters from input stream to output codec context */
		if ((ret = avcodec_parameters_to_context(*dec_ctx, st->codecpar)) < 0)
		{
			Warning("Failed to copy %s codec parameters to decoder context\n",
					av_get_media_type_string(type));
			avcodec_free_context(dec_ctx);
			return ret;
		}

		/* Init the decoders */
		if ((ret = avcodec_open2(*dec_ctx, dec, NULL)) < 0)
		{
			Warning("Failed to open %s codec\n",
					av_get_media_type_string(type));
			avcodec_free_context(dec_ctx);
			return ret;
		}

		*stream_idx = stream_index;
	}

	return 0;
}

// ===========================================================================
// CBinkMaterialRGBTextureRegenerator - Inherited from ITextureRegenerator
//	Copies the decoded RGB image into an opaque RGBA texture
// ===========================================================================
CBinkMaterialRGBTextureRegenerator::CBinkMaterialRGBTextureRegenerator() :
	m_SrcImage( nullptr ),
	m_nSourceWidth( 0 ),
	m_nSourceHeight( 0 )
{
}


CBinkMaterialRGBTextureRegenerator::~CBinkMaterialRGBTextureRegenerator() 
{
	// nothing to do
}

void CBinkMaterialRGBTextureRegenerator::SetSourceImage( uint8_t *SrcImage, int nWidth, int nHeight )
{
	m_SrcImage = SrcImage;
	m_nSourceWidth	= nWidth;
	m_nSourceHeight = nHeight;
}

void CBinkMaterialRGBTextureRegenerator::RegenerateTextureBits( ITexture *pTexture, IVTFTexture *pVTFTexture, Rect_t *pRect )
{
	AssertExit( pVTFTexture != nullptr );

	// Error condition, should only have 1 frame, 1 face, 1 mip level
	if ( ( pVTFTexture->FrameCount() > 1 ) || ( pVTFTexture->FaceCount() > 1 ) || ( pVTFTexture->MipCount() > 1 ) || ( pVTFTexture->Depth() > 1 ) )
	{
		WarningAssert( "Texture Properties Incorrect ");
		memset( pVTFTexture->ImageData(), 0xAA, pVTFTexture->ComputeTotalSize() );
		return;
	}

	// Make sure we have a valid video image source	
/*	if ( m_SrcGWorld == nullptr )
	{
		WarningAssert( "Video texture source not set" );
		memset( pVTFTexture->ImageData(), 0xCC, pVTFTexture->ComputeTotalSize() );
		return;
	}*/

	// Verify the destination texture is set up correctly
	Assert( pVTFTexture->Format() == IMAGE_FORMAT_RGBA8888 );
	Assert( pVTFTexture->RowSizeInBytes( 0 ) >= pVTFTexture->Width() * 4 );
	Assert( pVTFTexture->Width() >= m_nSourceWidth );
	Assert( pVTFTexture->Height() >= m_nSourceHeight );

	// Copy directly from the Quicktime GWorld
	BYTE   *pImageData	= pVTFTexture->ImageData();
	int dstStride = pVTFTexture->RowSizeInBytes( 0 );

	for (int y = 0; y < m_nSourceHeight; y++ )
	{
		const BYTE *pSrcData = m_SrcImage + y * m_nSourceWidth * 3;
		for ( int x = 0; x < m_nSourceWidth; ++x )
		{
#if defined( ANDROID )
			// The YUV->RGB conversion writes BGR bytes, and GLES uploads
			// RGBA8888 verbatim, so swap R and B for correct colors.
			pImageData[x * 4 + 0] = pSrcData[x * 3 + 2];
			pImageData[x * 4 + 1] = pSrcData[x * 3 + 1];
			pImageData[x * 4 + 2] = pSrcData[x * 3 + 0];
#else
			pImageData[x * 4 + 0] = pSrcData[x * 3 + 0];
			pImageData[x * 4 + 1] = pSrcData[x * 3 + 1];
			pImageData[x * 4 + 2] = pSrcData[x * 3 + 2];
#endif
			pImageData[x * 4 + 3] = 255;
		}

		pImageData += dstStride;
	}
}


void CBinkMaterialRGBTextureRegenerator::Release()
{
	// we don't invoke the destructor here, we're not using the no-release extensions
}



// ===========================================================================
// CBinkMaterial class - creates a material, opens a QuickTime movie
//		   and plays the movie onto the material
// ===========================================================================

//-----------------------------------------------------------------------------
// CBinkMaterial Constructor
//-----------------------------------------------------------------------------
CBinkMaterial::CBinkMaterial() :
	m_LastResult( VideoResult::SUCCESS ),
	m_TexCordU( 0.0f ),
	m_TexCordV( 0.0f ),
	m_VideoFrameWidth( 0 ),
	m_VideoFrameHeight( 0 ),
	m_pFileName( nullptr ),
	m_PlaybackFlags( VideoPlaybackFlags::NO_PLAYBACK_OPTIONS ),
	m_bInitCalled( false ),
	m_bMovieInitialized( false ),
	m_bMoviePlaying( false ),
	m_bMovieFinishedPlaying( false ),
	m_bMoviePaused( false ),
	m_bLoopMovie( false ),
	m_bHasAudio( false ),
	m_bMuted( false ),
	m_CurrentVolume( 1.0f ),
	m_QTMovieTimeScale( 0.0f ),
	m_QTMoviefloat( 0.0f ),
	m_QTMovieDuration( 0.0f ),
	m_QTMovieDurationinSec( 0.0f ),
	m_QTMovieFrameCount( 0 ),
	m_MovieFirstFrameTime( 0.0 ),
	m_NextInterestingTimeToPlay( 0.0 ),
	m_MoviePauseTime( 0.0f ),
	m_AVFrame( nullptr ),
	m_AVAudioFrame( nullptr ),
	m_AVPkt( nullptr ),
	m_AVFmtCtx( nullptr ),
	m_AVVideoStreamID( -1 ),
	m_AVAudioStreamID( -1 ),
	m_AVVideoDecCtx( nullptr ),
	m_AVAudioDecCtx( nullptr ),
	m_AVVideoStream( nullptr ),
	m_AVAudioStream( nullptr ),
	m_AVPixFormat( AV_PIX_FMT_NONE ),
	m_MovieFrameDuration( 1.0 / 30.0 ),
	m_SwrContext( nullptr ),
	m_AudioOutputFormat( AV_SAMPLE_FMT_NONE ),
	m_bAudioOutputConfigured( false ),
	m_bAudioDecoderDrained( false ),
	m_AudioQueueRead( 0 ),
	m_RGBData( nullptr )
{
	memset( &m_AudioSpec, 0, sizeof(m_AudioSpec) );
	memset( m_AVVideoData, 0, sizeof(m_AVVideoData) );
	memset( m_AVVideoLinesize, 0, sizeof(m_AVVideoLinesize) );

	Reset();
}


//-----------------------------------------------------------------------------
// CBinkMaterial Destructor
//-----------------------------------------------------------------------------
CBinkMaterial::~CBinkMaterial()
{
	Shutdown();

	av_frame_free( &m_AVFrame );
	av_frame_free( &m_AVAudioFrame );
	av_packet_free( &m_AVPkt );

}


void CBinkMaterial::Reset()
{
	CloseFile();

	DestroyProceduralTexture();
	DestroyProceduralMaterial();

	m_TexCordU = 0.0f;
	m_TexCordV = 0.0f;

	m_VideoFrameWidth = 0;
	m_VideoFrameHeight = 0;

	m_AVPixFormat = AV_PIX_FMT_NONE;
	m_PlaybackFlags = VideoPlaybackFlags::NO_PLAYBACK_OPTIONS;

	m_bMovieInitialized = false;
	m_bMoviePlaying = false;
	m_bMovieFinishedPlaying = false;
	m_bMoviePaused = false;
	m_bLoopMovie = false;

	m_bHasAudio = false;
	m_bMuted = false;

	m_CurrentVolume = 1.0f;

	m_QTMovieTimeScale = 0;
	m_QTMovieDuration = 0;
	m_QTMovieDurationinSec = 0.0f;
	m_QTMovieFrameRate.SetFPS( 0, false );
	m_QTMovieFrameCount = 0;
	m_MovieFirstFrameTime = 0.0;
	m_NextInterestingTimeToPlay = 0.0;
	m_MoviePauseTime = 0.0f;
	m_MovieFrameDuration = 1.0 / 30.0;

	if( !m_AVFrame )
		m_AVFrame = av_frame_alloc();
	if( !m_AVAudioFrame )
		m_AVAudioFrame = av_frame_alloc();
	if( !m_AVPkt)
		m_AVPkt = av_packet_alloc();

	AssertMsg( m_AVFrame, "av_frame_alloc return nullptr\n" );
	AssertMsg( m_AVAudioFrame, "av_frame_alloc return nullptr\n" );
	AssertMsg( m_AVPkt, "av_packet_alloc return nullptr\n"  );

	m_LastResult = VideoResult::SUCCESS;
	m_bInitCalled = false;
}


void CBinkMaterial::SetFileName( const char *theMovieFileName )
{
	SAFE_DELETE_ARRAY( m_pFileName );

	if ( theMovieFileName != nullptr )
	{
		AssertMsg( V_strlen( theMovieFileName ) <= MAX_FILENAME_LEN, "Bad Bink Movie Filename" );
		m_pFileName = COPY_STRING( theMovieFileName );
	}
}


VideoResult_t CBinkMaterial::SetResult( VideoResult_t status )
{
	m_LastResult = status;
	return status;
}


//-----------------------------------------------------------------------------
// Video information functions
//-----------------------------------------------------------------------------


//-----------------------------------------------------------------------------
// Returns the resolved filename of the video, as it might differ from
// what the user supplied, (also with absolute path)
//-----------------------------------------------------------------------------
const char *CBinkMaterial::GetVideoFileName()
{
	return m_pFileName;
}


VideoFrameRate_t &CBinkMaterial::GetVideoFrameRate()
{
	return m_QTMovieFrameRate;
}


VideoResult_t CBinkMaterial::GetLastResult()
{
	return m_LastResult;
}


//-----------------------------------------------------------------------------
// Audio Functions
//-----------------------------------------------------------------------------
bool CBinkMaterial::HasAudio()
{
	return m_bHasAudio;
}


bool CBinkMaterial::SetVolume( float fVolume )
{
	AUTO_LOCK( m_AudioMutex );
	m_CurrentVolume = clamp( fVolume, 0.0f, 1.0f );
	SetResult( VideoResult::SUCCESS );
	return true;
}


float CBinkMaterial::GetVolume()
{
	AUTO_LOCK( m_AudioMutex );
	return m_CurrentVolume;
}


void CBinkMaterial::SetMuted( bool bMuteState )
{
	AssertExitFunc( m_bMoviePlaying, SetResult( VideoResult::OPERATION_OUT_OF_SEQUENCE) );
	AUTO_LOCK( m_AudioMutex );

	SetResult( VideoResult::SUCCESS );

	if ( bMuteState == m_bMuted  )		// no change?
	{
		return;
	}

	m_bMuted = bMuteState;

	if ( m_bHasAudio )
	{

	}

	SetResult( VideoResult::SUCCESS );
}


bool CBinkMaterial::IsMuted()
{
	AUTO_LOCK( m_AudioMutex );
	return m_bMuted;
}


VideoResult_t CBinkMaterial::SoundDeviceCommand( VideoSoundDeviceOperation_t operation, void *pDevice, void *pData )
{
	AssertExitV( m_bMovieInitialized || m_bMoviePlaying, VideoResult::OPERATION_OUT_OF_SEQUENCE );

	switch( operation )
	{
		// On win32, we try and create an audio context from a GUID
		case VideoSoundDeviceOperation::SET_DIRECT_SOUND_DEVICE:
		{
#if defined ( WIN32 )
			SAFE_RELEASE_AUDIOCONTEXT( m_AudioContext );
			return ( CreateMovieAudioContext( m_bHasAudio, m_QTMovie, &m_AudioContext ) ? SetResult( VideoResult::SUCCESS ) : SetResult( VideoResult::AUDIO_ERROR_OCCURED ) );
#else
			// On any other OS, we don't support this operation
			return SetResult( VideoResult::OPERATION_NOT_SUPPORTED );
#endif
		}
		case VideoSoundDeviceOperation::SET_SOUND_MANAGER_DEVICE:
		{
#if defined ( OSX )
			SAFE_RELEASE_AUDIOCONTEXT( m_AudioContext );
			return ( CreateMovieAudioContext( m_bHasAudio, m_QTMovie, &m_AudioContext ) ? SetResult( VideoResult::SUCCESS ) : SetResult( VideoResult::AUDIO_ERROR_OCCURED ) );
#else
			// On any other OS, we don't support this operation
			return SetResult( VideoResult::OPERATION_NOT_SUPPORTED );
#endif
		}
		case VideoSoundDeviceOperation::SET_LIB_AUDIO_DEVICE:
		case VideoSoundDeviceOperation::HOOK_X_AUDIO:
		case VideoSoundDeviceOperation::SET_MILES_SOUND_DEVICE:
		{
			return SetResult( VideoResult::OPERATION_NOT_SUPPORTED );
		}
		default:
		{
			return SetResult( VideoResult::BAD_INPUT_PARAMETERS );
		}
	}

}


bool CBinkMaterial::ConfigureAudioOutput( const SDL_AudioSpec &audioSpec )
{
	if ( !m_bHasAudio )
		return true;

	AVSampleFormat outputFormat = AV_SAMPLE_FMT_NONE;
	if ( audioSpec.format == AUDIO_S16SYS )
		outputFormat = AV_SAMPLE_FMT_S16;
	else if ( audioSpec.format == AUDIO_F32SYS )
		outputFormat = AV_SAMPLE_FMT_FLT;
	else
		return false;

	AVChannelLayout outputLayout;
	av_channel_layout_default( &outputLayout, audioSpec.channels );
	swr_free( &m_SwrContext );
	int result = swr_alloc_set_opts2( &m_SwrContext, &outputLayout, outputFormat, audioSpec.freq,
		&m_AVAudioDecCtx->ch_layout, m_AVAudioDecCtx->sample_fmt, m_AVAudioDecCtx->sample_rate, 0, nullptr );
	av_channel_layout_uninit( &outputLayout );
	if ( result < 0 || !m_SwrContext || swr_init( m_SwrContext ) < 0 )
	{
		swr_free( &m_SwrContext );
		return false;
	}

	AUTO_LOCK( m_AudioMutex );
	m_AudioSpec = audioSpec;
	m_AudioOutputFormat = outputFormat;
	m_bAudioOutputConfigured = true;
	m_AudioQueue.clear();
	m_AudioQueueRead = 0;
	Msg( "Bink audio output: %d Hz, %d channels, SDL format 0x%x\n",
		audioSpec.freq, audioSpec.channels, audioSpec.format );
	return true;
}


void CBinkMaterial::MixAudio( uint8_t *pOutput, int outputBytes )
{
	AUTO_LOCK( m_AudioMutex );
	if ( !pOutput || outputBytes <= 0 || !m_bMoviePlaying || m_bMoviePaused )
		return;
	const size_t available = m_AudioQueue.size() - m_AudioQueueRead;
	const int bytesToMix = min( outputBytes, static_cast<int>(available) );
	if ( bytesToMix <= 0 )
		return;

	if ( !m_bMuted && m_CurrentVolume > 0.0f )
	{
		SDL_MixAudioFormat( pOutput, &m_AudioQueue[m_AudioQueueRead], m_AudioSpec.format, bytesToMix,
			static_cast<int>( m_CurrentVolume * SDL_MIX_MAXVOLUME ) );
	}
	m_AudioQueueRead += bytesToMix;
	if ( m_AudioQueueRead == m_AudioQueue.size() )
	{
		m_AudioQueue.clear();
		m_AudioQueueRead = 0;
	}
}


int CBinkMaterial::QueuedAudioBytes()
{
	AUTO_LOCK( m_AudioMutex );
	return static_cast<int>( m_AudioQueue.size() - m_AudioQueueRead );
}


void CBinkMaterial::ClearAudioQueue()
{
	AUTO_LOCK( m_AudioMutex );
	m_AudioQueue.clear();
	m_AudioQueueRead = 0;
}


bool CBinkMaterial::QueueAudioFrame( const AVFrame *pFrame )
{
	if ( !m_bAudioOutputConfigured || !m_SwrContext )
		return true;

	const int outputSamples = static_cast<int>( av_rescale_rnd(
		swr_get_delay( m_SwrContext, m_AVAudioDecCtx->sample_rate ) + pFrame->nb_samples,
		m_AudioSpec.freq, m_AVAudioDecCtx->sample_rate, AV_ROUND_UP ) );
	const int outputBytes = av_samples_get_buffer_size( nullptr, m_AudioSpec.channels, outputSamples,
		m_AudioOutputFormat, 1 );
	if ( outputBytes <= 0 )
		return false;

	std::vector<uint8_t> converted( outputBytes );
	uint8_t *outputData[] = { converted.data() };
	// swr_convert() input pointer constness differs between FFmpeg 5.x/6.x and
	// distro-patched headers; const_cast so it compiles against both signatures.
	const int convertedSamples = swr_convert( m_SwrContext, outputData, outputSamples,
		const_cast<const uint8_t **>( pFrame->extended_data ), pFrame->nb_samples );
	if ( convertedSamples < 0 )
		return false;

	const int convertedBytes = av_samples_get_buffer_size( nullptr, m_AudioSpec.channels, convertedSamples,
		m_AudioOutputFormat, 1 );
	AUTO_LOCK( m_AudioMutex );
	if ( m_AudioQueueRead > 0 )
	{
		m_AudioQueue.erase( m_AudioQueue.begin(), m_AudioQueue.begin() + m_AudioQueueRead );
		m_AudioQueueRead = 0;
	}
	m_AudioQueue.insert( m_AudioQueue.end(), converted.begin(), converted.begin() + convertedBytes );
	return true;
}


bool CBinkMaterial::FlushAudioResampler()
{
	if ( !m_bAudioOutputConfigured || !m_SwrContext )
		return true;

	while ( true )
	{
		const int outputSamples = static_cast<int>( swr_get_delay( m_SwrContext, m_AudioSpec.freq ) );
		if ( outputSamples <= 0 )
			return true;
		const int outputBytes = av_samples_get_buffer_size( nullptr, m_AudioSpec.channels, outputSamples,
			m_AudioOutputFormat, 1 );
		if ( outputBytes <= 0 )
			return false;

		std::vector<uint8_t> converted( outputBytes );
		uint8_t *outputData[] = { converted.data() };
		const int convertedSamples = swr_convert( m_SwrContext, outputData, outputSamples, nullptr, 0 );
		if ( convertedSamples < 0 )
			return false;
		if ( convertedSamples == 0 )
			return true;

		const int convertedBytes = av_samples_get_buffer_size( nullptr, m_AudioSpec.channels, convertedSamples,
			m_AudioOutputFormat, 1 );
		AUTO_LOCK( m_AudioMutex );
		if ( m_AudioQueueRead > 0 )
		{
			m_AudioQueue.erase( m_AudioQueue.begin(), m_AudioQueue.begin() + m_AudioQueueRead );
			m_AudioQueueRead = 0;
		}
		m_AudioQueue.insert( m_AudioQueue.end(), converted.begin(), converted.begin() + convertedBytes );
	}
}


bool CBinkMaterial::DecodeAudioPacket( const AVPacket *pPacket )
{
	if ( !m_AVAudioDecCtx )
		return true;

	int result = avcodec_send_packet( m_AVAudioDecCtx, pPacket );
	if ( result < 0 && result != AVERROR_EOF )
		return false;

	while ( true )
	{
		result = avcodec_receive_frame( m_AVAudioDecCtx, m_AVAudioFrame );
		if ( result == AVERROR(EAGAIN) || result == AVERROR_EOF )
		{
			m_bAudioDecoderDrained = ( result == AVERROR_EOF );
			return result != AVERROR_EOF || FlushAudioResampler();
		}
		if ( result < 0 || !QueueAudioFrame( m_AVAudioFrame ) )
			return false;
		av_frame_unref( m_AVAudioFrame );
	}
}


//-----------------------------------------------------------------------------
// Initializes the video material
//-----------------------------------------------------------------------------
bool CBinkMaterial::Init( const char *pMaterialName, const char *pFileName, VideoPlaybackFlags_t flags )
{
	SetResult( VideoResult::BAD_INPUT_PARAMETERS );
	AssertExitF( IS_NOT_EMPTY( pFileName ) );
	AssertExitF( m_bInitCalled == false );

	m_PlaybackFlags	= flags;

	OpenMovie( pFileName );	// Open up the Quicktime file

	if ( !m_bMovieInitialized )
	{
		return false;					// Something bad happened when we went to open
	}

	// Now we can properly setup our regenerators
//	m_TextureRegen.SetSourceGWorld( m_MovieGWorld, m_VideoFrameWidth, m_VideoFrameHeight );

	CreateProceduralTexture( pMaterialName );
	CreateProceduralMaterial( pMaterialName );

	// Start movie playback
	if ( !BITFLAGS_SET( m_PlaybackFlags, VideoPlaybackFlags::DONT_AUTO_START_VIDEO ) )
	{
		StartVideo();
	}

	m_bInitCalled = true;				// Look, if you only got one shot...

	return true;
}


void CBinkMaterial::Shutdown( void )
{
	if ( m_bMoviePlaying )
		StopVideo();
	Reset();
}


//-----------------------------------------------------------------------------
// Video playback state functions
//-----------------------------------------------------------------------------
bool CBinkMaterial::IsVideoReadyToPlay()
{
	return m_bMovieInitialized;
}


bool CBinkMaterial::IsVideoPlaying()
{
	return m_bMoviePlaying;
}


//-----------------------------------------------------------------------------
// Checks to see if the video has a new frame ready to be rendered and 
// downloaded into the texture and eventually display
//-----------------------------------------------------------------------------
bool CBinkMaterial::IsNewFrameReady( void )
{
	// Are we waiting to start playing the first frame? if so, tell them we are ready!
	if ( m_bMovieInitialized == true  )
	{
		return true;
	}

	// paused?
	if ( m_bMoviePaused )
	{
		return false;
	}

//	float curMovieTime;
	// Enough time passed to get to next frame??
/*	if ( curMovieTime < m_NextInterestingTimeToPlay )
	{
		// nope.. use the previous frame
		return false;
	}*/

	// we have a new frame we want then..
	return true;
}


bool CBinkMaterial::IsFinishedPlaying()
{
	return m_bMovieFinishedPlaying;
}


void CBinkMaterial::SetLooping( bool bLoopVideo )
{
	m_bLoopMovie = bLoopVideo;
}


bool CBinkMaterial::IsLooping()
{
	return m_bLoopMovie;
}


void CBinkMaterial::SetPaused( bool bPauseState )
{
	AUTO_LOCK( m_AudioMutex );
	if ( !m_bMoviePlaying || m_bMoviePaused == bPauseState )
	{
		Assert( m_bMoviePlaying );
		return;
	}

	if ( bPauseState )			// Pausing the movie?
	{
		m_MoviePauseTime = static_cast<float>( Plat_FloatTime() );
	}
	else  // unpausing the movie
	{
		m_NextInterestingTimeToPlay += Plat_FloatTime() - m_MoviePauseTime;
	}

	m_bMoviePaused = bPauseState;
}


bool CBinkMaterial::IsPaused()
{
	AUTO_LOCK( m_AudioMutex );
	return ( m_bMoviePlaying ) ? m_bMoviePaused : false;
}


// Begins playback of the movie
bool CBinkMaterial::StartVideo()
{
	if ( !m_bMovieInitialized )
	{
		Assert( false );
		SetResult( VideoResult::OPERATION_ALREADY_PERFORMED );
		return false;
	}

	m_NextInterestingTimeToPlay = Plat_FloatTime();

	// Transition to playing state
	{
		AUTO_LOCK( m_AudioMutex );
		m_bMovieInitialized = false;
		m_bMoviePlaying = true;
	}

	Update();

	return true;
}


// stops movie for good, frees resources, but retains texture & material of last frame rendered
bool CBinkMaterial::StopVideo()
{
	if ( !m_bMoviePlaying )
	{
		SetResult( VideoResult::OPERATION_OUT_OF_SEQUENCE );
		return false;
	}

	{
		AUTO_LOCK( m_AudioMutex );
		m_bMoviePlaying = false;
		m_bMoviePaused = false;
		m_bMovieFinishedPlaying = true;
	}

	// free resources
	CloseFile();

	SetResult( VideoResult::SUCCESS );
	return true;
}


//-----------------------------------------------------------------------------
// Purpose: Updates our scene
// Output : true = movie playing ok, false = time to end movie
// supposed to be: Returns true on a new frame of video being downloaded into the texture
//-----------------------------------------------------------------------------
bool CBinkMaterial::Update( void )
{
	AssertExitF( m_bMoviePlaying );

	if ( m_bMoviePaused )
		return false;

	if( m_NextInterestingTimeToPlay > Plat_FloatTime() )
		return false;

	m_NextInterestingTimeToPlay += m_MovieFrameDuration;

	while ( true )
	{
		int ret = avcodec_receive_frame( m_AVVideoDecCtx, m_AVFrame );
		if ( ret == 0 )
		{
			AVPixelFormat pixelFormat = static_cast<AVPixelFormat>( m_AVFrame->format );
			if ( pixelFormat == AV_PIX_FMT_YUV420P || pixelFormat == AV_PIX_FMT_YUVJ420P )
			{
				yuv420_rgb24_std( m_VideoFrameWidth, m_VideoFrameHeight,
					m_AVFrame->data[0], m_AVFrame->data[1], m_AVFrame->data[2],
					m_AVFrame->linesize[0], m_AVFrame->linesize[1], m_RGBData, m_VideoFrameWidth * 3, YCBCR_601 );
			}
			else if ( pixelFormat == AV_PIX_FMT_NV12 )
			{
				nv12_rgb24_std( m_VideoFrameWidth, m_VideoFrameHeight,
					m_AVFrame->data[0], m_AVFrame->data[1], m_AVFrame->linesize[0], m_AVFrame->linesize[1],
					m_RGBData, m_VideoFrameWidth * 3, YCBCR_601 );
			}
			else if ( pixelFormat == AV_PIX_FMT_NV21 )
			{
				nv21_rgb24_std( m_VideoFrameWidth, m_VideoFrameHeight,
					m_AVFrame->data[0], m_AVFrame->data[1], m_AVFrame->linesize[0], m_AVFrame->linesize[1],
					m_RGBData, m_VideoFrameWidth * 3, YCBCR_601 );
			}
			else
			{
				Warning( "Unsupported Bink pixel format: %s\n", av_get_pix_fmt_name( pixelFormat ) );
				av_frame_unref(m_AVFrame);
				StopVideo();
				SetResult( VideoResult::VIDEO_ERROR_OCCURED );
				return false;
			}

			av_frame_unref(m_AVFrame);
			Rect_t videoRect = { 0, 0, m_VideoFrameWidth, m_VideoFrameHeight };
			m_Texture->Download( &videoRect );
			SetResult( VideoResult::SUCCESS );
			return true;
		}

		if ( ret == AVERROR_EOF )
		{
			if ( m_bHasAudio && QueuedAudioBytes() > 0 )
				return false;
			StopVideo();
			return false;
		}
		if ( ret != AVERROR(EAGAIN) )
		{
			StopVideo();
			SetResult( VideoResult::VIDEO_ERROR_OCCURED );
			return false;
		}

		while ( true )
		{
			ret = av_read_frame( m_AVFmtCtx, m_AVPkt );
			if ( ret == AVERROR_EOF )
			{
				if ( m_AVAudioDecCtx && !m_bAudioDecoderDrained && !DecodeAudioPacket( nullptr ) )
				{
					StopVideo();
					SetResult( VideoResult::AUDIO_ERROR_OCCURED );
					return false;
				}
				ret = avcodec_send_packet( m_AVVideoDecCtx, nullptr );
				if ( ret < 0 && ret != AVERROR_EOF )
				{
					StopVideo();
					SetResult( VideoResult::VIDEO_ERROR_OCCURED );
					return false;
				}
				break;
			}
			if ( ret < 0 )
			{
				StopVideo();
				SetResult( VideoResult::FILE_ERROR_OCCURED );
				return false;
			}
			if ( m_AVPkt->stream_index == m_AVAudioStreamID )
			{
				const bool decoded = DecodeAudioPacket( m_AVPkt );
				av_packet_unref( m_AVPkt );
				if ( !decoded )
				{
					StopVideo();
					SetResult( VideoResult::AUDIO_ERROR_OCCURED );
					return false;
				}
				continue;
			}
			if ( m_AVPkt->stream_index != m_AVVideoStreamID )
			{
				av_packet_unref( m_AVPkt );
				continue;
			}

			ret = avcodec_send_packet( m_AVVideoDecCtx, m_AVPkt );
			av_packet_unref( m_AVPkt );
			if ( ret < 0 )
			{
				StopVideo();
				SetResult( VideoResult::VIDEO_ERROR_OCCURED );
				return false;
			}
			break;
		}
	}
}


//-----------------------------------------------------------------------------
// Returns the material
//-----------------------------------------------------------------------------
IMaterial *CBinkMaterial::GetMaterial()
{
	return m_Material;
}


//-----------------------------------------------------------------------------
// Returns the texcoord range
//-----------------------------------------------------------------------------
void CBinkMaterial::GetVideoTexCoordRange( float *pMaxU, float *pMaxV )
{
	AssertExit( pMaxU != nullptr && pMaxV != nullptr );

	if ( m_Texture == nullptr )		// no texture?
	{
		*pMaxU = *pMaxV = 1.0f;
		return;
	}

	*pMaxU = m_TexCordU;
	*pMaxV = m_TexCordV;
}


//-----------------------------------------------------------------------------
// Returns the frame size of the QuickTime Video in pixels 
//-----------------------------------------------------------------------------
void CBinkMaterial::GetVideoImageSize( int *pWidth, int *pHeight )
{
	Assert( pWidth != nullptr && pHeight != nullptr );

	*pWidth  = m_VideoFrameWidth;
	*pHeight = m_VideoFrameHeight;
}

void CBinkMaterial::GetTextureSize( int *pWidth, int *pHeight )
{
	Assert( pWidth != nullptr && pHeight != nullptr );
	*pWidth = ( m_Texture != nullptr ) ? m_Texture->GetActualWidth() : m_VideoFrameWidth;
	*pHeight = ( m_Texture != nullptr ) ? m_Texture->GetActualHeight() : m_VideoFrameHeight;
}


float CBinkMaterial::GetVideoDuration()
{
	return m_QTMovieDurationinSec;
}


int CBinkMaterial::GetFrameCount()
{
	return m_QTMovieFrameCount;
}


//-----------------------------------------------------------------------------
// Sets the frame for an QuickTime  Material (use instead of SetTime)
//-----------------------------------------------------------------------------
bool CBinkMaterial::SetFrame( int FrameNum )
{
	if ( !m_bMoviePlaying )
	{
		Assert( false );
		SetResult( VideoResult::OPERATION_OUT_OF_SEQUENCE );
		return false;
	}

	float	theTime = (float) FrameNum * m_QTMovieFrameRate.GetFPS();
	return SetTime( theTime );
}


int CBinkMaterial::GetCurrentFrame()
{
	AssertExitV( m_bMoviePlaying, -1 );

	float curTime; // = m_bMoviePaused ? m_MoviePauseTime : GetMovieTime( m_QTMovie, nullptr );

	return curTime / m_QTMovieFrameRate.GetUnitsPerFrame();
}


float CBinkMaterial::GetCurrentVideoTime()
{
	AssertExitV( m_bMoviePlaying, -1.0f );

	float curTime; // = m_bMoviePaused ? m_MoviePauseTime : GetMovieTime( m_QTMovie, nullptr );

	return curTime / m_QTMovieFrameRate.GetUnitsPerSecond();
}


bool CBinkMaterial::SetTime( float flTime )
{
	AssertExitF( m_bMoviePlaying );
	AssertExitF( flTime >= 0 && flTime < m_QTMovieDurationinSec );

	float newTime = ( flTime * m_QTMovieFrameRate.GetUnitsPerSecond() + 0.5f) ;

	clamp( newTime,  m_MovieFirstFrameTime, m_QTMovieDuration ); 

	// Are we paused?
	if ( m_bMoviePaused )
	{
		m_MoviePauseTime = newTime;
		return true;
	}

	float curMovieTime; // = GetMovieTime( m_QTMovie, nullptr );

	// Don't stop and reset movie if we are within 1 frame of the requested time
	if ( newTime <= curMovieTime - m_QTMovieFrameRate.GetUnitsPerFrame() || newTime >= curMovieTime + m_QTMovieFrameRate.GetUnitsPerFrame() )
	{
		// Reset the movie to the requested time
/*		StopMovie( m_QTMovie );
		SetMovieTimeValue( m_QTMovie, newTime );
		StartMovie( m_QTMovie );

		Assert( GetMoviesError() == noErr );*/
	}

	return true;
}


//-----------------------------------------------------------------------------
// Initializes, shuts down the procedural texture
//-----------------------------------------------------------------------------
void CBinkMaterial::CreateProceduralTexture( const char *pTextureName )
{
	AssertIncRange( m_VideoFrameWidth, cMinVideoFrameWidth, cMaxVideoFrameWidth );
	AssertIncRange( m_VideoFrameHeight, cMinVideoFrameHeight, cMaxVideoFrameHeight );
	AssertStr( pTextureName );

	// Either make the texture the same dimensions as the video,
	// or choose power-of-two textures which are at least as big as the video
	bool actualSizeTexture = BITFLAGS_SET( m_PlaybackFlags, VideoPlaybackFlags::TEXTURES_ACTUAL_SIZE );

	int nWidth  = ( actualSizeTexture ) ? ALIGN_VALUE( m_VideoFrameWidth, TEXTURE_SIZE_ALIGNMENT ) : ComputeGreaterPowerOfTwo( m_VideoFrameWidth ); 
	int nHeight = ( actualSizeTexture ) ? ALIGN_VALUE( m_VideoFrameHeight, TEXTURE_SIZE_ALIGNMENT ) : ComputeGreaterPowerOfTwo( m_VideoFrameHeight ); 

	// Use an explicit opaque alpha channel; GL may promote RGB888 internally.
	m_Texture.InitProceduralTexture( pTextureName, "VideoCacheTextures", nWidth, nHeight, 
				IMAGE_FORMAT_RGBA8888, TEXTUREFLAGS_CLAMPS | TEXTUREFLAGS_CLAMPT | TEXTUREFLAGS_NOMIP |
				TEXTUREFLAGS_PROCEDURAL | TEXTUREFLAGS_SINGLECOPY | TEXTUREFLAGS_NOLOD );

	// Use this to get the updated frame from the remote connection	
	m_Texture->SetTextureRegenerator( &m_TextureRegen /* , false */ );

	// compute the texcoords
	int nTextureWidth = m_Texture->GetActualWidth();
	int nTextureHeight = m_Texture->GetActualHeight();

	m_TexCordU = ( nTextureWidth > 0 ) ? (float) m_VideoFrameWidth / (float) nTextureWidth : 0.0f;
	m_TexCordV = ( nTextureHeight > 0 ) ? (float) m_VideoFrameHeight / (float) nTextureHeight : 0.0f;
}


void CBinkMaterial::DestroyProceduralTexture()
{
	if ( m_Texture != nullptr )
	{
		// DO NOT Call release on the Texture Regenerator, as it will destroy this object!  bad bad bad
		// instead we tell it to assign a NULL regenerator and flag it to not call release
		m_Texture->SetTextureRegenerator( nullptr /*, false */ );
		// Texture, texture go away...
		m_Texture.Shutdown( true );
	}
}


//-----------------------------------------------------------------------------
// Initializes, shuts down the procedural material
//-----------------------------------------------------------------------------
void CBinkMaterial::CreateProceduralMaterial( const char *pMaterialName )
{
	// create keyvalues if necessary
	KeyValues *pVMTKeyValues = new KeyValues( "UnlitGeneric" );
	{
		pVMTKeyValues->SetString( "$basetexture", m_Texture->GetName() );
		pVMTKeyValues->SetInt( "$ignorez", 1 );
		pVMTKeyValues->SetInt( "$nofog", 1 );
		pVMTKeyValues->SetInt( "$no_fullbright", 1 );
		pVMTKeyValues->SetInt( "$nocull", 1 );
		pVMTKeyValues->SetInt( "$nolod", 1 );
		pVMTKeyValues->SetInt( "$nomip", 1 );
		pVMTKeyValues->SetInt( "$gammacolorread", 0 );
	}

	// FIXME: gak, this is backwards.  Why doesn't the material just see that it has a funky basetexture?
	m_Material.Init( pMaterialName, pVMTKeyValues );
	m_Material->Refresh();
}


void CBinkMaterial::DestroyProceduralMaterial()
{
	// Store the internal material pointer for later use
	IMaterial *pMaterial = m_Material;
	m_Material.Shutdown();
	materials->UncacheUnusedMaterials();

	// Now be sure to free that material because we don't want to reference it again later, we'll recreate it!
	if ( pMaterial != nullptr )
	{
		pMaterial->DeleteIfUnreferenced();
	}
}



//-----------------------------------------------------------------------------
// Opens a movie file using quicktime
//-----------------------------------------------------------------------------
void CBinkMaterial::OpenMovie( const char *theMovieFileName )
{
	AssertExit( IS_NOT_EMPTY( theMovieFileName ) );
/*
	// Set graphics port 
#if defined ( WIN32 )
	SetGWorld ( (CGrafPtr) GetNativeWindowPort( nil ), nil ); 
#elif defined ( OSX		)
	SetGWorld( nil, nil );
#endif
*/

	SetFileName( theMovieFileName );

	if (avformat_open_input(&m_AVFmtCtx, theMovieFileName, NULL, NULL) < 0)
 	{
		Warning("Could not open source file %s\n", theMovieFileName);
 		SetResult( VideoResult::FILE_ERROR_OCCURED ) ;
		CloseFile();
		return;
	}

	if (avformat_find_stream_info(m_AVFmtCtx, NULL) < 0)
	{
		Warning("Could not find stream information for %s\n", theMovieFileName);
 		SetResult( VideoResult::FILE_ERROR_OCCURED ) ;
		CloseFile();
		return;
	}

	if (open_codec_context(&m_AVVideoStreamID, &m_AVVideoDecCtx, m_AVFmtCtx, AVMEDIA_TYPE_VIDEO) == 0)
	{
		m_AVVideoStream = m_AVFmtCtx->streams[m_AVVideoStreamID];

		m_VideoFrameWidth = m_AVVideoDecCtx->width;
		m_VideoFrameHeight = m_AVVideoDecCtx->height;
		m_AVPixFormat = m_AVVideoDecCtx->pix_fmt;
	m_RGBData = static_cast<uint8_t *>( calloc( static_cast<size_t>(m_VideoFrameWidth) * m_VideoFrameHeight * 3, 1 ) );

		if ( !m_RGBData )
		{
			Warning("Could not allocate raw video buffer\n", theMovieFileName);
 			SetResult( VideoResult::SYSTEM_ERROR_OCCURED ) ;
			CloseFile();
			return;
		}
	}
	else
	{
			Warning("open_codec_context failed for %s\n", theMovieFileName);
 			SetResult( VideoResult::SYSTEM_ERROR_OCCURED ) ;
			CloseFile();
			return;
	}

	if ( !BITFLAGS_SET( m_PlaybackFlags, VideoPlaybackFlags::NO_AUDIO ) &&
		open_codec_context( &m_AVAudioStreamID, &m_AVAudioDecCtx, m_AVFmtCtx, AVMEDIA_TYPE_AUDIO ) == 0 )
	{
		m_AVAudioStream = m_AVFmtCtx->streams[m_AVAudioStreamID];
		m_bHasAudio = true;
		Msg( "Bink audio: %d Hz, %d channels, %s\n", m_AVAudioDecCtx->sample_rate,
			m_AVAudioDecCtx->ch_layout.nb_channels, av_get_sample_fmt_name( m_AVAudioDecCtx->sample_fmt ) );
	}

	AVRational frameRate = av_guess_frame_rate( m_AVFmtCtx, m_AVVideoStream, nullptr );
	if ( frameRate.num <= 0 || frameRate.den <= 0 )
		frameRate = AVRational{ 30, 1 };
	const double framesPerSecond = av_q2d( frameRate );
	m_MovieFrameDuration = 1.0 / framesPerSecond;
	m_QTMovieFrameRate.SetFPS( static_cast<float>( framesPerSecond ) );
	if ( m_AVVideoStream->duration != AV_NOPTS_VALUE )
		m_QTMovieDurationinSec = static_cast<float>( m_AVVideoStream->duration * av_q2d( m_AVVideoStream->time_base ) );
	else if ( m_AVFmtCtx->duration != AV_NOPTS_VALUE )
		m_QTMovieDurationinSec = static_cast<float>( m_AVFmtCtx->duration / static_cast<double>( AV_TIME_BASE ) );
	m_QTMovieFrameCount = ( m_QTMovieDurationinSec > 0.0f ) ? static_cast<int>( m_QTMovieDurationinSec * framesPerSecond + 0.5 ) : 0;
	m_TextureRegen.SetSourceImage( m_RGBData, m_VideoFrameWidth, m_VideoFrameHeight );
	Msg( "Bink video: %dx%d at %.3f FPS\n", m_VideoFrameWidth, m_VideoFrameHeight, framesPerSecond );

#if 0
	Handle	MovieFileDataRef = nullptr;
	OSType	MovieFileDataRefType = 0;

	CFStringRef	imageStrRef = CFStringCreateWithCString ( NULL,  theQTMovieFileName, 0 ); 
	AssertExitFunc( imageStrRef != nullptr, SetResult( VideoResult::SYSTEM_ERROR_OCCURED ) );

	OSErr status = QTNewDataReferenceFromFullPathCFString( imageStrRef, (QTPathStyle) kQTNativeDefaultPathStyle, 0, &MovieFileDataRef, &MovieFileDataRefType );
	AssertExitFunc( status == noErr, SetResult( VideoResult::FILE_ERROR_OCCURED ) );

	CFRelease( imageStrRef );

//	status = NewMovieFromDataRef( &m_QTMovie, newMovieActive, nil, MovieFileDataRef, MovieFileDataRefType );
//	SAFE_DISPOSE_HANDLE( MovieFileDataRef );

	if ( status != noErr )
	{
		Assert( false );
		Reset();
		SetResult( VideoResult::VIDEO_ERROR_OCCURED );
		return;
	}

	// disabling audio?
	if ( BITFLAGS_SET( m_PlaybackFlags, VideoPlaybackFlags::NO_AUDIO ) )
	{
		m_bHasAudio = false;
	}
	else
	{
		// does movie have audio?
//		Track audioTrack = GetMovieIndTrackType( m_QTMovie, 1, SoundMediaType, movieTrackMediaType );
//		m_bHasAudio = ( audioTrack != nullptr );
	}

	// Now we need to extract the time info from the QT Movie 
//	m_QTMovieTimeScale	= GetMovieTimeScale( m_QTMovie );
//	m_QTMovieDuration	= GetMovieDuration( m_QTMovie );

	// compute movie duration	
/*	m_QTMovieDurationinSec = float ( double( m_QTMovieDuration ) / double( m_QTMovieTimeScale ) );
	if ( !MovieGetStaticFrameRate( m_QTMovie, m_QTMovieFrameRate ) )
	{
		WarningAssert( "Couldn't Get Frame Rate" );
	}*/
	
	// and get an estimated frame count
	m_QTMovieFrameCount = m_QTMovieDuration / m_QTMovieTimeScale;
	
	if ( m_QTMovieFrameRate.GetUnitsPerSecond() == m_QTMovieTimeScale )
	{
		m_QTMovieFrameCount = m_QTMovieDuration / m_QTMovieFrameRate.GetUnitsPerFrame();
	}
	else
	{
		m_QTMovieFrameCount = (int) ( (float) m_QTMovieDurationinSec * m_QTMovieFrameRate.GetFPS() + 0.5f );
	}
	
	// what size do we set the output rect to?
//	GetMovieNaturalBoundsRect(m_QTMovie, &m_QTMovieRect);
	
	m_VideoFrameWidth = m_QTMovieRect.right;
	m_VideoFrameHeight = m_QTMovieRect.bottom;
	
	// Sanity check...
	AssertExitFunc( m_QTMovieRect.top == 0 && m_QTMovieRect.left == 0 &&
					m_QTMovieRect.right >= cMinVideoFrameWidth && m_QTMovieRect.right <= cMaxVideoFrameWidth && 
					m_QTMovieRect.bottom >= cMinVideoFrameHeight && m_QTMovieRect.bottom <= cMaxVideoFrameHeight &&
					m_QTMovieRect.right % 4 == 0,
					SetResult( VideoResult::VIDEO_ERROR_OCCURED ) );

	// Setup the QuiuckTime Graphics World for the Movie
/*	status = QTNewGWorld( &m_MovieGWorld, k32BGRAPixelFormat, &m_QTMovieRect, nil, nil, 0 );
	AssertExit( status == noErr );

	// Setup the playback gamma according to the convar
	SetGWorldDecodeGamma( m_MovieGWorld, VideoPlaybackGamma::USE_GAMMA_CONVAR );

	// Assign the GWorld to this movie
	SetMovieGWorld( m_QTMovie, m_MovieGWorld, nil );

	// Setup Movie Audio, unless suppressed
	if ( !CreateMovieAudioContext( m_bHasAudio, m_QTMovie, &m_AudioContext, true, &m_CurrentVolume ) )
	{
		SetResult( VideoResult::AUDIO_ERROR_OCCURED );
		WarningAssert( "Couldn't Set Audio" );
	}
	
	// Get the time of the first frame
	OSType	qTypes[1] = { VisualMediaCharacteristic };
	short	qFlags = nextTimeStep | nextTimeEdgeOK;			// use nextTimeStep instead of nextTimeMediaSample for MPEG 1-2 compatibility
		
	GetMovieNextInterestingTime( m_QTMovie, qFlags, 1, qTypes, (TimeValue) 0, fixed1, &m_MovieFirstFrameTime, NULL );
	AssertExitFunc( GetMoviesError() == noErr, SetResult( VideoResult::VIDEO_ERROR_OCCURED ) );

	// Preroll the movie
	if ( BITFLAGS_SET( m_PlaybackFlags, VideoPlaybackFlags::PRELOAD_VIDEO ) )
	{
		Fixed playRate = GetMoviePreferredRate( m_QTMovie );
		status = PrerollMovie( m_QTMovie, m_MovieFirstFrameTime, playRate );
		AssertExitFunc( status == noErr, SetResult( VideoResult::VIDEO_ERROR_OCCURED ) );
	}*/
	
#endif
	m_bMovieInitialized = true;
}


void CBinkMaterial::CloseFile()
{
	if ( m_AVPkt )
		av_packet_unref( m_AVPkt );
	if ( m_AVFrame )
		av_frame_unref( m_AVFrame );
	if ( m_AVAudioFrame )
		av_frame_unref( m_AVAudioFrame );
	ClearAudioQueue();
	swr_free( &m_SwrContext );
	av_freep( &m_AVVideoData[0] );
	avcodec_free_context( &m_AVVideoDecCtx );
	avcodec_free_context( &m_AVAudioDecCtx );
	avformat_close_input( &m_AVFmtCtx );
	SAFE_FREE( m_RGBData );
	m_AVVideoStream = nullptr;
	m_AVAudioStream = nullptr;
	m_AVVideoStreamID = -1;
	m_AVAudioStreamID = -1;
	m_AVPixFormat = AV_PIX_FMT_NONE;
	m_bHasAudio = false;
	m_bAudioOutputConfigured = false;
	m_bAudioDecoderDrained = false;
	m_AudioOutputFormat = AV_SAMPLE_FMT_NONE;
	m_TextureRegen.SetSourceImage( nullptr, 0, 0 );

	SetFileName( nullptr );
}
