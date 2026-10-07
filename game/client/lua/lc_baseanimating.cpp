//===== Copy	right © 1996-2005, Valve Corporation, All rights reserved. ==//
//
// Purpose: 
//
// $NoKeywords: $
//===========================================================================//
#define lc_baseanimating_cpp

#include "cbase.h"
#include "luamanager.h"
#include "luasrclib.h"
#include "lc_baseanimating.h"
#include "lbaseentity_shared.h"
#include "lbaseplayer_shared.h"
#include "basescripted.h"	// HL2SB (2026-10-04): pre-bind write diagnostic reads C_BaseScripted::GetScriptedClassname
#include "mathlib/lvector.h"
#include "model_types.h"	// HL2SB: STUDIO_RENDER, the default of Entity:DrawModel()
#include "lvphysics_interface.h"
#include "mathlib/lvmatrix.h"
#include "animation.h"	// HL2SB (2026-10-03): GetNumBodyGroups for Entity:GetBodygroups	// HL2SB: lua_pushvmatrix for Entity:GetBoneMatrix()
#include "studio.h"			// HL2SB: studiohdr_t for Entity:GetBoneCount()
#include "c_baseflex.h"		// HL2SB: C_BaseFlex flex weights (GetFlexWeight/SetFlexWeight)

// memdbgon must be the last include file in a .cpp file!!!
#include "tier0/memdbgon.h"

// ===== HL2SB GMod compat (2026-09-24): Entity callbacks =====================
// GMod lets addons attach per-entity callbacks via Entity:AddCallback(name,
// fn) / RemoveCallback(name, id) / GetCallbacks(name).  The First Person Body
// addon drives its whole bone mirroring through
// Entity:AddCallback( "BuildBonePositions", fn ).
//
// This fork dispatches exactly one callback name: "BuildBonePositions",
// fired from C_BaseAnimating::SetupBones (c_baseanimating.cpp) right after
// BuildTransformations filled the live bone accessor - so callbacks read the
// bones with GetBoneMatrix and rewrite them with SetBoneMatrix, which is the
// GMod contract.  Unknown names return nothing from AddCallback, mirroring
// GMod's behaviour for a non-existent callback.
struct HL2SB_EntityCallback_t
{
	int nID;
	char szName[48];
	int nRef;
};

struct HL2SB_EntityCallbackList_t
{
	CBaseHandle hEntity;
	CUtlVector< HL2SB_EntityCallback_t > items;
};

static CUtlVector< HL2SB_EntityCallbackList_t * > s_EntityCallbacks;
static int s_nNextEntityCallbackID = 1;

static HL2SB_EntityCallbackList_t *HL2SB_FindCallbackList( C_BaseAnimating *pEntity, bool bCreate )
{
	if ( pEntity == NULL )
		return NULL;
	const CBaseHandle h = pEntity->GetRefEHandle();
	for ( int i = 0; i < s_EntityCallbacks.Count(); ++i )
	{
		if ( s_EntityCallbacks[i]->hEntity == h )
			return s_EntityCallbacks[i];
	}
	if ( !bCreate )
		return NULL;
	HL2SB_EntityCallbackList_t *pList = new HL2SB_EntityCallbackList_t;
	pList->hEntity = h;
	s_EntityCallbacks.AddToTail( pList );
	return pList;
}

// Stale lists (entity destroyed since) are dropped the next time an addon
// registers a callback and the registry grew past this watermark.
static void HL2SB_PurgeDeadCallbackLists()
{
	for ( int i = s_EntityCallbacks.Count() - 1; i >= 0; --i )
	{
		if ( s_EntityCallbacks[i]->hEntity.Get() != NULL )
			continue;
		for ( int j = 0; j < s_EntityCallbacks[i]->items.Count(); ++j )
			luaL_unref( L, LUA_REGISTRYINDEX, s_EntityCallbacks[i]->items[j].nRef );
		delete s_EntityCallbacks[i];
		s_EntityCallbacks.FastRemove( i );
	}
}

// HL2SB (2026-09-24 crash): every bone/flex/pose binding resolves studio data
// through this guard.  GetStudiomodel on a NULL or non-studio model_t is the
// near-null deref the dump caught (read at 0x20 inside engine.dll); sprites,
// brush models and the NULL model are simply answered with NULL here.
static studiohdr_t *HL2SB_GetStudioHdrSafe( const C_BaseAnimating *pEntity )
{
	if ( pEntity == NULL )
		return NULL;
	const model_t *pModel = pEntity->GetModel();
	if ( !pModel || modelinfo->GetModelType( pModel ) != mod_studio )
		return NULL;
	return modelinfo->GetStudiomodel( pModel );
}

// Called from C_BaseAnimating::SetupBones via a local extern declaration
// (AGENTS.md: avoid touching headers - waf does not propagate them).
static bool s_bInEntityCallbacks = false;

void HL2SB_RunEntityCallbacks( C_BaseAnimating *pEntity, const char *pszName )
{
	// HL2SB (2026-09-24 crash): the dispatch MUST be re-entrancy guarded.  The
	// First Person Body callback calls Entity:SetupBones / GetBoneMatrix on
	// the body INSIDE the callback, i.e. inside a SetupBones rebuild that is
	// still in flight - readable-bone flags are only set when the rebuild
	// FINISHES, so the re-entrant call started ANOTHER full rebuild, fired the
	// callback again, and the cycle recursed until the corrupted walk died in
	// engine.dll!CModelInfo::GetStudiomodel (engine.log: the same 7 client.dll
	// frames repeating 4x).  One level only, same rule as GMod's own dispatch.
	// HL2SB (2026-10-03): hoisted to file scope - the bone-manipulation
	// immediate write consults it to recognise callback context.
	if ( s_bInEntityCallbacks || L == NULL || pEntity == NULL || s_EntityCallbacks.Count() == 0 )
		return;

	HL2SB_EntityCallbackList_t *pList = HL2SB_FindCallbackList( pEntity, false );
	if ( pList == NULL || pList->items.Count() == 0 )
		return;

	const model_t *pModel = pEntity->GetModel();
	if ( !pModel || modelinfo->GetModelType( pModel ) != mod_studio )
		return;
	studiohdr_t *pHdr = modelinfo->GetStudiomodel( pModel );
	const int nBones = pHdr ? pHdr->numbones : 0;

	s_bInEntityCallbacks = true;

	for ( int i = 0; i < pList->items.Count(); ++i )
	{
		HL2SB_EntityCallback_t &cb = pList->items[i];
		if ( Q_stricmp( cb.szName, pszName ) )
			continue;
		lua_getref( L, cb.nRef );
		if ( !lua_isfunction( L, -1 ) )
		{
			lua_pop( L, 1 );
			continue;
		}
		lua_pushanimating( L, pEntity );
		lua_pushinteger( L, nBones );
		luasrc_pcall( L, 2, 0, 0 );
	}

	s_bInEntityCallbacks = false;
}

//-----------------------------------------------------------------------------
// HL2SB GMod compat (2026-10-03): Entity:ManipulateBonePosition / ManipulateBoneScale.
// Reference behaviour: the offset/scale is stored per (entity, bone) and applied
// on every bone setup; the optional 4th boolean (default true) also applies it
// to the live bone array immediately.  A side table keyed by EHANDLE keeps
// C_BaseAnimating's layout untouched (same pattern as the callback list above).
// Position offsets are added to the bone matrix translation in world space and
// the scale multiplies the matrix basis - that is what the observed reference
// consumers need (hiding a bone far away / zeroing it), and it is what the
// read side (GetBoneMatrix) hands back afterwards.
//-----------------------------------------------------------------------------
struct HL2SB_BoneManip_t
{
	EHANDLE hEntity;
	int nBone;
	Vector vecPos;
	Vector vecScale;
	QAngle angAngles;
	bool bHasPos;
	bool bHasScale;
	bool bHasAngles;
};

static CUtlVector< HL2SB_BoneManip_t > s_BoneManips;

static HL2SB_BoneManip_t *HL2SB_FindBoneManip( C_BaseAnimating *pEntity, int nBone, bool bCreate )
{
	for ( int i = 0; i < s_BoneManips.Count(); ++i )
	{
		if ( s_BoneManips[i].hEntity.Get() == pEntity && s_BoneManips[i].nBone == nBone )
			return &s_BoneManips[i];
	}
	if ( !bCreate )
		return NULL;
	HL2SB_BoneManip_t &manip = s_BoneManips[s_BoneManips.AddToTail()];
	manip.hEntity = pEntity;
	manip.nBone = nBone;
	manip.vecPos = vec3_origin;
	manip.vecScale = vec3_origin;
	manip.angAngles = vec3_angle;
	manip.bHasPos = false;
	manip.bHasScale = false;
	manip.bHasAngles = false;
	return &manip;
}

// bBonesReady: called from the C_BaseAnimating::SetupBones tail, where the
// bone array under construction is already allocated and filled - the write
// happens in place and NO SetupBones call may happen here: the tail itself
// lives inside SetupBones, so calling it again is unbounded recursion (the
// 1MB-stack-eating crash when the minecraft SWEP manipulated every bone).
// The !bBonesReady path serves the immediate write from the Lua bindings.
static void HL2SB_ApplyBoneManipEntry( C_BaseAnimating *pEntity, HL2SB_BoneManip_t &manip, bool bBonesReady )
{
	studiohdr_t *pHdr = HL2SB_GetStudioHdrSafe( pEntity );
	if ( pHdr == NULL || manip.nBone < 0 || manip.nBone >= pHdr->numbones )
		return;

	if ( !bBonesReady )
	{
		// Outside bone setup (a Lua ManipulateBone* call): re-run a full
		// SetupBones -- its tail replays every stored manipulation onto the
		// freshly rebuilt array, so each call takes effect and the relative
		// offset can never double-apply.  (The old per-frame stamp skipped
		// same-frame replays AND same-frame Lua calls; predicted players
		// invalidate their bone cache several times a frame in multiplayer,
		// which dropped the manipulation and left the FPB head visible.)
		if ( !pEntity->IsEFlagSet( EFL_SETTING_UP_BONES ) && !s_bInEntityCallbacks )
		{
			// Force a rebuild even when the bone cache believes it is current --
			// otherwise a second ManipulateBone* call in the same frame would be
			// short-circuited by the cache and the new value never applied.
			pEntity->InvalidateBoneCache();
			pEntity->SetupBones( NULL, 0, BONE_USED_BY_ANYTHING, gpGlobals->curtime );
			// the tail replay above applied this entry already
			return;
		}

		// Mid-setup (a BuildBonePositions callback manipulating its own
		// bones): the array is already being written, fall through to the
		// in-place write like the reference behaviour does.
		// s_bInEntityCallbacks covers the BuildBonePositions callback (the
		// EFL is already cleared by the time the tail runs).
	}

	matrix3x4_t &bone = pEntity->GetBoneForWrite( manip.nBone );
	if ( manip.bHasPos )
	{
		Vector vTranslation;
		MatrixGetTranslation( bone, vTranslation );
		vTranslation += manip.vecPos;
		MatrixSetTranslation( vTranslation, bone );
	}
	if ( manip.bHasScale )
	{
		MatrixScaleBy( manip.vecScale.x, bone );
	}
	if ( manip.bHasAngles )
	{
		// Rotate the basis around the bone origin; the translation column is
		// kept so a ManipulateBonePosition offset and a rotation compose
		// independently of write order.
		matrix3x4_t rot, out;
		AngleMatrix( manip.angAngles, rot );
		Vector vTranslation;
		MatrixGetTranslation( bone, vTranslation );
		ConcatTransforms( rot, bone, out );
		MatrixSetTranslation( vTranslation, out );
		MatrixCopy( out, bone );
	}

}

// Called from C_BaseAnimating::SetupBones right before the BuildBonePositions
// callbacks, so manipulations survive every rebuild and Lua callbacks observe
// the manipulated bones.  Dead-entity entries are swept here too.
void HL2SB_ApplyBoneManipulations( C_BaseAnimating *pEntity )
{
	if ( L == NULL || pEntity == NULL )
		return;
	for ( int i = s_BoneManips.Count() - 1; i >= 0; --i )
	{
		if ( s_BoneManips[i].hEntity.Get() == NULL )
		{
			s_BoneManips.FastRemove( i );
			continue;
		}
		if ( s_BoneManips[i].hEntity == pEntity )
			HL2SB_ApplyBoneManipEntry( pEntity, s_BoneManips[i], true );
	}
}

//-----------------------------------------------------------------------------
// HL2SB GMod compat (2026-10-03): Entity:SetRenderClipPlaneEnabled( bool ) /
// Entity:SetRenderClipPlane( vec normal, float dist ).  Reference behaviour:
// per-entity clip plane storage the renderer consults while drawing that
// entity; the confirmed engine consumer is the shadow pass, and the addon
// consumer that needs it visible is the portalgun's player clone, whose
// raster must be cut at the portal plane so the clone does not bleed through
// walls.  EHANDLE-keyed side table (class layout untouched); the plane is
// applied around CBaseScripted::DrawModel via the getter below.
//-----------------------------------------------------------------------------
struct HL2SB_RenderClipPlane_t
{
	EHANDLE hEntity;
	Vector vecNormal;
	float flDist;
	bool bEnabled;
};

static CUtlVector< HL2SB_RenderClipPlane_t > s_RenderClipPlanes;

static HL2SB_RenderClipPlane_t *HL2SB_FindRenderClipPlane( C_BaseEntity *pEntity, bool bCreate )
{
	for ( int i = 0; i < s_RenderClipPlanes.Count(); ++i )
	{
		if ( s_RenderClipPlanes[i].hEntity.Get() == pEntity )
			return &s_RenderClipPlanes[i];
	}
	if ( !bCreate )
		return NULL;
	HL2SB_RenderClipPlane_t &plane = s_RenderClipPlanes[s_RenderClipPlanes.AddToTail()];
	plane.hEntity = pEntity;
	plane.vecNormal = vec3_origin;
	plane.flDist = 0.0f;
	plane.bEnabled = false;
	return &plane;
}

