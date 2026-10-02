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
#include "filesystem.h"
#include "bitmap/imageformat.h"
#include "pixelwriter.h"
#include "materialsystem/itexture.h"
#include "materialsystem/imaterialsystem.h"
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
// Extensions we are willing to load straight from disk.
//-----------------------------------------------------------------------------
static const char *s_pImageExtensions[] =
{
	".png",
	".jpg",
	".jpeg",
	".tga",
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
// "materials/<name>" is how texture files are addressed through the search
// paths (CTexture::GetFilename does the same thing with a .vtf suffix).
//-----------------------------------------------------------------------------
static bool HL2SB_ImageFileExists( const char *pLogicalName )
{
	char szPath[MAX_PATH];

	Q_snprintf( szPath, sizeof( szPath ), "materials/%s", pLogicalName );
	if ( g_pFullFileSystem->FileExists( szPath, "GAME" ) )
		return true;

	Q_strncpy( szPath, pLogicalName, sizeof( szPath ) );
	return g_pFullFileSystem->FileExists( szPath, "GAME" );
}

bool HL2SB_ResolveImageTexture( const char *pTextureName, char *pOutLogicalName, int nOutLogicalNameSize )
{
	if ( !pTextureName || !pTextureName[0] || !pOutLogicalName )
		return false;

	// Texture names are addressed relative to materials/, but accept a name
	// that already carries the prefix (vgui passes some through verbatim).
	const char *pName = pTextureName;
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
static CThreadFastMutex g_HL2SBImageDecodeMutex;

ITextureRegenerator *HL2SB_CreateImageTextureRegenerator( const char *pLogicalName, int *pOutWidth, int *pOutHeight )
{
	if ( !pLogicalName || !pLogicalName[0] )
		return NULL;

	CUtlBuffer bufFile;
	bool bRead = false;

	const char *pPathIDs[] = { "GAME", "MOD", NULL };
	for ( int i = 0; pPathIDs[i]; ++i )
	{
		char szPath[MAX_PATH];
		Q_snprintf( szPath, sizeof( szPath ), "materials/%s", pLogicalName );

		bufFile.Purge();
		if ( g_pFullFileSystem->ReadFile( szPath, pPathIDs[i], bufFile ) && bufFile.TellPut() > 0 )
		{
			bRead = true;
			break;
		}

		bufFile.Purge();
		if ( g_pFullFileSystem->ReadFile( pLogicalName, pPathIDs[i], bufFile ) && bufFile.TellPut() > 0 )
		{
			bRead = true;
			break;
		}
	}

	if ( !bRead )
		return NULL;

	int nWidth = 0, nHeight = 0, nChannels = 0;
	unsigned char *pRGBA = NULL;
	{
		AUTO_LOCK( g_HL2SBImageDecodeMutex );
		pRGBA = stbi_load_from_memory( (const stbi_uc *)bufFile.Base(), bufFile.TellPut(), &nWidth, &nHeight, &nChannels, 4 );
	}

	if ( !pRGBA || nWidth <= 0 || nHeight <= 0 )
	{
		if ( pRGBA )
			stbi_image_free( pRGBA );
		return NULL;
	}

	// CImageTextureRegenerator sizes its buffer with a 32-bit count; reject
	// anything that would overflow it rather than wrap to a tiny allocation.
	if ( (int64_t)nWidth * (int64_t)nHeight * 4 > (int64_t)0x7fffffff )
	{
		Warning( "[HL2SB] image \"%s\" is %dx%d - too large, skipped\n", pLogicalName, nWidth, nHeight );
		stbi_image_free( pRGBA );
		return NULL;
	}

	CImageTextureRegenerator *pRegenerator = new CImageTextureRegenerator( nWidth, nHeight );
	Q_memcpy( pRegenerator->GetImageBits(), pRGBA, (size_t)nWidth * nHeight * 4 );
	stbi_image_free( pRGBA );

	if ( pOutWidth )
		*pOutWidth = nWidth;
	if ( pOutHeight )
		*pOutHeight = nHeight;

	return pRegenerator;
}
