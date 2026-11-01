//===== Copyright © 1996-2005, Valve Corporation, All rights reserved. ======//
//
// Purpose: 
//
// $NoKeywords: $
//===========================================================================//
#define lc_baseentity_cpp

#include "cbase.h"
#include "basecombatweapon_shared.h"
#include "luamanager.h"
#include "luasrclib.h"
#include "lbaseentity_shared.h"
#include "mathlib/lvector.h"
#include "model_types.h"	// HL2SB: STUDIO_RENDER, the default of Entity:DrawModel()
#include "c_baseanimating.h"
#include "clientleafsystem.h"
#include "basescripted.h"
#include "c_fire_smoke.h"	// HL2SB: Entity:IsOnFire (C_EntityFlame = the networked flame)
#include "view.h"			// HL2SB: MainViewOrigin/MainViewAngles (GUIMousePressed ray)
#include "view_scene.h"		// HL2SB: CViewSetup (the picking ray uses the live FOV)
#include "particles_new.h"	// HL2SB: Entity:CreateParticleEffect (CNewParticleEffect)
#include "particle_parse.h"	// HL2SB: ParticleAttachment_t
#include "lnewparticle.h"	// HL2SB: HL2SB_PushNewParticleEffect
#include <vgui/IInput.h>
#include <vgui_controls/Controls.h>
#include <vgui_controls/lPanel.h>

// memdbgon must be the last include file in a .cpp file!!!
#include "tier0/memdbgon.h"


static int CBaseEntity_SpawnClientEntity (lua_State *L) {
  luaL_checkentity(L, 1)->SpawnClientEntity();
  return 0;
}

static int CBaseEntity_Interp_HierarchyUpdateInterpolationAmounts (lua_State *L) {
  luaL_checkentity(L, 1)->Interp_HierarchyUpdateInterpolationAmounts();
  return 0;
}

static int CBaseEntity_Init (lua_State *L) {
  lua_pushboolean(L, luaL_checkentity(L, 1)->Init(luaL_checkint(L, 2), luaL_checkint(L, 3)));
  return 1;
}

static int CBaseEntity_Term (lua_State *L) {
  luaL_checkentity(L, 1)->Term();
  return 0;
}

static int CBaseEntity_EnableInToolView (lua_State *L) {
  luaL_checkentity(L, 1)->EnableInToolView(luaL_checkboolean(L, 2));
  return 0;
}

static int CBaseEntity_IsEnabledInToolView (lua_State *L) {
  lua_pushboolean(L, luaL_checkentity(L, 1)->IsEnabledInToolView());
  return 1;
}

static int CBaseEntity_SetToolRecording (lua_State *L) {
  luaL_checkentity(L, 1)->SetToolRecording(luaL_checkboolean(L, 2));
  return 0;
}

static int CBaseEntity_IsToolRecording (lua_State *L) {
  lua_pushboolean(L, luaL_checkentity(L, 1)->IsToolRecording());
  return 1;
}

static int CBaseEntity_HasRecordedThisFrame (lua_State *L) {
  lua_pushboolean(L, luaL_checkentity(L, 1)->HasRecordedThisFrame());
  return 1;
}

static int CBaseEntity_RecordToolMessage (lua_State *L) {
  luaL_checkentity(L, 1)->RecordToolMessage();
  return 0;
}

static int CBaseEntity_DontRecordInTools (lua_State *L) {
  luaL_checkentity(L, 1)->DontRecordInTools();
  return 0;
}

static int CBaseEntity_ShouldRecordInTools (lua_State *L) {
  lua_pushboolean(L, luaL_checkentity(L, 1)->ShouldRecordInTools());
  return 1;
}

static int CBaseEntity_Release (lua_State *L) {
  luaL_checkentity(L, 1)->Release();
  return 0;
}

static int CBaseEntity_GetRenderOrigin (lua_State *L) {
  Vector v = luaL_checkentity(L, 1)->GetRenderOrigin();
  lua_pushvector(L, v);
  return 1;
}

static int CBaseEntity_GetRenderAngles (lua_State *L) {
  QAngle v = luaL_checkentity(L, 1)->GetRenderAngles();
  lua_pushangle(L, v);
  return 1;
}

