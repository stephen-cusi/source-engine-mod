#include "cbase.h"
#include "materialsystem/itexture.h"
#include "luamanager.h"
#include "luasrclib.h"
#include "litexture.h"
#include "lua/materialsystem/limaterial.h"
#include "materialsystem/imaterial.h"
#include "materialsystem/imaterialvar.h"
#include "filesystem.h"
#include "tier1/utlmap.h"
#include "hl2sb_framestats_cat.h"
#ifdef CLIENT_DLL
#include "rendertexture.h"
#include "view_scene.h"
#include "igamesystem.h"
#include "vtf/vtf.h"
#include "bitmap/imageformat.h"
#include <materialsystem/imaterialsystem.h>
#include "texture_group_names.h"
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

//-----------------------------------------------------------------------------
// HL2SB: immediate PNG -> RGBA -> vgui texture upload for the main-menu UI.
// File-backed vgui textures (DrawSetTextureFile) are owned by the texture
// manager and are invalidated on level transitions; after one map load every
// menu thumbnail drew as nothing while the manager kept retrying the load
// (the "images gone and laggy" report against the Lua start-game dialog).
// This mirrors gameui's PNGImagePanel: filesystem read, stbi decode, one
// immediate DrawSetTextureRGBA.  No manager entry, nothing to flush.
//-----------------------------------------------------------------------------
bool HL2SB_UploadPNGToTexture( int nTextureID, const char *pszPath )
{
	CUtlBuffer bufFile;
	if ( !g_pFullFileSystem->ReadFile( pszPath, "GAME", bufFile ) || bufFile.TellPut() <= 0 )
		return false;

	int nWidth = 0, nHeight = 0, nChannels = 0;
	unsigned char *pRGBA = stbi_load_from_memory( (const stbi_uc *)bufFile.Base(), bufFile.TellPut(), &nWidth, &nHeight, &nChannels, 4 );
	if ( pRGBA == NULL )
		return false;

	vgui::surface()->DrawSetTextureRGBA( nTextureID, pRGBA, nWidth, nHeight, false, true );
	stbi_image_free( pRGBA );
	return true;
}

struct HL2SB_ImageColorCache_t
{
    char m_szTextureName[MAX_PATH];
    int m_nWidth;
    int m_nHeight;
    CUtlVector< unsigned char > m_RGBA;
};

static CUtlVector< HL2SB_ImageColorCache_t * > s_ImageColorCache;

// s_ImageColorCache is a bare CUtlVector and stb_image keeps its failure state
// in a file-scope global, so the miss path (append + decode) has to be
// serialised - a second thread would otherwise push a duplicate entry and the
// two decodes would scribble over each other (issue #41, 7.4).
static CThreadFastMutex s_ImageColorCacheMutex;

static const char *s_pImageTextureExtensions[] = { ".png", ".jpg", ".jpeg", ".tga", ".cache", ".bmp" };

// HL2SB: case-insensitive "does pName end with pExt".
static bool HL2SB_NameEndsWith( const char *pName, const char *pExt )
{
    if ( !pName || !pExt )
        return false;

    const int nNameLen = Q_strlen( pName );
    const int nExtLen = Q_strlen( pExt );
    return ( nNameLen > nExtLen ) && ( !Q_stricmp( pName + nNameLen - nExtLen, pExt ) );
}

// HL2SB (2026-10-09): GMod's Material path validation (client Material.PathValidate):
// reject drive separators and relative escapes; everything else passes.
static bool HL2SB_ImagePathValid( const char *pName )
{
    if ( !pName || !pName[0] )
        return false;
    if ( Q_strstr( pName, ":" ) || Q_strstr( pName, "../" ) || Q_strstr( pName, "..\\" ) )
        return false;
    return true;
}

