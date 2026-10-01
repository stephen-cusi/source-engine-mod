//========= Copyright Valve Corporation, All rights reserved. ============//
//
// Purpose:		Player for HL2.
//
//=============================================================================//

#include "cbase.h"
#include "weapon_hl2mpbasehlmpcombatweapon.h"
#include "hl2mp_player.h"
#include "player.h"
#include "activitylist.h"
#include "cdll_int.h"	// HL2SB: player_info_t for the gesture/overlay name probes
#include "globalstate.h"
#include "game.h"
#include "gamerules.h"
#include "hl2mp_player_shared.h"
#include "predicted_viewmodel.h"
#include "in_buttons.h"
#include "hl2mp_gamerules.h"
#include "Multiplayer/multiplayer_animstate.h"
#include "KeyValues.h"
#include "team.h"
#include "weapon_hl2mpbase.h"

#ifdef HL2SB
#include "hl2sb_player_model_manager.h"
#include "hl2sb_model_config.h"
#include "igameevents.h"
#endif
#include "grenade_satchel.h"
#include "eventqueue.h"
#include "gamestats.h"
#ifdef LUA_SDK
#include "luamanager.h"
// HL2SB (2026-09-27): lua_pushvector for the animation hook dispatches.
#include "mathlib/lvector.h"
#include "lbaseentity_shared.h"
#include "lbaseplayer_shared.h"
#include "lbasecombatweapon_shared.h"	// HL2SB GMod compat: lua_pushweapon (SWEP:OnDrop)
#include "lhl2mp_player_shared.h"
#include "ltakedamageinfo.h"
#endif

#include "engine/IEngineSound.h"
#include "SoundEmitterSystem/isoundemittersystembase.h"

#include "ilagcompensationmanager.h"

// HL2SB: flashlight turned on by default at spawn
extern ConVar sv_flashlight_default;
// HL2SB: replicated animation debug switch (defined in hl2mp_player_shared.cpp),
// used by gesture/overlay probes in PostThink and HL2SB_AnimRestartGesture.
extern ConVar hl2sb_anim_debug;

CBaseEntity	 *g_pLastCombineSpawn = NULL;
CBaseEntity	 *g_pLastRebelSpawn = NULL;
extern CBaseEntity				*g_pLastSpawn;
ConVar spawnpoint("spawnpoint", "ct");

extern ConVar mode;

#define HL2MP_COMMAND_MAX_RATE 0.3

void DropPrimedFragGrenade( CHL2MP_Player *pPlayer, CBaseCombatWeapon *pGrenade );

LINK_ENTITY_TO_CLASS( player, CHL2MP_Player );

LINK_ENTITY_TO_CLASS( info_player_combine, CPointEntity );
LINK_ENTITY_TO_CLASS( info_player_rebel, CPointEntity );

// Andrew; we may end up using other game content - these allow us to use other
// maps besides deathmatch ones.
#ifdef HL2SB
LINK_ENTITY_TO_CLASS( info_player_counterterrorist, CPointEntity );
LINK_ENTITY_TO_CLASS( info_player_terrorist, CPointEntity );
LINK_ENTITY_TO_CLASS( info_player_allies, CPointEntity );
LINK_ENTITY_TO_CLASS( info_player_axis, CPointEntity );
#endif
IMPLEMENT_SERVERCLASS_ST(CHL2MP_Player, DT_HL2MP_Player)
	SendPropAngle( SENDINFO_VECTORELEM(m_angEyeAngles, 0), 11, SPROP_CHANGES_OFTEN ),
	SendPropAngle( SENDINFO_VECTORELEM(m_angEyeAngles, 1), 11, SPROP_CHANGES_OFTEN ),
	SendPropEHandle( SENDINFO( m_hRagdoll ) ),
	SendPropInt( SENDINFO( m_iSpawnInterpCounter), 4 ),
	SendPropInt( SENDINFO( m_iPlayerSoundType), 3 ),
	SendPropFloat( SENDINFO( m_flStartCharge ) ),
	SendPropFloat( SENDINFO( m_flAmmoStartCharge ) ),
	SendPropFloat( SENDINFO( m_flPlayAftershock ) ),
	SendPropFloat( SENDINFO( m_flNextAmmoBurn ) ),

	// HL2SB (2026-09-27): GMod per-player movement speeds (Player:SetWalkSpeed /
	// SetRunSpeed / SetJumpPower).  Networked so client prediction in
	// CGameMovement (GetPlayerMaxSpeed / CheckJumpButton) agrees with the
	// server-authoritative values Lua writes.
	SendPropFloat( SENDINFO( m_flHL2SBWalkSpeed ) ),
	SendPropFloat( SENDINFO( m_flHL2SBRunSpeed ) ),
	SendPropFloat( SENDINFO( m_flHL2SBSlowWalkSpeed ) ),
	SendPropFloat( SENDINFO( m_flHL2SBJumpPower ) ),
	// HL2SB (2026-09-27): the taunt clock.  GMod's client reads the very same
	// float through IsPlayingTaunt() to run the taunt camera and gate the act
	// command, so it has to replicate.
	SendPropFloat( SENDINFO( m_flHL2SBTauntEnd ) ),
	// HL2SB (2026-09-27): the hands entity handle.  GMod's datadesc prop is
	// "m_Hands"; this fork's pair is self-consistently named m_hHands (nothing
	// external parses the datatable, so only the pair matching matters).
	SendPropEHandle( SENDINFO( m_hHands ) ),

	SendPropExclude( "DT_BaseAnimating", "m_flPoseParameter" ),
	SendPropExclude( "DT_BaseFlex", "m_viewtarget" ),

//	SendPropExclude( "DT_ServerAnimationData" , "m_flCycle" ),	
//	SendPropExclude( "DT_AnimTimeMustBeFirst" , "m_flAnimTime" ),
	
END_SEND_TABLE()

BEGIN_DATADESC( CHL2MP_Player )
	DEFINE_FIELD( m_flStartCharge, FIELD_FLOAT ),
	DEFINE_FIELD( m_flAmmoStartCharge, FIELD_FLOAT ),
	DEFINE_FIELD( m_flPlayAftershock, FIELD_FLOAT ),
	DEFINE_FIELD( m_flNextAmmoBurn, FIELD_FLOAT ),
END_DATADESC()

// HL2SB (2026-09-27): the hardcoded per-team random model lists
// (g_ppszRandomCitizenModels / g_ppszRandomCombineModels) are gone.  The player
// model is whatever cl_playermodel says, like GMod; the menu list comes from
// player_manager (lua/autorun/client/hl2sb_playermodels.lua scanning
// models/player/), and the fallback everywhere below is GMod's default
// playermodel, models/player/kleiner.mdl.
#define HL2SB_DEFAULT_PLAYERMODEL "models/player/kleiner.mdl"

#define MODEL_CHANGE_INTERVAL 0.1f
#define TEAM_CHANGE_INTERVAL 0.1f

#define HL2MPPLAYER_PHYSDAMAGE_SCALE 4.0f

#pragma warning( disable : 4355 )

//-----------------------------------------------------------------------------
// HL2SB (2026-09-27): GMod's `act <name>` command, ported from the reference
// x86 server.dll handler ():
//
//   * UTIL_GetCommandClient(); bail unless the caller is an alive player
//   * 16 hardcoded names -> activities (a strcmp chain in GMod).  forward/
//     group/halt are HL2's squad SIGNAL gestures, the rest is the ACT_GMOD set.
//   * a bare `act` or an unknown name silently does nothing
//   * nothing happens while a taunt is still up (TauntEnd() > curtime)
//   * GM:PlayerShouldTaunt( ply, actid ) returning false cancels
//   * duration = SequenceDuration( SelectWeightedSequence( activity ) )
//   * TauntEnd = curtime + duration (replicated float; drives IsPlayingTaunt
//     and the taunt camera on every realm -- GMod keeps no other flag)
//   * GMod plays the gesture as animstate DoAnimationEvent( CUSTOM_GESTURE,
//     act, restart ), which lands in GESTURE_SLOT_CUSTOM -- this fork's
//     equivalent is HL2SB_AnimRestartGesture( GESTURE_SLOT_CUSTOM, act, true ).
//     The gesture has to be started HERE, not on the client: the client's
//     overlay layers are overwritten by the networked DT_BaseAnimatingOverlay
//     data on every update, so a client-only layer never rendered.  The layer
//     is networked, so everyone sees it.
//   * GM:PlayerStartTaunt( ply, actid, length )
//-----------------------------------------------------------------------------
struct HL2SB_ActEntry_t
{
	const char	*pszName;
	Activity	activity;
};

static const HL2SB_ActEntry_t s_HL2SBGModActList[] =
{
	{ "wave",		ACT_GMOD_GESTURE_WAVE },
	{ "agree",		ACT_GMOD_GESTURE_AGREE },
	{ "becon",		ACT_GMOD_GESTURE_BECON },
	{ "bow",		ACT_GMOD_GESTURE_BOW },
	{ "disagree",	ACT_GMOD_GESTURE_DISAGREE },
	{ "salute",		ACT_GMOD_TAUNT_SALUTE },
	{ "forward",	ACT_SIGNAL_FORWARD },
	{ "group",		ACT_SIGNAL_GROUP },
	{ "halt",		ACT_SIGNAL_HALT },
	{ "pers",		ACT_GMOD_TAUNT_PERSISTENCE },
	{ "muscle",		ACT_GMOD_TAUNT_MUSCLE },
	{ "laugh",		ACT_GMOD_TAUNT_LAUGH },
	{ "cheer",		ACT_GMOD_TAUNT_CHEER },
	{ "zombie",		ACT_GMOD_GESTURE_TAUNT_ZOMBIE },
	{ "dance",		ACT_GMOD_TAUNT_DANCE },
	{ "robot",		ACT_GMOD_TAUNT_ROBOT },
};

CON_COMMAND_F( act, "Plays a 'taunt' animation, if the server allows it. Valid options are: wave, agree, disagree, becon, bow, salute, forward, group, halt, pers, muscle, laugh, cheer, zombie, dance, robot.", FCVAR_GAMEDLL )
{
	CHL2MP_Player *pPlayer = ToHL2MPPlayer( UTIL_GetCommandClient() );
	if ( !pPlayer || !pPlayer->IsAlive() )
		return;

	const char *pszAct = ( args.ArgC() >= 2 ) ? args[ 1 ] : "";

	Activity activity = ACT_INVALID;
	for ( int i = 0; i < ARRAYSIZE( s_HL2SBGModActList ); ++i )
	{
		if ( !Q_stricmp( pszAct, s_HL2SBGModActList[ i ].pszName ) )
		{
			activity = s_HL2SBGModActList[ i ].activity;
			break;
		}
	}
	if ( activity == ACT_INVALID )
		return;

	if ( pPlayer->HL2SB_TauntEnd() > gpGlobals->curtime )
		return;

	// GM:PlayerShouldTaunt( ply, actid ) -- only an explicit false cancels
	// (nil = the gamemode has no opinion = allow; GMod relies on its base
	// gamemode defining the hook, this fork does now too, but stay lenient).
	bool bAllowed = true;
	BEGIN_LUA_CALL_HOOK( "PlayerShouldTaunt" );
		lua_pushplayer( L, pPlayer );
		lua_pushinteger( L, activity );
	END_LUA_CALL_HOOK( 2, 1 );
	bAllowed = !lua_isboolean( L, -1 ) || ( lua_toboolean( L, -1 ) != 0 );
	lua_pop( L, 1 );
	if ( !bAllowed )
		return;

	const int iSequence = pPlayer->SelectWeightedSequence( activity );
	const float flDuration = ( iSequence >= 0 ) ? pPlayer->SequenceDuration( iSequence ) : 0.0f;

	const float flEnd = gpGlobals->curtime + flDuration;
	if ( pPlayer->HL2SB_TauntEnd() != flEnd )
		pPlayer->HL2SB_SetTauntEnd( flEnd );

	pPlayer->HL2SB_AnimRestartGesture( GESTURE_SLOT_CUSTOM, activity, true );

	BEGIN_LUA_CALL_HOOK( "PlayerStartTaunt" );
		lua_pushplayer( L, pPlayer );
		lua_pushinteger( L, activity );
		lua_pushnumber( L, flDuration );
	END_LUA_CALL_HOOK( 3, 0 );
}