static int CBaseEntity_GetObserverCamOrigin (lua_State *L) {
  Vector v = luaL_checkentity(L, 1)->GetObserverCamOrigin();
  lua_pushvector(L, v);
  return 1;
}

static int CBaseEntity_IsTwoPass (lua_State *L) {
  lua_pushboolean(L, luaL_checkentity(L, 1)->IsTwoPass());
  return 1;
}

static int CBaseEntity_UsesPowerOfTwoFrameBufferTexture (lua_State *L) {
  lua_pushboolean(L, luaL_checkentity(L, 1)->UsesPowerOfTwoFrameBufferTexture());
  return 1;
}

static int CBaseEntity_UsesFullFrameBufferTexture (lua_State *L) {
  lua_pushboolean(L, luaL_checkentity(L, 1)->UsesFullFrameBufferTexture());
  return 1;
}

static int CBaseEntity_DrawModel (lua_State *L) {
  C_BaseEntity *pEntity = luaL_checkentity(L, 1);
  int nFlags = luaL_optint(L, 2, STUDIO_RENDER);

  // GMod Entity:DrawModel is a direct native model draw for every entity.
  // Scripted ENT:Draw and RenderOverride callbacks are dispatched by the
  // renderer through CBaseScripted::DrawModel, not by this Lua binding.

  // Studio drawing cannot consume a brush or sprite model header.
  const model_t *pModel = pEntity->GetModel();
  if ( pModel == NULL || modelinfo->GetModelType( pModel ) != mod_studio )
  {
    lua_pushinteger( L, 0 );
    return 1;
  }

  int nResult = 0;
  C_BaseAnimating *pAnim = dynamic_cast<C_BaseAnimating *>(pEntity);
  if (pAnim != NULL)
    nResult = pAnim->InternalDrawModel(nFlags);
  else
    nResult = pEntity->DrawModel(nFlags);

  lua_pushinteger(L, nResult);
  return 1;
}

// HL2SB GMod compat: Entity:SetRenderBounds( mins, maxs, add ).
//
// Wiki: "Sets the render bounds for the entity", and the third argument (default
// Vector( 0, 0, 0 )) "adds this vector to maxs and subtracts this vector from mins".
// Client only, like the wiki says.  There was no such method anywhere in this fork's
// entity code, so a scripted entity that draws itself (npc_verity draws a 128x128
// sprite 64 units above its origin, with no model at all) could not widen the box
// the renderer culls against.
extern void HL2SB_SetClientParentPhysicsNumber( C_BaseEntity *entity, int number );

static int CBaseEntity_SetParentPhysNum (lua_State *L) {
  C_BaseEntity *entity = luaL_checkentity(L, 1);
  HL2SB_SetClientParentPhysicsNumber(entity, (int)luaL_checknumber(L, 2));
  return 0;
}

static int CBaseEntity_SetRenderBounds (lua_State *L) {
  C_BaseEntity *pEntity = luaL_checkentity(L, 1);
  Vector mins = luaL_checkvector(L, 2);
  Vector maxs = luaL_checkvector(L, 3);
  Vector add( 0.0f, 0.0f, 0.0f );
  if ( luaL_testudata( L, 4, LUA_VECTORLIBNAME ) )
    add = lua_tovector( L, 4 );

  Vector lower, upper;
  VectorMin( mins, maxs, lower );
  VectorMax( mins, maxs, upper );
  pEntity->SetScriptedRenderBounds( lower - add, upper + add );
  if ( pEntity->GetRenderHandle() != INVALID_CLIENT_RENDER_HANDLE )
    ClientLeafSystem()->RenderableChanged( pEntity->GetRenderHandle() );
  return 0;
}

