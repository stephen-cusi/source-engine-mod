//=============================================================================//
//
// HL2SB: GMod's lua/effects/*.lua runtime (client half).
//
// A GMod effect script is a global EFFECT table with Init/Think/Render methods.
// luasrc_LoadEffects() (game/shared/lua/luamanager.cpp) runs every
// lua/effects/<name>.lua with a fresh EFFECT global, calls GMod's
// effects.Register(EFFECT, name) with it and files the template away under
// __hl2sb_lua_effects[name].
//
// Here that template becomes a live effect:
//
//   * util.Effect( name, data ) / DispatchEffect( name, data ) land in
//     HL2SB_CreateLuaEffect() (hooked from DispatchEffectToCallback in
//     c_te_effect_dispatch.cpp, which is where every effect name ends up --
//     server-sent ones through TE_DispatchEffect, client-side ones directly);
//   * one shallow copy of the template per spawn (which is exactly what GMod's
//     effects.Create does, and what keeps two live tracers from sharing state);
//   * the copy is pushed onto the engine's client-side effect list
//     (clienteffects, game/client/clientsideeffects.cpp) so
//     CEffectsList::DrawEffects() drives EFFECT:Think() and EFFECT:Render()
//     once per frame from CViewRender::ViewDrawScene -- inside the world render,
//     with the 3D fog disabled, which is where render.DrawBeam needs to run.
//
//=============================================================================//

#include "cbase.h"

#include "clientsideeffects.h"
#include "effect_dispatch_data.h"
#include "luamanager.h"
#include "luasrclib.h"
#include "leffect_dispatch_data.h"
#include "lbaseentity_shared.h"
#include "mathlib/lvector.h"
#include "lua_effects.h"

#include "c_baseanimating.h"
#include "c_baseplayer.h"
#include "c_baseviewmodel.h"
#include "c_basecombatweapon.h"

// memdbgon must be the last include file in a .cpp file!!!
#include "tier0/memdbgon.h"

#define HL2SB_LUA_EFFECT_TEMPLATES "__hl2sb_lua_effects"

//=============================================================================
// Engine-provided methods on the effect table.
//
// GMod hands every effect a handful of C++ methods (SetRenderBoundsWS,
// GetTracerShootPos, ...).  They are stamped onto each spawned copy here.
//=============================================================================

// EFFECT:SetRenderBoundsWS( mins, maxs )
//
// GMod uses these bounds to cull the effect; the engine's client FX list does
// not cull, so the values are only remembered (a future cull can read them).
static int LuaEffect_SetRenderBoundsWS( lua_State *L )
{
	Vector mins = luaL_checkvector( L, 2 );
	Vector maxs = luaL_checkvector( L, 3 );

	lua_pushvector( L, mins );
	lua_setfield( L, 1, "__hl2sb_renderbounds_min" );
	lua_pushvector( L, maxs );
	lua_setfield( L, 1, "__hl2sb_renderbounds_max" );

	return 0;
}

