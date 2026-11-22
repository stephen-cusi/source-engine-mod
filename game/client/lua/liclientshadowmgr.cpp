//========= Copyright © 1996-2005, Valve Corporation, All rights reserved. ============//
//
// Purpose: 
//
// $NoKeywords: $
//
//=============================================================================//

#define liclientshadowmgr_cpp

#include "cbase.h"
#include "luamanager.h"
#include "mathlib/lvector.h"
#include "materialsystem/imaterialsystem.h"  // FlashlightState_t, materials
#include "materialsystem/itexture.h"         // ITexture (AddRef/Release on SetTexture)
#include "texture_group_names.h"             // TEXTURE_GROUP_OTHER
#include "lColor.h"                          // luaL_checkcolor / lua_pushcolor
#include "litexture.h"                       // luaL_checkitexture / lua_pushitexture
#include "lbaseentity_shared.h"
#include "igamesystem.h"
#include "tier1/utlvector.h"
#include <new>

// memdbgon must be the last include file in a .cpp file!!!
#include "tier0/memdbgon.h"



static int g_pClientShadowMgr_AdvanceFrame (lua_State *L) {
  g_pClientShadowMgr->AdvanceFrame();
  return 0;
}

static int g_pClientShadowMgr_GetShadowDirection (lua_State *L) {
  Vector v = g_pClientShadowMgr->GetShadowDirection();
  lua_pushvector(L, v);
  return 1;
}

static int g_pClientShadowMgr_RenderShadowTexture (lua_State *L) {
  g_pClientShadowMgr->RenderShadowTexture(luaL_checkint(L, 1), luaL_checkint(L, 2));
  return 0;
}

static int g_pClientShadowMgr_SetShadowBlobbyCutoffArea (lua_State *L) {
  g_pClientShadowMgr->SetShadowBlobbyCutoffArea(luaL_checknumber(L, 1));
  return 0;
}

static int g_pClientShadowMgr_SetShadowColor (lua_State *L) {
  g_pClientShadowMgr->SetShadowColor(luaL_checkint(L, 1), luaL_checkint(L, 2), luaL_checkint(L, 3));
  return 0;
}

static int g_pClientShadowMgr_SetShadowDirection (lua_State *L) {
  g_pClientShadowMgr->SetShadowDirection(luaL_checkvector(L, 1));
  return 0;
}

static int g_pClientShadowMgr_SetShadowDistance (lua_State *L) {
  g_pClientShadowMgr->SetShadowDistance(luaL_checknumber(L, 1));
  return 0;
}

static int g_pClientShadowMgr_SetShadowsDisabled (lua_State *L) {
  g_pClientShadowMgr->SetShadowsDisabled(luaL_checkboolean(L, 1));
  return 0;
}

static int g_pClientShadowMgr_UnlockAllShadowDepthTextures (lua_State *L) {
  g_pClientShadowMgr->UnlockAllShadowDepthTextures();
  return 0;
}


static const luaL_Reg g_pClientShadowMgrlib[] = {
  {"AdvanceFrame",   g_pClientShadowMgr_AdvanceFrame},
  {"GetShadowDirection",   g_pClientShadowMgr_GetShadowDirection},
  {"RenderShadowTexture",   g_pClientShadowMgr_RenderShadowTexture},
  {"SetShadowBlobbyCutoffArea",   g_pClientShadowMgr_SetShadowBlobbyCutoffArea},
  {"SetShadowColor",   g_pClientShadowMgr_SetShadowColor},
  {"SetShadowDirection",   g_pClientShadowMgr_SetShadowDirection},
  {"SetShadowDistance",   g_pClientShadowMgr_SetShadowDistance},
  {"UnlockAllShadowDepthTextures",   g_pClientShadowMgr_UnlockAllShadowDepthTextures},
  {NULL, NULL}
};


// ---------------------------------------------------------------------------
// HL2SB: ProjectedTexture() -- GMod's client-side spotlight object.
//
// Engine side it is the same flashlight handle the env_projectedtexture
// entity drives (CreateFlashlight / UpdateFlashlightState /
// DestroyFlashlight on g_pClientShadowMgr).  The Lua object stores the light
// parameters; Update() folds them into one FlashlightState_t and pushes the
// state to the shadow manager, so every setter takes effect on the next
// Update -- which is exactly how the nvg addon drives its NVG light
// (set parameters, call Update every frame while the goggles are down,
// Remove() when they flip up or the battery dies).
//
// FindMetaTable("ProjectedTexture") resolves through the alias table in
// lsrcinit, so only the metatable name and the global constructor are
// registered here (luaopen_g_pClientShadowMgr, client realm only).
// ---------------------------------------------------------------------------
#define HL2SB_PROJECTEDTEXTURE_METATABLE "ProjectedTexture"