// HL2SB GMod compat: Entity:SetRenderBoundsWS( mins, maxs, add ) -- world-space
// variant (wiki). Converts both world-space points to entity-local space.
static int CBaseEntity_SetRenderBoundsWS (lua_State *L) {
  C_BaseEntity *pEntity = luaL_checkentity(L, 1);
  Vector mins = luaL_checkvector(L, 2);
  Vector maxs = luaL_checkvector(L, 3);
  Vector add( 0.0f, 0.0f, 0.0f );
  if ( luaL_testudata( L, 4, LUA_VECTORLIBNAME ) )
    add = lua_tovector( L, 4 );

  Vector localMins, localMaxs, lower, upper;
  const matrix3x4_t &transform = pEntity->EntityToWorldTransform();
  VectorITransform( mins, transform, localMins );
  VectorITransform( maxs, transform, localMaxs );
  VectorMin( localMins, localMaxs, lower );
  VectorMax( localMins, localMaxs, upper );
  pEntity->SetScriptedRenderBounds( lower - add, upper + add );
  if ( pEntity->GetRenderHandle() != INVALID_CLIENT_RENDER_HANDLE )
    ClientLeafSystem()->RenderableChanged( pEntity->GetRenderHandle() );
  return 0;
}

// HL2SB GMod compat: Entity:EnableMatrix( matrixType, matrix ).
//
// Reference behaviour: the only supported matrixType is "RenderMultiply" -
// any other string is silently ignored and changes nothing.  When the third
// argument is not a matrix the stored matrix is CLEARED rather than set, and
// the method returns no values.  The renderer consumes the stored matrix in
// C_BaseAnimating::InternalDrawModel; the reference engine has no
// server-side entry for either method, so both bindings are client only.
static int CBaseEntity_EnableMatrix (lua_State *L) {
  C_BaseEntity *pEntity = luaL_checkentity(L, 1);
  const char *pszType = luaL_checkstring(L, 2);
  if (Q_strcmp(pszType, "RenderMultiply") != 0)
    return 0;
  extern void HL2SB_RenderMatrix_Set( C_BaseEntity *pEntity, const VMatrix *pMatrix );
  HL2SB_RenderMatrix_Set( pEntity, (const VMatrix *)luaL_testudata(L, 3, "VMatrix") );
  return 0;
}

// HL2SB GMod compat: Entity:DisableMatrix( matrixType ) - clears whatever
// Entity:EnableMatrix stored for this entity.  Same string gate.
static int CBaseEntity_DisableMatrix (lua_State *L) {
  C_BaseEntity *pEntity = luaL_checkentity(L, 1);
  const char *pszType = luaL_checkstring(L, 2);
  if (Q_strcmp(pszType, "RenderMultiply") != 0)
    return 0;
  extern void HL2SB_RenderMatrix_Clear( C_BaseEntity *pEntity );
  HL2SB_RenderMatrix_Clear( pEntity );
  return 0;
}

static int CBaseEntity_ComputeFxBlend (lua_State *L) {
  luaL_checkentity(L, 1)->ComputeFxBlend();
  return 0;
}

static int CBaseEntity_GetFxBlend (lua_State *L) {
  lua_pushinteger(L, luaL_checkentity(L, 1)->GetFxBlend());
  return 1;
}

static int CBaseEntity_LODTest (lua_State *L) {
  lua_pushboolean(L, luaL_checkentity(L, 1)->LODTest());
  return 1;
}

static int CBaseEntity_SetNextClientThink (lua_State *L) {
  luaL_checkentity(L, 1)->SetNextClientThink(luaL_checknumber(L, 2));
  return 0;
}


