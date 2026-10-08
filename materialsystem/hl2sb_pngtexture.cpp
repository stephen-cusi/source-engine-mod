//========= Copyright Valve Corporation, All rights reserved. ============//
//
// Purpose: GMod-style raw image textures (.png / .jpg / .tga / .bmp).
//
//          See hl2sb_pngtexture.h.  The image is decoded with stb_image (a
//          single-header decoder already vendored under thirdparty/, used by
//          gameui/PNGImagePanel.cpp) and expanded into a mip chain, which is
//          then uploaded through the ordinary procedural-texture path.
//
//=======================================================================================//

#include <stdlib.h>
#include <string.h>

#include "basetypes.h"
#include "tier0/basetypes.h"
#include "tier0/dbg.h"
#include "tier0/threadtools.h"
#include "tier1/utlvector.h"
#include "utlbuffer.h"
#include "convar.h"
#include "filesystem.h"
#include "bitmap/imageformat.h"
#include "pixelwriter.h"
#include "materialsystem/itexture.h"
#include "materialsystem/imaterialsystem.h"
#include "vtf/vtf.h"
#include "tier1/checksum_crc.h"
#include "hl2sb_pngtexture.h"

// stb_image is a single-header decoder.  Disable what we do not need; PNG/JPG/
// TGA/BMP stay enabled so a GMod material can point at any of them.
#define STB_IMAGE_IMPLEMENTATION
#define STBI_NO_STDIO
#define STBI_NO_HDR
#define STBI_NO_LINEAR
#define STBI_NO_PSD
#define STBI_NO_GIF
#define STBI_NO_PIC
#define STBI_NO_PNM
#include "../thirdparty/stb/stb_image.h"

#include "tier0/memdbgon.h"

//-----------------------------------------------------------------------------
// Extensions we are willing to load straight from disk.  GMod's resources.dll
// whitelist is png/jpg/jpeg/tga/cache (content sniffing decides the real
// decoder); .bmp stays as a harmless superset for local content.
//-----------------------------------------------------------------------------
static const char *s_pImageExtensions[] =
{
	".png",
	".jpg",
	".jpeg",
	".tga",
	".cache",
	".bmp",
};

bool HL2SB_IsImageFileName( const char *pFileName )
{
	if ( !pFileName || !pFileName[0] )
		return false;

	for ( int i = 0; i < ARRAYSIZE( s_pImageExtensions ); ++i )
	{
		int nNameLen = Q_strlen( pFileName );
		int nExtLen = Q_strlen( s_pImageExtensions[i] );
		if ( nNameLen > nExtLen && !Q_stricmp( pFileName + nNameLen - nExtLen, s_pImageExtensions[i] ) )
			return true;
	}

	return false;
}

//-----------------------------------------------------------------------------
// HL2SB (2026-10-03): set to 1 before loading an addon to trace every image
// texture resolution: thread id, raw requested name, dictionary verdict and
// whether the pixels came from the on-disk cache or a fresh decode.  The
// engine log alone (the "[HL2SB] image texture" Msg in LoadTexture) cannot
// distinguish these, which is how 35/35 PNGs double-decoded for two rounds
// without the funnel becoming visible.
//-----------------------------------------------------------------------------
ConVar hl2sb_image_debug(
	"hl2sb_image_debug", "0", 0,
	"Trace image texture loads (thread id, name, dictionary hit, disk cache vs decode)." );

//-----------------------------------------------------------------------------
// "materials/<name>" is how texture files are addressed through the search
// paths (CTexture::GetFilename does the same thing with a .vtf suffix).
//-----------------------------------------------------------------------------
static bool HL2SB_ImageFileExists( const char *pLogicalName )
{
	char szPath[MAX_PATH];

	// GMod's CImage::LoadImageFromFile probes "spawnicons" paths MOD-first
	// (their client writes generated spawn icons into garrysmod/materials);
	// everything else is GAME first.
	const char *pPathIDs[2] = { "GAME", "MOD" };
	const bool bSpawnIcons = !Q_strnicmp( pLogicalName, "spawnicons", 10 );
	const int nFirst = bSpawnIcons ? 1 : 0;

	for ( int i = 0; i < 2; ++i )
	{
		const char *pPathID = pPathIDs[ ( nFirst + i ) % 2 ];

		Q_snprintf( szPath, sizeof( szPath ), "materials/%s", pLogicalName );
		if ( g_pFullFileSystem->FileExists( szPath, pPathID ) )
			return true;

		Q_strncpy( szPath, pLogicalName, sizeof( szPath ) );
		if ( g_pFullFileSystem->FileExists( szPath, pPathID ) )
			return true;
	}

	return false;
}

