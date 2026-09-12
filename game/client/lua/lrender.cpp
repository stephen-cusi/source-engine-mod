#include "cbase.h"
#include "materialsystem/itexture.h"
#include "luamanager.h"
#include "luasrclib.h"
#include "lrender.h"
#ifdef CLIENT_DLL
#include "rendertexture.h"
#include "view_scene.h"
#include <materialsystem/imaterialsystem.h>
#include <materialsystem/imesh.h>
#include <vgui/ISurface.h>
#include <vgui_controls/Controls.h>
#include "mathlib/lvector.h"
#include <lColor.h>
#include <renderparm.h>
#include <view.h>
#include <litexture.h>
#include <shaderapi/ishaderapi.h>
#include <utlstack.h>
#include "beamdraw.h"
#include "materialsystem/limaterial.h"
#include "iviewrender_beams.h"
#include <mathlib/lvmatrix.h>
#endif

// memdbgon must be the last include file in a .cpp file!!!
#include "tier0/memdbgon.h"

LUA_REGISTRATION_INIT( Renders );

#ifdef CLIENT_DLL

// Minifaction and magnification filter stacks
static CUtlStack< ShaderAPITextureHandle_t > filterTextureHandlesMinification;
static CUtlStack< ShaderAPITextureHandle_t > filterTextureHandlesMagnification;

// HL2SB: last material bound by render.SetMaterial, so DrawBeam can pass it
// explicitly to CBeamSegDraw::Start (relying on the bound-material path made
// the Nyan Gun's rainbow tracer invisible).
static IMaterial *g_pHL2SBLastBoundMaterial = NULL;

// HL2SB: path -> IMaterial cache.  SetMaterial is called every frame from
// ENT:Draw and effect Render; re-resolving through FindMaterialEx each time
// re-ran the PNG-synthesise path mid-render, which is the documented
// "creating materials while drawing crashes" landmine.  Resolve once, reuse.
struct HL2SB_MatCacheEntry_t { IMaterial *pMat; };
static CUtlDict<HL2SB_MatCacheEntry_t, unsigned short> s_MatCache;

#ifdef GAME_DLL

LUA_BINDING_BEGIN( Renders, SpawnBeam, "library", "Spawns a temporary beam entity to render a beam. The alpha of the color servers as the brightness of it.", "server" )
{
    Vector start = LUA_BINDING_ARGUMENT( luaL_checkvector, 1, "start" );
    Vector end = LUA_BINDING_ARGUMENT( luaL_checkvector, 2, "end" );
    int modelIndex = LUA_BINDING_ARGUMENT( luaL_checknumber, 3, "modelIndex" );
    int haloIndex = LUA_BINDING_ARGUMENT( luaL_checknumber, 4, "haloIndex" );
    unsigned char frameStart = LUA_BINDING_ARGUMENT( luaL_checknumber, 5, "frameStart" );
    unsigned char frameRate = LUA_BINDING_ARGUMENT( luaL_checknumber, 6, "frameRate" );
    float life = LUA_BINDING_ARGUMENT( luaL_checknumber, 7, "life" );
    unsigned char width = LUA_BINDING_ARGUMENT( luaL_checknumber, 8, "width" );
    unsigned char endWidth = LUA_BINDING_ARGUMENT( luaL_checknumber, 9, "endWidth" );
    unsigned char fadeLength = LUA_BINDING_ARGUMENT( luaL_checknumber, 10, "fadeLength" );
    unsigned char noise = LUA_BINDING_ARGUMENT( luaL_checknumber, 11, "noise" );
    lua_Color colorAndBrightness = LUA_BINDING_ARGUMENT( luaL_checkcolor, 12, "colorAndBrightness" );
    unsigned char speed = LUA_BINDING_ARGUMENT( luaL_checknumber, 13, "speed" );

    UTIL_Beam(
        start,
        end,
        modelIndex,
        haloIndex,
        frameStart,
        frameRate,
        life,
        width,
        endWidth,
        fadeLength,
        noise,
        colorAndBrightness.r(),
        colorAndBrightness.g(),
        colorAndBrightness.b(),
        colorAndBrightness.a(),
        speed );

    return 0;
}
LUA_BINDING_END()

#endif

LUA_BINDING_BEGIN( Renders, GetRenderTarget, "library", "Get the currently active render target.", "client" )
{
    CMatRenderContextPtr pRenderContext( materials );
    ITexture *pTexture = pRenderContext->GetRenderTarget();
    lua_pushitexture( L, pTexture );

    return 1;
}
LUA_BINDING_END( "Texture", "The currently active render target texture." )

LUA_BINDING_BEGIN( Renders, CopyRenderTargetToTexture, "library", "Copies the currently active Render Target to the specified texture.", "client" )
{
    CMatRenderContextPtr pRenderContext( materials );
    ITexture *pTexture = LUA_BINDING_ARGUMENT( luaL_checkitexture, 1, "texture" );
    pRenderContext->CopyRenderTargetToTexture( pTexture );

    return 0;
}
LUA_BINDING_END()

