//========= Copyright Valve Corporation, All rights reserved. ============//
//
// Purpose: HL2SB: Garry's Mod's spawn commands, implemented in the engine.
//
// GMod defines these in its sandbox gamemode (gamemodes/sandbox/gamemode/
// commands.lua) and its spawn menu talks to nothing else.  Having them here means
// every client - the Lua spawn menu included - spawns through ONE code path that:
//
//   * hands a WEAPON to the player instead of creating it in the world.  A world
//     weapon entity has no owner, and that is what crashed the client on
//     `ent_create weapon_rpg`.
//   * places whatever it spawns at the player's eye trace, the way GMod does,
//     instead of at a fixed distance.
//   * applies the NPC weapon the player configured (gmod_npcweapon) as a keyvalue
//     BEFORE the entity spawns, which is when the AI reads it.
//   * records every spawn in the undo stack through HL2SB_UndoRecord
//     (game/server/hl2sb_undo.cpp), which is the engine's own recorder.
//
// $NoKeywords: $
//=============================================================================//
#include "cbase.h"
#include "hl2sb_undo.h"

// memdbgon must be the last include file in a .cpp file!!!
#include "tier0/memdbgon.h"

// GMod keeps the NPC weapon in a cvar; it is ARCHIVE so the choice survives a
// restart, which is the one thing a client-side variable cannot do here.
static ConVar gmod_npcweapon( "gmod_npcweapon", "", FCVAR_ARCHIVE,
	"Weapon given to NPCs spawned from the spawn menu (empty = the NPC's own default, \"none\" = unarmed)." );

static CBasePlayer *GM_CommandPlayer( void )
{
	CBasePlayer *pPlayer = UTIL_GetCommandClient();

	if ( pPlayer == NULL )
		pPlayer = UTIL_GetLocalPlayer();

	return pPlayer;
}

// GMod drops what it spawns at the end of the player's eye trace, a little off the
// surface that was hit, and level with the ground rather than at eye height.
static void GM_PlaceAtEyeTrace( CBasePlayer *pPlayer, CBaseEntity *pEnt )
{
	Vector vecEye = pPlayer->EyePosition();
	Vector vecForward;
	pPlayer->EyeVectors( &vecForward );

	trace_t tr;
	UTIL_TraceLine( vecEye, vecEye + vecForward * 8192.0f, MASK_SOLID, pPlayer, COLLISION_GROUP_NONE, &tr );

	// back off along the surface normal so the entity is not inside what was hit
	pEnt->SetAbsOrigin( tr.endpos + tr.plane.normal * 16.0f );

	QAngle angles = pPlayer->EyeAngles();
	angles.x = 0.0f;
	angles.z = 0.0f;
	pEnt->SetAbsAngles( angles );
}

static CBaseEntity *GM_SpawnAtEyeTrace( CBasePlayer *pPlayer, const char *pszClass,
                                        const char *pszModel, const char *pszEquipment,
                                        const char *pszVehicleScript = NULL )
{
	CBaseEntity *pEnt = CreateEntityByName( pszClass );

	if ( pEnt == NULL )
	{
		Warning( "[HL2SB] %s: no such class \"%s\"\n", "gm_spawn", pszClass );
		return NULL;
	}

	if ( pszModel != NULL && pszModel[ 0 ] != '\0' )
		pEnt->SetModel( pszModel );

	// the vehicle script is a KEYVALUE - it has to be on the entity before
	// DispatchSpawn, which is when prop_vehicle reads it.  A model-less
	// prop_vehicle is a server crash (CFourWheelVehiclePhysics::Initialize),
	// so the model and the script ride together from the spawn menu.
	if ( pszVehicleScript != NULL && pszVehicleScript[ 0 ] != '\0' )
		pEnt->KeyValue( "vehiclescript", pszVehicleScript );

	// equipment is a KEYVALUE - it has to be set before DispatchSpawn, which is when
	// the NPC reads it.  "none" is GMod's "unarmed" and is honoured the same way.
	if ( pszEquipment != NULL && pszEquipment[ 0 ] != '\0' )
		pEnt->KeyValue( "additionalequipment", pszEquipment );

	GM_PlaceAtEyeTrace( pPlayer, pEnt );

	DispatchSpawn( pEnt );
	pEnt->Activate();

	return pEnt;
}