// EFFECT:GetTracerShootPos( position, entity, attachment )
//
// GMod's tracer effects start at the muzzle, not at the shooter's eyes:
// rb655_nyan_tracer.lua does
//     self.StartPos = self:GetTracerShootPos( self.Position, self.WeaponEnt, self.Attachment )
//
// HL2SB: the entity GMod hands these effects is whatever the caller put in the
// effect data, and the two callers differ:
//   * util.Effect() from a SWEP's DoImpactEffect / a script: whatever the script set;
//   * the engine's own tracer path -- HL2MP's C_TEHL2MPFireBullets::CreateEffects()
//     sets m_hEntity = pWpn->GetRefEHandle(), i.e. THE WEAPON (not the player).
//
// The first cut only special-cased "the entity IS the local player", so the
// shooter's own shots fell into the generic branch and took the WORLD MODEL
// weapon's attachment.  A weapon the local client never simulates has no
// meaningful world position there, so the Nyan Gun's rainbow tracer was drawn
// from the wrong place: invisible against near walls, and visibly offset
// ("misplaced") when it hit something at range.
//
// So resolve the owner too: a weapon held by the local player draws from the
// VIEWMODEL's muzzle, which is where GMod's own tracers start.
static int LuaEffect_GetTracerShootPos( lua_State *L )
{
	Vector vecPosition = luaL_checkvector( L, 2 );
	CBaseEntity *pEntity = lua_toentity( L, 3 );
	int iAttachment = luaL_optinteger( L, 4, 0 );

	Vector vecResult = vecPosition;
	const char *pszSource = "position (no entity)";
	const char *pszEntity = "none";

	if ( pEntity != NULL )
	{
		pszEntity = pEntity->GetClassname();

		C_BasePlayer *pPlayer = ToBasePlayer( pEntity );
		CBaseCombatWeapon *pWeapon = NULL;

		if ( pPlayer == NULL )
		{
			pWeapon = dynamic_cast<CBaseCombatWeapon *>( pEntity );

			if ( pWeapon != NULL )
			{
				// The engine's tracer path hands us the weapon: the interesting
				// entity for "where does this shot come from" is its owner.
				CBaseCombatCharacter *pOwner = pWeapon->GetOwner();

				if ( pOwner != NULL )
				{
					pPlayer = ToBasePlayer( pOwner );
				}
			}
		}

		if ( pPlayer != NULL && pPlayer->IsLocalPlayer() )
		{
			// First person: the muzzle lives on the viewmodel.
			C_BaseViewModel *pViewModel = pPlayer->GetViewModel();

			if ( pViewModel != NULL && iAttachment > 0 )
			{
				Vector vecOrigin;

				if ( pViewModel->GetAttachment( iAttachment, vecOrigin ) )
				{
					vecResult = vecOrigin;
					pszSource = "local player viewmodel attachment";
				}
				else
				{
					pszSource = "position (viewmodel attachment missing)";
				}
			}
			else
			{
				pszSource = "position (local player, no viewmodel/attachment)";
			}
		}
		else
		{
			// Third person (another player's or an NPC's weapon): the model that
			// is actually on screen is the right place to start from.
			C_BaseAnimating *pAnimating = ( pWeapon != NULL ) ? pWeapon->GetBaseAnimating() : pEntity->GetBaseAnimating();

			if ( pAnimating != NULL && iAttachment > 0 )
			{
				Vector vecOrigin;

				if ( pAnimating->GetAttachment( iAttachment, vecOrigin ) )
				{
					vecResult = vecOrigin;
					pszSource = ( pWeapon != NULL ) ? "weapon model attachment" : "entity model attachment";
				}
				else
				{
					pszSource = "position (attachment missing)";
				}
			}
			else
			{
				pszSource = "position (no animating model/attachment)";
			}
		}
	}

	// HL2SB diagnostic: says which branch supplied the tracer's start point,
	// once per branch per DLL load (see AGENTS.md 9.7).
	{
		char szKey[ 128 ];
		Q_snprintf( szKey, sizeof( szKey ), "tracer-start:%s", pszSource );
		HL2SB_WarnOnce( szKey, "GetTracerShootPos: '%s' (attachment %d) -> %s\n", pszEntity, iAttachment, pszSource );
	}

	lua_pushvector( L, vecResult );
	return 1;
}

//=============================================================================
// One live effect: a Lua table plus the engine's draw slot.
//=============================================================================
class CLuaEffect : public CClientSideEffect
{
public:
	CLuaEffect( const char *pszName, int nRef, const CEffectData &data );
	virtual ~CLuaEffect( void );

	virtual void Draw( double frametime );

private:
	// HL2SB: the base class keeps the POINTER we hand it, so it has to point at
	// storage that outlives the call.  Taking the address of our own member in
	// the mem-initialiser list is safe here -- it is a plain char array, so
	// nothing has to have run before it is usable.
	char m_szName[128];
	int m_nRef;
};

CLuaEffect::~CLuaEffect( void )
{
	// CEffectsList deletes the effect when Think() says it is done, and
	// clienteffects->Flush() deletes the rest on level change, so this is the
	// single place the Lua registry reference is released.
	if ( L != NULL && m_nRef >= 0 )
		lua_unref( L, m_nRef );

	m_nRef = LUA_NOREF;
}

CLuaEffect::CLuaEffect( const char *pszName, int nRef, const CEffectData &data )
	: CClientSideEffect( m_szName )
	, m_nRef( nRef )
{
	Q_strncpy( m_szName, ( pszName != NULL ) ? pszName : "lua_effect", sizeof( m_szName ) );

	if ( L == NULL )
		return;

	lua_getref( L, m_nRef );
	if ( !lua_istable( L, -1 ) )
	{
		lua_pop( L, 1 );
		return;
	}

	lua_getfield( L, -1, "Init" );
	if ( lua_isfunction( L, -1 ) )
	{
		lua_pushvalue( L, -2 );                 // self
		CEffectData dataCopy = data;            // lua_pusheffect takes a reference
		lua_pusheffect( L, dataCopy );          // data
		luasrc_pcall( L, 2, 0, 0 );
	}
	else
	{
		lua_pop( L, 1 );
	}

	lua_pop( L, 1 );
}