//-----------------------------------------------------------------------------
// "!<digits><path>" parameterised image texture names (GMod's FreeImage
// material naming).  The digit string is exactly HL2SB_IMAGE_PARAMS_DIGITS
// chars of '0'..'9'; everything after it is the plain path.
//-----------------------------------------------------------------------------
bool HL2SB_SplitImageTextureName( const char *pTextureName,
	char *pOutDigits, int nOutDigitsSize,
	char *pOutPath, int nOutPathSize )
{
	if ( pOutDigits && nOutDigitsSize > 0 )
		pOutDigits[0] = '\0';
	if ( pOutPath && nOutPathSize > 0 )
		pOutPath[0] = '\0';

	if ( !pTextureName || pTextureName[0] != '!' )
	{
		if ( pOutPath )
			Q_strncpy( pOutPath, pTextureName ? pTextureName : "", nOutPathSize );
		return false;
	}

	const char *pDigits = pTextureName + 1;
	for ( int i = 0; i < HL2SB_IMAGE_PARAMS_DIGITS; ++i )
	{
		if ( pDigits[i] < '0' || pDigits[i] > '9' )
		{
			// Not our shape - hand the whole name back untouched.
			if ( pOutPath )
				Q_strncpy( pOutPath, pTextureName, nOutPathSize );
			return false;
		}
	}

	if ( pOutDigits && nOutDigitsSize > HL2SB_IMAGE_PARAMS_DIGITS )
	{
		Q_memcpy( pOutDigits, pDigits, HL2SB_IMAGE_PARAMS_DIGITS );
		pOutDigits[ HL2SB_IMAGE_PARAMS_DIGITS ] = '\0';
	}
	if ( pOutPath )
		Q_strncpy( pOutPath, pDigits + HL2SB_IMAGE_PARAMS_DIGITS, nOutPathSize );
	return true;
}

unsigned int HL2SB_ImageTextureFlags( const char *pDigits )
{
	// Default mirrors GMod's CResources::CreateMaterialFromTextureFile.
	unsigned int nFlags = TEXTUREFLAGS_SINGLECOPY | TEXTUREFLAGS_PROCEDURAL |
		TEXTUREFLAGS_NOLOD | TEXTUREFLAGS_NOMIP |
		TEXTUREFLAGS_CLAMPS | TEXTUREFLAGS_CLAMPT | TEXTUREFLAGS_POINTSAMPLE;

	char szDigits[ HL2SB_IMAGE_PARAMS_DIGITS + 1 ] = { 0 };
	if ( pDigits )
		Q_strncpy( szDigits, pDigits, sizeof( szDigits ) );

	// digits[3] "mips": clear NOMIP | NOLOD so the engine builds a mip chain.
	if ( szDigits[3] != '0' && szDigits[3] != '\0' )
		nFlags &= ~(unsigned)( TEXTUREFLAGS_NOMIP | TEXTUREFLAGS_NOLOD );
	// digits[4] "noclamp": no CLAMPS | CLAMPT.
	if ( szDigits[4] != '0' && szDigits[4] != '\0' )
		nFlags &= ~(unsigned)( TEXTUREFLAGS_CLAMPS | TEXTUREFLAGS_CLAMPT );
	// digits[5] "smooth": linear filtering instead of point sampling.
	if ( szDigits[5] != '0' && szDigits[5] != '\0' )
		nFlags &= ~(unsigned)( TEXTUREFLAGS_POINTSAMPLE | TEXTUREFLAGS_NOLOD );

	return nFlags;
}

bool HL2SB_ResolveImageTexture( const char *pTextureName, char *pOutLogicalName, int nOutLogicalNameSize )
{
	if ( !pTextureName || !pTextureName[0] || !pOutLogicalName )
		return false;

	// Strip a "!<digits>" parameter prefix first; the path under it is what
	// names the actual file.
	char szDigits[ HL2SB_IMAGE_PARAMS_DIGITS + 1 ];
	char szBare[MAX_PATH];
	HL2SB_SplitImageTextureName( pTextureName, szDigits, sizeof( szDigits ), szBare, sizeof( szBare ) );

	// Texture names are addressed relative to materials/, but accept a name
	// that already carries the prefix (vgui passes some through verbatim).
	const char *pName = szBare;
	if ( !Q_strnicmp( pName, "materials/", 10 ) )
		pName += 10;
	if ( !Q_strnicmp( pName, "materials\\", 10 ) )
		pName += 10;

	if ( HL2SB_IsImageFileName( pName ) )
	{
		if ( !HL2SB_ImageFileExists( pName ) )
			return false;

		Q_strncpy( pOutLogicalName, pName, nOutLogicalNameSize );
		return true;
	}

	// No extension (or a non-image one such as .vtf): probe for an image beside it.
	for ( int i = 0; i < ARRAYSIZE( s_pImageExtensions ); ++i )
	{
		char szCandidate[MAX_PATH];
		Q_snprintf( szCandidate, sizeof( szCandidate ), "%s%s", pName, s_pImageExtensions[i] );
		if ( HL2SB_ImageFileExists( szCandidate ) )
		{
			Q_strncpy( pOutLogicalName, szCandidate, nOutLogicalNameSize );
			return true;
		}
	}

	return false;
}

