//========== HL2SB ===========//
//
// Purpose: Bridge between the engine's spawn console commands and Garry's
//          Mod's Lua undo module.
//
//          The undo stack is NOT implemented here any more.  It is GMod's
//          lua/includes/modules/undo.lua, loaded from lua/includes/modules/ on
//          both realms: it owns the per-player stack, the CanCreateUndo /
//          PreUndo / PostUndo / CanUndo force conditions, the OnUndo client
//          notification and the `undo` / `gmod_undo` / `gmod_undonum` console
//          commands.
//
//          The previous C++ stack kept a second, private copy of all of that
//          and registered the same `hl2sb_undo` / `hl2sb_undoclear` command
//          names as the Lua module, so one silently shadowed the other and the
//          command could drain an empty stack.
//
//          This file now only lets `ent_create` / `prop_physics_create`
//          (engine console commands, not Lua) register what they spawned:
//
//              undo.Create( name )
//              undo.AddEntity( ent )
//              undo.SetPlayer( ply )
//              undo.Finish( name )
//
//          which is exactly what a GMod spawnmenu does from Lua.
//
//===========================================================================//

#include "cbase.h"
#include "hl2sb_undo.h"
#include "player.h"
#include "luamanager.h"
#include "luasrclib.h"
#include "lbaseentity_shared.h"
#include "lbaseplayer_shared.h"

// memdbgon must be the last include file in a .cpp file!!!
#include "tier0/memdbgon.h"

// The shared Lua state (same one lnet.cpp / the HUD hooks use).
extern lua_State *L;

//-----------------------------------------------------------------------------
// Purpose: Run one undo.<pszFunc>() with the `undo` table at the top of the
//          stack and nArgs values pushed above it.  Leaves exactly the `undo`
//          table behind on every path -- a missed pop here would corrupt the
//          Lua state for the rest of the level.
//-----------------------------------------------------------------------------
static void HL2SB_CallUndoFunc( lua_State *pL, const char *pszFunc, int nArgs )
{
	// [ undo, args... ]
	lua_getfield( pL, -( nArgs + 1 ), pszFunc );	// [ undo, args..., func ]

	if ( !lua_isfunction( pL, -1 ) )
	{
		lua_pop( pL, nArgs + 1 );					// [ undo ]
		return;
	}

	// [ undo, args..., func ] -> [ undo, func, args... ] so pcall sees
	// func followed by its arguments.
	lua_insert( pL, -( nArgs + 1 ) );

	// HL2SB: report the failure.
	//
	// This used to be a bare `luasrc_pcall( pL, nArgs, 0, 0 );` -- return value
	// discarded, error object left unread.  Every failure inside
	// undo.Create / AddEntity / SetPlayer / Finish was therefore invisible, and
	// the only symptom was the undo command later saying
	// "no undo entry recorded" with a perfectly empty stack.  Now the Lua error
	// names itself in ds_debug.log.
	int iStatus = luasrc_pcall( pL, nArgs, 0, 0 );
	if ( iStatus != 0 )
	{
		const char *pszErr = lua_tostring( pL, -1 );
		Warning( "[HL2SB undo] undo.%s() failed: %s\n", pszFunc, pszErr ? pszErr : "(no message)" );
		lua_pop( pL, 1 );							// [ undo ]
	}
}

//-----------------------------------------------------------------------------
// Purpose: HL2SB_UndoRecord - register a freshly spawned entity with GMod's
//          Lua undo module as one undoable action.
//-----------------------------------------------------------------------------
// 2-arg overload for the pre-existing callers (props.cpp, baseentity.cpp):
// classname label, exactly the old behavior.
void HL2SB_UndoRecord( CBasePlayer *pOwner, CBaseEntity *pEnt )
{
	HL2SB_UndoRecord( pOwner, pEnt, NULL );
}