struct HL2SB_ProjectedTexture_t
{
  Vector        origin;
  Quaternion    orientation;
  float         fovH;
  float         fovV;
  float         brightness;
  Color         color;
  bool          enableShadows;
  float         nearZ;
  float         farZ;
  float         quadraticAtten;
  float         linearAtten;
  float         constantAtten;
  bool          lightWorld;
  float         shadowSlopeScaleDepthBias;
  float         shadowDepthBias;
  float         shadowFilter;
  float         shadowMapResolution;
  bool          ortho, noCull, skipShadowUpdates;
  float         orthoLeft, orthoTop, orthoRight, orthoBottom;
  CHandle<C_BaseEntity> target;
  int           shadowQuality;
  int           textureFrame;
  ITexture     *pTexture;   // borrowed ITexture (AddRef'd) when SetTexture got one
  bool          alive;      // cleared by Remove(); the light may exist before first Update
  ClientShadowHandle_t handle;
};

static CUtlVector<HL2SB_ProjectedTexture_t *> s_ProjectedTextures;
static ConVar r_projectedtexture_filter( "r_projectedtexture_filter", "1", FCVAR_ARCHIVE );
extern ConVar r_flashlightdepthtexture;
extern ConVar r_flashlightdepthres;

static HL2SB_ProjectedTexture_t *HL2SB_ProjectedTexture_Check( lua_State *L, int nArg )
{
  return (HL2SB_ProjectedTexture_t *)luaL_checkudata( L, nArg, HL2SB_PROJECTEDTEXTURE_METATABLE );
}

static void HL2SB_ProjectedTexture_Destroy( HL2SB_ProjectedTexture_t *pPT )
{
  if ( pPT->handle != CLIENTSHADOW_INVALID_HANDLE && g_pClientShadowMgr != NULL )
  {
    g_pClientShadowMgr->DestroyFlashlight( pPT->handle );
  }
  pPT->handle = CLIENTSHADOW_INVALID_HANDLE;

  if ( pPT->pTexture != NULL )
  {
    pPT->pTexture->Release();
    pPT->pTexture = NULL;
  }
  pPT->alive = false;
}

class CHL2SBProjectedTextureSystem : public CAutoGameSystem
{
public:
  CHL2SBProjectedTextureSystem() : CAutoGameSystem( "LuaProjectedTextures" ) {}
  void LevelShutdownPreEntity() override
  {
    FOR_EACH_VEC( s_ProjectedTextures, i )
      HL2SB_ProjectedTexture_Destroy( s_ProjectedTextures[i] );
  }
};
static CHL2SBProjectedTextureSystem s_ProjectedTextureSystem;