//-----------------------------------------------------------------------------
// Holds the decoded image and paints it into whatever region the engine
// actually allocated for the texture.
//
// IMPORTANT (issue #41): every dimension used for *writing* comes from the
// IVTFTexture (or the pSubRect the engine hands us), never from the source
// image.  CTexture::ReconstructProceduralBits() runs ComputeActualSize()
// first, which shrinks the texture for mat_picmip, texture-group dimension
// limits and the hardware max texture size - so the allocated buffer is
// routinely *smaller* than the PNG we decoded.  The previous version wrote
// m_nWidth x m_nHeight pixels no matter what, walked off the end of the VTF
// buffer, and after a handful of images corrupted the bionic heap badly enough
// that the GL driver jumped through a clobbered function pointer in
// libtogl.so.
//-----------------------------------------------------------------------------
class CImageTextureRegenerator : public ITextureRegenerator
{
public:
	CImageTextureRegenerator( int nWidth, int nHeight ) :
		m_nWidth( nWidth ), m_nHeight( nHeight )
	{
		m_RGBA.SetSize( nWidth * nHeight * 4 );
	}

	// HL2SB (2026-10-03): pre-decoded bits (disk-cache hit or stb decode) are
	// handed in by the caller instead of the regenerator zero-filling first.
	CImageTextureRegenerator( int nWidth, int nHeight, const unsigned char *pBits ) :
		m_nWidth( nWidth ), m_nHeight( nHeight )
	{
		m_RGBA.SetSize( nWidth * nHeight * 4 );
		Q_memcpy( m_RGBA.Base(), pBits, (size_t)nWidth * nHeight * 4 );
	}

	unsigned char *GetImageBits() { return m_RGBA.Base(); }