LUA_BINDING_BEGIN( Renders, CreateRenderTargetTextureEx, "library", "Create a new render target texture.", "client" )
{
    const char *name = LUA_BINDING_ARGUMENT( luaL_checkstring, 1, "name" );
    int width = LUA_BINDING_ARGUMENT( luaL_checknumber, 2, "width" );
    int height = LUA_BINDING_ARGUMENT( luaL_checknumber, 3, "height" );
    int sizeMode = LUA_BINDING_ARGUMENT_WITH_DEFAULT( luaL_optnumber, 4, RT_SIZE_DEFAULT, "sizeMode" );
    int depthMode = LUA_BINDING_ARGUMENT_WITH_DEFAULT( luaL_optnumber, 5, MATERIAL_RT_DEPTH_SHARED, "depthMode" );
    int textureFlags = LUA_BINDING_ARGUMENT_WITH_DEFAULT( luaL_optnumber, 6, TEXTUREFLAGS_CLAMPS | TEXTUREFLAGS_CLAMPT, "textureFlags" );
    int renderTargetFlags = LUA_BINDING_ARGUMENT_WITH_DEFAULT( luaL_optnumber, 7, 0, "renderTargetFlags" );
    int imageFormat = LUA_BINDING_ARGUMENT_WITH_DEFAULT( luaL_optnumber, 8, IMAGE_FORMAT_RGBA8888, "imageFormat" );

    g_pMaterialSystem->OverrideRenderTargetAllocation( true );
    ITexture *pTexture = materials->CreateNamedRenderTargetTextureEx(
        name,
        width,
        height,
        ( RenderTargetSizeMode_t )sizeMode,
        ( ImageFormat )imageFormat,
        ( MaterialRenderTargetDepth_t )depthMode,
        textureFlags,
        renderTargetFlags );
    g_pMaterialSystem->OverrideRenderTargetAllocation( false );

    lua_pushitexture( L, pTexture );

    return 1;
}
LUA_BINDING_END( "Texture", "The created render target texture." )

LUA_BINDING_BEGIN( Renders, CullMode, "library", "Sets the cull mode that decides how back faces are culled when rendering geometry.", "client" )
{
    CMatRenderContextPtr pRenderContext( materials );
    MaterialCullMode_t cullMode = LUA_BINDING_ARGUMENT_ENUM( MaterialCullMode_t, 1, "cullMode" );
    pRenderContext->CullMode( cullMode );

    return 0;
}
LUA_BINDING_END()

LUA_BINDING_BEGIN( Renders, GetScreenEffectTexture, "library", "Get the screen effect texture.", "client" )
{
    lua_ITexture *pTexture = GetFullFrameFrameBufferTexture( LUA_BINDING_ARGUMENT( luaL_checknumber, 1, "textureIndex" ) );
    lua_pushitexture( L, pTexture );
    return 1;
}
LUA_BINDING_END( "Texture", "The screen effect texture." )

LUA_BINDING_BEGIN( Renders, SetClippingEnabled, "library", "Set the clipping enabled.", "client" )
{
    CMatRenderContextPtr pRenderContext( materials );
    pRenderContext->EnableClipping( LUA_BINDING_ARGUMENT( lua_toboolean, 1, "isEnabled" ) );
    return 0;
}
LUA_BINDING_END()

LUA_BINDING_BEGIN( Renders, UpdateScreenEffectTexture, "library", "Update the screen effect texture.", "client" )
{
    const CViewSetup *pViewSetup = view->GetViewSetup();
    UpdateScreenEffectTexture(
        LUA_BINDING_ARGUMENT_WITH_DEFAULT(
            luaL_optnumber, 1, 0, "textureIndex" ),
        pViewSetup->x,
        pViewSetup->y,
        pViewSetup->width,
        pViewSetup->height );
    return 0;
}
LUA_BINDING_END()

LUA_BINDING_BEGIN( Renders, PushView3D, "library", "Push a 3D view.", "client" )
{
    CViewSetup playerView = *view->GetPlayerViewSetup();

    Vector origin = LUA_BINDING_ARGUMENT_WITH_DEFAULT( luaL_optvector, 1, &playerView.origin, "origin" );
    QAngle angles = LUA_BINDING_ARGUMENT_WITH_DEFAULT( luaL_optangle, 2, &playerView.angles, "angles" );
    float fov = LUA_BINDING_ARGUMENT_WITH_DEFAULT( luaL_optnumber, 3, playerView.fov, "fov" );
    int x = LUA_BINDING_ARGUMENT_WITH_DEFAULT( luaL_optnumber, 4, playerView.x, "x" );
    int y = LUA_BINDING_ARGUMENT_WITH_DEFAULT( luaL_optnumber, 5, playerView.y, "y" );
    int width = LUA_BINDING_ARGUMENT_WITH_DEFAULT( luaL_optnumber, 6, playerView.width, "width" );
    int height = LUA_BINDING_ARGUMENT_WITH_DEFAULT( luaL_optnumber, 7, playerView.height, "height" );
    float zNear = LUA_BINDING_ARGUMENT_WITH_DEFAULT( luaL_optnumber, 8, playerView.zNear, "zNear" );
    float zFar = LUA_BINDING_ARGUMENT_WITH_DEFAULT( luaL_optnumber, 9, playerView.zFar, "zFar" );

    CViewSetup viewSetup( playerView );
    viewSetup.origin = origin;
    viewSetup.angles = angles;
    viewSetup.fov = fov;
    viewSetup.x = x;
    viewSetup.y = y;
    viewSetup.width = width;
    viewSetup.height = height;
    viewSetup.zNear = zNear;
    viewSetup.zFar = zFar;

    render->Push3DView( viewSetup, 0, NULL, view->GetFrustum() );

    return 0;
}
LUA_BINDING_END()

LUA_BINDING_BEGIN( Renders, PopView3D, "library", "Pop a 3D view.", "client" )
{
    render->PopView( view->GetFrustum() );
    return 0;
}
LUA_BINDING_END()

LUA_BINDING_BEGIN( Renders, PushView2D, "library", "Push a 2D view.", "client" )
{
    CViewSetup viewSetup = *view->GetPlayerViewSetup();
    render->Push2DView( viewSetup, 0, NULL, view->GetFrustum() );
    return 0;
}
LUA_BINDING_END()

LUA_BINDING_BEGIN( Renders, PopView2D, "library", "Pop a 2D view.", "client" )
{
    render->PopView( view->GetFrustum() );
    return 0;
}
LUA_BINDING_END()