CHL2MP_Player::CHL2MP_Player() : m_PlayerAnimState( this )
{
	m_angEyeAngles.Init();

	// HL2SB: keep m_iPlayerSoundType valid from construction on. SetupPlayerSoundsByModel()
	// only assigns it for the three stock model name patterns, so a custom playermodel
	// used to leave it at uninitialised memory (death sound crash 20260915_091815).
	m_iPlayerSoundType = (int)PLAYER_SOUNDS_CITIZEN;

	m_iLastWeaponFireUsercmd = 0;

	m_flNextModelChangeTime = 0.0f;
	m_flNextTeamChangeTime = 0.0f;

	m_iSpawnInterpCounter = 0;

	// HL2SB (2026-09-27): GMod defaults (base gamemode player class).  Gesture
	// slots start empty; speeds default to GMod's walk 150 / run 400 so the
	// movement feel matches even before any Lua touches them.
	for ( int i = 0; i < GESTURE_SLOT_COUNT; i++ )
	{
		m_iHL2SBSlotLayer[i] = -1;
		m_iHL2SBSlotActivity[i] = ACT_INVALID;
	}

	m_flHL2SBWalkSpeed = 150.0f;
	m_flHL2SBRunSpeed = 400.0f;
	m_flHL2SBSlowWalkSpeed = 100.0f;
	m_flHL2SBJumpPower = 0.0f;	// 0 = engine default (SetJumpPower writes 200)

	// HL2SB: stored-only GMod knobs (GMod defaults).
	m_flHL2SBCrouchedWalkSpeed = 0.34f;
	m_flHL2SBDuckSpeed = 0.4f;
	m_flHL2SBUnDuckSpeed = 0.4f;
	m_flHL2SBLadderClimbSpeed = 100.0f;
	m_flHL2SBStepSize = 18.0f;
	m_bHL2SBAllowWeaponsInVehicle = false;

    m_bEnterObserver = false;
	m_bReady = false;

	BaseClass::ChangeTeam( 0 );
	UseClientSideAnimation();
}

CHL2MP_Player::~CHL2MP_Player( void )
{

}

void CHL2MP_Player::UpdateOnRemove( void )
{
	if ( m_hRagdoll )
	{
		UTIL_RemoveImmediate( m_hRagdoll );
		m_hRagdoll = NULL;
	}

	BaseClass::UpdateOnRemove();
}

void CHL2MP_Player::Precache( void )
{
	BaseClass::Precache();

	PrecacheModel ( "sprites/glow01.vmt" );

	// HL2SB: no hardcoded list to precache any more.  The default playermodel is
	// precached here; every other model is precached on demand by SetPlayerModel.
	PrecacheModel( HL2SB_DEFAULT_PLAYERMODEL );

	PrecacheFootStepSounds();

	PrecacheScriptSound( "NPC_MetroPolice.Die" );
	PrecacheScriptSound( "NPC_CombineS.Die" );
	PrecacheScriptSound( "NPC_Citizen.die" );
}

void CHL2MP_Player::GiveAllItems( void )
{
	EquipSuit();

	CBasePlayer::GiveAmmo( 255,	"Pistol");
	CBasePlayer::GiveAmmo( 255,	"AR2" );
	CBasePlayer::GiveAmmo( 5,	"AR2AltFire" );
	CBasePlayer::GiveAmmo( 255,	"SMG1");
	CBasePlayer::GiveAmmo( 5,	"smg1_grenade");
	CBasePlayer::GiveAmmo( 255,	"Buckshot");
	CBasePlayer::GiveAmmo( 32,	"357" );
	CBasePlayer::GiveAmmo( 3,	"rpg_round");
	CBasePlayer::GiveAmmo( 5,	"grenade" );
	CBasePlayer::GiveAmmo( 5,	"slam" );
	GiveNamedItem( "weapon_crowbar" );
	GiveNamedItem( "weapon_stunstick" );
	GiveNamedItem( "weapon_pistol" );
	GiveNamedItem( "weapon_357" );
	GiveNamedItem( "weapon_smg1" );
	GiveNamedItem( "weapon_ar2" );
	GiveNamedItem( "weapon_shotgun" );
	GiveNamedItem( "weapon_frag" );
	GiveNamedItem( "weapon_crossbow" );
	GiveNamedItem( "weapon_rpg" );
	GiveNamedItem( "weapon_slam" );
	GiveNamedItem( "weapon_physcannon" );
	GiveNamedItem( "weapon_physgun");
	GiveNamedItem( "weapon_toolgun");
}

void CHL2MP_Player::GiveDefaultItems( void )
{
#if defined( LUA_SDK )
	// HL2SB: let Lua gamemode handle weapon loadout if defined.
	// Lua returns false = "I handled it, skip C++ default".
	// Lua returns nil (undefined) = fall through to C++ fallback.
	//
	// Push the full CHL2MP_Player, not CBasePlayer: CHL2MP_Player's __index
	// walks CHL2MP_Player -> CBasePlayer -> CBaseAnimating -> CBaseEntity, so
	// gamemode loadouts can use HL2MP-only methods such as GetPlayerModelType().
	BEGIN_LUA_CALL_HOOK( "GiveDefaultItems" );
		lua_pushhl2mpplayer( L, this );
	END_LUA_CALL_HOOK( 1, 1 );

	RETURN_LUA_NONE();
#endif

	// If we in coop mode, we must spawn without weapons in first maps of HL2
	if ( FStrEq(mode.GetString(), "coop") )
	{
		// Зачем я вообще это сделал? Ведь на картах уже разбросали оружие :P
		//if ( Q_strnicmp( gpGlobals->mapname.ToCStr(), "d1_", 4 ) )
		// тупой хак
		RemoveSuit();
		return;
	}
	else
	{
		GiveAllItems();
	}

}


void CHL2MP_Player::PickDefaultSpawnTeam( void )
{
	if ( GetTeamNumber() == 0 )
	{
		if ( HL2MPRules()->IsTeamplay() == false )
		{
			if ( GetModelPtr() == NULL )
			{
				const char *szModelName = NULL;
				szModelName = engine->GetClientConVarValue( engine->IndexOfEdict( edict() ), "cl_playermodel" );

				if ( ValidatePlayerModel( szModelName ) == false )
				{
					char szReturnString[512];
					Q_snprintf( szReturnString, sizeof (szReturnString ), "cl_playermodel %s\n", HL2SB_DEFAULT_PLAYERMODEL );
					engine->ClientCommand ( edict(), szReturnString );
				}

				ChangeTeam( TEAM_UNASSIGNED );
			}
		}
		else
		{
			CTeam *pCombine = g_Teams[TEAM_COMBINE];
			CTeam *pRebels = g_Teams[TEAM_REBELS];

			if ( pCombine == NULL || pRebels == NULL )
			{
				ChangeTeam( random->RandomInt( TEAM_COMBINE, TEAM_REBELS ) );
			}
			else
			{
				if ( pCombine->GetNumPlayers() > pRebels->GetNumPlayers() )
				{
					ChangeTeam( TEAM_REBELS );
				}
				else if ( pCombine->GetNumPlayers() < pRebels->GetNumPlayers() )
				{
					ChangeTeam( TEAM_COMBINE );
				}
				else
				{
					ChangeTeam( random->RandomInt( TEAM_COMBINE, TEAM_REBELS ) );
				}
			}
		}
	}
}

//-----------------------------------------------------------------------------
// Purpose: Sets HL2 specific defaults.
//-----------------------------------------------------------------------------
void CHL2MP_Player::Spawn(void)
{
	m_flNextModelChangeTime = 0.0f;
	m_flNextTeamChangeTime = 0.0f;

	PickDefaultSpawnTeam();

	BaseClass::Spawn();
	
	if ( !IsObserver() )
	{
		pl.deadflag = false;
		RemoveSolidFlags( FSOLID_NOT_SOLID );

		RemoveEffects( EF_NODRAW );
		
		GiveDefaultItems();
	}

	// HL2SB: turn the flashlight on by default (requires the suit, which GiveDefaultItems equips)
	if ( sv_flashlight_default.GetBool() )
	{
		FlashlightTurnOn();
	}

	SetNumAnimOverlays( 3 );
	ResetAnimation();

	// HL2SB (2026-09-27): a respawn wipes the overlay, so the Lua-side gesture
	// slot bookkeeping must follow (GMod's animstate keeps its slots across
	// life, but its layers die with SetNumAnimOverlays just the same).
	for ( int i = 0; i < GESTURE_SLOT_COUNT; i++ )
	{
		m_iHL2SBSlotLayer[i] = -1;
		m_iHL2SBSlotActivity[i] = ACT_INVALID;
	}

	m_nRenderFX = kRenderNormal;

	m_Local.m_iHideHUD = 0;
	
	AddFlag(FL_ONGROUND); // set the player on the ground at the start of the round.

	m_impactEnergyScale = HL2MPPLAYER_PHYSDAMAGE_SCALE;

	if ( HL2MPRules()->IsIntermission() )
	{
		AddFlag( FL_FROZEN );
	}
	else
	{
		RemoveFlag( FL_FROZEN );
	}

	m_iSpawnInterpCounter = (m_iSpawnInterpCounter + 1) % 8;

	m_Local.m_bDucked = false;

	SetPlayerUnderwater(false);

	m_bReady = false;

#ifdef HL2SB
	// GMod-style: a requested player model change is applied when we (re)spawn.
	// This runs on every spawn/respawn, so hl2sb_setmodel/cl_playermodel changes
	// made while alive take effect on the next death/respawn. SetPlayerModel
	// reads cl_playermodel and applies it (precaching as needed).
	if ( hl2sb_model_respawn_only.GetBool() )
	{
		SetPlayerModel();
	}
	else
	{
		// HL2SB: with respawn-only OFF the model is applied when the convar changes, so
		// the appearance (bodygroups / skin / colours) has to be applied here instead -
		// otherwise nothing ever calls it and the sliders do nothing at all.
		HL2SB_ModelManager_ApplyAppearance( this );
	}
#endif
}

void CHL2MP_Player::PickupObject( CBaseEntity *pObject, bool bLimitMassAndSize )
{
#ifdef LUA_SDK
	BEGIN_LUA_CALL_HOOK( "PlayerPickupObject" );
		lua_pushhl2mpplayer( L, this );
		lua_pushentity( L, pObject );
		lua_pushboolean( L, bLimitMassAndSize );
	END_LUA_CALL_HOOK( 3, 1 );

	RETURN_LUA_NONE();
#endif

#ifdef HL2SB
	// can't pick up what you're standing on
	if ( GetGroundEntity() == pObject )
		return;
	
	if ( bLimitMassAndSize == true )
	{
		if ( CBasePlayer::CanPickupObject( pObject, 35, 128 ) == false )
			 return;
	}

	// Can't be picked up if NPCs are on me
	if ( pObject->HasNPCsOnIt() )
		return;

	PlayerPickupObject( this, pObject );
#else
#endif
}

bool CHL2MP_Player::ValidatePlayerModel( const char *pModel )
{
#ifdef HL2SB
	// Also accept models from HL2SB config
	if ( HL2SB_FindModelConfigByPath( pModel ) )
	{
		return true;
	}

	// HL2SB: and any player model the client has, without a cfg.
	//
	// The list is built the GMod way now - lua/autorun/client/hl2sb_playermodels.lua
	// scans models/player/ and calls player_manager.AddValidModel, which reaches
	// HL2SB_AddRuntimeModelConfig - but an addon that ships nothing but the .mdl (a
	// GMod playermodel addon does exactly that) has to be accepted as well, or picking
	// it in the new player model menu would be silently reverted to the team model.
	// The server precaches it on spawn (SetPlayerModel -> PrecacheModel), and only
	// player-model paths are accepted, so cl_playermodel still cannot turn the player
	// into a crate.
	if ( Q_stristr( pModel, "models/player/" ) != NULL )
	{
		return true;
	}
#endif

	return false;
}