// Consumed by CBaseScripted::DrawModel: answers whether the entity draws
// behind a clip plane, and sweeps dead entries while it is at it.
bool HL2SB_GetEntityRenderClipPlane( C_BaseEntity *pEntity, Vector &outNormal, float &outDist )
{
	for ( int i = s_RenderClipPlanes.Count() - 1; i >= 0; --i )
	{
		if ( s_RenderClipPlanes[i].hEntity.Get() == NULL )
		{
			s_RenderClipPlanes.FastRemove( i );
			continue;
		}
		if ( s_RenderClipPlanes[i].hEntity.Get() == pEntity && s_RenderClipPlanes[i].bEnabled )
		{
			outNormal = s_RenderClipPlanes[i].vecNormal;
			outDist = s_RenderClipPlanes[i].flDist;
			return true;
		}
	}
	return false;
}

static int CBaseAnimating_SetRenderClipPlaneEnabled (lua_State *L) {
  C_BaseAnimating *pEntity = luaL_checkanimating(L, 1);
  HL2SB_RenderClipPlane_t *pPlane = HL2SB_FindRenderClipPlane( pEntity, lua_toboolean(L, 2) != 0 );
  if ( pPlane != NULL )
    pPlane->bEnabled = lua_toboolean(L, 2) != 0;
  return 0;
}

static int CBaseAnimating_SetRenderClipPlane (lua_State *L) {
  C_BaseAnimating *pEntity = luaL_checkanimating(L, 1);
  const Vector &vecNormal = luaL_checkvector(L, 2);
  float flDist = (float)luaL_checknumber(L, 3);
  HL2SB_RenderClipPlane_t *pPlane = HL2SB_FindRenderClipPlane( pEntity, true );
  pPlane->vecNormal = vecNormal;
  pPlane->flDist = flDist;
  return 0;
}

//-----------------------------------------------------------------------------
// HL2SB GMod compat (2026-10-03): the RenderOverride Lua field.  Reference
// behaviour: an entity whose Lua table carries a RenderOverride function draws
// through that function INSTEAD of the engine path; the function receives the
// entity and the studio draw flags (STUDIO_RENDER / shadow-depth / SSAO bits -
// how reference addons gate their projected-shadow copies).  Scripted entities
// and nextbots override DrawModel themselves and never reach the base version,
// so a plain field check here cannot double-dispatch.  Entity:DrawModel (the
// binding) uses InternalDrawModel directly and does not re-enter this.
//-----------------------------------------------------------------------------
bool HL2SB_RunRenderOverride( C_BaseAnimating *pEntity, int nFlags )
{
	if ( L == NULL || pEntity == NULL )
		return false;
	if ( !lua_isrefvalid( L, pEntity->m_nTableReference ) )
		return false;

	lua_getref( L, pEntity->m_nTableReference );
	if ( !lua_istable( L, -1 ) )
	{
		lua_pop( L, 1 );
		return false;
	}
	lua_getfield( L, -1, "RenderOverride" );
	lua_remove( L, -2 );
	if ( !lua_isfunction( L, -1 ) )
	{
		lua_pop( L, 1 );
		return false;
	}

	lua_pushanimating( L, pEntity );
	lua_pushinteger( L, nFlags );
	luasrc_pcall( L, 2, 0, 0 );
	return true;
}

/*
** access functions (stack -> C)
*/


LUA_API lua_CBaseAnimating *lua_toanimating (lua_State *L, int idx) {
  CBaseHandle *hEntity = dynamic_cast<CBaseHandle *>((CBaseHandle *)lua_touserdata(L, idx));
  if (hEntity == NULL)
    return NULL;
  return dynamic_cast<lua_CBaseAnimating *>(hEntity->Get());
}



/*
** push functions (C -> stack)
*/


LUA_API void lua_pushanimating (lua_State *L, CBaseAnimating *pEntity) {
  CBaseHandle *hEntity = (CBaseHandle *)lua_newuserdata(L, sizeof(CBaseHandle));
  hEntity->Set(pEntity);
  luaL_getmetatable(L, "CBaseAnimating");
  lua_setmetatable(L, -2);
}


LUALIB_API lua_CBaseAnimating *luaL_checkanimating (lua_State *L, int narg) {
  lua_CBaseAnimating *d = lua_toanimating(L, narg);
  if (d == NULL)  /* avoid extra test when d is not 0 */
    luaL_argerror(L, narg, "CBaseAnimating expected, got NULL entity");
  return d;
}


static int CBaseAnimating_AddEntity (lua_State *L) {
  luaL_checkanimating(L, 1)->AddEntity();
  return 0;
}

static int CBaseAnimating_AddToClientSideAnimationList (lua_State *L) {
  luaL_checkanimating(L, 1)->AddToClientSideAnimationList();
  return 0;
}

static int CBaseAnimating_BecomeRagdollOnClient (lua_State *L) {
  lua_pushanimating(L, luaL_checkanimating(L, 1)->BecomeRagdollOnClient());
  return 1;
}

static int CBaseAnimating_CalculateIKLocks (lua_State *L) {
  luaL_checkanimating(L, 1)->CalculateIKLocks(luaL_checknumber(L, 2));
  return 0;
}

static int CBaseAnimating_ClampCycle (lua_State *L) {
  lua_pushnumber(L, luaL_checkanimating(L, 1)->ClampCycle(luaL_checknumber(L, 2), luaL_checkboolean(L, 3)));
  return 1;
}

static int CBaseAnimating_Clear (lua_State *L) {
  luaL_checkanimating(L, 1)->Clear();
  return 0;
}

static int CBaseAnimating_ClearRagdoll (lua_State *L) {
  luaL_checkanimating(L, 1)->ClearRagdoll();
  return 0;
}

static int CBaseAnimating_ClientSideAnimationChanged (lua_State *L) {
  luaL_checkanimating(L, 1)->ClientSideAnimationChanged();
  return 0;
}

static int CBaseAnimating_ComputeClientSideAnimationFlags (lua_State *L) {
  lua_pushinteger(L, luaL_checkanimating(L, 1)->ComputeClientSideAnimationFlags());
  return 1;
}

static int CBaseAnimating_ComputeEntitySpaceHitboxSurroundingBox (lua_State *L) {
  Vector pVecWorldMins, pVecWorldMaxs;
  lua_pushboolean(L, luaL_checkanimating(L, 1)->ComputeEntitySpaceHitboxSurroundingBox(&pVecWorldMins, &pVecWorldMaxs));
  lua_pushvector(L, pVecWorldMins);
  lua_pushvector(L, pVecWorldMaxs);
  return 3;
}

static int CBaseAnimating_ComputeHitboxSurroundingBox (lua_State *L) {
  Vector pVecWorldMins, pVecWorldMaxs;
  lua_pushboolean(L, luaL_checkanimating(L, 1)->ComputeHitboxSurroundingBox(&pVecWorldMins, &pVecWorldMaxs));
  lua_pushvector(L, pVecWorldMins);
  lua_pushvector(L, pVecWorldMaxs);
  return 3;
}

static int CBaseAnimating_CreateRagdollCopy (lua_State *L) {
  lua_pushanimating(L, luaL_checkanimating(L, 1)->CreateRagdollCopy());
  return 1;
}

static int CBaseAnimating_CreateUnragdollInfo (lua_State *L) {
  luaL_checkanimating(L, 1)->CreateUnragdollInfo(luaL_checkanimating(L, 2));
  return 0;
}

static int CBaseAnimating_DisableMuzzleFlash (lua_State *L) {
  luaL_checkanimating(L, 1)->DisableMuzzleFlash();
  return 0;
}

static int CBaseAnimating_DispatchMuzzleEffect (lua_State *L) {
  lua_pushboolean(L, luaL_checkanimating(L, 1)->DispatchMuzzleEffect(luaL_checkstring(L, 2), luaL_checkboolean(L, 3)));
  return 1;
}

static int CBaseAnimating_DoMuzzleFlash (lua_State *L) {
  luaL_checkanimating(L, 1)->DoMuzzleFlash();
  return 0;
}

static int CBaseAnimating_DrawClientHitboxes (lua_State *L) {
  luaL_checkanimating(L, 1)->DrawClientHitboxes(luaL_optnumber(L, 2, 0.0f), luaL_optboolean(L, 3, false));
  return 0;
}

/*
** HL2SB: the player-model colour, i.e. the piece the 3D preview needs.
**
** GMod draws a clientside model with the colour the entity's Lua GetPlayerColor
** answers, which is exactly why GMod's player model selector writes
**
**     mdl.Entity.GetPlayerColor = function() return Vector( GetConVarString( "cl_playercolor" ) ) end
**
** (garrysmod/gamemodes/sandbox/gamemode/editor_player.lua, UpdateFromConvars).  In
** this fork nothing ever asked the entity for it, so moving the Colors tab wrote
** cl_playercolor and the preview model never changed.
**
** Only a Lua-provided GetPlayerColor is honoured: an entity without one is drawn
** with whatever modulation the caller set (lua/vgui/DModelPanel.lua:Paint sets its
** own Color), so nothing else changes behaviour.
**
** Returns true when a colour was applied; the caller must restore pflPrev (3 floats)
** after the draw.
*/
static bool HL2SB_IsLuaVector (lua_State *L, int idx) {
  if (!lua_isuserdata(L, idx))
    return false;
  if (lua_getmetatable(L, idx) == 0)
    return false;
  luaL_getmetatable(L, "Vector");
  bool bVector = lua_rawequal(L, -1, -2) != 0;
  lua_pop(L, 2);
  return bVector;
}

static bool HL2SB_ApplyLuaPlayerColor (lua_State *L, int nEntity, float *pflPrev) {
  lua_CBaseAnimating *pEntity = lua_toanimating(L, nEntity);

  if (pEntity == NULL || !lua_isrefvalid(L, pEntity->m_nTableReference))
    return false;

  lua_getref(L, pEntity->m_nTableReference);
  lua_getfield(L, -1, "GetPlayerColor");
  lua_remove(L, -2);                       // leave only the field

  if (!lua_isfunction(L, -1)) {
    lua_pop(L, 1);
    return false;
  }

  lua_pushvalue(L, nEntity);               // self

  if (luasrc_pcall(L, 1, 1, 0) != 0) {     // error path leaves the nil placeholder (2026-10-04)
    lua_pop(L, 1);
    return false;
  }

  if (!HL2SB_IsLuaVector(L, -1)) {
    lua_pop(L, 1);
    return false;
  }

  const Vector &vecColor = luaL_checkvector(L, -1);
  lua_pop(L, 1);

  render->GetColorModulation(pflPrev);

  float color[3] = { vecColor.x, vecColor.y, vecColor.z };
  render->SetColorModulation(color);

  return true;
}

static int CBaseAnimating_DrawModel (lua_State *L) {
  // HL2SB GMod compat: wiki says `Entity:DrawModel( number flags = STUDIO_RENDER )`
  // - the flags are OPTIONAL. npc_shaklin_scp096's client ENT:Draw() calls it as
  // `self.Entity:DrawModel()`, and the old luaL_checkint(L, 2) turned that into
  // "bad argument #2" on every frame.
  lua_CBaseAnimating *pEntity = luaL_checkanimating(L, 1);

  float flPrevColor[3] = { 1.0f, 1.0f, 1.0f };
  bool bTinted = false;

  /*
  ** HL2SB: DISABLED (2026-09-17).  This read the entity's Lua GetPlayerColor and pushed
  ** it through render->SetColorModulation(), i.e. through the ENGINE's per-draw
  ** modulation - which writes $color2 on EVERY material of the model
  ** (studiorender/r_studio.cpp) and therefore overrode the real mechanism.
  **
  ** The real one is the player-model VMTs' own proxy:
  **
  **     Proxies { PlayerColor { resultVar $color2 ... } }      // "pass the player color
  **                                                           //  value to Gmod"
  **
  ** 42 materials in the GMod playermodel pack declare it, and the matching proxy
  ** implementation lives in game/client/c_viewmodel_attachment.cpp.  Tinting by VMT
  ** declaration is exactly GMod's behaviour ("what can be coloured, changes; what
  ** cannot, does not"), so the whole-model modulation must NOT be applied on top.
  */
  if ( false )
  {
    bTinted = HL2SB_ApplyLuaPlayerColor(L, 1, flPrevColor);
  }

  int nResult = 0;

  // HL2SB: NOT the virtual DrawModel() -- for a C_BaseScripted that virtual
  // re-dispatches ENT:Draw(), so "ENT:Draw() { self:DrawModel() }" recursed
  // into itself until the Lua C-stack limit aborted the chain.  Every level's
  // pcall swallowed the abort, so the real model draw NEVER ran and the entity
  // rendered nothing at all: the thrown cod-c4 was invisible while its red
  // blink sprite (drawn without going through DrawModel) still flashed.
  // GMod's Entity:DrawModel draws the model directly and never re-enters
  // RenderOverride / ENT:Draw -- InternalDrawModel is this fork's
  // non-dispatching path (C_BaseScripted does not override it).
  //
  nResult = pEntity->InternalDrawModel(luaL_optint(L, 2, STUDIO_RENDER));

  if (bTinted)
    render->SetColorModulation(flPrevColor);

  lua_pushinteger(L, nResult);
  return 1;
}

// HL2SB GMod compat: bone accessors the minecraft SWEP's world model needs
// (the ClientsideModel world model is drawn from the player's hand bone
// matrix, and every bone is scaled to 0.4).
static int CBaseAnimating_GetBoneCount (lua_State *L) {
  C_BaseAnimating *pEntity = luaL_checkanimating(L, 1);
  studiohdr_t *pHdr = HL2SB_GetStudioHdrSafe( pEntity );
  lua_pushinteger(L, pHdr ? pHdr->numbones : 0);
  return 1;
}