static void HL2SB_ProjectedTexture_Apply( HL2SB_ProjectedTexture_t *pPT )
{
  if ( !pPT->alive || pPT->handle == CLIENTSHADOW_INVALID_HANDLE || g_pClientShadowMgr == NULL )
    return;

  extern void HL2SB_SetProjectedTextureSuppressed( ClientShadowHandle_t, bool );
  const bool suppressed = pPT->nearZ == 0.0f;
  HL2SB_SetProjectedTextureSuppressed( pPT->handle, suppressed );
  if ( suppressed )
    return;

  FlashlightState_t state;
  state.m_vecLightOrigin = pPT->origin;
  state.m_quatOrientation = pPT->orientation;
  state.m_fHorizontalFOVDegrees = pPT->fovH;
  state.m_fVerticalFOVDegrees = pPT->fovV;
  state.m_fQuadraticAtten = pPT->quadraticAtten;
  state.m_fLinearAtten = pPT->linearAtten;
  state.m_fConstantAtten = pPT->constantAtten;
  // GMod stores the colour premultiplied by brightness; same result here.
  state.m_Color[0] = ( pPT->color.r() / 255.0f ) * pPT->brightness;
  state.m_Color[1] = ( pPT->color.g() / 255.0f ) * pPT->brightness;
  state.m_Color[2] = ( pPT->color.b() / 255.0f ) * pPT->brightness;
  state.m_Color[3] = 1.0f;
  state.m_NearZ = pPT->nearZ;
  state.m_FarZ = pPT->farZ;
  state.m_bEnableShadows = pPT->enableShadows;
  state.m_flShadowSlopeScaleDepthBias = pPT->shadowSlopeScaleDepthBias;
  state.m_flShadowDepthBias = pPT->shadowDepthBias;
  state.m_flShadowFilterSize = pPT->shadowFilter;
  state.m_flShadowMapResolution = pPT->shadowMapResolution;
  state.m_nShadowQuality = pPT->shadowQuality;
  state.m_pSpotlightTexture = ( pPT->pTexture != NULL )
    ? pPT->pTexture
    : materials->FindTexture( "effects/flashlight001", TEXTURE_GROUP_OTHER, false );
  state.m_nSpotlightTextureFrame = pPT->textureFrame;

  g_pClientShadowMgr->UpdateFlashlightState( pPT->handle, state );
  extern void HL2SB_SetProjectedTextureOptions( ClientShadowHandle_t, bool, float, float, float, float, bool, bool );
  HL2SB_SetProjectedTextureOptions( pPT->handle, pPT->ortho, pPT->orthoLeft, pPT->orthoTop,
    pPT->orthoRight, pPT->orthoBottom, pPT->noCull, pPT->skipShadowUpdates );

  g_pClientShadowMgr->SetFlashlightTarget( pPT->handle, pPT->target.Get() );
  g_pClientShadowMgr->SetFlashlightLightWorld( pPT->handle, pPT->lightWorld );
  g_pClientShadowMgr->UpdateProjectedTexture( pPT->handle, true );
}

static int HL2SB_ProjectedTexture_Create( lua_State *L )
{
  HL2SB_ProjectedTexture_t *pPT =
    (HL2SB_ProjectedTexture_t *)lua_newuserdata( L, sizeof( HL2SB_ProjectedTexture_t ) );
  new (pPT) HL2SB_ProjectedTexture_t();

  pPT->origin.Init();
  pPT->orientation.Init( 0, 0, 0, 0 );
  pPT->fovH = 90.0f;
  pPT->fovV = 90.0f;
  pPT->brightness = 1.0f;
  pPT->color.SetColor( 255, 255, 255, 255 );
  pPT->enableShadows = r_flashlightdepthtexture.GetBool();
  pPT->nearZ = 1.0f;
  pPT->farZ = 2048.0f;
  pPT->quadraticAtten = 0.0f;
  pPT->linearAtten = 100.0f;
  pPT->constantAtten = 0.0f;
  pPT->lightWorld = true;
  pPT->shadowSlopeScaleDepthBias = 2.0f;
  pPT->shadowDepthBias = 0.0001f;
  pPT->shadowFilter = r_projectedtexture_filter.GetFloat();
  pPT->shadowMapResolution = (float)r_flashlightdepthres.GetInt();
  pPT->ortho = pPT->noCull = pPT->skipShadowUpdates = false;
  pPT->orthoLeft = pPT->orthoTop = pPT->orthoRight = pPT->orthoBottom = 0.0f;
  pPT->shadowQuality = 1;
  pPT->textureFrame = 0;
  pPT->pTexture = NULL;
  pPT->alive = true;
  pPT->handle = CLIENTSHADOW_INVALID_HANDLE;

  FlashlightState_t state;
  state.m_vecLightOrigin = pPT->origin;
  state.m_quatOrientation = pPT->orientation;
  state.m_NearZ = pPT->nearZ;
  state.m_FarZ = pPT->farZ;
  state.m_fHorizontalFOVDegrees = state.m_fVerticalFOVDegrees = 90.0f;
  state.m_fQuadraticAtten = state.m_fConstantAtten = 0.0f;
  state.m_fLinearAtten = 100.0f;
  state.m_Color[0] = state.m_Color[1] = state.m_Color[2] = state.m_Color[3] = 1.0f;
  state.m_pSpotlightTexture = materials->FindTexture( "effects/flashlight001", TEXTURE_GROUP_OTHER, false );
  state.m_nSpotlightTextureFrame = 0;
  state.m_bEnableShadows = pPT->enableShadows;
  state.m_flShadowMapResolution = pPT->shadowMapResolution;
  state.m_flShadowFilterSize = pPT->shadowFilter;
  state.m_flShadowSlopeScaleDepthBias = pPT->shadowSlopeScaleDepthBias;
  state.m_flShadowDepthBias = pPT->shadowDepthBias;
  state.m_nShadowQuality = 1;
  if ( g_pClientShadowMgr ) pPT->handle = g_pClientShadowMgr->CreateFlashlight( state );
  pPT->alive = pPT->handle != CLIENTSHADOW_INVALID_HANDLE;
  s_ProjectedTextures.AddToTail( pPT );

  luaL_getmetatable( L, HL2SB_PROJECTEDTEXTURE_METATABLE );
  lua_setmetatable( L, -2 );
  return 1;
}

