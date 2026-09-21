//========= Copyright HL2SB, all rights reserved. ============//
//
// Purpose: HL2SB: GMod's .pcf engine-particle Lua surface, client half.
//
//          A .pcf particle system ("CNewParticleEffect" in GMod terms) is the
//          *engine's* particle simulator -- game.AddParticles() loads the
//          file, PrecacheParticleSystem() registers a named system from it,
//          and these bindings spawn instances of it:
//
//              game.AddParticles( "particles/explosion.pcf" )
//              PrecacheParticleSystem( "ExplosionCore_wall" )
//              CreateParticleSystemNoEntity( "ExplosionCore_wall", pos )
//              CreateParticleSystem( ent, "ExplosionCore_wall", PATTACH_POINT_FOLLOW )
//              ent:CreateParticleEffect( "ExplosionCore_wall", 1 )
//
//          The shared globals ParticleEffect / ParticleEffectAttach live in
//          game/shared/lua/lparticle_system.cpp (GMod realm: Shared -- the
//          server half dispatches the "ParticleEffect" TE, which lands in
//          c_particle_system.cpp's ParticleEffectCallback) and call the
//          HL2SB_Client* helpers here on the client.
//
//          The wrapper holds a CSmartPtr, so the script can keep an effect
//          alive across frames even after the entity it was attached to is
//          gone; once nothing references it and emission has stopped, the
//          particle manager reaps it (same refcount design as GMod's).
//
// $NoKeywords: $
//===========================================================================//

#ifdef CLIENT_DLL

#include "cbase.h"
#include "luamanager.h"
#include "luasrclib.h"
#include "luabinding.h"
#include "mathlib/lvector.h"
#include "lbaseentity_shared.h"	// lua_pushentity / luaL_checkentity
#include "lnewparticle.h"
#include "particles_new.h"
#include "particle_parse.h"

// memdbgon must be the last include file in a .cpp file!!!
#include "tier0/memdbgon.h"

// ---------------------------------------------------------------------------
// The wrapper.  CSmartPtr keeps the effect object alive while a script holds
// it; GetObject() == NULL (or the effect released back to the manager) is the
// GMod "not valid" state.
// ---------------------------------------------------------------------------
struct LuaNewParticleUD
{
	CSmartPtr<CNewParticleEffect> m_pEffect;
};

static int LuaNewParticle_gc( lua_State *Lstate )
{
	LuaNewParticleUD *pUD = (LuaNewParticleUD *)lua_touserdata( Lstate, 1 );
	if ( pUD )
		pUD->~LuaNewParticleUD();
	return 0;
}

void HL2SB_PushNewParticleEffect( lua_State *L, CNewParticleEffect *pEffect )
{
	if ( pEffect == NULL )
	{
		lua_pushnil( L );
		return;
	}
	LuaNewParticleUD *pUD = (LuaNewParticleUD *)lua_newuserdata( L, sizeof( LuaNewParticleUD ) );
	new ( &pUD->m_pEffect ) CSmartPtr<CNewParticleEffect>( pEffect );
	luaL_getmetatable( L, "CNewParticleEffect" );
	lua_setmetatable( L, -2 );
}

static LuaNewParticleUD *LuaNewParticle_checkudata( lua_State *Lstate, int iArg )
{
	LuaNewParticleUD *pUD = (LuaNewParticleUD *)luaL_checkudata( Lstate, iArg, "CNewParticleEffect" );
	if ( pUD == NULL )
		luaL_argerror( Lstate, iArg, "CNewParticleEffect expected" );
	return pUD;
}

// GMod: IsValid is "false once the particle system is scheduled for removal".
static CNewParticleEffect *LuaNewParticle_checkalive( lua_State *Lstate, int iArg )
{
	LuaNewParticleUD *pUD = LuaNewParticle_checkudata( Lstate, iArg );
	CNewParticleEffect *pEffect = pUD->m_pEffect.GetObject();
	if ( pEffect == NULL || pEffect->GetRemoveFlag() )
		luaL_error( Lstate, "Tried to use a NULL CNewParticleEffect!" );
	return pEffect;
}

// ---------------------------------------------------------------------------
// Creation helpers shared with lparticle_system.cpp (ParticleEffect /
// ParticleEffectAttach client halves).
// ---------------------------------------------------------------------------