static bool HL2SB_DynamicImagePath( const char *input, char *output, size_t size )
{
    if ( !input || !input[0] || !output || size == 0 )
        return false;
    // The stock menu addresses the game-root backgrounds through materials/../backgrounds/.
    if ( !Q_strnicmp( input, "../backgrounds/", 15 ) || !Q_strnicmp( input, "..\\backgrounds\\", 15 ) )
        input += 3;
    if ( !HL2SB_ImagePathValid( input ) || input[0] == '/' || input[0] == '\\' || Q_strlen( input ) >= size )
        return false;
    Q_strncpy( output, input, static_cast<int>( size ) );
    Q_FixSlashes( output, '/' );
    return true;
}

static bool HL2SB_ReadDynamicImage( const char *name, CUtlVector<unsigned char> &pixels, int &width, int &height )
{
    char path[MAX_PATH];
    if ( !HL2SB_DynamicImagePath( name, path, sizeof(path) ) )
        return false;
    char materialPath[MAX_PATH];
    Q_snprintf( materialPath, sizeof(materialPath), "materials/%s", path );
    const bool spawnicon = !Q_strnicmp( path, "spawnicons/", 11 );
    const char *paths[] = { materialPath, materialPath, path, path };
    const char *ids[] = { spawnicon ? "MOD" : "GAME", spawnicon ? "GAME" : "MOD",
                          spawnicon ? "MOD" : "GAME", spawnicon ? "GAME" : "MOD" };
    for ( int i = 0; i < ARRAYSIZE(paths); ++i ) {
        CUtlBuffer file;
        if ( !g_pFullFileSystem->ReadFile( paths[i], ids[i], file ) || file.TellPut() <= 0 )
            continue;
        int channels = 0, w = 0, h = 0;
        AUTO_LOCK( s_ImageColorCacheMutex );
        if ( !stbi_info_from_memory( static_cast<const stbi_uc *>(file.Base()), file.TellPut(), &w, &h, &channels ) ||
             w <= 0 || h <= 0 || static_cast<int64>(w) * h * 4 > 0x7fffffff )
            continue;
        unsigned char *rgba = stbi_load_from_memory( static_cast<const stbi_uc *>(file.Base()), file.TellPut(), &w, &h, &channels, 4 );
        if ( !rgba ) continue;
        pixels.SetCount( w * h * 4 );
        Q_memcpy( pixels.Base(), rgba, static_cast<size_t>(w) * h * 4 );
        stbi_image_free( rgba );
        width = w;
        height = h;
        return true;
    }
    return false;
}

class CHL2SBDynamicImage : public ITextureRegenerator
{
public:
    explicit CHL2SBDynamicImage( const char *path ) { Q_strncpy( m_Path, path, sizeof(m_Path) ); }

    bool SetPath( const char *path )
    {
        char normalized[MAX_PATH];
        if ( !HL2SB_DynamicImagePath( path, normalized, sizeof(normalized) ) ) return false;
        AUTO_LOCK( m_Mutex );
        Q_strncpy( m_Path, normalized, sizeof(m_Path) );
        return true;
    }