void HL2SB_UndoRecord( CBasePlayer *pOwner, CBaseEntity *pEnt, const char *pszLabel )
{
	// HL2SB: log every entry, not just the failures.
	//
	// The failure prints below only fire once the call is already inside, so a
	// silent run was ambiguous: "HL2SB_UndoRecord was never called" (the spawn
	// command did not create anything -- CreatePhysicsProp returns NULL when the
	// model is missing) looks exactly like "it was called and worked".  Those
	// two need completely different fixes, so say which one it is.
	if ( !pEnt )
	{
		Warning( "[HL2SB undo] HL2SB_UndoRecord: entity is NULL (the spawn failed -- model missing?), nothing recorded\n" );
		return;
	}
	if ( !pOwner )
	{
		Warning( "[HL2SB undo] HL2SB_UndoRecord: owner is NULL for %s, nothing recorded\n", pEnt->GetClassname() );
		return;
	}

	// HL2SB: never put an entity that is already gone into the undo stack.
	//
	// The undo stack is meant to be run LATER, so anything recorded here has to
	// stay reachable until then.  A spawn that DispatchSpawn() gave up on is
	// marked for deletion on the spot (a vehicle whose `vehiclescript` does not
	// parse does exactly that: CFourWheelVehiclePhysics::Initialize ->
	// UTIL_Remove), and recording it would leave `undo` holding a corpse.  With
	// undo.lua's UndoValid() made real again, Do_Undo() would skip it -- but it
	// must never get in there in the first place.
	//
	// IsEntityPtr() compares pointers in the global entity list without
	// dereferencing them, so unlike IsMarkedForDeletion() it is safe even if the
	// entity has already been freed.
	if ( pEnt->IsMarkedForDeletion() || !gEntList.IsEntityPtr( pEnt ) )
	{
		Warning( "[HL2SB undo] HL2SB_UndoRecord: %s is already marked for deletion or no longer in the entity list, nothing recorded\n",
				 pEnt->GetClassname() );
		return;
	}

	if ( cvar && cvar->FindVar( "hl2sb_hud_debug" ) && cvar->FindVar( "hl2sb_hud_debug" )->GetInt() != 0 )
		Warning( "[HL2SB undo] HL2SB_UndoRecord( %s, %s )\n", pOwner->GetPlayerName(), pEnt->GetClassname() );

	lua_State *pL = L;
	if ( !pL )
		return;

	// HL2SB: the spawn menu passes the display name the entry was registered
	// with ("Jeep", "wood_crate001a", the NPC pack's own label) -- a menu-spawned
	// prop otherwise undid as "prop_physics" no matter which prop it was.
	const char *pszName = ( pszLabel != NULL && pszLabel[ 0 ] != '\0' ) ? pszLabel : pEnt->GetClassname();
	if ( !pszName || !pszName[ 0 ] )
		pszName = "entity";

	const int nBase = lua_gettop( pL );

	lua_getglobal( pL, "undo" );					// [ undo ]
	if ( !lua_istable( pL, -1 ) )
	{
		// HL2SB: same reasoning as the pcall report below -- say WHY the record
		// is being dropped instead of returning in silence.  "undo is nil" means
		// lua/includes/modules/undo.lua never ran in this Lua state, which is a
		// completely different problem from an error inside undo.Finish().
		Warning( "[HL2SB undo] global `undo` is %s, not a table -- cannot record %s\n",
			luaL_typename( pL, -1 ), pszName );
		lua_settop( pL, nBase );
		return;
	}

	// undo.Create( name )
	lua_pushstring( pL, pszName );
	HL2SB_CallUndoFunc( pL, "Create", 1 );

	// undo.AddEntity( ent )
	lua_pushentity( pL, pEnt );
	HL2SB_CallUndoFunc( pL, "AddEntity", 1 );

	// undo.SetPlayer( ply )
	lua_pushplayer( pL, pOwner );
	HL2SB_CallUndoFunc( pL, "SetPlayer", 1 );

	// undo.Finish( name ) -- NiceText shown by the client notification
	lua_pushstring( pL, pszName );
	HL2SB_CallUndoFunc( pL, "Finish", 1 );

	lua_settop( pL, nBase );
}
