#include "cbase.h"
#include "materialsystem/itexture.h"
#include "luamanager.h"
#include "luasrclib.h"
#include "lrender.h"

// HL2SB GMod compat (ConVars In Garrysmod): the physgun visual toggles.
// physgun_drawbeams is read by hl2sb/weapon_physgun.cpp's draw paths,
// physgun_halo by the Lua halo capture (hl2sb_physgun_halo.lua).
// physgun_halo_shell gates the legacy C++ whole-model glow shell -- OFF by
// default since 2026-09-23: GMod has no such shell, and stacked with the
// halo library it reads as "the whole prop wrapped in one flat color".
ConVar physgun_halo( "physgun_halo", "1", FCVAR_ARCHIVE, "Draw the physgun halo on the held entity" );
ConVar physgun_halo_shell( "physgun_halo_shell", "0", FCVAR_ARCHIVE, "Draw the legacy whole-model physgun glow shell" );
ConVar physgun_drawbeams( "physgun_drawbeams", "1", FCVAR_ARCHIVE, "Draw the physgun beams" );
#ifdef CLIENT_DLL
#include "rendertexture.h"
#include "view_scene.h"
#include <materialsystem/imaterialsystem.h>
#include <materialsystem/imaterialvar.h>
#include <materialsystem/imesh.h>
#include <vgui/ISurface.h>
#include <VGuiMatSurface/IMatSystemSurface.h>	// HL2SB: cam.Start3D2D surface matrix stack (g_pMatSystemSurface)
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
// HL2SB: g_pStudioRender, for render.SetLocalModelLights (declared in istudiorender.h:383).
#include "istudiorender.h"
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

    // HL2SB (2026-09-22): NULL (the backbuffer) is Lua nil -- pushing a
    // NULL-wrapped ITexture made the caller's SetRenderTarget( rt_Scene )
    // throw "ITexture expected, got NULL" inside halo's failure fuse, which
    // is what kept the frame black.
    if ( pTexture == NULL )
        lua_pushnil( L );
    else
        lua_pushitexture( L, pTexture );

    return 1;
}
LUA_BINDING_END( "Texture", "The currently active render target texture." )

// render.CreateNamedRenderTarget( name, wide, tall ) -- HL2SB (2026-09-27):
// spawnicon snapshot support.  Creates (or finds -- CreateNamedRenderTargetTextureEx2
// is name-keyed, so repeat calls return the same texture) a 32bpp RGBA render
// target with its own depth surface, so a model can be rendered into it once
// and blitted as a texture forever after, instead of every thumbnail rendering
// its model live every frame.
LUA_BINDING_BEGIN( Renders, CreateNamedRenderTarget, "library", "Creates or finds a named render target texture.", "client" )
{
    const char *pszName = LUA_BINDING_ARGUMENT( luaL_checkstring, 1, "name" );
    int nWide = LUA_BINDING_ARGUMENT( luaL_checkint, 2, "wide" );
    int nTall = LUA_BINDING_ARGUMENT( luaL_checkint, 3, "tall" );

    ITexture *pTexture = materials->CreateNamedRenderTargetTextureEx2(
        pszName, nWide, nTall, RT_SIZE_LITERAL, IMAGE_FORMAT_RGBA8888,
        MATERIAL_RT_DEPTH_SEPARATE,
        TEXTUREFLAGS_CLAMPS | TEXTUREFLAGS_CLAMPT | TEXTUREFLAGS_NOMIP | TEXTUREFLAGS_NOLOD,
        0 );

    if ( pTexture == NULL )
        lua_pushnil( L );
    else
        lua_pushitexture( L, pTexture );

    return 1;
}
LUA_BINDING_END( "Texture", "The render target texture." )

LUA_BINDING_BEGIN( Renders, CopyRenderTargetToTexture, "library", "Copies the currently active Render Target to the specified texture.", "client" )
{
    CMatRenderContextPtr pRenderContext( materials );
    ITexture *pTexture = LUA_BINDING_ARGUMENT( luaL_checkitexture, 1, "texture" );

    // HL2SB (2026-09-26): GMod parity, implemented as the ENGINE'S OWN
    // UpdateScreenEffectTexture inline (view_scene.h) minus the FB lookup --
    // that is the copy path the working halo capture always used.  The src
    // rect comes from GetRenderTargetDimensions (the LIVE render-target
    // size), NOT GetViewport (stale/wrong at PostDrawEffects -- a small src
    // rect StretchRects a corner of garbage into the copy, which blacked the
    // scene-restore step), and the dest rect is scaled like the inline when
    // the texture differs in size.
    int nSrcW, nSrcH;
    pRenderContext->GetRenderTargetDimensions( nSrcW, nSrcH );
    int nDstW = pTexture->GetActualWidth();
    int nDstH = pTexture->GetActualHeight();

    Rect_t srcRect;
    srcRect.x = 0;
    srcRect.y = 0;
    srcRect.width = nSrcW;
    srcRect.height = nSrcH;

    Rect_t destRect = srcRect;
    if ( nSrcW > nDstW || nSrcH > nDstH )
    {
        float scaleX = ( float )nDstW / ( float )nSrcW;
        float scaleY = ( float )nDstH / ( float )nSrcH;
        destRect.x = ( int )( srcRect.x * scaleX );
        destRect.y = ( int )( srcRect.y * scaleY );
        destRect.width = ( int )( srcRect.width * scaleX );
        destRect.height = ( int )( srcRect.height * scaleY );
        destRect.x = clamp( destRect.x, 0, nDstW );
        destRect.y = clamp( destRect.y, 0, nDstH );
        destRect.width = clamp( destRect.width, 0, nDstW - destRect.x );
        destRect.height = clamp( destRect.height, 0, nDstH - destRect.y );
    }

    pRenderContext->CopyRenderTargetToTextureEx( pTexture, 0, &srcRect, &destRect );

    // HL2SB (2026-09-26): GMod-parity registration -- same slots the engine
    // inline registers (the frame-buffer pair handed out by
    // render.GetScreenEffectTexture).
    if ( pTexture == GetFullFrameFrameBufferTexture( 0 ) )
    {
        pRenderContext->SetFrameBufferCopyTexture( pTexture, 0 );
    }
    else if ( pTexture == GetFullFrameFrameBufferTexture( 1 ) )
    {
        pRenderContext->SetFrameBufferCopyTexture( pTexture, 1 );
    }

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
    // HL2SB (2026-09-24, second pass): hand out the ENGINE'S frame-buffer
    // pair (_rt_FullFrameFB / _rt_FullFrameFB1, MAX_FB_TEXTURES = 4) --
    // exactly what GMod's render.GetScreenEffectTexture returns.  The
    // dedicated _rt_hl2sb_screenfx0/1 targets built on 09-22 sampled BLACK
    // through halo's restore/composite quads: this fork's
    // CopyRenderTargetToTextureEx resolves the framebuffer into a texture's
    // DDR clone (cmatrendercontext.cpp "CopyFrameBufferToMe"), and only a
    // texture REGISTERED via SetFrameBufferCopyTexture -- which is what the
    // view_scene.h UpdateScreenEffectTexture inline does for the engine FB
    // pair, and what nothing ever did for the dedicated RTs -- hands that
    // clone to the sampler.  The 09-22 "self-copy garbage" worry is moot in
    // this arrangement: halo captures with UpdateScreenEffectTexture BEFORE
    // drawing any silhouette, so the FB holds the pre-halo scene and is only
    // sampled after the pass.
    int nIndex = (int)LUA_BINDING_ARGUMENT_WITH_DEFAULT( luaL_optnumber, 1, 0, "textureIndex" );
    nIndex = clamp( nIndex, 0, 1 );
    lua_pushitexture( L, GetFullFrameFrameBufferTexture( nIndex ) );
    return 1;
}
LUA_BINDING_END( "Texture", "The screen effect texture." )

