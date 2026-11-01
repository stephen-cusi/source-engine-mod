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
#include "hl2sb_pickupnotice.h"
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
#include "basetempentity.h"		// HL2SB: CTEPlayerAnimEvent (GMod gesture broadcast)
#include "recipientfilter.h"		// HL2SB: CPVSFilter for the same

// HL2SB: flashlight turned on by default at spawn
extern ConVar sv_flashlight_default;
// HL2SB: replicated animation debug switch (defined in hl2mp_player_shared.cpp),
// used by gesture/overlay probes in PostThink and HL2SB_AnimRestartGesture.
extern ConVar hl2sb_anim_debug;

// ============================================================================
// HL2SB (2026-10-02): GMod gesture broadcast channel - CTEPlayerAnimEvent.
//
// GMod's DT_HL2MP_Player excludes DT_BaseAnimatingOverlay::overlay_vars, so
// gesture layers are NEVER networked: every client builds them locally when
// this tiny temp entity arrives (player handle + event + data - no layer
// data at all).  This is what fixes "other players never play attack
// animations": the old architecture networked the layers and the client-side
// LATCH_ANIMATION_VAR interpolation clobbered them on remote entities
// (phone RECV probe: correct first frame, zeroed 10ms later).
//
// Structure mirrors the in-tree CS/DOD CTEPlayerAnimEvent exactly (same
// fields, same DT name); the single fire point is HL2SB_AnimRestartGesture
// below, which sends PLAYERANIMEVENT_CUSTOM_GESTURE + the resolved
// activity - the client lands it in GESTURE_SLOT_CUSTOM, matching GMod's
// CMultiPlayerAnimState::DoAnimationEvent CUSTOM_GESTURE case.
// ============================================================================
class CTEPlayerAnimEvent : public CBaseTempEntity
{
public:
	DECLARE_CLASS( CTEPlayerAnimEvent, CBaseTempEntity );
	DECLARE_SERVERCLASS();

	CTEPlayerAnimEvent( const char *name ) : CBaseTempEntity( name )
	{
	}

	CNetworkHandle( CBasePlayer, m_hPlayer );
	CNetworkVar( int, m_iEvent );
	CNetworkVar( int, m_nData );
};

IMPLEMENT_SERVERCLASS_ST_NOBASE( CTEPlayerAnimEvent, DT_TEPlayerAnimEvent )
	SendPropEHandle( SENDINFO( m_hPlayer ) ),
	SendPropInt( SENDINFO( m_iEvent ), Q_log2( PLAYERANIMEVENT_COUNT ) + 1, SPROP_UNSIGNED ),
	SendPropInt( SENDINFO( m_nData ), 32 )
END_SEND_TABLE()

static CTEPlayerAnimEvent g_TEPlayerAnimEvent( "PlayerAnimEvent" );

