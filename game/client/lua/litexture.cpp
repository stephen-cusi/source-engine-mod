#include "cbase.h"
#include "materialsystem/itexture.h"
#include "luamanager.h"
#include "luasrclib.h"
#include "litexture.h"
#include "lua/materialsystem/limaterial.h"
#include "materialsystem/imaterial.h"
#include "materialsystem/imaterialvar.h"
#include "filesystem.h"
#ifdef CLIENT_DLL
#include "rendertexture.h"
#include "view_scene.h"
#include <materialsystem/imaterialsystem.h>
#include <vgui/ISurface.h>
#include <vgui_controls/Controls.h>
#include "mathlib/lvector.h"
#include <lColor.h>
#include <renderparm.h>
#include <view.h>
#endif

// memdbgon must be the last include file in a .cpp file!!!
#include "tier0/memdbgon.h"

LUA_REGISTRATION_INIT( Texture );

#ifdef CLIENT_DLL
// List of all texture ids created during the lifetime of the client Lua state
static CUtlVector< int > g_TextureIDs;

/// <summary>
/// Creates a new TextureID, keeping track of it, so it can be destroyed on shutdown
/// of the client Lua state.
/// </summary>
/// <param name="procedural"></param>
/// <returns></returns>
int CreateNewAutoDestroyTextureId( bool procedural )
{
    int textureID = vgui::surface()->CreateNewTextureID( procedural );

    if ( textureID != -1 )
    {
        g_TextureIDs.AddToTail( textureID );
    }

    return textureID;
}

/// <summary>
/// Destroys all TextureIDs created during the lifetime of the client Lua state
/// </summary>
void DestroyCreatedTextureIds()
{
    for ( int i = 0; i < g_TextureIDs.Count(); i++ )
    {
        vgui::surface()->DestroyTextureID( g_TextureIDs[i] );
    }

    g_TextureIDs.RemoveAll();
}
#endif

/*
** access functions (stack -> C)
*/
LUA_API lua_ITexture *lua_toitexture( lua_State *L, int idx )
{
    lua_ITexture **ppTexture = ( lua_ITexture ** )lua_touserdata( L, idx );
    return *ppTexture;
}

/*
** push functions (C -> stack)
*/
LUA_API void lua_pushitexture( lua_State *L, lua_ITexture *pTexture )
{
    lua_ITexture **ppTexture = ( lua_ITexture ** )lua_newuserdata( L, sizeof( pTexture ) );
    *ppTexture = pTexture;
    LUA_SAFE_SET_METATABLE( L, LUA_ITEXTUREMETANAME );
}

LUALIB_API lua_ITexture *luaL_checkitexture( lua_State *L, int narg )
{
    lua_ITexture **ppData = ( lua_ITexture ** )luaL_checkudata( L, narg, LUA_ITEXTUREMETANAME );

    if ( *ppData == 0 ) /* avoid extra test when d is not 0 */
        luaL_argerror( L, narg, "ITexture expected, got NULL" );

    return *ppData;
}

/*
** ITexture metatable
**
** Upstream declares LUA_REGISTRATION_INIT( ITexture ) here while every binding below
** is written as LUA_BINDING_BEGIN( Texture, ... ) and luaopen_ITexture() commits
** LUA_REGISTRATION_COMMIT( Texture ) -- i.e. it declares a registry nothing fills
** and fills a registry nothing declares.  The working declaration is at the top of
** this file; the stray one is dropped.
*/

LUA_BINDING_BEGIN( Texture, Download, "class", "Downloads the texture into the material system." )
{
    lua_ITexture *pTexture = LUA_BINDING_ARGUMENT( luaL_checkitexture, 1, "texture" );
    pTexture->Download();
    return 0;
}
LUA_BINDING_END()

LUA_BINDING_BEGIN( Texture, GetName, "class", "Gets the name of the texture." )
{
    lua_ITexture *pTexture = LUA_BINDING_ARGUMENT( luaL_checkitexture, 1, "texture" );
    lua_pushstring( L, pTexture->GetName() );
    return 1;
}
LUA_BINDING_END( "string", "The name of the texture." )

LUA_BINDING_BEGIN( Texture, GetActualWidth, "class", "Gets the actual width of the texture." )
{
    lua_ITexture *pTexture = LUA_BINDING_ARGUMENT( luaL_checkitexture, 1, "texture" );
    lua_pushinteger( L, pTexture->GetActualWidth() );
    return 1;
}
LUA_BINDING_END( "integer", "The actual width of the texture." )