// CNewParticleEffect::Create with CP0/CP1 at vecPos and CP0 oriented by
// angOrient -- the exact setup ParticleEffectCallback runs for the
// entity-less "ParticleEffect" TE, so a server-dispatched effect and a locally
// created one look the same.
static CNewParticleEffect *HL2SB_CreateAt( const char *pszName,
	const Vector &vecPos, const QAngle &angOrient )
{
	CSmartPtr<CNewParticleEffect> pEffect = CNewParticleEffect::Create( NULL, pszName );
	if ( pEffect.GetObject() == NULL || !pEffect->IsValid() )
	{
		luasrc_LuaInfoMsgF( "[HL2SB] ParticleEffect('%s'): the particle system is unknown -- was it loaded with game.AddParticles and registered with PrecacheParticleSystem?\n", pszName );
		return pEffect.GetObject();   // may still be non-NULL; IsValid() answers it
	}
	pEffect->SetSortOrigin( vecPos );
	pEffect->SetControlPoint( 0, vecPos );
	pEffect->SetControlPoint( 1, vecPos );
	Vector vecForward, vecRight, vecUp;
	AngleVectors( angOrient, &vecForward, &vecRight, &vecUp );
	pEffect->SetControlPointOrientation( 0, vecForward, vecRight, vecUp );
	return pEffect.GetObject();
}

CNewParticleEffect *HL2SB_ClientCreateParticleEffect( const char *pszName,
	const Vector &vecPos, const QAngle &angOrient, CBaseEntity *pParent )
{
	if ( pParent == NULL || pParent->IsDormant() )
		return HL2SB_CreateAt( pszName, vecPos, angOrient );

	// Parented: follow the entity's origin, and orient CP0 by the given
	// angles -- the wiki note "you must provide the entity argument for the
	// angles to take effect" is about exactly this combination.
	CNewParticleEffect *pEffect = pParent->ParticleProp()->Create( pszName, PATTACH_ABSORIGIN_FOLLOW );
	if ( pEffect != NULL && pEffect->IsValid() )
	{
		Vector vecForward, vecRight, vecUp;
		AngleVectors( angOrient, &vecForward, &vecRight, &vecUp );
		pEffect->SetControlPointOrientation( 0, vecForward, vecRight, vecUp );
	}
	return pEffect;
}

CNewParticleEffect *HL2SB_ClientAttachParticleEffect( const char *pszName,
	int iAttachType, CBaseEntity *pEntity, int iAttachmentPoint )
{
	if ( pEntity == NULL )
		return NULL;
	CNewParticleEffect *pEffect = pEntity->ParticleProp()->Create( pszName,
		(ParticleAttachment_t)iAttachType, iAttachmentPoint );
	if ( pEffect != NULL && !pEffect->IsValid() )
	{
		luasrc_LuaInfoMsgF( "[HL2SB] ParticleEffectAttach('%s'): the particle system is unknown -- was it loaded with game.AddParticles and registered with PrecacheParticleSystem?\n", pszName );
	}
	return pEffect;
}

// ---------------------------------------------------------------------------
// CNewParticleEffect methods (names follow the GMod wiki page).
// ---------------------------------------------------------------------------

LUA_REGISTRATION_INIT( CNewParticleEffectReg );

// IsValid()
LUA_BINDING_BEGIN( CNewParticleEffectReg, IsValid, "method", "Returns whether the particle effect is valid and not scheduled for removal.", "client" )
{
	LuaNewParticleUD *pUD = LuaNewParticle_checkudata( L, 1 );
	CNewParticleEffect *pEffect = pUD->m_pEffect.GetObject();
	lua_pushboolean( L, ( pEffect != NULL && pEffect->IsValid() && !pEffect->GetRemoveFlag() ) ? 1 : 0 );
	return 1;
}
LUA_BINDING_END()

// GetEffectName()
LUA_BINDING_BEGIN( CNewParticleEffectReg, GetEffectName, "method", "Returns the particle system's name.", "client" )
{
	CNewParticleEffect *pEffect = LuaNewParticle_checkalive( L, 1 );
	const char *pszName = pEffect->GetEffectName();
	lua_pushstring( L, pszName ? pszName : "" );
	return 1;
}
LUA_BINDING_END()

// GetOwner()
LUA_BINDING_BEGIN( CNewParticleEffectReg, GetOwner, "method", "Returns the entity the particle effect is attached to, or NULL.", "client" )
{
	CNewParticleEffect *pEffect = LuaNewParticle_checkalive( L, 1 );
	CBaseEntity *pOwner = pEffect->GetOwner();
	if ( pOwner != NULL )
		lua_pushentity( L, pOwner );
	else
		lua_pushnil( L );
	return 1;
}
LUA_BINDING_END()