void TE_PlayerAnimEvent( CBasePlayer *pPlayer, PlayerAnimEvent_t event, int nData )
{
	if ( !pPlayer )
		return;

	CPVSFilter filter( (const Vector &)pPlayer->EyePosition() );

	g_TEPlayerAnimEvent.m_hPlayer = pPlayer;
	g_TEPlayerAnimEvent.m_iEvent = event;
	g_TEPlayerAnimEvent.m_nData = nData;
	g_TEPlayerAnimEvent.Create( filter, 0 );
}
// ============================================================================


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
	SendPropFloat( SENDINFO_NOCHECK( m_flHL2SBDuckSpeed ), 0, SPROP_NOSCALE ),
	SendPropFloat( SENDINFO_NOCHECK( m_flHL2SBUnDuckSpeed ), 0, SPROP_NOSCALE ),
	// HL2SB (2026-09-27): the taunt clock.  GMod's client reads the very same
	// float through IsPlayingTaunt() to run the taunt camera and gate the act
	// command, so it has to replicate.
	SendPropFloat( SENDINFO( m_flHL2SBTauntEnd ) ),
	SendPropInt( SENDINFO( m_iHL2SBPlayerClassID ), 16, SPROP_UNSIGNED ),
	// HL2SB (2026-09-27): the hands entity handle.  GMod's datadesc prop is
	// "m_Hands"; this fork's pair is self-consistently named m_hHands (nothing
	// external parses the datatable, so only the pair matching matters).
	SendPropEHandle( SENDINFO( m_hHands ) ),

	SendPropExclude( "DT_BaseAnimating", "m_flPoseParameter" ),
	SendPropExclude( "DT_BaseAnimating", "m_flPlaybackRate" ),
	SendPropExclude( "DT_BaseAnimating", "m_nSequence" ),
	SendPropExclude( "DT_ServerAnimationData", "m_flCycle" ),
	SendPropExclude( "DT_AnimTimeMustBeFirst", "m_flAnimTime" ),
	SendPropExclude( "DT_BaseFlex", "m_viewtarget" ),
	// HL2SB (2026-10-02): GMod excludes this too - gesture/overlay layers are
	// never networked for players.  Every client builds them locally from the
	// CTEPlayerAnimEvent broadcast (see the class at the top of this file);
	// networked layers were being zeroed by the client interpolation of
	// remote entities, which is why other players never played attack
	// animations.  Server-side layers are still created (slot bookkeeping,
	// IsPlayingTaunt) - they are simply invisible to clients now.
	SendPropExclude( "DT_BaseAnimatingOverlay", "overlay_vars" ),

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
		if ( !Q_strcmp( pszAct, s_HL2SBGModActList[ i ].pszName ) )
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
	m_flHL2SBJumpPower = 200.0f;
	m_iHL2SBPlayerClassID = 0;

	// HL2SB: stored-only GMod knobs (GMod defaults).
	m_flHL2SBCrouchedWalkSpeed = 0.34f;
	m_flHL2SBDuckSpeed = 0.3f;
	m_flHL2SBUnDuckSpeed = 0.3f;
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

#if defined( LUA_SDK )
static bool HL2SB_HasPlayerGamemodeMethod( const char *name )
{
	if ( L == NULL )
		return false;
	lua_getglobal( L, "_GAMEMODE" );
	bool found = false;
	if ( lua_istable( L, -1 ) )
	{
		luasrc_PushScriptField( L, -1, name );
		found = lua_isfunction( L, -1 ) != 0;
		lua_pop( L, 1 );
	}
	lua_pop( L, 1 );
	return found;
}
#endif

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

	// The gamemode's spawn chain already calls PlayerLoadout.
	if ( HL2SB_HasPlayerGamemodeMethod( "PlayerSpawn" ) )
		return;
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
	// A new life no longer owns the previous death ragdoll.
	if ( m_hRagdoll )
	{
		UTIL_Remove( m_hRagdoll );
		m_hRagdoll = NULL;
	}

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
    // cl_playermodel stores the registered name; model paths are resolved locally.
    const char *requested = engine->GetClientConVarValue( entindex(), "cl_playermodel" );
    const HL2SB_ModelConfig_t *config = requested && requested[0]
        ? HL2SB_GetModelConfigByName( requested ) : NULL;
    const char *model = config ? config->szPlayerModel : requested;
    if ( model == NULL || model[0] == '\0' || !ValidatePlayerModel( model ) )
        model = HL2SB_DEFAULT_PLAYERMODEL;

    if ( CBaseEntity::PrecacheModel( model ) <= 0 )
    {
        model = HL2SB_DEFAULT_PLAYERMODEL;
        CBaseEntity::PrecacheModel( model );
    }
    m_iModelType = Q_stristr( model, "models/player/human" ) ? TEAM_REBELS : TEAM_COMBINE;
    SetModel( model );
    SetupPlayerSoundsByModel( model );
#ifdef HL2SB
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

const QAngle &CHL2MP_Player::GetRenderAngles()
{
	return m_PlayerAnimState.GetRenderAngles();
}