void CHL2MP_Player::SetPlayerTeamModel( void )
{
	// HL2SB (2026-09-27): the per-team random model lists are gone.  In teamplay
	// the player model is still whatever cl_playermodel says - the same rule as
	// the non-teamplay path - not a random citizen/combine head.
	SetPlayerModel();
}

void CHL2MP_Player::SetPlayerModel( void )
{
	const char *szModelName = NULL;
	const char *pszCurrentModelName = modelinfo->GetModelName( GetModel());

	szModelName = engine->GetClientConVarValue( engine->IndexOfEdict( edict() ), "cl_playermodel" );

#ifdef HL2SB
	// Precache the model before using it
	if ( szModelName && szModelName[0] )
	{
		int idx = modelinfo->GetModelIndex( szModelName );
		if ( idx == -1 )
		{
			Msg( "[HL2SB] Precaching model: %s\n", szModelName );
			CBaseEntity::PrecacheModel( szModelName );
		}
	}
#endif

	if ( ValidatePlayerModel( szModelName ) == false )
	{
		char szReturnString[512];

		if ( ValidatePlayerModel( pszCurrentModelName ) == false )
		{
			pszCurrentModelName = HL2SB_DEFAULT_PLAYERMODEL;
		}

		Q_snprintf( szReturnString, sizeof (szReturnString ), "cl_playermodel %s\n", pszCurrentModelName );
		engine->ClientCommand ( edict(), szReturnString );

		szModelName = pszCurrentModelName;
	}

	// HL2SB (2026-09-27): no per-team override any more - GMod semantics, the
	// convar wins.  m_iModelType only classifies the model for the sound prefix.
	if ( Q_strlen( szModelName ) == 0 )
	{
		szModelName = HL2SB_DEFAULT_PLAYERMODEL;
	}

	if ( Q_stristr( szModelName, "models/player/human") )
	{
		m_iModelType = TEAM_REBELS;
	}
	else
	{
		m_iModelType = TEAM_COMBINE;
	}

	int modelIndex = modelinfo->GetModelIndex( szModelName );

	if ( modelIndex == -1 )
	{
		szModelName = HL2SB_DEFAULT_PLAYERMODEL;
		m_iModelType = TEAM_COMBINE;

		char szReturnString[512];

		Q_snprintf( szReturnString, sizeof (szReturnString ), "cl_playermodel %s\n", szModelName );
		engine->ClientCommand ( edict(), szReturnString );
	}

	SetModel( szModelName );
	SetupPlayerSoundsByModel( szModelName );

#ifdef HL2SB
	// HL2SB: and then the appearance - cl_playerbodygroups / cl_playerskin /
	// cl_playercolor / cl_weaponcolor, written by the player model selector's
	// Bodygroups and Colors tabs.
	//
	// ⚠️ It has to be HERE, and AFTER SetModel(): SetModel() resets the bodygroups to
	// the model's own defaults.  Nothing in the tree called
	// HL2SB_ApplyClientAppearance() before (the manager entry points it hangs off,
	// HL2SB_ModelManager_PlayerSpawn / _ClientSettingsChanged, are never called), so
	// the sliders only ever changed the preview - the world model kept its hat even
	// after respawning (2026-09-17, user report).
	HL2SB_ModelManager_ApplyAppearance( this );
#endif

	m_flNextModelChangeTime = gpGlobals->curtime + MODEL_CHANGE_INTERVAL;
}

void CHL2MP_Player::SetupPlayerSoundsByModel( const char *pModelName )
{
	// HL2SB: always land on a valid entry first. The three stock patterns below do
	// not match a custom playermodel, and without a default the field stayed at
	// uninitialised memory -> GetPlayerModelSoundPrefix() indexed
	// g_ppszPlayerSoundPrefixNames[] out of range -> wild pointer -> AV in
	// DeathSound()/footsteps (crash dump 20260915_091815).
	m_iPlayerSoundType = (int)PLAYER_SOUNDS_CITIZEN;

	if ( Q_stristr( pModelName, "models/player/human") )
	{
		m_iPlayerSoundType = (int)PLAYER_SOUNDS_CITIZEN;
	}
	else if ( Q_stristr(pModelName, "police" ) )
	{
		m_iPlayerSoundType = (int)PLAYER_SOUNDS_METROPOLICE;
	}
	else if ( Q_stristr(pModelName, "combine" ) )
	{
		m_iPlayerSoundType = (int)PLAYER_SOUNDS_COMBINESOLDIER;
	}
}

void CHL2MP_Player::ResetAnimation( void )
{
	if ( IsAlive() )
	{
		SetSequence ( -1 );
		SetActivity( ACT_INVALID );

		if (!GetAbsVelocity().x && !GetAbsVelocity().y)
			SetAnimation( PLAYER_IDLE );
		else if ((GetAbsVelocity().x || GetAbsVelocity().y))
			SetAnimation( PLAYER_WALK );
		else if (GetWaterLevel() > 1)
			SetAnimation( PLAYER_WALK );
	}
}


bool CHL2MP_Player::Weapon_Switch( CBaseCombatWeapon *pWeapon, int viewmodelindex )
{
	bool bRet = BaseClass::Weapon_Switch( pWeapon, viewmodelindex );

	if ( bRet == true )
	{
		ResetAnimation();
	}

	return bRet;
}

void CHL2MP_Player::PreThink( void )
{
#ifdef HL2SB
	//Andrew; See http://forums.steampowered.com/forums/showthread.php?t=1372727
	QAngle vOldAngles = GetLocalAngles();
	QAngle vTempAngles = GetLocalAngles();

	vTempAngles = EyeAngles();

	if ( vTempAngles[PITCH] > 180.0f )
	{
		vTempAngles[PITCH] -= 360.0f;
	}

	SetLocalAngles( vTempAngles );
#endif
	BaseClass::PreThink();
	State_PreThink();

	//Reset bullet force accumulator, only lasts one frame
	m_vecTotalBulletForce = vec3_origin;
#ifdef HL2SB
	SetLocalAngles( vOldAngles );
#endif
}

void CHL2MP_Player::PostThink( void )
{
	BaseClass::PostThink();
	
	if ( GetFlags() & FL_DUCKING )
	{
		SetCollisionBounds( VEC_CROUCH_TRACE_MIN, VEC_CROUCH_TRACE_MAX );
	}

	m_PlayerAnimState.Update();

	// HL2SB diagnostic: per-second dump of the SERVER-side gesture layers.
	// The client DUMP showed received weight ~0 / cycle 0 while creation logs
	// said wt=1.00 - this tells whether the server's own layer state decays to
	// 1 tick of weight right after creation (then the send table is faithfully
	// carrying garbage) or stays healthy (then the loss is client-side recv).
	if ( hl2sb_anim_debug.GetBool() )
	{
		static float s_flHL2SBSvOverlayDump[MAX_PLAYERS + 1] = {};
		int slot = entindex();
		if ( slot >= 1 && slot <= MAX_PLAYERS && gpGlobals->curtime >= s_flHL2SBSvOverlayDump[slot] )
		{
			s_flHL2SBSvOverlayDump[slot] = gpGlobals->curtime + 1.0f;
			player_info_t info;
			const char *pszName = engine->GetPlayerInfo( slot, &info ) ? info.name : "?";
			Msg( "[HL2SB overlay/sv] DUMP: ply=%d '%s' count=%d\n", slot, pszName, GetNumAnimOverlays() );
			for ( int k = 0; k < GetNumAnimOverlays(); k++ )
			{
				CAnimationLayer *pLayer = GetAnimOverlay( k );
				Msg( "    [%d] seq=%d order=%d wt=%.4f cycle=%.4f prev=%.4f flags=%d\n",
					 k, (int)pLayer->m_nSequence, (int)pLayer->m_nOrder,
					 (float)pLayer->m_flWeight, (float)pLayer->m_flCycle,
					 (float)pLayer->m_flPrevCycle, (int)pLayer->m_fFlags );
			}
		}
	}

	// HL2SB (2026-09-27): GM:UpdateAnimation( ply, velocity, maxSeqGroundSpeed )
	// - animations.lua:197.  The base gamemode writes SetPlaybackRate and the
	// vehicle pose parameters here every frame, exactly like GMod; the client
	// realm dispatches the same hook for its own pose/flex work (MouthMove).
	{
		float flGroundSpeed = GetSequenceGroundSpeed( GetSequence() );
		BEGIN_LUA_CALL_HOOK( "UpdateAnimation" );
			lua_pushplayer( L, this );
			lua_pushvector( L, GetAbsVelocity() );
			lua_pushnumber( L, flGroundSpeed );
		END_LUA_CALL_HOOK( 3, 0 );
	}

	// Store the eye angles pitch so the client can compute its animation state correctly.
	m_angEyeAngles = EyeAngles();

	QAngle angles = GetLocalAngles();
	angles[PITCH] = 0;
	SetLocalAngles( angles );
}

void CHL2MP_Player::PlayerDeathThink()
{
#if defined ( LUA_SDK )
	BEGIN_LUA_CALL_HOOK( "PlayerDeathThink" );
		lua_pushhl2mpplayer( L, this );
	END_LUA_CALL_HOOK( 1, 0 );
#endif
	if( !IsObserver() )
	{
		BaseClass::PlayerDeathThink();
	}
}

void CHL2MP_Player::FireBullets ( const FireBulletsInfo_t &info )
{
	// Move other players back to history positions based on local player's lag
	lagcompensation->StartLagCompensation( this, this->GetCurrentCommand() );

	FireBulletsInfo_t modinfo = info;

	CWeaponHL2MPBase *pWeapon = dynamic_cast<CWeaponHL2MPBase *>( GetActiveWeapon() );

	if ( pWeapon )
	{
		modinfo.m_iPlayerDamage = modinfo.m_flDamage = pWeapon->GetHL2MPWpnData().m_iPlayerDamage;
	}

	NoteWeaponFired();

	BaseClass::FireBullets( modinfo );

	// Move other players back to history positions based on local player's lag
	lagcompensation->FinishLagCompensation( this );
}

void CHL2MP_Player::NoteWeaponFired( void )
{
	Assert( m_pCurrentCommand );
	if( m_pCurrentCommand )
	{
		m_iLastWeaponFireUsercmd = m_pCurrentCommand->command_number;
	}
}

extern ConVar sv_maxunlag;

bool CHL2MP_Player::WantsLagCompensationOnEntity( const CBasePlayer *pPlayer, const CUserCmd *pCmd, const CBitVec<MAX_EDICTS> *pEntityTransmitBits ) const
{
	// No need to lag compensate at all if we're not attacking in this command and
	// we haven't attacked recently.
	if ( !( pCmd->buttons & IN_ATTACK ) && !( pCmd->buttons & IN_ATTACK2 ) && (pCmd->command_number - m_iLastWeaponFireUsercmd > 5) )
		return false;

	// If this entity hasn't been transmitted to us and acked, then don't bother lag compensating it.
	if ( pEntityTransmitBits && !pEntityTransmitBits->Get( pPlayer->entindex() ) )
		return false;

	const Vector &vMyOrigin = GetAbsOrigin();
	const Vector &vHisOrigin = pPlayer->GetAbsOrigin();

	// get max distance player could have moved within max lag compensation time, 
	// multiply by 1.5 to to avoid "dead zones"  (sqrt(2) would be the exact value)
	float maxDistance = 1.5 * pPlayer->MaxSpeed() * sv_maxunlag.GetFloat();

	// If the player is within this distance, lag compensate them in case they're running past us.
	if ( vHisOrigin.DistTo( vMyOrigin ) < maxDistance )
		return true;

	// If their origin is not within a 45 degree cone in front of us, no need to lag compensate.
	Vector vForward;
	AngleVectors( pCmd->viewangles, &vForward );
	
	Vector vDiff = vHisOrigin - vMyOrigin;
	VectorNormalize( vDiff );

	float flCosAngle = 0.707107f;	// 45 degree angle
	if ( vForward.Dot( vDiff ) < flCosAngle )
		return false;

	return true;
}

