//========== HL2SB ===========//
//
// Purpose: Bridge between the engine's spawn console commands and Garry's
//          Mod's Lua undo module.
//
//          The undo stack is NOT implemented here.  It is GMod's
//          lua/includes/modules/undo.lua, loaded on both realms: it owns the
//          per-player stack, the CanCreateUndo / PreUndo / PostUndo / CanUndo
//          force conditions, the OnUndo client notification and the `undo` /
//          `gmod_undo` / `gmod_undonum` console commands.  GMod itself has no
//          C++ undo at all (lua_shared.dll's 151 exports and both game DLLs
//          contain zero undo symbols - audited 2026-09-29); its recording call
//          sites are Lua (gamemodes/sandbox/gamemode/commands.lua).  This
//          file therefore only reproduces those call sites for the spawn
//          commands the fork moved into the engine:
//
//            prop_* entity (commands.lua:302-305, 165-168, 329-332):
//              undo.Create( <entity class> )
//                undo.SetPlayer( ply )
//                undo.AddEntity( e )
//              undo.Finish( "#<entity class> (<model path>)" )
//
//            NPC / SENT / Vehicle (commands.lua:638-644, 876-882, 1120-1124):
//              undo.Create( <category> )
//                undo.SetPlayer( ply )
//                undo.AddEntity( e )
//                undo.SetCustomUndoText( "Undone <display>" )
//              undo.Finish( "#undo.generic.<npc|entity|vehicle> (<display>)" )
//
//          The call ORDER is GMod's order (SetPlayer before AddEntity, and
//          SetCustomUndoText between AddEntity and Finish).
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

	int iStatus = luasrc_pcall( pL, nArgs, 0, 0 );
	if ( iStatus != 0 )
	{
		const char *pszErr = lua_tostring( pL, -1 );
		Warning( "[HL2SB undo] undo.%s() failed: %s\n", pszFunc, pszErr ? pszErr : "(no message)" );
		lua_pop( pL, 1 );							// [ undo ]
	}
}

//-----------------------------------------------------------------------------
// Purpose: Shared preamble: validates, then runs GMod's
//
//          undo.Create( pszCreateName ); undo.SetPlayer( ply ); undo.AddEntity( e )
//
//          in GMod's order.  Leaves [ undo ] on the stack and returns true;
//          restores the stack and returns false on any rejection.
//-----------------------------------------------------------------------------
static bool HL2SB_UndoPrefix( CBasePlayer *pOwner, CBaseEntity *pEnt,
                              const char *pszCreateName, const char *pszWhat )
{
	if ( !pEnt )
	{
		Warning( "[HL2SB undo] HL2SB_UndoRecord(%s): entity is NULL (the spawn failed -- model missing?), nothing recorded\n", pszWhat );
		return false;
	}
	if ( !pOwner )
	{
		Warning( "[HL2SB undo] HL2SB_UndoRecord(%s): owner is NULL for %s, nothing recorded\n", pszWhat, pEnt->GetClassname() );
		return false;
	}

	// Never put an entity that is already gone into the undo stack: the stack
	// is run LATER, and a spawn DispatchSpawn gave up on (a vehicle whose
	// `vehiclescript` does not parse UTIL_Remove's itself inside
	// CFourWheelVehiclePhysics::Initialize) would otherwise leave `undo`
	// holding a corpse.
	if ( pEnt->IsMarkedForDeletion() || !gEntList.IsEntityPtr( pEnt ) )
	{
		Warning( "[HL2SB undo] HL2SB_UndoRecord(%s): %s is already marked for deletion or no longer in the entity list, nothing recorded\n",
				 pszWhat, pEnt->GetClassname() );
		return false;
	}

	lua_State *pL = L;
	if ( !pL )
		return false;

	lua_getglobal( pL, "undo" );					// [ undo ]
	if ( !lua_istable( pL, -1 ) )
	{
		Warning( "[HL2SB undo] global `undo` is %s, not a table -- cannot record %s\n",
				 luaL_typename( pL, -1 ), pszCreateName );
		lua_pop( pL, 1 );
		return false;
	}

	// undo.Create( name )
	lua_pushstring( pL, pszCreateName );
	HL2SB_CallUndoFunc( pL, "Create", 1 );

	// undo.SetPlayer( ply )   -- GMod's order: owner first, then the entity.
	lua_pushplayer( pL, pOwner );
	HL2SB_CallUndoFunc( pL, "SetPlayer", 1 );

	// undo.AddEntity( ent )
	lua_pushentity( pL, pEnt );
	HL2SB_CallUndoFunc( pL, "AddEntity", 1 );

	return true;
}