	virtual void RegenerateTextureBits( ITexture *pTexture, IVTFTexture *pVTFTexture, Rect_t *pSubRect )
	{
		if ( !pVTFTexture )
			return;

		const int nTexW = pVTFTexture->Width();
		const int nTexH = pVTFTexture->Height();
		const int nTexD = pVTFTexture->Depth();
		if ( nTexW <= 0 || nTexH <= 0 || nTexD <= 0 )
			return;

		if ( m_nWidth <= 0 || m_nHeight <= 0 )
			return;
		if ( m_RGBA.Count() < m_nWidth * m_nHeight * 4 )
			return;

		// Fill region in mip-0 texels, clamped inside the texture.  For the
		// normal (non-partial) download the engine passes m_dimsActual here;
		// that is exactly what pSubRect is for.
		int nFillX = 0, nFillY = 0, nFillW = nTexW, nFillH = nTexH;
		if ( pSubRect && pSubRect->width > 0 && pSubRect->height > 0 )
		{
			nFillX = Clamp( pSubRect->x, 0, nTexW );
			nFillY = Clamp( pSubRect->y, 0, nTexH );
			nFillW = Clamp( pSubRect->width, 1, nTexW - nFillX );
			nFillH = Clamp( pSubRect->height, 1, nTexH - nFillY );
		}

		// Say so when the engine allocated something other than the image's
		// own size - that is the condition the old code could not survive, and
		// seeing it named in the next log settles whether picmip / a dimension
		// limit / the hardware cap is the trigger.
		if ( nFillW != m_nWidth || nFillH != m_nHeight )
		{
			static int s_nClampReports = 0;
			if ( s_nClampReports < 16 )
			{
				++s_nClampReports;
				Warning( "[HL2SB] image texture clamped %dx%d -> %dx%d (picmip / dimension limit / hw max)\n",
					m_nWidth, m_nHeight, nFillW, nFillH );
			}
		}

		const int nPixelSize = ImageLoader::SizeInBytes( pVTFTexture->Format() );
		if ( nPixelSize <= 0 )
			return;

		const int nMipCount = pVTFTexture->MipCount();
		const int nFrameCount = pVTFTexture->FrameCount();
		const int nFaceCount = pVTFTexture->FaceCount();

		for ( int iFrame = 0; iFrame < nFrameCount; ++iFrame )
		{
			for ( int iFace = 0; iFace < nFaceCount; ++iFace )
			{
				// NOTE: the source image is re-read from m_RGBA for every face
				// and frame.  The old code shuffled a local buffer down the mip
				// chain and then restarted the loops with the *full* source
				// dimensions, so face/frame 1 indexed a 1x1 buffer as if it
				// were still the base level.
				for ( int iMip = 0; iMip < nMipCount; ++iMip )
				{
					int nMipW = 0, nMipH = 0, nMipD = 0;
					pVTFTexture->ComputeMipLevelDimensions( iMip, &nMipW, &nMipH, &nMipD );
					if ( nMipW <= 0 || nMipH <= 0 )
						continue;

					// Where the fill region lands in this mip.
					const int nShift = iMip;
					int nDstX = nFillX >> nShift;
					int nDstY = nFillY >> nShift;
					int nDstW = ( ( nFillX + nFillW + ( 1 << nShift ) - 1 ) >> nShift ) - nDstX;
					int nDstH = ( ( nFillY + nFillH + ( 1 << nShift ) - 1 ) >> nShift ) - nDstY;

					if ( nDstX < 0 ) { nDstW += nDstX; nDstX = 0; }
					if ( nDstY < 0 ) { nDstH += nDstY; nDstY = 0; }
					if ( nDstX >= nMipW || nDstY >= nMipH )
						continue;

					nDstW = Min( nDstW, nMipW - nDstX );
					nDstH = Min( nDstH, nMipH - nDstY );
					if ( nDstW <= 0 || nDstH <= 0 )
						continue;

					// Hard ceiling: never put more texels along a row than the
					// row actually has room for, whatever the dimensions say.
					const int nRowBytes = pVTFTexture->RowSizeInBytes( iMip );
					const int nMaxPixels = nRowBytes / nPixelSize;
					if ( nDstX + nDstW > nMaxPixels )
						nDstW = nMaxPixels - nDstX;
					if ( nDstW <= 0 )
						continue;

					const int nDepth = Min( nMipD, nTexD );

					// Fill-relative start of this mip's rect, so the resampler
					// indexes the source without caring about offsets.
					const int nRelX = nDstX - ( nFillX >> nShift );
					const int nRelY = nDstY - ( nFillY >> nShift );

					for ( int z = 0; z < nDepth; ++z )
					{
						CPixelWriter pixelWriter;
						pixelWriter.SetPixelMemory( pVTFTexture->Format(),
							pVTFTexture->ImageData( iFrame, iFace, iMip, nDstX, nDstY, z ),
							nRowBytes );

						for ( int y = 0; y < nDstH; ++y )
						{
							pixelWriter.Seek( 0, y );
							for ( int x = 0; x < nDstW; ++x )
							{
								int nR, nG, nB, nA;
								ReadTexel( nRelX + x, nRelY + y, nShift, nFillW, nFillH, nR, nG, nB, nA );
								pixelWriter.WritePixel( nR, nG, nB, nA );
							}
						}
					}
				}
			}
		}
	}