Activity CHL2MP_Player::TranslateTeamActivity( Activity ActToTranslate )
{
	if ( m_iModelType == TEAM_COMBINE )
		 return ActToTranslate;
	
	if ( ActToTranslate == ACT_RUN )
		 return ACT_RUN_AIM_AGITATED;

	if ( ActToTranslate == ACT_IDLE )
		 return ACT_IDLE_AIM_AGITATED;

	if ( ActToTranslate == ACT_WALK )
		 return ACT_WALK_AIM_AGITATED;

	return ActToTranslate;
}

extern ConVar hl2_normspeed;
extern ConVar hl2sb_anim_debug;

//-----------------------------------------------------------------------------
// HL2SB (2026-09-27): GMod player-animation glue.  The three hooks below are
// the exact contract GMod's base gamemode animations.lua drives player
// animation with (hook names resolved through lua_shared.dll's id->name
// registry; the client.dll/server.dll binaries carry no hook strings).
//-----------------------------------------------------------------------------

// GM:DoAnimationEvent( ply, event, data ) - animations.lua:361.  Returns the
// viewmodel activity the event should play (ACT_INVALID for "none").  Answers
// false when the gamemode has no Lua handler, so callers fall back to the
// built-in HL2MP path.
bool CHL2MP_Player::HL2SB_DoAnimationEventLua( PlayerAnimEvent_t event, int nData )
{
	bool bHandled = false;

	BEGIN_LUA_CALL_HOOK( "DoAnimationEvent" );
		lua_pushplayer( L, this );
		lua_pushinteger( L, event );
		lua_pushinteger( L, nData );
	END_LUA_CALL_HOOK( 3, 1 );

	if ( lua_isnumber( L, -1 ) )
	{
		bHandled = true;
		Activity vmAct = (Activity)lua_tointeger( L, -1 );
		// GMod returns ACT_VM_* to drive the viewmodel, ACT_INVALID for "no
		// viewmodel change" (reload/jump answer ACT_INVALID in animations.lua).
		if ( vmAct > ACT_INVALID )
		{
			Weapon_SetActivity( vmAct, 0 );
		}
	}
	lua_pop( L, 1 );

	return bHandled;
}

// GM:TranslateActivity( ply, act ) - animations.lua:348 (weapon acttable plus
// the ACT_HL2MP_* idle-family fallback).  Returns 'fallback' when the gamemode
// does not answer, which keeps the built-in Weapon_TranslateActivity() chain.
Activity CHL2MP_Player::HL2SB_TranslateActivityLua( Activity act, Activity fallback )
{
	Activity translated = fallback;

	BEGIN_LUA_CALL_HOOK( "TranslateActivity" );
		lua_pushplayer( L, this );
		lua_pushinteger( L, act );
	END_LUA_CALL_HOOK( 2, 1 );

	if ( lua_isnumber( L, -1 ) )
		translated = (Activity)lua_tointeger( L, -1 );
	lua_pop( L, 1 );

	return translated;
}

void CHL2MP_Player::HL2SB_AnimRestartGesture( int iSlot, Activity activity, bool bRestart )
{
	if ( iSlot < 0 || iSlot >= GESTURE_SLOT_COUNT || activity <= ACT_INVALID )
		return;

	// Stale bookkeeping: the layer finished (autokill) since the slot was
	// filled - forget it so the gesture can start again.
	if ( m_iHL2SBSlotLayer[iSlot] >= 0 && !IsValidLayer( m_iHL2SBSlotLayer[iSlot] ) )
	{
		m_iHL2SBSlotLayer[iSlot] = -1;
		m_iHL2SBSlotActivity[iSlot] = ACT_INVALID;
	}

	// GMod's authoritative semantics (CMultiPlayerAnimState::RestartGesture,
	// game/shared/Multiplayer/multiplayer_animstate.cpp:545): asking for the
	// activity an active slot is ALREADY playing RESETS its cycle - it replays.
	// That is what makes a second shot / a re-triggered gesture visible again;
	// returning early here (the first version) meant fire-fire only animated the
	// first shot.  A different activity (or an empty slot) starts a new layer.
	if ( m_iHL2SBSlotLayer[iSlot] >= 0 && m_iHL2SBSlotActivity[iSlot] == activity )
	{
		if ( bRestart )
			SetLayerCycle( m_iHL2SBSlotLayer[iSlot], 0.0f, 0.0f );

		return;
	}

	if ( m_iHL2SBSlotLayer[iSlot] >= 0 )
		RemoveLayer( m_iHL2SBSlotLayer[iSlot], 0.0f, 0.0f );

	// GMod gestures play through the weapon acttable (ACT_MP_ATTACK_* ->
	// ACT_HL2MP_GESTURE_RANGE_ATTACK_<holdtype>); fall back to the bare
	// activity for model-authored layer activities (ACT_GMOD_IN_CHAT,
	// ACT_GMOD_NOCLIP_LAYER).
	Activity translated = Weapon_TranslateActivity( activity );
	int iSequence = SelectWeightedSequence( translated );
	if ( iSequence <= 0 )
		iSequence = SelectWeightedSequence( activity );
	if ( iSequence <= 0 )
	{
		if ( hl2sb_anim_debug.GetBool() )
		{
			player_info_t info;
			const char *pszName = engine->GetPlayerInfo( entindex(), &info ) ? info.name : "?";
			Msg( "[HL2SB gesture/sv] NO SEQUENCE: ply=%d '%s' slot=%d act=%s translated=%s (model %s has no matching gesture activity - layer NOT created)\n",
				 entindex(), pszName, iSlot, ActivityList_NameForIndex( (int)activity ),
				 ActivityList_NameForIndex( (int)translated ), STRING( GetModelName() ) );
		}
		m_iHL2SBSlotLayer[iSlot] = -1;
		m_iHL2SBSlotActivity[iSlot] = ACT_INVALID;
		return;
	}

	int iLayer = AddGestureSequence( iSequence, true );
	if ( iLayer < 0 )
	{
		if ( hl2sb_anim_debug.GetBool() )
		{
			Msg( "[HL2SB gesture/sv] ADD LAYER FAILED: ply=%d slot=%d seq=%d (%s)\n",
				 entindex(), iSlot, iSequence, ActivityList_NameForIndex( (int)activity ) );
		}
		m_iHL2SBSlotLayer[iSlot] = -1;
		m_iHL2SBSlotActivity[iSlot] = ACT_INVALID;
		return;
	}

	if ( hl2sb_anim_debug.GetBool() )
	{
		player_info_t info;
		const char *pszName = engine->GetPlayerInfo( entindex(), &info ) ? info.name : "?";
		Msg( "[HL2SB gesture/sv] OK: ply=%d '%s' slot=%d act=%s translated=%s seq=%d layer=%d wt=%.2f\n",
			 entindex(), pszName, iSlot, ActivityList_NameForIndex( (int)activity ),
			 ActivityList_NameForIndex( (int)translated ), iSequence, iLayer,
			 GetLayerWeight( iLayer ) );
	}

	SetLayerWeight( iLayer, 1.0f );

	m_iHL2SBSlotLayer[iSlot] = iLayer;
	m_iHL2SBSlotActivity[iSlot] = activity;
}

void CHL2MP_Player::HL2SB_AnimResetGestureSlot( int iSlot )
{
	if ( iSlot < 0 || iSlot >= GESTURE_SLOT_COUNT )
		return;

	if ( m_iHL2SBSlotLayer[iSlot] >= 0 && IsValidLayer( m_iHL2SBSlotLayer[iSlot] ) )
		RemoveLayer( m_iHL2SBSlotLayer[iSlot], 0.0f, 0.0f );

	m_iHL2SBSlotLayer[iSlot] = -1;
	m_iHL2SBSlotActivity[iSlot] = ACT_INVALID;
}

void CHL2MP_Player::HL2SB_AnimSetGestureWeight( int iSlot, float flWeight )
{
	if ( iSlot < 0 || iSlot >= GESTURE_SLOT_COUNT )
		return;

	if ( m_iHL2SBSlotLayer[iSlot] >= 0 && IsValidLayer( m_iHL2SBSlotLayer[iSlot] ) )
		SetLayerWeight( m_iHL2SBSlotLayer[iSlot], clamp( flWeight, 0.0f, 1.0f ) );
}

bool CHL2MP_Player::HL2SB_IsPlayingTaunt( void )
{
	// GMod (reference x86 server.dll, ): the taunt state is exactly
	// this comparison against the replicated end time -- no extra flag.
	const float flEnd = m_flHL2SBTauntEnd;
	return ( gpGlobals->curtime <= flEnd ) && ( flEnd != gpGlobals->curtime );
}

// GMod: AnimSetGestureSequence( slot, sequence ) - swap the slot's layer to a
// raw sequence (bypasses activity translation).
void CHL2MP_Player::HL2SB_AnimSetGestureSequence( int iSlot, int iSequence )
{
	if ( iSlot < 0 || iSlot >= GESTURE_SLOT_COUNT || iSequence <= 0 )
		return;

	if ( m_iHL2SBSlotLayer[iSlot] >= 0 && IsValidLayer( m_iHL2SBSlotLayer[iSlot] ) )
		RemoveLayer( m_iHL2SBSlotLayer[iSlot], 0.0f, 0.0f );

	int iLayer = AddGestureSequence( iSequence, true );
	if ( iLayer >= 0 )
	{
		SetLayerWeight( iLayer, 1.0f );
		m_iHL2SBSlotLayer[iSlot] = iLayer;
		m_iHL2SBSlotActivity[iSlot] = ACT_INVALID;
	}
	else
	{
		m_iHL2SBSlotLayer[iSlot] = -1;
		m_iHL2SBSlotActivity[iSlot] = ACT_INVALID;
	}
}

// GMod movement: players run at SetRunSpeed (400) and drop to SetWalkSpeed
// (150) only while the walk key is held.  This supersedes the CHL2 PreThink
// walk/sprint MaxSpeed writes (hl2_walkspeed / hl2_normspeed /
// hl2_sprintspeed), matching GMod's player-class-driven speeds.
float CHL2MP_Player::GetPlayerMaxSpeed( void )
{
	if ( IsObserver() || !IsAlive() )
		return BaseClass::GetPlayerMaxSpeed();

	if ( m_nButtons & IN_WALK )
		return m_flHL2SBWalkSpeed;

	return m_flHL2SBRunSpeed;
}