//-----------------------------------------------------------------------------
// Purpose: GMod's prop grammar (commands.lua:302-305).  The undo LIST entry
//          shows the class with the MODEL PATH in parentheses, never the
//          spawn-menu label - that is GMod's behavior and the reason the
//          client's Undo_AddUndo receiver has a "#token (secondary)"
//          localizer for exactly this string shape.
//-----------------------------------------------------------------------------
void HL2SB_UndoRecordProp( CBasePlayer *pOwner, CBaseEntity *pEnt )
{
	const char *pszClass = ( pEnt != NULL && pEnt->GetClassname() != NULL ) ? pEnt->GetClassname() : "prop_physics";
	const char *pszModel = ( pEnt != NULL && pEnt->GetModelName() != NULL_STRING )
		? STRING( pEnt->GetModelName() )
		: pszClass;

	if ( !HL2SB_UndoPrefix( pOwner, pEnt, pszClass, "prop" ) )
		return;

	// undo.Finish( "#<class> (<model>)" )   (commands.lua:305)
	char szNiceText[ 512 ];
	Q_snprintf( szNiceText, sizeof( szNiceText ), "#%s (%s)", pszClass, pszModel );

	lua_pushstring( L, szNiceText );
	HL2SB_CallUndoFunc( L, "Finish", 1 );

	lua_pop( L, 1 );		// the undo table
}

//-----------------------------------------------------------------------------
// Purpose: GMod's category grammar for "NPC" / "SENT" / "Vehicle".
//          pszDisplay is the registry title (GMod's NiceName / PrintName /
//          vehicle.Name), falling back to the class name when empty; props
//          never take this route (see HL2SB_UndoRecordProp).
//-----------------------------------------------------------------------------
void HL2SB_UndoRecordGeneric( CBasePlayer *pOwner, CBaseEntity *pEnt,
                              const char *pszKind, const char *pszDisplay )
{
	if ( !pszKind )
		pszKind = "SENT";

	// GMod's translation-key noun per category (commands.lua:644, 882, 1124).
	const char *pszNoun = "entity";
	if ( Q_stricmp( pszKind, "NPC" ) == 0 )
		pszNoun = "npc";
	else if ( Q_stricmp( pszKind, "Vehicle" ) == 0 )
		pszNoun = "vehicle";

	char szDisplay[ 256 ];
	if ( pszDisplay && pszDisplay[ 0 ] )
		Q_strncpy( szDisplay, pszDisplay, sizeof( szDisplay ) );
	else if ( pEnt && pEnt->GetClassname() )
		Q_strncpy( szDisplay, pEnt->GetClassname(), sizeof( szDisplay ) );
	else
		Q_strncpy( szDisplay, "entity", sizeof( szDisplay ) );

	if ( !HL2SB_UndoPrefix( pOwner, pEnt, pszKind, pszKind ) )
		return;

	// undo.SetCustomUndoText( "Undone <display>" )   (commands.lua:642, 880, 1123)
	char szCustom[ 320 ];
	Q_snprintf( szCustom, sizeof( szCustom ), "Undone %s", szDisplay );

	lua_pushstring( L, szCustom );
	HL2SB_CallUndoFunc( L, "SetCustomUndoText", 1 );

	// undo.Finish( "#undo.generic.<noun> (<display>)" )
	char szNiceText[ 512 ];
	Q_snprintf( szNiceText, sizeof( szNiceText ), "#undo.generic.%s (%s)", pszNoun, szDisplay );

	lua_pushstring( L, szNiceText );
	HL2SB_CallUndoFunc( L, "Finish", 1 );

	lua_pop( L, 1 );		// the undo table
}

//-----------------------------------------------------------------------------
// Purpose: The raw engine console commands' entry point (CC_Ent_Create /
//          CC_Prop_Physics_Create) and the generic gm_spawn path: classify by
//          the entity's own class and emit GMod's grammar.  GMod's C++ does
//          not record undo for these commands at all (its menu spawns go
//          through Lua helpers); this keeps "anything a player creates is
//          undoable" with GMod's string shapes.
//-----------------------------------------------------------------------------
void HL2SB_UndoRecord( CBasePlayer *pOwner, CBaseEntity *pEnt, const char *pszLabel )
{
	const char *pszClass = ( pEnt != NULL && pEnt->GetClassname() != NULL ) ? pEnt->GetClassname() : "";

	// Check the vehicle prefixes BEFORE the prop_ prefix: prop_vehicle_* would
	// otherwise be swallowed by the prop branch.
	if ( Q_stricmp( pszClass, "prop_vehicle_prisoner_pod" ) == 0 ||
		 Q_strnicmp( pszClass, "prop_vehicle_", 13 ) == 0 ||
		 Q_strnicmp( pszClass, "vehicle_", 8 ) == 0 )
	{
		HL2SB_UndoRecordGeneric( pOwner, pEnt, "Vehicle", pszLabel );
		return;
	}

	if ( Q_strnicmp( pszClass, "npc_", 4 ) == 0 )
	{
		HL2SB_UndoRecordGeneric( pOwner, pEnt, "NPC", pszLabel );
		return;
	}

	// Any prop_* class (prop_physics / prop_ragdoll / prop_dynamic ...) gets
	// GMod's prop grammar; the label is dropped exactly the way GMod's
	// SpawnProp drops the menu title (the MODEL is shown, not the label).
	if ( Q_strnicmp( pszClass, "prop_", 5 ) == 0 )
	{
		HL2SB_UndoRecordProp( pOwner, pEnt );
		return;
	}

	HL2SB_UndoRecordGeneric( pOwner, pEnt, "SENT", pszLabel );
}

void HL2SB_UndoRecord( CBasePlayer *pOwner, CBaseEntity *pEnt )
{
	HL2SB_UndoRecord( pOwner, pEnt, NULL );
}