static int CBaseAnimating_GetBoneMatrix (lua_State *L) {
  C_BaseAnimating *pEntity = luaL_checkanimating(L, 1);
  int nBone = luaL_checkint(L, 2);

  // HL2SB (2026-09-24): read the LIVE bone matrices (what the renderer
  // consumes) instead of a private SetupBones() stack copy, so a matrix
  // observed inside a "BuildBonePositions" callback reflects what
  // SetBoneMatrix wrote and what will be drawn this frame.  SetupBones(NULL)
  // hits the per-frame cache on re-entry, so calling this from inside the
  // callback does not rebuild bones recursively.
  studiohdr_t *pHdr = HL2SB_GetStudioHdrSafe( pEntity );
  const int nBoneTotal = pHdr ? pHdr->numbones : 0;
  if ( !pEntity->SetupBones( NULL, 0, BONE_USED_BY_ANYTHING, gpGlobals->curtime ) )
  {
    lua_pushnil( L );
    return 1;
  }
  if ( nBone < 0 || nBone >= nBoneTotal )
  {
    lua_pushnil( L );
    return 1;
  }
  // HL2SB (2026-09-25): the accessor already holds WORLD-space bone matrices
  // (BuildTransformations concatenates the entity transform into every bone;
  // wiki for GetBoneMatrix: "the transformation used to position the bone in
  // the world").  Return it as-is - no conversion of any kind.
  VMatrix vm;
  vm.CopyFrom3x4( pEntity->GetBone( nBone ) );
  lua_pushvmatrix(L, vm);
  return 1;
}

// HL2SB GMod compat (2026-09-24): Entity:SetBoneMatrix( bone, matrix ) -
// writes through the live bone accessor so the change shows up in the current
// frame's render (First Person Body's garbage-bone hiding / bone mirroring).
static int CBaseAnimating_SetBoneMatrix (lua_State *L) {
  C_BaseAnimating *pEntity = luaL_checkanimating(L, 1);
  int nBone = luaL_checkint(L, 2);
  const VMatrix &vm = luaL_checkvmatrix(L, 3);

  studiohdr_t *pHdr = HL2SB_GetStudioHdrSafe( pEntity );
  const int nBoneTotal = pHdr ? pHdr->numbones : 0;
  if ( !pEntity->SetupBones( NULL, 0, BONE_USED_BY_ANYTHING, gpGlobals->curtime ) )
    return 0;
  if ( nBone < 0 || nBone >= nBoneTotal )
    return 0;

  // HL2SB (2026-09-25): the incoming matrix is WORLD space and the accessor
  // stores WORLD space - write it through unchanged (see GetBoneMatrix).
  // HL2SB (2026-10-03): the write itself was missing until now - every caller
  // (First Person Body's garbage-bone hiding and player-bone mirroring)
  // silently rendered the untouched setup pose, i.e. a full body stuck in
  // front of the camera.  Reference behaviour: the world-space 3x4 of the
  // VMatrix is copied straight into the live bone array the renderer reads.
  pEntity->GetBoneForWrite( nBone ) = vm.As3x4();
  return 0;
}

// Entity:SetBonePosition( bone, vecPos, angAng ): GMod's pos+ang spelling of
// SetBoneMatrix - the portalgun's player clone mirrors every bone of the real
// player through the portal with it.  Same contract: SetupBones first, then a
// straight world-space write into the live bone array.
static int CBaseAnimating_SetBonePosition (lua_State *L) {
  C_BaseAnimating *pEntity = luaL_checkanimating(L, 1);
  int nBone = luaL_checkint(L, 2);
  const Vector &vecPos = luaL_checkvector(L, 3);
  const QAngle &angAng = luaL_checkangle(L, 4);

  studiohdr_t *pHdr = HL2SB_GetStudioHdrSafe( pEntity );
  const int nBoneTotal = pHdr ? pHdr->numbones : 0;
  if ( !pEntity->SetupBones( NULL, 0, BONE_USED_BY_ANYTHING, gpGlobals->curtime ) )
    return 0;
  if ( nBone < 0 || nBone >= nBoneTotal )
    return 0;

  QuaternionMatrix( Quaternion( angAng ), vecPos, pEntity->GetBoneForWrite( nBone ) );
  return 0;
}

// HL2SB GMod compat (2026-09-24): Entity:GetBoneName( bone ) - the First
// Person Body addon classifies bones by name ("ValveBiped.*") to decide which
// to hide while mirroring.
static int CBaseAnimating_GetBoneName (lua_State *L) {
  C_BaseAnimating *pEntity = luaL_checkanimating(L, 1);
  int nBone = luaL_checkint(L, 2);
  studiohdr_t *pHdr = HL2SB_GetStudioHdrSafe( pEntity );
  if ( !pHdr || nBone < 0 || nBone >= pHdr->numbones )
  {
    // Reference behaviour: every out-of-range bone answers the placeholder
    // string, never nil - the viewmodel bone-mod base iterates 0..GetBoneCount()
    // INCLUSIVE and table-indexes each name, so a nil here raised
    // "table index is nil" every frame (the portalgun's viewmodel lights).
    lua_pushstring( L, "__INVALIDBONE__" );
    return 1;
  }
  lua_pushstring( L, pHdr->pBone( nBone )->pszName() );
  return 1;
}

// HL2SB GMod compat (2026-09-24): Entity:GetBoneNames() - names indexed 0..n-1.
static int CBaseAnimating_GetBoneNames (lua_State *L) {
  C_BaseAnimating *pEntity = luaL_checkanimating(L, 1);
  studiohdr_t *pHdr = HL2SB_GetStudioHdrSafe( pEntity );
  lua_newtable( L );
  if ( !pHdr )
    return 1;
  for ( int i = 0; i < pHdr->numbones; ++i )
  {
    lua_pushstring( L, pHdr->pBone( i )->pszName() );
    lua_rawseti( L, -2, i );
  }
  return 1;
}

// HL2SB GMod compat (2026-09-24): Entity:GetChildBones( bone ) - direct
// children in a 1-based Lua list (the addon walks this to cascade a hidden
// parent's transform onto its children).
static int CBaseAnimating_GetChildBones (lua_State *L) {
  C_BaseAnimating *pEntity = luaL_checkanimating(L, 1);
  int nBone = luaL_checkint(L, 2);
  studiohdr_t *pHdr = HL2SB_GetStudioHdrSafe( pEntity );
  lua_newtable( L );
  if ( !pHdr )
    return 1;
  int nCount = 0;
  for ( int i = 0; i < pHdr->numbones; ++i )
  {
    if ( pHdr->pBone( i )->parent == nBone )
    {
      lua_pushinteger( L, i );
      lua_rawseti( L, -2, ++nCount );
    }
  }
  return 1;
}

// HL2SB GMod compat (2026-09-24): Entity:AddCallback( name, fn ) - returns the
// callback id, or nothing for an unsupported callback name (GMod returns
// nothing for a non-existent one).  Only "BuildBonePositions" dispatches.
static int CBaseAnimating_AddCallback (lua_State *L) {
  C_BaseAnimating *pEntity = luaL_checkanimating(L, 1);
  const char *pszName = luaL_checkstring(L, 2);
  luaL_checktype(L, 3, LUA_TFUNCTION);

  if ( Q_stricmp( pszName, "BuildBonePositions" ) )
    return 0;

  if ( s_EntityCallbacks.Count() > 32 )
    HL2SB_PurgeDeadCallbackLists();

  HL2SB_EntityCallbackList_t *pList = HL2SB_FindCallbackList( pEntity, true );
  if ( pList == NULL )
    return 0;

  lua_pushvalue( L, 3 );
  int nRef = luaL_ref( L, LUA_REGISTRYINDEX );
  if ( nRef == LUA_NOREF )  // AGENTS.md 5.4.1: a NOREF unref'd twice crashes
    return 0;

  HL2SB_EntityCallback_t cb;
  cb.nID = ++s_nNextEntityCallbackID;
  Q_strncpy( cb.szName, pszName, sizeof( cb.szName ) );
  cb.nRef = nRef;
  pList->items.AddToTail( cb );

  lua_pushinteger( L, cb.nID );
  return 1;
}

// HL2SB GMod compat (2026-09-24): Entity:RemoveCallback( name, id ).
static int CBaseAnimating_RemoveCallback (lua_State *L) {
  C_BaseAnimating *pEntity = luaL_checkanimating(L, 1);
  const char *pszName = luaL_checkstring(L, 2);
  int nID = luaL_checkint(L, 3);
  HL2SB_EntityCallbackList_t *pList = HL2SB_FindCallbackList( pEntity, false );
  if ( pList != NULL )
  {
    for ( int i = pList->items.Count() - 1; i >= 0; --i )
    {
      HL2SB_EntityCallback_t &cb = pList->items[i];
      if ( cb.nID == nID && !Q_stricmp( cb.szName, pszName ) )
      {
        luaL_unref( L, LUA_REGISTRYINDEX, cb.nRef );
        pList->items.FastRemove( i );
        break;
      }
    }
  }
  return 0;
}

// HL2SB GMod compat (2026-09-24): Entity:GetCallbacks( name ) - id-keyed
// table of live callbacks (the addon probes it before re-adding its own).
static int CBaseAnimating_GetCallbacks (lua_State *L) {
  C_BaseAnimating *pEntity = luaL_checkanimating(L, 1);
  const char *pszName = luaL_checkstring(L, 2);
  lua_newtable( L );
  HL2SB_EntityCallbackList_t *pList = HL2SB_FindCallbackList( pEntity, false );
  if ( pList == NULL )
    return 1;
  for ( int i = 0; i < pList->items.Count(); ++i )
  {
    HL2SB_EntityCallback_t &cb = pList->items[i];
    if ( Q_stricmp( cb.szName, pszName ) )
      continue;
    lua_getref( L, cb.nRef );
    lua_rawseti( L, -2, cb.nID );
  }
  return 1;
}

static int CBaseAnimating_ManipulateBoneScale (lua_State *L) {
  // HL2SB (2026-10-03): real per-bone scale (reference behaviour, see
  // HL2SB_BoneManip_t above).  The old whole-model degrade broke First Person
  // Body's vehicle head-hiding (scale 0 on ONE bone shrank the entire body);
  // a uniform scale on EVERY bone is still visually SetModelScale, so the
  // minecraft held-block usage is unaffected.
  C_BaseAnimating *pEntity = luaL_checkanimating(L, 1);
  int nBone = luaL_checkint(L, 2);
  Vector scale = luaL_checkvector(L, 3);
  bool bImmediate = lua_isnoneornil(L, 4) ? true : ( lua_toboolean(L, 4) != 0 );

  HL2SB_BoneManip_t *pManip = HL2SB_FindBoneManip( pEntity, nBone, true );
  pManip->vecScale = scale;
    bool bChanged = ( !pManip->bHasScale || !( pManip->vecScale == scale ) );
  pManip->bHasScale = true;

  if ( bImmediate && bChanged )
    HL2SB_ApplyBoneManipEntry( pEntity, *pManip, false );
  return 0;
}

static int CBaseAnimating_FindBodygroupByName (lua_State *L) {
  lua_pushinteger(L, luaL_checkanimating(L, 1)->FindBodygroupByName(luaL_checkstring(L, 2)));
  return 1;
}

static int CBaseAnimating_FindFollowedEntity (lua_State *L) {
  lua_pushanimating(L, luaL_checkanimating(L, 1)->FindFollowedEntity());
  return 1;
}

static int CBaseAnimating_FindTransitionSequence (lua_State *L) {
  int piDir;
  lua_pushinteger(L, luaL_checkanimating(L, 1)->FindTransitionSequence(luaL_checkint(L, 2), luaL_checkint(L, 3), &piDir));
  lua_pushinteger(L, piDir);
  return 2;
}

static int CBaseAnimating_FireEvent (lua_State *L) {
  luaL_checkanimating(L, 1)->FireEvent(luaL_checkvector(L, 2), luaL_checkangle(L, 3), luaL_checkint(L, 4), luaL_checkstring(L, 5));
  return 0;
}

static int CBaseAnimating_FireObsoleteEvent (lua_State *L) {
  luaL_checkanimating(L, 1)->FireObsoleteEvent(luaL_checkvector(L, 2), luaL_checkangle(L, 3), luaL_checkint(L, 4), luaL_checkstring(L, 5));
  return 0;
}

static int CBaseAnimating_ForceClientSideAnimationOn (lua_State *L) {
  luaL_checkanimating(L, 1)->ForceClientSideAnimationOn();
  return 0;
}

static int CBaseAnimating_FrameAdvance (lua_State *L) {
  lua_pushnumber(L, luaL_checkanimating(L, 1)->FrameAdvance(luaL_optnumber(L, 2, 0.0f)));
  return 1;
}

static int CBaseAnimating_GetAimEntOrigin (lua_State *L) {
  luaL_checkanimating(L, 1)->GetAimEntOrigin(luaL_checkentity(L, 2), &luaL_checkvector(L, 3), &luaL_checkangle(L, 4));
  return 0;
}

static int CBaseAnimating_GetAnimTimeInterval (lua_State *L) {
  lua_pushnumber(L, luaL_checkanimating(L, 1)->GetAnimTimeInterval());
  return 1;
}