// SetControlPoint( controlPoint, position )
LUA_BINDING_BEGIN( CNewParticleEffectReg, SetControlPoint, "method", "Sets the position of the given control point.", "client" )
{
	CNewParticleEffect *pEffect = LuaNewParticle_checkalive( L, 1 );
	pEffect->SetControlPoint( luaL_checkint( L, 2 ), luaL_checkvector( L, 3 ) );
	return 0;
}
LUA_BINDING_END()

// SetControlPointEntity( controlPoint, entity )
LUA_BINDING_BEGIN( CNewParticleEffectReg, SetControlPointEntity, "method", "Makes the given control point follow the entity's origin.", "client" )
{
	CNewParticleEffect *pEffect = LuaNewParticle_checkalive( L, 1 );
	pEffect->SetControlPointEntity( luaL_checkint( L, 2 ), luaL_checkentity( L, 3 ) );
	return 0;
}
LUA_BINDING_END()

// SetControlPointOrientation( controlPoint, orientation ) -- a GMod Quaternion
// table { x, y, z, w } -- or ( controlPoint, forward, right, up ).
LUA_BINDING_BEGIN( CNewParticleEffectReg, SetControlPointOrientation, "method", "Sets the orientation of the given control point, either from a Quaternion or from forward/right/up vectors.", "client" )
{
	CNewParticleEffect *pEffect = LuaNewParticle_checkalive( L, 1 );
	int nPoint = luaL_checkint( L, 2 );
	if ( lua_istable( L, 3 ) )
	{
		Quaternion q;
		lua_getfield( L, 3, "x" ); q.x = (float)luaL_optnumber( L, -1, 0 ); lua_pop( L, 1 );
		lua_getfield( L, 3, "y" ); q.y = (float)luaL_optnumber( L, -1, 0 ); lua_pop( L, 1 );
		lua_getfield( L, 3, "z" ); q.z = (float)luaL_optnumber( L, -1, 0 ); lua_pop( L, 1 );
		lua_getfield( L, 3, "w" ); q.w = (float)luaL_optnumber( L, -1, 1 ); lua_pop( L, 1 );

		QAngle ang;
		QuaternionAngles( q, ang );
		Vector vecForward, vecRight, vecUp;
		AngleVectors( ang, &vecForward, &vecRight, &vecUp );
		pEffect->SetControlPointOrientation( nPoint, vecForward, vecRight, vecUp );
	}
	else
	{
		pEffect->SetControlPointOrientation( nPoint, luaL_checkvector( L, 3 ),
			luaL_checkvector( L, 4 ), luaL_checkvector( L, 5 ) );
	}
	return 0;
}
LUA_BINDING_END()

LUA_BINDING_BEGIN( CNewParticleEffectReg, SetControlPointForwardVector, "method", "Sets the forward vector of the given control point.", "client" )
{
	CNewParticleEffect *pEffect = LuaNewParticle_checkalive( L, 1 );
	pEffect->SetControlPointForwardVector( luaL_checkint( L, 2 ), luaL_checkvector( L, 3 ) );
	return 0;
}
LUA_BINDING_END()

LUA_BINDING_BEGIN( CNewParticleEffectReg, SetControlPointUpVector, "method", "Sets the up vector of the given control point.", "client" )
{
	CNewParticleEffect *pEffect = LuaNewParticle_checkalive( L, 1 );
	pEffect->SetControlPointUpVector( luaL_checkint( L, 2 ), luaL_checkvector( L, 3 ) );
	return 0;
}
LUA_BINDING_END()

LUA_BINDING_BEGIN( CNewParticleEffectReg, SetControlPointRightVector, "method", "Sets the right vector of the given control point.", "client" )
{
	CNewParticleEffect *pEffect = LuaNewParticle_checkalive( L, 1 );
	pEffect->SetControlPointRightVector( luaL_checkint( L, 2 ), luaL_checkvector( L, 3 ) );
	return 0;
}
LUA_BINDING_END()

// SetControlPointParent( controlPoint, parentControlPoint )
LUA_BINDING_BEGIN( CNewParticleEffectReg, SetControlPointParent, "method", "Sets the parent control point of the given control point.", "client" )
{
	CNewParticleEffect *pEffect = LuaNewParticle_checkalive( L, 1 );
	pEffect->SetControlPointParent( luaL_checkint( L, 2 ), luaL_checkint( L, 3 ) );
	return 0;
}
LUA_BINDING_END()