// Set the activity based on an event or current state
void CHL2MP_Player::SetAnimation( PLAYER_ANIM playerAnim )
{
	int animDesired;

	float speed;

	speed = GetAbsVelocity().Length2D();

	// HL2SB: while riding a vehicle a passenger's velocity is the vehicle's, so the
	// state machine below kept selecting the RUN sequences and the seated model looked
	// like it was sprinting. GMod poses riders with the seat's OWN animation, which the
	// seat entity carries as Members.HandleAnimation - for every chair / pod seat in
	// GMod's list that is `player:SelectWeightedSequence( ACT_GMOD_SIT_ROLLERCOASTER )`
	// (see HL2SB_SelectVehicleSitSequence). The whole chain - the vehicle's own seat pose
	// and the sit_<holdtype> fallback - lives in HL2SB_ResolveSeatedSequence() so the
	// CLIENT can resolve the very same sequence (it needs it to tell "still seated" from
	// "the exit animation is running" - see UpdateVehicleAnimation()).
	//
	// HL2SB: THE PARENT TEST is what ends the seat pose on dismount. Leaving a vehicle
	// runs the other way round from entering it: CBaseServerVehicle::HandlePassengerExit()
	// starts the vehicle's exit animation and UNPARENTS the player at its START
	// (vehicle_baseserver.cpp SetParent( NULL )), while CBasePlayer::LeaveVehicle() - the
	// only place m_hVehicle is cleared - runs when that animation FINISHES. So for the
	// 0.2-0.5s in between, IsInAVehicle() is still true and this branch kept re-pinning
	// the sit sequence: the reported "the model stays in the seat pose for a moment after
	// dismounting". GMod keys off exactly this condition
	// (gamemodes/base/gamemode/animations.lua:144: "The player must have a parent to be in
	// a vehicle. If there's no parent, we are in the exit anim, so don't do sitting in 3rd
	// person anymore"), and the moment the test fails the normal path below (a few lines
	// down - idle/walk/run) takes over, so a fresh gait is selected on the same frame.
	if ( IsInAVehicle() && IsAlive() && GetMoveParent() != NULL )
	{
		// HL2SB: the reference resolves the seat pose IN LUA - animations.lua's
		// HandlePlayerDriving runs the vehicle list entry's Members.HandleAnimation
		// when it defines one (chairs: ACT_GMOD_SIT_ROLLERCOASTER) and otherwise
		// answers from its hardcoded per-class table (jeep -> drive_jeep,
		// airboat -> drive_airboat, prisoner pod (inner model) -> drive_pd,
		// anything else -> sit_rollercoaster), with the sit_<holdtype> family
		// overriding the plain sit when weapons are allowed in the vehicle.
		// Dispatch GM:CalcMainActivity exactly like the unseated path and pin
		// what the Lua answers; the C++ resolver only fills the gap when no Lua
		// gamemode is there to answer.
		Activity seatActivity = ACT_INVALID;
		int animDesired = -1;
		{
			BEGIN_LUA_CALL_HOOK( "CalcMainActivity" );
				lua_pushplayer( L, this );
				lua_pushvector( L, GetAbsVelocity() );
			END_LUA_CALL_HOOK( 2, 2 );

			if ( lua_isnumber( L, -2 ) )
			{
				seatActivity = (Activity)lua_tointeger( L, -2 );
				if ( lua_isnumber( L, -1 ) )
					animDesired = lua_tointeger( L, -1 );
			}
			lua_pop( L, 2 );
		}

		if ( animDesired <= 0 )
		{
			animDesired = HL2SB_ResolveSeatedSequence( this, GetVehicleEntity(), &seatActivity );
		}

		if ( ( animDesired > 0 ) && ( animDesired != GetSequence() ) )
		{
			if ( GetActivity() != seatActivity )
				SetActivity( seatActivity );

			m_flPlaybackRate = 1.0f;
			ResetSequence( animDesired );
			SetCycle( 0 );
		}

		return;
	}

	
	// bool bRunning = true;

	//Revisit!
/*	if ( ( m_nButtons & ( IN_FORWARD | IN_BACK | IN_MOVELEFT | IN_MOVERIGHT ) ) )
	{
		if ( speed > 1.0f && speed < hl2_normspeed.GetFloat() - 20.0f )
		{
			bRunning = false;
		}
	}*/

	if ( GetFlags() & ( FL_FROZEN | FL_ATCONTROLS ) )
	{
		speed = 0;
		playerAnim = PLAYER_IDLE;
	}

	// HL2SB: the die check must survive the Lua override below - a dying player
	// never gets a main-sequence change (GMod's animstate handles death itself).
	if ( playerAnim == PLAYER_DIE && m_lifeState == LIFE_ALIVE )
		return;

	Activity idealActivity = ACT_HL2MP_RUN;

	// HL2SB (2026-09-27): GM:CalcMainActivity( ply, velocity ) -
	// animations.lua:305.  Returns ( idealActivity, sequenceOverride ); -1
	// override = "no sequence pin".  The ACT_MP_* ideal flows through the same
	// weapon-acttable / name-pin chain below.  When the gamemode does not
	// answer, the built-in HL2MP state machine (kept verbatim) runs.
	int iLuaSeqOverride = -1;
	bool bLuaIdeal = false;
	{
		BEGIN_LUA_CALL_HOOK( "CalcMainActivity" );
			lua_pushplayer( L, this );
			lua_pushvector( L, GetAbsVelocity() );
		END_LUA_CALL_HOOK( 2, 2 );

		if ( lua_isnumber( L, -2 ) )
		{
			bLuaIdeal = true;
			idealActivity = (Activity)lua_tointeger( L, -2 );
			if ( lua_isnumber( L, -1 ) )
				iLuaSeqOverride = lua_tointeger( L, -1 );
		}
		lua_pop( L, 2 );
	}

	// HL2SB (2026-09-27): the EVENT dispatch below (jump / attack / reload) must run
	// whether or not GM:CalcMainActivity answered.  GMod starts its attack and reload
	// GESTURES from GM:DoAnimationEvent, and gating them on !bLuaIdeal meant that as
	// soon as a gamemode defined CalcMainActivity (i.e. always - the ported
	// animations.lua does) firing and reloading stopped animating the player at all.
	// Only the built-in GAIT selection (idle/walk/run/crouch/swim) is skipped when
	// Lua supplied the ideal.
	//
	// This could stand to be redone. Why is playerAnim abstracted from activity? (sjb)
	if ( playerAnim == PLAYER_JUMP )
	{
		// HL2SB: the jump ANIMEVENT goes to Lua first (animations.lua:388 sets
		// m_bJumping and restarts the main sequence); the ACT_HL2MP_JUMP
		// selection below still runs so a gamemode without Lua keeps jumping.
		HL2SB_DoAnimationEventLua( PLAYERANIMEVENT_JUMP, 0 );

		idealActivity = ACT_HL2MP_JUMP;
	}
	else if ( playerAnim == PLAYER_DIE )
	{
		if ( m_lifeState == LIFE_ALIVE )
		{
			return;
		}
	}
	else if ( playerAnim == PLAYER_ATTACK1 )
	{
		// HL2SB: GMod plays attacks as a GESTURE_SLOT_ATTACK_AND_RELOAD gesture
		// from GM:DoAnimationEvent (animations.lua:363), never touching the
		// main sequence.  When Lua answers, the viewmodel activity the handler
		// returned was already applied - done.
		if ( HL2SB_DoAnimationEventLua( PLAYERANIMEVENT_ATTACK_PRIMARY, 0 ) )
		{
			if ( hl2sb_anim_debug.GetBool() )
			{
				player_info_t info;
				const char *pszName = engine->GetPlayerInfo( entindex(), &info ) ? info.name : "?";
				Msg( "[HL2SB gesture/sv] ATTACK1 path=Lua (GM:DoAnimationEvent answered) ply=%d '%s'\n",
					 entindex(), pszName );
			}
			return;
		}

		if ( hl2sb_anim_debug.GetBool() )
		{
			player_info_t info;
			const char *pszName = engine->GetPlayerInfo( entindex(), &info ) ? info.name : "?";
			Msg( "[HL2SB gesture/sv] ATTACK1 path=C++ fallback (no Lua handler) ply=%d '%s'\n",
				 entindex(), pszName );
		}

		if ( GetActivity( ) == ACT_HOVER	||
			 GetActivity( ) == ACT_SWIM		||
			 GetActivity( ) == ACT_HOP		||
			 GetActivity( ) == ACT_LEAP		||
			 GetActivity( ) == ACT_DIESIMPLE )
		{
			idealActivity = GetActivity( );
		}
		else
		{
			idealActivity = ACT_HL2MP_GESTURE_RANGE_ATTACK;
		}
	}
	else if ( playerAnim == PLAYER_RELOAD )
	{
		if ( HL2SB_DoAnimationEventLua( PLAYERANIMEVENT_RELOAD, 0 ) )
			return;

		idealActivity = ACT_HL2MP_GESTURE_RELOAD;
	}
	else if ( playerAnim == PLAYER_IDLE || playerAnim == PLAYER_WALK )
	{
		// HL2SB: with a gamemode the gait ideal already came from
		// GM:CalcMainActivity above, so the built-in machine is skipped.
		if ( !bLuaIdeal )
		{
		// HL2SB: swimming is checked before the jump hold, exactly like GMod's
		// CalcMainActivity order (animations.lua:113 HandlePlayerSwimming is
		// after HandlePlayerJumping, but animations.lua:13 ends a jump as soon
		// as WaterLevel() > 0, so dropping into water ends the jump pose).
		// WaterLevel >= 2 and off the ground = swimming. The old commented block
		// below posed ACT_HOVER / ACT_SWIM - singleplayer HL2 activities no
		// player anim model carries. ACT_HL2MP_SWIM goes through the weapon
		// acttable (ACT_HL2MP_SWIM_<holdtype>, e.g. weapon_357.cpp:95) and then
		// HL2SB_SelectPlayerSequence pins "swimming_<holdtype>" / bare
		// "swimming" - all shipped by GMod's m_anm/f_anm/z_anm (mdl-bound
		// ACT_HL2MP_SWIM_* names, verified 2026-09-24).
		if ( GetWaterLevel() >= WL_Waist && !( GetFlags() & FL_ONGROUND ) )
		{
			idealActivity = ACT_HL2MP_SWIM;
		}
		else if ( !( GetFlags() & FL_ONGROUND ) && GetActivity( ) == ACT_HL2MP_JUMP )	// Still jumping
		{
			idealActivity = GetActivity( );
		}
		/*
		else if ( GetWaterLevel() > 1 )
		{
			if ( speed == 0 )
				idealActivity = ACT_HOVER;
			else
				idealActivity = ACT_SWIM;
		}
		*/
		else
		{
			if ( GetFlags() & FL_DUCKING )
			{
				if ( speed > 0 )
				{
					idealActivity = ACT_HL2MP_WALK_CROUCH;
				}
				else
				{
					idealActivity = ACT_HL2MP_IDLE_CROUCH;
				}
			}
			else
			{
				if ( speed > 0 )
				{
					// HL2SB: GMod's walk/run split - "speed > 150 -> RUN,
					// > 0.5 -> WALK" (gamemodes/base/gamemode/animations.lua:321) -
					// with 20 units of hysteresis. Switching the activity switches
					// the sequence and ResetSequence()s it, so a speed sitting right
					// on the threshold used to restart the legs every frame (the same
					// failure as AGENTS.md 20).
					//
					// Whether WALK can play at all is decided by the model: HL2MP's
					// own anim models only ship run_*. HL2SB_SelectPlayerSequence()
					// reports that with ACT_INVALID and the code below then keeps
					// running.
					const float flWalkRunThreshold = 150.0f;
					const float flHysteresis = 20.0f;

					bool bRunning = ( speed > flWalkRunThreshold );

					if ( GetActivity() == ACT_HL2MP_WALK && speed < flWalkRunThreshold + flHysteresis )
					{
						bRunning = false;
					}
					else if ( GetActivity() == ACT_HL2MP_RUN && speed > flWalkRunThreshold - flHysteresis )
					{
						bRunning = true;
					}

					idealActivity = bRunning ? ACT_HL2MP_RUN : ACT_HL2MP_WALK;
				}
				else
				{
					idealActivity = ACT_HL2MP_IDLE;
				}
			}
		}

		idealActivity = TranslateTeamActivity( idealActivity );
		}	// end if ( !bLuaIdeal )
	}

	if ( idealActivity == ACT_HL2MP_GESTURE_RANGE_ATTACK )
	{
		RestartGesture( Weapon_TranslateActivity( idealActivity ) );

		// HL2SB (2026-09-30): Lua SWEPs skip this.  CBaseCombatWeapon::
		// SetActivity is Valve's own "Oh man..." hack -- it flips the weapon
		// to its WORLD model to resolve the range-attack sequence, flips back
		// and forces that world-model sequence index onto the viewmodel
		// (or force-idles it with SetCycle(0)+ResetSequenceInfo when the
		// activity is missing, which is every combined SWEP model).  GMod
		// never runs this for scripted weapons: the body fire gesture is the
		// Lua animation layer's ACT_MP_* restart (visible in the probe log),
		// and the extra per-shot model/sequence churn was the "plays another
		// animation after each shot" report on the Nyan Gun.
		CBaseCombatWeapon *pFireWeapon = GetActiveWeapon();
		if ( pFireWeapon == NULL || !pFireWeapon->IsScripted() )
		{
			Weapon_SetActivity( Weapon_TranslateActivity( ACT_RANGE_ATTACK1 ), 0 );
		}

		return;
	}
	else if ( idealActivity == ACT_HL2MP_GESTURE_RELOAD )
	{
		RestartGesture( Weapon_TranslateActivity( idealActivity ) );
		return;
	}
	else
	{
		// HL2SB: pin the sequence by NAME ("run_pistol", "cwalk_ar2",
		// "idle_gravgun", ... - see HL2SB_SelectPlayerSequence()) instead of
		// trusting the activity -> hold type -> weighted-random-sequence chain.
		Activity translatedActivity;
		if ( bLuaIdeal )
		{
			// GMod order: the ACT_MP_* ideal goes through GM:TranslateActivity
			// (animations.lua:348 - weapon acttable, then the IdleActivityTranslate
			// ACT_HL2MP_* fallback).  No Lua answer -> plain weapon translation.
			translatedActivity = HL2SB_TranslateActivityLua( idealActivity, Weapon_TranslateActivity( idealActivity ) );
		}
		else
		{
			translatedActivity = Weapon_TranslateActivity( idealActivity );
		}

		animDesired = HL2SB_SelectPlayerSequence( this, translatedActivity, idealActivity );

		// HL2SB: a sequence override from GM:CalcMainActivity's second return
		// (CalcSeqOverride in animations.lua) pins the sequence outright.
		if ( iLuaSeqOverride > -1 )
		{
			animDesired = iLuaSeqOverride;
		}
		// A model with no walk animation (HL2MP's own anim models only ship run_*)
		// answers ACT_INVALID here: keep running instead of dropping to
		// sequence 0, which is what the old fallback chain would have done.
		else if ( animDesired == -1 && ( idealActivity == ACT_HL2MP_WALK || idealActivity == ACT_MP_WALK ) )
		{
			idealActivity = ( idealActivity == ACT_MP_WALK ) ? ACT_MP_RUN : ACT_HL2MP_RUN;

			translatedActivity = Weapon_TranslateActivity( idealActivity );
			animDesired = HL2SB_SelectPlayerSequence( this, translatedActivity, idealActivity );
		}

		if (animDesired == -1)
		{
			animDesired = SelectWeightedSequence( translatedActivity );

			if ( animDesired == -1 )
			{
				animDesired = SelectWeightedSequence( idealActivity );

				if ( animDesired == -1 )
				{
					animDesired = 0;
				}
			}
		}
	
		// HL2SB: the pin above is deterministic, so "am I already playing the
		// sequence the pin wants" is a valid test again - and it has to happen
		// BEFORE SetActivity(), which selects a sequence for the *base* activity
		// (ACT_HL2MP_RUN -> the bare "run" sequence) and would otherwise make the
		// test fail on every single frame: that was the real "legs freeze after a
		// few frames" bug (AGENTS 20 / 27). ResetSequence() also bumps the sequence
		// parity, which makes the CLIENT reset its own cycle on the next snapshot,
		// so the freeze showed up on both realms.
		if ( GetSequence() == animDesired )
			return;

		// HL2SB: remember whether the previous activity was a crouch one, so the
		// stand<->crouch boundary below can cross-fade instead of hard-cutting. The
		// client m_SequenceTransitioner only blends when the sequence changes without
		// ResetSequence() re-zeroing the cycle every time; doing ResetSequence + SetCycle(0)
		// on the crouch edge is what made ducking look a beat late.
		//
		// HL2SB: the GM:CalcMainActivity path hands us GMod's ACT_MP_CROUCH_IDLE /
		// ACT_MP_CROUCHWALK, so the test has to accept both vocabularies - matching
		// only ACT_HL2MP_* made bIsCrouch permanently false for Lua-driven animation,
		// which re-zeroed the cycle on every crouch frame (visible as a stuttering
		// crouch, and for hold types whose cwalk_* the model lacks it dropped to
		// sequence 0).
		const bool bWasCrouch = ( GetActivity() == ACT_HL2MP_IDLE_CROUCH ||
								  GetActivity() == ACT_HL2MP_WALK_CROUCH ||
								  GetActivity() == ACT_MP_CROUCH_IDLE ||
								  GetActivity() == ACT_MP_CROUCHWALK );
		const bool bIsCrouch  = ( idealActivity == ACT_HL2MP_IDLE_CROUCH ||
								  idealActivity == ACT_HL2MP_WALK_CROUCH ||
								  idealActivity == ACT_MP_CROUCH_IDLE ||
								  idealActivity == ACT_MP_CROUCHWALK );

		// Activity bookkeeping: GetActivity() is read by the jump case above and by
		// the walk/run split. Only touch it when it really changes, so the sequence
		// pinned above is not thrown away every frame.
		if ( GetActivity() != idealActivity )
			SetActivity( idealActivity );

		m_flPlaybackRate = 1.0;

		// HL2SB: GMod animations.lua:208 - "if we're under water we want to
		// constantly be swimming": the stroke plays at velocity/sequence-ground-
		// speed like land gaits would, never slower than half rate, so treading
		// water keeps cycling instead of freezing on one frame.
		if ( GetWaterLevel() >= WL_Waist && !( GetFlags() & FL_ONGROUND ) )
		{
			float flGroundSpeed = GetSequenceGroundSpeed( animDesired );
			float flRate = ( speed > 0.2f && flGroundSpeed > 0.001f )
								? ( speed / flGroundSpeed )
								: 1.0f;
			m_flPlaybackRate = clamp( flRate, 0.5f, 2.0f );
		}

		// GMod-style: on a stand<->crouch transition keep the phase we are on (do not
		// restart at cycle 0) and let the client blend, so ducking is smooth instead of
		// snapping to the crouch idles' first frame.
		const bool bCrouchEdge = ( bWasCrouch != bIsCrouch );
		const float flKeepCycle = bCrouchEdge ? GetCycle() : 0.0f;

		ResetSequence( animDesired );
		if ( bCrouchEdge )
			SetCycle( flKeepCycle );
		else
			SetCycle( 0 );
		return;
	}

	// Already using the desired animation?
	if ( GetSequence() == animDesired )
		return;

	//Msg( "Set animation to %d\n", animDesired );
	// Reset to first frame of desired animation
	ResetSequence( animDesired );
	SetCycle( 0 );
}