LUA_BINDING_BEGIN( Texture, GetActualHeight, "class", "Gets the actual height of the texture." )
{
    lua_ITexture *pTexture = LUA_BINDING_ARGUMENT( luaL_checkitexture, 1, "texture" );
    lua_pushinteger( L, pTexture->GetActualHeight() );
    return 1;
}
LUA_BINDING_END( "integer", "The actual height of the texture." )

LUA_BINDING_BEGIN( Texture, GetMappingWidth, "class", "Gets the mapping width of the texture." )
{
    lua_ITexture *pTexture = LUA_BINDING_ARGUMENT( luaL_checkitexture, 1, "texture" );
    lua_pushinteger( L, pTexture->GetMappingWidth() );
    return 1;
}
LUA_BINDING_END( "integer", "The mapping width of the texture." )

LUA_BINDING_BEGIN( Texture, GetMappingHeight, "class", "Gets the mapping height of the texture." )
{
    lua_ITexture *pTexture = LUA_BINDING_ARGUMENT( luaL_checkitexture, 1, "texture" );
    lua_pushinteger( L, pTexture->GetMappingHeight() );
    return 1;
}
LUA_BINDING_END( "integer", "The mapping height of the texture." )

// HL2SB: GMod's names for the same thing.  lua/derma/derma_gwen.lua:14 calls
// tex:Width() / tex:Height() while it builds the nine-slice skin borders.
LUA_BINDING_BEGIN( Texture, Width, "class", "Gets the width of the texture." )
{
    lua_ITexture *pTexture = LUA_BINDING_ARGUMENT( luaL_checkitexture, 1, "texture" );
    lua_pushinteger( L, pTexture->GetActualWidth() );
    return 1;
}
LUA_BINDING_END( "integer", "The width of the texture." )

LUA_BINDING_BEGIN( Texture, Height, "class", "Gets the height of the texture." )
{
    lua_ITexture *pTexture = LUA_BINDING_ARGUMENT( luaL_checkitexture, 1, "texture" );
    lua_pushinteger( L, pTexture->GetActualHeight() );
    return 1;
}
LUA_BINDING_END( "integer", "The height of the texture." )

LUA_BINDING_BEGIN( Texture, GetNumAnimationFrames, "class", "Gets the number of animation frames of the texture." )
{
    lua_ITexture *pTexture = LUA_BINDING_ARGUMENT( luaL_checkitexture, 1, "texture" );
    lua_pushinteger( L, pTexture->GetNumAnimationFrames() );
    return 1;
}
LUA_BINDING_END( "integer", "The number of animation frames of the texture." )

LUA_BINDING_BEGIN( Texture, IsTranslucent, "class", "Checks if the texture is translucent." )
{
    lua_ITexture *pTexture = LUA_BINDING_ARGUMENT( luaL_checkitexture, 1, "texture" );
    lua_pushboolean( L, pTexture->IsTranslucent() );
    return 1;
}
LUA_BINDING_END( "boolean", "true if the texture is translucent, false otherwise." )

LUA_BINDING_BEGIN( Texture, IsMipmapped, "class", "Checks if the texture is mipmapped." )
{
    lua_ITexture *pTexture = LUA_BINDING_ARGUMENT( luaL_checkitexture, 1, "texture" );
    lua_pushboolean( L, pTexture->IsMipmapped() );
    return 1;
}
LUA_BINDING_END( "boolean", "true if the texture is mipmapped, false otherwise." )

LUA_BINDING_BEGIN( Texture, IsError, "class", "Checks if the texture is in an error state." )
{
    lua_ITexture *pTexture = LUA_BINDING_ARGUMENT( luaL_checkitexture, 1, "texture" );
    lua_pushboolean( L, pTexture->IsError() );
    return 1;
}
LUA_BINDING_END( "boolean", "true if the texture is in an error state, false otherwise." )

LUA_BINDING_BEGIN( Texture, IsErrorTexture, "class", "Checks if the texture is an error texture." )
{
    lua_ITexture *pTexture = LUA_BINDING_ARGUMENT( luaL_checkitexture, 1, "texture" );
    lua_pushboolean( L, IsErrorTexture( pTexture ) );
    return 1;
}
LUA_BINDING_END( "boolean", "true if the texture is an error texture, false otherwise." )

LUA_BINDING_BEGIN( Texture, Release, "class", "Release the texture." )
{
    lua_ITexture *pTexture = LUA_BINDING_ARGUMENT( luaL_checkitexture, 1, "texture" );
    pTexture->Release();
    return 0;
}
LUA_BINDING_END()

