//========= Copyright Valve Corporation, All rights reserved. ============//
//
// Purpose: GMod-style raw image textures.
//
//          GMod's material system reads .png / .jpg / .tga / .bmp files
//          directly, so GMod content routinely writes
//
//              Material( "gwenskin/GModDefault.png" )
//              "$basetexture" "hud/killicons/default.png"
//
//          Stock Source can only read .vtf, so those names used to fall
//          through to the error material.  These helpers decode the image
//          into a procedurally generated texture instead.
//
//=======================================================================================//

#ifndef HL2SB_PNGTEXTURE_H
#define HL2SB_PNGTEXTURE_H
#ifdef _WIN32
#pragma once
#endif

class ITextureRegenerator;
class CThreadFastMutex;

// True if the name ends in an extension stb_image can decode.
bool HL2SB_IsImageFileName( const char *pFileName );

// Resolves a texture name to the logical texture name of an image file that
// actually exists ( "<dir>/<name>.png" relative to materials/ ), accepting the
// name with or without its extension, and with or without a leading
// "materials/".  Returns false when no image file exists for the name.
bool HL2SB_ResolveImageTexture( const char *pTextureName, char *pOutLogicalName, int nOutLogicalNameSize );

// Decodes the logical texture name into a new regenerator.  The caller owns the
// returned regenerator (hand it to ITextureInternal::CreateProceduralTexture).
//
// NOTE: the caller must already hold HL2SB_ImageDecodeMutex(): the decode
// itself used to lock it internally, but the dictionary check that decides
// whether to decode at all has to sit under the SAME lock (two resolver
// threads both passing the check before either inserts decoded every addon
// PNG exactly twice), and CThreadFastMutex is not recursive.
ITextureRegenerator *HL2SB_CreateImageTextureRegenerator( const char *pLogicalName, int *pOutWidth, int *pOutHeight );

// Serialises [dictionary re-check -> file read -> decode -> texture creation ->
// dictionary insert] for image textures.  The only lock in the engine that
// covers the whole "should I decode this PNG?" decision, so a second resolver
// (material precache vs queued/vgui bind) blocks here and then hits the
// freshly inserted dictionary entry instead of decoding the same file again.
CThreadFastMutex &HL2SB_ImageDecodeMutex();

#endif // HL2SB_PNGTEXTURE_H