extern int	gEvilImpulse101;
//-----------------------------------------------------------------------------
// Purpose: Player reacts to bumping a weapon. 
// Input  : pWeapon - the weapon that the player bumped into.
// Output : Returns true if player picked up the weapon
//-----------------------------------------------------------------------------
bool CHL2MP_Player::BumpWeapon( CBaseCombatWeapon *pWeapon )
{
	CBaseCombatCharacter *pOwner = pWeapon->GetOwner();

	// Can I have this weapon type?
	if ( !IsAllowedToPickupWeapons() )
		return false;

	if ( pOwner || !Weapon_CanUse( pWeapon ) || !g_pGameRules->CanHavePlayerItem( this, pWeapon ) )
	{
		if ( gEvilImpulse101 )
		{
			UTIL_Remove( pWeapon );
		}
		return false;
	}

	// Don't let the player fetch weapons through walls (use MASK_SOLID so that you can't pickup through windows)
	if( !pWeapon->FVisible( this, MASK_SOLID ) && !(GetFlags() & FL_NOTARGET) )
	{
		return false;
	}

	bool bOwnsWeaponAlready = !!Weapon_OwnsThisType( pWeapon->GetClassname(), pWeapon->GetSubType());

	if ( bOwnsWeaponAlready == true ) 
	{
		//If we have room for the ammo, then "take" the weapon too.
		 if ( Weapon_EquipAmmoOnly( pWeapon ) )
		 {
			 pWeapon->CheckRespawn();
			 UTIL_Remove( pWeapon );
			 return true;
		 }
		 else
		 {
			 return false;
		 }
	}

	pWeapon->CheckRespawn();
	Weapon_Equip( pWeapon );

#ifdef HL2SB
	// HL2SB: notify the client HUD that the local player picked up a weapon
	// (drives the ported GMod pickup animation).  item = weapon classname.
	{
		IGameEvent *event = gameeventmanager->CreateEvent( "item_pickup" );
		if ( event )
		{
			event->SetInt( "userid", GetUserID() );
			event->SetString( "item", pWeapon->GetClassname() );
			event->SetInt( "amount", 0 );
			gameeventmanager->FireEvent( event );
		}
	}
#endif

	return true;
}

void CHL2MP_Player::ChangeTeam( int iTeam )
{
	bool bKill = false;
	bool bWasSpectator = false;

	if ( HL2MPRules()->IsTeamplay() != true && iTeam != TEAM_SPECTATOR )
	{
		//don't let them try to join combine or rebels during deathmatch.
		iTeam = TEAM_UNASSIGNED;
	}

	if ( HL2MPRules()->IsTeamplay() == true )
	{
		if ( iTeam != GetTeamNumber() && GetTeamNumber() != TEAM_UNASSIGNED )
		{
			bKill = true;
		}
	}

	if (this->GetTeamNumber() == TEAM_SPECTATOR)
	{
		bWasSpectator = true;
	}

	BaseClass::ChangeTeam( iTeam );

	m_flNextTeamChangeTime = gpGlobals->curtime + TEAM_CHANGE_INTERVAL;

	if ( HL2MPRules()->IsTeamplay() == true )
	{
		SetPlayerTeamModel();
	}
	else
	{
		SetPlayerModel();
	}

	if ( bWasSpectator )
	{
		Spawn();
		return; // everything is useless afterwards
	}

	DetonateTripmines();
	ClearUseEntity();

	if ( iTeam == TEAM_SPECTATOR )
	{
		RemoveAllItems( true );

		if ( IsInAVehicle() )
		{
			LeaveVehicle();
		}

		State_Transition( STATE_OBSERVER_MODE );
	}

	if ( bKill == true )
	{
		CommitSuicide();
	}
}

bool CHL2MP_Player::HandleCommand_JoinTeam( int team )
{
	if ( !GetGlobalTeam( team ) || team == 0 )
	{
		Warning( "HandleCommand_JoinTeam( %d ) - invalid team index.\n", team );
		return false;
	}

	if ( team == TEAM_SPECTATOR )
	{
		// Prevent this is the cvar is set
		if ( !mp_allowspectators.GetInt() )
		{
			ClientPrint( this, HUD_PRINTCENTER, "#Cannot_Be_Spectator" );
			return false;
		}

		if ( GetTeamNumber() != TEAM_UNASSIGNED && !IsDead() )
		{
			m_fNextSuicideTime = gpGlobals->curtime;	// allow the suicide to work

			CommitSuicide();

			// add 1 to frags to balance out the 1 subtracted for killing yourself
			IncrementFragCount( 1 );
		}

		ChangeTeam( TEAM_SPECTATOR );

		return true;
	}
	else
	{
		StopObserverMode();
		State_Transition(STATE_ACTIVE);
	}

	// Switch their actual team...
	ChangeTeam( team );

	return true;
}

bool CHL2MP_Player::ClientCommand( const CCommand &args )
{
	if ( FStrEq( args[0], "spectate" ) )
	{
		if ( ShouldRunRateLimitedCommand( args ) )
		{
			// instantly join spectators
			HandleCommand_JoinTeam( TEAM_SPECTATOR );	
		}
		return true;
	}
	else if ( FStrEq( args[0], "jointeam" ) ) 
	{
		if ( args.ArgC() < 2 )
		{
			Warning( "Player sent bad jointeam syntax\n" );
		}

		if ( ShouldRunRateLimitedCommand( args ) )
		{
			int iTeam = atoi( args[1] );
			HandleCommand_JoinTeam( iTeam );
		}
		return true;
	}
	else if ( FStrEq( args[0], "joingame" ) )
	{
		return true;
	}

	return BaseClass::ClientCommand( args );
}

void CHL2MP_Player::CheatImpulseCommands( int iImpulse )
{
#if defined ( LUA_SDK )
	BEGIN_LUA_CALL_HOOK( "CheatImpulseCommands" );
		lua_pushhl2mpplayer( L, this );
		lua_pushinteger( L, iImpulse );
	END_LUA_CALL_HOOK( 2, 1 );

	RETURN_LUA_NONE();
#endif
	switch ( iImpulse )
	{
		case 101:
			{
				// HL2SB: legit cheats - no sv_cheats requirement
				// if( sv_cheats->GetBool() )
				// {
					GiveAllItems();
				// }
			}
			break;

		default:
			BaseClass::CheatImpulseCommands( iImpulse );
	}
}