// AddControlPoint( controlPoint, entity, attachType, attachmentName, offset )
LUA_BINDING_BEGIN( CNewParticleEffectReg, AddControlPoint, "method", "Adds a control point that follows the given entity.", "client" )
{
	CNewParticleEffect *pEffect = LuaNewParticle_checkalive( L, 1 );
	int nPoint = luaL_checkint( L, 2 );
	CBaseEntity *pEntity = luaL_checkentity( L, 3 );
	ParticleAttachment_t iAttachType = (ParticleAttachment_t)luaL_checkint( L, 4 );
	const char *pszAttachment = lua_isnoneornil( L, 5 ) ? NULL : luaL_checkstring( L, 5 );
	Vector vecOffset = lua_isnoneornil( L, 6 ) ? vec3_origin : luaL_checkvector( L, 6 );

	// The per-control-point follow machinery lives on the owning entity's
	// CParticleProperty; a free-standing effect can only pin the point to the
	// entity's origin.
	CBaseEntity *pOwnerEnt = pEffect->GetOwner();
	if ( pOwnerEnt != NULL && pOwnerEnt->ParticleProp() != NULL )
		pOwnerEnt->ParticleProp()->AddControlPoint( pEffect, nPoint, pEntity, iAttachType, pszAttachment, vecOffset );
	else
		pEffect->SetControlPointEntity( nPoint, pEntity );
	return 0;
}
LUA_BINDING_END()

// StartEmission( infiniteOnly )
LUA_BINDING_BEGIN( CNewParticleEffectReg, StartEmission, "method", "Restarts emission of a particle effect stopped with StopEmission.", "client" )
{
	CNewParticleEffect *pEffect = LuaNewParticle_checkalive( L, 1 );
	pEffect->StartEmission( luaL_optboolean( L, 2, 0 ) ? true : false );
	return 0;
}
LUA_BINDING_END()

// StopEmission( infiniteOnly, removeAllParticles, wakeOnStop )
LUA_BINDING_BEGIN( CNewParticleEffectReg, StopEmission, "method", "Stops emission of the particle effect; existing particles play out unless removeAllParticles is set.", "client" )
{
	LuaNewParticleUD *pUD = LuaNewParticle_checkudata( L, 1 );
	CNewParticleEffect *pEffect = pUD->m_pEffect.GetObject();
	if ( pEffect == NULL )
		luaL_error( L, "Tried to use a NULL CNewParticleEffect!" );
	// Argument order per the wiki: ( infiniteOnly, removeAllParticles, wakeOnStop ).
	pEffect->StopEmission( luaL_optboolean( L, 2, 0 ) ? true : false,
						   luaL_optboolean( L, 3, 0 ) ? true : false,
						   luaL_optboolean( L, 4, 0 ) ? true : false );
	return 0;
}
LUA_BINDING_END()

// StopEmissionAndDestroyImmediately( infiniteOnly, removeAllParticles, wakeOnStop )
LUA_BINDING_BEGIN( CNewParticleEffectReg, StopEmissionAndDestroyImmediately, "method", "Stops the particle effect and destroys it immediately.", "client" )
{
	LuaNewParticleUD *pUD = LuaNewParticle_checkudata( L, 1 );
	CNewParticleEffect *pEffect = pUD->m_pEffect.GetObject();
	if ( pEffect == NULL )
		luaL_error( L, "Tried to use a NULL CNewParticleEffect!" );
	bool bInfiniteOnly = luaL_optboolean( L, 2, 0 ) ? true : false;
	bool bRemoveAll = luaL_optboolean( L, 3, 0 ) ? true : false;
	bool bWakeOnStop = luaL_optboolean( L, 4, 0 ) ? true : false;
	pEffect->StopEmission( bInfiniteOnly, bRemoveAll, bWakeOnStop );
	pEffect->SetRemoveFlag();
	// Hand the destroy request to the entity-side property as well -- it holds
	// its own reference, and while it does, the manager will not reap the
	// effect no matter what this wrapper does.
	if ( pEffect->GetOwner() != NULL && pEffect->GetOwner()->ParticleProp() != NULL )
		pEffect->GetOwner()->ParticleProp()->StopEmissionAndDestroyImmediately( pEffect );
	return 0;
}
LUA_BINDING_END()

// IsFinished()
LUA_BINDING_BEGIN( CNewParticleEffectReg, IsFinished, "method", "Returns whether the particle effect has finished and is scheduled for removal.", "client" )
{
	LuaNewParticleUD *pUD = LuaNewParticle_checkudata( L, 1 );
	CNewParticleEffect *pEffect = pUD->m_pEffect.GetObject();
	lua_pushboolean( L, ( pEffect == NULL || !pEffect->IsValid() || pEffect->GetRemoveFlag() ) ? 1 : 0 );
	return 1;
}
LUA_BINDING_END()