LUA_BINDING_BEGIN( Texture, __tostring, "class", "Returns a string representation of the texture." )
{
    lua_ITexture *pTexture = LUA_BINDING_ARGUMENT( lua_toitexture, 1, "texture" );
    lua_pushfstring( L, "ITexture: %s", pTexture ? pTexture->GetName() : "NULL" );
    return 1;
}
LUA_BINDING_END()

/*
** HL2SB: GMod's IMaterial surface.
**
** These live here instead of in the shared public/lua/materialsystem/limaterial.cpp
** because this file is the client-only owner of the ITexture userdata that
** IMaterial:GetTexture has to hand back.  luaopen_IMaterial() runs earlier in
** luasrc_openLibs(), so the metatable below already exists when we extend it.
*/
/*
** HL2SB: resolve a material's texture, tolerating the material object the engine
** handed to Lua.
**
** FindMaterial() can give Lua an object that is not the one registered in the
** material dictionary: in the measured case the dictionary's material was
** precached correctly (mat=...B7F0, found=1, shader='UnlitGeneric') while the one
** the Derma skin held (mat=...B8C0) had no shader params at all, so every
** GetTexture( "$basetexture" ) returned nil and the skin atlas drawers died with
**
**   lua/derma/derma_gwen.lua:14: attempt to index a nil value (local 'tex')
**
** (surface.SetMaterial() is fine either way: it resolves by NAME, and the name
** finds the good dictionary material.)  For an image material the material name
** IS the texture name, so fall back to the texture system -- which resolves
** materials/gwenskin/gmoddefault.png through the image fallback in
** CTextureManager::LoadTexture.
*/
static ITexture *HL2SB_MaterialTexture( IMaterial *pMaterial, const char *pTextureVarName, bool &bFound )
{
    bFound = false;

    if ( !pMaterial )
        return NULL;

    IMaterialVar *pVar = pMaterial->FindVar( pTextureVarName, &bFound, false );
    return ( pVar && bFound ) ? pVar->GetTextureValue() : NULL;

    /*
    ** NOTE: a name-based "materials->FindTexture( material name )" fallback lived
    ** here for one revision and is deliberately gone.  It ran while a Lua panel
    ** was painting, i.e. mid-frame inside the render context, where
    ** CTextureManager::LoadTexture may create a texture -- the crashes reported
    ** after that build (access violation ~1s after the first undo, right after
    ** "DrawElements: No bound shader") point at it.  The materialsystem now
    ** returns a properly precached material instead, so no fallback is needed.
    */
}

static int HL2SB_IMaterial_GetTexture( lua_State *L )
{
    IMaterial *pMaterial = luaL_checkmaterial( L, 1 );
    const char *pTextureVarName = luaL_checkstring( L, 2 );

    bool bFound = false;
    ITexture *pTexture = HL2SB_MaterialTexture( pMaterial, pTextureVarName, bFound );

    if ( pTexture == NULL && pMaterial != NULL )
    {
        // Once per material: which object held no params, and did the name-based
        // fallback save it?  (See HL2SB_MaterialTexture.)
        static CUtlDict< int, int > s_LoggedFailures;

        if ( s_LoggedFailures.Find( pMaterial->GetName() ) == s_LoggedFailures.InvalidIndex() )
        {
            s_LoggedFailures.Insert( pMaterial->GetName(), 1 );
            Msg( "[HL2SB] GetTexture could not resolve '%s' for material='%s' mat=%p found=%d shader='%s'\n",
                pTextureVarName, pMaterial->GetName(), pMaterial, bFound, pMaterial->GetShaderName() );
        }
    }

    if ( pTexture == NULL )
    {
        lua_pushnil( L );
        return 1;
    }

    lua_pushitexture( L, pTexture );
    return 1;
}

static ITexture *HL2SB_MaterialBaseTexture( IMaterial *pMaterial )
{
    if ( !pMaterial )
        return NULL;

    bool bFound = false;
    return HL2SB_MaterialTexture( pMaterial, "$basetexture", bFound );
}

static int HL2SB_IMaterial_Width( lua_State *L )
{
    IMaterial *pMaterial = luaL_checkmaterial( L, 1 );
    ITexture *pTexture = HL2SB_MaterialBaseTexture( pMaterial );
    lua_pushinteger( L, pTexture ? pTexture->GetActualWidth() : 0 );
    return 1;
}

static int HL2SB_IMaterial_Height( lua_State *L )
{
    IMaterial *pMaterial = luaL_checkmaterial( L, 1 );
    ITexture *pTexture = HL2SB_MaterialBaseTexture( pMaterial );
    lua_pushinteger( L, pTexture ? pTexture->GetActualHeight() : 0 );
    return 1;
}