	virtual void Release() { delete this; }

private:
	// Box-averaged read of the source image for one destination texel.
	// nDstX/nDstY are fill-relative texel coords of the mip being written and
	// nShift is that mip's level, so the texel covers [nDstX<<nShift,
	// (nDstX+1)<<nShift) of the fill region in mip-0 texels.  The fill region
	// maps linearly onto the whole decoded image, which makes this an exact
	// copy at the native size and a proper box filter otherwise.
	void ReadTexel( int nDstX, int nDstY, int nShift, int nFillW, int nFillH,
		int &nR, int &nG, int &nB, int &nA ) const
	{
		int nU0 = nDstX << nShift;
		int nU1 = ( nDstX + 1 ) << nShift;
		int nV0 = nDstY << nShift;
		int nV1 = ( nDstY + 1 ) << nShift;

		if ( nU0 >= nFillW ) { nU0 = nFillW - 1; nU1 = nFillW; }
		if ( nV0 >= nFillH ) { nV0 = nFillH - 1; nV1 = nFillH; }
		if ( nU1 > nFillW ) { nU1 = nFillW; }
		if ( nV1 > nFillH ) { nV1 = nFillH; }

		int nX0 = nU0 * m_nWidth / nFillW;
		int nX1 = ( nU1 * m_nWidth + nFillW - 1 ) / nFillW;      // ceil
		int nY0 = nV0 * m_nHeight / nFillH;
		int nY1 = ( nV1 * m_nHeight + nFillH - 1 ) / nFillH;

		nX0 = Clamp( nX0, 0, m_nWidth - 1 );
		nY0 = Clamp( nY0, 0, m_nHeight - 1 );
		nX1 = Max( nX1, nX0 + 1 );
		nY1 = Max( nY1, nY0 + 1 );
		nX1 = Min( nX1, m_nWidth );
		nY1 = Min( nY1, m_nHeight );

		int64_t nSumR = 0, nSumG = 0, nSumB = 0, nSumA = 0;
		int nCount = 0;
		for ( int y = nY0; y < nY1; ++y )
		{
			for ( int x = nX0; x < nX1; ++x )
			{
				const unsigned char *pSrc = &m_RGBA[ ( y * m_nWidth + x ) * 4 ];
				nSumR += pSrc[0];
				nSumG += pSrc[1];
				nSumB += pSrc[2];
				nSumA += pSrc[3];
				++nCount;
			}
		}

		if ( nCount <= 0 )
		{
			nR = nG = nB = nA = 0;
			return;
		}

		nR = (int)( nSumR / nCount );
		nG = (int)( nSumG / nCount );
		nB = (int)( nSumB / nCount );
		nA = (int)( nSumA / nCount );
	}

	int m_nWidth;
	int m_nHeight;
	CUtlVector<unsigned char> m_RGBA;
};

// stb_image keeps its failure reason in a file-scope global, so two decodes
// running at once would scribble over each other.  Async texture loads and the
// main thread both land here, so serialise the decode (issue #41, 7.4).
//
// HL2SB (2026-10-02, second issue): the lock is now EXPORTED
// (HL2SB_ImageDecodeMutex) and taken by CTextureManager::LoadTexture around
// the WHOLE image branch - dictionary re-check included.  Locking only the
// decode left the check/insert window open: two resolver threads (material
// precache vs the queued/vgui-side bind) both passed the "is it already in
// the dictionary?" probe while the first was still decoding, and every addon
// PNG loaded exactly twice.  Do NOT re-lock it inside this file - the only
// caller is LoadTexture, which already holds it, and CThreadFastMutex is not
// recursive.
CThreadFastMutex g_HL2SBImageDecodeMutex;

CThreadFastMutex &HL2SB_ImageDecodeMutex()
{
	return g_HL2SBImageDecodeMutex;
}

//-----------------------------------------------------------------------------
// HL2SB (2026-10-03): on-disk decode cache.
//
// Decoded pixels are persisted under cache/images/<fnv1a64>.h2i inside the
// game dir, keyed by source logical name + source file size + source mtime, so
// an updated PNG gets a new key automatically.  A hit skips the source read
// AND the stb decode; the only per-session cost left is one small metadata
// read (GetFileSize/GetFileTime) and one file read of raw RGBA, which is far
// cheaper than PNG inflate on the ARM64-emulated test machines.  Every cache
// failure (missing dir, read-only disk, corrupt file) falls back silently to
// the decode path.
//-----------------------------------------------------------------------------

// FNV-1a 64 over a NUL-terminated string, then mixed with two integers.
// The file time is mixed at full 64-bit width now (v1 truncated it to 32
// bits, so two mtime values sharing a low-32 collided and served stale
// pixels).
static unsigned long long HL2SB_ImageCacheKey( const char *pLogicalName, int nFileSize, long nFileTime )
{
	unsigned long long h = 0xcbf29ce484222325ULL;
	for ( const char *p = pLogicalName; *p; ++p )
	{
		h ^= (unsigned char)*p;
		h *= 0x100000001b3ULL;
	}
	unsigned long long mix[2] =
	{
		( unsigned long long )( unsigned int )nFileSize,
		( unsigned long long )( unsigned long long )nFileTime
	};
	for ( int i = 0; i < 2; ++i )
	{
		for ( int b = 0; b < 8; ++b )
		{
			h ^= ( mix[i] >> ( b * 8 ) ) & 0xFF;
			h *= 0x100000001b3ULL;
		}
	}
	return h;
}