// HL2SB (2026-09-21): Entity:CreateParticleEffect( particle, attachment, options )
// -- GMod realm: client.  Creates a .pcf particle system owned by the entity's
// CParticleProperty and returns the CNewParticleEffect wrapper.  The options
// table (IDs 1..64) drives extra control points: each entry may carry
// attachtype (PATTACH_*, default PATTACH_ABSORIGIN), entity (the CP follows
// it) and position (a static CP position).
static int CBaseEntity_CreateParticleEffect (lua_State *L) {
  CBaseEntity *pEnt = luaL_checkentity(L, 1);
  const char *pszName = luaL_checkstring(L, 2);
  int iAttachment = luaL_optint(L, 3, 0);

  // With an attachment index the system must follow that attachment
  // (PATTACH_POINT_FOLLOW); without one, follow the origin.
  ParticleAttachment_t iAttachType = iAttachment > 0 ? PATTACH_POINT_FOLLOW : PATTACH_ABSORIGIN_FOLLOW;

  CNewParticleEffect *pEffect = pEnt->ParticleProp()->Create( pszName, iAttachType, iAttachment );
  if ( pEffect == NULL ) {
    lua_pushnil(L);
    return 1;
  }
  if ( !pEffect->IsValid() ) {
    Warning( "[HL2SB] Entity:CreateParticleEffect('%s'): the particle system is unknown -- was it loaded with game.AddParticles and registered with PrecacheParticleSystem?\n", pszName );
  }

  if ( lua_istable(L, 4) ) {
    for ( int i = 1; i <= 64; ++i ) {
      lua_rawgeti(L, 4, i);
      if ( lua_istable(L, -1) ) {
        lua_getfield(L, -1, "entity");
        CBaseEntity *pCPEnt = lua_isnil(L, -1) ? NULL : luaL_checkentity(L, -1);
        lua_pop(L, 1);

        int iCPType = PATTACH_ABSORIGIN;   // wiki: attachtype defaults to PATTACH_ABSORIGIN
        lua_getfield(L, -1, "attachtype");
        if ( !lua_isnil(L, -1) )
          iCPType = (int)lua_tointeger(L, -1);
        lua_pop(L, 1);

        if ( pCPEnt != NULL ) {
          pEnt->ParticleProp()->AddControlPoint( pEffect, i, pCPEnt, (ParticleAttachment_t)iCPType, NULL, vec3_origin );
        }

        lua_getfield(L, -1, "position");
        if ( !lua_isnil(L, -1) )
          pEffect->SetControlPoint( i, luaL_checkvector(L, -1) );
        lua_pop(L, 1);
      }
      lua_pop(L, 1);
    }
  }

  HL2SB_PushNewParticleEffect(L, pEffect);
  return 1;
}

// HL2SB (2026-09-21): Entity:StopParticlesInvolving( entity ) -- GMod realm:
// client.  Stops every particle system on this entity that has a control
// point attached to the given entity.
static int CBaseEntity_StopParticlesInvolving (lua_State *L) {
  CBaseEntity *pEnt = luaL_checkentity(L, 1);
  CBaseEntity *pOther = luaL_checkentity(L, 2);
  pEnt->ParticleProp()->StopParticlesInvolving( pOther );
  return 0;
}

// HL2SB GMod compat: Entity:PrintMessage( type, message ) -- client realm.
// GMod prints locally, routed per the HUD_PRINT_* type (chat, center,
// console, notify).  The HUD routing lives with the chat panels and the
// server-realm binding already delivers the real HUD print through the
// recipient filter; here every type lands in the console so a client-side
// call (hitnumbers' permission notice) is at least delivered instead of
// dying on a missing method.
static int CBaseEntity_PrintMessage (lua_State *L) {
  luaL_checkentity(L, 1);
  int nType = luaL_checkinteger(L, 2);
  const char *pszMessage = luaL_checkstring(L, 3);

  (void)nType;
  Msg( "%s\n", pszMessage );
  return 0;
}

// HL2SB: GMod's shared spawnflag readers, client half.  The reference reads
// the networked copy and answers a plain boolean bit test for HasSpawnFlags;
// the argument is lenient there (any number), which checkinteger covers for
// the integer constants every script passes.
static int CBaseEntity_GetSpawnFlags (lua_State *L) {
  lua_pushinteger( L, luaL_checkentity( L, 1 )->m_spawnflags );
  return 1;
}

static int CBaseEntity_HasSpawnFlags (lua_State *L) {
  lua_pushboolean( L, ( luaL_checkentity( L, 1 )->m_spawnflags & (unsigned)luaL_checkinteger( L, 2 ) ) != 0 );
  return 1;
}

// HL2SB: GMod's shared Entity:IsOnFire, client half.  The reference reads its
// own per-entity fire-state members; the engine-native source of truth here is
// the networked effect-entity handle (CBaseAnimating::Ignite points it at the
// entityflame, the flame's StopEffect clears it) plus the FL_ONFIRE flag.
// The stock properties panel runs the ignite/extinguish Filter on the CLIENT
// and one erroring Filter kills the whole DEntityProperties build, so without
// this the entire context-menu property list went dead, not just ignite.
// Extinguish stays server-only -- the reference publishes no client copy.
static int CBaseEntity_IsWorld (lua_State *L) {
  if (lua_isnoneornil(L, 1)) { lua_pushboolean(L, false); return 1; }
  bool isEntity = false;
  if (lua_type(L, 1) == LUA_TUSERDATA && lua_getmetatable(L, 1)) {
    lua_pushliteral(L, "MetaID");
    lua_rawget(L, -2);
    isEntity = lua_isnumber(L, -1) && lua_tointeger(L, -1) == 9;
    lua_pop(L, 2);
  }
  if (!isEntity) return luaL_argerror(L, 1, "Entity expected");
  CBaseEntity *entity = lua_toentity(L, 1);
  lua_pushboolean(L, entity != NULL && entity->IsWorld());
  return 1;
}

