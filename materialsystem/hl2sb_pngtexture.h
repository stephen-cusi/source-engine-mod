//========= Copyright Valve Corporation, All rights reserved. ============//
//
// Purpose: GMod-style raw image textures.
//
//          GMod's material system reads .png / .jpg / .tga files directly, so
//          GMod content routinely writes
//
//              Material( "gwenskin/GModDefault.png" )
//              "$basetexture" "hud/killicons/default.png"
//
//          Stock Source can only read .vtf, so those names used to fall
//          through to the error material.  These helpers decode the image
//          into a procedurally generated texture instead.
//
//          2026-10-09 GMod-alignment pass: image textures can carry GMod's
//          pngParameters digit string.  A parameterised texture is named
//
//              "!" <7 digits> <path>
//
//          (GMod builds exactly that shape for its "!"-prefixed FreeImage
//          materials), the digits select shader / flags exactly like GMod's
//          CResources::CreateMaterialFromTextureFile, and the same name feeds
//          the texture dictionary so two parameter sets never share a texture.
//
//=======================================================================================//

#ifndef HL2SB_PNGTEXTURE_H
#define HL2SB_PNGTEXTURE_H
#ifdef _WIN32
#pragma once
#endif

class ITextureRegenerator;
class CThreadFastMutex;

// Length of the pngParameters digit string carried in "!"/-prefixed names.
#define HL2SB_IMAGE_PARAMS_DIGITS 7

// True if the name ends in an extension stb_image can decode (GMod's
// resources.dll whitelist is png/jpg/jpeg/tga/cache; .bmp stays as a
// harmless superset for local content).
bool HL2SB_IsImageFileName( const char *pFileName );

// Splits a texture name into its optional "!<digits>" image-parameter prefix
// and the plain path that follows it.  Names without the prefix come back
// with an empty digit string.  Either out pointer may be NULL.
bool HL2SB_SplitImageTextureName( const char *pTextureName,
	char *pOutDigits, int nOutDigitsSize,
	char *pOutPath, int nOutPathSize );

// Resolves a texture name to the logical texture name of an image file that
// actually exists ( "<dir>/<name>.png" relative to materials/ ), accepting the
// name with or without its extension, with or without a leading "materials/",
// and with or without the "!<digits>" parameter prefix.  Returns false when no
// image file exists for the name.
bool HL2SB_ResolveImageTexture( const char *pTextureName, char *pOutLogicalName, int nOutLogicalNameSize );

// GMod pngParameters digits -> procedural texture flags.  Default mirrors
// CResources::CreateMaterialFromTextureFile:
//   SINGLECOPY | PROCEDURAL | NOLOD | NOMIP | CLAMPS | CLAMPT | POINTSAMPLE
//   digits[3] != '0' clears NOMIP|NOLOD   ("mips")
//   digits[4] != '0' clears CLAMPS|CLAMPT ("noclamp")
//   digits[5] != '0' clears POINTSAMPLE|NOLOD ("smooth")
unsigned int HL2SB_ImageTextureFlags( const char *pDigits );

// Decodes the logical texture name into a new regenerator.  The caller owns
// the returned regenerator (hand it to ITextureInternal::CreateProceduralTexture).
// pDigits is the parameter string (may be NULL / empty); it only selects
// behaviour that survives in the regenerator (currently none - flags live on
// the texture - but keep the signature symmetric with GMod's CImage).
//
// NOTE: the caller must already hold HL2SB_ImageDecodeMutex(): the decode
// itself used to lock it internally, but the dictionary check that decides
// whether to decode at all has to sit under the SAME lock (two resolver
// threads both passing the check before either inserts decoded every addon
// PNG exactly twice), and CThreadFastMutex is not recursive.
ITextureRegenerator *HL2SB_CreateImageTextureRegenerator( const char *pLogicalName, const char *pDigits,
	int *pOutWidth, int *pOutHeight );

// Serialises [dictionary re-check -> file read -> decode -> texture creation ->
// dictionary insert] for image textures.  The only lock in the engine that
// covers the whole "should I decode this PNG?" decision, so a second resolver
// (material precache vs queued/vgui bind) blocks here and then hits the
// freshly inserted dictionary entry instead of decoding the same file again.
CThreadFastMutex &HL2SB_ImageDecodeMutex();

// HL2SB (2026-10-03): source-tree revision of the image-texture path.  Bump
// whenever this file or the LoadTexture image branch changes; the value is
// printed at material system init ("[HL2SB] materialsystem image-cache build
// ...") so a user test session can prove which build it ran.
// 6: GMod pngParameters pass ("!"-prefixed parameterised names, texture flags
//    from digits, .cache whitelist, spawnicons MOD-first probe), H2IC v2
//    (64-bit mtime key + payload CRC) + size-capped cache pruning.
#define HL2SB_IMAGECACHE_BUILD_REV 6

#endif // HL2SB_PNGTEXTURE_H