static void GM_Record( CBasePlayer *pPlayer, CBaseEntity *pEnt )
{
	if ( pPlayer == NULL || pEnt == NULL )
		return;

	HL2SB_UndoRecord( pPlayer, pEnt );
}

//-----------------------------------------------------------------------------
// gm_giveswep: GMod's "give yourself this weapon".  The weapon goes into the
// player's inventory - no world weapon entity is created, which is exactly why the
// RPG cannot reach the code that assumed an owner.
//-----------------------------------------------------------------------------
CON_COMMAND( gm_giveswep, "Give yourself a weapon: gm_giveswep <class>  (GMod)" )
{
	CBasePlayer *pPlayer = GM_CommandPlayer();

	if ( pPlayer == NULL || args.ArgC() < 2 )
		return;

	pPlayer->GiveNamedItem( args[ 1 ] );
}

//-----------------------------------------------------------------------------
// gm_spawn: an entity / scripted entity / prop, with an optional model.
//-----------------------------------------------------------------------------
CON_COMMAND( gm_spawn, "Spawn an entity: gm_spawn <class> [model]  (GMod)" )
{
	CBasePlayer *pPlayer = GM_CommandPlayer();

	if ( pPlayer == NULL || args.ArgC() < 2 )
		return;

	CBaseEntity *pEnt = GM_SpawnAtEyeTrace( pPlayer, args[ 1 ],
		( args.ArgC() > 2 ) ? args[ 2 ] : NULL, NULL );

	GM_Record( pPlayer, pEnt );
}

//-----------------------------------------------------------------------------
// gm_spawnvehicle: the spawn menu sends <class> <model> <vehiclescript> -- the
// model and the script USED to be dropped here, so every menu-spawned vehicle
// was model-less (the exact shape that crashes the server or spawns nothing).
//-----------------------------------------------------------------------------
CON_COMMAND( gm_spawnvehicle, "Spawn a vehicle: gm_spawnvehicle <class> [model] [vehiclescript]  (GMod)" )
{
	CBasePlayer *pPlayer = GM_CommandPlayer();

	if ( pPlayer == NULL || args.ArgC() < 2 )
		return;

	CBaseEntity *pEnt = GM_SpawnAtEyeTrace( pPlayer, args[ 1 ],
		( args.ArgC() > 2 ) ? args[ 2 ] : NULL, NULL,
		( args.ArgC() > 3 ) ? args[ 3 ] : NULL );

	GM_Record( pPlayer, pEnt );
}

//-----------------------------------------------------------------------------
// gm_spawnnpc: the weapon is the second argument when the caller sent one, and the
// gmod_npcweapon cvar otherwise.
//-----------------------------------------------------------------------------
CON_COMMAND( gm_spawnnpc, "Spawn an NPC: gm_spawnnpc <class> [weapon]  (GMod)" )
{
	CBasePlayer *pPlayer = GM_CommandPlayer();

	if ( pPlayer == NULL || args.ArgC() < 2 )
		return;

	const char *pszWeapon = ( args.ArgC() > 2 ) ? args[ 2 ] : gmod_npcweapon.GetString();

	CBaseEntity *pEnt = GM_SpawnAtEyeTrace( pPlayer, args[ 1 ], NULL, pszWeapon );

	GM_Record( pPlayer, pEnt );
}

//-----------------------------------------------------------------------------
// gm_spawnprop: GMod spawns props with gm_spawn too, but the model-only form is
// what the Prop page and the toolgun's creator use.
//-----------------------------------------------------------------------------
CON_COMMAND( gm_spawnprop, "Spawn a prop: gm_spawnprop <model>  (GMod)" )
{
	CBasePlayer *pPlayer = GM_CommandPlayer();

	if ( pPlayer == NULL || args.ArgC() < 2 )
		return;

	CBaseEntity *pEnt = GM_SpawnAtEyeTrace( pPlayer, "prop_physics", args[ 1 ], NULL );

	GM_Record( pPlayer, pEnt );
}