LUA_BINDING_BEGIN( Renders, PushCustomClipPlane, "library", "Push a custom clip plane.", "client" )
{
    Vector normal = LUA_BINDING_ARGUMENT( luaL_checkvector, 1, "normal" );
    float distance = LUA_BINDING_ARGUMENT( luaL_checknumber, 2, "distance" );

    Vector4D plane;
    VectorCopy( normal, plane.AsVector3D() );
    plane.w = distance + 0.1f;

    CMatRenderContextPtr pRenderContext( materials );
    pRenderContext->PushCustomClipPlane( plane.Base() );

    return 0;
}
LUA_BINDING_END()

LUA_BINDING_BEGIN( Renders, PopCustomClipPlane, "library", "Pop a custom clip plane.", "client" )
{
    CMatRenderContextPtr pRenderContext( materials );
    pRenderContext->PopCustomClipPlane();

    return 0;
}
LUA_BINDING_END()

LUA_BINDING_BEGIN( Renders, GetModelMatrix, "library", "Get the model matrix.", "client" )
{
    VMatrix matrix;
    CMatRenderContextPtr pRenderContext( materials );
    pRenderContext->GetMatrix( MATERIAL_MODEL, &matrix );
    lua_pushvmatrix( L, matrix );

    return 1;
}
LUA_BINDING_END( "VMatrix", "The model matrix." )

LUA_BINDING_BEGIN( Renders, PushModelMatrix, "library", "Push a model matrix.", "client" )
{
    VMatrix matrix = LUA_BINDING_ARGUMENT( luaL_checkvmatrix, 1, "matrix" );
    bool shouldMultiply = LUA_BINDING_ARGUMENT_WITH_DEFAULT( luaL_optboolean, 2, false, "shouldMultiply" );

    CMatRenderContextPtr pRenderContext( materials );
    pRenderContext->MatrixMode( MATERIAL_MODEL );
    pRenderContext->PushMatrix();

    // TODO: Is this the correct implementation, the same as how GMod does it?
    if ( shouldMultiply )
    {
        pRenderContext->LoadMatrix( matrix );
    }
    else
    {
        pRenderContext->LoadIdentity();
        pRenderContext->LoadMatrix( matrix );
    }

    return 0;
}
LUA_BINDING_END()

LUA_BINDING_BEGIN( Renders, PopModelMatrix, "library", "Pop a model matrix.", "client" )
{
    CMatRenderContextPtr pRenderContext( materials );
    pRenderContext->MatrixMode( MATERIAL_MODEL );
    pRenderContext->PopMatrix();

    return 0;
}
LUA_BINDING_END()

LUA_BINDING_BEGIN( Renders, SuppressEngineLighting, "library", "Suppress engine lighting.", "client" )
{
    modelrender->SuppressEngineLighting( LUA_BINDING_ARGUMENT( lua_toboolean, 1, "suppress" ) );
    return 0;
}
LUA_BINDING_END()

LUA_BINDING_BEGIN( Renders, SetLightingOrigin, "library", "Set the lighting origin.", "client" )
{
    CMatRenderContextPtr pRenderContext( materials );
    pRenderContext->SetLightingOrigin( LUA_BINDING_ARGUMENT( luaL_checkvector, 1, "lightingOrigin" ) );
    return 0;
}
LUA_BINDING_END()

/*
directionFace:
    TODO: Check that these match the values in the engine
    BOX_FRONT	0	Place the light from the front
    BOX_BACK	1	Place the light behind
    BOX_RIGHT	2	Place the light to the right
    BOX_LEFT	3	Place the light to the left
    BOX_TOP	4	Place the light to the top
    BOX_BOTTOM	5	Place the light to the bottom
*/
LUA_BINDING_BEGIN( Renders, SetAmbientLightCube, "library", "Set the ambient light cube.", "client" )
{
    int directionFace = LUA_BINDING_ARGUMENT( luaL_checknumber, 1, "directionFace" );
    float r = LUA_BINDING_ARGUMENT( luaL_checknumber, 2, "r" );
    float g = LUA_BINDING_ARGUMENT( luaL_checknumber, 3, "g" );
    float b = LUA_BINDING_ARGUMENT( luaL_checknumber, 4, "b" );
    static Vector4D cubeFaces[6];
    int i;

    for ( i = 0; i < 6; i++ )
    {
        if ( i == directionFace )
        {
            cubeFaces[i].Init( r, g, b, 1.0f );
        }
        else
        {
            cubeFaces[i].Init( 0.0f, 0.0f, 0.0f, 0.0f );
        }
    }

    CMatRenderContextPtr pRenderContext( materials );
    pRenderContext->SetAmbientLightCube( cubeFaces );

    return 0;
}
LUA_BINDING_END()

LUA_BINDING_BEGIN( Renders, ResetAmbientLightCube, "library", "Reset the ambient light cube.", "client" )
{
    float r = LUA_BINDING_ARGUMENT( luaL_checknumber, 1, "r" );
    float g = LUA_BINDING_ARGUMENT( luaL_checknumber, 2, "g" );
    float b = LUA_BINDING_ARGUMENT( luaL_checknumber, 3, "b" );

    static Vector4D cubeFaces[6];
    int i;

    for ( i = 0; i < 6; i++ )
    {
        cubeFaces[i].Init( r, g, b, 1.0f );
    }

    CMatRenderContextPtr pRenderContext( materials );
    pRenderContext->SetAmbientLightCube( cubeFaces );

    return 0;
}
LUA_BINDING_END()

LUA_BINDING_BEGIN( Renders, RenderFlashlights, "library", "Render flashlights.", "client" )
{
    // Check that a function is provided
    luaL_checktype( L, 1, LUA_TFUNCTION );

    CMatRenderContextPtr pRenderContext( materials );
    pRenderContext->SetFlashlightMode( true );

    // Execute the function.
    // HL2SB: luasrc_pcall() takes an explicit error-function slot; upstream's three
    // argument form is a 5.1 era signature.
    //
    // nargs must be 0: the callback is the only value on the stack (it IS arg 1),
    // and lua_pcall calls the function at top-(nargs+1).  Passing 1 made
    // luasrc_pcall move its injected handler to index -3, which is BELOW this C
    // function's frame -- an out-of-frame write that corrupted the Lua stack.
    luasrc_pcall( L, 0, 0, 0 );

    pRenderContext->SetFlashlightMode( false );

    return 0;
}
LUA_BINDING_END()