//------------------------------------------------------------------------------
// Purpose: Legit impulse 101 (give all items) - not blocked by sv_cheats
//------------------------------------------------------------------------------
void CC_LegitImpulse101( void )
{
	CHL2MP_Player *pPlayer = ToHL2MPPlayer( UTIL_GetCommandClient() );
	if ( !pPlayer )
		return;

	pPlayer->GiveAllItems();
}

static ConCommand LegitImpulse( "LegitImpulse", CC_LegitImpulse101, "Legit impulse 101 - give all items (no sv_cheats needed)." );

bool CHL2MP_Player::ShouldRunRateLimitedCommand( const CCommand &args )
{
	int i = m_RateLimitLastCommandTimes.Find( args[0] );
	if ( i == m_RateLimitLastCommandTimes.InvalidIndex() )
	{
		m_RateLimitLastCommandTimes.Insert( args[0], gpGlobals->curtime );
		return true;
	}
	else if ( (gpGlobals->curtime - m_RateLimitLastCommandTimes[i]) < HL2MP_COMMAND_MAX_RATE )
	{
		// Too fast.
		return false;
	}
	else
	{
		m_RateLimitLastCommandTimes[i] = gpGlobals->curtime;
		return true;
	}
}

void CHL2MP_Player::CreateViewModel( int index /*=0*/ )
{
	Assert( index >= 0 && index < MAX_VIEWMODELS );

	if ( GetViewModel( index ) )
		return;

	CPredictedViewModel *vm = ( CPredictedViewModel * )CreateEntityByName( "predicted_viewmodel" );
	if ( vm )
	{
		vm->SetAbsOrigin( GetAbsOrigin() );
		vm->SetOwner( this );
		vm->SetIndex( index );
		DispatchSpawn( vm );
		vm->FollowEntity( this, false );
		m_hViewModel.Set( index, vm );
	}
}

bool CHL2MP_Player::BecomeRagdollOnClient( const Vector &force )
{
	return true;
}

// -------------------------------------------------------------------------------- //
// Ragdoll entities.
// -------------------------------------------------------------------------------- //

class CHL2MPRagdoll : public CBaseAnimatingOverlay
{
public:
	DECLARE_CLASS( CHL2MPRagdoll, CBaseAnimatingOverlay );
	DECLARE_SERVERCLASS();

	// Transmit ragdolls to everyone.
	virtual int UpdateTransmitState()
	{
		return SetTransmitState( FL_EDICT_ALWAYS );
	}

public:
	// In case the client has the player entity, we transmit the player index.
	// In case the client doesn't have it, we transmit the player's model index, origin, and angles
	// so they can create a ragdoll in the right place.
	CNetworkHandle( CBaseEntity, m_hPlayer );	// networked entity handle 
	CNetworkVector( m_vecRagdollVelocity );
	CNetworkVector( m_vecRagdollOrigin );
};

LINK_ENTITY_TO_CLASS( hl2mp_ragdoll, CHL2MPRagdoll );

IMPLEMENT_SERVERCLASS_ST_NOBASE( CHL2MPRagdoll, DT_HL2MPRagdoll )
	SendPropVector( SENDINFO(m_vecRagdollOrigin), -1,  SPROP_COORD ),
	SendPropEHandle( SENDINFO( m_hPlayer ) ),
	SendPropModelIndex( SENDINFO( m_nModelIndex ) ),
	SendPropInt		( SENDINFO(m_nForceBone), 8, 0 ),
	SendPropVector	( SENDINFO(m_vecForce), -1, SPROP_NOSCALE ),
	SendPropVector( SENDINFO( m_vecRagdollVelocity ) )
END_SEND_TABLE()


void CHL2MP_Player::CreateRagdollEntity( void )
{
	if ( m_hRagdoll )
	{
		UTIL_RemoveImmediate( m_hRagdoll );
		m_hRagdoll = NULL;
	}

	// If we already have a ragdoll, don't make another one.
	CHL2MPRagdoll *pRagdoll = dynamic_cast< CHL2MPRagdoll* >( m_hRagdoll.Get() );
	
	if ( !pRagdoll )
	{
		// create a new one
		pRagdoll = dynamic_cast< CHL2MPRagdoll* >( CreateEntityByName( "hl2mp_ragdoll" ) );
	}

	if ( pRagdoll )
	{
		pRagdoll->m_hPlayer = this;
		pRagdoll->m_vecRagdollOrigin = GetAbsOrigin();
		pRagdoll->m_vecRagdollVelocity = GetAbsVelocity();
		pRagdoll->m_nModelIndex = m_nModelIndex;
		pRagdoll->m_nForceBone = m_nForceBone;
		pRagdoll->m_vecForce = m_vecTotalBulletForce;
		pRagdoll->SetAbsOrigin( GetAbsOrigin() );
	}

	// ragdolls will be removed on round restart automatically
	m_hRagdoll = pRagdoll;
}

//-----------------------------------------------------------------------------
//-----------------------------------------------------------------------------
int CHL2MP_Player::FlashlightIsOn( void )
{
	return IsEffectActive( EF_DIMLIGHT );
}

extern ConVar flashlight;

//-----------------------------------------------------------------------------
//-----------------------------------------------------------------------------
void CHL2MP_Player::FlashlightTurnOn( void )
{
	if( flashlight.GetInt() > 0 && IsAlive() )
	{
		AddEffects( EF_DIMLIGHT );
		EmitSound( "HL2Player.FlashlightOn" );
	}
}


//-----------------------------------------------------------------------------
//-----------------------------------------------------------------------------
void CHL2MP_Player::FlashlightTurnOff( void )
{
	RemoveEffects( EF_DIMLIGHT );
	
	if( IsAlive() )
	{
		EmitSound( "HL2Player.FlashlightOff" );
	}
}

void CHL2MP_Player::Weapon_Drop( CBaseCombatWeapon *pWeapon, const Vector *pvecTarget, const Vector *pVelocity )
{
	//Drop a grenade if it's primed.
	if ( GetActiveWeapon() )
	{
		CBaseCombatWeapon *pGrenade = Weapon_OwnsThisType("weapon_frag");

		if ( GetActiveWeapon() == pGrenade )
		{
			if ( ( m_nButtons & IN_ATTACK ) || (m_nButtons & IN_ATTACK2) )
			{
				DropPrimedFragGrenade( this, pGrenade );
				return;
			}
		}
	}

	// HL2SB GMod compat (2026-09-23): SWEP:OnDrop( owner ) -- "called when
	// weapon is dropped by Player:DropWeapon"; handed the dropping owner,
	// fired BEFORE the engine drop so the script still sees itself carried.
#if defined( LUA_SDK )
	if ( pWeapon != NULL && pWeapon->IsScripted() && L != NULL )
	{
		BEGIN_LUA_CALL_WEAPON_HOOK( "OnDrop", pWeapon );
			lua_pushentity( L, this );
		END_LUA_CALL_WEAPON_HOOK( 1, 0 );
	}
#endif

	BaseClass::Weapon_Drop( pWeapon, pvecTarget, pVelocity );
}


void CHL2MP_Player::DetonateTripmines( void )
{
	CBaseEntity *pEntity = NULL;

	while ((pEntity = gEntList.FindEntityByClassname( pEntity, "npc_satchel" )) != NULL)
	{
		CSatchelCharge *pSatchel = dynamic_cast<CSatchelCharge *>(pEntity);
		if (pSatchel->m_bIsLive && pSatchel->GetThrower() == this )
		{
			g_EventQueue.AddEvent( pSatchel, "Explode", 0.20, this, this );
		}
	}

	// Play sound for pressing the detonator
	EmitSound( "Weapon_SLAM.SatchelDetonate" );
}

void CHL2MP_Player::Event_Killed( const CTakeDamageInfo &info )
{
	//update damage info with our accumulated physics force
	CTakeDamageInfo subinfo = info;
	subinfo.SetDamageForce( m_vecTotalBulletForce );

	SetNumAnimOverlays( 0 );

	// Note: since we're dead, it won't draw us on the client, but we don't set EF_NODRAW
	// because we still want to transmit to the clients in our PVS.
	CreateRagdollEntity();

	DetonateTripmines();

	BaseClass::Event_Killed( subinfo );

	if ( info.GetDamageType() & DMG_DISSOLVE )
	{
		if ( m_hRagdoll )
		{
			m_hRagdoll->GetBaseAnimating()->Dissolve( NULL, gpGlobals->curtime, false, ENTITY_DISSOLVE_NORMAL );
		}
	}

	CBaseEntity *pAttacker = info.GetAttacker();

	if ( pAttacker )
	{
		int iScoreToAdd = 1;

		if ( pAttacker == this )
		{
			iScoreToAdd = -1;
		}

		GetGlobalTeam( pAttacker->GetTeamNumber() )->AddScore( iScoreToAdd );
	}

	FlashlightTurnOff();

	m_lifeState = LIFE_DEAD;

	RemoveEffects( EF_NODRAW );	// still draw player body
	StopZooming();
}

int CHL2MP_Player::OnTakeDamage( const CTakeDamageInfo &inputInfo )
{
	//return here if the player is in the respawn grace period vs. slams.
	if ( gpGlobals->curtime < m_flSlamProtectTime &&  (inputInfo.GetDamageType() == DMG_BLAST ) )
		return 0;

	m_vecTotalBulletForce += inputInfo.GetDamageForce();
	
	gamestats->Event_PlayerDamage( this, inputInfo );

	return BaseClass::OnTakeDamage( inputInfo );
}

void CHL2MP_Player::DeathSound( const CTakeDamageInfo &info )
{
#if defined ( LUA_SDK )
	CTakeDamageInfo lInfo = info;

	BEGIN_LUA_CALL_HOOK( "PlayerDeathSound" );
		lua_pushhl2mpplayer( L, this );
		lua_pushdamageinfo( L, lInfo );
	END_LUA_CALL_HOOK( 2, 1 );

	RETURN_LUA_NONE();
#endif
	if ( m_hRagdoll && m_hRagdoll->GetBaseAnimating()->IsDissolving() )
		 return;

	char szStepSound[128];

	Q_snprintf( szStepSound, sizeof( szStepSound ), "%s.Die", GetPlayerModelSoundPrefix() );

	const char *pModelName = STRING( GetModelName() );

	CSoundParameters params;
	if ( GetParametersForSound( szStepSound, params, pModelName ) == false )
		return;

	Vector vecOrigin = GetAbsOrigin();
	
	CRecipientFilter filter;
	filter.AddRecipientsByPAS( vecOrigin );

	EmitSound_t ep;
	ep.m_nChannel = params.channel;
	ep.m_pSoundName = params.soundname;
	ep.m_flVolume = params.volume;
	ep.m_SoundLevel = params.soundlevel;
	ep.m_nFlags = 0;
	ep.m_nPitch = params.pitch;
	ep.m_pOrigin = &vecOrigin;

	EmitSound( filter, entindex(), ep );
}