// HL2SB (2026-10-02) GMod compat: render.GetMoBlurTex0() -- the motion-blur
// accumulation target lua/postprocess/motion_blur.lua draws with.  GMod's
// client lazily creates a named 256x256 render target ("s_pMoBlurTex0") on
// first use; do the same through the same creation call the spawnicon
// snapshot helper uses, so repeated calls return the same texture.
// HL2SB (2026-10-02, second pass): the creation must sit inside a render
// target allocation window -- CMaterialSystem refuses named RT creation
// outside one (Warning + NULL every call), which meant a per-frame failed
// attempt + warning once DrawMotionBlur ran from the HUD (213 warnings in
// one session).  Wrap the one-shot creation in the public allocation APIs.
LUA_BINDING_BEGIN( Renders, GetMoBlurTex0, "library", "Get the motion blur render target texture.", "client" )
{
    static ITexture *s_pMoBlurTex0 = NULL;
    if ( s_pMoBlurTex0 == NULL )
    {
        materials->BeginRenderTargetAllocation();
        s_pMoBlurTex0 = materials->CreateNamedRenderTargetTextureEx2(
            "s_pMoBlurTex0", 256, 256, RT_SIZE_LITERAL, IMAGE_FORMAT_RGBA8888,
            MATERIAL_RT_DEPTH_SEPARATE,
            TEXTUREFLAGS_CLAMPS | TEXTUREFLAGS_CLAMPT | TEXTUREFLAGS_NOMIP | TEXTUREFLAGS_NOLOD,
            0 );
        materials->EndRenderTargetAllocation();
        if ( s_pMoBlurTex0 == NULL )
        {
            lua_pushnil( L );
            return 1;
        }
    }
    lua_pushitexture( L, s_pMoBlurTex0 );
    return 1;
}
LUA_BINDING_END( "Texture", "The motion blur texture." )

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

// HL2SB GMod compat: render.UpdateRefractTexture() -- wiki: "Pretty much alias
// of render.UpdatePowerOfTwoTexture but does not return the texture."  The
// Nuke Pack's blastwave effect calls it before drawing refract sprites.
LUA_BINDING_BEGIN( Renders, UpdateRefractTexture, "library", "Updates the refract texture.", "client" )
{
    UpdateRefractTexture();
    return 0;
}
LUA_BINDING_END()

// HL2SB (2026-09-26): GMod's Material():SetTexture/SetString/SetFloat.  The
// GMod post-process stack (halo's pp/copy, pp/add; bloom's pp/blurx) retargets
// material variables at draw time; this fork's Lua Material() is a plain table
// (gmod_surface.lua), so the writes land through this library call.  GMod's
// halo pipeline is dead without it -- mat_Copy:SetTexture("$basetexture", rt)
// silently did nothing and the restore/composite quads sampled stale textures.
LUA_BINDING_BEGIN( Renders, MaterialSetVar, "library", "Sets a material variable (texture, number or string).", "client" )
{
    const char *pszPath = LUA_BINDING_ARGUMENT( luaL_checkstring, 1, "materialPath" );
    const char *pszVar = LUA_BINDING_ARGUMENT( luaL_checkstring, 2, "varName" );

    IMaterial *pMaterial = materials->FindMaterial( pszPath, TEXTURE_GROUP_CLIENT_EFFECTS );
    if ( pMaterial == NULL || pMaterial->IsErrorMaterial() )
        return 0;

    bool bFound = false;
    IMaterialVar *pVar = pMaterial->FindVar( pszVar, &bFound );
    if ( !bFound || pVar == NULL )
        return 0;

    int iType = lua_type( L, 3 );
    if ( iType == LUA_TNUMBER )
    {
        pVar->SetFloatValue( ( float )lua_tonumber( L, 3 ) );
    }
    else if ( iType == LUA_TSTRING )
    {
        pVar->SetStringValue( lua_tostring( L, 3 ) );
    }
    else if ( iType == LUA_TUSERDATA )
    {
        ITexture *pTexture = luaL_checkitexture( L, 3 );
        if ( pTexture != NULL )
            pVar->SetTextureValue( pTexture );
    }

    return 0;
}
LUA_BINDING_END()