// -- position / orientation ------------------------------------------------

static int HL2SB_ProjectedTexture_SetPos( lua_State *L )
{
  HL2SB_ProjectedTexture_Check( L, 1 )->origin = luaL_checkvector( L, 2 );
  return 0;
}

static int HL2SB_ProjectedTexture_GetPos( lua_State *L )
{
  lua_pushvector( L, HL2SB_ProjectedTexture_Check( L, 1 )->origin );
  return 1;
}

static int HL2SB_ProjectedTexture_SetAngles( lua_State *L )
{
  AngleQuaternion( luaL_checkangle( L, 2 ), HL2SB_ProjectedTexture_Check( L, 1 )->orientation );
  return 0;
}

static int HL2SB_ProjectedTexture_GetAngles( lua_State *L )
{
  QAngle angles;
  QuaternionAngles( HL2SB_ProjectedTexture_Check( L, 1 )->orientation, angles );
  lua_pushangle( L, angles );
  return 1;
}

// -- field of view ---------------------------------------------------------

static int HL2SB_ProjectedTexture_SetFOV( lua_State *L )
{
  HL2SB_ProjectedTexture_t *pPT = HL2SB_ProjectedTexture_Check( L, 1 );
  const float fov = (float)luaL_checknumber( L, 2 );
  pPT->fovH = fov;   // GMod's SetFOV writes both axes.
  pPT->fovV = fov;
  return 0;
}

static int HL2SB_ProjectedTexture_SetHorizontalFOV( lua_State *L )
{
  HL2SB_ProjectedTexture_Check( L, 1 )->fovH = (float)luaL_checknumber( L, 2 );
  return 0;
}

static int HL2SB_ProjectedTexture_GetHorizontalFOV( lua_State *L )
{
  lua_pushnumber( L, HL2SB_ProjectedTexture_Check( L, 1 )->fovH );
  return 1;
}

static int HL2SB_ProjectedTexture_SetVerticalFOV( lua_State *L )
{
  HL2SB_ProjectedTexture_Check( L, 1 )->fovV = (float)luaL_checknumber( L, 2 );
  return 0;
}

static int HL2SB_ProjectedTexture_GetVerticalFOV( lua_State *L )
{
  lua_pushnumber( L, HL2SB_ProjectedTexture_Check( L, 1 )->fovV );
  return 1;
}

// -- colour / brightness ---------------------------------------------------

static int HL2SB_ProjectedTexture_SetBrightness( lua_State *L )
{
  HL2SB_ProjectedTexture_Check( L, 1 )->brightness = (float)luaL_checknumber( L, 2 );
  return 0;
}

static int HL2SB_ProjectedTexture_GetBrightness( lua_State *L )
{
  lua_pushnumber( L, HL2SB_ProjectedTexture_Check( L, 1 )->brightness );
  return 1;
}

static int HL2SB_ProjectedTexture_SetColor( lua_State *L )
{
  HL2SB_ProjectedTexture_t *p = HL2SB_ProjectedTexture_Check( L, 1 );
  if ( !lua_istable( L, 2 ) )
  {
    p->color.SetColor( 255, 255, 255, 255 );
    return 0;
  }
  int values[4] = { 255, 255, 255, 255 };
  const char *fields[4] = { "r", "g", "b", "a" };
  for ( int i = 3; i >= 0; --i )
  {
    lua_getfield( L, 2, fields[i] );
    if ( lua_type( L, -1 ) == LUA_TNUMBER )
    {
      double value = lua_tonumber( L, -1 );
      values[i] = ( value >= -2147483648.0 && value < 2147483648.0 )
        ? (int)value : (-2147483647 - 1);
    }
    lua_pop( L, 1 );
  }
  p->color.SetColor( values[0], values[1], values[2], values[3] );
  return 0;
}

static int HL2SB_ProjectedTexture_GetColor( lua_State *L )
{
  lua_pushcolor( L, HL2SB_ProjectedTexture_Check( L, 1 )->color );
  return 1;
}