// Finds the actual readable file behind a logical image name ("x/y.png" or
// "x/y" relative to materials/).  Returns false if none of the probe
// combinations exist.  On success pOutPath is the filesystem path that later
// GetFileSize/GetFileTime/ReadFile calls must reuse (same pathID).
static bool HL2SB_LocateImageSource( const char *pLogicalName, char *pOutPath, int nOutPathLen,
	const char **ppOutPathID, int *pOutSize, long *pOutTime )
{
	const char *pPathIDs[] = { "GAME", "MOD", NULL };
	for ( int i = 0; pPathIDs[i]; ++i )
	{
		Q_snprintf( pOutPath, nOutPathLen, "materials/%s", pLogicalName );
		if ( g_pFullFileSystem->FileExists( pOutPath, pPathIDs[i] ) )
		{
			*ppOutPathID = pPathIDs[i];
			*pOutSize = ( int )g_pFullFileSystem->Size( pOutPath, pPathIDs[i] );
			*pOutTime = g_pFullFileSystem->GetFileTime( pOutPath, pPathIDs[i] );
			return ( *pOutSize > 0 );
		}

		Q_strncpy( pOutPath, pLogicalName, nOutPathLen );
		if ( g_pFullFileSystem->FileExists( pOutPath, pPathIDs[i] ) )
		{
			*ppOutPathID = pPathIDs[i];
			*pOutSize = ( int )g_pFullFileSystem->Size( pOutPath, pPathIDs[i] );
			*pOutTime = g_pFullFileSystem->GetFileTime( pOutPath, pPathIDs[i] );
			return ( *pOutSize > 0 );
		}
	}
	return false;
}

// cache/images/<16 hex>.h2i inside the game dir.
static void HL2SB_ImageCachePath( unsigned long long nKey, char *pOut, int nOutLen )
{
	Q_snprintf( pOut, nOutLen, "cache/images/%016I64x.h2i", nKey );
}

// Header layout v2 (little endian): magic 'H','2','I','C', version 2, width,
// height, payload bytes, logical-name length, CRC32 of the payload, logical
// name bytes, RGBA.  v1 had no CRC and a 32-bit-truncated key; v1 files get
// orphaned by the new key and are pruned on startup.
static const unsigned int HL2SB_IMAGECACHE_MAGIC = 0x43493248u;	// "H2IC" LE
static const unsigned int HL2SB_IMAGECACHE_VERSION = 2u;

// Total size the cache directory is pruned back to (one scan per session, at
// the first cache write).  A 4096x4096 image persists as 64MB of raw RGBA, so
// without a cap a handful of large originals would happily eat gigabytes.
static const unsigned int HL2SB_IMAGECACHE_CAP_BYTES = 256u * 1024u * 1024u;

// One pass over cache/images: delete foreign/stale-version files outright,
// then delete oldest-first until the directory fits the cap.  Runs once per
// process, right before the first cache write.
static void HL2SB_PruneImageCache()
{
	FileFindHandle_t find;
	const char *pName = g_pFullFileSystem->FindFirstEx( "cache/images/*.h2i", "GAME", &find );
	if ( !pName )
		return;

	struct CacheEntry_t { char szName[MAX_PATH]; unsigned int nSize; long nTime; unsigned int nVersion; bool bDeleted; };
	CUtlVector< CacheEntry_t > entries;
	unsigned int nTotal = 0;

	char szPath[MAX_PATH];
	do
	{
		Q_snprintf( szPath, sizeof( szPath ), "cache/images/%s", pName );

		CacheEntry_t &e = entries[ entries.AddToTail() ];
		Q_strncpy( e.szName, szPath, sizeof( e.szName ) );
		e.nSize = ( unsigned int )g_pFullFileSystem->Size( szPath, "GAME" );
		e.nTime = g_pFullFileSystem->GetFileTime( szPath, "GAME" );
		e.bDeleted = false;
		nTotal += e.nSize;

		// Peek the version so stale v1 files are dropped even under the cap.
		e.nVersion = 0;
		if ( FileHandle_t h = g_pFullFileSystem->Open( szPath, "rb", "GAME" ) )
		{
			unsigned int hdr[2] = { 0, 0 };
			g_pFullFileSystem->Read( hdr, sizeof( hdr ), h );
			g_pFullFileSystem->Close( h );
			if ( hdr[0] == HL2SB_IMAGECACHE_MAGIC )
				e.nVersion = hdr[1];
		}
	}
	while ( ( pName = g_pFullFileSystem->FindNext( find ) ) != NULL );
	g_pFullFileSystem->FindClose( find );

	// Pass 1: stale versions (v1 or foreign) go unconditionally - their keys
	// no longer match anything this build computes.
	for ( int i = 0; i < entries.Count(); ++i )
	{
		if ( entries[i].nVersion != HL2SB_IMAGECACHE_VERSION )
		{
			g_pFullFileSystem->RemoveFile( entries[i].szName, "GAME" );
			entries[i].bDeleted = true;
			if ( nTotal >= entries[i].nSize )
				nTotal -= entries[i].nSize;
		}
	}

	// Pass 2: over the cap, evict oldest first.
	while ( nTotal > HL2SB_IMAGECACHE_CAP_BYTES )
	{
		int iOldest = -1;
		for ( int i = 0; i < entries.Count(); ++i )
		{
			if ( entries[i].bDeleted )
				continue;
			if ( iOldest < 0 || entries[i].nTime < entries[iOldest].nTime )
				iOldest = i;
		}
		if ( iOldest < 0 )
			break;

		g_pFullFileSystem->RemoveFile( entries[iOldest].szName, "GAME" );
		entries[iOldest].bDeleted = true;
		if ( nTotal >= entries[iOldest].nSize )
			nTotal -= entries[iOldest].nSize;
	}
}