static int HL2SB_IMaterial_IsError( lua_State *L )
{
    IMaterial *pMaterial = luaL_checkmaterial( L, 1 );
    lua_pushboolean( L, pMaterial ? pMaterial->IsErrorMaterial() : true );
    return 1;
}

/*
** IMaterial:GetColor( x, y )
**
** GMod's Derma skin samples its own atlas for every UI colour it uses
** (lua/skins/default.lua -> GWEN.TextureColor -> mat:GetColor(x, y)), and that
** atlas is a PNG.  The engine's IMaterial::GetLowResColorSample() only knows how
** to read a VTF's embedded low-res image, so a .png material would sample as
** black.  Decode the image here (stb_image, cached) and read the exact pixel;
** anything that is not an image texture still goes through the engine.
*/
#define STB_IMAGE_IMPLEMENTATION
#define STBI_NO_STDIO
#define STBI_NO_HDR
#define STBI_NO_LINEAR
#define STBI_NO_PSD
#define STBI_NO_GIF
#define STBI_NO_PIC
#define STBI_NO_PNM
#include "../thirdparty/stb/stb_image.h"

struct HL2SB_ImageColorCache_t
{
    char m_szTextureName[MAX_PATH];
    int m_nWidth;
    int m_nHeight;
    CUtlVector< unsigned char > m_RGBA;
};

static CUtlVector< HL2SB_ImageColorCache_t * > s_ImageColorCache;

static const char *s_pImageTextureExtensions[] = { ".png", ".jpg", ".jpeg", ".tga", ".bmp" };

// HL2SB: case-insensitive "does pName end with pExt".
static bool HL2SB_NameEndsWith( const char *pName, const char *pExt )
{
    if ( !pName || !pExt )
        return false;

    const int nNameLen = Q_strlen( pName );
    const int nExtLen = Q_strlen( pExt );
    return ( nNameLen > nExtLen ) && ( !Q_stricmp( pName + nNameLen - nExtLen, pExt ) );
}

static HL2SB_ImageColorCache_t *HL2SB_FindCachedImage( const char *pTextureName )
{
    for ( int i = 0; i < s_ImageColorCache.Count(); ++i )
    {
        if ( !Q_stricmp( s_ImageColorCache[i]->m_szTextureName, pTextureName ) )
            return s_ImageColorCache[i];
    }

    return NULL;
}

// Returns NULL when the texture is not backed by an image file.
static HL2SB_ImageColorCache_t *HL2SB_GetImageColorCache( const char *pTextureName )
{
    if ( !pTextureName || !pTextureName[0] )
        return NULL;

    HL2SB_ImageColorCache_t *pCached = HL2SB_FindCachedImage( pTextureName );
    if ( pCached )
        return pCached->m_nWidth > 0 ? pCached : NULL;

    // Record the miss so a non-image texture is only probed for once.
    pCached = new HL2SB_ImageColorCache_t;
    Q_strncpy( pCached->m_szTextureName, pTextureName, sizeof( pCached->m_szTextureName ) );
    pCached->m_nWidth = 0;
    pCached->m_nHeight = 0;
    s_ImageColorCache.AddToTail( pCached );

    for ( int i = 0; i < ARRAYSIZE( s_pImageTextureExtensions ); ++i )
    {
        char szPath[MAX_PATH];
        // HL2SB: a name that already carries this image extension probes the
        // literal file -- image materials name their procedural texture after
        // the file (extension included), so IMaterial:GetColor hands us
        // "x/y.png" here and must find materials/x/y.png.
        if ( HL2SB_NameEndsWith( pTextureName, s_pImageTextureExtensions[i] ) )
            Q_snprintf( szPath, sizeof( szPath ), "materials/%s", pTextureName );
        else
            Q_snprintf( szPath, sizeof( szPath ), "materials/%s%s", pTextureName, s_pImageTextureExtensions[i] );

        if ( !g_pFullFileSystem->FileExists( szPath, "GAME" ) )
            continue;

        CUtlBuffer bufFile;
        if ( !g_pFullFileSystem->ReadFile( szPath, "GAME", bufFile ) || bufFile.TellPut() <= 0 )
            continue;

        int nWidth = 0, nHeight = 0, nChannels = 0;
        unsigned char *pRGBA = stbi_load_from_memory( (const stbi_uc *)bufFile.Base(), bufFile.TellPut(), &nWidth, &nHeight, &nChannels, 4 );

        if ( !pRGBA || nWidth <= 0 || nHeight <= 0 )
        {
            if ( pRGBA )
                stbi_image_free( pRGBA );
            continue;
        }

        pCached->m_nWidth = nWidth;
        pCached->m_nHeight = nHeight;
        pCached->m_RGBA.SetSize( nWidth * nHeight * 4 );
        Q_memcpy( pCached->m_RGBA.Base(), pRGBA, (size_t)nWidth * nHeight * 4 );
        stbi_image_free( pRGBA );
        return pCached;
    }

    return NULL;
}