// -- texture ---------------------------------------------------------------

static int HL2SB_ProjectedTexture_SetTexture( lua_State *L )
{
  HL2SB_ProjectedTexture_t *pPT = HL2SB_ProjectedTexture_Check( L, 1 );

  if ( lua_type( L, 2 ) == LUA_TSTRING )
  {
    ITexture *pTexture = materials->FindTexture( luaL_checkstring( L, 2 ), TEXTURE_GROUP_OTHER, false );
    if ( pTexture ) pTexture->AddRef();
    if ( pPT->pTexture ) pPT->pTexture->Release();
    pPT->pTexture = pTexture;
  }
  else
  {
    // GMod also accepts an ITexture object; keep a reference so the pointer
    // outlives material-system reloads between the setter and the next Update.
    ITexture *pTexture = luaL_checkitexture( L, 2 );
    if ( pTexture != pPT->pTexture )
    {
      if ( pPT->pTexture != NULL )
        pPT->pTexture->Release();
      pTexture->AddRef();
      pPT->pTexture = pTexture;
    }
  }
  pPT->textureFrame = 0;
  return 0;
}

static int HL2SB_ProjectedTexture_GetTexture( lua_State *L )
{
  HL2SB_ProjectedTexture_t *pPT = HL2SB_ProjectedTexture_Check( L, 1 );
  if ( pPT->pTexture ) lua_pushitexture( L, pPT->pTexture );
  else lua_pushnil( L );
  return 1;
}

static int HL2SB_ProjectedTexture_SetTextureFrame( lua_State *L )
{
  HL2SB_ProjectedTexture_t *p = HL2SB_ProjectedTexture_Check( L, 1 );
  p->textureFrame = (int)luaL_checknumber( L, 2 );
  if ( p->pTexture && p->pTexture->IsError() ) p->textureFrame = 0;
  return 0;
}

static int HL2SB_ProjectedTexture_GetTextureFrame( lua_State *L )
{
  lua_pushinteger( L, HL2SB_ProjectedTexture_Check( L, 1 )->textureFrame );
  return 1;
}

// -- shadows / range / attenuation / world light ---------------------------

static int HL2SB_ProjectedTexture_SetEnableShadows( lua_State *L )
{
  HL2SB_ProjectedTexture_Check( L, 1 )->enableShadows = lua_toboolean( L, 2 ) && r_flashlightdepthtexture.GetBool();
  return 0;
}

static int HL2SB_ProjectedTexture_GetEnableShadows( lua_State *L )
{
  lua_pushboolean( L, HL2SB_ProjectedTexture_Check( L, 1 )->enableShadows );
  return 1;
}

// The two bias fields always existed in the state struct and were always
// pushed in Apply, but no Lua setter ever reached them -- the reference
// binding exposes both (aligned 2026-10-13).
static int HL2SB_ProjectedTexture_SetShadowDepthBias( lua_State *L )
{
  HL2SB_ProjectedTexture_Check( L, 1 )->shadowDepthBias = (float)luaL_checknumber( L, 2 );
  return 0;
}

static int HL2SB_ProjectedTexture_GetShadowDepthBias( lua_State *L )
{
  lua_pushnumber( L, HL2SB_ProjectedTexture_Check( L, 1 )->shadowDepthBias );
  return 1;
}

static int HL2SB_ProjectedTexture_SetShadowSlopeScaleDepthBias( lua_State *L )
{
  HL2SB_ProjectedTexture_Check( L, 1 )->shadowSlopeScaleDepthBias = (float)luaL_checknumber( L, 2 );
  return 0;
}

static int HL2SB_ProjectedTexture_GetShadowSlopeScaleDepthBias( lua_State *L )
{
  lua_pushnumber( L, HL2SB_ProjectedTexture_Check( L, 1 )->shadowSlopeScaleDepthBias );
  return 1;
}

static int HL2SB_ProjectedTexture_SetNearZ( lua_State *L )
{
  HL2SB_ProjectedTexture_Check( L, 1 )->nearZ = (float)luaL_checknumber( L, 2 );
  return 0;
}

static int HL2SB_ProjectedTexture_GetNearZ( lua_State *L )
{
  lua_pushnumber( L, HL2SB_ProjectedTexture_Check( L, 1 )->nearZ );
  return 1;
}