// SetShouldDraw( shouldDraw )
LUA_BINDING_BEGIN( CNewParticleEffectReg, SetShouldDraw, "method", "Sets whether the particle effect is drawn.", "client" )
{
	CNewParticleEffect *pEffect = LuaNewParticle_checkalive( L, 1 );
	pEffect->SetDrawn( luaL_checkboolean( L, 2 ) ? true : false );
	return 0;
}
LUA_BINDING_END()

// SetShouldSimulate( shouldSimulate ) / GetShouldSimulate()
LUA_BINDING_BEGIN( CNewParticleEffectReg, SetShouldSimulate, "method", "Sets whether the particle effect is simulated.", "client" )
{
	CNewParticleEffect *pEffect = LuaNewParticle_checkalive( L, 1 );
	pEffect->SetShouldSimulate( luaL_checkboolean( L, 2 ) ? true : false );
	return 0;
}
LUA_BINDING_END()

LUA_BINDING_BEGIN( CNewParticleEffectReg, GetShouldSimulate, "method", "Returns whether the particle effect is simulated.", "client" )
{
	CNewParticleEffect *pEffect = LuaNewParticle_checkalive( L, 1 );
	lua_pushboolean( L, pEffect->ShouldSimulate() ? 1 : 0 );
	return 1;
}
LUA_BINDING_END()

// SetSortOrigin( position )
LUA_BINDING_BEGIN( CNewParticleEffectReg, SetSortOrigin, "method", "Sets the position used to sort (depth order) the particle effect.", "client" )
{
	CNewParticleEffect *pEffect = LuaNewParticle_checkalive( L, 1 );
	pEffect->SetSortOrigin( luaL_checkvector( L, 2 ) );
	return 0;
}
LUA_BINDING_END()

// Restart() -- kill what is on screen and emit from the start again.
LUA_BINDING_BEGIN( CNewParticleEffectReg, Restart, "method", "Restarts the particle simulation from the beginning.", "client" )
{
	CNewParticleEffect *pEffect = LuaNewParticle_checkalive( L, 1 );
	pEffect->StopEmission( false, true, false );
	pEffect->StartEmission( false );
	return 0;
}
LUA_BINDING_END()

// SetIsViewModelEffect / IsViewModelEffect -- this engine marks viewmodel
// effects on the particle system *definition* (CParticleSystemDefinition), not
// per instance; answer/no-op here so addons calling them do not die on nil,
// with a one-shot warning the first time it actually gets used.
LUA_BINDING_BEGIN( CNewParticleEffectReg, SetIsViewModelEffect, "method", "HL2SB: stored but not rendered as a viewmodel effect -- this engine tracks viewmodel particle systems per definition, not per instance.", "client" )
{
	static bool s_bWarned = false;
	if ( !s_bWarned )
	{
		s_bWarned = true;
		Warning( "[HL2SB] CNewParticleEffect:SetIsViewModelEffect has no per-instance support in this engine; the flag is ignored.\n" );
	}
	return 0;
}
LUA_BINDING_END()

LUA_BINDING_BEGIN( CNewParticleEffectReg, IsViewModelEffect, "method", "HL2SB: always false -- this engine tracks viewmodel particle systems per definition, not per instance.", "client" )
{
	LuaNewParticle_checkudata( L, 1 );
	lua_pushboolean( L, 0 );
	return 1;
}
LUA_BINDING_END()

// GetAutoUpdateBBox() -- whether the render bounds are recomputed from the
// live particles every frame.
LUA_BINDING_BEGIN( CNewParticleEffectReg, GetAutoUpdateBBox, "method", "Returns whether the particle system's bounding box updates automatically.", "client" )
{
	CNewParticleEffect *pEffect = LuaNewParticle_checkalive( L, 1 );
	lua_pushboolean( L, pEffect->GetAutoUpdateBBox() ? 1 : 0 );
	return 1;
}
LUA_BINDING_END()

// GetHighestControlPoint() -- highest CP id the definition actually uses.
LUA_BINDING_BEGIN( CNewParticleEffectReg, GetHighestControlPoint, "method", "Returns the highest control point number used by the particle system.", "client" )
{
	CNewParticleEffect *pEffect = LuaNewParticle_checkalive( L, 1 );
	lua_pushnumber( L, pEffect->GetHighestControlPoint() );
	return 1;
}
LUA_BINDING_END()