/*
** HL2SB GMod compat: real IMaterials for image files (Material( "x.png" )).
**
** GMod decodes .png/.jpg/... at runtime and hands out a working material; this
** engine's FindMaterial only loads .vmt, so image paths came back as the error
** material and everything drawn with them was invisible (the Nyan Gun's bomb
** draws itself purely with Material("nyan/cat.png") quads).
**
** No image decoding happens here: CTextureManager::LoadTexture already carries
** the GMod-style image fallback (materialsystem/texturemanager.cpp, logs
** "[HL2SB] image texture ..."), so a material whose $basetexture points at the
** file path picks the decoded texture up on first bind.  This function only
** builds that wrapper material and caches it per path.  A path with no image
** file is remembered too, so Material() falls through to FindMaterial (error
** material, same as GMod) without re-probing.
*/
struct HL2SB_ImageMaterial_t
{
    char m_szName[MAX_PATH];
    IMaterial *m_pMaterial;
};

static CUtlVector< HL2SB_ImageMaterial_t > s_ImageMaterials;

// Returns NULL when pMaterialName is not a loadable image file.  pMaterialName
// must carry the extension ("nyan/cat.png"), which is GMod's documented form.
static IMaterial *HL2SB_FindOrCreateImageMaterial( const char *pMaterialName )
{
    for ( int i = 0; i < s_ImageMaterials.Count(); ++i )
    {
        if ( !Q_stricmp( s_ImageMaterials[i].m_szName, pMaterialName ) )
            return s_ImageMaterials[i].m_pMaterial;
    }

    IMaterial *pMaterial = NULL;

    char szImageFile[MAX_PATH];
    Q_snprintf( szImageFile, sizeof( szImageFile ), "materials/%s", pMaterialName );

    if ( g_pFullFileSystem->FileExists( szImageFile, "GAME" ) )
    {
        KeyValues *pKV = new KeyValues( "UnlitGeneric" );
        pKV->SetString( "$basetexture", pMaterialName );
        pKV->SetInt( "$translucent", 1 );
        pKV->SetInt( "$vertexcolor", 1 );
        pKV->SetInt( "$vertexalpha", 1 );
        // HL2SB: GMod sprite quads (render.DrawQuadEasy) face wherever the
        // script points them; draw both faces so a back-facing quad is a
        // mirrored sprite, not a vanish.
        pKV->SetInt( "$nocull", 1 );
        pMaterial = materials->CreateMaterial( pMaterialName, pKV );
        // CreateMaterial takes ownership of pKV; do not deleteThis().

        if ( pMaterial && pMaterial->IsErrorMaterial() )
            pMaterial = NULL;

        if ( pMaterial )
        {
            // The cache holds this material for the process lifetime; without
            // the reference the material system may cull it (same as
            // HL2SB_CreateMaterial below).
            pMaterial->IncrementReferenceCount();
            luasrc_LuaInfoMsgF( "[HL2SB] Material('%s') -> image material\n", pMaterialName );
        }
    }

    HL2SB_ImageMaterial_t &entry = s_ImageMaterials[s_ImageMaterials.AddToTail()];
    Q_strncpy( entry.m_szName, pMaterialName, sizeof( entry.m_szName ) );
    entry.m_pMaterial = pMaterial;

    return pMaterial;
}

// HL2SB GMod compat: IMaterial:SetInt( name, value ) / GetInt( name )
// / SetFloat / GetFloat (wiki: IMaterial).  The Nuke Pack's nukestrike cl_init
// opens with matTargetLight:SetInt("$spriterendermode", 9) and the whole client
// file failed to load without it.  Implemented over IMaterial::FindVar, which
// creates the var when missing (same as the material system's own users).
static int HL2SB_IMaterial_SetInt( lua_State *L )
{
    IMaterial *pMaterial = luaL_checkmaterial( L, 1 );
    const char *pszVar = luaL_checkstring( L, 2 );
    const int iValue = luaL_checkint( L, 3 );

    bool bFound = false;
    IMaterialVar *pVar = pMaterial->FindVar( pszVar, &bFound );
    if ( pVar != NULL )
        pVar->SetIntValue( iValue );
    return 0;
}