LUA_BINDING_BEGIN( Renders, SetLight, "library", "Set a light.", "client" )
{
    LightDesc_t desc;
    memset( &desc, 0, sizeof( desc ) );

    desc.m_Type = MATERIAL_LIGHT_DIRECTIONAL;

    desc.m_Color[0] = LUA_BINDING_ARGUMENT( luaL_checknumber, 1, "r" );
    desc.m_Color[1] = LUA_BINDING_ARGUMENT( luaL_checknumber, 2, "g" );
    desc.m_Color[2] = LUA_BINDING_ARGUMENT( luaL_checknumber, 3, "b" );
    desc.m_Color *= LUA_BINDING_ARGUMENT( luaL_checknumber, 4, "intensity" ) / 255.0f;

    desc.m_Attenuation0 = 1.0f;
    desc.m_Attenuation1 = 0.0f;
    desc.m_Attenuation2 = 0.0f;
    desc.m_Flags = LIGHTTYPE_OPTIMIZATIONFLAGS_HAS_ATTENUATION0;

    desc.m_Direction = LUA_BINDING_ARGUMENT( luaL_checkvector, 5, "direction" );
    VectorNormalize( desc.m_Direction );

    desc.m_Theta = 0.0f;
    desc.m_Phi = 0.0f;
    desc.m_Falloff = 1.0f;

    CMatRenderContextPtr pRenderContext( materials );
    pRenderContext->SetLight( LUA_BINDING_ARGUMENT( luaL_checknumber, 6, "lightIndex" ), desc );

    return 0;
}
LUA_BINDING_END()

LUA_BINDING_BEGIN( Renders, SetColorModulation, "library", "Set the color modulation.", "client" )
{
    float color[3];
    color[0] = LUA_BINDING_ARGUMENT( luaL_checknumber, 1, "r" );
    color[1] = LUA_BINDING_ARGUMENT( luaL_checknumber, 2, "g" );
    color[2] = LUA_BINDING_ARGUMENT( luaL_checknumber, 3, "b" );

    render->SetColorModulation( color );

    return 0;
}
LUA_BINDING_END()

LUA_BINDING_BEGIN( Renders, SetBlend, "library", "Set the blend.", "client" )
{
    render->SetBlend( LUA_BINDING_ARGUMENT( luaL_checknumber, 1, "blend" ) );
    return 0;
}
LUA_BINDING_END()

LUA_BINDING_BEGIN( Renders, ClearBuffers, "library", "Clear the buffers.", "client" )
{
    bool bClearColor = LUA_BINDING_ARGUMENT( lua_toboolean, 1, "clearColor" );
    bool bClearDepth = LUA_BINDING_ARGUMENT( lua_toboolean, 2, "clearDepth" );
    bool bClearStencil = LUA_BINDING_ARGUMENT( lua_toboolean, 3, "clearStencil" );

    CMatRenderContextPtr pRenderContext( materials );
    pRenderContext->ClearBuffers( bClearColor, bClearDepth, bClearStencil );

    return 0;
}
LUA_BINDING_END()

LUA_BINDING_BEGIN( Renders, ClearColor, "library", "Clear the color.", "client" )
{
    lua_Color clr = LUA_BINDING_ARGUMENT( luaL_checkcolor, 1, "color" );

    CMatRenderContextPtr pRenderContext( materials );
    pRenderContext->ClearColor4ub( clr.r(), clr.g(), clr.b(), clr.a() );
    return 0;
}
LUA_BINDING_END()

LUA_BINDING_BEGIN( Renders, SetScissorRectangle, "library", "Set the scissor rectangle.", "client" )
{
    int nLeft = LUA_BINDING_ARGUMENT( luaL_checknumber, 1, "left" );
    int nTop = LUA_BINDING_ARGUMENT( luaL_checknumber, 2, "top" );
    int nRight = LUA_BINDING_ARGUMENT( luaL_checknumber, 3, "right" );
    int nBottom = LUA_BINDING_ARGUMENT( luaL_checknumber, 4, "bottom" );
    bool bEnableScissor = LUA_BINDING_ARGUMENT( lua_toboolean, 5, "enableScissor" );

    CMatRenderContextPtr pRenderContext( materials );
    pRenderContext->SetScissorRect( nLeft, nTop, nRight, nBottom, bEnableScissor );

    return 0;
}
LUA_BINDING_END()

LUA_BINDING_BEGIN( Renders, SetWriteDepthToDestinationAlpha, "library", "Set the write depth to destination alpha.", "client" )
{
    bool bEnable = LUA_BINDING_ARGUMENT( lua_toboolean, 1, "enable" );

    CMatRenderContextPtr pRenderContext( materials );
    pRenderContext->SetIntRenderingParameter( INT_RENDERPARM_WRITE_DEPTH_TO_DESTALPHA, bEnable );
    return 0;
}
LUA_BINDING_END()

LUA_BINDING_BEGIN( Renders, MainViewOrigin, "library", "Get the main view origin.", "client" )
{
    lua_pushvector( L, MainViewOrigin() );
    return 1;
}
LUA_BINDING_END( "Vector", "The main view origin." )

LUA_BINDING_BEGIN( Renders, MainViewAngles, "library", "Get the main view angles.", "client" )
{
    lua_pushangle( L, MainViewAngles() );
    return 1;
}
LUA_BINDING_END( "Angle", "The main view angles." )