void CHL2MP_Player::PostThink( void )
{
	BaseClass::PostThink();
	
	if ( GetFlags() & FL_DUCKING )
	{
		SetCollisionBounds( VEC_CROUCH_TRACE_MIN, VEC_CROUCH_TRACE_MAX );
	}

	m_angEyeAngles = EyeAngles();
	m_PlayerAnimState.Update();

	// HL2SB diagnostic: per-second dump of the SERVER-side gesture layers.
	// The client DUMP showed received weight ~0 / cycle 0 while creation logs
	// said wt=1.00 - this tells whether the server's own layer state decays to
	// 1 tick of weight right after creation (then the send table is faithfully
	// carrying garbage) or stays healthy (then the loss is client-side recv).
	if ( hl2sb_anim_debug.GetBool() )
	{
		static float s_flHL2SBSvOverlayDump[MAX_PLAYERS + 1] = {};
		static int s_nHL2SBSvLastSeq[MAX_PLAYERS + 1] = {};
		int slot = entindex();
		if ( slot >= 1 && slot <= MAX_PLAYERS )
		{
			int iLiveSeq = 0;
			for ( int k = 0; k < GetNumAnimOverlays(); k++ )
			{
				if ( (int)GetAnimOverlay( k )->m_nSequence > 0 )
				{
					iLiveSeq = (int)GetAnimOverlay( k )->m_nSequence;
					break;
				}
			}

			const char *pszReason = NULL;
			if ( iLiveSeq > 0 && s_nHL2SBSvLastSeq[slot] == 0 )
			{
				pszReason = "BIRTH";	// creation frame (pairs with gesture/sv OK)
			}
			else
			{
				float flInterval = ( iLiveSeq > 0 ) ? 0.25f : 1.0f;
				if ( gpGlobals->curtime >= s_flHL2SBSvOverlayDump[slot] )
				{
					s_flHL2SBSvOverlayDump[slot] = gpGlobals->curtime + flInterval;
					pszReason = ( iLiveSeq > 0 ) ? "ACTIVE" : "idle";
				}
			}
			s_nHL2SBSvLastSeq[slot] = iLiveSeq;

				if ( pszReason )
				{
					player_info_t info;
					const char *pszName = engine->GetPlayerInfo( slot, &info ) ? info.name : "?";
					Msg( "[HL2SB overlay/sv] DUMP(%s): ply=%d '%s' count=%d\n", pszReason, slot, pszName, GetNumAnimOverlays() );
					for ( int k = 0; k < GetNumAnimOverlays(); k++ )
					{
						CAnimationLayer *pLayer = GetAnimOverlay( k );
						Msg( "    [%d] seq=%d order=%d wt=%.4f cycle=%.4f prev=%.4f flags=%d\n",
							 k, (int)pLayer->m_nSequence, (int)pLayer->m_nOrder,
							 (float)pLayer->m_flWeight, (float)pLayer->m_flCycle,
							 (float)pLayer->m_flPrevCycle, (int)pLayer->m_fFlags );
					}
				}

				// Active-weapon follow state: the "weapon lying in the world
				// with its own animation" report is about the weapon ENTITY,
				// not the gesture layers - the parent link plus both coordinate
				// spaces (weapon world/local vs this player) split a lost
				// server-side parent from a stale origin, and seq/cycle shows
				// whether its own animation keeps advancing independently.
				static float s_flHL2SBSvWeaponDump[MAX_PLAYERS + 1] = {};
				if ( gpGlobals->curtime >= s_flHL2SBSvWeaponDump[slot] )
				{
					s_flHL2SBSvWeaponDump[slot] = gpGlobals->curtime + 1.0f;

					CBaseCombatWeapon *pWpn = GetActiveWeapon();
					player_info_t winfo;
					const char *pszPlyName = engine->GetPlayerInfo( slot, &winfo ) ? winfo.name : "?";
					// Viewmodel ownership: which server entities back this
					// player's viewmodel slots - the client-side ghost is a
					// viewmodel, so this pins whose it is.
					CBaseViewModel *pVm0 = GetViewModel( 0, false );
					CBaseViewModel *pVm1 = GetViewModel( 1, false );
					if ( pWpn )
					{
						CBaseEntity *pParent = pWpn->GetMoveParent();
						const Vector &vWpn = pWpn->GetAbsOrigin();
						const Vector &vLocal = pWpn->GetLocalOrigin();
						const Vector &vHere = GetAbsOrigin();
						Msg( "[HL2SB wpns/sv] ply=%d '%s' wp=%s parent=%s#%d abs=(%.0f %.0f %.0f) local=(%.0f %.0f %.0f) here=(%.0f %.0f %.0f) seq=%d cyc=%.2f model=%s fx=0x%x follow=%d patt=%d pmodel=%s vm0=%d vm1=%d\n",
							 slot, pszPlyName, pWpn->GetClassname(),
							 pParent ? pParent->GetClassname() : "NONE",
							 pParent ? pParent->entindex() : -1,
							 vWpn.x, vWpn.y, vWpn.z, vLocal.x, vLocal.y, vLocal.z,
							 vHere.x, vHere.y, vHere.z,
							 pWpn->GetSequence(), (float)pWpn->GetCycle(),
							 STRING( pWpn->GetModelName() ),
							 pWpn->GetEffects(), pWpn->IsFollowingEntity() ? 1 : 0,
							 pWpn->GetParentAttachment(),
							 STRING( GetModelName() ),
							 pVm0 ? pVm0->entindex() : -1,
							 pVm1 ? pVm1->entindex() : -1 );
					}
					else
					{
						Msg( "[HL2SB wpns/sv] ply=%d '%s' wp=NONE vm0=%d vm1=%d\n",
							 slot, pszPlyName,
							 pVm0 ? pVm0->entindex() : -1,
							 pVm1 ? pVm1->entindex() : -1 );
					}
				}
			}
	}

	// Store the eye angles pitch so the client can compute its animation state correctly.
	m_angEyeAngles = EyeAngles();

	QAngle angles = GetLocalAngles();
	angles[PITCH] = 0;
	SetLocalAngles( angles );
}

