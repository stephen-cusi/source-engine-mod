//========= Copyright Valve Corporation, All rights reserved. ============//
//
// Purpose: 
//
// $NoKeywords: $
//
//===========================================================================//
#include "tier2/riff.h"
#include "snd_io.h"
#include "filesystem.h"
#include "filesystem_engine.h"
#include "tier1/strtools.h"
#include "tier1/utlsymbol.h"

// memdbgon must be the last include file in a .cpp file!!!
#include "tier0/memdbgon.h"

//-----------------------------------------------------------------------------
// Purpose: Implements Audio IO on the engine's COMMON filesystem
//-----------------------------------------------------------------------------
class COM_IOReadBinary : public IFileReadBinary
{
public:
	intp open( const char *pFileName );
	int read( void *pOutput, int size, intp file );
	void seek( intp file, int pos );
	unsigned int tell( intp file );
	unsigned int size( intp file );
	void close( intp file );
};


// prepend sound/ to the filename -- all sounds are loaded from the sound/ directory
intp COM_IOReadBinary::open( const char *pFileName )
{
	char namebuffer[512];
	FileHandle_t hFile;
	Q_strncpy( namebuffer, "sound", sizeof( namebuffer ) );

	// the server is sending back sound names with slashes in front... 
	if ( pFileName[0] != '/' && pFileName[0] != '\\' )
	{
		Q_strncat( namebuffer, "/", sizeof( namebuffer ), COPY_ALL_CHARACTERS );
	}

	Q_strncat( namebuffer, pFileName, sizeof( namebuffer ), COPY_ALL_CHARACTERS );

	hFile = g_pFileSystem->Open( namebuffer, "rb", "GAME" );

	return (intp)hFile;
}

int COM_IOReadBinary::read( void *pOutput, int size, intp file )
{
	if ( !file )
		return 0;

	return g_pFileSystem->Read( pOutput, size, (FileHandle_t)file );
}

void COM_IOReadBinary::seek( intp file, int pos )
{
	if ( !file )
		return;

	g_pFileSystem->Seek( (FileHandle_t)file, pos, FILESYSTEM_SEEK_HEAD );
}

unsigned int COM_IOReadBinary::tell( intp file )
{
	if ( !file )
		return 0;

	return g_pFileSystem->Tell( (FileHandle_t)file );
}

unsigned int COM_IOReadBinary::size( intp file )
{
	if (!file)
		return 0;

	return g_pFileSystem->Size( (FileHandle_t)file );
}

void COM_IOReadBinary::close( intp file )
{
	if (!file)
		return;

	g_pFileSystem->Close( (FileHandle_t)file );
}

static COM_IOReadBinary io;
IFileReadBinary *g_pSndIO = &io;

void MaybeReportMissingWav( const char *pName )
{
	static CUtlSymbolTable wavErrors;
	if ( wavErrors.Find( pName ) != UTL_INVAL_SYMBOL )
		return;

	if ( g_pFullFileSystem->FileExists( pName ) )
		DevWarning( "Bad Audio file '%s'\n", pName );
	else
		DevWarning( "Missing wav file '%s'\n", pName );
	wavErrors.AddString( pName );
}

bool Audio_IsMP3( const char *pName )
{
	const size_t length = strlen( pName );
	return length > 4 && !Q_strnicmp( pName + length - 4, ".mp3", 4 );
}

int Audio_ADPCMSampleCount( int nChannels, int nSamplesPerBlock, int nBytes )
{
	if ( nChannels < 1 || nChannels > 2 || nSamplesPerBlock < 2 || nBytes <= 0 )
		return 0;

	const int blockSize = ( ( nSamplesPerBlock - 2 ) * nChannels ) / 2 + 7 * nChannels;
	const int blockRemainder = nBytes % blockSize;
	int samples = ( nBytes / blockSize ) * nSamplesPerBlock;
	if ( blockRemainder >= 7 * nChannels )
		samples += nSamplesPerBlock - ( ( blockSize - blockRemainder ) * 2 ) / nChannels;
	return samples;
}

float Audio_GetWaveDuration( const char *pName )
{
	if ( IsX360() )
		return 0.0f;

	InFileRIFF riff( pName, *g_pSndIO );
	const unsigned int chunkBytes = riff.RIFFSize();
	const unsigned int riffHeaderBytes = 12;
	if ( chunkBytes < 8 || chunkBytes > 0x7fffffff - riffHeaderBytes || riff.RIFFName() != RIFF_WAVE )
	{
		MaybeReportMissingWav( pName );
		return 0.0f;
	}

	// A PCM fmt chunk has 16 bytes. Microsoft ADPCM adds the extension size
	// and the samples-per-block field at bytes 16 and 18 respectively.
	const int pcmFormatBytes = 16;
	const int adpcmFormatTag = 2;
	struct
	{
		uint16 tag;
		uint16 channels;
		uint32 sampleRate;
		uint32 averageByteRate;
		uint16 blockAlign;
		uint16 bitsPerSample;
		uint16 extraBytes;
		uint16 samplesPerBlock;
	} format = {};
	static_assert( sizeof( format ) == 20, "Unexpected WAVE format layout" );
	int formatBytes = 0;
	int dataBytes = 0;
	const unsigned int fileBytes = chunkBytes + riffHeaderBytes;
	for ( IterateRIFF walk( riff, chunkBytes ); walk.ChunkAvailable(); walk.ChunkNext() )
	{
		const unsigned int payload = walk.ChunkFilePosition() + 8u;
		const unsigned int size = walk.ChunkSize();
		if ( payload > fileBytes || size > fileBytes - payload )
			return 0.0f;

		if ( walk.ChunkName() == WAVE_FMT && size >= pcmFormatBytes )
			formatBytes = walk.ChunkReadPartial( &format, MIN( size, (unsigned int)sizeof( format ) ) );
		else if ( walk.ChunkName() == WAVE_DATA )
			dataBytes = size;

		if ( formatBytes >= pcmFormatBytes && dataBytes > 0 )
			break;
	}

	const int tag = LittleWord( format.tag );
	const int channels = LittleWord( format.channels );
	const uint32 sampleRate = LittleDWord( format.sampleRate );
	const int blockAlign = LittleWord( format.blockAlign );
	if ( formatBytes < pcmFormatBytes || !tag || !channels || !sampleRate || !blockAlign )
		return 0.0f;

	if ( tag == adpcmFormatTag )
	{
		if ( formatBytes < sizeof( format ) )
			return 0.0f;
		return float( Audio_ADPCMSampleCount( channels, LittleWord( format.samplesPerBlock ), dataBytes ) ) / sampleRate;
	}
	return float( dataBytes / blockAlign ) / sampleRate;
}