LUA_BINDING_BEGIN( Renders, MainViewForward, "library", "Get the main view forward.", "client" )
{
    lua_pushvector( L, MainViewForward() );
    return 1;
}
LUA_BINDING_END( "Vector", "The main view forward." )

static ShaderTexFilterMode_t FilterModeFromEnumeration( int enumeration )
{
    switch ( enumeration )
    {
        case 0:
            return SHADER_TEXFILTERMODE_NEAREST;
        case 1:
            return SHADER_TEXFILTERMODE_NEAREST;
        case 2:
            return SHADER_TEXFILTERMODE_LINEAR;
        case 3:
            return SHADER_TEXFILTERMODE_ANISOTROPIC;
        default:
            return SHADER_TEXFILTERMODE_NEAREST;
            break;
    }
}

/*
** HL2SB: Renders.PushFilterMinification / PopFilterMinification /
** PushFilterMagnification / PopFilterMagnification are not ported.  Upstream marks
** all four "Non-functional; help wanted" / "TODO: This doesn't work", and they reach
** straight into the shader API through `g_pShaderApi`, which HL2SB does not expose
** outside the materialsystem DLL.  The filter stacks they maintained had no other
** user, so the whole group is left out rather than shipped broken.
*/
#if 0

LUA_BINDING_BEGIN( Renders, PushFilterMinification, "library", "Push a minification filter. (Non-functional; help wanted)", "client" )
{
    int filterMode = LUA_BINDING_ARGUMENT( luaL_checknumber, 1, "filter" );

    // TODO: This doesn't work
    CMatRenderContextPtr pRenderContext( materials );
    ITexture *screenTexture = pRenderContext->GetFrameBufferCopyTexture( 0 );
    ShaderAPITextureHandle_t hTexture = g_pShaderApi->CreateTexture(
        screenTexture->GetActualWidth(),
        screenTexture->GetActualHeight(),
        1,
        screenTexture->GetImageFormat(),
        1,
        1,
        TEXTUREFLAGS_CLAMPS | TEXTUREFLAGS_CLAMPT,
        "filterTextureMinification",
        "RenderTarget" );

    filterTextureHandlesMinification.Push( hTexture );
    g_pShaderApi->ModifyTexture( hTexture );
    g_pShaderApi->TexMinFilter( FilterModeFromEnumeration( filterMode ) );

    return 0;
}
LUA_BINDING_END()

LUA_BINDING_BEGIN( Renders, PopFilterMinification, "library", "Pop a minification filter.", "client" )
{
    g_pShaderApi->DeleteTexture( filterTextureHandlesMinification.Top() );
    filterTextureHandlesMinification.Pop();

    return 0;
}
LUA_BINDING_END()

LUA_BINDING_BEGIN( Renders, PushFilterMagnification, "library", "Push a magnification filter.", "client" )
{
    int filterMode = LUA_BINDING_ARGUMENT( luaL_checknumber, 1, "filter" );

    // TODO: This doesn't work
    CMatRenderContextPtr pRenderContext( materials );
    ITexture *screenTexture = pRenderContext->GetFrameBufferCopyTexture( 0 );
    ShaderAPITextureHandle_t hTexture = g_pShaderApi->CreateTexture(
        screenTexture->GetActualWidth(),
        screenTexture->GetActualHeight(),
        1,
        screenTexture->GetImageFormat(),
        1,
        1,
        TEXTUREFLAGS_CLAMPS | TEXTUREFLAGS_CLAMPT,
        "filterTextureMagnification",
        "RenderTarget" );

    filterTextureHandlesMagnification.Push( hTexture );
    g_pShaderApi->ModifyTexture( hTexture );
    g_pShaderApi->TexMagFilter( FilterModeFromEnumeration( filterMode ) );

    return 0;
}
LUA_BINDING_END()

LUA_BINDING_BEGIN( Renders, PopFilterMagnification, "library", "Pop a magnification filter. (Non-functional; help wanted)", "client" )
{
    g_pShaderApi->DeleteTexture( filterTextureHandlesMagnification.Top() );
    filterTextureHandlesMagnification.Pop();

    return 0;
}
LUA_BINDING_END()

#endif  // 0: the shader-API filter stack, see above

LUA_BINDING_BEGIN( Renders, DepthRange, "library", "Set's the depth range of the upcoming render.", "client" )
{
    float depthMin = LUA_BINDING_ARGUMENT( luaL_checknumber, 1, "depthMin" );
    float depthMax = LUA_BINDING_ARGUMENT( luaL_checknumber, 2, "depthMax" );

    CMatRenderContextPtr pRenderContext( materials );
    pRenderContext->DepthRange( depthMin, depthMax );

    return 0;
}
LUA_BINDING_END()

LUA_BINDING_BEGIN( Renders, GetViewEntity, "library", "Returns the entity the client is using to see (mostly the player itself)", "client" )
{
    int viewEntityId = render->GetViewEntity();
    C_BaseEntity *pCameraObject = cl_entitylist->GetEnt( viewEntityId );
    CBaseEntity::PushLuaInstanceSafe( L, pCameraObject );

    return 1;
}
LUA_BINDING_END( "Entity", "The view entity." )