    virtual void RegenerateTextureBits( ITexture *, IVTFTexture *texture, Rect_t * )
    {
        if ( !texture ) return;
        char path[MAX_PATH];
        {
            AUTO_LOCK( m_Mutex );
            Q_strncpy( path, m_Path, sizeof(path) );
        }
        CUtlVector<unsigned char> pixels;
        int sourceWidth = 0, sourceHeight = 0;
        const bool loaded = HL2SB_ReadDynamicImage( path, pixels, sourceWidth, sourceHeight );
        for ( int frame = 0; frame < texture->FrameCount(); ++frame ) {
            for ( int face = 0; face < texture->FaceCount(); ++face ) {
                for ( int mip = 0; mip < texture->MipCount(); ++mip ) {
                    int width = 0, height = 0, depth = 0;
                    texture->ComputeMipLevelDimensions( mip, &width, &height, &depth );
                    if ( width <= 0 || height <= 0 || depth != 1 || static_cast<int64>(width) * height * 4 > 0x7fffffff )
                        continue;
                    unsigned char *dest = texture->ImageData( frame, face, mip );
                    if ( !dest ) continue;
                    CUtlVector<unsigned char> scaled;
                    scaled.SetCount( width * height * 4 );
                    if ( loaded && width == sourceWidth && height == sourceHeight ) {
                        Q_memcpy( scaled.Base(), pixels.Base(), scaled.Count() );
                    } else if ( loaded ) {
                        ImageLoader::ResampleInfo_t info;
                        info.m_pSrc = pixels.Base();
                        info.m_pDest = scaled.Base();
                        info.m_nSrcWidth = sourceWidth;
                        info.m_nSrcHeight = sourceHeight;
                        info.m_nDestWidth = width;
                        info.m_nDestHeight = height;
                        info.m_flSrcGamma = 1.0f;
                        info.m_flDestGamma = 1.0f;
                        info.m_nFlags = ImageLoader::RESAMPLE_NICE_FILTER | ImageLoader::RESAMPLE_CLAMPS | ImageLoader::RESAMPLE_CLAMPT;
                        if ( !ImageLoader::ResampleRGBA8888( info ) )
                            Q_memset( scaled.Base(), 0xff, scaled.Count() );
                    } else {
                        Q_memset( scaled.Base(), 0xff, scaled.Count() );
                    }
                    ImageLoader::ConvertImageFormat( scaled.Base(), IMAGE_FORMAT_RGBA8888,
                        dest, texture->Format(), width, height );
                }
            }
        }
    }

    virtual void Release() { delete this; }

private:
    char m_Path[MAX_PATH];
    CThreadFastMutex m_Mutex;
};

struct HL2SBDynamicMaterialRecord
{
    IMaterial *material;
    ITexture *texture;
    CHL2SBDynamicImage *image;
};

static CUtlVector<HL2SBDynamicMaterialRecord> s_DynamicMaterials;

class CHL2SBDynamicImageSystem : public CAutoGameSystem
{
public:
    CHL2SBDynamicImageSystem() : CAutoGameSystem( "HL2SBDynamicImage" ) {}
    virtual void Shutdown()
    {
        if ( !materials ) return;
        MaterialLock_t lock = materials->Lock();
        for ( int i = 0; i < s_DynamicMaterials.Count(); ++i ) {
            HL2SBDynamicMaterialRecord &entry = s_DynamicMaterials[i];
            entry.texture->SetTextureRegenerator( NULL );
        }
        materials->Unlock( lock );
        // Queued draws may still hold these materials and textures while the
        // client DLL is unloading. The material system owns them until teardown.
        s_DynamicMaterials.Purge();
    }
};
static CHL2SBDynamicImageSystem s_DynamicImageSystem;