static int HL2SB_IMaterial_GetInt( lua_State *L )
{
    IMaterial *pMaterial = luaL_checkmaterial( L, 1 );
    const char *pszVar = luaL_checkstring( L, 2 );

    bool bFound = false;
    IMaterialVar *pVar = pMaterial->FindVar( pszVar, &bFound );
    lua_pushinteger( L, ( pVar != NULL ) ? pVar->GetIntValue() : 0 );
    return 1;
}

static int HL2SB_IMaterial_SetFloat( lua_State *L )
{
    IMaterial *pMaterial = luaL_checkmaterial( L, 1 );
    const char *pszVar = luaL_checkstring( L, 2 );
    const float flValue = (float)luaL_checknumber( L, 3 );

    bool bFound = false;
    IMaterialVar *pVar = pMaterial->FindVar( pszVar, &bFound );
    if ( pVar != NULL )
        pVar->SetFloatValue( flValue );
    return 0;
}

// HL2SB GMod compat (2026-09-22): IMaterial:SetTexture( varName, ITexture ).
// modules/halo.lua:106 does mat_Copy:SetTexture( "$basetexture", rt_Store ) --
// without this the call raised "attempt to call nil (method 'SetTexture')",
// aborted halo.lua's Render mid-frame (after render.Clear, before the scene
// restore) and left the SCREEN BLACK for as long as the beam was held.
static int HL2SB_IMaterial_SetTexture( lua_State *L )
{
    IMaterial *pMaterial = luaL_checkmaterial( L, 1 );
    const char *pszVar = luaL_checkstring( L, 2 );
    ITexture *pTexture = luaL_checkitexture( L, 3 );

    bool bFound = false;
    IMaterialVar *pVar = pMaterial->FindVar( pszVar, &bFound );
    if ( pVar != NULL && pTexture != NULL )
        pVar->SetTextureValue( pTexture );
    return 0;
}

// HL2SB GMod compat: IMaterial:SetString( varName, value ) -- halo.lua:107-108
// resets pp/copy's $color/$alpha every frame through this.
static int HL2SB_IMaterial_SetString( lua_State *L )
{
    IMaterial *pMaterial = luaL_checkmaterial( L, 1 );
    const char *pszVar = luaL_checkstring( L, 2 );
    const char *pszValue = luaL_checkstring( L, 3 );

    bool bFound = false;
    IMaterialVar *pVar = pMaterial->FindVar( pszVar, &bFound );
    if ( pVar != NULL )
        pVar->SetStringValue( pszValue );
    return 0;
}

static int HL2SB_IMaterial_GetFloat( lua_State *L )
{
    IMaterial *pMaterial = luaL_checkmaterial( L, 1 );
    const char *pszVar = luaL_checkstring( L, 2 );

    bool bFound = false;
    IMaterialVar *pVar = pMaterial->FindVar( pszVar, &bFound );
    lua_pushnumber( L, ( pVar != NULL ) ? pVar->GetFloatValue() : 0.0f );
    return 1;
}

static int HL2SB_IMaterial_GetColor( lua_State *L )
{
    IMaterial *pMaterial = luaL_checkmaterial( L, 1 );
    int x = luaL_checkint( L, 2 );
    int y = luaL_checkint( L, 3 );

    ITexture *pTexture = HL2SB_MaterialBaseTexture( pMaterial );

    if ( pTexture )
    {
        HL2SB_ImageColorCache_t *pImage = HL2SB_GetImageColorCache( pTexture->GetName() );

        if ( pImage )
        {
            // GMod samples with pixel coordinates and clamps out-of-range reads.
            x = Clamp( x, 0, pImage->m_nWidth - 1 );
            y = Clamp( y, 0, pImage->m_nHeight - 1 );

            const unsigned char *pPixel = &pImage->m_RGBA[( y * pImage->m_nWidth + x ) * 4];
            lua_pushcolor( L, Color( pPixel[0], pPixel[1], pPixel[2], pPixel[3] ) );
            return 1;
        }

        // VTF-backed material: the engine's own low-res sampling, normalised by
        // the texture size because GetLowResColorSample takes UVs.
        int nWidth = pTexture->GetActualWidth();
        int nHeight = pTexture->GetActualHeight();

        if ( nWidth > 0 && nHeight > 0 )
        {
            float color[3] = { 0.0f, 0.0f, 0.0f };
            pTexture->GetLowResColorSample( (float)x / (float)nWidth, (float)y / (float)nHeight, color );
            lua_pushcolor( L, Color( (int)( color[0] * 255.0f ), (int)( color[1] * 255.0f ), (int)( color[2] * 255.0f ), 255 ) );
            return 1;
        }
    }

    lua_pushcolor( L, Color( 255, 255, 255, 255 ) );
    return 1;
}