LUA_BINDING_BEGIN( Renders, SetMaterial, "library", "Binds a material for use in the next render operations", "client" )
{
    IMaterial *pMaterial = NULL;

    // HL2SB: GMod spells this render.SetMaterial( Material( path ) ), and this
    // engine's Material() is a Lua proxy table
    // (lua/includes/extensions/gmod_surface.lua) that only wraps a texture id --
    // it has Width/Height/GetTextureID but it is NOT an IMaterial, and render.*
    // is a 3D API that needs one.
    //
    // Every scripted effect (lua/effects/rb655_nyan_bounce.lua: Material(
    // "nyan/cat" ) then render.SetMaterial( Cat )) and ENT:Draw go through here,
    // and without this the call raised "bad argument #1 to 'SetMaterial'
    // (IMaterial expected, got table)" and nothing drew at all.
    if ( luaL_testudata( L, 1, LUA_MATERIALLIBNAME ) != NULL )
    {
        pMaterial = luaL_checkmaterial( L, 1 );
    }
    else
    {
        const char *pszName = NULL;

        if ( lua_type( L, 1 ) == LUA_TSTRING )
        {
            pszName = lua_tostring( L, 1 );
        }
        else if ( lua_istable( L, 1 ) )
        {
            // The proxy keeps its path in __path (mat:GetName() returns it).
            lua_getfield( L, 1, "__path" );
            if ( lua_type( L, -1 ) == LUA_TSTRING )
                pszName = lua_tostring( L, -1 );
            lua_pop( L, 1 );
        }
        else if ( lua_isnumber( L, 1 ) )
        {
            // Older HL2SB scripts pass a bare surface texture number.
            pszName = lua_tostring( L, 1 );
        }

        if ( pszName == NULL || pszName[0] == '\0' )
            luaL_argerror( L, 1, "material, material path or texture name expected" );

        // Cache hit: reuse the previously resolved material.  Avoids
        // re-running FindMaterialEx (and its PNG-synthesise path) every frame
        // from ENT:Draw / effect Render, which is the documented
        // "create material while drawing" crash.
        unsigned short idx = s_MatCache.Find( pszName );
        if ( s_MatCache.IsValidIndex( idx ) && s_MatCache[idx].pMat != NULL )
        {
            pMaterial = s_MatCache[idx].pMat;
        }
        else
        {
            char szResolved[ 512 ];
            Q_strncpy( szResolved, pszName, sizeof( szResolved ) );

            pMaterial = materials->FindMaterial( szResolved, TEXTURE_GROUP_OTHER, false );

            // GMod scripts routinely name the image where the shader next to it is
            // what actually ships: Material( "nyan/cat.png" ) against
            // materials/nyan/cat.vmt.  Retry without the image extension before
            // giving up on the error material.
            if ( pMaterial == NULL || pMaterial->IsErrorMaterial() )
            {
                int nLastDot = -1;
                int nLastSlash = -1;
                for ( int i = 0; szResolved[ i ] != '\0'; ++i )
                {
                    if ( szResolved[ i ] == '.' )
                        nLastDot = i;
                    else if ( szResolved[ i ] == '/' )
                        nLastSlash = i;
                }

                if ( nLastDot > nLastSlash )
                {
                    szResolved[ nLastDot ] = '\0';
                    IMaterial *pRetry = materials->FindMaterial( szResolved, TEXTURE_GROUP_OTHER, false );
                    if ( pRetry != NULL && !pRetry->IsErrorMaterial() )
                        pMaterial = pRetry;
                }
            }

            if ( pMaterial == NULL )
            {
                pMaterial = materials->FindMaterial( "debug/debugempty", TEXTURE_GROUP_OTHER, false );
            }

            if ( s_MatCache.Count() < 256 )
            {
                unsigned short newIdx = s_MatCache.Insert( pszName );
                if ( s_MatCache.IsValidIndex( newIdx ) )
                {
                    s_MatCache[newIdx].pMat = pMaterial;
                    // Pin so the material system cannot evict it while the
                    // cache holds the pointer (dangling-pointer crash).
                    if ( pMaterial && !pMaterial->IsErrorMaterial() )
                        pMaterial->IncrementReferenceCount();
                }
            }
        }
    }

    CMatRenderContextPtr pRenderContext( materials );
    pRenderContext->Bind( pMaterial );

    // HL2SB: DrawBeam's CBeamSegDraw::Start(pCtx, n, NULL) is supposed to pick
    // up the bound material, but the Nyan Gun's rainbow tracer came out
    // invisible after the colour-range fix -- the beam drew with whatever
    // material happened to be bound instead of the one SetMaterial just set.
    // Remember it so DrawBeam can pass it explicitly.
    g_pHL2SBLastBoundMaterial = pMaterial;

    return 0;
}
LUA_BINDING_END()

LUA_BINDING_BEGIN( Renders, DrawQuadEasy, "library", "Draws a quad with the currently bound material, facing the given normal.", "client" )
{
    Vector position = LUA_BINDING_ARGUMENT( luaL_checkvector, 1, "position" );
    Vector normal = LUA_BINDING_ARGUMENT( luaL_checkvector, 2, "normal" );
    float width = LUA_BINDING_ARGUMENT( luaL_checknumber, 3, "width" );
    float height = LUA_BINDING_ARGUMENT( luaL_checknumber, 4, "height" );
    lua_Color color = LUA_BINDING_ARGUMENT_WITH_DEFAULT( luaL_optcolor, 5, lua_Color( 255, 255, 255, 255 ), "color" );
    float rotation = LUA_BINDING_ARGUMENT_WITH_DEFAULT( luaL_optnumber, 6, 0.0f, "rotation" );

    VectorNormalize( normal );

    // A basis perpendicular to the quad's normal.
    Vector reference( 0.0f, 0.0f, 1.0f );
    if ( fabs( normal.z ) > 0.99f )
    {
        reference.Init( 0.0f, 1.0f, 0.0f );
    }

    Vector right = normal.Cross( reference );
    VectorNormalize( right );
    Vector up = right.Cross( normal );
    VectorNormalize( up );

    if ( rotation != 0.0f )
    {
        float flSin, flCos;
        SinCos( DEG2RAD( rotation ), &flSin, &flCos );

        Vector rotatedRight = right * flCos + up * flSin;
        Vector rotatedUp = up * flCos - right * flSin;

        right = rotatedRight;
        up = rotatedUp;
    }

    const Vector rightHalf = right * ( width * 0.5f );
    const Vector upHalf = up * ( height * 0.5f );

    const float flRed = color.r() / 255.0f;
    const float flGreen = color.g() / 255.0f;
    const float flBlue = color.b() / 255.0f;
    const float flAlpha = color.a() / 255.0f;

    CMatRenderContextPtr pRenderContext( materials );
    IMesh *pMesh = pRenderContext->GetDynamicMesh();

    CMeshBuilder meshBuilder;
    meshBuilder.Begin( pMesh, MATERIAL_QUADS, 1 );

    // ( 0, 0 ) .. ( 1, 1 ) in texture space, counter-clockwise.
    const float flU[ 4 ] = { 0.0f, 1.0f, 1.0f, 0.0f };
    const float flV[ 4 ] = { 0.0f, 0.0f, 1.0f, 1.0f };
    const float flSign[ 4 ][ 2 ] = { { -1.0f, -1.0f }, { 1.0f, -1.0f }, { 1.0f, 1.0f }, { -1.0f, 1.0f } };

    for ( int i = 0; i < 4; ++i )
    {
        Vector corner = position + rightHalf * flSign[ i ][ 0 ] + upHalf * flSign[ i ][ 1 ];

        meshBuilder.Position3fv( corner.Base() );
        meshBuilder.Color4f( flRed, flGreen, flBlue, flAlpha );
        meshBuilder.TexCoord2f( 0, flU[ i ], flV[ i ] );
        meshBuilder.AdvanceVertex();
    }

    meshBuilder.End();
    pMesh->Draw();

    return 0;
}
LUA_BINDING_END()