// HL2SB GMod compat: render.GetDXLevel() -- wiki: returns the maximum available
// DirectX version.  This fork is DX9-only (shaderapidx9), so the honest answer
// is a constant 90.
LUA_BINDING_BEGIN( Renders, GetDXLevel, "library", "Returns the maximum available DirectX version.", "client" )
{
    lua_pushinteger( L, 90 );
    return 1;
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

    /*
    ** HL2SB: the view setup above is copied from the PLAYER's, and that one carries the
    ** SCREEN's aspect ratio in m_flAspectRatio (1920/1280 = 1.5 in game).
    **
    ** CRender::Push3DView prefers that field over the rect when it builds the projection
    ** (engine/gl_rmain.cpp:552-556) and MatrixBuildPerspectiveX makes the vertical FOV
    ** `fovX` scaled by 1/aspect (public/mathlib/vmatrix.h:2027-2028).  A vgui subrect
    ** therefore inherited the screen's proportions and both symptoms of the player model
    ** preview follow (2026-09-17 screenshot): the model came out squashed sideways and
    ** cropped top-to-bottom - a 72-unit model in a 765x984 pane asking for FOV ~37 (which
    ** the engine turned into a ~25 degree vertical lens instead of ~46).
    **
    ** GMod's cam.Start3D() lets the rect decide the aspect; 0 is exactly that, because
    ** ComputeViewMatrices then derives it from width/height (gl_rmain.cpp:553-555).
    */
    viewSetup.m_flAspectRatio = 0.0f;

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

// HL2SB: the material render.SetMaterial last bound -- DrawScreenQuad draws it
// (this render context has no GetBoundMaterial).  Defined here so the
// DrawScreenQuad binding below sees it; SetMaterial (further down) writes it.
static IMaterial *s_pRendersLastSetMaterial = NULL;

//-----------------------------------------------------------------------------
// HL2SB (2026-09-22): the primitives GMod's halo.lua and the client render.lua
// shim need.  The shim's load gate is render.GetAmbientLightColor -- without
// it the whole file returned early and cam.Start2D/3D, the STENCIL_* aliases
// and render.BlurRenderTarget never existed, which is why the (already ported)
// modules/halo.lua was dead on arrival.
//-----------------------------------------------------------------------------

// render.GetAmbientLightColor() -> Color
LUA_BINDING_BEGIN( Renders, GetAmbientLightColor, "library", "Returns the ambient light color at the origin of the map.", "client" )
{
    Vector vecLight = engine->GetLightForPoint( Vector( 0, 0, 0 ), false );
    lua_Color clr( (unsigned char)clamp( (int)( vecLight.x * 255.0f ), 0, 255 ),
                   (unsigned char)clamp( (int)( vecLight.y * 255.0f ), 0, 255 ),
                   (unsigned char)clamp( (int)( vecLight.z * 255.0f ), 0, 255 ),
                   255 );
    lua_pushcolor( L, clr );
    return 1;
}
LUA_BINDING_END()

// render.Clear( r, g, b, a, clearDepth = false, clearStencil = false )
LUA_BINDING_BEGIN( Renders, Clear, "library", "Clears the current render target with the given color.", "client" )
{
    color32 clr;
    clr.r = (unsigned char)clamp( (int)luaL_checknumber( L, 1 ), 0, 255 );
    clr.g = (unsigned char)clamp( (int)luaL_checknumber( L, 2 ), 0, 255 );
    clr.b = (unsigned char)clamp( (int)luaL_checknumber( L, 3 ), 0, 255 );
    clr.a = (unsigned char)clamp( (int)luaL_checknumber( L, 4 ), 0, 255 );
    bool bClearDepth = luaL_optboolean( L, 5, 0 ) ? true : false;
    bool bClearStencil = luaL_optboolean( L, 6, 0 ) ? true : false;

    CMatRenderContextPtr pRenderContext( materials );
    pRenderContext->ClearColor4ub( clr.r, clr.g, clr.b, clr.a );
    pRenderContext->ClearBuffers( true, bClearDepth, bClearStencil );

    return 0;
}
LUA_BINDING_END()

// render.SetRenderTarget( texture ) -- nil restores the frame buffer.
LUA_BINDING_BEGIN( Renders, SetRenderTarget, "library", "Sets the render target to draw into.", "client" )
{
    CMatRenderContextPtr pRenderContext( materials );
    if ( lua_isnoneornil( L, 1 ) )
    {
        pRenderContext->SetRenderTarget( NULL );
    }
    else
    {
        pRenderContext->SetRenderTarget( LUA_BINDING_ARGUMENT( luaL_checkitexture, 1, "texture" ) );
    }
    return 0;
}
LUA_BINDING_END()

// render.PushRenderTarget( texture [, x, y, w, h] ) / render.PopRenderTarget()
LUA_BINDING_BEGIN( Renders, PushRenderTarget, "library", "Pushes a render target and viewport for rendering.", "client" )
{
    CMatRenderContextPtr pRenderContext( materials );
    ITexture *pTexture = LUA_BINDING_ARGUMENT( luaL_checkitexture, 1, "texture" );

    if ( lua_isnumber( L, 2 ) && lua_isnumber( L, 3 ) && lua_isnumber( L, 4 ) && lua_isnumber( L, 5 ) )
    {
        int nX = (int)lua_tonumber( L, 2 );
        int nY = (int)lua_tonumber( L, 3 );
        int nW = (int)lua_tonumber( L, 4 );
        int nH = (int)lua_tonumber( L, 5 );
        pRenderContext->PushRenderTargetAndViewport( pTexture, nX, nY, nW, nH );
    }
    else
    {
        pRenderContext->PushRenderTargetAndViewport( pTexture );
    }
    return 0;
}
LUA_BINDING_END()

LUA_BINDING_BEGIN( Renders, PopRenderTarget, "library", "Pops a render target pushed with PushRenderTarget.", "client" )
{
    CMatRenderContextPtr pRenderContext( materials );
    pRenderContext->PopRenderTargetAndViewport();
    return 0;
}
LUA_BINDING_END()

// render.DrawScreenQuad() -- one quad over the current viewport with the
// material set by render.SetMaterial.
LUA_BINDING_BEGIN( Renders, DrawScreenQuad, "library", "Draws a fullscreen quad with the currently bound material.", "client" )
{
    CMatRenderContextPtr pRenderContext( materials );
    IMaterial *pMaterial = s_pRendersLastSetMaterial;
    if ( pMaterial == NULL || pMaterial->IsErrorMaterial() )
        return luaL_error( L, "render.DrawScreenQuad: no material bound (call render.SetMaterial first)" );
    pRenderContext->DrawScreenSpaceQuad( pMaterial );
    return 0;
}
LUA_BINDING_END()

// render.DrawScreenQuadWithTexture( texture, materialName ) -- finds the REAL
// IMaterial (bypassing the Lua Material() stub wrapper, which cannot touch
// material vars and broke halo's scene-restore/composite steps), drives its
// $basetexture var, and draws one fullscreen quad with it.
LUA_BINDING_BEGIN( Renders, DrawScreenQuadWithTexture, "library", "Draws a fullscreen quad of the texture through the named material.", "client" )
{
    ITexture *pTexture = LUA_BINDING_ARGUMENT( luaL_checkitexture, 1, "texture" );
    const char *pszMaterial = luaL_checkstring( L, 2 );

    IMaterial *pMaterial = materials->FindMaterial( pszMaterial, TEXTURE_GROUP_CLIENT_EFFECTS );
    if ( pMaterial == NULL || pMaterial->IsErrorMaterial() )
        return luaL_error( L, "render.DrawScreenQuadWithTexture: material '%s' not found", pszMaterial );

    bool bFound = false;
    IMaterialVar *pBaseTexture = pMaterial->FindVar( "$basetexture", &bFound, false );
    if ( !bFound || pBaseTexture == NULL )
        return luaL_error( L, "render.DrawScreenQuadWithTexture: material '%s' has no $basetexture", pszMaterial );
    pBaseTexture->SetTextureValue( pTexture );

    CMatRenderContextPtr pRenderContext( materials );
    pRenderContext->Bind( pMaterial );
    pRenderContext->DrawScreenSpaceQuad( pMaterial );
    return 0;
}
LUA_BINDING_END()

// render.CopyFrameToTexture( texture ) -- copies the CURRENT FRAME into the
// given render target texture, using the same call the engine's own freeze
// frame uses (CopyRenderTargetToTextureEx with the view rect).  The plain
// CopyRenderTargetToTexture binding produced BLACK in this DX9 layer, which
// is what black-screened halo's scene-restore step.
LUA_BINDING_BEGIN( Renders, CopyFrameToTexture, "library", "Copies the current frame into the given texture.", "client" )
{
    ITexture *pTexture = LUA_BINDING_ARGUMENT( luaL_checkitexture, 1, "texture" );

    CViewSetup playerView = *view->GetPlayerViewSetup();
    Rect_t rect;
    rect.x = playerView.x;
    rect.y = playerView.y;
    rect.width = playerView.width;
    rect.height = playerView.height;

    CMatRenderContextPtr pRenderContext( materials );
    pRenderContext->CopyRenderTargetToTextureEx( pTexture, 0, &rect, &rect );

    return 0;
}
LUA_BINDING_END()

// render.GetBloomTex1() -> Texture
LUA_BINDING_BEGIN( Renders, GetBloomTex1, "library", "Returns the bloom render target texture.", "client" )
{
    lua_pushitexture( L, materials->FindTexture( "_rt_Bloom1", TEXTURE_GROUP_RENDER_TARGET ) );
    return 1;
}
LUA_BINDING_END()

// render.BlurRenderTarget( rt, blurx, blury, passes )
//
// HL2SB (2026-09-24): separable gaussian on the ORIGIN Source blur shaders.
// stdshader_dx9 ships BlurFilterX/BlurFilterY (the bloom-chain blur: taps at
// 1.3366..11.4401 texels, sigma ~4.7 -- full source in
// materialsystem/stdshaders/BlurFilter[XY].cpp), and their stock VMTs
// dev/blurfilterx|y mount from hl2_misc, so this needs NO new shader and NO
// GMod plugin dll.  (GMod's own g_blurx/g_blury live in
// game_shader_generic_garrysmod.dll; same algorithm, Valve's sources here.)
//
// Structure mirrors GMod's Lua loop: rounds = passes+1, each round does
// horizontal (source -> ping) then vertical (ping -> source, in place).
// blurx/blury are API parity -- GMod scales its tap STEP with $size; this
// kernel's step is fixed, so large sizes are approximated with EXTRA ROUNDS
// (repeated convolution widens sigma by ~sqrt(rounds)).
LUA_BINDING_BEGIN( Renders, BlurRenderTarget, "library", "Blurs a render target (separable gaussian).", "client" )
{
    ITexture *pSource = LUA_BINDING_ARGUMENT( luaL_checkitexture, 1, "renderTarget" );
    float flBlurX = LUA_BINDING_ARGUMENT_WITH_DEFAULT( luaL_optnumber, 2, 2, "blurx" );
    float flBlurY = LUA_BINDING_ARGUMENT_WITH_DEFAULT( luaL_optnumber, 3, 2, "blury" );
    int nPasses = (int)LUA_BINDING_ARGUMENT_WITH_DEFAULT( luaL_optnumber, 4, 1, "passes" );

    // rounds = passes+1 like GMod's Lua; big blurx/blury adds rounds.
    float flSize = MAX( flBlurX, flBlurY );
    int nRounds = nPasses + 1;
    if ( flSize >= 6.0f )
        nRounds += 2;
    else if ( flSize >= 4.0f )
        nRounds += 1;
    nRounds = clamp( nRounds, 1, 6 );

    static IMaterial *s_pBlurX = NULL, *s_pBlurY = NULL;
    static IMaterialVar *s_pVarX = NULL, *s_pVarY = NULL;
    static ITexture *s_pPing = NULL;
    static bool s_bWarned = false;

    if ( s_pBlurX == NULL )
    {
        s_pBlurX = materials->FindMaterial( "dev/blurfilterx", TEXTURE_GROUP_CLIENT_EFFECTS );
        s_pBlurY = materials->FindMaterial( "dev/blurfiltery", TEXTURE_GROUP_CLIENT_EFFECTS );
        bool bVarX = false, bVarY = false;
        if ( s_pBlurX != NULL && !s_pBlurX->IsErrorMaterial() )
            s_pVarX = s_pBlurX->FindVar( "$basetexture", &bVarX );
        if ( s_pBlurY != NULL && !s_pBlurY->IsErrorMaterial() )
            s_pVarY = s_pBlurY->FindVar( "$basetexture", &bVarY );
        if ( !bVarX || !bVarY || s_pVarX == NULL || s_pVarY == NULL )
        {
            s_pBlurX = s_pBlurY = NULL;
            s_pVarX = s_pVarY = NULL;
        }
    }
    if ( s_pBlurX == NULL )
    {
        if ( !s_bWarned )
        {
            s_bWarned = true;
            Warning( "[HL2SB] BlurRenderTarget: dev/blurfilterx|y not resolvable - blur skipped\n" );
        }
        return 0;
    }

    int nW = pSource->GetActualWidth();
    int nH = pSource->GetActualHeight();
    if ( nW < 8 || nH < 8 )
        return 0;

    if ( s_pPing == NULL )
    {
        g_pMaterialSystem->OverrideRenderTargetAllocation( true );
        s_pPing = materials->CreateNamedRenderTargetTextureEx( "_rt_hl2sb_blurping",
            nW, nH, RT_SIZE_NO_CHANGE,
            IMAGE_FORMAT_RGBA8888, MATERIAL_RT_DEPTH_SHARED,
            TEXTUREFLAGS_CLAMPS | TEXTUREFLAGS_CLAMPT, 0 );
        g_pMaterialSystem->OverrideRenderTargetAllocation( false );
        if ( s_pPing == NULL )
            return 0;
    }
    // later calls with a different-size source clamp into the ping viewport
    int nVW = MIN( nW, s_pPing->GetActualWidth() );
    int nVH = MIN( nH, s_pPing->GetActualHeight() );

    CMatRenderContextPtr pRenderContext( materials );

    for ( int i = 0; i < nRounds; ++i )
    {
        // horizontal: source -> ping
        pRenderContext->PushRenderTargetAndViewport( s_pPing, 0, 0, nVW, nVH );
        s_pVarX->SetTextureValue( pSource );
        pRenderContext->DrawScreenSpaceQuad( s_pBlurX );
        pRenderContext->PopRenderTargetAndViewport();

        // vertical: ping -> source (in place, GMod parity)
        pRenderContext->PushRenderTargetAndViewport( pSource, 0, 0, nVW, nVH );
        s_pVarY->SetTextureValue( s_pPing );
        pRenderContext->DrawScreenSpaceQuad( s_pBlurY );
        pRenderContext->PopRenderTargetAndViewport();
    }

    return 0;
}
LUA_BINDING_END()

// render.ModelMaterialOverride( IMaterial|nil ) -- forces the material onto
// every DrawModel until cleared with nil.  The stable halo technique: draw the
// entity tinted with an additive flat material on top of its normal pass.
LUA_BINDING_BEGIN( Renders, ModelMaterialOverride, "library", "Forces a material onto all model rendering (nil clears).", "client" )
{
    if ( lua_isnoneornil( L, 1 ) )
    {
        modelrender->ForcedMaterialOverride( NULL );
        return 0;
    }

    IMaterial *pMaterial = NULL;
    if ( luaL_testudata( L, 1, LUA_MATERIALLIBNAME ) != NULL )
    {
        pMaterial = luaL_checkmaterial( L, 1 );
    }
    else if ( lua_type( L, 1 ) == LUA_TTABLE || lua_type( L, 1 ) == LUA_TSTRING )
    {
        // GMod parity with render.SetMaterial: accept Material() proxies/paths.
        const char *pszName = NULL;
        if ( lua_type( L, 1 ) == LUA_TSTRING )
        {
            pszName = lua_tostring( L, 1 );
        }
        else
        {
            lua_getfield( L, 1, "__path" );
            if ( lua_type( L, -1 ) == LUA_TSTRING )
                pszName = lua_tostring( L, -1 );
            lua_pop( L, 1 );
        }
        if ( pszName != NULL && pszName[0] != '\0' )
            pMaterial = materials->FindMaterial( pszName, TEXTURE_GROUP_CLIENT_EFFECTS, false );
    }

    if ( pMaterial == NULL || pMaterial->IsErrorMaterial() )
        return luaL_error( L, "render.ModelMaterialOverride: material expected" );

    modelrender->ForcedMaterialOverride( pMaterial );
    return 0;
}
LUA_BINDING_END()

//-----------------------------------------------------------------------------
// HL2SB GMod compat: the cam library.  cam.Start( {type="3D"/"2D", ...} )
// mirrors the table API the content render.lua shim builds on top of.
//-----------------------------------------------------------------------------

static int cam_Start (lua_State *L) {
  luaL_checktype( L, 1, LUA_TTABLE );

  lua_getfield( L, 1, "type" );
  const char *pszType = luaL_optstring( L, -1, "3D" );
  lua_pop( L, 1 );

  CViewSetup playerView = *view->GetPlayerViewSetup();
  CViewSetup viewSetup( playerView );
  viewSetup.m_flAspectRatio = 0.0f;   // the rect decides the aspect (see PushView3D)

  if ( !Q_stricmp( pszType, "2D" ) )
  {
    render->Push2DView( viewSetup, 0, NULL, view->GetFrustum() );
    return 0;
  }

  lua_getfield( L, 1, "origin" );
  if ( !lua_isnil( L, -1 ) ) viewSetup.origin = luaL_checkvector( L, -1 );
  lua_pop( L, 1 );

  lua_getfield( L, 1, "angles" );
  if ( !lua_isnil( L, -1 ) ) viewSetup.angles = luaL_checkangle( L, -1 );
  lua_pop( L, 1 );

  lua_getfield( L, 1, "fov" );
  if ( !lua_isnil( L, -1 ) ) viewSetup.fov = (float)lua_tonumber( L, -1 );
  lua_pop( L, 1 );

  lua_getfield( L, 1, "x" );
  if ( !lua_isnil( L, -1 ) ) viewSetup.x = (int)lua_tonumber( L, -1 );
  lua_pop( L, 1 );
  lua_getfield( L, 1, "y" );
  if ( !lua_isnil( L, -1 ) ) viewSetup.y = (int)lua_tonumber( L, -1 );
  lua_pop( L, 1 );
  lua_getfield( L, 1, "w" );
  if ( !lua_isnil( L, -1 ) ) viewSetup.width = (int)lua_tonumber( L, -1 );
  lua_pop( L, 1 );
  lua_getfield( L, 1, "h" );
  if ( !lua_isnil( L, -1 ) ) viewSetup.height = (int)lua_tonumber( L, -1 );
  lua_pop( L, 1 );

  lua_getfield( L, 1, "znear" );
  if ( !lua_isnil( L, -1 ) ) viewSetup.zNear = (float)lua_tonumber( L, -1 );
  lua_pop( L, 1 );
  lua_getfield( L, 1, "zfar" );
  if ( !lua_isnil( L, -1 ) ) viewSetup.zFar = (float)lua_tonumber( L, -1 );
  lua_pop( L, 1 );

  render->Push3DView( viewSetup, 0, NULL, view->GetFrustum() );
  return 0;
}

static int cam_End (lua_State *L) {
  render->PopView( view->GetFrustum() );
  return 0;
}

// cam.IgnoreZ( ignore ) -- depth override for subsequent 2D/3D2D draws.
// Reference behavior (GMod): the context's depth range is clamped to
// (0, 0.01) while ignored -- everything drawn afterwards maps to the near
// plane, so already-rendered geometry cannot occlude it -- and restored to
// (0, 1) when the override is lifted.
static int cam_IgnoreZ (lua_State *L) {
  bool bIgnore = lua_toboolean( L, 1 ) != 0;

  CMatRenderContextPtr pRenderContext( materials );
  pRenderContext->DepthRange( 0.0f, bIgnore ? 0.01f : 1.0f );
  return 0;
}

// cam.Start3D2D( pos, angles, scale ) / cam.End3D2D() -- HL2SB GMod compat
// (2026-09-25).  GMod: builds Translate(pos)*Rotate(angles)*Scale(scale) and
// pushes it as the model matrix so 2D draws land in that world plane.  The
// push goes to BOTH consumers: the render context (meshes, render.DrawQuad)
// and CMatSystemSurface (surface text/rects - its StartDrawing resets the
// context matrix around every batch, so it carries its own stack, see
// vguimatsurface/MatSystemSurface.cpp).
static int cam_Start3D2D (lua_State *L) {
	Vector pos = luaL_checkvector( L, 1 );
	QAngle ang = luaL_checkangle( L, 2 );
	float scale = luaL_optnumber( L, 3, 1.0f );

	if ( !g_pMatSystemSurface )
		return 0;

	matrix3x4_t mat;
	AngleMatrix( ang, pos, mat );
	VMatrix vm( mat );

	// HL2SB GMod compat (2026-09-25): exact wiki formula - SetAngles,
	// SetTranslation, SetScale( Vector( scale, -scale, 1 ) ).  Rows of the
	// AngleMatrix are the world-space basis (row0 = forward = 2D +x,
	// row1 = -right = 2D +y), so SetScale scales ROWS: x by +scale, y by
	// -scale (flips vgui's downward y), z (out of the plane) left at 1.
	// The old uniform scale mirrored the text and broke the winding.
	for ( int c = 0; c < 4; c++ )
	{
		vm.m[0][c] *= scale;
		vm.m[1][c] *= -scale;
	}

	g_pMatSystemSurface->PushModelMatrix( vm );
	CMatRenderContextPtr pRenderContext( materials );
	pRenderContext->MatrixMode( MATERIAL_MODEL );
	pRenderContext->PushMatrix();
	pRenderContext->LoadIdentity();
	pRenderContext->LoadMatrix( vm );
	return 0;
}

static int cam_End3D2D (lua_State *L) {
	if ( !g_pMatSystemSurface )
		return 0;
	g_pMatSystemSurface->PopModelMatrix();
	CMatRenderContextPtr pRenderContext( materials );
	pRenderContext->MatrixMode( MATERIAL_MODEL );
	pRenderContext->PopMatrix();
	return 0;
}

static const luaL_Reg cam_funcs[] = {
  { "Start", cam_Start },
  { "End", cam_End },
  { "IgnoreZ", cam_IgnoreZ },
  { "Start3D2D", cam_Start3D2D },
  { "End3D2D", cam_End3D2D },
  { NULL, NULL }
};


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

/*
** HL2SB: GMod's render.SetLocalModelLights( lights )
**   https://wiki.facepunch.com/gmod/render.SetLocalModelLights
**   https://wiki.facepunch.com/gmod/Structures/LocalLight
**   enums: MATERIAL_LIGHT_DISABLE/POINT/DIRECTIONAL/SPOT = 0/1/2/3
**          (https://wiki.facepunch.com/gmod/Enums/MATERIAL_LIGHT)
**
** Up to four lights, used by the player model editor for its three coloured point
** lights (sandbox/gamemode/editor_player.lua, mdl:PreDrawModel).
**
** WARNING: the studio renderer KEEPS THE POINTER, so the array has to outlive the draw
** call.  It is a file static for that reason: handing it a stack local is exactly the
** bug that turns every model and brush purple after a CModelPanel overlay closes
** (the removed C++ context menu hit the same trap in its CModelPanel::Paint override).
*/
static LightDesc_t g_HL2SBLocalModelLights[4];
static int g_nHL2SBLocalModelLights = 0;

// Read a Vector field out of the table at the top of the stack.  luaL_checkvector()
// asserts the metatable, so a table holding something else must not reach it.
static bool HL2SB_LuaTableVector( lua_State *L, const char *pszField, Vector &out )
{
    bool bFound = false;

    lua_getfield( L, -1, pszField );

    if ( luaL_testudata( L, -1, LUA_VECTORLIBNAME ) != NULL )
    {
        out = luaL_checkvector( L, -1 );
        bFound = true;
    }

    lua_pop( L, 1 );
    return bFound;
}

static float HL2SB_LuaTableNumber( lua_State *L, const char *pszField, float flDefault )
{
    float flValue = flDefault;

    lua_getfield( L, -1, pszField );

    if ( lua_isnumber( L, -1 ) )
        flValue = (float)lua_tonumber( L, -1 );

    lua_pop( L, 1 );
    return flValue;
}

LUA_BINDING_BEGIN( Renders, SetLocalModelLights, "library", "Sets up local lighting for any upcoming render operation", "client" )
{
    g_nHL2SBLocalModelLights = 0;

    if ( lua_istable( L, 1 ) )
    {
        for ( int i = 0; i < 4; i++ )
        {
            lua_rawgeti( L, 1, i + 1 );

            if ( !lua_istable( L, -1 ) )
            {
                lua_pop( L, 1 );
                break;      // a sequential table ends at the first hole
            }

            LightDesc_t &desc = g_HL2SBLocalModelLights[g_nHL2SBLocalModelLights];
            memset( &desc, 0, sizeof( desc ) );

            Vector vecColor = vec3_origin;
            Vector vecPos = vec3_origin;
            Vector vecDir = Vector( 0, 1, 0 );

            HL2SB_LuaTableVector( L, "color", vecColor );
            HL2SB_LuaTableVector( L, "pos", vecPos );
            HL2SB_LuaTableVector( L, "dir", vecDir );

            const int iType = (int)HL2SB_LuaTableNumber( L, "type", MATERIAL_LIGHT_POINT );

            // Angles are degrees in Lua (the wiki says so) and radians in LightDesc_t.
            const float flInner = DEG2RAD( HL2SB_LuaTableNumber( L, "innerAngle", 45.0f ) );
            const float flOuter = DEG2RAD( HL2SB_LuaTableNumber( L, "outerAngle", 45.0f ) );

            if ( iType == MATERIAL_LIGHT_DIRECTIONAL )
            {
                desc.InitDirectional( vecDir, vecColor );
            }
            else if ( iType == MATERIAL_LIGHT_SPOT )
            {
                desc.InitSpot( vecPos, vecColor, vecPos + vecDir * 100.0f, flInner, flOuter );
                desc.m_Falloff = HL2SB_LuaTableNumber( L, "angularFalloff", 5.0f );
            }
            else
            {
                desc.InitPoint( vecPos, vecColor );
            }

            desc.m_Range = HL2SB_LuaTableNumber( L, "range", 0.0f );

            /*
            ** Falloff, in GMod's documented order of preference
            ** (Structures/LocalLight): fiftyPercentDistance/zeroPercentDistance win, and
            ** the explicit constant/linear/quadratic terms are only used without them.
            **
            ** GMod's own engine has its own attenuation model that this tree has no
            ** source for; what is implemented here is the same shape:
            **   intensity(d) = 1 / ( a0 + a1*d + a2*d^2 )
            **   - a1 = 1 / fiftyPercentDistance   gives exactly 50% at that distance
            **   - zeroPercentDistance becomes the light's range (it fades to nothing)
            ** so the three-light rig the player editor builds (which passes neither) and
            ** the common addon form both behave.
            */
            const float flFifty = HL2SB_LuaTableNumber( L, "fiftyPercentDistance", 0.0f );
            const float flZero = HL2SB_LuaTableNumber( L, "zeroPercentDistance", 0.0f );

            if ( flFifty > 0.0f || flZero > 0.0f )
            {
                desc.m_Attenuation0 = 1.0f;
                desc.m_Attenuation1 = ( flFifty > 0.0f ) ? ( 1.0f / flFifty ) : 0.0f;
                desc.m_Attenuation2 = 0.0f;
                desc.m_Flags |= LIGHTTYPE_OPTIMIZATIONFLAGS_HAS_ATTENUATION0;
                desc.m_Flags |= LIGHTTYPE_OPTIMIZATIONFLAGS_HAS_ATTENUATION1;

                if ( flZero > 0.0f && desc.m_Range <= 0.0f )
                    desc.m_Range = flZero;
            }
            else
            {
                desc.m_Attenuation0 = HL2SB_LuaTableNumber( L, "constantFalloff", 1.0f );
                desc.m_Attenuation1 = HL2SB_LuaTableNumber( L, "linearFalloff", 0.0f );
                desc.m_Attenuation2 = HL2SB_LuaTableNumber( L, "quadraticFalloff", 0.0f );

                desc.m_Flags |= LIGHTTYPE_OPTIMIZATIONFLAGS_HAS_ATTENUATION0;

                if ( desc.m_Attenuation1 != 0.0f )
                    desc.m_Flags |= LIGHTTYPE_OPTIMIZATIONFLAGS_HAS_ATTENUATION1;

                if ( desc.m_Attenuation2 != 0.0f )
                    desc.m_Flags |= LIGHTTYPE_OPTIMIZATIONFLAGS_HAS_ATTENUATION2;
            }

            desc.RecalculateDerivedValues();

            g_nHL2SBLocalModelLights++;

            lua_pop( L, 1 );
        }
    }

    if ( g_pStudioRender != NULL )
    {
        g_pStudioRender->SetLocalLights( g_nHL2SBLocalModelLights,
            ( g_nHL2SBLocalModelLights > 0 ) ? g_HL2SBLocalModelLights : NULL );
    }

    return 0;
}
LUA_BINDING_END()

LUA_BINDING_BEGIN( Renders, BindLocalCubemap, "library", "Binds a cubemap for the upcoming model draws.", "client" )
{
    /*
    ** GMod: render.BindLocalCubemap( name ) -- "Binds a local cubemap to be used in the
    ** next render operations" (wiki).  Its own player model selector calls it with
    ** "editor/cubemap" so the preview does not inherit the map's reflections:
    **
    **     render.BindLocalCubemap( "editor/cubemap" )
    **     (garrysmod/gamemodes/sandbox/gamemode/editor_player.lua, mdl:PreDrawModel)
    **
    ** The texture is the one the engine's own CModelPanel already loads for every model
    ** preview (game/client/game_controls/basemodelpanel.cpp:511), so it is present in
    ** this fork's content.
    **
    ** Deviation: nil clears the binding again.  The bind is global render state and the
    ** Lua model panel draws inside a vgui paint, so leaving the preview's cubemap bound
    ** would leak it into the world pass - the same class of leak documented in
    ** the removed C++ context menu hit the same trap.
    */
    CMatRenderContextPtr pRenderContext( materials );

    if ( lua_isnoneornil( L, 1 ) )
    {
        pRenderContext->BindLocalCubemap( NULL );
        return 0;
    }

    const char *pszName = LUA_BINDING_ARGUMENT( luaL_checkstring, 1, "name" );
    pRenderContext->BindLocalCubemap( materials->FindTexture( pszName, NULL, true ) );

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

    // HL2SB: the path this call asked for, kept in scope for the diagnostic
    // below (NULL when the caller handed us a real IMaterial object).
    const char *pszName = NULL;

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

    // HL2SB: DrawScreenQuad draws THIS material -- render.SetMaterial is the
    // only way Lua binds one, so remember it here.
    s_pRendersLastSetMaterial = pMaterial;
    // HL2SB diagnostic: name what a script's Material() path actually resolved
    // to.  A proxy table (gmod_surface.lua's Material()) is not an IMaterial, so
    // this is also the place that can silently hand back the error material.
    // PERF (2026-09-23): SetMaterial runs per entity per frame on the render
    // path; only the error-material case is worth a lookup + format.  The
    // not-an-error spam was WarnOnce-deduped anyway, so nothing visible changes.
    if ( pMaterial != NULL && pMaterial->IsErrorMaterial() )
    {
        char szKey[ 192 ];
        Q_snprintf( szKey, sizeof( szKey ), "setmaterial:%s", pMaterial->GetName() );
        HL2SB_WarnOnce( szKey,
            "render.SetMaterial -> '%s' (error=%d, requested '%s')\n",
            pMaterial->GetName(),
            pMaterial->IsErrorMaterial() ? 1 : 0,
            ( pszName != NULL ) ? pszName : "<material object>" );
    }

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

    // HL2SB diagnostic: the nyan bomb only draws via DrawQuadEasy -- if this
    // never runs, ENT:Draw is silent and the entity looks like it has "no model".
    // PERF (2026-09-23): WarnOnce already dedupes the output, but the key
    // format + GetName ran per call on the render path; log once, ever.
    {
        static bool s_bLoggedDrawQuad = false;
        if ( !s_bLoggedDrawQuad )
        {
            s_bLoggedDrawQuad = true;
            char szKey[ 192 ];
            Q_snprintf( szKey, sizeof( szKey ), "drawquad:%s",
                ( g_pHL2SBLastBoundMaterial != NULL ) ? g_pHL2SBLastBoundMaterial->GetName() : "<none>" );
            HL2SB_WarnOnce( szKey,
                "DrawQuadEasy mat='%s' pos=(%.0f %.0f %.0f) n=(%.1f %.1f %.1f) %.0fx%.0f a=%d\n",
                ( g_pHL2SBLastBoundMaterial != NULL ) ? g_pHL2SBLastBoundMaterial->GetName() : "<none>",
                position.x, position.y, position.z,
                normal.x, normal.y, normal.z,
                width, height, color.a() );
        }
    }

    VectorNormalize( normal );

    // A basis perpendicular to the quad's normal.
    Vector reference( 0.0f, 0.0f, 1.0f );
    if ( fabs( normal.z ) > 0.99f )
    {
        reference.Init( 0.0f, 1.0f, 0.0f );
    }

    Vector right = normal.Cross( reference );
    VectorNormalize( right );
    // HL2SB (2026-09-21): was right.Cross( normal ) -- the in-plane up ended
    // up 180 degrees from GMod's convention and every DrawQuadEasy sprite
    // (the nyan grenade cat) rendered upside-down.  normal.Cross( right ) is
    // the GMod orientation (verified against the nyan addon's tuned rotation).
    Vector up = normal.Cross( right );
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
    // HL2SB: the material goes in explicitly -- the mesh's vertex format comes
    // from it, and the no-argument form can hand back a mesh whose format does
    // not match, in which case the vertex writes land in the wrong fields.
    IMesh *pMesh = pRenderContext->GetDynamicMesh( true, NULL, NULL, g_pHL2SBLastBoundMaterial );

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


    // HL2SB: the casts are required - clang and gcc both reject narrowing an int
    // to color32's byte fields inside a braced initializer list
    // ([-Wc++11-narrowing] on the Android builds).  MSVC accepts it silently.
    color32 rawColor = { (byte)color.r(), (byte)color.g(), (byte)color.b(), (byte)color.a() };
    DrawSprite( position, width, height, rawColor );

    return 0;
}
LUA_BINDING_END()

LUA_BINDING_BEGIN( Renders, ComputeLighting, "library", "Returns the lighting at the given position, in the 0..1 range GMod scripts expect.", "client" )
{
    Vector position = LUA_BINDING_ARGUMENT( luaL_checkvector, 1, "position" );
    Vector defaultNormal = vec3_origin;
    Vector normal = LUA_BINDING_ARGUMENT_WITH_DEFAULT( luaL_optvector, 2, &defaultNormal, "normal" );
    bool bClamp = LUA_BINDING_ARGUMENT_WITH_DEFAULT( luaL_optboolean, 3, true, "clamp" );

    // HL2SB: this is a real engine entry point, not an approximation --
    // IVEngineClient::ComputeLighting() (public/cdll_int.h:392), the same call
    // game/client/c_impact_effects.cpp:486 makes for impact lighting.  With a
    // normal it evaluates the world lights plus the ambient cube at that point,
    // which is what GMod's render.ComputeLighting returns; the bClamp argument
    // (GMod's default is true) is passed straight through.
    Vector color( 1, 1, 1 );
    engine->ComputeLighting( position, ( normal == vec3_origin ) ? NULL : &normal, bClamp, color, NULL );

    lua_pushvector( L, color );
    return 1;
}
LUA_BINDING_END( "Vector", "The lighting colour at the position." )

LUA_BINDING_BEGIN( Renders, DrawBeam, "library", "Draws a beam", "client" )
{
    Vector &start = LUA_BINDING_ARGUMENT( luaL_checkvector, 1, "start" );
    Vector &end = LUA_BINDING_ARGUMENT( luaL_checkvector, 2, "end" );
    float width = LUA_BINDING_ARGUMENT( luaL_checknumber, 3, "width" );
    float textureStart = LUA_BINDING_ARGUMENT( luaL_checknumber, 4, "textureStart" );
    float textureEnd = LUA_BINDING_ARGUMENT( luaL_checknumber, 5, "textureEnd" );
    lua_Color color = LUA_BINDING_ARGUMENT_WITH_DEFAULT( luaL_optcolor, 6, lua_Color( 255, 255, 255, 255 ), "color" );

    // HL2SB diagnostic: a handful of full-geometry lines so a missing beam can
    // still be distinguished from "never submitted".  Was capped at 120, which
    // flooded ds_debug.log on one nyan impact (and looked like errors).
    {
        static int s_nDrawBeamLogged = 0;

        if ( s_nDrawBeamLogged < 8 )
        {
            ++s_nDrawBeamLogged;

            const char *pszMaterial = ( g_pHL2SBLastBoundMaterial != NULL ) ? g_pHL2SBLastBoundMaterial->GetName() : "<none bound>";
            Vector vecViewOrigin = MainViewOrigin();

            Warning( "[HL2SB] render.DrawBeam #%d: material '%s' width=%.2f alpha=%.2f length=%.0f start=(%.0f %.0f %.0f) end=(%.0f %.0f %.0f) view=(%.0f %.0f %.0f)\n",
                s_nDrawBeamLogged, pszMaterial, width, color.a() / 255.0f, end.DistTo( start ),
                start.x, start.y, start.z, end.x, end.y, end.z,
                vecViewOrigin.x, vecViewOrigin.y, vecViewOrigin.z );
        }
    }

    CMatRenderContextPtr pRenderContext( materials );

    // HL2SB: hand-built ribbon instead of CBeamSegDraw.
    //
    // CBeamSegDraw::SpecifySeg() writes a SECOND texture coordinate and
    // TANGENT_S/TANGENT_T per vertex.  That is fine for the materials the engine
    // feeds it (CSpriteTrail passes a sprite-shader material, and the Nyan Gun's
    // bomb trail -- the one rainbow that is actually visible -- is drawn that
    // way), but a GMod effect beam binds an ordinary UnlitGeneric vmt
    // (nyan/rainbow.vmt, sprites/redglow1.vmt, ...): its vertex format has no
    // tangent and no second uv set, so those writes land on a -1 vertex offset
    // and the beam comes out with uninitialised vertex data.
    //
    // The ribbon is therefore built the way the engine's own FX line renderer
    // builds its tracers (game/client/fx_line.cpp, CFXLine::Draw): a
    // camera-facing quad out of position/colour/texcoord0, with the width axis
    // taken from the CURRENT VIEW ORIGIN.  That last part is not cosmetic: the
    // render context's world-space camera position is not necessarily filled in
    // inside the client-effect pass, and a wrong origin turns the quad edge-on --
    // every number in the log still correct, and not one pixel on screen.
    Vector vecCameraPos = MainViewOrigin();

    Vector vecAlong = end - start;
    Vector vecWidth;

    CrossProduct( vecAlong, start - vecCameraPos, vecWidth );

    if ( vecWidth.LengthSqr() < 1e-6f )
    {
        // The beam points at the camera: fall back to a stable world axis.
        Vector vecReference( 0.0f, 0.0f, 1.0f );
        if ( fabs( vecAlong.z ) > 0.9f )
        {
            vecReference.Init( 1.0f, 0.0f, 0.0f );
        }

        CrossProduct( vecAlong, vecReference, vecWidth );
    }

    if ( vecWidth.LengthSqr() < 1e-12f )
    {
        // Degenerate both ways (zero length beam): nothing to draw.
        return 0;
    }

    VectorNormalize( vecWidth );

    const Vector &vecAxis = vecWidth;	// unit width axis
    const Vector vecHalfA = vecAxis * ( width * 0.5f );

    const float flRed   = color.r() / 255.0f;
    const float flGreen = color.g() / 255.0f;
    const float flBlue  = color.b() / 255.0f;
    const float flAlpha = color.a() / 255.0f;

    const unsigned char ubRed   = (unsigned char)( flRed * 255.0f );
    const unsigned char ubGreen = (unsigned char)( flGreen * 255.0f );
    const unsigned char ubBlue  = (unsigned char)( flBlue * 255.0f );
    const unsigned char ubAlpha = (unsigned char)( flAlpha * 255.0f );

    // The material goes in explicitly: the mesh's vertex format comes from it,
    // and GetDynamicMesh() without it can hand back a mesh whose format does not
    // match the material at all -- the draw then writes the wrong fields and
    // nothing appears.  (CBeamSegDraw::Start did the same thing.)
    IMesh *pMesh = pRenderContext->GetDynamicMesh( true, NULL, NULL, g_pHL2SBLastBoundMaterial );
    CMeshBuilder meshBuilder;

    // One quad, written exactly the way CFXLine::Draw() (game/client/fx_line.cpp)
    // writes the engine's own tracers: position, texcoord0, colour and normal,
    // MATERIAL_QUADS.  Staying identical to the working implementation matters
    // more here than any cleverness of my own.
    Vector vecTmp;

    meshBuilder.Begin( pMesh, MATERIAL_QUADS, 1 );

    // Start edge, u = 1
    VectorMA( start, -1.0f, vecHalfA, vecTmp );
    meshBuilder.Position3fv( vecTmp.Base() );
    meshBuilder.TexCoord2f( 0, 1.0f, textureStart );
    meshBuilder.Color4ub( ubRed, ubGreen, ubBlue, ubAlpha );
    meshBuilder.Normal3fv( vecAxis.Base() );
    meshBuilder.AdvanceVertex();

    // Start edge, u = 0
    VectorMA( start, 1.0f, vecHalfA, vecTmp );
    meshBuilder.Position3fv( vecTmp.Base() );
    meshBuilder.TexCoord2f( 0, 0.0f, textureStart );
    meshBuilder.Color4ub( ubRed, ubGreen, ubBlue, ubAlpha );
    meshBuilder.Normal3fv( vecAxis.Base() );
    meshBuilder.AdvanceVertex();

    // End edge, u = 0
    VectorMA( end, 1.0f, vecHalfA, vecTmp );
    meshBuilder.Position3fv( vecTmp.Base() );
    meshBuilder.TexCoord2f( 0, 0.0f, textureEnd );
    meshBuilder.Color4ub( ubRed, ubGreen, ubBlue, ubAlpha );
    meshBuilder.Normal3fv( vecAxis.Base() );
    meshBuilder.AdvanceVertex();

    // End edge, u = 1
    VectorMA( end, -1.0f, vecHalfA, vecTmp );
    meshBuilder.Position3fv( vecTmp.Base() );
    meshBuilder.TexCoord2f( 0, 1.0f, textureEnd );
    meshBuilder.Color4ub( ubRed, ubGreen, ubBlue, ubAlpha );
    meshBuilder.Normal3fv( vecAxis.Base() );
    meshBuilder.AdvanceVertex();

    meshBuilder.End();
    pMesh->Draw();

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
    // HL2SB: the cam library -- cam.Start( {type="3D"/"2D", ...} ) / cam.End() /
    // cam.IgnoreZ().  The content render.lua shim builds cam.Start3D/Start2D on
    // top of these.
    luaL_register( L, "cam", cam_funcs );
    lua_pop( L, 1 );

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

    // HL2SB: the light types render.SetLocalModelLights takes
    // (https://wiki.facepunch.com/gmod/Enums/MATERIAL_LIGHT).  lua_pushenum publishes
    // both the short name ( MATERIAL_LIGHT_POINT ) and the table field
    // ( MATERIAL_LIGHT.POINT ), so GMod Lua finds the global it expects.
    LUA_SET_ENUM_LIB_BEGIN( L, "MATERIAL_LIGHT" );
    lua_pushenum( L, MATERIAL_LIGHT_DISABLE, "DISABLE" );
    lua_pushenum( L, MATERIAL_LIGHT_POINT, "POINT" );
    lua_pushenum( L, MATERIAL_LIGHT_DIRECTIONAL, "DIRECTIONAL" );
    lua_pushenum( L, MATERIAL_LIGHT_SPOT, "SPOT" );
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