static int HL2SB_DynamicMaterial( lua_State *L )
{
    const char *path = luaL_checkstring( L, 1 );
    const char *digits = luaL_optstring( L, 2, "0000000" );
    if ( Q_strlen( digits ) < 7 ) digits = "0000000";
    char normalized[MAX_PATH];
    CUtlVector<unsigned char> pixels;
    int width = 0, height = 0;
    if ( !HL2SB_DynamicImagePath( path, normalized, sizeof(normalized) ) ||
         !HL2SB_ReadDynamicImage( normalized, pixels, width, height ) )
        return 0;

    static unsigned int serial = 0;
    char name[80];
    Q_snprintf( name, sizeof(name), "__hl2sb_dynamic_%u", ++serial );
    int flags = TEXTUREFLAGS_SINGLECOPY | TEXTUREFLAGS_PROCEDURAL | TEXTUREFLAGS_NOLOD |
                TEXTUREFLAGS_NOMIP | TEXTUREFLAGS_CLAMPS | TEXTUREFLAGS_CLAMPT | TEXTUREFLAGS_POINTSAMPLE;
    if ( digits[3] != '0' ) flags &= ~(TEXTUREFLAGS_NOMIP | TEXTUREFLAGS_NOLOD);
    if ( digits[4] != '0' ) flags &= ~(TEXTUREFLAGS_CLAMPS | TEXTUREFLAGS_CLAMPT);
    if ( digits[5] != '0' ) flags &= ~(TEXTUREFLAGS_POINTSAMPLE | TEXTUREFLAGS_NOLOD);
    ITexture *texture = materials->CreateProceduralTexture( name, TEXTURE_GROUP_VGUI, width, height, IMAGE_FORMAT_RGBA8888, flags );
    if ( !texture || IsErrorTexture(texture) ) return 0;
    texture->IncrementReferenceCount();
    CHL2SBDynamicImage *image = new CHL2SBDynamicImage( normalized );
    texture->SetTextureRegenerator( image );
    texture->Download();

    KeyValues *params = new KeyValues( digits[0] == '1' ? "VertexLitGeneric" : "UnlitGeneric" );
    params->SetString( "$basetexture", name );
    params->SetInt( "$vertexcolor", 1 );
    params->SetInt( "$realwidth", width );
    params->SetInt( "$realheight", height );
    if ( digits[1] == '1' ) params->SetInt( "$nocull", 1 );
    if ( digits[2] == '1' ) params->SetInt( "$alphatest", 1 );
    else if ( digits[2] == '0' && digits[0] != '1' ) params->SetInt( "$vertexalpha", 1 );
    if ( digits[6] == '1' ) params->SetInt( "$ignorez", 1 );
    IMaterial *material = materials->CreateMaterial( name, params );
    if ( !material || material->IsErrorMaterial() ) {
        texture->SetTextureRegenerator( NULL );
        texture->DecrementReferenceCount();
        if ( material ) material->DecrementReferenceCount();
        return 0;
    }
    bool found = false;
    IMaterialVar *base = material->FindVar( "$basetexture", &found, false );
    if ( found && base ) base->SetTextureValue( texture );
    HL2SBDynamicMaterialRecord entry = { material, texture, image };
    s_DynamicMaterials.AddToTail( entry );
    lua_pushmaterial( L, material );
    return 1;
}

static int HL2SB_IMaterial_SetDynamicImage( lua_State *L )
{
    IMaterial *material = luaL_checkmaterial( L, 1 );
    const char *path = luaL_checkstring( L, 2 );
    ITexture *texture = HL2SB_MaterialBaseTexture( material );
    if ( !texture || IsErrorTexture(texture) ) return 0;
    for ( int i = 0; i < s_DynamicMaterials.Count(); ++i ) {
        if ( s_DynamicMaterials[i].texture == texture ) {
            if ( !s_DynamicMaterials[i].image->SetPath( path ) ) return 0;
            texture->Download();
            return 0;
        }
    }
    return 0;
}