/*
** GMod's global Material( path ).  It has to be installed before
** lua/includes/extensions/gmod_surface.lua runs, because that file only
** installs its Lua material proxy "if ( Material == nil )".  Missing materials
** come back as the error material (mat:IsError() == true), which is what GMod
** hands out too.
*/
static int HL2SB_Material( lua_State *L )
{
    const char *pMaterialName = luaL_checkstring( L, 1 );

    // HL2SB GMod compat: image paths.  GMod hands out a decoded runtime
    // material for "x.png"/"x.jpg"/..., which the Nyan Gun's bomb sprite and
    // plenty of other addons rely on.  Only names carrying an image extension
    // take this path; everything else keeps the VMT lookup below.  (The gate
    // also keeps VMT names out of the image-material cache.)
    bool bImageName = false;
    for ( int i = 0; i < ARRAYSIZE( s_pImageTextureExtensions ); ++i )
    {
        if ( HL2SB_NameEndsWith( pMaterialName, s_pImageTextureExtensions[i] ) )
        {
            bImageName = true;
            break;
        }
    }

    if ( bImageName )
    {
        IMaterial *pImageMaterial = HL2SB_FindOrCreateImageMaterial( pMaterialName );
        if ( pImageMaterial )
        {
            lua_pushmaterial( L, pImageMaterial );
            return 1;
        }
        // Not a loadable image: fall through, FindMaterial returns the error
        // material -- which is what GMod hands out for a missing image too.
    }

    IMaterial *pMaterial = materials->FindMaterial( pMaterialName, TEXTURE_GROUP_VGUI, false );

    if ( !pMaterial )
    {
        lua_pushnil( L );
        return 1;
    }

    /*
    ** NOTE: do NOT "self-heal" this material with Refresh().  Tried and reverted:
    ** the image material has no .vmt on disk, so Refresh() goes to the file
    ** system, fails, and turns the material into an error material with
    **
    **   CMaterial::PrecacheVars: error loading vmt file for gwenskin/gmoddefault
    **
    ** on every frame.  The duplicate/procedural-material problem has to be
    ** fixed in the materialsystem (see the queue in cmaterialsystem.cpp).
    */
    // HL2SB: pin the material so it cannot be evicted between this call
    // (file-scope load time) and the first render.SetMaterial / DrawQuadEasy.
    // An evicted material gets re-resolved mid-render, which is the documented
    // "Binding uncached material ... artificially incrementing refcount" path
    // that crashes the Nyan Gun's bomb Draw (EXECUTE on heap).
    if ( !pMaterial->IsErrorMaterial() )
    {
        pMaterial->IncrementReferenceCount();
    }

    lua_pushmaterial( L, pMaterial );
    return 1;
}

/*
** HL2SB: CreateMaterial( name, paramsTable ) -- GMod's runtime material builder.
**
** paramsTable is a flat Lua table.  Key "shader" picks the shader (default
** "UnlitGeneric"); every other key becomes a VMT variable.  Numbers go in as
** floats, booleans as 0/1, strings verbatim.  The material is created (or
** replaced) via IMaterialSystem::CreateMaterial so subsequent Material( name )
** finds it.
*/
static int HL2SB_CreateMaterial( lua_State *L )
{
    // HL2SB GMod compat: GMod's signature is
    //     CreateMaterial( name, shaderName, paramsTable )
    // (wiki: Global.CreateMaterial).  This host originally shipped a 2-arg form
    //     CreateMaterial( name, paramsTable )        -- "shader" key in the table
    // and the Nuke Pack's detpack/missile cl_init called the GMod form, which
    // failed here with "bad argument #2 (table expected, got string)" and left
    // the whole entity unregistered.  Dispatch on the runtime types: a string in
    // slot 2 means the GMod form.
    const char *pName = luaL_checkstring( L, 1 );
    bool bGModForm = ( lua_type( L, 2 ) == LUA_TSTRING );
    int iParams = bGModForm ? 3 : 2;
    if ( !lua_istable( L, iParams ) )
    {
        luaL_error( L, "CreateMaterial: params table expected (got %s)", lua_typename( L, lua_type( L, iParams ) ) );
    }

    const char *pszShader = "UnlitGeneric";
    if ( bGModForm )
    {
        pszShader = luaL_checkstring( L, 2 );
    }
    else
    {
        lua_getfield( L, 2, "shader" );
        if ( lua_type( L, -1 ) == LUA_TSTRING )
            pszShader = lua_tostring( L, -1 );
        lua_pop( L, 1 );
    }

    KeyValues *pKV = new KeyValues( pszShader );

    // Walk the params table, skipping the "shader" key.
    lua_pushnil( L );
    while ( lua_next( L, iParams ) != 0 )
    {
        // key at -2, value at -1
        if ( lua_type( L, -2 ) == LUA_TSTRING )
        {
            const char *pszKey = lua_tostring( L, -2 );
            if ( pszKey[0] != '\0' && Q_stricmp( pszKey, "shader" ) != 0 )
            {
                if ( lua_type( L, -1 ) == LUA_TNUMBER )
                    pKV->SetFloat( pszKey, (float)lua_tonumber( L, -1 ) );
                else if ( lua_type( L, -1 ) == LUA_TBOOLEAN )
                    pKV->SetInt( pszKey, lua_toboolean( L, -1 ) ? 1 : 0 );
                else if ( lua_type( L, -1 ) == LUA_TSTRING )
                    pKV->SetString( pszKey, lua_tostring( L, -1 ) );
            }
        }
        lua_pop( L, 1 );
    }

    IMaterial *pMaterial = materials->CreateMaterial( pName, pKV );
    // CreateMaterial takes ownership of pKV; do not deleteThis().

    if ( !pMaterial || pMaterial->IsErrorMaterial() )
    {
        lua_pushnil( L );
        return 1;
    }

    pMaterial->IncrementReferenceCount();
    lua_pushmaterial( L, pMaterial );
    return 1;
}