static int HL2SB_ProjectedTexture_SetFarZ( lua_State *L )
{
  HL2SB_ProjectedTexture_Check( L, 1 )->farZ = (float)luaL_checknumber( L, 2 );
  return 0;
}

static int HL2SB_ProjectedTexture_GetFarZ( lua_State *L )
{
  lua_pushnumber( L, HL2SB_ProjectedTexture_Check( L, 1 )->farZ );
  return 1;
}

static int HL2SB_ProjectedTexture_SetQuadraticAttenuation( lua_State *L )
{
  HL2SB_ProjectedTexture_Check( L, 1 )->quadraticAtten = (float)luaL_checknumber( L, 2 );
  return 0;
}

static int HL2SB_ProjectedTexture_GetQuadraticAttenuation( lua_State *L )
{
  lua_pushnumber( L, HL2SB_ProjectedTexture_Check( L, 1 )->quadraticAtten );
  return 1;
}

static int HL2SB_ProjectedTexture_SetLinearAttenuation( lua_State *L )
{
  HL2SB_ProjectedTexture_Check( L, 1 )->linearAtten = (float)luaL_checknumber( L, 2 );
  return 0;
}

static int HL2SB_ProjectedTexture_GetLinearAttenuation( lua_State *L )
{
  lua_pushnumber( L, HL2SB_ProjectedTexture_Check( L, 1 )->linearAtten );
  return 1;
}

static int HL2SB_ProjectedTexture_SetConstantAttenuation( lua_State *L )
{
  HL2SB_ProjectedTexture_Check( L, 1 )->constantAtten = (float)luaL_checknumber( L, 2 );
  return 0;
}

static int HL2SB_ProjectedTexture_GetConstantAttenuation( lua_State *L )
{
  lua_pushnumber( L, HL2SB_ProjectedTexture_Check( L, 1 )->constantAtten );
  return 1;
}

static int HL2SB_ProjectedTexture_SetLightWorld( lua_State *L )
{
  HL2SB_ProjectedTexture_Check( L, 1 )->lightWorld = lua_toboolean( L, 2 ) != 0;
  return 0;
}

static int HL2SB_ProjectedTexture_GetLightWorld( lua_State *L )
{
  lua_pushboolean( L, HL2SB_ProjectedTexture_Check( L, 1 )->lightWorld );
  return 1;
}

static int HL2SB_ProjectedTexture_SetTargetEntity( lua_State *L )
{
  HL2SB_ProjectedTexture_t *p = HL2SB_ProjectedTexture_Check( L, 1 );
  if ( lua_isnoneornil( L, 2 ) )
  {
    p->target = NULL;
    return 0;
  }
  bool isEntity = false;
  if ( lua_type( L, 2 ) == LUA_TUSERDATA && lua_getmetatable( L, 2 ) )
  {
    lua_pushliteral( L, "MetaID" );
    lua_rawget( L, -2 );
    isEntity = lua_isnumber( L, -1 ) && lua_tointeger( L, -1 ) == 9;
    lua_pop( L, 2 );
  }
  if ( !isEntity ) return luaL_argerror( L, 2, "Entity expected" );
  p->target = lua_toentity( L, 2 );
  return 0;
}

static int HL2SB_ProjectedTexture_GetTargetEntity( lua_State *L )
{
  CBaseEntity *target = HL2SB_ProjectedTexture_Check( L, 1 )->target.Get();
  if ( target )
    lua_pushentity( L, target );
  else
  {
    lua_getglobal( L, "NULL" );
    if ( lua_isnil( L, -1 ) )
    {
      lua_pop( L, 1 );
      lua_pushentity( L, NULL );
    }
  }
  return 1;
}

static int HL2SB_ProjectedTexture_SetShadowFilter( lua_State *L )
{
  HL2SB_ProjectedTexture_Check( L, 1 )->shadowFilter = (float)luaL_checknumber( L, 2 );
  return 0;
}

static int HL2SB_ProjectedTexture_GetShadowFilter( lua_State *L )
{
  lua_pushnumber( L, HL2SB_ProjectedTexture_Check( L, 1 )->shadowFilter );
  return 1;
}