LUA_BINDING_BEGIN( Renders, DrawSprite, "library", "Draws a sprite", "client" )
{
    Vector position = LUA_BINDING_ARGUMENT( luaL_checkvector, 1, "position" );
    float width = LUA_BINDING_ARGUMENT( luaL_checknumber, 2, "width" );
    float height = LUA_BINDING_ARGUMENT( luaL_checknumber, 3, "height" );
    lua_Color color = LUA_BINDING_ARGUMENT_WITH_DEFAULT( luaL_optcolor, 4, lua_Color( 255, 255, 255, 255 ), "color" );

    color32 rawColor = { color.r(), color.g(), color.b(), color.a() };
    DrawSprite( position, width, height, rawColor );

    return 0;
}
LUA_BINDING_END()

LUA_BINDING_BEGIN( Renders, DrawBeam, "library", "Draws a beam", "client" )
{
    Vector &start = LUA_BINDING_ARGUMENT( luaL_checkvector, 1, "start" );
    Vector &end = LUA_BINDING_ARGUMENT( luaL_checkvector, 2, "end" );
    float width = LUA_BINDING_ARGUMENT( luaL_checknumber, 3, "width" );
    float textureStart = LUA_BINDING_ARGUMENT( luaL_checknumber, 4, "textureStart" );
    float textureEnd = LUA_BINDING_ARGUMENT( luaL_checknumber, 5, "textureEnd" );
    lua_Color color = LUA_BINDING_ARGUMENT_WITH_DEFAULT( luaL_optcolor, 6, lua_Color( 255, 255, 255, 255 ), "color" );

    CMatRenderContextPtr pRenderContext( materials );
    CBeamSegDraw beamDraw;
    // Pass the material SetMaterial just bound explicitly -- NULL means
    // "whatever is currently bound", which was not reliably the Lua material.
    beamDraw.Start( pRenderContext, 2, g_pHL2SBLastBoundMaterial );

    // CBeamSegDraw feeds m_vColor / m_flAlpha straight into Color4f, which
    // expects 0-1.  GMod's Color() is 0-255, so passing the raw bytes made
    // every beam draw with overflowed vertex colour (the Nyan Gun's rainbow
    // tracer came out yellow-green) and a hardcoded alpha of 1.0 killed the
    // script's fade-out.  Same convention as DrawQuadEasy above.
    const float flRed   = color.r() / 255.0f;
    const float flGreen = color.g() / 255.0f;
    const float flBlue  = color.b() / 255.0f;
    const float flAlpha = color.a() / 255.0f;

    BeamSeg_t seg;
    seg.m_flAlpha = flAlpha;
    seg.m_flWidth = width;
    seg.m_vColor = Vector( flRed, flGreen, flBlue );

    seg.m_vPos = start;
    seg.m_flTexCoord = textureStart;
    beamDraw.NextSeg( &seg );

    seg.m_vPos = end;
    seg.m_flTexCoord = textureEnd;
    beamDraw.NextSeg( &seg );

    beamDraw.End();

    return 0;
}
LUA_BINDING_END()

#define STENCIL_COMPARISON_FUNCTION StencilComparisonFunction_t
#define STENCIL_OPERATION StencilOperation_t

LUA_BINDING_BEGIN( Renders, SetStencilCompareFunction, "library", "Set the stencil compare function.", "client" )
{
    CMatRenderContextPtr pRenderContext( materials );
    pRenderContext->SetStencilCompareFunction( LUA_BINDING_ARGUMENT_ENUM( STENCIL_COMPARISON_FUNCTION, 1, "compareFunction" ) );
    return 0;
}
LUA_BINDING_END()

LUA_BINDING_BEGIN( Renders, SetStencilEnable, "library", "Set the stencil enable.", "client" )
{
    CMatRenderContextPtr pRenderContext( materials );
    pRenderContext->SetStencilEnable( LUA_BINDING_ARGUMENT( lua_toboolean, 1, "enable" ) );
    return 0;
}
LUA_BINDING_END()

LUA_BINDING_BEGIN( Renders, SetStencilFailOperation, "library", "Set the stencil fail operation.", "client" )
{
    CMatRenderContextPtr pRenderContext( materials );
    pRenderContext->SetStencilFailOperation( LUA_BINDING_ARGUMENT_ENUM( STENCIL_OPERATION, 1, "failOperation" ) );
    return 0;
}
LUA_BINDING_END()