static int CBaseAnimating_GetAttachment (lua_State *L) {
  // HL2SB (2026-09-22): GMod signature GetAttachment( id|name ) -> a table
  // { Pos = Vector, Ang = Angle }, nil when the attachment doesn't exist.
  // cf_beast's shell eject reads vm:GetAttachment(att) this way; the old
  // binding only knew the HL2 out-parameter form, so the single-argument call
  // threw "bad argument #2 ... Vector expected" and the whole Muzzle effect
  // chain died on every shot.
  if (lua_gettop(L) == 2) {
    C_BaseAnimating *pAnimating = luaL_checkanimating(L, 1);
    Vector origin;
    QAngle angles;
    bool bOk;
    if (lua_type(L, 2) == LUA_TNUMBER)
      bOk = pAnimating->GetAttachment(luaL_checkint(L, 2), origin, angles);
    else
      bOk = pAnimating->GetAttachment(luaL_checkstring(L, 2), origin, angles);
    if (!bOk) {
      lua_pushnil(L);
      return 1;
    }
    lua_newtable(L);                        // [tab]
    lua_pushvector(L, origin);              // [tab, Pos]
    lua_setfield(L, -2, "Pos");
    lua_pushangle(L, angles);               // [tab, Ang]
    lua_setfield(L, -2, "Ang");
    return 1;
  }
  switch(lua_type(L, 2)) {
	case LUA_TNUMBER:
      {
        if (lua_gettop(L) <= 3)
          lua_pushboolean(L, luaL_checkanimating(L, 1)->GetAttachment(luaL_checkint(L, 2), luaL_checkvector(L, 3)));
        else
          lua_pushboolean(L, luaL_checkanimating(L, 1)->GetAttachment(luaL_checkint(L, 2), luaL_checkvector(L, 3), luaL_checkangle(L, 4)));
        break;
      }
	case LUA_TSTRING:
	default:
      {
        if (lua_gettop(L) <= 3)
          lua_pushboolean(L, luaL_checkanimating(L, 1)->GetAttachment(luaL_checkstring(L, 2), luaL_checkvector(L, 3)));
        else
          lua_pushboolean(L, luaL_checkanimating(L, 1)->GetAttachment(luaL_checkstring(L, 2), luaL_checkvector(L, 3), luaL_checkangle(L, 4)));
        break;
	  }
  }
  return 1;
}

static int CBaseAnimating_GetAttachmentLocal (lua_State *L) {
  lua_pushboolean(L, luaL_checkanimating(L, 1)->GetAttachmentLocal(luaL_checkint(L, 2), luaL_checkvector(L, 3), luaL_checkangle(L, 4)));
  return 1;
}

static int CBaseAnimating_GetAttachmentVelocity (lua_State *L) {
  Vector originVel;
  Quaternion angleVel;
  lua_pushboolean(L, luaL_checkanimating(L, 1)->GetAttachmentVelocity(luaL_checkint(L, 2), originVel, angleVel));
  lua_pushvector(L, originVel);
  // Todo: implement Quaternion class!!
  // lua_pushquaternion(L, &angleVel);
  return 2;
}

static int CBaseAnimating_GetBaseAnimating (lua_State *L) {
  lua_pushanimating(L, luaL_checkanimating(L, 1)->GetBaseAnimating());
  return 1;
}

static int CBaseAnimating_GetBlendedLinearVelocity (lua_State *L) {
  Vector pVec;
  luaL_checkanimating(L, 1)->GetBlendedLinearVelocity(&pVec);
  lua_pushvector(L, pVec);
  return 1;
}

static int CBaseAnimating_GetBody (lua_State *L) {
  lua_pushinteger(L, luaL_checkanimating(L, 1)->GetBody());
  return 1;
}

static int CBaseAnimating_GetBodygroup (lua_State *L) {
  lua_pushinteger(L, luaL_checkanimating(L, 1)->GetBodygroup(luaL_checkint(L, 2)));
  return 1;
}

// HL2SB GMod compat (2026-10-03): Entity:GetBodygroups() - every bodygroup as
// a 1-based array of { id, name, num } tables.  First Person Body copies the
// player's bodygroups onto its body copies through this.
static int CBaseAnimating_GetBodygroups (lua_State *L) {
  C_BaseAnimating *pEntity = luaL_checkanimating(L, 1);
  studiohdr_t *pHdr = HL2SB_GetStudioHdrSafe( pEntity );
  lua_newtable( L );
  if ( pHdr == NULL )
    return 1;

  CStudioHdr hdr( pHdr );
  const int nGroups = GetNumBodyGroups( &hdr );
  for ( int i = 0; i < nGroups; i++ )
  {
    lua_createtable( L, 0, 3 );
    lua_pushinteger( L, i );
    lua_setfield( L, -2, "id" );
    lua_pushstring( L, pEntity->GetBodygroupName( i ) );
    lua_setfield( L, -2, "name" );
    lua_pushinteger( L, pEntity->GetBodygroupCount( i ) );
    lua_setfield( L, -2, "num" );
    lua_rawseti( L, -2, i + 1 );
  }
  return 1;
}

static int CBaseAnimating_GetBodygroupCount (lua_State *L) {
  lua_pushinteger(L, luaL_checkanimating(L, 1)->GetBodygroupCount(luaL_checkint(L, 2)));
  return 1;
}

static int CBaseAnimating_GetBodygroupName (lua_State *L) {
  lua_pushstring(L, luaL_checkanimating(L, 1)->GetBodygroupName(luaL_checkint(L, 2)));
  return 1;
}

static int CBaseAnimating_GetBoneControllers (lua_State *L) {
  float controllers[MAXSTUDIOBONECTRLS];
  luaL_checkanimating(L, 1)->GetBoneControllers(controllers);
  int i;
  for( i=0; i < MAXSTUDIOBONECTRLS; i++)
  {
	  lua_pushnumber(L, controllers[ i ]);
  }
  return MAXSTUDIOBONECTRLS;
}

static int CBaseAnimating_GetBonePosition (lua_State *L) {
  // HL2SB GMod contract (2026-09-29): Entity:GetBonePosition( boneIndex )
  // RETURNS position, angle (cod_c4's DrawWorldModel:
  // `local pos, ang = owner:GetBonePosition( bone )` every frame - the old
  // out-arguments-only binding raised "bad argument #3 to 'GetBonePosition'
  // (QAngle expected, got no value)" hundreds of times per session).  The
  // legacy (bone, vecOut, angOut) form is kept working: when the out args
  // are present they are written as before.  An index outside the model's
  // bones answers the entity's abs transform, GMod's documented fallback
  // for a missing bone - and the only way to keep C_BaseAnimating::
  // GetBonePosition (unchecked array math) from reading past the bone array.
  C_BaseAnimating *pAnimating = luaL_checkanimating(L, 1);
  const int iBone = luaL_checkint(L, 2);

  Vector origin;
  QAngle angles;
  studiohdr_t *pHdr = HL2SB_GetStudioHdrSafe( pAnimating );
  const int nBones = pHdr ? pHdr->numbones : 0;
  if ( iBone >= 0 && iBone < nBones )
  {
    pAnimating->GetBonePosition( iBone, origin, angles );
  }
  else
  {
    origin = pAnimating->GetAbsOrigin();
    angles = pAnimating->GetAbsAngles();
  }

  const int nArgs = lua_gettop(L);
  if ( nArgs >= 3 && lua_isuserdata(L, 3) )
    luaL_checkvector(L, 3) = origin;
  if ( nArgs >= 4 && lua_isuserdata(L, 4) )
    luaL_checkangle(L, 4) = angles;

  lua_pushvector(L, origin);
  lua_pushangle(L, angles);
  return 2;
}

static int CBaseAnimating_GetClientSideFade (lua_State *L) {
  lua_pushinteger(L, luaL_checkanimating(L, 1)->GetClientSideFade());
  return 1;
}

static int CBaseAnimating_GetCollideType (lua_State *L) {
  lua_pushinteger(L, luaL_checkanimating(L, 1)->GetCollideType());
  return 1;
}

static int CBaseAnimating_GetCycle (lua_State *L) {
  lua_pushnumber(L, luaL_checkanimating(L, 1)->GetCycle());
  return 1;
}

static int CBaseAnimating_GetFlexControllerName (lua_State *L) {
  lua_pushstring(L, luaL_checkanimating(L, 1)->GetFlexControllerName((LocalFlexController_t)luaL_checkint(L, 2)));
  return 1;
}

static int CBaseAnimating_GetFlexControllerType (lua_State *L) {
  lua_pushstring(L, luaL_checkanimating(L, 1)->GetFlexControllerType((LocalFlexController_t)luaL_checkint(L, 2)));
  return 1;
}

// HL2SB GMod compat (2026-09-24): flex + pose + render-bounds bindings the
// First Person Body addon drives its shadow-body cloning with.
// HL2SB (2026-09-27): GMod's Entity:GetFlexIDByName( name ) - resolves a
// flex controller by name (returns -1 when the model lacks it), which the
// ported MouthMoveAnimation uses for the five jaw/mouth flexes.
static int CBaseAnimating_GetFlexIDByName (lua_State *L) {
  C_BaseAnimating *pAnimating = luaL_checkanimating(L, 1);
  C_BaseFlex *pFlex = dynamic_cast<C_BaseFlex *>(pAnimating);
  if (pFlex == NULL) { lua_pushinteger(L, -1); return 1; }
  lua_pushinteger(L, (int)pFlex->FindFlexController( luaL_checkstring(L, 2) ));
  return 1;
}

static int CBaseAnimating_GetFlexNum (lua_State *L) {
  lua_pushinteger(L, luaL_checkanimating(L, 1)->GetNumFlexControllers());
  return 1;
}

// The engine keeps no client-side flex scale, so round-trip it here per entity.
struct HL2SB_FlexScale_t { CBaseHandle hEntity; float flScale; };
static CUtlVector<HL2SB_FlexScale_t> s_FlexScales;

static float HL2SB_GetFlexScaleValue( C_BaseAnimating *pEntity )
{
  const CBaseHandle h = pEntity->GetRefEHandle();
  for ( int i = 0; i < s_FlexScales.Count(); ++i )
    if ( s_FlexScales[i].hEntity == h )
      return s_FlexScales[i].flScale;
  return 1.0f;
}

static int CBaseAnimating_GetFlexScale (lua_State *L) {
  lua_pushnumber(L, HL2SB_GetFlexScaleValue( luaL_checkanimating(L, 1) ));
  return 1;
}

static int CBaseAnimating_SetFlexScale (lua_State *L) {
  C_BaseAnimating *pEntity = luaL_checkanimating(L, 1);
  float flScale = luaL_checknumber(L, 2);
  const CBaseHandle h = pEntity->GetRefEHandle();
  for ( int i = 0; i < s_FlexScales.Count(); ++i )
  {
    if ( s_FlexScales[i].hEntity == h )
    {
      s_FlexScales[i].flScale = flScale;
      return 0;
    }
  }
  if ( s_FlexScales.Count() > 64 )
  {
    for ( int i = s_FlexScales.Count() - 1; i >= 0; --i )
      if ( s_FlexScales[i].hEntity.Get() == NULL )
        s_FlexScales.FastRemove( i );
  }
  HL2SB_FlexScale_t &entry = s_FlexScales[s_FlexScales.AddToTail()];
  entry.hEntity = h;
  entry.flScale = flScale;
  return 0;
}

static int CBaseAnimating_GetFlexWeight (lua_State *L) {
  C_BaseAnimating *pEntity = luaL_checkanimating(L, 1);
  int i = luaL_checkint(L, 2);
  C_BaseFlex *pFlex = dynamic_cast<C_BaseFlex *>( pEntity );
  if ( !pFlex || i < 0 || i >= (int)pFlex->GetNumFlexControllers() )
  {
    lua_pushnumber(L, 0.0f);
    return 1;
  }
  lua_pushnumber(L, pFlex->GetFlexWeight( (LocalFlexController_t)i ));
  return 1;
}

static int CBaseAnimating_SetFlexWeight (lua_State *L) {
  C_BaseAnimating *pEntity = luaL_checkanimating(L, 1);
  int i = luaL_checkint(L, 2);
  float flWeight = luaL_checknumber(L, 3);
  C_BaseFlex *pFlex = dynamic_cast<C_BaseFlex *>( pEntity );
  if ( !pFlex || i < 0 || i >= (int)pFlex->GetNumFlexControllers() )
    return 0;
  pFlex->SetFlexWeight( (LocalFlexController_t)i, flWeight );
  return 0;
}

static int CBaseAnimating_GetNumPoseParameters (lua_State *L) {
  C_BaseAnimating *pEntity = luaL_checkanimating(L, 1);
  CStudioHdr hdr( HL2SB_GetStudioHdrSafe( pEntity ) );
  lua_pushinteger(L, hdr.IsValid() ? hdr.GetNumPoseParameters() : 0);
  return 1;
}

// HL2SB GMod compat (2026-09-24): Entity:GetModelRenderBounds() -> mins, maxs.
static int CBaseAnimating_GetModelRenderBounds (lua_State *L) {
  C_BaseAnimating *pEntity = luaL_checkanimating(L, 1);
  const model_t *pModel = pEntity->GetModel();
  Vector mins, maxs;
  if ( pModel )
    modelinfo->GetModelRenderBounds( pModel, mins, maxs );
  lua_pushvector(L, mins);
  lua_pushvector(L, maxs);
  return 2;
}

// HL2SB GMod compat (2026-09-24): Entity:SetLOD(level) - the client model has
// no LOD override storage in this fork, accept it as a safe no-op (First
// Person Body uses it to pin its shadow-body clone to full detail).
static int CBaseAnimating_SetLOD (lua_State *L) {
  luaL_checkanimating(L, 1);
  luaL_checkint(L, 2);
  return 0;
}

// HL2SB GMod compat (2026-09-25): Entity:SetupBones() - forces the bone build
// (wiki: "forces the entity to reconfigure its bones"; GMod notes it fires the
// BuildBonePositions callback - the re-entrancy guard inside
// HL2SB_RunEntityCallbacks is what keeps the documented infinite-loop warning
// from becoming a real one).  Returns the engine's success flag.
static int CBaseAnimating_SetupBones (lua_State *L) {
  C_BaseAnimating *pEntity = luaL_checkanimating(L, 1);
  // HL2SB (2026-10-04) GMod contract: Entity:SetupBones FORCES a bone rebuild.
  // Honoring the per-frame dedup here let one mid-frame forced build serve as
  // the render build: First Person Body's CalcView work zeroes head/steer pose
  // parameters around its own SetupBones and restores them right after -- with
  // a cache hit on the render pass the restore was never rebuilt, and the
  // seated model vibrated between the two poses every frame.
  pEntity->InvalidateBoneCache();
  lua_pushboolean(L, pEntity->SetupBones( NULL, 0, BONE_USED_BY_ANYTHING, gpGlobals->curtime ));
  return 1;
}