CBaseEntity* CHL2MP_Player::EntSelectSpawnPoint( void )
{
	CBaseEntity *pSpot = NULL;
	CBaseEntity *pLastSpawnPoint = g_pLastSpawn;
	edict_t		*player = edict();
	const char *pSpawnpointName = "info_player_deathmatch";

	if ( HL2MPRules()->IsTeamplay() == true )
	{
		if ( GetTeamNumber() == TEAM_COMBINE )
		{
			pSpawnpointName = "info_player_combine";
			pLastSpawnPoint = g_pLastCombineSpawn;
		}
		else if ( GetTeamNumber() == TEAM_REBELS )
		{
			pSpawnpointName = "info_player_rebel";
			pLastSpawnPoint = g_pLastRebelSpawn;
		}

		if ( gEntList.FindEntityByClassname( NULL, pSpawnpointName ) == NULL )
		{
			// Andrew; this could be neater, or the entire function could be
			// rewritten to pool together our various point classes and select
			// one randomly. For now, we'll prefer spawnpoints by appid if we
			// can't find anything from deathmatch.
#ifdef HL2SB
			if ( GetTeamNumber() == TEAM_COMBINE || FStrEq( spawnpoint.GetString(), "terrorist" ) )
			{
				pSpawnpointName = "info_player_terrorist";
				pLastSpawnPoint = g_pLastCombineSpawn;
			}
			else if ( GetTeamNumber() == TEAM_REBELS || FStrEq( spawnpoint.GetString(), "ct") )
			{
				pSpawnpointName = "info_player_counterterrorist";
				pLastSpawnPoint = g_pLastRebelSpawn;
			}

			// try once more for dod
			if ( gEntList.FindEntityByClassname( NULL, pSpawnpointName ) == NULL )
			{
				if ( GetTeamNumber() == TEAM_COMBINE )
				{
					pSpawnpointName = "info_player_axis";
					pLastSpawnPoint = g_pLastCombineSpawn;
				}
				else if ( GetTeamNumber() == TEAM_REBELS )
				{
					pSpawnpointName = "info_player_allies";
					pLastSpawnPoint = g_pLastRebelSpawn;
				}

				// three strikes, you're out!
				if ( gEntList.FindEntityByClassname( NULL, pSpawnpointName ) == NULL )
				{
					pSpawnpointName = "info_player_deathmatch";
					pLastSpawnPoint = g_pLastSpawn;
				}
			}
#else
			pSpawnpointName = "info_player_deathmatch";
			pLastSpawnPoint = g_pLastSpawn;
#endif
		}
	}
#ifdef HL2SB
	else
	{
		if ( random->RandomInt(0,1) )
		{
			pSpawnpointName = "info_player_terrorist";
			pLastSpawnPoint = g_pLastCombineSpawn;
		}
		else
		{
			pSpawnpointName = "info_player_counterterrorist";
			pLastSpawnPoint = g_pLastRebelSpawn;
		}

		// try once more for dod
		if ( gEntList.FindEntityByClassname( NULL, pSpawnpointName ) == NULL )
		{
			if ( random->RandomInt(0,1) )
			{
				pSpawnpointName = "info_player_axis";
				pLastSpawnPoint = g_pLastCombineSpawn;
			}
			else
			{
				pSpawnpointName = "info_player_allies";
				pLastSpawnPoint = g_pLastRebelSpawn;
			}

			// three strikes, you're out!
			if ( gEntList.FindEntityByClassname( NULL, pSpawnpointName ) == NULL )
			{
				pSpawnpointName = "info_player_deathmatch";
				pLastSpawnPoint = g_pLastSpawn;
			}
		}
	}
#endif

	pSpot = pLastSpawnPoint;
	// Randomize the start spot
	for ( int i = random->RandomInt(1,5); i > 0; i-- )
		pSpot = gEntList.FindEntityByClassname( pSpot, pSpawnpointName );
	if ( !pSpot )  // skip over the null point
		pSpot = gEntList.FindEntityByClassname( pSpot, pSpawnpointName );

	CBaseEntity *pFirstSpot = pSpot;

	do 
	{
		if ( pSpot )
		{
			// check if pSpot is valid
			if ( g_pGameRules->IsSpawnPointValid( pSpot, this ) )
			{
				if ( pSpot->GetLocalOrigin() == vec3_origin )
				{
					pSpot = gEntList.FindEntityByClassname( pSpot, pSpawnpointName );
					continue;
				}

				// if so, go to pSpot
				goto ReturnSpot;
			}
		}
		// increment pSpot
		pSpot = gEntList.FindEntityByClassname( pSpot, pSpawnpointName );
	} while ( pSpot != pFirstSpot ); // loop if we're not back to the start

	// we haven't found a place to spawn yet,  so kill any guy at the first spawn point and spawn there
	if ( pSpot && ( TEAM_SPECTATOR != GetTeamNumber() ) )
	{
		CBaseEntity *ent = NULL;
		for ( CEntitySphereQuery sphere( pSpot->GetAbsOrigin(), 128 ); (ent = sphere.GetCurrentEntity()) != NULL; sphere.NextEntity() )
		{
			// if ent is a client, kill em (unless they are ourselves)
			if ( ent->IsPlayer() && !(ent->edict() == player) )
				ent->TakeDamage( CTakeDamageInfo( GetContainingEntity(INDEXENT(0)), GetContainingEntity(INDEXENT(0)), 300, DMG_GENERIC ) );
		}
		goto ReturnSpot;
	}

#ifdef HL2SB
	// If startspot is set, (re)spawn there.
	if ( !gpGlobals->startspot || !strlen(STRING(gpGlobals->startspot)))
	{
		pSpot = FindPlayerStart( "info_player_start" );
		if ( pSpot )
			goto ReturnSpot;
	}
	else
	{
		pSpot = gEntList.FindEntityByName( NULL, gpGlobals->startspot );
		if ( pSpot )
			goto ReturnSpot;
	}

#else
	if ( !pSpot  )
	{
		pSpot = gEntList.FindEntityByClassname( pSpot, "info_player_start" );

		if ( pSpot )
			goto ReturnSpot;
	}
#endif

ReturnSpot:

	if ( HL2MPRules()->IsTeamplay() == true )
	{
		if ( GetTeamNumber() == TEAM_COMBINE )
		{
			g_pLastCombineSpawn = pSpot;
		}
		else if ( GetTeamNumber() == TEAM_REBELS ) 
		{
			g_pLastRebelSpawn = pSpot;
		}
	}

	g_pLastSpawn = pSpot;

	m_flSlamProtectTime = gpGlobals->curtime + 0.5;

	return pSpot;
} 


CON_COMMAND( timeleft, "prints the time remaining in the match" )
{
	CHL2MP_Player *pPlayer = ToHL2MPPlayer( UTIL_GetCommandClient() );

	int iTimeRemaining = (int)HL2MPRules()->GetMapRemainingTime();
    
	if ( iTimeRemaining == 0 )
	{
		if ( pPlayer )
		{
			ClientPrint( pPlayer, HUD_PRINTTALK, "This game has no timelimit." );
		}
		else
		{
			Msg( "* No Time Limit *\n" );
		}
	}
	else
	{
		int iMinutes, iSeconds;
		iMinutes = iTimeRemaining / 60;
		iSeconds = iTimeRemaining % 60;

		char minutes[8];
		char seconds[8];

		Q_snprintf( minutes, sizeof(minutes), "%d", iMinutes );
		Q_snprintf( seconds, sizeof(seconds), "%2.2d", iSeconds );

		if ( pPlayer )
		{
			ClientPrint( pPlayer, HUD_PRINTTALK, "Time left in map: %s1:%s2", minutes, seconds );
		}
		else
		{
			Msg( "Time Remaining:  %s:%s\n", minutes, seconds );
		}
	}	
}


void CHL2MP_Player::Reset()
{	
	ResetDeathCount();
	ResetFragCount();
}

bool CHL2MP_Player::IsReady()
{
	return m_bReady;
}

void CHL2MP_Player::SetReady( bool bReady )
{
	m_bReady = bReady;
}

void CHL2MP_Player::CheckChatText( char *p, int bufsize )
{
	//Look for escape sequences and replace

	char *buf = new char[bufsize];
	int pos = 0;

	// Parse say text for escape sequences
	for ( char *pSrc = p; pSrc != NULL && *pSrc != 0 && pos < bufsize-1; pSrc++ )
	{
		// copy each char across
		buf[pos] = *pSrc;
		pos++;
	}

	buf[pos] = '\0';

	// copy buf back into p
	Q_strncpy( p, buf, bufsize );

	delete[] buf;	

	const char *pReadyCheck = p;

	HL2MPRules()->CheckChatForReadySignal( this, pReadyCheck );
}

void CHL2MP_Player::State_Transition( HL2MPPlayerState newState )
{
	State_Leave();
	State_Enter( newState );
}


void CHL2MP_Player::State_Enter( HL2MPPlayerState newState )
{
	m_iPlayerState = newState;
	m_pCurStateInfo = State_LookupInfo( newState );

	// Initialize the new state.
	if ( m_pCurStateInfo && m_pCurStateInfo->pfnEnterState )
		(this->*m_pCurStateInfo->pfnEnterState)();
}


void CHL2MP_Player::State_Leave()
{
	if ( m_pCurStateInfo && m_pCurStateInfo->pfnLeaveState )
	{
		(this->*m_pCurStateInfo->pfnLeaveState)();
	}
}


void CHL2MP_Player::State_PreThink()
{
	if ( m_pCurStateInfo && m_pCurStateInfo->pfnPreThink )
	{
		(this->*m_pCurStateInfo->pfnPreThink)();
	}
}


CHL2MPPlayerStateInfo *CHL2MP_Player::State_LookupInfo( HL2MPPlayerState state )
{
	// This table MUST match the 
	static CHL2MPPlayerStateInfo playerStateInfos[] =
	{
		{ STATE_ACTIVE,			"STATE_ACTIVE",			&CHL2MP_Player::State_Enter_ACTIVE, NULL, &CHL2MP_Player::State_PreThink_ACTIVE },
		{ STATE_OBSERVER_MODE,	"STATE_OBSERVER_MODE",	&CHL2MP_Player::State_Enter_OBSERVER_MODE,	NULL, &CHL2MP_Player::State_PreThink_OBSERVER_MODE }
	};

	for ( int i=0; i < ARRAYSIZE( playerStateInfos ); i++ )
	{
		if ( playerStateInfos[i].m_iPlayerState == state )
			return &playerStateInfos[i];
	}

	return NULL;
}

bool CHL2MP_Player::StartObserverMode(int mode)
{
	//we only want to go into observer mode if the player asked to, not on a death timeout
	if ( m_bEnterObserver == true )
	{
		VPhysicsDestroyObject();
		return BaseClass::StartObserverMode( mode );
	}
	return false;
}

void CHL2MP_Player::StopObserverMode()
{
	m_bEnterObserver = false;
	BaseClass::StopObserverMode();
}

void CHL2MP_Player::State_Enter_OBSERVER_MODE()
{
	int observerMode = m_iObserverLastMode;
	if ( IsNetClient() )
	{
		const char *pIdealMode = engine->GetClientConVarValue( engine->IndexOfEdict( edict() ), "cl_spec_mode" );
		if ( pIdealMode )
		{
			observerMode = atoi( pIdealMode );
			if ( observerMode <= OBS_MODE_FIXED || observerMode > OBS_MODE_ROAMING )
			{
				observerMode = m_iObserverLastMode;
			}
		}
	}
	m_bEnterObserver = true;
	StartObserverMode( observerMode );
}

void CHL2MP_Player::State_PreThink_OBSERVER_MODE()
{
	// Make sure nobody has changed any of our state.
	//	Assert( GetMoveType() == MOVETYPE_FLY );
	Assert( m_takedamage == DAMAGE_NO );
	Assert( IsSolidFlagSet( FSOLID_NOT_SOLID ) );
	//	Assert( IsEffectActive( EF_NODRAW ) );

	// Must be dead.
	Assert( m_lifeState == LIFE_DEAD );
	Assert( pl.deadflag );
}


void CHL2MP_Player::State_Enter_ACTIVE()
{
	SetMoveType( MOVETYPE_WALK );
	
	// md 8/15/07 - They'll get set back to solid when they actually respawn. If we set them solid now and mp_forcerespawn
	// is false, then they'll be spectating but blocking live players from moving.
	// RemoveSolidFlags( FSOLID_NOT_SOLID );
	
	m_Local.m_iHideHUD = 0;
}


void CHL2MP_Player::State_PreThink_ACTIVE()
{
	//we don't really need to do anything here. 
	//This state_prethink structure came over from CS:S and was doing an assert check that fails the way hl2dm handles death
}

//-----------------------------------------------------------------------------
// Purpose: 
//-----------------------------------------------------------------------------
bool CHL2MP_Player::CanHearAndReadChatFrom( CBasePlayer *pPlayer )
{
	// can always hear the console unless we're ignoring all chat
	if ( !pPlayer )
		return false;

	return true;
}