// GetRenderBounds() -- the current bounding box of the system's particles.
LUA_BINDING_BEGIN( CNewParticleEffectReg, GetRenderBounds, "method", "Returns mins, maxs of the particle system's bounding box.", "client" )
{
	CNewParticleEffect *pEffect = LuaNewParticle_checkalive( L, 1 );
	Vector vecMins, vecMaxs;
	pEffect->GetRenderBounds( vecMins, vecMaxs );
	lua_pushvector( L, vecMins );
	lua_pushvector( L, vecMaxs );
	return 2;
}
LUA_BINDING_END()

// Render() -- GMod can force a particle system into the current render
// context (vgui panels).  This engine has no out-of-band particle render
// path: systems only draw through the world renderable list, so this is a
// reported no-op (same precedent as SetIsViewModelEffect above).
LUA_BINDING_BEGIN( CNewParticleEffectReg, Render, "method", "HL2SB: no manual render path -- particle systems draw through the world render only.", "client" )
{
	LuaNewParticle_checkudata( L, 1 );
	static bool s_bWarned = false;
	if ( !s_bWarned )
	{
		s_bWarned = true;
		Warning( "[HL2SB] CNewParticleEffect:Render has no manual render path in this engine; systems auto-draw.\n" );
	}
	return 0;
}
LUA_BINDING_END()

// ---------------------------------------------------------------------------
// Globals: CreateParticleSystem( ent, name, attachType, attachmentID, offset )
// and CreateParticleSystemNoEntity( name, pos, ang ).
// ---------------------------------------------------------------------------

static int LuaGlobal_CreateParticleSystem( lua_State *Lstate )
{
	CBaseEntity *pEnt = luaL_checkentity( Lstate, 1 );
	const char *pszName = luaL_checkstring( Lstate, 2 );
	int iAttachType = luaL_checkint( Lstate, 3 );
	int iAttachment = luaL_optint( Lstate, 4, INVALID_PARTICLE_ATTACHMENT );
	Vector vecOffset = lua_isnoneornil( Lstate, 5 ) ? vec3_origin : luaL_checkvector( Lstate, 5 );

	CNewParticleEffect *pEffect = pEnt->ParticleProp()->Create( pszName,
		(ParticleAttachment_t)iAttachType, iAttachment, vecOffset );
	if ( pEffect != NULL && !pEffect->IsValid() )
	{
		luasrc_LuaInfoMsgF( "[HL2SB] CreateParticleSystem('%s'): the particle system is unknown -- was it loaded with game.AddParticles and registered with PrecacheParticleSystem?\n", pszName );
	}
	HL2SB_PushNewParticleEffect( Lstate, pEffect );
	return 1;
}

static int LuaGlobal_CreateParticleSystemNoEntity( lua_State *Lstate )
{
	const char *pszName = luaL_checkstring( Lstate, 1 );
	Vector vecPos = luaL_checkvector( Lstate, 2 );
	QAngle angOrient( 0, 0, 0 );
	if ( !lua_isnoneornil( Lstate, 3 ) )
		angOrient = luaL_checkangle( Lstate, 3 );
	HL2SB_PushNewParticleEffect( Lstate, HL2SB_CreateAt( pszName, vecPos, angOrient ) );
	return 1;
}

// ---------------------------------------------------------------------------
// luaopen: the metatable + the two globals.  Registered under CLIENT_DLL in
// lsrcinit.cpp's luasrclibs table.
// ---------------------------------------------------------------------------
LUALIB_API int luaopen_CNewParticleEffect( lua_State *Lstate )
{
	luaL_newmetatable( Lstate, "CNewParticleEffect" );
	LUA_REGISTRATION_COMMIT( CNewParticleEffectReg );
	lua_pushvalue( Lstate, -1 );
	lua_setfield( Lstate, -2, "__index" );
	lua_pushcfunction( Lstate, LuaNewParticle_gc );
	lua_setfield( Lstate, -2, "__gc" );
	lua_pop( Lstate, 1 );

	lua_pushcfunction( Lstate, LuaGlobal_CreateParticleSystem );
	lua_setglobal( Lstate, "CreateParticleSystem" );
	lua_pushcfunction( Lstate, LuaGlobal_CreateParticleSystemNoEntity );
	lua_setglobal( Lstate, "CreateParticleSystemNoEntity" );
	return 0;
}

#endif // CLIENT_DLL