static bool HL2SB_TryReadCachedImage( const char *pLogicalName, const char *pCachePath,
	CUtlVector< unsigned char > &outBits, int &nOutWidth, int &nOutHeight )
{
	if ( !g_pFullFileSystem->FileExists( pCachePath, "GAME" ) )
		return false;

	CUtlBuffer bufFile;
	if ( !g_pFullFileSystem->ReadFile( pCachePath, "GAME", bufFile ) || bufFile.TellPut() <= 0 )
		return false;

	bufFile.SeekGet( CUtlBuffer::SEEK_HEAD, 0 );

	if ( bufFile.GetUnsignedInt() != HL2SB_IMAGECACHE_MAGIC )
		return false;
	if ( bufFile.GetUnsignedInt() != HL2SB_IMAGECACHE_VERSION )
		return false;

	const int nWidth = ( int )bufFile.GetUnsignedInt();
	const int nHeight = ( int )bufFile.GetUnsignedInt();
	const unsigned int nPayload = bufFile.GetUnsignedInt();
	const unsigned int nNameBytes = bufFile.GetUnsignedInt();
	const unsigned int nCRC = bufFile.GetUnsignedInt();

	if ( nWidth <= 0 || nHeight <= 0 )
		return false;
	if ( nPayload != ( unsigned int )nWidth * nHeight * 4 )
		return false;
	if ( nNameBytes == 0 || nNameBytes > 1024 )
		return false;

	char szStoredName[1025];
	bufFile.Get( szStoredName, nNameBytes );
	szStoredName[ nNameBytes - 1 ] = '\0';
	if ( Q_stricmp( szStoredName, pLogicalName ) != 0 )
		return false;

	const int nBytes = nWidth * nHeight * 4;
	outBits.SetSize( nBytes );
	if ( !bufFile.Get( outBits.Base(), nBytes ) )
	{
		outBits.Purge();
		return false;
	}

	// v2 carries a payload CRC: a torn write or bit-rot inside the RGBA is
	// detected here instead of rendering as garbage.
	if ( CRC32_ProcessSingleBuffer( outBits.Base(), nBytes ) != nCRC )
	{
		outBits.Purge();
		return false;
	}

	nOutWidth = nWidth;
	nOutHeight = nHeight;
	return true;
}

static bool HL2SB_TryWriteCachedImage( const char *pLogicalName, const char *pCachePath,
	const unsigned char *pBits, int nWidth, int nHeight )
{
	const int nBytes = nWidth * nHeight * 4;
	if ( nBytes <= 0 || nBytes > 128 * 1024 * 1024 )
		return false;

	static bool s_bDirTried = false;
	if ( !s_bDirTried )
	{
		s_bDirTried = true;
		g_pFullFileSystem->CreateDirHierarchy( "cache/images", NULL );
		HL2SB_PruneImageCache();
	}

	const unsigned int nNameBytes = ( unsigned int )Q_strlen( pLogicalName ) + 1;

	CUtlBuffer bufOut;
	bufOut.EnsureCapacity( 4 * 7 + nNameBytes + nBytes );
	bufOut.PutUnsignedInt( HL2SB_IMAGECACHE_MAGIC );
	bufOut.PutUnsignedInt( HL2SB_IMAGECACHE_VERSION );
	bufOut.PutUnsignedInt( ( unsigned int )nWidth );
	bufOut.PutUnsignedInt( ( unsigned int )nHeight );
	bufOut.PutUnsignedInt( ( unsigned int )nBytes );
	bufOut.PutUnsignedInt( nNameBytes );
	bufOut.PutUnsignedInt( CRC32_ProcessSingleBuffer( pBits, nBytes ) );
	bufOut.Put( pLogicalName, nNameBytes );
	bufOut.Put( pBits, nBytes );

	return g_pFullFileSystem->WriteFile( pCachePath, NULL, bufOut );
}