// HL2SB GMod compat (2026-09-25): Entity:GetPoseParameterName( id ) -> string.
static int CBaseAnimating_GetPoseParameterName (lua_State *L) {
  C_BaseAnimating *pEntity = luaL_checkanimating(L, 1);
  int nParam = luaL_checkint(L, 2);
  studiohdr_t *pHdr = HL2SB_GetStudioHdrSafe( pEntity );
  CStudioHdr hdr( pHdr );
  if ( !hdr.IsValid() || nParam < 0 || nParam >= hdr.GetNumPoseParameters() )
  {
    lua_pushnil( L );
    return 1;
  }
  lua_pushstring( L, hdr.pPoseParameter( nParam ).pszName() );
  return 1;
}

// HL2SB GMod compat (2026-09-25, real 2026-10-03): Entity:ManipulateBonePosition(
// bone, vec [, immediate=true] ) - the offset is stored per (entity, bone) and
// applied on every bone setup plus immediately by default (reference
// behaviour: the optional 4th boolean defaults to true).  First
// Person Body pushes the head bone to (0, 10000, 0) to hide it.
static int CBaseAnimating_ManipulateBonePosition (lua_State *L) {
  C_BaseAnimating *pEntity = luaL_checkanimating(L, 1);
  int nBone = luaL_checkint(L, 2);
  const Vector &vecPos = luaL_checkvector(L, 3);
  bool bImmediate = lua_isnoneornil(L, 4) ? true : ( lua_toboolean(L, 4) != 0 );

  HL2SB_BoneManip_t *pManip = HL2SB_FindBoneManip( pEntity, nBone, true );
  pManip->vecPos = vecPos;
    bool bChanged = ( !pManip->bHasPos || !( pManip->vecPos == vecPos ) );
  pManip->bHasPos = true;

  if ( bImmediate && bChanged )
    HL2SB_ApplyBoneManipEntry( pEntity, *pManip, false );
  return 0;
}

// HL2SB GMod compat (2026-10-03): Entity:ManipulateBoneAngles( bone, ang ).
// Same per-(entity, bone) storage as Position/Scale; the portalgun's viewmodel
// bone-mod base writes angles, scales and offsets together every frame and
// reads all three back through GetManipulateBone*.
static int CBaseAnimating_ManipulateBoneAngles (lua_State *L) {
  C_BaseAnimating *pEntity = luaL_checkanimating(L, 1);
  int nBone = luaL_checkint(L, 2);
  const QAngle &angAngles = luaL_checkangle(L, 3);
  bool bImmediate = lua_isnoneornil(L, 4) ? true : ( lua_toboolean(L, 4) != 0 );

  HL2SB_BoneManip_t *pManip = HL2SB_FindBoneManip( pEntity, nBone, true );
  pManip->angAngles = angAngles;
    bool bChanged = ( !pManip->bHasAngles || !( pManip->angAngles == angAngles ) );
  pManip->bHasAngles = true;

  if ( bImmediate && bChanged )
    HL2SB_ApplyBoneManipEntry( pEntity, *pManip, false );
  return 0;
}

// GetManipulateBone{Scale,Angles,Position}: the stored manipulated values.
// Unmanipulated bones answer the identity (1 1 1 / 0 0 0 / 0 0 0) - the
// viewmodel bone-mod base compares these against what it is about to write,
// so the identity defaults are what keeps it from re-writing every frame.
static int CBaseAnimating_GetManipulateBoneScale (lua_State *L) {
  C_BaseAnimating *pEntity = luaL_checkanimating(L, 1);
  HL2SB_BoneManip_t *pManip = HL2SB_FindBoneManip( pEntity, luaL_checkint(L, 2), false );
  if ( pManip == NULL || !pManip->bHasScale )
  {
    lua_pushvector( L, Vector( 1.0f, 1.0f, 1.0f ) );
    return 1;
  }
  lua_pushvector( L, pManip->vecScale );
  return 1;
}

static int CBaseAnimating_GetManipulateBoneAngles (lua_State *L) {
  C_BaseAnimating *pEntity = luaL_checkanimating(L, 1);
  HL2SB_BoneManip_t *pManip = HL2SB_FindBoneManip( pEntity, luaL_checkint(L, 2), false );
  if ( pManip == NULL || !pManip->bHasAngles )
  {
    lua_pushangle( L, QAngle( 0.0f, 0.0f, 0.0f ) );
    return 1;
  }
  lua_pushangle( L, pManip->angAngles );
  return 1;
}

static int CBaseAnimating_GetManipulateBonePosition (lua_State *L) {
  C_BaseAnimating *pEntity = luaL_checkanimating(L, 1);
  HL2SB_BoneManip_t *pManip = HL2SB_FindBoneManip( pEntity, luaL_checkint(L, 2), false );
  if ( pManip == NULL || !pManip->bHasPos )
  {
    lua_pushvector( L, Vector( 0.0f, 0.0f, 0.0f ) );
    return 1;
  }
  lua_pushvector( L, pManip->vecPos );
  return 1;
}

// Entity:GetBoneParent( bone ): the studio bone's parent index, -1 when the
// bone has no parent or is out of range.  The portalgun's bone-mod base walks
// parents to accumulate inherited scales.
static int CBaseAnimating_GetBoneParent (lua_State *L) {
  C_BaseAnimating *pEntity = luaL_checkanimating(L, 1);
  int nBone = luaL_checkint(L, 2);
  studiohdr_t *pHdr = HL2SB_GetStudioHdrSafe( pEntity );
  if ( !pHdr || nBone < 0 || nBone >= pHdr->numbones )
  {
    lua_pushinteger( L, -1 );
    return 1;
  }
  lua_pushinteger( L, pHdr->pBone( nBone )->parent );
  return 1;
}

// HL2SB GMod compat (2026-09-24): Entity:CreateShadow(radius) /
// Entity:DestroyShadow() are accepted as safe NO-OPS.  GMod builds a
// clientside shadow copy of the model; this fork has no clientside projected
// shadow machinery, and First Person Body only uses them for the body's fake
// ground shadow (cosmetic) - everything else in the addon degrades cleanly.
static int CBaseAnimating_CreateShadow (lua_State *L) {
  C_BaseAnimating *pEntity = luaL_checkanimating(L, 1);
  // HL2SB (2026-10-04) GMod contract: Entity:CreateShadow renders the clone
  // ONLY into the shadow depth pass.  This fork has no clientside shadow-depth
  // rendering, and a plain clientside model left visible draws as a FULL model
  // in the main view at whatever stale bone state it last built -- First Person
  // Body's Body_Shadow_1..4 appeared as a second player (head included) and
  // were the "shaking model" inside vehicles.  NODRAW the clone: the fake-body
  // shadow feature stays disabled here until a clientside depth pass exists
  // (GMod with shadows disabled behaves the same way).
  pEntity->AddEffects( EF_NODRAW );
  return 0;
}

static int CBaseAnimating_DestroyShadow (lua_State *L) {
  luaL_checkanimating(L, 1);
  return 0;
}

static int CBaseAnimating_GetFlexDescFacs (lua_State *L) {
  lua_pushstring(L, luaL_checkanimating(L, 1)->GetFlexDescFacs(luaL_checkint(L, 2)));
  return 1;
}

static int CBaseAnimating_GetHitboxSet (lua_State *L) {
  lua_pushinteger(L, luaL_checkanimating(L, 1)->GetHitboxSet());
  return 1;
}

static int CBaseAnimating_GetHitboxSetCount (lua_State *L) {
  lua_pushinteger(L, luaL_checkanimating(L, 1)->GetHitboxSetCount());
  return 1;
}

static int CBaseAnimating_GetHitboxSetName (lua_State *L) {
  lua_pushstring(L, luaL_checkanimating(L, 1)->GetHitboxSetName());
  return 1;
}

/*static int CBaseAnimating_GetModelWidthScale (lua_State *L) {
  lua_pushnumber(L, luaL_checkanimating(L, 1)->GetModelWidthScale());
  return 1;
}
*/

static int CBaseAnimating_GetNumBodyGroups (lua_State *L) {
  lua_pushinteger(L, luaL_checkanimating(L, 1)->GetNumBodyGroups());
  return 1;
}

static int CBaseAnimating_GetNumFlexControllers (lua_State *L) {
  lua_pushinteger(L, luaL_checkanimating(L, 1)->GetNumFlexControllers());
  return 1;
}

static int CBaseAnimating_GetPlaybackRate (lua_State *L) {
  lua_pushnumber(L, luaL_checkanimating(L, 1)->GetPlaybackRate());
  return 1;
}

static int CBaseAnimating_GetPoseParameter (lua_State *L) {
  C_BaseAnimating *pEntity = luaL_checkanimating(L, 1);

  // HL2SB GMod compat (2026-09-25): GMod accepts a NAME or an index
  // (First Person Body passes "head_yaw"/"body_yaw"/... names).
  int nParam = -1;
  if ( lua_type( L, 2 ) == LUA_TSTRING )
  {
    const char *pszName = luaL_checkstring( L, 2 );
    studiohdr_t *pHdr = HL2SB_GetStudioHdrSafe( pEntity );
    CStudioHdr hdr( pHdr );
    if ( hdr.IsValid() )
    {
      for ( int i = 0; i < hdr.GetNumPoseParameters(); ++i )
      {
        if ( !Q_stricmp( hdr.pPoseParameter( i ).pszName(), pszName ) )
        {
          nParam = i;
          break;
        }
      }
    }
    if ( nParam == -1 )
    {
      lua_pushnumber( L, 0.0f );
      return 1;
    }
  }
  else
  {
    nParam = luaL_checkint( L, 2 );
  }

  lua_pushnumber(L, pEntity->GetPoseParameter(nParam));
  return 1;
}

static int CBaseAnimating_GetPoseParameterRange (lua_State *L) {
  float minValue, maxValue;
  lua_pushboolean(L, luaL_checkanimating(L, 1)->GetPoseParameterRange(luaL_checkint(L, 2), minValue, maxValue));
  lua_pushnumber(L, minValue);
  lua_pushnumber(L, maxValue);
  return 3;
}

static int CBaseAnimating_GetRenderAngles (lua_State *L) {
  QAngle v = luaL_checkanimating(L, 1)->GetRenderAngles();
  lua_pushangle(L, v);
  return 1;
}

static int CBaseAnimating_GetRenderBounds (lua_State *L) {
  Vector theMins, theMaxs;
  luaL_checkanimating(L, 1)->GetRenderBounds(theMins, theMaxs);
  lua_pushvector(L, theMins);
  lua_pushvector(L, theMaxs);
  return 2;
}

static int CBaseAnimating_GetRenderOrigin (lua_State *L) {
  Vector v = luaL_checkanimating(L, 1)->GetRenderOrigin();
  lua_pushvector(L, v);
  return 1;
}

static int CBaseAnimating_GetSequence (lua_State *L) {
  lua_pushinteger(L, luaL_checkanimating(L, 1)->GetSequence());
  return 1;
}

static int CBaseAnimating_GetSequenceActivity (lua_State *L) {
  lua_pushinteger(L, luaL_checkanimating(L, 1)->GetSequenceActivity(luaL_checkint(L, 2)));
  return 1;
}

static int CBaseAnimating_GetSequenceActivityName (lua_State *L) {
  lua_pushstring(L, luaL_checkanimating(L, 1)->GetSequenceActivityName(luaL_checkint(L, 2)));
  return 1;
}

static int CBaseAnimating_GetSequenceGroundSpeed (lua_State *L) {
  lua_pushnumber(L, luaL_checkanimating(L, 1)->GetSequenceGroundSpeed(luaL_checkint(L, 2)));
  return 1;
}

static int CBaseAnimating_GetSequenceLinearMotion (lua_State *L) {
  Vector pVec;
  luaL_checkanimating(L, 1)->GetSequenceLinearMotion(luaL_checkint(L, 2), &pVec);
  lua_pushvector(L, pVec);
  return 1;
}

static int CBaseAnimating_GetSequenceName (lua_State *L) {
  lua_pushstring(L, luaL_checkanimating(L, 1)->GetSequenceName(luaL_checkint(L, 2)));
  return 1;
}

static int CBaseAnimating_GetServerIntendedCycle (lua_State *L) {
  lua_pushnumber(L, luaL_checkanimating(L, 1)->GetServerIntendedCycle());
  return 1;
}

static int CBaseAnimating_GetSkin (lua_State *L) {
  lua_pushinteger(L, luaL_checkanimating(L, 1)->GetSkin());
  return 1;
}

static int CBaseAnimating_IgniteRagdoll (lua_State *L) {
  luaL_checkanimating(L, 1)->IgniteRagdoll(luaL_checkanimating(L, 2));
  return 0;
}

static int CBaseAnimating_InitBoneSetupThreadPool (lua_State *L) {
  CBaseAnimating::InitBoneSetupThreadPool();
  return 0;
}

static int CBaseAnimating_InitModelEffects (lua_State *L) {
  luaL_checkanimating(L, 1)->InitModelEffects();
  return 0;
}

static int CBaseAnimating_InternalDrawModel (lua_State *L) {
  lua_pushinteger(L, luaL_checkanimating(L, 1)->InternalDrawModel(luaL_checkint(L, 2)));
  return 1;
}

static int CBaseAnimating_Interpolate (lua_State *L) {
  lua_pushboolean(L, luaL_checkanimating(L, 1)->Interpolate(luaL_checknumber(L, 2)));
  return 1;
}

static int CBaseAnimating_InvalidateBoneCache (lua_State *L) {
  luaL_checkanimating(L, 1)->InvalidateBoneCache();
  return 0;
}