// Strips GMod's "!<7 digits>" image-parameter prefix so GetColor's cache can
// find the file behind a parameterised image texture.
static void HL2SB_StripImageParamPrefix( const char *pIn, char *pOut, int nOutLen )
{
    if ( pIn && pIn[0] == '!' )
    {
        int i = 1;
        while ( pIn[i] >= '0' && pIn[i] <= '9' && i < 8 )
            ++i;
        if ( i > 1 )
        {
            Q_strncpy( pOut, pIn + i, nOutLen );
            return;
        }
    }
    Q_strncpy( pOut, pIn ? pIn : "", nOutLen );
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

    // Parameterised image textures name their texture "!<digits><path>"; the
    // file on disk is named by the path alone.
    char szBareName[MAX_PATH];
    HL2SB_StripImageParamPrefix( pTextureName, szBareName, sizeof( szBareName ) );
    pTextureName = szBareName;

    AUTO_LOCK( s_ImageColorCacheMutex );

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
// HL2SB (2026-10-03): the cache was a CUtlVector scanned with one Q_stricmp
// per entry, so every Material() hit was O(entries) and an addon painting
// ~40 image textures ~300x per frame walked thousands of string compares per
// frame.  A name-ordered map turns the hit path into O(log entries) - one
// binary-search compare against the name - with no behaviour change.
static bool HL2SB_ImageMaterialLess( const char * const &pszLhs, const char * const &pszRhs )
{
    return Q_stricmp( pszLhs, pszRhs ) < 0;
}

// Key strings are heap-owned by the map (strdup at insert) and live for the
// process lifetime, matching the old per-entry MAX_PATH copies.
static CUtlMap< const char *, IMaterial * > s_ImageMaterials( HL2SB_ImageMaterialLess );

// s_ImageMaterials is shared state; two Material("x.png") calls from
// different threads would race the insert against the Find above.
static CThreadFastMutex s_ImageMaterialsMutex;

// Returns NULL when pMaterialName is not a loadable image file.  pMaterialName
// must carry the extension ("nyan/cat.png"), which is GMod's documented form.
// pDigits is GMod's pngParameters digit string ("0000000" for the default);
// the material is created under GMod's "!"-prefixed combined name so two
// parameter sets never share a texture.
//
// 2026-10-09 GMod alignment: shader / flags / translucency now follow the
// digits exactly like CResources::CreateMaterialFromTextureFile
//   [0] '1' -> VertexLitGeneric, else UnlitGeneric
//   [1] '1' -> $nocull 1
//   [2] '1' -> $alphatest 1; '0' -> $vertexalpha 1 (Unlit only); >=2 -> neither
//   [6] '1' -> $ignorez 1
// and the unconditional $translucent/$nocull the old synthesis applied are
// gone (GMod has neither by default).  A path with no image file is no longer
// cached either -- GMod re-probes every call and hands out the error material.
IMaterial *HL2SB_FindImageMaterial( const char *pMaterialName, const char *pDigits )
{
    char szKey[MAX_PATH];
    Q_snprintf( szKey, sizeof( szKey ), "!%s%s", pDigits, pMaterialName );

    AUTO_LOCK( s_ImageMaterialsMutex );

    int iFound = s_ImageMaterials.Find( szKey );
    if ( iFound != s_ImageMaterials.InvalidIndex() )
    {
        // GMod reuses a cached FreeImage material only while it (and its
        // basetexture) is not the error material.
        IMaterial *pCached = s_ImageMaterials[iFound];
        ITexture *pTexture = pCached && !pCached->IsErrorMaterial()
            ? HL2SB_MaterialBaseTexture( pCached ) : NULL;
        if ( pTexture && !IsErrorTexture( pTexture ) )
            return pCached;
    }

    IMaterial *pMaterial = NULL;

    char szImageFile[MAX_PATH];
    Q_snprintf( szImageFile, sizeof( szImageFile ), "materials/%s", pMaterialName );

    if ( g_pFullFileSystem->FileExists( szImageFile, "GAME" ) ||
         g_pFullFileSystem->FileExists( pMaterialName, "GAME" ) )
    {
        // Resolve the raw image before publishing the wrapper material.  A
        // VGUI consumer may bind it immediately, and deferring this work lets
        // the normal material path try to load entities/jeep as a VMT.
        materials->FindTexture( szKey, TEXTURE_GROUP_VGUI );

        KeyValues *pKV = new KeyValues( ( pDigits[0] == '1' ) ? "VertexLitGeneric" : "UnlitGeneric" );
        pKV->SetString( "$basetexture", szKey );
        pKV->SetInt( "$vertexcolor", 1 );
        if ( pDigits[1] == '1' )
            pKV->SetInt( "$nocull", 1 );
        if ( pDigits[2] == '1' )
        {
            pKV->SetInt( "$alphatest", 1 );
        }
        else if ( pDigits[2] == '0' && pDigits[0] != '1' )
        {
            pKV->SetInt( "$vertexalpha", 1 );
        }
        if ( pDigits[6] == '1' )
            pKV->SetInt( "$ignorez", 1 );
        if ( iFound != s_ImageMaterials.InvalidIndex() && s_ImageMaterials[iFound] )
        {
            // Keep existing Lua and VGUI references on the same material.
            pMaterial = s_ImageMaterials[iFound];
            pMaterial->SetShaderAndParams( pKV );
            pKV->deleteThis();
        }
        else
        {
            // CreateMaterial takes ownership of pKV.
            pMaterial = materials->CreateMaterial( szKey, pKV );
        }

        if ( pMaterial && pMaterial->IsErrorMaterial() )
            pMaterial = NULL;

        if ( pMaterial && iFound == s_ImageMaterials.InvalidIndex() )
        {
            // The cache holds this material for the process lifetime; without
            // the reference the material system may cull it (same as
            // HL2SB_CreateMaterial below).
            pMaterial->IncrementReferenceCount();
        }
    }

    if ( pMaterial && iFound == s_ImageMaterials.InvalidIndex() )
        s_ImageMaterials.Insert( strdup( szKey ), pMaterial );

    return pMaterial;
}

// Material variables are looked up without creating missing parameters.
static int HL2SB_IMaterial_SetInt( lua_State *L )
{
    IMaterial *pMaterial = luaL_checkmaterial( L, 1 );
    const char *pszVar = luaL_checkstring( L, 2 );
    bool bFound = false;
    IMaterialVar *pVar = pMaterial->FindVar( pszVar, &bFound, false );
    if ( pVar != NULL && bFound )
    {
        const int iValue = (int)luaL_checknumber( L, 3 );
        if ( pVar->GetIntValue() != iValue )
        {
            pVar->SetIntValue( iValue );
            pMaterial->RecomputeStateSnapshots();
        }
    }
    return 0;
}

static int HL2SB_IMaterial_GetInt( lua_State *L )
{
    IMaterial *pMaterial = luaL_checkmaterial( L, 1 );
    const char *pszVar = luaL_checkstring( L, 2 );

    bool bFound = false;
    IMaterialVar *pVar = pMaterial->FindVar( pszVar, &bFound, false );
    if ( pVar == NULL || !bFound )
        return 0;
    lua_pushinteger( L, pVar->GetIntValue() );
    return 1;
}

static int HL2SB_IMaterial_SetFloat( lua_State *L )
{
    IMaterial *pMaterial = luaL_checkmaterial( L, 1 );
    const char *pszVar = luaL_checkstring( L, 2 );
    bool bFound = false;
    IMaterialVar *pVar = pMaterial->FindVar( pszVar, &bFound, false );
    if ( pVar != NULL && bFound )
        pVar->SetFloatValue( (float)luaL_checknumber( L, 3 ) );
    return 0;
}

// HL2SB GMod compat: IMaterial:SetVector( varName, Vector ) / GetVector --
// the matproxy result write hl1sweps' proxies use
// (hl1_matproxy.lua: mat:SetVector( self.ResultTo, col )).  GMod creates the
// variable when the .vmt does not declare it; the material system's
// IMaterialVar::Create factory is not reachable from the client link in this
// engine, so a missing variable is skipped instead.  Every proxy result the
// hl1sweps vmts actually write ($envmaptint, $color2) is a shader-registered
// variable and resolves through FindVar.
static int HL2SB_IMaterial_SetVector( lua_State *L )
{
    IMaterial *pMaterial = luaL_checkmaterial( L, 1 );
    const char *pszVar = luaL_checkstring( L, 2 );
    const Vector &vec = luaL_checkvector( L, 3 );

    bool bFound = false;
    IMaterialVar *pVar = pMaterial->FindVar( pszVar, &bFound );
    if ( pVar != NULL && bFound )
        pVar->SetVecValue( vec.x, vec.y, vec.z );
    return 0;
}

static int HL2SB_IMaterial_GetVector( lua_State *L )
{
    IMaterial *pMaterial = luaL_checkmaterial( L, 1 );
    const char *pszVar = luaL_checkstring( L, 2 );

    bool bFound = false;
    IMaterialVar *pVar = pMaterial->FindVar( pszVar, &bFound );
    if ( pVar != NULL && bFound )
    {
        Vector vec( 0, 0, 0 );
        pVar->GetVecValue( &vec.x, 3 );
        lua_pushvector( L, vec );
    }
    else
    {
        lua_pushvector( L, Vector( 0, 0, 0 ) );
    }
    return 1;
}

// HL2SB GMod compat (2026-09-22): IMaterial:SetTexture( varName, ITexture ).
// modules/halo.lua:106 does mat_Copy:SetTexture( "$basetexture", rt_Store ) --
// without this the call raised "attempt to call nil (method 'SetTexture')",
// aborted halo.lua's Render mid-frame (after render.Clear, before the scene
// restore) and left the SCREEN BLACK for as long as the beam was held.
//
// HL2SB (2026-10-08): GMod's IMaterial:SetTexture also accepts a texture NAME
// string as the second argument (wiki: "string or ITexture texture"; GMod's
// own matproxy/sky_paint.lua passes GetDTString( 0 ) straight through) --
// resolve it through materials->FindTexture instead of erroring.
static int HL2SB_IMaterial_SetTexture( lua_State *L )
{
    IMaterial *pMaterial = luaL_checkmaterial( L, 1 );
    const char *pszVar = luaL_checkstring( L, 2 );
    bool bFound = false;
    IMaterialVar *pVar = pMaterial->FindVar( pszVar, &bFound, false );
    if ( pVar == NULL || !bFound )
        return 0;
    ITexture *pTexture = NULL;
    if ( lua_type( L, 3 ) == LUA_TSTRING )
    {
        pTexture = materials->FindTexture( lua_tostring( L, 3 ), "" );
    }
    else
    {
        pTexture = luaL_checkitexture( L, 3 );
    }

    CMatRenderContextPtr pRenderContext( materials );
    ITexture *pCurrent = ( pVar->GetType() == MATERIAL_VAR_TYPE_TEXTURE || pRenderContext->GetCallQueue() )
        ? pVar->GetTextureValue() : materials->FindTexture( "error", TEXTURE_GROUP_OTHER, true );
    if ( pTexture != NULL && pTexture != pCurrent )
    {
        pVar->SetTextureValue( pTexture );
        pMaterial->RecomputeStateSnapshots();
    }
    return 0;
}

// HL2SB GMod compat: IMaterial:SetString( varName, value ) -- halo.lua:107-108
// resets pp/copy's $color/$alpha every frame through this.
static int HL2SB_IMaterial_SetString( lua_State *L )
{
    IMaterial *pMaterial = luaL_checkmaterial( L, 1 );
    const char *pszVar = luaL_checkstring( L, 2 );
    bool bFound = false;
    IMaterialVar *pVar = pMaterial->FindVar( pszVar, &bFound, false );
    if ( pVar != NULL && bFound )
        pVar->SetStringValue( luaL_checkstring( L, 3 ) );
    return 0;
}

static int HL2SB_IMaterial_GetFloat( lua_State *L )
{
    IMaterial *pMaterial = luaL_checkmaterial( L, 1 );
    const char *pszVar = luaL_checkstring( L, 2 );

    bool bFound = false;
    IMaterialVar *pVar = pMaterial->FindVar( pszVar, &bFound, false );
    if ( pVar == NULL || !bFound )
        return 0;
    lua_pushnumber( L, pVar->GetFloatValue() );
    return 1;
}

static int HL2SB_IMaterial_GetShader( lua_State *L )
{
    IMaterial *pMaterial = luaL_checkmaterial( L, 1 );
    lua_pushstring( L, pMaterial ? pMaterial->GetShaderName() : "" );
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
** GMod's global Material( path [, pngParameters] ).  It has to be installed
** before lua/includes/extensions/gmod_surface.lua runs, because that file
** defines the words->digits Lua wrapper over C_Material.  Missing materials
** come back as the error material (mat:IsError() == true), which is what GMod
** hands out too.
**
** 2026-10-09 GMod alignment: the second argument is GMod's 7-digit
** pngParameters string (the Lua wrapper in gmod_surface.lua converts the
** words table exactly like GMod's util.lua).  The C binding is registered as
** both "C_Material" (GMod's name) and "Material" (fallback if the extension
** never loads); the image path returns ( IMaterial, creationSeconds ) like
** GMod's C_Material.
*/
static int HL2SB_Material( lua_State *L )
{
    const char *pMaterialName = luaL_checkstring( L, 1 );
    const char *pDigits = luaL_optstring( L, 2, "0000000" );
    if ( Q_strlen( pDigits ) < 7 )
        pDigits = "0000000";

    const float flStart = Plat_FloatTime();

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

    if ( bImageName && HL2SB_ImagePathValid( pMaterialName ) )
    {
        HL2SB_FrameStatsCatScope fcScope( HL2SB_FCAT_MATERIAL_IMG );
        IMaterial *pImageMaterial = HL2SB_FindImageMaterial( pMaterialName, pDigits );
        if ( pImageMaterial )
        {
            lua_pushmaterial( L, pImageMaterial );
            lua_pushnumber( L, (lua_Number)( Plat_FloatTime() - flStart ) );
            return 2;
        }
        // Preserve the normal lookup's error result so ContentIcon can try
        // the legacy VGUI/entities material when the PNG does not exist.
    }

    HL2SB_FrameStatsCatScope fcScope( HL2SB_FCAT_MATERIAL_VMT );
    IMaterial *pMaterial = materials->FindMaterial( pMaterialName, TEXTURE_GROUP_VGUI, false );

    if ( !pMaterial )
    {
        lua_pushnil( L );
        lua_pushnumber( L, (lua_Number)( Plat_FloatTime() - flStart ) );
        return 2;
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
    lua_pushnumber( L, (lua_Number)( Plat_FloatTime() - flStart ) );
    return 2;
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
        if ( L == LGameUI ) {
            lua_pushcfunction( L, HL2SB_IMaterial_SetDynamicImage );
            lua_setfield( L, -2, "SetDynamicImage" );
        }
        lua_pushcfunction( L, HL2SB_IMaterial_SetFloat );
        lua_setfield( L, -2, "SetFloat" );
        lua_pushcfunction( L, HL2SB_IMaterial_GetFloat );
        lua_setfield( L, -2, "GetFloat" );
        // HL2SB: IMaterial:SetVector / GetVector -- the matproxy result write
        // (hl1sweps' weapon-colour proxy).
        lua_pushcfunction( L, HL2SB_IMaterial_SetVector );
        lua_setfield( L, -2, "SetVector" );
        lua_pushcfunction( L, HL2SB_IMaterial_GetVector );
        lua_setfield( L, -2, "GetVector" );
        lua_pushcfunction( L, HL2SB_IMaterial_SetTexture );
        lua_setfield( L, -2, "SetTexture" );
        lua_pushcfunction( L, HL2SB_IMaterial_SetString );
        lua_setfield( L, -2, "SetString" );

        // HL2SB: IMaterial:GetShader() -- the shader NAME.  The stock DImage
        // branches on it (lua/vgui/dimage.lua:122: string.find on
        // "VertexLitGeneric"/"Cable" to pick its UV path); without the binding
        // that call was "attempt to call a nil value (method 'GetShader')".
        lua_pushcfunction( L, HL2SB_IMaterial_GetShader );
        lua_setfield( L, -2, "GetShader" );
    }
    lua_pop( L, 1 );

    /* HL2SB: GMod's Material( path ) constructor.  Registered under GMod's
    ** real C name "C_Material" (gmod_surface.lua's Lua Material wrapper calls
    ** it) AND as "Material" so a broken extension load can never leave the
    ** global missing -- the Lua wrapper overwrites "Material" when it loads. */
    lua_pushcfunction( L, HL2SB_Material );
    lua_setglobal( L, "C_Material" );
    lua_pushcfunction( L, HL2SB_Material );
    lua_setglobal( L, "Material" );

    if ( L == LGameUI ) {
        lua_pushcfunction( L, HL2SB_DynamicMaterial );
        lua_setglobal( L, "DynamicMaterial" );
    }

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