static int CBaseEntity_IsOnFire (lua_State *L) {
  C_BaseEntity *pEntity = luaL_checkentity( L, 1 );
  bool bOnFire = ( pEntity->GetFlags() & FL_ONFIRE ) != 0;
  if ( !bOnFire )
  {
    bOnFire = ( dynamic_cast< C_EntityFlame * >( pEntity->GetEffectEntity() ) != NULL );
  }
  lua_pushboolean( L, bOnFire );
  return 1;
}

static const luaL_Reg CBaseEntitymeta[] = {
  {"SpawnClientEntity", CBaseEntity_SpawnClientEntity},
  {"Interp_HierarchyUpdateInterpolationAmounts", CBaseEntity_Interp_HierarchyUpdateInterpolationAmounts},
  {"Init", CBaseEntity_Init},
  {"Term", CBaseEntity_Term},
  {"PrintMessage", CBaseEntity_PrintMessage},
  {"EnableInToolView", CBaseEntity_EnableInToolView},
  {"IsEnabledInToolView", CBaseEntity_IsEnabledInToolView},
  {"SetToolRecording", CBaseEntity_SetToolRecording},
  {"IsToolRecording", CBaseEntity_IsToolRecording},
  {"HasRecordedThisFrame", CBaseEntity_HasRecordedThisFrame},
  {"RecordToolMessage", CBaseEntity_RecordToolMessage},
  {"DontRecordInTools", CBaseEntity_DontRecordInTools},
  {"ShouldRecordInTools", CBaseEntity_ShouldRecordInTools},
  {"Release", CBaseEntity_Release},
  {"GetRenderOrigin", CBaseEntity_GetRenderOrigin},
  {"GetRenderAngles", CBaseEntity_GetRenderAngles},
  {"GetObserverCamOrigin", CBaseEntity_GetObserverCamOrigin},
  {"IsTwoPass", CBaseEntity_IsTwoPass},
  {"UsesPowerOfTwoFrameBufferTexture", CBaseEntity_UsesPowerOfTwoFrameBufferTexture},
  {"UsesFullFrameBufferTexture", CBaseEntity_UsesFullFrameBufferTexture},
  {"DrawModel", CBaseEntity_DrawModel},
  {"SetRenderBounds", CBaseEntity_SetRenderBounds},
  {"SetParentPhysNum", CBaseEntity_SetParentPhysNum},
  {"SetRenderBoundsWS", CBaseEntity_SetRenderBoundsWS},
  // HL2SB GMod compat: the render matrix pair (client only, like the reference).
  {"EnableMatrix", CBaseEntity_EnableMatrix},
  {"DisableMatrix", CBaseEntity_DisableMatrix},
  {"ComputeFxBlend", CBaseEntity_ComputeFxBlend},
  {"GetFxBlend", CBaseEntity_GetFxBlend},
  {"LODTest", CBaseEntity_LODTest},
  {"SetNextClientThink", CBaseEntity_SetNextClientThink},
  // HL2SB (2026-09-21): the .pcf particle surface.  StopParticles lives in the
  // SHARED entity metatable (lbaseentity_shared.cpp) -- both realms register
  // into the same table.
  {"CreateParticleEffect", CBaseEntity_CreateParticleEffect},
  {"StopParticlesInvolving", CBaseEntity_StopParticlesInvolving},
  // HL2SB: GMod publishes these two on the CLIENT entity metatable too --
  // Entity:HasSpawnFlags/GetSpawnFlags are shared.  lua_run.lua's Initialize
  // calls self:HasSpawnFlags on the client copy of the entity and died with
  // "attempt to call a nil value" before this.  The value is the networked
  // m_spawnflags copy (DT_BaseEntity carries it; Add/RemoveSpawnFlags stay
  // server-only, matching the reference surface).
  {"GetSpawnFlags", CBaseEntity_GetSpawnFlags},
  {"HasSpawnFlags", CBaseEntity_HasSpawnFlags},
  {"IsOnFire", CBaseEntity_IsOnFire},
  {"IsWorld", CBaseEntity_IsWorld},
  {NULL, NULL}
};