void CLuaEffect::Draw( double frametime )
{
	if ( L == NULL )
	{
		Destroy();
		return;
	}

	lua_getref( L, m_nRef );
	if ( !lua_istable( L, -1 ) )
	{
		lua_pop( L, 1 );
		Destroy();
		return;
	}

	// EFFECT:Think() -- returning false retires the effect (GMod contract).
	lua_getfield( L, -1, "Think" );
	if ( lua_isfunction( L, -1 ) )
	{
		lua_pushvalue( L, -2 );
		luasrc_pcall( L, 1, 1, 0 );
		if ( !lua_toboolean( L, -1 ) )
			Destroy();
		lua_pop( L, 1 );
	}
	else
	{
		lua_pop( L, 1 );
	}

	if ( !IsActive() )
	{
		lua_pop( L, 1 );
		return;
	}

	lua_getfield( L, -1, "Render" );
	if ( lua_isfunction( L, -1 ) )
	{
		lua_pushvalue( L, -2 );
		luasrc_pcall( L, 1, 0, 0 );
	}
	else
	{
		lua_pop( L, 1 );
	}

	lua_pop( L, 1 );
}

//=============================================================================
// Spawning
//=============================================================================

static bool HL2SB_FindLuaEffectTemplate( lua_State *L, const char *pszName )
{
	char szLookup[ 128 ];

	// HL2SB: the name comes from the network (an effect string-table entry), so
	// refuse anything that would be truncated -- two names sharing their first
	// 127 characters would otherwise resolve to each other's template.
	if ( pszName == NULL || pszName[0] == '\0' || Q_strlen( pszName ) >= (int)sizeof( szLookup ) )
	{
		return false;
	}

	Q_strncpy( szLookup, pszName, sizeof( szLookup ) );
	Q_strlower( szLookup );

	lua_getglobal( L, HL2SB_LUA_EFFECT_TEMPLATES );
	if ( !lua_istable( L, -1 ) )
	{
		lua_pop( L, 1 );
		return false;
	}

	lua_getfield( L, -1, szLookup );
	lua_remove( L, -2 );                // drop the registry, leave the template

	if ( !lua_istable( L, -1 ) )
	{
		lua_pop( L, 1 );
		return false;
	}

	return true;
}

bool HL2SB_HasLuaEffect( const char *pszName )
{
	if ( L == NULL || pszName == NULL || pszName[0] == '\0' )
		return false;

	const bool bFound = HL2SB_FindLuaEffectTemplate( L, pszName );
	if ( bFound )
		lua_pop( L, 1 );

	return bFound;
}

bool HL2SB_CreateLuaEffect( const char *pszName, const CEffectData &data )
{
	if ( L == NULL || pszName == NULL || pszName[0] == '\0' )
		return false;

	if ( !HL2SB_FindLuaEffectTemplate( L, pszName ) )
		return false;

	// stack: template
	lua_newtable( L );                          // template, copy

	// Shallow copy of the template -- one table per live effect, so two tracers
	// in flight do not overwrite each other's state.
	lua_pushnil( L );
	while ( lua_next( L, -3 ) != 0 )
	{
		lua_pushvalue( L, -2 );                 // key
		lua_insert( L, -2 );                    // key, value -> key, key, value
		lua_settable( L, -4 );                  // copy[ key ] = value
	}

	// GMod's engine methods.
	lua_pushcfunction( L, LuaEffect_SetRenderBoundsWS );
	lua_setfield( L, -2, "SetRenderBoundsWS" );

	lua_pushcfunction( L, LuaEffect_GetTracerShootPos );
	lua_setfield( L, -2, "GetTracerShootPos" );

	const int nRef = luaL_ref( L, LUA_REGISTRYINDEX );   // pops the copy
	lua_pop( L, 1 );                                     // the template

	if ( nRef < 0 )
		return false;

	// CEffectsList::AddEffect() silently drops the effect once its 256 slot list
	// is full: the registry reference taken above would leak for the lifetime of
	// the process, and reporting success would also suppress the engine's own
	// fallback for this effect name.  Ask before taking the slot.
	if ( !HL2SB_ClientEffectsHaveRoom() )
	{
		lua_unref( L, nRef );
		return false;
	}

	clienteffects->AddEffect( new CLuaEffect( pszName, nRef, data ) );

	// HL2SB diagnostic: one line per effect name per DLL load, so ds_debug.log
	// says whether an effect really reached this client and with what geometry.
	{
		char szKey[ 128 ];
		Q_snprintf( szKey, sizeof( szKey ), "lua-effect-created:%s", pszName );
		HL2SB_WarnOnce( szKey,
			"Lua effect '%s' created: start=(%.0f %.0f %.0f) origin=(%.0f %.0f %.0f) entindex=%d flags=0x%X attach=%d\n",
			pszName,
			data.m_vStart.x, data.m_vStart.y, data.m_vStart.z,
			data.m_vOrigin.x, data.m_vOrigin.y, data.m_vOrigin.z,
			data.entindex(), (unsigned int)data.m_fFlags, data.m_nAttachmentIndex );
	}

	return true;
}