static int CBaseAnimating_InvalidateBoneCaches (lua_State *L) {
  CBaseAnimating::InvalidateBoneCaches();
  return 0;
}

static int CBaseAnimating_InvalidateMdlCache (lua_State *L) {
  luaL_checkanimating(L, 1)->InvalidateMdlCache();
  return 0;
}

static int CBaseAnimating_IsActivityFinished (lua_State *L) {
  lua_pushboolean(L, luaL_checkanimating(L, 1)->IsActivityFinished());
  return 1;
}

static int CBaseAnimating_IsBoneCacheValid (lua_State *L) {
  lua_pushboolean(L, luaL_checkanimating(L, 1)->IsBoneCacheValid());
  return 1;
}

static int CBaseAnimating_IsOnFire (lua_State *L) {
  lua_pushboolean(L, luaL_checkanimating(L, 1)->IsOnFire());
  return 1;
}

static int CBaseAnimating_IsRagdoll (lua_State *L) {
  lua_pushboolean(L, luaL_checkanimating(L, 1)->IsRagdoll());
  return 1;
}

static int CBaseAnimating_IsSelfAnimating (lua_State *L) {
  lua_pushboolean(L, luaL_checkanimating(L, 1)->IsSelfAnimating());
  return 1;
}

static int CBaseAnimating_IsSequenceFinished (lua_State *L) {
  lua_pushboolean(L, luaL_checkanimating(L, 1)->IsSequenceFinished());
  return 1;
}

static int CBaseAnimating_IsSequenceLooping (lua_State *L) {
  lua_pushboolean(L, luaL_checkanimating(L, 1)->IsSequenceLooping(luaL_checkint(L, 2)));
  return 1;
}

static int CBaseAnimating_IsViewModel (lua_State *L) {
  lua_pushboolean(L, luaL_checkanimating(L, 1)->IsViewModel());
  return 1;
}

static int CBaseAnimating_LookupActivity (lua_State *L) {
  lua_pushinteger(L, luaL_checkanimating(L, 1)->LookupActivity(luaL_checkstring(L, 2)));
  return 1;
}

static int CBaseAnimating_LookupAttachment (lua_State *L) {
  lua_pushinteger(L, luaL_checkanimating(L, 1)->LookupAttachment(luaL_checkstring(L, 2)));
  return 1;
}

static int CBaseAnimating_LookupBone (lua_State *L) {
  lua_pushinteger(L, luaL_checkanimating(L, 1)->LookupBone(luaL_checkstring(L, 2)));
  return 1;
}

static int CBaseAnimating_LookupPoseParameter (lua_State *L) {
  lua_pushinteger(L, luaL_checkanimating(L, 1)->LookupPoseParameter(luaL_checkstring(L, 2)));
  return 1;
}

static int CBaseAnimating_LookupRandomAttachment (lua_State *L) {
  lua_pushinteger(L, luaL_checkanimating(L, 1)->LookupRandomAttachment(luaL_checkstring(L, 2)));
  return 1;
}

static int CBaseAnimating_LookupSequence (lua_State *L) {
  // HL2SB (2026-09-22): GMod returns TWO values -- the sequence id and its
  // play duration.  cf_beast's SetupENUM does
  //     self.ReloadSpeed = select( 2, vm:LookupSequence( "reload" ) )
  // and got nil (Reload-speed arithmetic died on every reload).
  C_BaseAnimating *pAnimating = luaL_checkanimating(L, 1);
  const int iSeq = pAnimating->LookupSequence(luaL_checkstring(L, 2));
  lua_pushinteger(L, iSeq);
  lua_pushnumber(L, (iSeq >= 0) ? pAnimating->SequenceDuration(iSeq) : 0.0f);
  return 2;
}

static int CBaseAnimating_NotifyShouldTransmit (lua_State *L) {
  luaL_checkanimating(L, 1)->NotifyShouldTransmit((ShouldTransmitState_t)luaL_checkint(L, 2));
  return 0;
}

static int CBaseAnimating_OnDataChanged (lua_State *L) {
  luaL_checkanimating(L, 1)->OnDataChanged((DataUpdateType_t)luaL_checkint(L, 2));
  return 0;
}

static int CBaseAnimating_OnPreDataChanged (lua_State *L) {
  luaL_checkanimating(L, 1)->OnPreDataChanged((DataUpdateType_t)luaL_checkint(L, 2));
  return 0;
}

static int CBaseAnimating_PopBoneAccess (lua_State *L) {
  C_BaseAnimating::PopBoneAccess(luaL_checkstring(L, 1));
  return 0;
}

static int CBaseAnimating_PostDataUpdate (lua_State *L) {
  luaL_checkanimating(L, 1)->PostDataUpdate((DataUpdateType_t)luaL_checkint(L, 2));
  return 0;
}

static int CBaseAnimating_PreDataUpdate (lua_State *L) {
  luaL_checkanimating(L, 1)->PreDataUpdate((DataUpdateType_t)luaL_checkint(L, 2));
  return 0;
}

static int CBaseAnimating_ProcessMuzzleFlashEvent (lua_State *L) {
  luaL_checkanimating(L, 1)->ProcessMuzzleFlashEvent();
  return 0;
}

static int CBaseAnimating_PushAllowBoneAccess (lua_State *L) {
  C_BaseAnimating::PushAllowBoneAccess(luaL_checkboolean(L, 1), luaL_checkboolean(L, 2), luaL_checkstring(L, 3));
  return 0;
}

static int CBaseAnimating_RagdollMoved (lua_State *L) {
  luaL_checkanimating(L, 1)->RagdollMoved();
  return 0;
}

static int CBaseAnimating_Release (lua_State *L) {
  luaL_checkanimating(L, 1)->Release();
  return 0;
}

static int CBaseAnimating_RemoveFromClientSideAnimationList (lua_State *L) {
  luaL_checkanimating(L, 1)->RemoveFromClientSideAnimationList();
  return 0;
}

/*
static int CBaseAnimating_ResetEventsParity (lua_State *L) {
  luaL_checkanimating(L, 1)->ResetEventsParity();
  return 0;
}
*/

static int CBaseAnimating_ResetLatched (lua_State *L) {
  luaL_checkanimating(L, 1)->ResetLatched();
  return 0;
}

static int CBaseAnimating_ResetSequence (lua_State *L) {
  luaL_checkanimating(L, 1)->ResetSequence(luaL_checkint(L, 2));
  return 0;
}

static int CBaseAnimating_ResetSequenceInfo (lua_State *L) {
  luaL_checkanimating(L, 1)->ResetSequenceInfo();
  return 0;
}

static int CBaseAnimating_RetrieveRagdollInfo (lua_State *L) {
  Vector pos;
  Quaternion q;
  lua_pushboolean(L, luaL_checkanimating(L, 1)->RetrieveRagdollInfo(&pos, &q));
  lua_pushvector(L, pos);
  // Todo: implement Quaternion class!!
  // lua_pushquaternion(L, &q);
  return 2;
}

static int CBaseAnimating_SelectWeightedSequence (lua_State *L) {
  lua_pushinteger(L, luaL_checkanimating(L, 1)->SelectWeightedSequence(luaL_checkint(L, 2)));
  return 1;
}

static int CBaseAnimating_SequenceDuration (lua_State *L) {
  if (lua_isnoneornil(L, 2))
    lua_pushnumber(L, luaL_checkanimating(L, 1)->SequenceDuration());
  else
	lua_pushnumber(L, luaL_checkanimating(L, 1)->SequenceDuration(luaL_checkint(L, 2)));
  return 1;
}

static int CBaseAnimating_SequenceLoops (lua_State *L) {
  lua_pushboolean(L, luaL_checkanimating(L, 1)->SequenceLoops());
  return 1;
}

static int CBaseAnimating_SetBodygroup (lua_State *L) {
  luaL_checkanimating(L, 1)->SetBodygroup(luaL_checkint(L, 2), luaL_checkint(L, 3));
  return 0;
}

static int CBaseAnimating_SetBoneController (lua_State *L) {
  lua_pushnumber(L, luaL_checkanimating(L, 1)->SetBoneController(luaL_checkint(L, 2), luaL_checknumber(L, 3)));
  return 1;
}

static int CBaseAnimating_SetCycle (lua_State *L) {
  luaL_checkanimating(L, 1)->SetCycle(luaL_checknumber(L, 2));
  return 0;
}

static int CBaseAnimating_SetHitboxSet (lua_State *L) {
  luaL_checkanimating(L, 1)->SetHitboxSet(luaL_checkint(L, 2));
  return 0;
}

static int CBaseAnimating_SetHitboxSetByName (lua_State *L) {
  luaL_checkanimating(L, 1)->SetHitboxSetByName(luaL_checkstring(L, 2));
  return 0;
}

/*
static int CBaseAnimating_SetModelWidthScale (lua_State *L) {
  luaL_checkanimating(L, 1)->SetModelWidthScale(luaL_checknumber(L, 2));
  return 0;
}
*/

static int CBaseAnimating_SetPlaybackRate (lua_State *L) {
  luaL_checkanimating(L, 1)->SetPlaybackRate(luaL_checknumber(L, 2));
  return 0;
}

static int CBaseAnimating_SetPoseParameter (lua_State *L) {
  C_BaseAnimating *pEntity = luaL_checkanimating(L, 1);
  float flNewValue;
  switch(lua_type(L, 2)) {
	case LUA_TNUMBER:
	  flNewValue = pEntity->SetPoseParameter(luaL_checkint(L, 2), luaL_checknumber(L, 3));
	  break;
	case LUA_TSTRING:
	default:
	  flNewValue = pEntity->SetPoseParameter(luaL_checkstring(L, 2), luaL_checknumber(L, 3));
	  break;
  }
  // HL2SB (2026-10-04) GMod contract: a pose write must be visible to the next
  // bone build.  CBaseAnimating::SetPoseParameter only stores the value -- the
  // per-frame bone cache then kept serving a build made from the poses that
  // were current when it last ran (First Person Body zeroes head/steer poses
  // around a forced SetupBones in its CalcView work and restores them right
  // after; without this the seated model vibrated between the two poses).
  pEntity->InvalidateBoneCache();
  lua_pushnumber(L, flNewValue);
  return 1;
}

static int CBaseAnimating_SetPredictable (lua_State *L) {
  luaL_checkanimating(L, 1)->SetPredictable(luaL_checkboolean(L, 2));
  return 0;
}

static int CBaseAnimating_SetPredictionEligible (lua_State *L) {
  luaL_checkanimating(L, 1)->SetPredictionEligible(luaL_checkboolean(L, 2));
  return 0;
}

static int CBaseAnimating_SetPredictionPlayer (lua_State *L) {
  luaL_checkanimating(L, 1)->SetPredictionPlayer(luaL_checkplayer(L, 2));
  return 0;
}

static int CBaseAnimating_SetReceivedSequence (lua_State *L) {
  luaL_checkanimating(L, 1)->SetReceivedSequence();
  return 0;
}

static int CBaseAnimating_SetSequence (lua_State *L) {
  luaL_checkanimating(L, 1)->SetSequence(luaL_checkint(L, 2));
  return 0;
}

//-----------------------------------------------------------------------------
// HL2SB GMod compat: Entity:TranslatePhysBoneToBone( physBone ) -- the bone
// index a physics bone is attached to (wiki).  The mapping lives in the studio
// bone table (mstudiobone_t::physicsbone); no match answers -1.
//-----------------------------------------------------------------------------
static int CBaseAnimating_TranslatePhysBoneToBone (lua_State *L) {
  C_BaseAnimating *pEntity = luaL_checkanimating( L, 1 );
  int nPhysBone = luaL_checkint( L, 2 );

  CStudioHdr *pStudioHdr = pEntity->GetModelPtr();
  if ( pStudioHdr != NULL ) {
    for ( int i = 0; i < pStudioHdr->numbones(); i++ ) {
      if ( pStudioHdr->pBone( i )->physicsbone == nPhysBone ) {
        lua_pushinteger( L, i );
        return 1;
      }
    }
  }

  lua_pushinteger( L, -1 );
  return 1;
}

// HL2SB GMod compat: Entity:TranslateBoneToPhysBone( bone ) -- the inverse of
// TranslatePhysBoneToBone: which physics bone the model bone belongs to.
// -1 for bones with no physics body (the GMod answer).
static int CBaseAnimating_TranslateBoneToPhysBone (lua_State *L) {
  C_BaseAnimating *pEntity = luaL_checkanimating( L, 1 );
  int nBone = luaL_checkint( L, 2 );

  CStudioHdr *pStudioHdr = pEntity->GetModelPtr();
  if ( pStudioHdr != NULL && nBone >= 0 && nBone < pStudioHdr->numbones() ) {
    lua_pushinteger( L, pStudioHdr->pBone( nBone )->physicsbone );
    return 1;
  }

  lua_pushinteger( L, -1 );
  return 1;
}

static int CBaseAnimating_SetServerIntendedCycle (lua_State *L) {
  luaL_checkanimating(L, 1)->SetServerIntendedCycle(luaL_checknumber(L, 2));
  return 0;
}

static int CBaseAnimating_ShadowCastType (lua_State *L) {
  lua_pushinteger(L, luaL_checkanimating(L, 1)->ShadowCastType());
  return 1;
}

static int CBaseAnimating_ShouldMuzzleFlash (lua_State *L) {
  lua_pushboolean(L, luaL_checkanimating(L, 1)->ShouldMuzzleFlash());
  return 1;
}

static int CBaseAnimating_ShouldResetSequenceOnNewModel (lua_State *L) {
  lua_pushboolean(L, luaL_checkanimating(L, 1)->ShouldResetSequenceOnNewModel());
  return 1;
}

static int CBaseAnimating_ShutdownBoneSetupThreadPool (lua_State *L) {
  CBaseAnimating::ShutdownBoneSetupThreadPool();
  return 0;
}