/*
** Open render ITexture metatable
*/
LUALIB_API int luaopen_ITexture( lua_State *L )
{
    LUA_PUSH_NEW_METATABLE( L, LUA_ITEXTUREMETANAME );

    LUA_REGISTRATION_COMMIT( Texture );

    lua_pushvalue( L, -1 );           /* push metatable */
    lua_setfield( L, -2, "__index" ); /* metatable.__index = metatable */
    lua_pushstring( L, LUA_ITEXTUREMETANAME );
    lua_setfield( L, -2, "__type" ); /* metatable.__type = "Texture" */

    /* HL2SB: extend the IMaterial metatable with GMod's names. */
    luaL_getmetatable( L, LUA_MATERIALLIBNAME );
    if ( lua_istable( L, -1 ) )
    {
        lua_pushcfunction( L, HL2SB_IMaterial_GetTexture );
        lua_setfield( L, -2, "GetTexture" );

        lua_pushcfunction( L, HL2SB_IMaterial_Width );
        lua_setfield( L, -2, "Width" );

        lua_pushcfunction( L, HL2SB_IMaterial_Height );
        lua_setfield( L, -2, "Height" );

        lua_pushcfunction( L, HL2SB_IMaterial_IsError );
        lua_setfield( L, -2, "IsError" );

        // Overrides limaterial.cpp's version, which can only read VTF low-res
        // images (and therefore samples .png materials as black).
        lua_pushcfunction( L, HL2SB_IMaterial_GetColor );
        lua_setfield( L, -2, "GetColor" );

        lua_pushcfunction( L, HL2SB_IMaterial_SetInt );
        lua_setfield( L, -2, "SetInt" );
        lua_pushcfunction( L, HL2SB_IMaterial_GetInt );
        lua_setfield( L, -2, "GetInt" );
        lua_pushcfunction( L, HL2SB_IMaterial_SetFloat );
        lua_setfield( L, -2, "SetFloat" );
        lua_pushcfunction( L, HL2SB_IMaterial_GetFloat );
        lua_setfield( L, -2, "GetFloat" );
        lua_pushcfunction( L, HL2SB_IMaterial_SetTexture );
        lua_setfield( L, -2, "SetTexture" );
        lua_pushcfunction( L, HL2SB_IMaterial_SetString );
        lua_setfield( L, -2, "SetString" );
    }
    lua_pop( L, 1 );

    /* HL2SB: GMod's Material( path ) constructor. */
    lua_pushcfunction( L, HL2SB_Material );
    lua_setglobal( L, "Material" );

    /* HL2SB: GMod's CreateMaterial( name, paramsTable ).
    **
    ** paramsTable keys map to VMT variables; the "shader" key selects the
    ** shader (default UnlitGeneric).  Creates or replaces a named material
    ** at runtime -- useful for addons that build materials from params
    ** instead of shipping .vmt files.
    **
    ** Minimal but real: reads string/number/bool from the Lua table into a
    ** KeyValues tree, then materials->CreateMaterial(). */
    lua_pushcfunction( L, HL2SB_CreateMaterial );
    lua_setglobal( L, "CreateMaterial" );

    return 1;
}