LUA_BINDING_BEGIN( Renders, SetStencilPassOperation, "library", "Set the stencil pass operation.", "client" )
{
    CMatRenderContextPtr pRenderContext( materials );
    pRenderContext->SetStencilPassOperation( LUA_BINDING_ARGUMENT_ENUM( STENCIL_OPERATION, 1, "passOperation" ) );
    return 0;
}
LUA_BINDING_END()

LUA_BINDING_BEGIN( Renders, SetStencilReferenceValue, "library", "Set the stencil reference value.", "client" )
{
    CMatRenderContextPtr pRenderContext( materials );
    pRenderContext->SetStencilReferenceValue( LUA_BINDING_ARGUMENT( luaL_checknumber, 1, "referenceValue" ) );
    return 0;
}
LUA_BINDING_END()

LUA_BINDING_BEGIN( Renders, SetStencilTestMask, "library", "Set the stencil test mask.", "client" )
{
    CMatRenderContextPtr pRenderContext( materials );
    pRenderContext->SetStencilTestMask( LUA_BINDING_ARGUMENT( luaL_checknumber, 1, "testMask" ) );
    return 0;
}
LUA_BINDING_END()

LUA_BINDING_BEGIN( Renders, SetStencilWriteMask, "library", "Set the stencil write mask.", "client" )
{
    CMatRenderContextPtr pRenderContext( materials );
    pRenderContext->SetStencilWriteMask( LUA_BINDING_ARGUMENT( luaL_checknumber, 1, "writeMask" ) );
    return 0;
}
LUA_BINDING_END()

LUA_BINDING_BEGIN( Renders, SetStencilZFailOperation, "library", "Set the stencil z fail operation.", "client" )
{
    CMatRenderContextPtr pRenderContext( materials );
    pRenderContext->SetStencilZFailOperation( LUA_BINDING_ARGUMENT_ENUM( STENCIL_OPERATION, 1, "zFailOperation" ) );
    return 0;
}
LUA_BINDING_END()

LUA_BINDING_BEGIN( Renders, ClearStencilBufferRectangle, "library", "Clear the stencil buffer rectangle.", "client" )
{
    int startX = LUA_BINDING_ARGUMENT( luaL_checknumber, 1, "startX" );
    int startY = LUA_BINDING_ARGUMENT( luaL_checknumber, 2, "startY" );
    int endX = LUA_BINDING_ARGUMENT( luaL_checknumber, 3, "endX" );
    int endY = LUA_BINDING_ARGUMENT( luaL_checknumber, 4, "endY" );
    int value = LUA_BINDING_ARGUMENT( luaL_checknumber, 5, "value" );

    CMatRenderContextPtr pRenderContext( materials );
    pRenderContext->ClearStencilBufferRectangle( startX, startY, endX, endY, value );

    return 0;
}
LUA_BINDING_END()

#endif  // CLIENT_DLL

/*
** Open render library
*/
LUALIB_API int luaopen_render( lua_State *L )
{
    LUA_REGISTRATION_COMMIT_LIBRARY( Renders );

#ifdef CLIENT_DLL
    LUA_SET_ENUM_LIB_BEGIN( L, "CULL_MODE" );
    lua_pushenum( L, MaterialCullMode_t::MATERIAL_CULLMODE_CCW, "COUNTER_CLOCKWISE" );
    lua_pushenum( L, MaterialCullMode_t::MATERIAL_CULLMODE_CW, "CLOCKWISE" );
    LUA_SET_ENUM_LIB_END( L );

    LUA_SET_ENUM_LIB_BEGIN( L, "STENCIL_COMPARISON_FUNCTION" );
    lua_pushenum( L, StencilComparisonFunction_t::STENCILCOMPARISONFUNCTION_NEVER, "NEVER" );
    lua_pushenum( L, StencilComparisonFunction_t::STENCILCOMPARISONFUNCTION_LESS, "LESS" );
    lua_pushenum( L, StencilComparisonFunction_t::STENCILCOMPARISONFUNCTION_EQUAL, "EQUAL" );
    lua_pushenum( L, StencilComparisonFunction_t::STENCILCOMPARISONFUNCTION_LESSEQUAL, "LESS_OR_EQUAL" );
    lua_pushenum( L, StencilComparisonFunction_t::STENCILCOMPARISONFUNCTION_GREATER, "GREATER" );
    lua_pushenum( L, StencilComparisonFunction_t::STENCILCOMPARISONFUNCTION_NOTEQUAL, "NOT_EQUAL" );
    lua_pushenum( L, StencilComparisonFunction_t::STENCILCOMPARISONFUNCTION_GREATEREQUAL, "GREATER_OR_EQUAL" );
    lua_pushenum( L, StencilComparisonFunction_t::STENCILCOMPARISONFUNCTION_ALWAYS, "ALWAYS" );
    LUA_SET_ENUM_LIB_END( L );

    LUA_SET_ENUM_LIB_BEGIN( L, "STENCIL_OPERATION" );
    lua_pushenum( L, StencilOperation_t::STENCILOPERATION_KEEP, "KEEP" );
    lua_pushenum( L, StencilOperation_t::STENCILOPERATION_ZERO, "ZERO" );
    lua_pushenum( L, StencilOperation_t::STENCILOPERATION_REPLACE, "REPLACE" );
    lua_pushenum( L, StencilOperation_t::STENCILOPERATION_INCRSAT, "INCREMENT_CLAMP" );
    lua_pushenum( L, StencilOperation_t::STENCILOPERATION_DECRSAT, "DECREMENT_CLAMP" );
    lua_pushenum( L, StencilOperation_t::STENCILOPERATION_INVERT, "INVERT" );
    lua_pushenum( L, StencilOperation_t::STENCILOPERATION_INCR, "INCREMENT_WRAP" );
    lua_pushenum( L, StencilOperation_t::STENCILOPERATION_DECR, "DECREMENT_WRAP" );
    LUA_SET_ENUM_LIB_END( L );
#endif

    return 1;
}