static int HL2SB_ProjectedTexture_SetOrthographic( lua_State *L )
{
  HL2SB_ProjectedTexture_t *p = HL2SB_ProjectedTexture_Check( L, 1 );
  float left = 0, top = 0, right = 0, bottom = 0;
  if ( lua_gettop( L ) > 2 )
  {
    bottom = (float)luaL_checknumber( L, 6 );
    right = (float)luaL_checknumber( L, 5 );
    top = (float)luaL_checknumber( L, 4 );
    left = (float)luaL_checknumber( L, 3 );
  }
  p->ortho = lua_toboolean( L, 2 ) != 0;
  if ( p->ortho )
  {
    p->orthoLeft = -(float)fabs( left );
    p->orthoTop = -(float)fabs( top );
    p->orthoRight = (float)fabs( right );
    p->orthoBottom = (float)fabs( bottom );
  }
  return 0;
}

static int HL2SB_ProjectedTexture_GetOrthographic( lua_State *L )
{
  HL2SB_ProjectedTexture_t *p = HL2SB_ProjectedTexture_Check( L, 1 );
  lua_pushboolean( L, p->ortho );
  if ( !p->ortho ) return 1;
  lua_pushnumber( L, -p->orthoLeft );
  lua_pushnumber( L, -p->orthoTop );
  lua_pushnumber( L, p->orthoRight );
  lua_pushnumber( L, p->orthoBottom );
  return 5;
}

#define PROJECTED_BOOL_ACCESSORS(Name, Field) \
static int HL2SB_ProjectedTexture_Set##Name( lua_State *L ) { HL2SB_ProjectedTexture_Check( L, 1 )->Field = lua_toboolean( L, 2 ) != 0; return 0; } \
static int HL2SB_ProjectedTexture_Get##Name( lua_State *L ) { lua_pushboolean( L, HL2SB_ProjectedTexture_Check( L, 1 )->Field ); return 1; }
PROJECTED_BOOL_ACCESSORS( NoCull, noCull )
PROJECTED_BOOL_ACCESSORS( SkipShadowUpdates, skipShadowUpdates )
#undef PROJECTED_BOOL_ACCESSORS

// -- lifecycle -------------------------------------------------------------

static int HL2SB_ProjectedTexture_Update( lua_State *L )
{
  HL2SB_ProjectedTexture_Apply( HL2SB_ProjectedTexture_Check( L, 1 ) );
  return 0;
}

static int HL2SB_ProjectedTexture_Remove( lua_State *L )
{
  HL2SB_ProjectedTexture_Destroy( HL2SB_ProjectedTexture_Check( L, 1 ) );
  return 0;
}

static int HL2SB_ProjectedTexture_IsValid( lua_State *L )
{
  lua_pushboolean( L, HL2SB_ProjectedTexture_Check( L, 1 )->alive );
  return 1;
}

static int HL2SB_ProjectedTexture_GC( lua_State *L )
{
  HL2SB_ProjectedTexture_t *p = HL2SB_ProjectedTexture_Check( L, 1 );
  if ( s_ProjectedTextures.Find( p ) == s_ProjectedTextures.InvalidIndex() ) return 0;
  HL2SB_ProjectedTexture_Destroy( p );
  s_ProjectedTextures.FindAndRemove( p );
  p->~HL2SB_ProjectedTexture_t();
  return 0;
}

static int HL2SB_ProjectedTexture_ToString( lua_State *L )
{
  lua_pushliteral( L, "ProjectedTexture" );
  return 1;
}