static int CBaseAnimating_Simulate (lua_State *L) {
  luaL_checkanimating(L, 1)->Simulate();
  return 0;
}

static int CBaseAnimating_StudioFrameAdvance (lua_State *L) {
  luaL_checkanimating(L, 1)->StudioFrameAdvance();
  return 0;
}

static int CBaseAnimating_ThreadedBoneSetup (lua_State *L) {
  CBaseAnimating::ThreadedBoneSetup();
  return 0;
}

static int CBaseAnimating_TransferDissolveFrom (lua_State *L) {
  luaL_checkanimating(L, 1)->TransferDissolveFrom(luaL_checkanimating(L, 2));
  return 0;
}

static int CBaseAnimating_UncorrectViewModelAttachment (lua_State *L) {
  luaL_checkanimating(L, 1)->UncorrectViewModelAttachment(luaL_checkvector(L, 2));
  return 0;
}

static int CBaseAnimating_UpdateClientSideAnimation (lua_State *L) {
  luaL_checkanimating(L, 1)->UpdateClientSideAnimation();
  return 0;
}

static int CBaseAnimating_UpdateClientSideAnimations (lua_State *L) {
  CBaseAnimating::UpdateClientSideAnimations();
  return 0;
}

static int CBaseAnimating_UpdateIKLocks (lua_State *L) {
  luaL_checkanimating(L, 1)->UpdateIKLocks(luaL_checknumber(L, 2));
  return 0;
}

static int CBaseAnimating_UseClientSideAnimation (lua_State *L) {
  luaL_checkanimating(L, 1)->UseClientSideAnimation();
  return 0;
}

static int CBaseAnimating_UsesPowerOfTwoFrameBufferTexture (lua_State *L) {
  lua_pushboolean(L, luaL_checkanimating(L, 1)->UsesPowerOfTwoFrameBufferTexture());
  return 1;
}

static int CBaseAnimating_VPhysicsGetObjectList (lua_State *L) {
  IPhysicsObject *pList[VPHYSICS_MAX_OBJECT_LIST_COUNT];
  int count = luaL_checkanimating(L, 1)->VPhysicsGetObjectList( pList, ARRAYSIZE(pList) );
  lua_pushinteger(L, count);
  lua_newtable(L);
  for( int i = 0 ; i < count ; i++ )
  {
	  lua_pushinteger(L, i);
	  lua_pushphysicsobject(L, pList[i]);
	  lua_settable(L, -3);
  }
  return 2;
}

static int CBaseAnimating_VPhysicsUpdate (lua_State *L) {
  luaL_checkanimating(L, 1)->VPhysicsUpdate(luaL_checkphysicsobject(L, 2));
  return 0;
}

// HL2SB GMod compat: on the CLIENT a Lua nextbot has no per-instance script table
// (m_nTableReference is taken on the server; the client entity is a plain
// C_NextBotCombatCharacter built from the network).  So a script that reaches for
// `self.Entity`, or calls one of its own hooks - base_nextbot's default
// ENTITY:DrawTranslucent does `self:Draw( flags )` - resolved to nil, and calling
// that nil aborted the game ("attempt to call a nil value" with an EMPTY traceback,
// because the lookup happened in C, outside any pcall).
//
// Give back the two things GMod's ENTITY tables provide:
//   "Entity"        -> the entity itself (GMod's ENT.Entity is the same object)
//   anything else   -> the script's class table, scripted_ents.GetStored( c ).t
//
// The class-table read goes through a protected call on purpose: a script table can
// carry an __index FUNCTION, and an error raised by a bare lua_gettable from C is
// unprotected - it lands in lua_atpanic and kills the process.
static int LuaGetTableKey (lua_State *L) {  /* [t][k] -> [t[k]] */
  lua_gettable(L, 1);
  return 1;
}

static void LuaPushScriptedEntityField ( lua_State *L, const char *pszClassname, const char *pszKey )
{
  if ( pszClassname == NULL || pszClassname[0] == '\0' || pszKey == NULL )
  {
    lua_pushnil( L );
    return;
  }

  lua_getglobal( L, "scripted_ents" );
  if ( !lua_istable( L, -1 ) ) { lua_pop( L, 1 ); lua_pushnil( L ); return; }

  lua_getfield( L, -1, "GetStored" );
  if ( !lua_isfunction( L, -1 ) ) { lua_pop( L, 2 ); lua_pushnil( L ); return; }

  lua_pushstring( L, pszClassname );
  if ( luasrc_pcall( L, 1, 1, 0 ) != 0 || !lua_istable( L, -1 ) ) { lua_pop( L, 3 ); lua_pushnil( L ); return; }

  lua_getfield( L, -1, "t" );                       // stored.t
  if ( !lua_istable( L, -1 ) ) { lua_pop( L, 4 ); lua_pushnil( L ); return; }
  lua_remove( L, -2 );                              // stored
  lua_remove( L, -2 );                              // GetStored
  lua_remove( L, -2 );                              // scripted_ents   -> [t]

  lua_pushcfunction( L, LuaGetTableKey );
  lua_pushvalue( L, -2 );                           // t
  lua_pushstring( L, pszKey );
  if ( luasrc_pcall( L, 2, 1, 0 ) != 0 )
    lua_pushnil( L );
  lua_remove( L, -2 );                              // leave only the value
}

static int CBaseAnimating___index (lua_State *L) {
  CBaseAnimating *pEntity = lua_toanimating(L, 1);
  if (pEntity == NULL) {  /* avoid extra test when d is not 0 */
    /* HL2SB: GMod's NULL sentinel answers reads instead of raising -- same
    ** contract as CBaseEntity___index / CBasePlayer___index. */
    HL2SB_PushNullEntityIndex( L, lua_tostring( L, 2 ) );
    return 1;
  }
  /* HL2SB (2026-10-07): self.Owner for ENT methods.  Mirrors the server copy
  ** in lbaseanimating.cpp -- keep the two in sync.  GMod answers self.Owner
  ** with the holder entity; a script-table Owner field wins first. */
  if ( lua_tostring( L, 2 ) != NULL && Q_stricmp( lua_tostring( L, 2 ), "Owner" ) == 0 )
  {
    if ( lua_isrefvalid( L, pEntity->m_nTableReference ) )
    {
      lua_getref( L, pEntity->m_nTableReference );
      lua_getfield( L, -1, "Owner" );
      if ( !lua_isnil( L, -1 ) )
      {
        lua_remove( L, -2 );   /* instance table out, the value stays on top */
        return 1;
      }
      lua_pop( L, 2 );         /* no script Owner -- fall through to the C entity */
    }
    CBaseEntity::PushLuaInstanceSafe( L, pEntity->GetOwnerEntity() );
    return 1;
  }
  if (lua_isrefvalid(L, pEntity->m_nTableReference)) {
    // HL2SB (2026-09-20): SCRIPT-TABLE FUNCTIONS are overrides and win over
    // C++ methods; a script-table DATA (non-function) field never shadows a
    // C++ method.  Same rule as the server copy in lbaseanimating.cpp -- keep
    // the two in sync.  Raw reads: lua_gettable on the metatable would
    // re-enter this __index through its own __index field and answer
    // script-table fields with the NULL-sentinel method.
    lua_getref(L, pEntity->m_nTableReference);
    lua_pushvalue(L, 2);
    lua_rawget(L, -2);
    if (lua_isfunction(L, -1))
      return 1;                    // Lua override beats everything
    lua_pop(L, 2);

    lua_getmetatable(L, 1);
    lua_pushvalue(L, 2);
    lua_rawget(L, -2);
    if (lua_isfunction(L, -1))
      return 1;                    // C++ method beats a data field
    lua_pop(L, 2);

    luaL_getmetatable(L, LUA_BASEANIMATINGLIBNAME);
    lua_pushvalue(L, 2);
    lua_rawget(L, -2);
    if (lua_isfunction(L, -1))
      return 1;
    lua_pop(L, 2);

    luaL_getmetatable(L, "CBaseEntity");
    lua_pushvalue(L, 2);
    lua_rawget(L, -2);
    if (lua_isfunction(L, -1))
      return 1;
    lua_pop(L, 2);

    // script-table data value
    lua_getref(L, pEntity->m_nTableReference);
    lua_pushvalue(L, 2);
    lua_rawget(L, -2);
    // falls through to the legacy self-reference / scripted-field tail below
  }
  else {
    lua_getmetatable(L, 1);
    lua_pushvalue(L, 2);
    lua_gettable(L, -2);
    if (lua_isnil(L, -1)) {
      lua_pop(L, 2);

      /* the same CBaseAnimating-metatable step as above */
      luaL_getmetatable(L, LUA_BASEANIMATINGLIBNAME);
      lua_pushvalue(L, 2);
      lua_gettable(L, -2);
      if (lua_isnil(L, -1)) {
        lua_pop(L, 2);
        luaL_getmetatable(L, "CBaseEntity");
        lua_pushvalue(L, 2);
        lua_gettable(L, -2);
      }
    }
  }
  // HL2SB GMod compat: nothing in the entity's own table and nothing in the
  // bindings -> try the script's class table (see LuaPushScriptedEntityField),
  // and answer GMod's deprecated self-reference fields ("Entity", and the
  // SWEP spelling "Weapon") with the entity itself, like GMod does.  Reaching
  // here with a bound table now works too: the script-table-data read above
  // falls through when it produced nil (2026-09-21).
  if ( lua_isnil( L, -1 ) )
  {
    const char *pszKey = lua_tostring( L, 2 );
    lua_pop( L, 1 );

    if ( pszKey != NULL )
    {
      if ( Q_strcmp( pszKey, "Entity" ) == 0 || Q_strcmp( pszKey, "Weapon" ) == 0 )
      {
        lua_pushvalue( L, 1 );        // self.Entity == self, like GMod's ENT.Entity
        return 1;
      }

      LuaPushScriptedEntityField( L, pEntity->GetClassname(), pszKey );
      return 1;
    }
  }
  return 1;
}

static int CBaseAnimating___newindex (lua_State *L) {
  CBaseAnimating *pEntity = lua_toanimating(L, 1);
  if (pEntity == NULL) {
    /* HL2SB (2026-10-03): field writes on the NULL sentinel are silently
    ** discarded (reference behaviour; see CBaseEntity___newindex). */
    return 0;
  }
  const char *field = luaL_checkstring(L, 2);
  if (Q_strcmp(field, "m_bClientSideAnimation") == 0)
    pEntity->m_bClientSideAnimation = luaL_checkboolean(L, 3);
  else if (Q_strcmp(field, "m_bLastClientSideFrameReset") == 0)
    pEntity->m_bLastClientSideFrameReset = luaL_checkboolean(L, 3);
  else if (Q_strcmp(field, "m_nBody") == 0)
    pEntity->m_nBody = luaL_checkint(L, 3);
  else if (Q_strcmp(field, "m_nHitboxSet") == 0)
    pEntity->m_nHitboxSet = luaL_checkint(L, 3);
  else if (Q_strcmp(field, "m_nSkin") == 0)
    pEntity->m_nSkin = luaL_checkint(L, 3);
  else {
    // HL2SB: < 0, not == LUA_NOREF -- LUA_REFNIL (-1) is a legal "no table"
    // state; the old test let lua_getref(-1) push nil and silently drop the
    // field write (see CBaseEntity___newindex).
    if (pEntity->m_nTableReference < 0) {
      // HL2SB (2026-10-04) diagnostic: a field write on a scripted entity that
      // has a networked class but no bound table yet is what used to burn the
      // one-shot class bind (the SP gmod_hands regression).  The OnDataChanged
      // gate now heals, but name the writer once per entity anyway -- it pins
      // down WHICH addon/hook writes before OnDataChanged if the heal ever
      // misfires.
      C_BaseScripted *pScripted = dynamic_cast<C_BaseScripted *>(pEntity);
      if ( pScripted != NULL && pScripted->GetScriptedClassname() != NULL &&
           pScripted->GetScriptedClassname()[ 0 ] != '\0' )
      {
        static int s_nPreBindWrites = 0;
        if ( s_nPreBindWrites < 40 )
        {
          ++s_nPreBindWrites;
          Warning( "[HL2SB] pre-bind field write '%s' on scripted entity ent=%d (class '%s') -- auto-table allocated before OnDataChanged\n",
                   field, pEntity->entindex(), pScripted->GetScriptedClassname() );
        }
      }
      lua_newtable(L);
      pEntity->m_nTableReference = luaL_ref(L, LUA_REGISTRYINDEX);
    }
    lua_getref(L, pEntity->m_nTableReference);
    lua_pushvalue(L, 3);
    lua_setfield(L, -2, field);
	lua_pop(L, 1);
  }
  return 0;
}

static int CBaseAnimating___eq (lua_State *L) {
  lua_pushboolean(L, lua_toanimating(L, 1) == lua_toanimating(L, 2));
  return 1;
}

static int CBaseAnimating___tostring (lua_State *L) {
  CBaseAnimating *pEntity = lua_toanimating(L, 1);
  if (pEntity == NULL)
    lua_pushstring(L, "NULL");
  else
    lua_pushfstring(L, "CBaseAnimating: %d \"%s\"", pEntity->entindex(), pEntity->GetClassname());
  return 1;
}