/*
** GMod compat (2026-10-21): the client asks the script before a mouse press
** reaches a panel -- the hook is "VGUIMousePressAllowed", it is answered with
** the mouse code, and a script that answers true swallows the press.
**
** vgui2 cannot reach the script, so the callback is installed here: this
** library is opened when the client Lua state comes up, and the game never
** needs the hook before that.
*/
extern "C" void VGUI_SetOnMousePressAllowed( bool ( *fn )( vgui::Panel *, int mouseCode ) );
extern "C" void VGUI_SetOnMouseReleased( void ( *fn )( vgui::Panel *, int mouseCode ) );
bool HL2SB_IsScreenClickPanel(vgui::Panel *panel);

bool HL2SB_GetScreenClickDirection( Vector &direction, int x, int y, const CViewSetup *setup );

static void HL2SB_VGUIOnMouseReleased( vgui::Panel *panel, int mouseCode )
{
	if ( !L || !HL2SB_IsScreenClickPanel( panel ) ) return;
	const int top = lua_gettop( L );
	int x = 0, y = 0;
	vgui::input()->GetCursorPos( x, y );
	Vector direction( 0, 0, 1 );
	HL2SB_GetScreenClickDirection( direction, x, y, NULL );
	BEGIN_LUA_CALL_HOOK( "GUIMouseReleased" );
		lua_pushinteger( L, mouseCode );
		lua_pushvector( L, direction );
	END_LUA_CALL_HOOK( 2, 0 );
	lua_settop( L, top );
}

static bool HL2SB_VGUIOnMousePressAllowed( vgui::Panel *panel, int mouseCode )
{
	if ( L == NULL )
		return false;

	const int iTop = lua_gettop( L );
	BEGIN_LUA_CALL_HOOK( "VGUIMousePressAllowed" );
		lua_pushinteger( L, mouseCode );
	END_LUA_CALL_HOOK( 1, 1 );
	// The hook answers true to swallow the press.
	const bool bBlocked = lua_toboolean( L, -1 );
	lua_settop( L, iTop );
	if (!bBlocked) {
		BEGIN_LUA_CALL_HOOK("VGUIMousePressed");
			lua_pushpanel(L, panel);
			lua_pushinteger(L, mouseCode);
		END_LUA_CALL_HOOK(2, 0);
		lua_settop(L, iTop);
	}

	// World screen clicks are distinct from ordinary control input.
	if ( !bBlocked && HL2SB_IsScreenClickPanel( panel ) ) {
		int x = 0, y = 0;
		vgui::input()->GetCursorPos( x, y );
		Vector vecDir( 0, 0, 1 );
		HL2SB_GetScreenClickDirection( vecDir, x, y, NULL );
		BEGIN_LUA_CALL_HOOK( "GUIMousePressed" );
			lua_pushinteger( L, mouseCode );
			lua_pushvector( L, vecDir );
		END_LUA_CALL_HOOK( 2, 0 );
		lua_settop( L, iTop );
	}
	return bBlocked;
}

/*
** Open CBaseEntity object
*/
LUALIB_API int luaopen_CBaseEntity (lua_State *L) {
  luaL_getmetatable(L, LUA_BASEENTITYLIBNAME);
  if (lua_isnoneornil(L, -1)) {
    lua_pop(L, 1);
    luaL_newmetatable(L, LUA_BASEENTITYLIBNAME);
  }
  luaL_register(L, NULL, CBaseEntitymeta);
  VGUI_SetOnMousePressAllowed( HL2SB_VGUIOnMousePressAllowed );
  VGUI_SetOnMouseReleased( HL2SB_VGUIOnMouseReleased );
  return 1;
}