static const luaL_Reg projectedtexturelib[] = {
  {"SetPos", HL2SB_ProjectedTexture_SetPos},
  {"GetPos", HL2SB_ProjectedTexture_GetPos},
  {"SetAngles", HL2SB_ProjectedTexture_SetAngles},
  {"GetAngles", HL2SB_ProjectedTexture_GetAngles},
  {"SetFOV", HL2SB_ProjectedTexture_SetFOV},
  {"SetHorizontalFOV", HL2SB_ProjectedTexture_SetHorizontalFOV},
  {"GetHorizontalFOV", HL2SB_ProjectedTexture_GetHorizontalFOV},
  {"SetVerticalFOV", HL2SB_ProjectedTexture_SetVerticalFOV},
  {"GetVerticalFOV", HL2SB_ProjectedTexture_GetVerticalFOV},
  {"SetBrightness", HL2SB_ProjectedTexture_SetBrightness},
  {"GetBrightness", HL2SB_ProjectedTexture_GetBrightness},
  {"SetColor", HL2SB_ProjectedTexture_SetColor},
  {"GetColor", HL2SB_ProjectedTexture_GetColor},
  {"SetTexture", HL2SB_ProjectedTexture_SetTexture},
  {"GetTexture", HL2SB_ProjectedTexture_GetTexture},
  {"SetTextureFrame", HL2SB_ProjectedTexture_SetTextureFrame},
  {"GetTextureFrame", HL2SB_ProjectedTexture_GetTextureFrame},
  {"SetEnableShadows", HL2SB_ProjectedTexture_SetEnableShadows},
  {"GetEnableShadows", HL2SB_ProjectedTexture_GetEnableShadows},
  {"SetShadowDepthBias", HL2SB_ProjectedTexture_SetShadowDepthBias},
  {"GetShadowDepthBias", HL2SB_ProjectedTexture_GetShadowDepthBias},
  {"SetShadowSlopeScaleDepthBias", HL2SB_ProjectedTexture_SetShadowSlopeScaleDepthBias},
  {"GetShadowSlopeScaleDepthBias", HL2SB_ProjectedTexture_GetShadowSlopeScaleDepthBias},
  {"SetNearZ", HL2SB_ProjectedTexture_SetNearZ},
  {"GetNearZ", HL2SB_ProjectedTexture_GetNearZ},
  {"SetFarZ", HL2SB_ProjectedTexture_SetFarZ},
  {"GetFarZ", HL2SB_ProjectedTexture_GetFarZ},
  {"SetQuadraticAttenuation", HL2SB_ProjectedTexture_SetQuadraticAttenuation},
  {"GetQuadraticAttenuation", HL2SB_ProjectedTexture_GetQuadraticAttenuation},
  {"SetLinearAttenuation", HL2SB_ProjectedTexture_SetLinearAttenuation},
  {"GetLinearAttenuation", HL2SB_ProjectedTexture_GetLinearAttenuation},
  {"SetConstantAttenuation", HL2SB_ProjectedTexture_SetConstantAttenuation},
  {"GetConstantAttenuation", HL2SB_ProjectedTexture_GetConstantAttenuation},
  {"SetLightWorld", HL2SB_ProjectedTexture_SetLightWorld},
  {"GetLightWorld", HL2SB_ProjectedTexture_GetLightWorld},
  {"SetTargetEntity", HL2SB_ProjectedTexture_SetTargetEntity},
  {"GetTargetEntity", HL2SB_ProjectedTexture_GetTargetEntity},
  {"SetShadowFilter", HL2SB_ProjectedTexture_SetShadowFilter},
  {"GetShadowFilter", HL2SB_ProjectedTexture_GetShadowFilter},
  {"SetOrthographic", HL2SB_ProjectedTexture_SetOrthographic},
  {"GetOrthographic", HL2SB_ProjectedTexture_GetOrthographic},
  {"SetNoCull", HL2SB_ProjectedTexture_SetNoCull},
  {"GetNoCull", HL2SB_ProjectedTexture_GetNoCull},
  {"SetSkipShadowUpdates", HL2SB_ProjectedTexture_SetSkipShadowUpdates},
  {"GetSkipShadowUpdates", HL2SB_ProjectedTexture_GetSkipShadowUpdates},
  {"Update", HL2SB_ProjectedTexture_Update},
  {"Remove", HL2SB_ProjectedTexture_Remove},
  {"IsValid", HL2SB_ProjectedTexture_IsValid},
  {"__tostring", HL2SB_ProjectedTexture_ToString},
  {NULL, NULL}
};


/*
** Open g_pClientShadowMgr library
*/
LUALIB_API int luaopen_g_pClientShadowMgr (lua_State *L) {
  luaL_register(L, "g_pClientShadowMgr", g_pClientShadowMgrlib);

  // HL2SB: the ProjectedTexture metatable + global constructor (client-only
  // state, same list entry that has always opened this library).
  luaL_newmetatable(L, HL2SB_PROJECTEDTEXTURE_METATABLE);
  lua_pushvalue(L, -1);
  lua_setfield(L, -2, "__index");   // methods resolve through the metatable
  luaL_register(L, NULL, projectedtexturelib);
  lua_pushcfunction(L, HL2SB_ProjectedTexture_GC);
  lua_setfield(L, -2, "__gc");
  lua_pushstring(L, HL2SB_PROJECTEDTEXTURE_METATABLE);
  lua_setfield(L, -2, "__type");    // type( p ) returns "ProjectedTexture"
  lua_pop(L, 1);

  lua_pushcfunction(L, HL2SB_ProjectedTexture_Create);
  lua_setglobal(L, "ProjectedTexture");

  return 1;
}