void CHL2MP_Player::PlayerDeathThink()
{
	// The base death-think performs housekeeping before the Lua respawn hook.
#if defined( LUA_SDK )
	if ( HL2SB_HasPlayerGamemodeMethod( "PlayerDeathThink" ) || !IsObserver() )
#else
	if ( !IsObserver() )
#endif
		BaseClass::PlayerDeathThink();
}

void CHL2MP_Player::FireBullets ( const FireBulletsInfo_t &info )
{
	// Move other players back to history positions based on local player's lag
	lagcompensation->StartLagCompensation( this, this->GetCurrentCommand() );

	FireBulletsInfo_t modinfo = info;

	CWeaponHL2MPBase *pWeapon = dynamic_cast<CWeaponHL2MPBase *>( GetActiveWeapon() );

	if ( pWeapon && !pWeapon->IsScripted() )
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

	// GMod gestures play through the weapon acttable (ACT_MP_ATTACK_* ->
	// ACT_HL2MP_GESTURE_RANGE_ATTACK_<holdtype>); fall back to the bare
	// activity for model-authored layer activities (ACT_GMOD_IN_CHAT,
	// ACT_GMOD_NOCLIP_LAYER).
	Activity translated = Weapon_TranslateActivity( activity );
	int iSequence = SelectWeightedSequence( translated );
	Activity broadcastAct = ( iSequence > 0 ) ? translated : activity;
	if ( iSequence <= 0 )
		iSequence = SelectWeightedSequence( activity );

	// HL2SB (2026-10-02): GMod gesture broadcast - overlay_vars is excluded
	// from the player send table, so THIS is the only way other clients ever
	// see the gesture.  One broadcast per server-side gesture creation (Lua
	// GM:DoAnimationEvent attacks/reloads, animations.lua land/chat, the act
	// command, the C++ SetAnimation fallbacks - every path funnels through
	// here).  The client receives CUSTOM_GESTURE + this activity and builds
	// the layer locally in GESTURE_SLOT_CUSTOM.  The server layer created
	// below still exists for IsPlayingTaunt/slot bookkeeping but is invisible
	// (excluded from the send table).
	//
	// Broadcast the RESOLVED activity, not the caller's ideal: the client has
	// no Weapon_TranslateActivity (server-only), so an ideal
	// ACT_MP_ATTACK_STAND_PRIMARYFIRE that the model only carries as
	// ACT_HL2MP_GESTURE_RANGE_ATTACK_<holdtype> dead-ends in the client's
	// SelectWeightedSequence and the remote player never animates (two-player
	// log: sv OK translated=... vs cl NO SEQUENCE ideal=...).  Activities the
	// acttable leaves alone translate to themselves, so act/land/wave traffic
	// is unchanged.  Above the replay/NO SEQUENCE returns so every funnel
	// call broadcasts exactly once, including rapid-fire replays.
	TE_PlayerAnimEvent( this, PLAYERANIMEVENT_CUSTOM_GESTURE, (int)broadcastAct );

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
    switch ( playerAnim )
    {
    case PLAYER_JUMP:
        HL2SB_DoAnimationEventLua( PLAYERANIMEVENT_JUMP, 0 );
        TE_PlayerAnimEvent( this, PLAYERANIMEVENT_JUMP, 0 );
        break;
    case PLAYER_ATTACK1:
        HL2SB_DoAnimationEventLua( PLAYERANIMEVENT_ATTACK_PRIMARY, 0 );
        break;
    case PLAYER_RELOAD:
        HL2SB_DoAnimationEventLua( PLAYERANIMEVENT_RELOAD, 0 );
        break;
    default:
        break;
    }
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
	if ( !HL2SB_IsPickupNoticeSuppressed( this ) )
	{
		IGameEvent *event = gameeventmanager->CreateEvent( "item_pickup" );
		if ( event )
		{
			event->SetInt( "userid", GetUserID() );
			event->SetString( "item", pWeapon->GetClassname() );
			event->SetInt( "amount", 0 );
			event->SetBool( "weapon", true );
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
	// HL2SB: the corpse has to carry the player's skin and bodygroup state
	// (GMod's CreateRagdollEntity copies both onto the ragdoll).  This table is
	// NOBASE, so without these props the networked m_nSkin/m_nBody never reach
	// the client-side ragdoll and it renders the model's default bodygroups.
	SendPropInt		( SENDINFO(m_nSkin), ANIMATION_SKIN_BITS ),
	SendPropInt		( SENDINFO(m_nBody), ANIMATION_BODY_BITS ),
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
		// HL2SB: GMod's CreateRagdollEntity copies the full visual state onto
		// the corpse (skin and every bodygroup slot).  Both are networked via
		// DT_HL2MPRagdoll above, so the client-side ragdoll keeps the bodygroups
		// the living model had instead of snapping back to the model defaults.
		pRagdoll->m_nSkin = m_nSkin;
		pRagdoll->m_nBody = m_nBody;
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
#if defined ( LUA_SDK )
	// HL2SB GMod compat (2026-10-08): GM:PlayerSwitchFlashlight( ply, true ) -
	// an explicit false refuses the toggle (GMod asks this from both switch
	// paths, including the Player:Flashlight() method).
	{
		bool bAllowed = true;

		BEGIN_LUA_CALL_HOOK( "PlayerSwitchFlashlight" );
			lua_pushplayer( L, this );
			lua_pushboolean( L, 1 );
			END_LUA_CALL_HOOK( 2, 1 );
		if ( L != NULL && lua_isboolean( L, -1 ) )
			bAllowed = ( lua_toboolean( L, -1 ) != 0 );
		if ( L != NULL )
			lua_pop( L, 1 );

		if ( !bAllowed )
			return;
	}
#endif

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
#if defined ( LUA_SDK )
	{
		bool bAllowed = true;

		BEGIN_LUA_CALL_HOOK( "PlayerSwitchFlashlight" );
			lua_pushplayer( L, this );
			lua_pushboolean( L, 0 );
			END_LUA_CALL_HOOK( 2, 1 );
		if ( L != NULL && lua_isboolean( L, -1 ) )
			bAllowed = ( lua_toboolean( L, -1 ) != 0 );
		if ( L != NULL )
			lua_pop( L, 1 );

		if ( !bAllowed )
			return;
	}
#endif

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

#if defined(LUA_SDK)
CBaseEntity *HL2SB_GetPlayerRagdollEntity(CBasePlayer *player)
{
    CHL2MP_Player *mp = dynamic_cast<CHL2MP_Player *>(player);
    return mp ? mp->m_hRagdoll.Get() : NULL;
}

void HL2SB_PrepareSilentPlayerDeath(CBasePlayer *player)
{
    CHL2MP_Player *mp = dynamic_cast<CHL2MP_Player *>(player);
    if (!mp) return;
    mp->DetonateTripmines();
}
#endif

void CHL2MP_Player::Event_Killed( const CTakeDamageInfo &info )
{
	//update damage info with our accumulated physics force
	CTakeDamageInfo subinfo = info;
	subinfo.SetDamageForce( m_vecTotalBulletForce );

	SetNumAnimOverlays( 0 );

	// Note: since we're dead, it won't draw us on the client, but we don't set EF_NODRAW
	// because we still want to transmit to the clients in our PVS.
#if defined( LUA_SDK )
	if ( !HL2SB_HasPlayerGamemodeMethod( "DoPlayerDeath" ) )
#endif
		CreateRagdollEntity();

	DetonateTripmines();

#if defined( LUA_SDK )
	m_lifeState = LIFE_DYING;
	// HL2SB GMod compat (2026-10-10): hook.Run( "DoPlayerDeath", victim, attacker, dmginfo )
	// - fired at the same point in GMod's death pipeline (before the death
	// hooks like PlayerDeath run their UI); the portalgun fizzles the dead
	// player's portals here.
	// - argument order is GMod's: base gamemode's GM:DoPlayerDeath scores off
	// the SECOND argument as the killer (inflictor never gets frags), and the
	// third argument is the damage info object.
	{
		BEGIN_LUA_CALL_HOOK( "DoPlayerDeath" );
			lua_pushplayer( L, this );
			CBaseEntity::PushLuaInstanceSafe( L, info.GetAttacker() );
			lua_pushdamageinfo( L, subinfo );
		END_LUA_CALL_HOOK( 3, 0 );
	}

	CBaseCombatWeapon *pDeathWeapon = GetActiveWeapon();
	if ( pDeathWeapon )
	{
		bool bDropWeapon = false;
		if ( L != NULL && m_nTableReference >= 0 && lua_isrefvalid( L, m_nTableReference ) )
		{
			lua_getref( L, m_nTableReference );
			if ( lua_istable( L, -1 ) )
			{
				lua_pushliteral( L, "__hl2sbDropWeaponOnDeath" );
				lua_rawget( L, -2 );
				bDropWeapon = lua_toboolean( L, -1 ) != 0;
				lua_pop( L, 1 );
			}
			lua_pop( L, 1 );
		}
		if ( bDropWeapon && pDeathWeapon->IsScripted() )
		{
			extern int HL2SB_WeaponShouldDropOnDieVote( CBaseCombatWeapon *pWeapon );
			bDropWeapon = HL2SB_WeaponShouldDropOnDieVote( pDeathWeapon ) != 0;
		}
		if ( !bDropWeapon )
		{
			pDeathWeapon->Holster();
			UTIL_Remove( pDeathWeapon );
			SetActiveWeapon( NULL );
		}
	}
#endif

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

		CTeam *pAttackerTeam = GetGlobalTeam( pAttacker->GetTeamNumber() );
		if ( pAttackerTeam )
			pAttackerTeam->AddScore( iScoreToAdd );
	}

	// Reference behavior: scoring lives in the gamemode's GM:DoPlayerDeath Lua
	// hook (dispatched above), never in the engine death pipeline - ported
	// gamemodes carry their own copy of that hook and must not double count.
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

#if defined ( LUA_SDK )
	// HL2SB GMod compat (2026-10-08): GM:PlayerSelectSpawn( ply, transition ).
	// GMod's C++ asks the gamemode before its own spawn-point logic and takes
	// a returned entity as the whole answer (nil keeps the engine path).  This
	// engine's C++ spawn selection stays authoritative for nil answers, so the
	// base gamemode's Lua implementation and this block can coexist.
	{
		CBaseEntity *pLuaSpot = NULL;

		BEGIN_LUA_CALL_HOOK( "PlayerSelectSpawn" );
			lua_pushplayer( L, this );
			lua_pushboolean( L, 0 );	// transition: this path has none
			END_LUA_CALL_HOOK( 2, 1 );
		if ( L != NULL && lua_isnil( L, -1 ) == 0 && lua_isuserdata( L, -1 ) )
			pLuaSpot = lua_toentity( L, -1 );
		if ( L != NULL )
			lua_pop( L, 1 );

		if ( pLuaSpot != NULL )
			return pLuaSpot;
	}
#endif

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