static const luaL_Reg CBaseAnimatingmeta[] = {
  {"AddCallback", CBaseAnimating_AddCallback},
  {"AddEntity", CBaseAnimating_AddEntity},
  {"AddToClientSideAnimationList", CBaseAnimating_AddToClientSideAnimationList},
  {"BecomeRagdollOnClient", CBaseAnimating_BecomeRagdollOnClient},
  {"CalculateIKLocks", CBaseAnimating_CalculateIKLocks},
  {"ClampCycle", CBaseAnimating_ClampCycle},
  {"Clear", CBaseAnimating_Clear},
  {"ClearRagdoll", CBaseAnimating_ClearRagdoll},
  {"ClientSideAnimationChanged", CBaseAnimating_ClientSideAnimationChanged},
  {"ComputeClientSideAnimationFlags", CBaseAnimating_ComputeClientSideAnimationFlags},
  {"ComputeEntitySpaceHitboxSurroundingBox", CBaseAnimating_ComputeEntitySpaceHitboxSurroundingBox},
  {"ComputeHitboxSurroundingBox", CBaseAnimating_ComputeHitboxSurroundingBox},
  {"CreateRagdollCopy", CBaseAnimating_CreateRagdollCopy},
  {"CreateUnragdollInfo", CBaseAnimating_CreateUnragdollInfo},
  {"DisableMuzzleFlash", CBaseAnimating_DisableMuzzleFlash},
  {"DispatchMuzzleEffect", CBaseAnimating_DispatchMuzzleEffect},
  {"DoMuzzleFlash", CBaseAnimating_DoMuzzleFlash},
  {"DrawClientHitboxes", CBaseAnimating_DrawClientHitboxes},
  {"DrawModel", CBaseAnimating_DrawModel},
  // HL2SB GMod compat: bone accessors (minecraft SWEP world model).
  {"CreateShadow", CBaseAnimating_CreateShadow},
  {"DestroyShadow", CBaseAnimating_DestroyShadow},
  {"GetBoneCount", CBaseAnimating_GetBoneCount},
  {"GetBoneMatrix", CBaseAnimating_GetBoneMatrix},
  {"GetBoneName", CBaseAnimating_GetBoneName},
  {"GetBoneNames", CBaseAnimating_GetBoneNames},
  {"GetBoneParent", CBaseAnimating_GetBoneParent},
  {"GetManipulateBoneScale", CBaseAnimating_GetManipulateBoneScale},
  {"GetManipulateBoneAngles", CBaseAnimating_GetManipulateBoneAngles},
  {"GetManipulateBonePosition", CBaseAnimating_GetManipulateBonePosition},
  {"GetCallbacks", CBaseAnimating_GetCallbacks},
  {"GetChildBones", CBaseAnimating_GetChildBones},
  {"GetFlexNum", CBaseAnimating_GetFlexNum},
  {"GetFlexIDByName", CBaseAnimating_GetFlexIDByName},
  {"GetFlexScale", CBaseAnimating_GetFlexScale},
  {"GetFlexWeight", CBaseAnimating_GetFlexWeight},
  {"GetModelRenderBounds", CBaseAnimating_GetModelRenderBounds},
  {"GetNumPoseParameters", CBaseAnimating_GetNumPoseParameters},
  {"ManipulateBoneScale", CBaseAnimating_ManipulateBoneScale},
  {"ManipulateBoneAngles", CBaseAnimating_ManipulateBoneAngles},
  {"FindBodygroupByName", CBaseAnimating_FindBodygroupByName},
  {"FindFollowedEntity", CBaseAnimating_FindFollowedEntity},
  {"FindTransitionSequence", CBaseAnimating_FindTransitionSequence},
  {"FireEvent", CBaseAnimating_FireEvent},
  {"FireObsoleteEvent", CBaseAnimating_FireObsoleteEvent},
  {"ForceClientSideAnimationOn", CBaseAnimating_ForceClientSideAnimationOn},
  {"FrameAdvance", CBaseAnimating_FrameAdvance},
  {"GetAimEntOrigin", CBaseAnimating_GetAimEntOrigin},
  {"GetAnimTimeInterval", CBaseAnimating_GetAnimTimeInterval},
  {"GetAttachment", CBaseAnimating_GetAttachment},
  {"GetAttachmentLocal", CBaseAnimating_GetAttachmentLocal},
  {"GetAttachmentVelocity", CBaseAnimating_GetAttachmentVelocity},
  {"GetBaseAnimating", CBaseAnimating_GetBaseAnimating},
  {"GetBlendedLinearVelocity", CBaseAnimating_GetBlendedLinearVelocity},
  {"GetBody", CBaseAnimating_GetBody},
  {"GetBodygroup", CBaseAnimating_GetBodygroup},
  {"GetBodygroups", CBaseAnimating_GetBodygroups},
  {"GetBodygroupCount", CBaseAnimating_GetBodygroupCount},
  {"GetBodygroupName", CBaseAnimating_GetBodygroupName},
  {"GetBoneControllers", CBaseAnimating_GetBoneControllers},
  {"GetBonePosition", CBaseAnimating_GetBonePosition},
  {"GetClientSideFade", CBaseAnimating_GetClientSideFade},
  {"GetCollideType", CBaseAnimating_GetCollideType},
  {"GetCycle", CBaseAnimating_GetCycle},
  {"GetFlexControllerName", CBaseAnimating_GetFlexControllerName},
  {"GetFlexControllerType", CBaseAnimating_GetFlexControllerType},
  {"GetFlexDescFacs", CBaseAnimating_GetFlexDescFacs},
  {"GetHitboxSet", CBaseAnimating_GetHitboxSet},
  {"GetHitboxSetCount", CBaseAnimating_GetHitboxSetCount},
  {"GetHitboxSetName", CBaseAnimating_GetHitboxSetName},
//  {"GetModelWidthScale", CBaseAnimating_GetModelWidthScale},
  {"GetNumBodyGroups", CBaseAnimating_GetNumBodyGroups},
  {"GetNumFlexControllers", CBaseAnimating_GetNumFlexControllers},
  {"GetPlaybackRate", CBaseAnimating_GetPlaybackRate},
  {"GetPoseParameter", CBaseAnimating_GetPoseParameter},
  {"GetPoseParameterRange", CBaseAnimating_GetPoseParameterRange},
  {"GetRenderAngles", CBaseAnimating_GetRenderAngles},
  {"GetRenderBounds", CBaseAnimating_GetRenderBounds},
  {"GetRenderOrigin", CBaseAnimating_GetRenderOrigin},
  {"GetSequence", CBaseAnimating_GetSequence},
  {"GetSequenceActivity", CBaseAnimating_GetSequenceActivity},
  {"GetSequenceActivityName", CBaseAnimating_GetSequenceActivityName},
  {"GetSequenceGroundSpeed", CBaseAnimating_GetSequenceGroundSpeed},
  {"GetSequenceLinearMotion", CBaseAnimating_GetSequenceLinearMotion},
  {"GetSequenceName", CBaseAnimating_GetSequenceName},
  {"GetServerIntendedCycle", CBaseAnimating_GetServerIntendedCycle},
  {"GetSkin", CBaseAnimating_GetSkin},
  {"IgniteRagdoll", CBaseAnimating_IgniteRagdoll},
  {"InitBoneSetupThreadPool", CBaseAnimating_InitBoneSetupThreadPool},
  {"InitModelEffects", CBaseAnimating_InitModelEffects},
  {"InternalDrawModel", CBaseAnimating_InternalDrawModel},
  {"Interpolate", CBaseAnimating_Interpolate},
  {"InvalidateBoneCache", CBaseAnimating_InvalidateBoneCache},
  {"InvalidateBoneCaches", CBaseAnimating_InvalidateBoneCaches},
  {"InvalidateMdlCache", CBaseAnimating_InvalidateMdlCache},
  {"IsActivityFinished", CBaseAnimating_IsActivityFinished},
  {"IsBoneCacheValid", CBaseAnimating_IsBoneCacheValid},
  {"IsOnFire", CBaseAnimating_IsOnFire},
  {"IsRagdoll", CBaseAnimating_IsRagdoll},
  {"IsSelfAnimating", CBaseAnimating_IsSelfAnimating},
  {"IsSequenceFinished", CBaseAnimating_IsSequenceFinished},
  {"IsSequenceLooping", CBaseAnimating_IsSequenceLooping},
  {"IsViewModel", CBaseAnimating_IsViewModel},
  {"LookupActivity", CBaseAnimating_LookupActivity},
  {"LookupAttachment", CBaseAnimating_LookupAttachment},
  {"LookupBone", CBaseAnimating_LookupBone},
  {"LookupPoseParameter", CBaseAnimating_LookupPoseParameter},
  {"LookupRandomAttachment", CBaseAnimating_LookupRandomAttachment},
  {"LookupSequence", CBaseAnimating_LookupSequence},
  {"NotifyShouldTransmit", CBaseAnimating_NotifyShouldTransmit},
  {"OnDataChanged", CBaseAnimating_OnDataChanged},
  {"OnPreDataChanged", CBaseAnimating_OnPreDataChanged},
  {"PopBoneAccess", CBaseAnimating_PopBoneAccess},
  {"PostDataUpdate", CBaseAnimating_PostDataUpdate},
  {"PreDataUpdate", CBaseAnimating_PreDataUpdate},
  {"ProcessMuzzleFlashEvent", CBaseAnimating_ProcessMuzzleFlashEvent},
  {"PushAllowBoneAccess", CBaseAnimating_PushAllowBoneAccess},
  {"RagdollMoved", CBaseAnimating_RagdollMoved},
  {"Release", CBaseAnimating_Release},
  {"RemoveFromClientSideAnimationList", CBaseAnimating_RemoveFromClientSideAnimationList},
  {"RemoveCallback", CBaseAnimating_RemoveCallback},
//  {"ResetEventsParity", CBaseAnimating_ResetEventsParity},
  {"ResetLatched", CBaseAnimating_ResetLatched},
  {"ResetSequence", CBaseAnimating_ResetSequence},
  {"ResetSequenceInfo", CBaseAnimating_ResetSequenceInfo},
  {"RetrieveRagdollInfo", CBaseAnimating_RetrieveRagdollInfo},
  {"SelectWeightedSequence", CBaseAnimating_SelectWeightedSequence},
  {"SequenceDuration", CBaseAnimating_SequenceDuration},
  {"SequenceLoops", CBaseAnimating_SequenceLoops},
  {"SetBodygroup", CBaseAnimating_SetBodygroup},
  {"SetBoneController", CBaseAnimating_SetBoneController},
  {"SetBoneMatrix", CBaseAnimating_SetBoneMatrix},
  {"SetBonePosition", CBaseAnimating_SetBonePosition},
  {"SetRenderClipPlane", CBaseAnimating_SetRenderClipPlane},
  {"SetRenderClipPlaneEnabled", CBaseAnimating_SetRenderClipPlaneEnabled},
  {"SetFlexScale", CBaseAnimating_SetFlexScale},
  {"SetFlexWeight", CBaseAnimating_SetFlexWeight},
  {"GetPoseParameterName", CBaseAnimating_GetPoseParameterName},
  {"ManipulateBonePosition", CBaseAnimating_ManipulateBonePosition},
  {"SetLOD", CBaseAnimating_SetLOD},
  {"SetupBones", CBaseAnimating_SetupBones},
  {"TranslatePhysBoneToBone", CBaseAnimating_TranslatePhysBoneToBone},
  {"TranslateBoneToPhysBone", CBaseAnimating_TranslateBoneToPhysBone},
  {"SetCycle", CBaseAnimating_SetCycle},
  {"SetHitboxSet", CBaseAnimating_SetHitboxSet},
  {"SetHitboxSetByName", CBaseAnimating_SetHitboxSetByName},
 // {"SetModelWidthScale", CBaseAnimating_SetModelWidthScale},
  {"SetPlaybackRate", CBaseAnimating_SetPlaybackRate},
  {"SetPoseParameter", CBaseAnimating_SetPoseParameter},
  {"SetPredictable", CBaseAnimating_SetPredictable},
  {"SetPredictionEligible", CBaseAnimating_SetPredictionEligible},
  {"SetPredictionPlayer", CBaseAnimating_SetPredictionPlayer},
  {"SetReceivedSequence", CBaseAnimating_SetReceivedSequence},
  {"SetSequence", CBaseAnimating_SetSequence},
  {"SetServerIntendedCycle", CBaseAnimating_SetServerIntendedCycle},
  {"ShadowCastType", CBaseAnimating_ShadowCastType},
  {"ShouldMuzzleFlash", CBaseAnimating_ShouldMuzzleFlash},
  {"ShouldResetSequenceOnNewModel", CBaseAnimating_ShouldResetSequenceOnNewModel},
  {"ShutdownBoneSetupThreadPool", CBaseAnimating_ShutdownBoneSetupThreadPool},
  {"Simulate", CBaseAnimating_Simulate},
  {"StudioFrameAdvance", CBaseAnimating_StudioFrameAdvance},
  {"ThreadedBoneSetup", CBaseAnimating_ThreadedBoneSetup},
  {"TransferDissolveFrom", CBaseAnimating_TransferDissolveFrom},
  {"UncorrectViewModelAttachment", CBaseAnimating_UncorrectViewModelAttachment},
  {"UpdateClientSideAnimation", CBaseAnimating_UpdateClientSideAnimation},
  {"UpdateClientSideAnimations", CBaseAnimating_UpdateClientSideAnimations},
  {"UpdateIKLocks", CBaseAnimating_UpdateIKLocks},
  {"UseClientSideAnimation", CBaseAnimating_UseClientSideAnimation},
  {"UsesPowerOfTwoFrameBufferTexture", CBaseAnimating_UsesPowerOfTwoFrameBufferTexture},
  {"VPhysicsGetObjectList", CBaseAnimating_VPhysicsGetObjectList},
  {"VPhysicsUpdate", CBaseAnimating_VPhysicsUpdate},
  {"__index", CBaseAnimating___index},
  {"__newindex", CBaseAnimating___newindex},
  {"__eq", CBaseAnimating___eq},
  {"__tostring", CBaseAnimating___tostring},
  {NULL, NULL}
};


/*
** Open CBaseAnimating object
*/
LUALIB_API int luaopen_CBaseAnimating (lua_State *L) {
  luaL_newmetatable(L, LUA_BASEANIMATINGLIBNAME);
  luaL_register(L, NULL, CBaseAnimatingmeta);
  lua_pushstring(L, "entity");
  lua_setfield(L, -2, "__type");  /* metatable.__type = "entity" */
  return 1;
}