ITextureRegenerator *HL2SB_CreateImageTextureRegenerator( const char *pLogicalName, const char *pDigits,
	int *pOutWidth, int *pOutHeight )
{
	if ( !pLogicalName || !pLogicalName[0] )
		return NULL;
	( void )pDigits;	// decoded flags live on the texture; kept for CImage symmetry

	// Locate the source first: its concrete path feeds the cache key (size +
	// mtime), so an updated PNG invalidates its own cache entry.
	char szSourcePath[MAX_PATH];
	const char *pSourcePathID = NULL;
	int nFileSize = 0;
	long nFileTime = 0;
	bool bLocated = HL2SB_LocateImageSource( pLogicalName, szSourcePath, sizeof( szSourcePath ), &pSourcePathID, &nFileSize, &nFileTime );

	const bool bDbg = hl2sb_image_debug.GetBool();

	CUtlVector< unsigned char > bits;
	int nWidth = 0, nHeight = 0;
	const char *pSource = "none";

	if ( bLocated )
	{
		char szCachePath[MAX_PATH];
		HL2SB_ImageCachePath( HL2SB_ImageCacheKey( pLogicalName, nFileSize, nFileTime ), szCachePath, sizeof( szCachePath ) );

		if ( HL2SB_TryReadCachedImage( pLogicalName, szCachePath, bits, nWidth, nHeight ) )
		{
			pSource = "cache";
		}
		else
		{
			CUtlBuffer bufFile;
			if ( g_pFullFileSystem->ReadFile( szSourcePath, pSourcePathID, bufFile ) && bufFile.TellPut() > 0 )
			{
				int nChannels = 0;
				unsigned char *pRGBA = NULL;
				// Caller holds HL2SB_ImageDecodeMutex() (see the note on the
				// lock above) - locking it here again would self-deadlock.
				pRGBA = stbi_load_from_memory( ( const stbi_uc * )bufFile.Base(), bufFile.TellPut(), &nWidth, &nHeight, &nChannels, 4 );

				if ( pRGBA && nWidth > 0 && nHeight > 0 )
				{
					bits.SetSize( nWidth * nHeight * 4 );
					Q_memcpy( bits.Base(), pRGBA, (size_t)nWidth * nHeight * 4 );
					pSource = "decode";
				}

				if ( pRGBA )
					stbi_image_free( pRGBA );

				// Persist for the next session; a read-only disk or a full
				// drive just means the next load decodes again.
				if ( bits.Count() > 0 )
				{
					if ( !HL2SB_TryWriteCachedImage( pLogicalName, szCachePath, bits.Base(), nWidth, nHeight ) && bDbg )
						Msg( "[HL2SB imgdb] cache write failed for '%s'\n", pLogicalName );
				}
			}
		}
	}

	if ( bits.Count() <= 0 || nWidth <= 0 || nHeight <= 0 )
	{
		if ( bDbg )
			Msg( "[HL2SB imgdb] tid=%u FAILED locate/decode '%s' (located=%d)\n",
				( unsigned )ThreadGetCurrentId(), pLogicalName, bLocated ? 1 : 0 );
		return NULL;
	}

	// CImageTextureRegenerator sizes its buffer with a 32-bit count; reject
	// anything that would overflow it rather than wrap to a tiny allocation.
	if ( (int64)nWidth * (int64)nHeight * 4 > (int64)0x7fffffff )
	{
		Warning( "[HL2SB] image \"%s\" is %dx%d - too large, skipped\n", pLogicalName, nWidth, nHeight );
		return NULL;
	}

	if ( bDbg )
		Msg( "[HL2SB imgdb] tid=%u '%s' %dx%d via %s\n",
			( unsigned )ThreadGetCurrentId(), pLogicalName, nWidth, nHeight, pSource );

	CImageTextureRegenerator *pRegenerator = new CImageTextureRegenerator( nWidth, nHeight, bits.Base() );

	if ( pOutWidth )
		*pOutWidth = nWidth;
	if ( pOutHeight )
		*pOutHeight = nHeight;

	return pRegenerator;
}
