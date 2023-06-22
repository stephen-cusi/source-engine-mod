//========= Copyright Valve Corporation, All rights reserved. ============//
//
// Purpose:		Player for HL2.
//
//=============================================================================//

#include "cbase.h"
#include "weapon_hl2mpbasehlmpcombatweapon.h"
#include "hl2mp_player.h"
#include "globalstate.h"
#include "game.h"
#include "gamerules.h"
#include "hl2mp_player_shared.h"
#include "predicted_viewmodel.h"
#include "in_buttons.h"
#include "hl2mp_gamerules.h"
#include "KeyValues.h"
#include "team.h"
#include "weapon_hl2mpbase.h"
#include "grenade_satchel.h"
#include "eventqueue.h"
#include "gamestats.h"
//#include "weapon_portalbase.h"

#include "engine/IEngineSound.h"
#include "SoundEmitterSystem/isoundemittersystembase.h"

#include "ilagcompensationmanager.h"
#include "prop_portal_shared.h"
#include "weapon_portalgun.h"
#include "soundent.h"
#include "cs_gamerules.h"
#include "cs_gamestats.h"
#include "player_resource.h"
#include "cs_player_resource.h"

#include "datacache/imdlcache.h"
#include "cs_blackmarket.h"
#include "ammodef.h"
#include "cs_ammodef.h"
#include "bot_util.h"
#include "hintmessage.h"
#include "cs_client.h"
#include "cs_achievement_constants.h"

void TE_RadioIcon(IRecipientFilter& filter, float delay, CBaseEntity* pPlayer);

int g_iLastCitizenModel = 0;
int g_iLastCombineModel = 0;

CBaseEntity	 *g_pLastCombineSpawn = NULL;
CBaseEntity	 *g_pLastRebelSpawn = NULL;
extern CBaseEntity				*g_pLastSpawn;
extern ConVar sv_autojump;
ConVar sv_give_upgraded_portalgun("sv_give_upgraded_portalgun", "1", 0,"Give the upgraded portalgun on spawn");

#define HL2MP_COMMAND_MAX_RATE 0.3

void DropPrimedFragGrenade( CHL2MP_Player *pPlayer, CBaseCombatWeapon *pGrenade );

LINK_ENTITY_TO_CLASS( player, CHL2MP_Player );

LINK_ENTITY_TO_CLASS( info_player_combine, CPointEntity );
LINK_ENTITY_TO_CLASS( info_player_rebel, CPointEntity );

IMPLEMENT_SERVERCLASS_ST(CHL2MP_Player, DT_HL2MP_Player)
	SendPropAngle( SENDINFO_VECTORELEM(m_angEyeAngles, 0), 11, SPROP_CHANGES_OFTEN ),
	SendPropAngle( SENDINFO_VECTORELEM(m_angEyeAngles, 1), 11, SPROP_CHANGES_OFTEN ),
	SendPropEHandle( SENDINFO( m_hRagdoll ) ),
	SendPropInt( SENDINFO( m_iSpawnInterpCounter), 4 ),
	SendPropInt( SENDINFO( m_iPlayerSoundType), 3 ),
	
	SendPropExclude( "DT_BaseAnimating", "m_flPoseParameter" ),
	SendPropExclude( "DT_BaseFlex", "m_viewtarget" ),

//	SendPropExclude( "DT_ServerAnimationData" , "m_flCycle" ),	
//	SendPropExclude( "DT_AnimTimeMustBeFirst" , "m_flAnimTime" ),
	
END_SEND_TABLE()

BEGIN_DATADESC( CHL2MP_Player )
END_DATADESC()

const char *g_ppszRandomCitizenModels[] = 
{
	"models/humans/group03/male_01.mdl",
	"models/humans/group03/male_02.mdl",
	"models/humans/group03/female_01.mdl",
	"models/humans/group03/male_03.mdl",
	"models/humans/group03/female_02.mdl",
	"models/humans/group03/male_04.mdl",
	"models/humans/group03/female_03.mdl",
	"models/humans/group03/male_05.mdl",
	"models/humans/group03/female_04.mdl",
	"models/humans/group03/male_06.mdl",
	"models/humans/group03/female_06.mdl",
	"models/humans/group03/male_07.mdl",
	"models/humans/group03/female_07.mdl",
	"models/humans/group03/male_08.mdl",
	"models/humans/group03/male_09.mdl",
};

const char *g_ppszRandomCombineModels[] =
{
	"models/combine_soldier.mdl",
	"models/combine_soldier_prisonguard.mdl",
	"models/combine_super_soldier.mdl",
	"models/police.mdl",
};


#define MAX_COMBINE_MODELS 4
#define MODEL_CHANGE_INTERVAL 5.0f
#define TEAM_CHANGE_INTERVAL 5.0f

#define HL2MPPLAYER_PHYSDAMAGE_SCALE 4.0f

#pragma warning( disable : 4355 )


// Minimum interval between rate-limited commands that players can run.
#define CS_COMMAND_MAX_RATE 0.3

const float CycleLatchInterval = 0.2f;

#define CS_PUSHAWAY_THINK_CONTEXT	"CSPushawayThink"

ConVar cs_ShowStateTransitions("cs_ShowStateTransitions", "-2", FCVAR_CHEAT, "cs_ShowStateTransitions <ent index or -1 for all>. Show player state transitions.");
ConVar sv_max_usercmd_future_ticks("sv_max_usercmd_future_ticks", "8", 0, "Prevents clients from running usercmds too far in the future. Prevents speed hacks.");
//ConVar sv_motd_unload_on_dismissal("sv_motd_unload_on_dismissal", "0", 0, "If enabled, the MOTD contents will be unloaded when the player closes the MOTD.");
extern ConVar sv_motd_unload_on_dismissal;
//=============================================================================
// HPE_BEGIN:
// [Forrest] Allow MVP to be turned off for a server
// [Forrest] Allow freezecam to be turned off for a server
// [Forrest] Allow win panel to be turned off for a server
//=============================================================================
static void SvNoMVPChangeCallback(IConVar* pConVar, const char* pOldValue, float flOldValue)
{
	ConVarRef var(pConVar);
	if (var.IsValid() && var.GetBool())
	{
		// Clear the MVPs of all players when MVP is turned off.
		for (int i = 1; i <= MAX_PLAYERS; i++)
		{
			CHL2MP_Player* pPlayer = ToCSPlayer(UTIL_PlayerByIndex(i));

			if (pPlayer)
			{
				pPlayer->SetNumMVPs(0);
			}
		}
	}
}
ConVar sv_nomvp("sv_nomvp", "0", 0, "Disable MVP awards.", SvNoMVPChangeCallback);
ConVar sv_disablefreezecam("sv_disablefreezecam", "0", FCVAR_REPLICATED, "Turn on/off freezecam on server");
ConVar sv_nowinpanel("sv_nowinpanel", "0", FCVAR_REPLICATED, "Turn on/off win panel on server");
//=============================================================================
// HPE_END
//=============================================================================


// ConVar bot_mimic( "bot_mimic", "0", FCVAR_CHEAT );
ConVar bot_freeze("bot_freeze", "0", FCVAR_CHEAT);
ConVar bot_crouch("bot_crouch", "0", FCVAR_CHEAT);
ConVar bot_mimic_yaw_offset("bot_mimic_yaw_offset", "180", FCVAR_CHEAT);

ConVar sv_legacy_grenade_damage("sv_legacy_grenade_damage", "0", FCVAR_REPLICATED, "Enable to replicate grenade damage behavior of the original Counter-Strike Source game.");

extern ConVar mp_autokick;
extern ConVar mp_holiday_nogifts;
extern ConVar sv_turbophysics;
//=============================================================================
// HPE_BEGIN:
// [menglish] Added in convars for freeze cam time length
//=============================================================================

extern ConVar spec_freeze_time;
extern ConVar spec_freeze_traveltime;

//=============================================================================
// HPE_END
//=============================================================================

extern ConVar ammo_hegrenade_max;
extern ConVar ammo_flashbang_max;
extern ConVar ammo_smokegrenade_max;


#define THROWGRENADE_COUNTER_BITS 3


EHANDLE g_pLastCTSpawn;
EHANDLE g_pLastTerroristSpawn;

// Max mass the player can lift with +use
#define PORTAL_PLAYER_MAX_LIFT_MASS 85
#define PORTAL_PLAYER_MAX_LIFT_SIZE 128

class NotVIP
{
public:
	bool operator()(CBasePlayer* player)
	{
		CHL2MP_Player* csPlayer = static_cast<CHL2MP_Player*>(player);
		csPlayer->MakeVIP(false);

		return true;
	}
};

// Expose the VIP selection to plugins, since we don't have an official VIP mode.  This
// allows plugins to access the (limited) VIP functionality already present (scoreboard
// identification and radar color).
CON_COMMAND(cs_make_vip, "Marks a player as the VIP")
{
	if (!UTIL_IsCommandIssuedByServerAdmin())
		return;

	if (args.ArgC() != 2)
	{
		return;
	}

	CHL2MP_Player* player = static_cast<CHL2MP_Player*>(UTIL_PlayerByIndex(atoi(args[1])));
	if (!player)
	{
		// Invalid value clears out VIP
		NotVIP notVIP;
		ForEachPlayer(notVIP);
		return;
	}

	player->MakeVIP(true);
}



class CTEPlayerAnimEvent : public CBaseTempEntity
{
public:
	DECLARE_CLASS(CTEPlayerAnimEvent, CBaseTempEntity);
	DECLARE_SERVERCLASS();

	CTEPlayerAnimEvent(const char* name) : CBaseTempEntity(name)
	{
	}

	CNetworkHandle(CBasePlayer, m_hPlayer);
	CNetworkVar(int, m_iEvent);
	CNetworkVar(int, m_nData);
};

IMPLEMENT_SERVERCLASS_ST_NOBASE(CTEPlayerAnimEvent, DT_TEPlayerAnimEvent)
SendPropEHandle(SENDINFO(m_hPlayer)),
SendPropInt(SENDINFO(m_iEvent), Q_log2(PLAYERANIMEVENT_COUNT) + 1, SPROP_UNSIGNED),
SendPropInt(SENDINFO(m_nData), 32)
END_SEND_TABLE()

static CTEPlayerAnimEvent g_TEPlayerAnimEvent("PlayerAnimEvent");

void TE_PlayerAnimEvent(CBasePlayer* pPlayer, PlayerAnimEvent_t event, int nData)
{
	CPVSFilter filter((const Vector&)pPlayer->EyePosition());

	g_TEPlayerAnimEvent.m_hPlayer = pPlayer;
	g_TEPlayerAnimEvent.m_iEvent = event;
	g_TEPlayerAnimEvent.m_nData = nData;
	g_TEPlayerAnimEvent.Create(filter, 0);
}


CHL2MP_Player::CHL2MP_Player() : m_PlayerAnimState( this )
{
	m_angEyeAngles.Init();

	m_iLastWeaponFireUsercmd = 0;

	m_flNextModelChangeTime = 0.0f;
	m_flNextTeamChangeTime = 0.0f;

	m_iSpawnInterpCounter = 0;

    m_bEnterObserver = false;
	m_bReady = false;

	BaseClass::ChangeTeam( 0 );
	
//	UseClientSideAnimation();
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

	//Precache Citizen models
	int nHeads = ARRAYSIZE( g_ppszRandomCitizenModels );
	int i;	

	for ( i = 0; i < nHeads; ++i )
	   	 PrecacheModel( g_ppszRandomCitizenModels[i] );

	//Precache Combine Models
	nHeads = ARRAYSIZE( g_ppszRandomCombineModels );

	for ( i = 0; i < nHeads; ++i )
	   	 PrecacheModel( g_ppszRandomCombineModels[i] );

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
	CBasePlayer::GiveAmmo( 1,	"smg1_grenade");
	CBasePlayer::GiveAmmo( 255,	"Buckshot");
	CBasePlayer::GiveAmmo( 32,	"357" );
	CBasePlayer::GiveAmmo( 3,	"rpg_round");

	CBasePlayer::GiveAmmo( 1,	"grenade" );
	CBasePlayer::GiveAmmo( 2,	"slam" );

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
	
}

void CHL2MP_Player::GiveDefaultItems( void )
{
	EquipSuit();

	CBasePlayer::GiveAmmo( 255,	"Pistol");
	CBasePlayer::GiveAmmo( 45,	"SMG1");
	CBasePlayer::GiveAmmo( 1,	"grenade" );
	CBasePlayer::GiveAmmo( 6,	"Buckshot");
	CBasePlayer::GiveAmmo( 6,	"357" );

	if ( GetPlayerModelType() == PLAYER_SOUNDS_METROPOLICE || GetPlayerModelType() == PLAYER_SOUNDS_COMBINESOLDIER )
	{
		GiveNamedItem( "weapon_stunstick" );
	}
	else if ( GetPlayerModelType() == PLAYER_SOUNDS_CITIZEN )
	{
		GiveNamedItem( "weapon_crowbar" );
	}
	
	GiveNamedItem( "weapon_pistol" );
	GiveNamedItem( "weapon_smg1" );
	GiveNamedItem( "weapon_frag" );
	GiveNamedItem( "weapon_physcannon" );
	GiveNamedItem( "weapon_physgun" );
	GiveNamedItem( "weapon_portalgun" );

	CWeaponPortalgun* pPortalGun = static_cast<CWeaponPortalgun*>(Weapon_OwnsThisType("weapon_portalgun"));
	if (pPortalGun && sv_give_upgraded_portalgun.GetBool())
	{
		pPortalGun->SetCanFirePortal1();
		pPortalGun->SetCanFirePortal2();
	}

	const char *szDefaultWeaponName = engine->GetClientConVarValue( engine->IndexOfEdict( edict() ), "cl_defaultweapon" );

	CBaseCombatWeapon *pDefaultWeapon = Weapon_OwnsThisType( szDefaultWeaponName );

	if ( pDefaultWeapon )
	{
		Weapon_Switch( pDefaultWeapon );
	}
	else
	{
		Weapon_Switch( Weapon_OwnsThisType( "weapon_physcannon" ) );
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

					Q_snprintf( szReturnString, sizeof (szReturnString ), "cl_playermodel models/combine_soldier.mdl\n" );
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

	SetNumAnimOverlays( 3 );
	ResetAnimation();

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
}

//-----------------------------------------------------------------------------
// Purpose: Overload for portal-- Our player can lift his own mass.
// Input  : *pObject - The object to lift
//			bLimitMassAndSize - check for mass/size limits
//-----------------------------------------------------------------------------
void CHL2MP_Player::PickupObject(CBaseEntity* pObject, bool bLimitMassAndSize)
{
	// can't pick up what you're standing on
	if (GetGroundEntity() == pObject)
		return;

	if (bLimitMassAndSize == true)
	{
		if (CBasePlayer::CanPickupObject(pObject, PORTAL_PLAYER_MAX_LIFT_MASS, PORTAL_PLAYER_MAX_LIFT_SIZE) == false)
			return;
	}

	// Can't be picked up if NPCs are on me
	if (pObject->HasNPCsOnIt())
		return;

	PlayerPickupObject(this, pObject);
}

bool CHL2MP_Player::ValidatePlayerModel( const char *pModel )
{
	int iModels = ARRAYSIZE( g_ppszRandomCitizenModels );
	int i;	

	for ( i = 0; i < iModels; ++i )
	{
		if ( !Q_stricmp( g_ppszRandomCitizenModels[i], pModel ) )
		{
			return true;
		}
	}

	iModels = ARRAYSIZE( g_ppszRandomCombineModels );

	for ( i = 0; i < iModels; ++i )
	{
	   	if ( !Q_stricmp( g_ppszRandomCombineModels[i], pModel ) )
		{
			return true;
		}
	}

	return false;
}

void CHL2MP_Player::SetPlayerTeamModel( void )
{
	const char *szModelName = NULL;
	szModelName = engine->GetClientConVarValue( engine->IndexOfEdict( edict() ), "cl_playermodel" );

	int modelIndex = modelinfo->GetModelIndex( szModelName );

	if ( modelIndex == -1 || ValidatePlayerModel( szModelName ) == false )
	{
		szModelName = "models/Combine_Soldier.mdl";
		m_iModelType = TEAM_COMBINE;

		char szReturnString[512];

		Q_snprintf( szReturnString, sizeof (szReturnString ), "cl_playermodel %s\n", szModelName );
		engine->ClientCommand ( edict(), szReturnString );
	}

	if ( GetTeamNumber() == TEAM_COMBINE )
	{
		if ( Q_stristr( szModelName, "models/human") )
		{
			int nHeads = ARRAYSIZE( g_ppszRandomCombineModels );
		
			g_iLastCombineModel = ( g_iLastCombineModel + 1 ) % nHeads;
			szModelName = g_ppszRandomCombineModels[g_iLastCombineModel];
		}

		m_iModelType = TEAM_COMBINE;
	}
	else if ( GetTeamNumber() == TEAM_REBELS )
	{
		if ( !Q_stristr( szModelName, "models/human") )
		{
			int nHeads = ARRAYSIZE( g_ppszRandomCitizenModels );

			g_iLastCitizenModel = ( g_iLastCitizenModel + 1 ) % nHeads;
			szModelName = g_ppszRandomCitizenModels[g_iLastCitizenModel];
		}

		m_iModelType = TEAM_REBELS;
	}
	
	SetModel( szModelName );
	SetupPlayerSoundsByModel( szModelName );

	m_flNextModelChangeTime = gpGlobals->curtime + MODEL_CHANGE_INTERVAL;
}

void CHL2MP_Player::SetPlayerModel( void )
{
	const char *szModelName = NULL;
	const char *pszCurrentModelName = modelinfo->GetModelName( GetModel());

	szModelName = engine->GetClientConVarValue( engine->IndexOfEdict( edict() ), "cl_playermodel" );

	if ( ValidatePlayerModel( szModelName ) == false )
	{
		char szReturnString[512];

		if ( ValidatePlayerModel( pszCurrentModelName ) == false )
		{
			pszCurrentModelName = "models/Combine_Soldier.mdl";
		}

		Q_snprintf( szReturnString, sizeof (szReturnString ), "cl_playermodel %s\n", pszCurrentModelName );
		engine->ClientCommand ( edict(), szReturnString );

		szModelName = pszCurrentModelName;
	}

	if ( GetTeamNumber() == TEAM_COMBINE )
	{
		int nHeads = ARRAYSIZE( g_ppszRandomCombineModels );
		
		g_iLastCombineModel = ( g_iLastCombineModel + 1 ) % nHeads;
		szModelName = g_ppszRandomCombineModels[g_iLastCombineModel];

		m_iModelType = TEAM_COMBINE;
	}
	else if ( GetTeamNumber() == TEAM_REBELS )
	{
		int nHeads = ARRAYSIZE( g_ppszRandomCitizenModels );

		g_iLastCitizenModel = ( g_iLastCitizenModel + 1 ) % nHeads;
		szModelName = g_ppszRandomCitizenModels[g_iLastCitizenModel];

		m_iModelType = TEAM_REBELS;
	}
	else
	{
		if ( Q_strlen( szModelName ) == 0 ) 
		{
			szModelName = g_ppszRandomCitizenModels[0];
		}

		if ( Q_stristr( szModelName, "models/human") )
		{
			m_iModelType = TEAM_REBELS;
		}
		else
		{
			m_iModelType = TEAM_COMBINE;
		}
	}

	int modelIndex = modelinfo->GetModelIndex( szModelName );

	if ( modelIndex == -1 )
	{
		szModelName = "models/Combine_Soldier.mdl";
		m_iModelType = TEAM_COMBINE;

		char szReturnString[512];

		Q_snprintf( szReturnString, sizeof (szReturnString ), "cl_playermodel %s\n", szModelName );
		engine->ClientCommand ( edict(), szReturnString );
	}

	SetModel( szModelName );
	SetupPlayerSoundsByModel( szModelName );

	m_flNextModelChangeTime = gpGlobals->curtime + MODEL_CHANGE_INTERVAL;
}

void CHL2MP_Player::SetupPlayerSoundsByModel( const char *pModelName )
{
	if ( Q_stristr( pModelName, "models/human") )
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
		else if ((GetAbsVelocity().x || GetAbsVelocity().y) && ( GetFlags() & FL_ONGROUND ))
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
	QAngle vOldAngles = GetLocalAngles();
	QAngle vTempAngles = GetLocalAngles();

	vTempAngles = EyeAngles();

	if ( vTempAngles[PITCH] > 180.0f )
	{
		vTempAngles[PITCH] -= 360.0f;
	}

	SetLocalAngles( vTempAngles );

	BaseClass::PreThink();
	State_PreThink();

	//Reset bullet force accumulator, only lasts one frame
	m_vecTotalBulletForce = vec3_origin;
	SetLocalAngles( vOldAngles );
}

void CHL2MP_Player::PostThink( void )
{
	BaseClass::PostThink();
	
	if ( GetFlags() & FL_DUCKING )
	{
		SetCollisionBounds( VEC_CROUCH_TRACE_MIN, VEC_CROUCH_TRACE_MAX );
	}

	m_PlayerAnimState.Update();

	// Store the eye angles pitch so the client can compute its animation state correctly.
	m_angEyeAngles = EyeAngles();

	QAngle angles = GetLocalAngles();
	angles[PITCH] = 0;
	SetLocalAngles( angles );
}

void CHL2MP_Player::PlayerDeathThink()
{
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
	if ( !( pCmd->buttons & IN_ATTACK ) && (pCmd->command_number - m_iLastWeaponFireUsercmd > 5) )
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

// Set the activity based on an event or current state
void CHL2MP_Player::SetAnimation( PLAYER_ANIM playerAnim )
{
	int animDesired;

	float speed;

	speed = GetAbsVelocity().Length2D();

	
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

	Activity idealActivity = ACT_HL2MP_RUN;

	// This could stand to be redone. Why is playerAnim abstracted from activity? (sjb)
	if ( playerAnim == PLAYER_JUMP )
	{
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
		idealActivity = ACT_HL2MP_GESTURE_RELOAD;
	}
	else if ( playerAnim == PLAYER_IDLE || playerAnim == PLAYER_WALK )
	{
		if ( !( GetFlags() & FL_ONGROUND ) && GetActivity( ) == ACT_HL2MP_JUMP )	// Still jumping
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
					/*
					if ( bRunning == false )
					{
						idealActivity = ACT_WALK;
					}
					else
					*/
					{
						idealActivity = ACT_HL2MP_RUN;
					}
				}
				else
				{
					idealActivity = ACT_HL2MP_IDLE;
				}
			}
		}

		idealActivity = TranslateTeamActivity( idealActivity );
	}
	
	if ( idealActivity == ACT_HL2MP_GESTURE_RANGE_ATTACK )
	{
		RestartGesture( Weapon_TranslateActivity( idealActivity ) );

		// FIXME: this seems a bit wacked
		Weapon_SetActivity( Weapon_TranslateActivity( ACT_RANGE_ATTACK1 ), 0 );

		return;
	}
	else if ( idealActivity == ACT_HL2MP_GESTURE_RELOAD )
	{
		RestartGesture( Weapon_TranslateActivity( idealActivity ) );
		return;
	}
	else
	{
		SetActivity( idealActivity );

		animDesired = SelectWeightedSequence( Weapon_TranslateActivity ( idealActivity ) );

		if (animDesired == -1)
		{
			animDesired = SelectWeightedSequence( idealActivity );

			if ( animDesired == -1 )
			{
				animDesired = 0;
			}
		}
	
		// Already using the desired animation?
		if ( GetSequence() == animDesired )
			return;

		m_flPlaybackRate = 1.0;
		ResetSequence( animDesired );
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

	return true;
}

void CHL2MP_Player::ChangeTeam( int iTeam )
{
/*	if ( GetNextTeamChangeTime() >= gpGlobals->curtime )
	{
		char szReturnString[128];
		Q_snprintf( szReturnString, sizeof( szReturnString ), "Please wait %d more seconds before trying to switch teams again.\n", (int)(GetNextTeamChangeTime() - gpGlobals->curtime) );

		ClientPrint( this, HUD_PRINTTALK, szReturnString );
		return;
	}*/

	bool bKill = false;

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

	if ( iTeam == TEAM_SPECTATOR )
	{
		RemoveAllItems( true );

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
	switch ( iImpulse )
	{
		case 101:
			{
				if( sv_cheats->GetBool() )
				{
					GiveAllItems();
				}
			}
			break;

		default:
			BaseClass::CheatImpulseCommands( iImpulse );
	}
}

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
			pSpawnpointName = "info_player_deathmatch";
			pLastSpawnPoint = g_pLastSpawn;
		}
	}

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
	if ( pSpot )
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

	if ( !pSpot  )
	{
		pSpot = gEntList.FindEntityByClassname( pSpot, "info_player_start" );

		if ( pSpot )
			goto ReturnSpot;

		pSpot = gEntList.FindEntityByClassname(pSpot, "info_player_terrorist");
	}

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

void UTIL_CSRadioMessage(IRecipientFilter& filter, int iClient, int msg_dest, const char* msg_name, const char* param1 = NULL, const char* param2 = NULL, const char* param3 = NULL, const char* param4 = NULL)
{
	UserMessageBegin(filter, "RadioText");
	WRITE_BYTE(msg_dest);
	WRITE_BYTE(iClient);
	WRITE_STRING(msg_name);

	if (param1)
		WRITE_STRING(param1);
	else
		WRITE_STRING("");

	if (param2)
		WRITE_STRING(param2);
	else
		WRITE_STRING("");

	if (param3)
		WRITE_STRING(param3);
	else
		WRITE_STRING("");

	if (param4)
		WRITE_STRING(param4);
	else
		WRITE_STRING("");

	MessageEnd();
}
void CHL2MP_Player::ConstructRadioFilter(CRecipientFilter& filter)
{
	filter.MakeReliable();

	int localTeam = GetTeamNumber();

	int i;
	for (i = 1; i <= gpGlobals->maxClients; ++i)
	{
		CHL2MP_Player* player = static_cast<CHL2MP_Player*>(UTIL_PlayerByIndex(i));
		if (!player)
			continue;

		// Skip players ignoring the radio
		if (player->m_bIgnoreRadio)
			continue;

		if (player->GetTeamNumber() == TEAM_SPECTATOR)
		{
			// add spectators
			if (player->m_iObserverMode == OBS_MODE_IN_EYE || player->m_iObserverMode == OBS_MODE_CHASE)
			{
				filter.AddRecipient(player);
			}
		}
		else if (player->GetTeamNumber() == localTeam)
		{
			// add teammates
			filter.AddRecipient(player);
		}
	}
}


void CHL2MP_Player::State_Transition( CSPlayerState newState )
{
	State_Leave();
	State_Enter( newState );
}


void CHL2MP_Player::State_Enter( CSPlayerState newState )
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


CHL2MPPlayerStateInfo *CHL2MP_Player::State_LookupInfo( CSPlayerState state )
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


CWeaponHL2MPBase* CHL2MP_Player::GetActivePortalWeapon() const
{
	CBaseCombatWeapon* pWeapon = GetActiveWeapon();
	if (pWeapon)
	{
		return dynamic_cast<CWeaponHL2MPBase*>(pWeapon);
	}
	else
	{
		return NULL;
	}
}

void CHL2MP_Player::IncrementPortalsPlaced(void)
{
	m_StatsThisLevel.iNumPortalsPlaced++;

	if (m_iBonusChallenge == PORTAL_CHALLENGE_PORTALS)
		SetBonusProgress(static_cast<int>(m_StatsThisLevel.iNumPortalsPlaced));
}

void CHL2MP_Player::IncrementStepsTaken(void)
{
	m_StatsThisLevel.iNumStepsTaken++;

	if (m_iBonusChallenge == PORTAL_CHALLENGE_STEPS)
		SetBonusProgress(static_cast<int>(m_StatsThisLevel.iNumStepsTaken));
}

void CHL2MP_Player::UpdateSecondsTaken(void)
{
	float fSecondsSinceLastUpdate = (gpGlobals->curtime - m_fTimeLastNumSecondsUpdate);
	m_StatsThisLevel.fNumSecondsTaken += fSecondsSinceLastUpdate;
	m_fTimeLastNumSecondsUpdate = gpGlobals->curtime;

	if (m_iBonusChallenge == PORTAL_CHALLENGE_TIME)
		SetBonusProgress(static_cast<int>(m_StatsThisLevel.fNumSecondsTaken));

	if (m_fNeuroToxinDamageTime > 0.0f)
	{
		float fTimeRemaining = m_fNeuroToxinDamageTime - gpGlobals->curtime;

		if (fTimeRemaining < 0.0f)
		{
			CTakeDamageInfo info;
			info.SetDamage(gpGlobals->frametime * 50.0f);
			info.SetDamageType(DMG_NERVEGAS);
			TakeDamage(info);
			fTimeRemaining = 0.0f;
		}

		PauseBonusProgress(false);
		SetBonusProgress(static_cast<int>(fTimeRemaining));
	}
}

void CHL2MP_Player::ResetThisLevelStats(void)
{
	m_StatsThisLevel.iNumPortalsPlaced = 0;
	m_StatsThisLevel.iNumStepsTaken = 0;
	m_StatsThisLevel.fNumSecondsTaken = 0.0f;

	if (m_iBonusChallenge != PORTAL_CHALLENGE_NONE)
		SetBonusProgress(0);
}

void CHL2MP_Player::UpdatePortalViewAreaBits(unsigned char* pvs, int pvssize)
{
	Assert(pvs);

	int iPortalCount = CProp_Portal_Shared::AllPortals.Count();
	if (iPortalCount == 0)
		return;

	CProp_Portal** pPortals = CProp_Portal_Shared::AllPortals.Base();
	int* portalArea = (int*)stackalloc(sizeof(int) * iPortalCount);
	bool* bUsePortalForVis = (bool*)stackalloc(sizeof(bool) * iPortalCount);

	unsigned char* portalTempBits = (unsigned char*)stackalloc(sizeof(unsigned char) * 32 * iPortalCount);
	COMPILE_TIME_ASSERT((sizeof(unsigned char) * 32) >= sizeof(((CPlayerLocalData*)0)->m_chAreaBits));

	// setup area bits for these portals
	for (int i = 0; i < iPortalCount; ++i)
	{
		CProp_Portal* pLocalPortal = pPortals[i];
		// Make sure this portal is active before adding it's location to the pvs
		if (pLocalPortal && pLocalPortal->m_bActivated)
		{
			CProp_Portal* pRemotePortal = pLocalPortal->m_hLinkedPortal.Get();

			// Make sure this portal's linked portal is in the PVS before we add what it can see
			if (pRemotePortal && pRemotePortal->m_bActivated && pRemotePortal->NetworkProp() &&
				pRemotePortal->NetworkProp()->IsInPVS(edict(), pvs, pvssize))
			{
				portalArea[i] = engine->GetArea(pPortals[i]->GetAbsOrigin());

				if (portalArea[i] >= 0)
				{
					bUsePortalForVis[i] = true;
				}

				engine->GetAreaBits(portalArea[i], &portalTempBits[i * 32], sizeof(unsigned char) * 32);
			}
		}
	}

	// Use the union of player-view area bits and the portal-view area bits of each portal
	for (int i = 0; i < m_Local.m_chAreaBits.Count(); i++)
	{
		for (int j = 0; j < iPortalCount; ++j)
		{
			// If this portal is active, in PVS and it's location is valid
			if (bUsePortalForVis[j])
			{
				m_Local.m_chAreaBits.Set(i, m_Local.m_chAreaBits[i] | portalTempBits[(j * 32) + i]);
			}
		}
	}
}


//////////////////////////////////////////////////////////////////////////
// AddPortalCornersToEnginePVS
// Subroutine to wrap the adding of portal corners to the PVS which is called once for the setup of each portal.
// input - pPortal: the portal we are viewing 'out of' which needs it's corners added to the PVS
//////////////////////////////////////////////////////////////////////////
void AddPortalCornersToEnginePVS(CProp_Portal* pPortal)
{
	Assert(pPortal);

	if (!pPortal)
		return;

	Vector vForward, vRight, vUp;
	pPortal->GetVectors(&vForward, &vRight, &vUp);

	// Center of the remote portal
	Vector ptOrigin = pPortal->GetAbsOrigin();

	// Distance offsets to the different edges of the portal... Used in the placement checks
	Vector vToTopEdge = vUp * (PORTAL_HALF_HEIGHT - PORTAL_BUMP_FORGIVENESS);
	Vector vToBottomEdge = -vToTopEdge;
	Vector vToRightEdge = vRight * (PORTAL_HALF_WIDTH - PORTAL_BUMP_FORGIVENESS);
	Vector vToLeftEdge = -vToRightEdge;

	// Distance to place PVS points away from portal, to avoid being in solid
	Vector vForwardBump = vForward * 1.0f;

	// Add center and edges to the engine PVS
	engine->AddOriginToPVS(ptOrigin + vForwardBump);
	engine->AddOriginToPVS(ptOrigin + vToTopEdge + vToLeftEdge + vForwardBump);
	engine->AddOriginToPVS(ptOrigin + vToTopEdge + vToRightEdge + vForwardBump);
	engine->AddOriginToPVS(ptOrigin + vToBottomEdge + vToLeftEdge + vForwardBump);
	engine->AddOriginToPVS(ptOrigin + vToBottomEdge + vToRightEdge + vForwardBump);
}

void PortalSetupVisibility(CBaseEntity* pPlayer, int area, unsigned char* pvs, int pvssize)
{
	int iPortalCount = CProp_Portal_Shared::AllPortals.Count();
	if (iPortalCount == 0)
		return;

	CProp_Portal** pPortals = CProp_Portal_Shared::AllPortals.Base();
	for (int i = 0; i != iPortalCount; ++i)
	{
		CProp_Portal* pPortal = pPortals[i];

		if (pPortal && pPortal->m_bActivated)
		{
			if (pPortal->NetworkProp()->IsInPVS(pPlayer->edict(), pvs, pvssize))
			{
				if (engine->CheckAreasConnected(area, pPortal->NetworkProp()->AreaNum()))
				{
					CProp_Portal* pLinkedPortal = static_cast<CProp_Portal*>(pPortal->m_hLinkedPortal.Get());
					if (pLinkedPortal)
					{
						AddPortalCornersToEnginePVS(pLinkedPortal);
					}
				}
			}
		}
	}
}

void CHL2MP_Player::SetupVisibility(CBaseEntity* pViewEntity, unsigned char* pvs, int pvssize)
{
	BaseClass::SetupVisibility(pViewEntity, pvs, pvssize);

	int area = pViewEntity ? pViewEntity->NetworkProp()->AreaNum() : NetworkProp()->AreaNum();

	// At this point the EyePosition has been added as a view origin, but if we are currently stuck
	// in a portal, our EyePosition may return a point in solid. Find the reflected eye position
	// and use that as a vis origin instead.
	if (m_hPortalEnvironment)
	{
		CProp_Portal* pPortal = NULL, * pRemotePortal = NULL;
		pPortal = m_hPortalEnvironment;
		pRemotePortal = pPortal->m_hLinkedPortal;

		if (pPortal && pRemotePortal && pPortal->m_bActivated && pRemotePortal->m_bActivated)
		{
			Vector ptPortalCenter = pPortal->GetAbsOrigin();
			Vector vPortalForward;
			pPortal->GetVectors(&vPortalForward, NULL, NULL);

			Vector eyeOrigin = EyePosition();
			Vector vEyeToPortalCenter = ptPortalCenter - eyeOrigin;

			float fPortalDist = vPortalForward.Dot(vEyeToPortalCenter);
			if (fPortalDist > 0.0f) //eye point is behind portal
			{
				// Move eye origin to it's transformed position on the other side of the portal
				UTIL_Portal_PointTransform(pPortal->MatrixThisToLinked(), eyeOrigin, eyeOrigin);

				// Use this as our view origin (as this is where the client will be displaying from)
				engine->AddOriginToPVS(eyeOrigin);
				if (!pViewEntity || pViewEntity->IsPlayer())
				{
					area = engine->GetArea(eyeOrigin);
				}
			}
		}
	}

	PortalSetupVisibility(this, area, pvs, pvssize);
}

void CHL2MP_Player::ForceDuckThisFrame(void)
{
	if (m_Local.m_bDucked != true)
	{
		//m_Local.m_bDucking = false;
		m_Local.m_bDucked = true;
		ForceButtons(IN_DUCK);
		AddFlag(FL_DUCKING);
		SetVCollisionState(GetAbsOrigin(), GetAbsVelocity(), VPHYS_CROUCH);
	}
}
bool CHL2MP_Player::IsInBuyZone()
{
	return m_bInBuyZone && !IsVIP();
}

bool CHL2MP_Player::CanPlayerBuy(bool display)
{
	// is the player in a buy zone?
	if (!IsInBuyZone())
	{
		return false;
	}

	CCSGameRules* mp = CSGameRules();

	// is the player alive?
	if (m_lifeState != LIFE_ALIVE)
	{
		return false;
	}

	int buyTime = (int)(mp_buytime.GetFloat() * 60);

	if (mp->IsBuyTimeElapsed())
	{
		if (display == true)
		{
			char strBuyTime[16];
			Q_snprintf(strBuyTime, sizeof(strBuyTime), "%d", buyTime);
			ClientPrint(this, HUD_PRINTCENTER, "#Cant_buy", strBuyTime);
		}

		return false;
	}

	if (m_bIsVIP)
	{
		if (display == true)
			ClientPrint(this, HUD_PRINTCENTER, "#VIP_cant_buy");

		return false;
	}

	if (mp->m_bCTCantBuy && (GetTeamNumber() == TEAM_CT))
	{
		if (display == true)
			ClientPrint(this, HUD_PRINTCENTER, "#CT_cant_buy");

		return false;
	}

	if (mp->m_bTCantBuy && (GetTeamNumber() == TEAM_TERRORIST))
	{
		if (display == true)
			ClientPrint(this, HUD_PRINTCENTER, "#Terrorist_cant_buy");

		return false;
	}

	return true;
}
void CHL2MP_Player::RescueZoneTouch(inputdata_t& inputdata)
{
	m_bInHostageRescueZone = true;
	if (GetTeamNumber() == TEAM_CT && !(m_iDisplayHistoryBits & DHF_IN_RESCUE_ZONE))
	{
		HintMessage("#Hint_hostage_rescue_zone", false);
		m_iDisplayHistoryBits |= DHF_IN_RESCUE_ZONE;
	}
}

bool CHL2MP_Player::RunMimicCommand(CUserCmd& cmd)
{
	if (!IsBot())
		return false;

	int iMimic = abs(bot_mimic.GetInt());
	if (iMimic > gpGlobals->maxClients)
		return false;

	CBasePlayer* pPlayer = UTIL_PlayerByIndex(iMimic);
	if (!pPlayer)
		return false;

	if (!pPlayer->GetLastUserCommand())
		return false;

	cmd = *pPlayer->GetLastUserCommand();
	cmd.viewangles[YAW] += bot_mimic_yaw_offset.GetFloat();

	pl.fixangle = FIXANGLE_NONE;

	return true;
}

bool CHL2MP_Player::HasC4() const
{
	return (Weapon_OwnsThisType("weapon_c4") != NULL);
}

CWeaponHL2MPBase* CHL2MP_Player::GetActiveCSWeapon() const
{
	return dynamic_cast<CWeaponHL2MPBase*>(GetActiveWeapon());
}
void CHL2MP_Player::AwardAchievement(int iAchievement, int iCount)
{
	Assert(iAchievement >= 0 && iAchievement < 0xFFFF);		// must fit in short

	CSingleUserRecipientFilter filter(this);

	UserMessageBegin(filter, "AchievementEvent");
	WRITE_SHORT(iAchievement);
	WRITE_SHORT(iCount);
	MessageEnd();
}
void CHL2MP_Player::IncrementNumMVPs(CSMvpReason_t mvpReason)
{
	//=============================================================================
	// HPE_BEGIN:
	// [Forrest] Allow MVP to be turned off for a server
	//=============================================================================
	if (sv_nomvp.GetBool())
	{
		Msg("Round MVP disabled: sv_nomvp is set.\n");
		return;
	}
	//=============================================================================
	// HPE_END
	//=============================================================================

	m_iMVPs++;
	CCS_GameStats.Event_MVPEarned(this);
	IGameEvent* mvpEvent = gameeventmanager->CreateEvent("round_mvp");

	if (mvpEvent)
	{
		mvpEvent->SetInt("userid", GetUserID());
		mvpEvent->SetInt("reason", mvpReason);
		gameeventmanager->FireEvent(mvpEvent);
	}
}

//-----------------------------------------------------------------------------
// Purpose: Sets the number of rounds this player has caused to be won for their team
//-----------------------------------------------------------------------------
void CHL2MP_Player::SetNumMVPs(int iNumMVP)
{
	m_iMVPs = iNumMVP;
}
//-----------------------------------------------------------------------------
// Purpose: Returns the number of rounds this player has caused to be won for their team
//-----------------------------------------------------------------------------
int CHL2MP_Player::GetNumMVPs()
{
	return m_iMVPs;
}

bool CHL2MP_Player::CSWeaponDrop(CBaseCombatWeapon* pWeapon, bool bDropShield, bool bThrowForward)
{
	bool bSuccess = false;

	if (HasShield() && bDropShield == true)
	{
		DropShield();
		return true;
	}

	if (pWeapon)
	{
		Vector vForward;

		AngleVectors(EyeAngles(), &vForward, NULL, NULL);
		//GetVectors( &vForward, NULL, NULL );
		Vector vTossPos = WorldSpaceCenter();

		if (bThrowForward)
			vTossPos = vTossPos + vForward * 64;

		Weapon_Drop(pWeapon, &vTossPos, NULL);

		pWeapon->SetSolidFlags(FSOLID_NOT_STANDABLE | FSOLID_TRIGGER | FSOLID_USE_TRIGGER_BOUNDS);
		pWeapon->SetMoveCollide(MOVECOLLIDE_FLY_BOUNCE);

		CWeaponHL2MPBase* pCSWeapon = dynamic_cast<CWeaponHL2MPBase*>(pWeapon);

		if (pCSWeapon)
		{
			pCSWeapon->SetWeaponModelIndex(pCSWeapon->GetCSWpnData().szWorldModel);

			//Find out the index of the ammo type
			int iAmmoIndex = pCSWeapon->GetPrimaryAmmoType();

			//If it has an ammo type, find out how much the player has
			if (iAmmoIndex != -1)
			{
				// Check to make sure we don't have other weapons using this ammo type
				bool bAmmoTypeInUse = false;
				if (IsAlive() && GetHealth() > 0)
				{
					for (int i = 0; i < MAX_WEAPONS; ++i)
					{
						CBaseCombatWeapon* pOtherWeapon = GetWeapon(i);
						if (pOtherWeapon && pOtherWeapon != pWeapon && pOtherWeapon->GetPrimaryAmmoType() == iAmmoIndex)
						{
							bAmmoTypeInUse = true;
							break;
						}
					}
				}

				if (!bAmmoTypeInUse)
				{
					int iAmmoToDrop = GetAmmoCount(iAmmoIndex);

					//Add this much to the dropped weapon
					pCSWeapon->SetExtraAmmoCount(iAmmoToDrop);

					//Remove all ammo of this type from the player
					SetAmmoCount(0, iAmmoIndex);
				}
			}
		}

		//=========================================
		// Teleport the weapon to the player's hand
		//=========================================
		int iBIndex = -1;
		int iWeaponBoneIndex = -1;

		MDLCACHE_CRITICAL_SECTION();
		CStudioHdr* hdr = pWeapon->GetModelPtr();
		// If I have a hand, set the weapon position to my hand bone position.
		if (hdr && hdr->numbones() > 0)
		{
			// Assume bone zero is the root
			for (iWeaponBoneIndex = 0; iWeaponBoneIndex < hdr->numbones(); ++iWeaponBoneIndex)
			{
				iBIndex = LookupBone(hdr->pBone(iWeaponBoneIndex)->pszName());
				// Found one!
				if (iBIndex != -1)
				{
					break;
				}
			}

			if (iWeaponBoneIndex == hdr->numbones())
				return true;

			if (iBIndex == -1)
			{
				iBIndex = LookupBone("ValveBiped.Bip01_R_Hand");
			}
		}
		else
		{
			iBIndex = LookupBone("ValveBiped.Bip01_R_Hand");
		}

		if (iBIndex != -1)
		{
			Vector origin;
			QAngle angles;
			matrix3x4_t transform;

			// Get the transform for the weapon bonetoworldspace in the NPC
			GetBoneTransform(iBIndex, transform);

			// find offset of root bone from origin in local space
			// Make sure we're detached from hierarchy before doing this!!!
			pWeapon->StopFollowingEntity();
			pWeapon->SetAbsOrigin(Vector(0, 0, 0));
			pWeapon->SetAbsAngles(QAngle(0, 0, 0));
			pWeapon->InvalidateBoneCache();
			matrix3x4_t rootLocal;
			pWeapon->GetBoneTransform(iWeaponBoneIndex, rootLocal);

			// invert it
			matrix3x4_t rootInvLocal;
			MatrixInvert(rootLocal, rootInvLocal);

			matrix3x4_t weaponMatrix;
			ConcatTransforms(transform, rootInvLocal, weaponMatrix);
			MatrixAngles(weaponMatrix, angles, origin);

			pWeapon->Teleport(&origin, &angles, NULL);

			//Have to teleport the physics object as well

			IPhysicsObject* pWeaponPhys = pWeapon->VPhysicsGetObject();

			if (pWeaponPhys)
			{
				Vector vPos;
				QAngle vAngles;
				pWeaponPhys->GetPosition(&vPos, &vAngles);
				pWeaponPhys->SetPosition(vPos, angles, true);

				AngularImpulse	angImp(0, 0, 0);
				Vector vecAdd = GetAbsVelocity();
				pWeaponPhys->AddVelocity(&vecAdd, &angImp);
			}
		}

		bSuccess = true;
	}

	return bSuccess;
}


bool CHL2MP_Player::DropRifle(bool fromDeath)
{
	bool bSuccess = false;

	CBaseCombatWeapon* pWeapon = Weapon_GetSlot(WEAPON_SLOT_RIFLE);
	if (pWeapon)
	{
		bSuccess = CSWeaponDrop(pWeapon, false);
	}

	//=============================================================================
	// HPE_BEGIN:
	// [menglish] Add the dropped weapon to the dropped equipment list
	//=============================================================================
	if (fromDeath && bSuccess)
	{
		m_hDroppedEquipment[DROPPED_WEAPON] = static_cast<CBaseEntity*>(pWeapon);
	}
	//=============================================================================
	// HPE_END
	//=============================================================================

	return bSuccess;
}


bool CHL2MP_Player::DropPistol(bool fromDeath)
{
	bool bSuccess = false;

	CBaseCombatWeapon* pWeapon = Weapon_GetSlot(WEAPON_SLOT_PISTOL);
	if (pWeapon)
	{
		bSuccess = CSWeaponDrop(pWeapon, false);
		m_bUsingDefaultPistol = false;
	}
	//=============================================================================
	// HPE_BEGIN:
	// [menglish] Add the dropped weapon to the dropped equipment list
	//=============================================================================
	if (fromDeath && bSuccess)
	{
		m_hDroppedEquipment[DROPPED_WEAPON] = static_cast<CBaseEntity*>(pWeapon);
	}
	//=============================================================================
	// HPE_END
	//=============================================================================

	return bSuccess;
}


bool CHL2MP_Player::HasPrimaryWeapon(void)
{
	bool bSuccess = false;

	CBaseCombatWeapon* pWeapon = Weapon_GetSlot(WEAPON_SLOT_RIFLE);

	if (pWeapon)
	{
		bSuccess = true;
	}

	return bSuccess;
}


bool CHL2MP_Player::HasSecondaryWeapon(void)
{
	bool bSuccess = false;

	CBaseCombatWeapon* pWeapon = Weapon_GetSlot(WEAPON_SLOT_PISTOL);
	if (pWeapon)
	{
		bSuccess = true;
	}

	return bSuccess;
}



BuyResult_e CHL2MP_Player::AttemptToBuyVest(void)
{
	int iKevlarPrice = KEVLAR_PRICE;

	if (CSGameRules()->IsBlackMarket())
	{
		iKevlarPrice = CSGameRules()->GetBlackMarketPriceForWeapon(WEAPON_KEVLAR);
	}

	if (ArmorValue() >= 100)
	{
		if (!m_bIsInAutoBuy && !m_bIsInRebuy)
			ClientPrint(this, HUD_PRINTCENTER, "#Already_Have_Kevlar");
		return BUY_ALREADY_HAVE;
	}
	else if (m_iAccount < iKevlarPrice)
	{
		if (!m_bIsInAutoBuy && !m_bIsInRebuy)
			ClientPrint(this, HUD_PRINTCENTER, "#Not_Enough_Money");
		return BUY_CANT_AFFORD;
	}
	else
	{
		if (m_bHasHelmet)
		{
			if (!m_bIsInAutoBuy && !m_bIsInRebuy)
				ClientPrint(this, HUD_PRINTCENTER, "#Already_Have_Helmet_Bought_Kevlar");
		}

		IGameEvent* event = gameeventmanager->CreateEvent("item_pickup");
		if (event)
		{
			event->SetInt("userid", GetUserID());
			event->SetString("item", "vest");
			gameeventmanager->FireEvent(event);
		}

		GiveNamedItem("item_kevlar");
		AddAccount(-iKevlarPrice, true, true, "item_kevlar");
		BlackMarketAddWeapon("item_kevlar", this);
		return BUY_BOUGHT;
	}
}


BuyResult_e CHL2MP_Player::AttemptToBuyAssaultSuit(void)
{
	// WARNING: This price logic also exists in C_HL2MP_Player::GetCurrentAssaultSuitPrice
	// and must be kept in sync if changes are made.

	int fullArmor = ArmorValue() >= 100 ? 1 : 0;

	int price = 0, enoughMoney = 0;

	int iHelmetPrice = HELMET_PRICE;
	int iKevlarPrice = KEVLAR_PRICE;
	int iAssaultSuitPrice = ASSAULTSUIT_PRICE;

	if (CSGameRules()->IsBlackMarket())
	{
		iKevlarPrice = CSGameRules()->GetBlackMarketPriceForWeapon(WEAPON_KEVLAR);
		iAssaultSuitPrice = CSGameRules()->GetBlackMarketPriceForWeapon(WEAPON_ASSAULTSUIT);

		iHelmetPrice = iAssaultSuitPrice - iKevlarPrice;
	}

	if (fullArmor && m_bHasHelmet)
	{
		if (!m_bIsInAutoBuy && !m_bIsInRebuy)
			ClientPrint(this, HUD_PRINTCENTER, "#Already_Have_Kevlar_Helmet");
		return BUY_ALREADY_HAVE;
	}
	else if (fullArmor && !m_bHasHelmet && m_iAccount >= iHelmetPrice)
	{
		enoughMoney = 1;
		price = iHelmetPrice;
		if (!m_bIsInAutoBuy && !m_bIsInRebuy)
			ClientPrint(this, HUD_PRINTCENTER, "#Already_Have_Kevlar_Bought_Helmet");
	}
	else if (!fullArmor && m_bHasHelmet && m_iAccount >= iKevlarPrice)
	{
		enoughMoney = 1;
		price = iKevlarPrice;
		if (!m_bIsInAutoBuy && !m_bIsInRebuy)
			ClientPrint(this, HUD_PRINTCENTER, "#Already_Have_Helmet_Bought_Kevlar");
	}
	else if (m_iAccount >= iAssaultSuitPrice)
	{
		enoughMoney = 1;
		price = iAssaultSuitPrice;
	}

	// process the result
	if (!enoughMoney)
	{
		if (!m_bIsInAutoBuy && !m_bIsInRebuy)
			ClientPrint(this, HUD_PRINTCENTER, "#Not_Enough_Money");
		return BUY_CANT_AFFORD;
	}
	else
	{
		IGameEvent* event = gameeventmanager->CreateEvent("item_pickup");
		if (event)
		{
			event->SetInt("userid", GetUserID());
			event->SetString("item", "vesthelm");
			gameeventmanager->FireEvent(event);
		}

		GiveNamedItem("item_assaultsuit");
		AddAccount(-price, true, true, "item_assaultsuit");
		BlackMarketAddWeapon("item_assaultsuit", this);
		return BUY_BOUGHT;
	}
}

BuyResult_e CHL2MP_Player::AttemptToBuyShield(void)
{
#ifdef CS_SHIELD_ENABLED
	if (HasShield())		// prevent this guy from buying more than 1 Defuse Kit
	{
		if (!m_bIsInAutoBuy && !m_bIsInRebuy)
			ClientPrint(this, HUD_PRINTCENTER, "#Already_Have_One");
		return BUY_ALREADY_HAVE;
	}
	else if (m_iAccount < SHIELD_PRICE)
	{
		if (!m_bIsInAutoBuy && !m_bIsInRebuy)
			ClientPrint(this, HUD_PRINTCENTER, "#Not_Enough_Money");
		return BUY_CANT_AFFORD;
	}
	else
	{
		if (HasSecondaryWeapon())
		{
			CBaseCombatWeapon* pWeapon = Weapon_GetSlot(WEAPON_SLOT_PISTOL);
			CWeaponHL2MPBase* pCSWeapon = dynamic_cast<CWeaponHL2MPBase*>(pWeapon);

			if (pCSWeapon && pCSWeapon->GetCSWpnData().m_bCanUseWithShield == false)
				return;
		}

		if (HasPrimaryWeapon())
			DropRifle();

		GiveShield();

		CPASAttenuationFilter filter(this, "Player.PickupWeapon");
		EmitSound(filter, entindex(), "Player.PickupWeapon");

		m_bAnythingBought = true;
		AddAccount(-SHIELD_PRICE, true, true, "item_shield");
		return BUY_BOUGHT;
	}
#else
	ClientPrint(this, HUD_PRINTCENTER, "Tactical shield disabled");
	return BUY_NOT_ALLOWED;
#endif
}

BuyResult_e CHL2MP_Player::AttemptToBuyDefuser(void)
{
	CCSGameRules* MPRules = CSGameRules();

	if ((GetTeamNumber() == TEAM_CT) && MPRules->IsBombDefuseMap())
	{
		if (HasDefuser())		// prevent this guy from buying more than 1 Defuse Kit
		{
			if (!m_bIsInAutoBuy && !m_bIsInRebuy)
				ClientPrint(this, HUD_PRINTCENTER, "#Already_Have_One");
			return BUY_ALREADY_HAVE;
		}
		else if (m_iAccount < DEFUSEKIT_PRICE)
		{
			if (!m_bIsInAutoBuy && !m_bIsInRebuy)
				ClientPrint(this, HUD_PRINTCENTER, "#Not_Enough_Money");
			return BUY_CANT_AFFORD;
		}
		else
		{
			GiveDefuser();

			CPASAttenuationFilter filter(this, "Player.PickupWeapon");
			EmitSound(filter, entindex(), "Player.PickupWeapon");

			AddAccount(-DEFUSEKIT_PRICE, true, true, "item_defuser");
			return BUY_BOUGHT;
		}
	}

	return BUY_NOT_ALLOWED;
}

BuyResult_e CHL2MP_Player::AttemptToBuyNightVision(void)
{
	int iNVGPrice = NVG_PRICE;

	if (CSGameRules()->IsBlackMarket())
	{
		iNVGPrice = CSGameRules()->GetBlackMarketPriceForWeapon(WEAPON_NVG);
	}

	if (m_bHasNightVision == TRUE)
	{
		if (!m_bIsInAutoBuy && !m_bIsInRebuy)
			ClientPrint(this, HUD_PRINTCENTER, "#Already_Have_One");
		return BUY_ALREADY_HAVE;
	}
	else if (m_iAccount < iNVGPrice)
	{
		if (!m_bIsInAutoBuy && !m_bIsInRebuy)
			ClientPrint(this, HUD_PRINTCENTER, "#Not_Enough_Money");
		return BUY_CANT_AFFORD;
	}
	else
	{
		IGameEvent* event = gameeventmanager->CreateEvent("item_pickup");
		if (event)
		{
			event->SetInt("userid", GetUserID());
			event->SetString("item", "nvgs");
			gameeventmanager->FireEvent(event);
		}

		GiveNamedItem("item_nvgs");
		AddAccount(-iNVGPrice, true, true);
		BlackMarketAddWeapon("nightvision", this);

		if (!(m_iDisplayHistoryBits & DHF_NIGHTVISION))
		{
			HintMessage("#Hint_use_nightvision", false);
			m_iDisplayHistoryBits |= DHF_NIGHTVISION;
		}
		return BUY_BOUGHT;
	}
}


// Handles the special "buy" alias commands we're creating to accommodate the buy
// scripts players use (now that we've rearranged the buy menus and broken the scripts)
//=============================================================================
// HPE_BEGIN:
//[tj]  This is essentially a shim so I can easily check the return
//      value without adding new code to all the return points.
//=============================================================================

BuyResult_e CHL2MP_Player::HandleCommand_Buy(const char* item)
{
	BuyResult_e result = HandleCommand_Buy_Internal(item);
	if (result == BUY_BOUGHT)
	{
		m_bMadePurchseThisRound = true;
		CCS_GameStats.IncrementStat(this, CSSTAT_ITEMS_PURCHASED, 1);
	}
	return result;
}

BuyResult_e CHL2MP_Player::HandleCommand_Buy_Internal(const char* wpnName)
//=============================================================================
// HPE_END
//=============================================================================
{
	BuyResult_e result = CanPlayerBuy(false) ? BUY_PLAYER_CANT_BUY : BUY_INVALID_ITEM; // set some defaults

	// translate the new weapon names to the old ones that are actually being used.
	wpnName = GetTranslatedWeaponAlias(wpnName);

	CCSWeaponInfo* pWeaponInfo = GetWeaponInfo(AliasToWeaponID(wpnName));
	if (pWeaponInfo == NULL)
	{
		if (Q_stricmp(wpnName, "primammo") == 0)
		{
			result = AttemptToBuyAmmo(0);
		}
		else if (Q_stricmp(wpnName, "secammo") == 0)
		{
			result = AttemptToBuyAmmo(1);
		}
		else if (Q_stristr(wpnName, "defuser"))
		{
			if (CanPlayerBuy(true))
			{
				result = AttemptToBuyDefuser();
			}
		}
	}
	else
	{

		if (!CanPlayerBuy(true))
		{
			return BUY_PLAYER_CANT_BUY;
		}

		BuyResult_e equipResult = BUY_INVALID_ITEM;

		if (Q_stristr(wpnName, "kevlar"))
		{
			equipResult = AttemptToBuyVest();
		}
		else if (Q_stristr(wpnName, "assaultsuit"))
		{
			equipResult = AttemptToBuyAssaultSuit();
		}
		else if (Q_stristr(wpnName, "shield"))
		{
			equipResult = AttemptToBuyShield();
		}
		else if (Q_stristr(wpnName, "nightvision"))
		{
			equipResult = AttemptToBuyNightVision();
		}

		if (equipResult != BUY_INVALID_ITEM)
		{
			if (equipResult == BUY_BOUGHT)
			{
				BuildRebuyStruct();
			}
			return equipResult; // intentional early return here
		}

		bool bPurchase = false;

		// MIKETODO: assasination maps have a specific set of weapons that can be used in them.
		if (pWeaponInfo->m_iTeam != TEAM_UNASSIGNED && GetTeamNumber() != pWeaponInfo->m_iTeam)
		{
			result = BUY_NOT_ALLOWED;
			if (pWeaponInfo->m_WrongTeamMsg[0] != 0)
			{
				ClientPrint(this, HUD_PRINTCENTER, "#Alias_Not_Avail", pWeaponInfo->m_WrongTeamMsg);
			}
		}
		else if (pWeaponInfo->GetWeaponPrice() <= 0)
		{
			// ClientPrint( this, HUD_PRINTCENTER, "#Cant_buy_this_item", pWeaponInfo->m_WrongTeamMsg );
		}
		else if (pWeaponInfo->m_WeaponType == WEAPONTYPE_GRENADE)
		{
			// make sure the player can afford this item.
			if (m_iAccount >= pWeaponInfo->GetWeaponPrice())
			{
				bPurchase = true;

				const char* szWeaponName = NULL;
				int ammoMax = 0;
				if (Q_strstr(pWeaponInfo->szClassName, "flashbang"))
				{
					szWeaponName = "weapon_flashbang";
					ammoMax = ammo_flashbang_max.GetInt();
				}
				else if (Q_strstr(pWeaponInfo->szClassName, "hegrenade"))
				{
					szWeaponName = "weapon_hegrenade";
					ammoMax = ammo_hegrenade_max.GetInt();
				}
				else if (Q_strstr(pWeaponInfo->szClassName, "smokegrenade"))
				{
					szWeaponName = "weapon_smokegrenade";
					ammoMax = ammo_smokegrenade_max.GetInt();
				}

				if (szWeaponName != NULL)
				{
					CBaseCombatWeapon* pGrenadeWeapon = Weapon_OwnsThisType(szWeaponName);
					{
						if (pGrenadeWeapon != NULL)
						{
							int nAmmoType = pGrenadeWeapon->GetPrimaryAmmoType();

							if (nAmmoType != -1)
							{
								if (GetAmmoCount(nAmmoType) >= ammoMax)
								{
									result = BUY_ALREADY_HAVE;
									if (!m_bIsInAutoBuy && !m_bIsInRebuy)
										ClientPrint(this, HUD_PRINTCENTER, "#Cannot_Carry_Anymore");
									bPurchase = false;
								}
							}
						}
					}
				}
			}
		}
		else if (!Weapon_OwnsThisType(pWeaponInfo->szClassName))	//don't buy duplicate weapons
		{
			// do they have enough money?
			if (m_iAccount >= pWeaponInfo->GetWeaponPrice())
			{
				if (m_lifeState != LIFE_DEAD)
				{
					if (pWeaponInfo->iSlot == WEAPON_SLOT_PISTOL)
					{
						DropPistol();
					}
					else if (pWeaponInfo->iSlot == WEAPON_SLOT_RIFLE)
					{
						DropRifle();
					}
				}

				bPurchase = true;
			}
			else
			{
				result = BUY_CANT_AFFORD;
				if (!m_bIsInAutoBuy && !m_bIsInRebuy)
					ClientPrint(this, HUD_PRINTCENTER, "#Not_Enough_Money");
			}
		}
		else
		{
			result = BUY_ALREADY_HAVE;
		}

		if (HasShield())
		{
			if (pWeaponInfo->m_bCanUseWithShield == false)
			{
				result = BUY_NOT_ALLOWED;
				bPurchase = false;
			}
		}

		if (bPurchase)
		{
			result = BUY_BOUGHT;

			if (bPurchase && pWeaponInfo->iSlot == WEAPON_SLOT_PISTOL)
				m_bUsingDefaultPistol = false;

			GiveNamedItem(pWeaponInfo->szClassName);
			AddAccount(-pWeaponInfo->GetWeaponPrice(), true, true, pWeaponInfo->szClassName);
			BlackMarketAddWeapon(wpnName, this);
		}
	}

	if (result == BUY_BOUGHT)
	{
		BuildRebuyStruct();
	}

	return result;
}


BuyResult_e CHL2MP_Player::BuyGunAmmo(CBaseCombatWeapon* pWeapon, bool bBlinkMoney)
{
	if (!CanPlayerBuy(false))
	{
		return BUY_PLAYER_CANT_BUY;
	}

	// Ensure that the weapon uses ammo
	int nAmmo = pWeapon->GetPrimaryAmmoType();
	if (nAmmo == -1)
	{
		return BUY_ALREADY_HAVE;
	}

	// Can only buy if the player does not already have full ammo
	if (GetAmmoCount(nAmmo) >= GetAmmoDef()->MaxCarry(nAmmo))
	{
		return BUY_ALREADY_HAVE;
	}

	// Purchase the ammo if the player has enough money
	if (m_iAccount >= GetCSAmmoDef()->GetCost(nAmmo))
	{
		GiveAmmo(GetCSAmmoDef()->GetBuySize(nAmmo), nAmmo, true);
		AddAccount(-GetCSAmmoDef()->GetCost(nAmmo), true, true, GetCSAmmoDef()->GetAmmoOfIndex(nAmmo)->pName);
		return BUY_BOUGHT;
	}

	if (bBlinkMoney)
	{
		// Not enough money.. let the player know
		if (!m_bIsInAutoBuy && !m_bIsInRebuy)
			ClientPrint(this, HUD_PRINTCENTER, "#Not_Enough_Money");
	}

	return BUY_CANT_AFFORD;
}


BuyResult_e CHL2MP_Player::BuyAmmo(int nSlot, bool bBlinkMoney)
{
	if (!CanPlayerBuy(false))
	{
		return BUY_PLAYER_CANT_BUY;
	}

	if (nSlot < 0 || nSlot > 1)
	{
		return BUY_INVALID_ITEM;
	}

	// Buy one ammo clip for all weapons in the given slot
	//
	//  nSlot == 1 : Primary weapons
	//  nSlot == 2 : Secondary weapons

	CBaseCombatWeapon* pSlot = Weapon_GetSlot(nSlot);
	if (!pSlot)
		return BUY_INVALID_ITEM;

	//MIKETODO: shield.
	//if ( player->HasShield() && player->m_rgpPlayerItems[2] )
	//	 pItem = player->m_rgpPlayerItems[2];

	return BuyGunAmmo(pSlot, bBlinkMoney);
}


BuyResult_e CHL2MP_Player::AttemptToBuyAmmo(int iAmmoType)
{
	Assert(iAmmoType == 0 || iAmmoType == 1);

	BuyResult_e result = BuyAmmo(iAmmoType, true);

	if (result == BUY_BOUGHT)
	{
		while (BuyAmmo(iAmmoType, false) == BUY_BOUGHT)
		{
			// empty loop - keep buying
		}

		return BUY_BOUGHT;
	}

	return result;
}

BuyResult_e CHL2MP_Player::AttemptToBuyAmmoSingle(int iAmmoType)
{
	Assert(iAmmoType == 0 || iAmmoType == 1);

	BuyResult_e result = BuyAmmo(iAmmoType, true);

	if (result == BUY_BOUGHT)
	{
		BuildRebuyStruct();
	}

	return result;
}
void CHL2MP_Player::HandleMenu_Radio1(int slot)
{
	if (m_iRadioMessages < 0)
		return;

	if (m_flRadioTime > gpGlobals->curtime)
		return;

	m_iRadioMessages--;
	m_flRadioTime = gpGlobals->curtime + 1.5;

	switch (slot)
	{
	case 1:
		Radio("Radio.CoverMe", "#Cstrike_TitlesTXT_Cover_me");
		break;

	case 2:
		Radio("Radio.YouTakeThePoint", "#Cstrike_TitlesTXT_You_take_the_point");
		break;

	case 3:
		Radio("Radio.HoldPosition", "#Cstrike_TitlesTXT_Hold_this_position");
		break;

	case 4:
		Radio("Radio.Regroup", "#Cstrike_TitlesTXT_Regroup_team");
		break;

	case 5:
		Radio("Radio.FollowMe", "#Cstrike_TitlesTXT_Follow_me");
		break;

	case 6:
		Radio("Radio.TakingFire", "#Cstrike_TitlesTXT_Taking_fire");
		break;
	}

	// tell bots about radio message
	IGameEvent* event = gameeventmanager->CreateEvent("player_radio");
	if (event)
	{
		event->SetInt("userid", GetUserID());
		event->SetInt("slot", RADIO_START_1 + slot);
		gameeventmanager->FireEvent(event);
	}
}

void CHL2MP_Player::HandleMenu_Radio2(int slot)
{
	if (m_iRadioMessages < 0)
		return;

	if (m_flRadioTime > gpGlobals->curtime)
		return;

	m_iRadioMessages--;
	m_flRadioTime = gpGlobals->curtime + 1.5;

	switch (slot)
	{
	case 1:
		Radio("Radio.GoGoGo", "#Cstrike_TitlesTXT_Go_go_go");
		break;

	case 2:
		Radio("Radio.TeamFallBack", "#Cstrike_TitlesTXT_Team_fall_back");
		break;

	case 3:
		Radio("Radio.StickTogether", "#Cstrike_TitlesTXT_Stick_together_team");
		break;

	case 4:
		Radio("Radio.GetInPosition", "#Cstrike_TitlesTXT_Get_in_position_and_wait");
		break;

	case 5:
		Radio("Radio.StormFront", "#Cstrike_TitlesTXT_Storm_the_front");
		break;

	case 6:
		Radio("Radio.ReportInTeam", "#Cstrike_TitlesTXT_Report_in_team");
		break;
	}

	// tell bots about radio message
	IGameEvent* event = gameeventmanager->CreateEvent("player_radio");
	if (event)
	{
		event->SetInt("userid", GetUserID());
		event->SetInt("slot", RADIO_START_2 + slot);
		gameeventmanager->FireEvent(event);
	}
}

void CHL2MP_Player::HandleMenu_Radio3(int slot)
{
	if (m_iRadioMessages < 0)
		return;

	if (m_flRadioTime > gpGlobals->curtime)
		return;

	m_iRadioMessages--;
	m_flRadioTime = gpGlobals->curtime + 1.5;

	switch (slot)
	{
	case 1:
		if (random->RandomInt(0, 1))
			Radio("Radio.Affirmitive", "#Cstrike_TitlesTXT_Affirmative");
		else
			Radio("Radio.Roger", "#Cstrike_TitlesTXT_Roger_that");

		break;

	case 2:
		Radio("Radio.EnemySpotted", "#Cstrike_TitlesTXT_Enemy_spotted");
		break;

	case 3:
		Radio("Radio.NeedBackup", "#Cstrike_TitlesTXT_Need_backup");
		break;

	case 4:
		Radio("Radio.SectorClear", "#Cstrike_TitlesTXT_Sector_clear");
		break;

	case 5:
		Radio("Radio.InPosition", "#Cstrike_TitlesTXT_In_position");
		break;

	case 6:
		Radio("Radio.ReportingIn", "#Cstrike_TitlesTXT_Reporting_in");
		break;

	case 7:
		Radio("Radio.GetOutOfThere", "#Cstrike_TitlesTXT_Get_out_of_there");
		break;

	case 8:
		Radio("Radio.Negative", "#Cstrike_TitlesTXT_Negative");
		break;

	case 9:
		Radio("Radio.EnemyDown", "#Cstrike_TitlesTXT_Enemy_down");
		break;
	}

	// tell bots about radio message
	IGameEvent* event = gameeventmanager->CreateEvent("player_radio");
	if (event)
	{
		event->SetInt("userid", GetUserID());
		event->SetInt("slot", RADIO_START_3 + slot);
		gameeventmanager->FireEvent(event);
	}
}

bool CHL2MP_Player::HandleCommand_JoinClass(int iClass)
{
	if (iClass == CS_CLASS_NONE)
	{
		// User choosed random class
		switch (GetTeamNumber())
		{
		case TEAM_TERRORIST:	iClass = RandomInt(FIRST_T_CLASS, LAST_T_CLASS);
			break;

		case TEAM_CT:			iClass = RandomInt(FIRST_CT_CLASS, LAST_CT_CLASS);
			break;

		default:				iClass = CS_CLASS_NONE;
			break;
		}
	}

	// clamp to valid classes
	switch (GetTeamNumber())
	{
	case TEAM_TERRORIST:
		iClass = clamp(iClass, FIRST_T_CLASS, LAST_T_CLASS);
		break;
	case TEAM_CT:
		iClass = clamp(iClass, FIRST_CT_CLASS, LAST_CT_CLASS);
		break;
	default:
		iClass = CS_CLASS_NONE;
	}

	// Reset the player's state
	if (State_Get() == STATE_ACTIVE)
	{
		CSGameRules()->CheckWinConditions();
	}

	if (!IsBot() && State_Get() == STATE_ACTIVE) // Bots are responsible about only switching classes when they join.
	{
		// Kill player if switching classes while alive.
		// This mimics goldsrc CS 1.6, and prevents a player from hiding, and switching classes to
		// make the opposing team think there are more enemies than there really are.
		CommitSuicide();
	}

	m_iClass = iClass;

	if (State_Get() == STATE_PICKINGCLASS)
	{
		// 		SetModelFromClass();
		GetIntoGame();
	}

	return true;
}
void CHL2MP_Player::MakeVIP(bool isVIP)
{
	if (isVIP)
	{
		NotVIP notVIP;
		ForEachPlayer(notVIP);
	}
	m_isVIP = isVIP;
}
void CHL2MP_Player::DoAnimationEvent(PlayerAnimEvent_t event, int nData)
{
	/*if (event == PLAYERANIMEVENT_THROW_GRENADE)
	{
		// Grenade throwing has to synchronize exactly with the player's grenade weapon going away,
		// and events get delayed a bit, so we let CCSPlayerAnimState pickup the change to this
		// variable.
		m_iThrowGrenadeCounter = (m_iThrowGrenadeCounter + 1) % (1 << THROWGRENADE_COUNTER_BITS);
	}
	else
	{
		m_PlayerAnimState.DoAnimationEvent(event, nData);
		TE_PlayerAnimEvent(this, event, nData);	// Send to any clients who can see this guy.
	}*/
}


void CHL2MP_Player::DropShield( void )
{
#ifdef CS_SHIELD_ENABLED
	//Drop an item_defuser
	Vector vForward, vRight;
	AngleVectors( GetAbsAngles(), &vForward, &vRight, NULL );

	RemoveShield();

	CBaseAnimating *pShield = (CBaseAnimating *)CBaseEntity::Create( "item_shield", WorldSpaceCenter(), GetLocalAngles() );
	pShield->ApplyAbsVelocityImpulse( vForward * 200 + vRight * random->RandomFloat( -50, 50 ) );

	CBaseCombatWeapon *pActive = GetActiveWeapon();

	if ( pActive )
	{
		pActive->Deploy();
	}
#endif
}

void CHL2MP_Player::SetShieldDrawnState( bool bState )
{
#ifdef CS_SHIELD_ENABLED
	m_bShieldDrawn = bState;
#endif
}
static unsigned int s_BulletGroupCounter = 0;

void CHL2MP_Player::StartNewBulletGroup()
{
	s_BulletGroupCounter++;
}

int CHL2MP_Player::PlayerClass() const
{
	return m_iClass;
}
void CHL2MP_Player::AutoBuy()
{
	if (!IsInBuyZone())
	{
		EmitPrivateSound("BuyPreset.CantBuy");
		return;
	}

	const char* autobuyString = engine->GetClientConVarValue(engine->IndexOfEdict(edict()), "cl_autobuy");
	if (!autobuyString || !*autobuyString)
	{
		EmitPrivateSound("BuyPreset.AlreadyBought");
		return;
	}

	bool boughtPrimary = false, boughtSecondary = false;

	m_bIsInAutoBuy = true;
	ParseAutoBuyString(autobuyString, boughtPrimary, boughtSecondary);
	m_bIsInAutoBuy = false;

	m_bAutoReload = true;

	//TODO ?: stripped out all the attempts to buy career weapons.
	// as we're not porting cs:cz, these were skipped
}

//-----------------------------------------------------------------------------
// Purpose: Removes all nemesis relationships between this player and others
//-----------------------------------------------------------------------------
void CHL2MP_Player::RemoveNemesisRelationships()
{
	for (int i = 1; i <= gpGlobals->maxClients; i++)
	{
		CHL2MP_Player* pTemp = ToCSPlayer(UTIL_PlayerByIndex(i));
		if (pTemp && pTemp != this)
		{
			// set this player to be not dominating anyone else
			SetPlayerDominated(pTemp, false);

			// set no one else to be dominating this player		
			pTemp->SetPlayerDominated(this, false);
		}
	}
}
void CHL2MP_Player::SetPlayerDominated(CHL2MP_Player* pPlayer, bool bDominated)
{
	int iPlayerIndex = pPlayer->entindex();
	m_bPlayerDominated.Set(iPlayerIndex, bDominated);
	pPlayer->SetPlayerDominatingMe(this, bDominated);
}

//-----------------------------------------------------------------------------
// Purpose: Sets whether this player is being dominated by the other player
//-----------------------------------------------------------------------------
void CHL2MP_Player::SetPlayerDominatingMe(CHL2MP_Player* pPlayer, bool bDominated)
{
	int iPlayerIndex = pPlayer->entindex();
	m_bPlayerDominatingMe.Set(iPlayerIndex, bDominated);
}


//-----------------------------------------------------------------------------
// Purpose: Returns whether this player is dominating the specified other player
//-----------------------------------------------------------------------------
bool CHL2MP_Player::IsPlayerDominated(int iPlayerIndex)
{
	return m_bPlayerDominated.Get(iPlayerIndex);
}

bool CHL2MP_Player::IsPlayerDominatingMe(int iPlayerIndex)
{
	return m_bPlayerDominatingMe.Get(iPlayerIndex);
}









void CHL2MP_Player::BuildRebuyStruct()
{
	if (m_bIsInRebuy)
	{
		// if we are in the middle of a rebuy, we don't want to update the buy struct.
		return;
	}

	CBaseCombatWeapon* primary = Weapon_GetSlot(WEAPON_SLOT_RIFLE);
	CBaseCombatWeapon* secondary = Weapon_GetSlot(WEAPON_SLOT_PISTOL);

	// do the primary weapon/ammo stuff.
	if (primary == NULL)
	{
		// count a shieldgun as a primary.
		if (HasShield())
		{
			//m_rebuyStruct.m_primaryWeapon = WEAPON_SHIELDGUN;
			Q_strncpy(m_rebuyStruct.m_szPrimaryWeapon, "shield", sizeof(m_rebuyStruct.m_szPrimaryWeapon));
			m_rebuyStruct.m_primaryAmmo = 0; // shields don't have ammo.
		}
		else
		{

			m_rebuyStruct.m_szPrimaryWeapon[0] = 0;	// if we don't have a shield and we don't have a primary weapon, we got nuthin.
			m_rebuyStruct.m_primaryAmmo = 0;		// can't have ammo if we don't have a gun right?
		}
	}
	else
	{
		//strip off the "weapon_"

		const char* wpnName = primary->GetClassname();

		Q_strncpy(m_rebuyStruct.m_szPrimaryWeapon, wpnName + 7, sizeof(m_rebuyStruct.m_szPrimaryWeapon));

		if (primary->GetPrimaryAmmoType() != -1)
		{
			m_rebuyStruct.m_primaryAmmo = GetAmmoCount(primary->GetPrimaryAmmoType());
		}
	}

	// do the secondary weapon/ammo stuff.
	if (secondary == NULL)
	{
		m_rebuyStruct.m_szSecondaryWeapon[0] = 0;
		m_rebuyStruct.m_secondaryAmmo = 0; // can't have ammo if we don't have a gun right?
	}
	else
	{
		const char* wpnName = secondary->GetClassname();

		Q_strncpy(m_rebuyStruct.m_szSecondaryWeapon, wpnName + 7, sizeof(m_rebuyStruct.m_szSecondaryWeapon));

		if (secondary->GetPrimaryAmmoType() != -1)
		{
			m_rebuyStruct.m_secondaryAmmo = GetAmmoCount(secondary->GetPrimaryAmmoType());
		}
	}

	CBaseCombatWeapon* pGrenade;

	//MATTTODO: right now you can't buy more than one grenade. make it so you can
	//buy more and query the number you have.
	// HE Grenade
	pGrenade = Weapon_OwnsThisType("weapon_hegrenade");
	if (pGrenade && pGrenade->GetPrimaryAmmoType() != -1)
	{
		m_rebuyStruct.m_heGrenade = GetAmmoCount(pGrenade->GetPrimaryAmmoType());
	}
	else
		m_rebuyStruct.m_heGrenade = 0;


	// flashbang
	pGrenade = Weapon_OwnsThisType("weapon_flashbang");
	if (pGrenade && pGrenade->GetPrimaryAmmoType() != -1)
	{
		m_rebuyStruct.m_flashbang = GetAmmoCount(pGrenade->GetPrimaryAmmoType());
	}
	else
		m_rebuyStruct.m_flashbang = 0;

	// smokegrenade
	pGrenade = Weapon_OwnsThisType("weapon_smokegrenade");
	if (pGrenade /*&& pGrenade->GetPrimaryAmmoType() != -1*/)
	{
		m_rebuyStruct.m_smokeGrenade = 1; //GetAmmoCount(pGrenade->GetPrimaryAmmoType());
	}
	else
		m_rebuyStruct.m_smokeGrenade = 0;

	// defuser
	m_rebuyStruct.m_defuser = HasDefuser();

	// night vision
	m_rebuyStruct.m_nightVision = m_bHasNightVision.Get();	//cast to avoid strange compiler warning

	// check for armor.
	m_rebuyStruct.m_armor = (m_bHasHelmet ? 2 : (ArmorValue() > 0 ? 1 : 0));
}

void CHL2MP_Player::Rebuy(void)
{
	if (!IsInBuyZone())
	{
		EmitPrivateSound("BuyPreset.CantBuy");
		return;
	}

	const char* rebuyString = engine->GetClientConVarValue(engine->IndexOfEdict(edict()), "cl_rebuy");
	if (!rebuyString || !*rebuyString)
	{
		EmitPrivateSound("BuyPreset.AlreadyBought");
		return;
	}

	m_bIsInRebuy = true;
	BuyResult_e overallResult = BUY_ALREADY_HAVE;

	char token[256];
	rebuyString = engine->ParseFile(rebuyString, token, sizeof(token));

	while (rebuyString != NULL)
	{
		BuyResult_e result = BUY_ALREADY_HAVE;

		if (!Q_strncmp(token, "PrimaryWeapon", 14))
		{
			result = RebuyPrimaryWeapon();
		}
		else if (!Q_strncmp(token, "PrimaryAmmo", 12))
		{
			result = RebuyPrimaryAmmo();
		}
		else if (!Q_strncmp(token, "SecondaryWeapon", 16))
		{
			result = RebuySecondaryWeapon();
		}
		else if (!Q_strncmp(token, "SecondaryAmmo", 14))
		{
			result = RebuySecondaryAmmo();
		}
		else if (!Q_strncmp(token, "HEGrenade", 10))
		{
			result = RebuyHEGrenade();
		}
		else if (!Q_strncmp(token, "Flashbang", 10))
		{
			result = RebuyFlashbang();
		}
		else if (!Q_strncmp(token, "SmokeGrenade", 13))
		{
			result = RebuySmokeGrenade();
		}
		else if (!Q_strncmp(token, "Defuser", 8))
		{
			result = RebuyDefuser();
		}
		else if (!Q_strncmp(token, "NightVision", 12))
		{
			result = RebuyNightVision();
		}
		else if (!Q_strncmp(token, "Armor", 6))
		{
			result = RebuyArmor();
		}

		overallResult = CombineBuyResults(overallResult, result);

		rebuyString = engine->ParseFile(rebuyString, token, sizeof(token));
	}

	m_bIsInRebuy = false;

	// after we're done buying, the user is done with their equipment purchasing experience.
	// so we are effectively out of the buy zone.
//	if (TheTutor != NULL)
//	{
//		TheTutor->OnEvent(EVENT_PLAYER_LEFT_BUY_ZONE);
//	}

	m_bAutoReload = true;

	if (overallResult == BUY_CANT_AFFORD)
	{
		EmitPrivateSound("BuyPreset.CantBuy");
	}
	else if (overallResult == BUY_ALREADY_HAVE)
	{
		EmitPrivateSound("BuyPreset.AlreadyBought");
	}
	else if (overallResult == BUY_BOUGHT)
	{
		g_iReBuyPurchases++;
	}
}

BuyResult_e CHL2MP_Player::RebuyPrimaryWeapon()
{
	CBaseCombatWeapon* primary = Weapon_GetSlot(WEAPON_SLOT_RIFLE);
	if (primary != NULL)
	{
		return BUY_ALREADY_HAVE;	// don't drop primary weapons via rebuy - if the player picked up a different weapon, he wants to keep it.
	}

	if (strlen(m_rebuyStruct.m_szPrimaryWeapon) > 0)
		return HandleCommand_Buy(m_rebuyStruct.m_szPrimaryWeapon);

	return BUY_ALREADY_HAVE;
}

BuyResult_e CHL2MP_Player::RebuySecondaryWeapon()
{
	CBaseCombatWeapon* pistol = Weapon_GetSlot(WEAPON_SLOT_PISTOL);
	if (pistol != NULL && !m_bUsingDefaultPistol)
	{
		return BUY_ALREADY_HAVE;	// don't drop pistols via rebuy if we've bought one other than the default pistol
	}

	if (strlen(m_rebuyStruct.m_szSecondaryWeapon) > 0)
		return HandleCommand_Buy(m_rebuyStruct.m_szSecondaryWeapon);

	return BUY_ALREADY_HAVE;
}

BuyResult_e CHL2MP_Player::RebuyPrimaryAmmo()
{
	CBaseCombatWeapon* primary = Weapon_GetSlot(WEAPON_SLOT_RIFLE);

	if (primary == NULL)
	{
		return BUY_ALREADY_HAVE;	// can't buy ammo when we don't even have a gun.
	}

	// Ensure that the weapon uses ammo
	int nAmmo = primary->GetPrimaryAmmoType();
	if (nAmmo == -1)
	{
		return BUY_ALREADY_HAVE;
	}

	// if we had more ammo before than we have now, buy more.
	if (m_rebuyStruct.m_primaryAmmo > GetAmmoCount(nAmmo))
	{
		return HandleCommand_Buy("primammo");
	}

	return BUY_ALREADY_HAVE;
}


BuyResult_e CHL2MP_Player::RebuySecondaryAmmo()
{
	CBaseCombatWeapon* secondary = Weapon_GetSlot(WEAPON_SLOT_PISTOL);

	if (secondary == NULL)
	{
		return BUY_ALREADY_HAVE; // can't buy ammo when we don't even have a gun.
	}

	// Ensure that the weapon uses ammo
	int nAmmo = secondary->GetPrimaryAmmoType();
	if (nAmmo == -1)
	{
		return BUY_ALREADY_HAVE;
	}

	if (m_rebuyStruct.m_secondaryAmmo > GetAmmoCount(nAmmo))
	{
		return HandleCommand_Buy("secammo");
	}

	return BUY_ALREADY_HAVE;
}

BuyResult_e CHL2MP_Player::RebuyHEGrenade()
{
	CBaseCombatWeapon* pGrenade = Weapon_OwnsThisType("weapon_hegrenade");

	int numGrenades = 0;

	if (pGrenade)
	{
		int nAmmo = pGrenade->GetPrimaryAmmoType();
		if (nAmmo == -1)
		{
			return BUY_ALREADY_HAVE;
		}

		numGrenades = GetAmmoCount(nAmmo);
	}

	BuyResult_e overallResult = BUY_ALREADY_HAVE;
	int numToBuy = MAX(0, m_rebuyStruct.m_heGrenade - numGrenades);
	for (int i = 0; i < numToBuy; ++i)
	{
		BuyResult_e result = HandleCommand_Buy("hegrenade");
		overallResult = CombineBuyResults(overallResult, result);
	}

	return overallResult;
}

BuyResult_e CHL2MP_Player::RebuyFlashbang()
{
	CBaseCombatWeapon* pGrenade = Weapon_OwnsThisType("weapon_flashbang");

	int numGrenades = 0;

	if (pGrenade)
	{
		int nAmmo = pGrenade->GetPrimaryAmmoType();
		if (nAmmo == -1)
		{
			return BUY_ALREADY_HAVE;
		}
		numGrenades = GetAmmoCount(nAmmo);

	}

	BuyResult_e overallResult = BUY_ALREADY_HAVE;
	int numToBuy = MAX(0, m_rebuyStruct.m_flashbang - numGrenades);
	for (int i = 0; i < numToBuy; ++i)
	{
		BuyResult_e result = HandleCommand_Buy("flashbang");
		overallResult = CombineBuyResults(overallResult, result);
	}

	return overallResult;
}

BuyResult_e CHL2MP_Player::RebuySmokeGrenade()
{
	CBaseCombatWeapon* pGrenade = Weapon_OwnsThisType("weapon_smokegrenade");

	int numGrenades = 0;

	if (pGrenade)
	{
		int nAmmo = pGrenade->GetPrimaryAmmoType();
		if (nAmmo == -1)
		{
			return BUY_ALREADY_HAVE;
		}

		numGrenades = GetAmmoCount(nAmmo);
	}

	BuyResult_e overallResult = BUY_ALREADY_HAVE;
	int numToBuy = MAX(0, m_rebuyStruct.m_smokeGrenade - numGrenades);
	for (int i = 0; i < numToBuy; ++i)
	{
		BuyResult_e result = HandleCommand_Buy("smokegrenade");
		overallResult = CombineBuyResults(overallResult, result);
	}

	return overallResult;
}

BuyResult_e CHL2MP_Player::RebuyDefuser()
{
	//If we don't have a defuser, and we want one, buy it!
	if (!HasDefuser() && m_rebuyStruct.m_defuser)
	{
		return HandleCommand_Buy("defuser");
	}

	return BUY_ALREADY_HAVE;
}

BuyResult_e CHL2MP_Player::RebuyNightVision()
{
	//if we don't have night vision and we want one, buy it!
	if (!m_bHasNightVision && m_rebuyStruct.m_nightVision)
	{
		return HandleCommand_Buy("nvgs");
	}

	return BUY_ALREADY_HAVE;
}

BuyResult_e CHL2MP_Player::RebuyArmor()
{
	if (m_rebuyStruct.m_armor > 0)
	{
		int armor = 0;

		if (m_bHasHelmet)
			armor = 2;
		else if (ArmorValue() > 0)
			armor = 1;

		if (armor < m_rebuyStruct.m_armor)
		{
			if (m_rebuyStruct.m_armor == 1)
			{
				return HandleCommand_Buy("vest");
			}
			else
			{
				return HandleCommand_Buy("vesthelm");
			}
		}

	}

	return BUY_ALREADY_HAVE;
}


void CHL2MP_Player::StockPlayerAmmo(CBaseCombatWeapon* pNewWeapon)
{
	CWeaponHL2MPBase* pWeapon = dynamic_cast<CWeaponHL2MPBase*>(pNewWeapon);

	if (pWeapon)
	{
		if (pWeapon->GetWpnData().iFlags & ITEM_FLAG_EXHAUSTIBLE)
			return;

		int nAmmo = pWeapon->GetPrimaryAmmoType();

		if (nAmmo != -1)
		{
			GiveAmmo(9999, nAmmo, true);
			pWeapon->m_iClip1 = pWeapon->GetMaxClip1();
		}

		return;
	}

	pWeapon = dynamic_cast<CWeaponHL2MPBase*>(Weapon_GetSlot(WEAPON_SLOT_RIFLE));

	if (pWeapon)
	{
		int nAmmo = pWeapon->GetPrimaryAmmoType();

		if (nAmmo != -1)
		{
			GiveAmmo(9999, nAmmo, true);
			pWeapon->m_iClip1 = pWeapon->GetMaxClip1();
		}
	}

	pWeapon = dynamic_cast<CWeaponHL2MPBase*>(Weapon_GetSlot(WEAPON_SLOT_PISTOL));

	if (pWeapon)
	{
		int nAmmo = pWeapon->GetPrimaryAmmoType();

		if (nAmmo != -1)
		{
			GiveAmmo(9999, nAmmo, true);
			pWeapon->m_iClip1 = pWeapon->GetMaxClip1();
		}
	}
}

void CHL2MP_Player::OnCanceledDefuse()
{
	if (m_gooseChaseStep == GC_SHOT_DURING_DEFUSE)
	{
		m_gooseChaseStep = GC_STOPPED_AFTER_GETTING_SHOT;
	}
}


void CHL2MP_Player::OnStartedDefuse()
{
	if (m_defuseDefenseStep == DD_NONE)
	{
		m_defuseDefenseStep = DD_STARTED_DEFUSE;
	}
}

void CHL2MP_Player::SetProgressBarTime(int barTime)
{
	m_iProgressBarDuration = barTime;
	m_flProgressBarStartTime = this->m_flSimulationTime;
}

void CHL2MP_Player::GetIntoGame()
{
	// Set their model and if they're allowed to spawn right now, put them into the world.
	//SetPlayerModel( iClass );

	SetFOV(this, 0);
	m_flLastMovement = gpGlobals->curtime;

	CCSGameRules* MPRules = CSGameRules();

	/*	//MIKETODO: Escape gameplay ?
		if ( ( MPRules->m_bMapHasEscapeZone == true ) && ( m_iTeam == TEAM_CT ) )
		{
			m_iAccount = 0;

			CheckStartMoney();
			AddAccount( (int)startmoney.value, true );
		}
		*/


		//****************New Code by SupraFiend************
	if (!MPRules->FPlayerCanRespawn(this))
	{
		// This player is joining in the middle of a round or is an observer. Put them directly into observer mode.
		//pev->deadflag		= DEAD_RESPAWNABLE;
		//pev->classname		= MAKE_STRING("player");
		//pev->flags		   &= ( FL_PROXY | FL_FAKECLIENT );	// clear flags, but keep proxy and bot flags that might already be set
		//pev->flags		   |= FL_CLIENT | FL_SPECTATOR;
		//SetThink(PlayerDeathThink);
		if (!(m_iDisplayHistoryBits & DHF_SPEC_DUCK))
		{
			m_iDisplayHistoryBits |= DHF_SPEC_DUCK;
			HintMessage("#Spec_Duck", true, true);
		}

		State_Transition(STATE_OBSERVER_MODE);

		m_wasNotKilledNaturally = true;

		MPRules->CheckWinConditions();
	}
	else// else spawn them right in
	{
		State_Transition(STATE_ACTIVE);

		Spawn();

		MPRules->CheckWinConditions();

		//=============================================================================
		// HPE_BEGIN:
		// [menglish] Have the rules update anything related to a player spawning in late
		//=============================================================================

		MPRules->SpawningLatePlayer(this);

		//=============================================================================
		// HPE_END
		//=============================================================================

		if (MPRules->m_flRestartRoundTime == 0.0f)
		{
			//Bomb target, no bomber and no bomb lying around.
			if (MPRules->IsBombDefuseMap() && !MPRules->IsThereABomber() && !MPRules->IsThereABomb())
				MPRules->GiveC4(); //Checks for terrorists.
		}

		// If a new terrorist is entering the fray, then up the # of potential escapers.
		if (GetTeamNumber() == TEAM_TERRORIST)
			MPRules->m_iNumEscapers++;

		//=============================================================================
		// HPE_BEGIN:
		// [menglish] Reset Round Based Achievement Variables
		//=============================================================================

		ResetRoundBasedAchievementVariables();

		//=============================================================================
		// HPE_END
		//=============================================================================

	}
}


BuyResult_e CHL2MP_Player::CombineBuyResults(BuyResult_e prevResult, BuyResult_e newResult)
{
	if (newResult == BUY_BOUGHT)
	{
		prevResult = BUY_BOUGHT;
	}
	else if (prevResult != BUY_BOUGHT &&
		(newResult == BUY_CANT_AFFORD || newResult == BUY_INVALID_ITEM || newResult == BUY_PLAYER_CANT_BUY))
	{
		prevResult = BUY_CANT_AFFORD;
	}

	return prevResult;
}


void CHL2MP_Player::ParseAutoBuyString(const char* string, bool& boughtPrimary, bool& boughtSecondary)
{
	char command[32];
	int nBuffSize = sizeof(command) - 1; // -1 to leave space for the NULL at the end of the string
	const char* c = string;

	if (c == NULL)
	{
		EmitPrivateSound("BuyPreset.AlreadyBought");
		return;
	}

	BuyResult_e overallResult = BUY_ALREADY_HAVE;

	// loop through the string of commands, trying each one in turn.
	while (*c != 0)
	{
		int i = 0;
		// copy the next word into the command buffer.
		while ((*c != 0) && (*c != ' ') && (i < nBuffSize))
		{
			command[i] = *(c);
			++c;
			++i;
		}
		if (*c == ' ')
		{
			++c; // skip the space.
		}

		command[i] = 0; // terminate the string.

		// clear out any spaces.
		i = 0;
		while (command[i] != 0)
		{
			if (command[i] == ' ')
			{
				command[i] = 0;
				break;
			}
			++i;
		}

		// make sure we actually have a command.
		if (strlen(command) == 0)
		{
			continue;
		}

		AutoBuyInfoStruct* commandInfo = GetAutoBuyCommandInfo(command);

		if (ShouldExecuteAutoBuyCommand(commandInfo, boughtPrimary, boughtSecondary))
		{
			BuyResult_e result = HandleCommand_Buy(command);

			overallResult = CombineBuyResults(overallResult, result);

			// check to see if we actually bought a primary or secondary weapon this time.
			PostAutoBuyCommandProcessing(commandInfo, boughtPrimary, boughtSecondary);
		}
	}

	if (overallResult == BUY_CANT_AFFORD)
	{
		EmitPrivateSound("BuyPreset.CantBuy");
	}
	else if (overallResult == BUY_ALREADY_HAVE)
	{
		EmitPrivateSound("BuyPreset.AlreadyBought");
	}
	else if (overallResult == BUY_BOUGHT)
	{
		g_iAutoBuyPurchases++;
	}
}

//==============================================
//PostAutoBuyCommandProcessing
//==============================================
void CHL2MP_Player::PostAutoBuyCommandProcessing(const AutoBuyInfoStruct* commandInfo, bool& boughtPrimary, bool& boughtSecondary)
{
	if (commandInfo == NULL)
	{
		return;
	}

	CBaseCombatWeapon* pPrimary = Weapon_GetSlot(WEAPON_SLOT_RIFLE);
	CBaseCombatWeapon* pSecondary = Weapon_GetSlot(WEAPON_SLOT_PISTOL);

	if ((pPrimary != NULL) && (stricmp(pPrimary->GetClassname(), commandInfo->m_classname) == 0))
	{
		// I just bought the gun I was trying to buy.
		boughtPrimary = true;
	}
	else if ((pPrimary == NULL) && ((commandInfo->m_class & AUTOBUYCLASS_SHIELD) == AUTOBUYCLASS_SHIELD) && HasShield())
	{
		// the shield is a primary weapon even though it isn't a "real" weapon.
		boughtPrimary = true;
	}
	else if ((pSecondary != NULL) && (stricmp(pSecondary->GetClassname(), commandInfo->m_classname) == 0))
	{
		// I just bought the pistol I was trying to buy.
		boughtSecondary = true;
	}
}

bool CHL2MP_Player::ShouldExecuteAutoBuyCommand(const AutoBuyInfoStruct* commandInfo, bool boughtPrimary, bool boughtSecondary)
{
	if (commandInfo == NULL)
	{
		return false;
	}

	if ((boughtPrimary) && ((commandInfo->m_class & AUTOBUYCLASS_PRIMARY) != 0) && ((commandInfo->m_class & AUTOBUYCLASS_AMMO) == 0))
	{
		// this is a primary weapon and we already have one.
		return false;
	}

	if ((boughtSecondary) && ((commandInfo->m_class & AUTOBUYCLASS_SECONDARY) != 0) && ((commandInfo->m_class & AUTOBUYCLASS_AMMO) == 0))
	{
		// this is a secondary weapon and we already have one.
		return false;
	}

	if (commandInfo->m_class & AUTOBUYCLASS_ARMOR && ArmorValue() >= 100)
	{
		return false;
	}

	return true;
}

AutoBuyInfoStruct* CHL2MP_Player::GetAutoBuyCommandInfo(const char* command)
{
	int i = 0;
	AutoBuyInfoStruct* ret = NULL;
	AutoBuyInfoStruct* temp = &(g_autoBuyInfo[i]);

	// loop through all the commands till we find the one that matches.
	while ((ret == NULL) && (temp->m_class != (AutoBuyClassType)0))
	{
		temp = &(g_autoBuyInfo[i]);
		++i;

		if (stricmp(temp->m_command, command) == 0)
		{
			ret = temp;
		}
	}

	return ret;
}

//==============================================
//PostAutoBuyCommandProcessing
//- reorders the tokens in autobuyString based on the order of tokens in the priorityString.
//==============================================
void CHL2MP_Player::PrioritizeAutoBuyString(char* autobuyString, const char* priorityString)
{
	char newString[256];
	int newStringPos = 0;
	char priorityToken[32];

	if ((priorityString == NULL) || (autobuyString == NULL))
	{
		return;
	}

	const char* priorityChar = priorityString;

	while (*priorityChar != 0)
	{
		int i = 0;

		// get the next token from the priority string.
		while ((*priorityChar != 0) && (*priorityChar != ' '))
		{
			priorityToken[i] = *priorityChar;
			++i;
			++priorityChar;
		}
		priorityToken[i] = 0;

		// skip spaces
		while (*priorityChar == ' ')
		{
			++priorityChar;
		}

		if (strlen(priorityToken) == 0)
		{
			continue;
		}

		// see if the priority token is in the autobuy string.
		// if  it is, copy that token to the new string and blank out
		// that token in the autobuy string.
		char* autoBuyPosition = strstr(autobuyString, priorityToken);
		if (autoBuyPosition != NULL)
		{
			while ((*autoBuyPosition != 0) && (*autoBuyPosition != ' '))
			{
				newString[newStringPos] = *autoBuyPosition;
				*autoBuyPosition = ' ';
				++newStringPos;
				++autoBuyPosition;
			}

			newString[newStringPos++] = ' ';
		}
	}

	// now just copy anything left in the autobuyString to the new string in the order it's in already.
	char* autobuyPosition = autobuyString;
	while (*autobuyPosition != 0)
	{
		// skip spaces
		while (*autobuyPosition == ' ')
		{
			++autobuyPosition;
		}

		// copy the token over to the new string.
		while ((*autobuyPosition != 0) && (*autobuyPosition != ' '))
		{
			newString[newStringPos] = *autobuyPosition;
			++newStringPos;
			++autobuyPosition;
		}

		// add a space at the end.
		newString[newStringPos++] = ' ';
	}

	// terminate the string.  Trailing spaces shouldn't matter.
	newString[newStringPos] = 0;

	Q_snprintf(autobuyString, sizeof(autobuyString), "%s", newString);
}

void CHL2MP_Player::AddAccount(int amount, bool bTrackChange, bool bItemBought, const char* pItemName)
{
	m_iAccount += amount;

	//=============================================================================
	// HPE_BEGIN:
	// [menglish] Description of reason for change
	//=============================================================================

	if (amount > 0)
	{
		CCS_GameStats.Event_MoneyEarned(this, amount);
	}
	else if (amount < 0 && bItemBought)
	{
		CCS_GameStats.Event_MoneySpent(this, ABS(amount), pItemName);
	}

	//=============================================================================
	// HPE_END
	//=============================================================================

	if (m_iAccount < 0)
		m_iAccount = 0;
	else if (m_iAccount > 16000)
		m_iAccount = 16000;
}
void CHL2MP_Player::HintMessage(const char* pMessage, bool bDisplayIfDead, bool bOverrideClientSettings)
{
	if ((!bDisplayIfDead && !IsAlive()) || !IsNetClient() || !m_pHintMessageQueue)
		return;

	if (bOverrideClientSettings || m_bShowHints)
		m_pHintMessageQueue->AddMessage(pMessage);
}
void CHL2MP_Player::DropC4()
{
}


bool CHL2MP_Player::HasDefuser()
{
	return m_bHasDefuser;
}

void CHL2MP_Player::RemoveDefuser()
{
	m_bHasDefuser = false;
}

void CHL2MP_Player::GiveDefuser(bool bPickedUp /* = false */)
{
	if (!m_bHasDefuser)
	{
		IGameEvent* event = gameeventmanager->CreateEvent("item_pickup");
		if (event)
		{
			event->SetInt("userid", GetUserID());
			event->SetString("item", "defuser");
			gameeventmanager->FireEvent(event);
		}
	}

	m_bHasDefuser = true;

	//=============================================================================
	// HPE_BEGIN:
	// [dwenger] Added for fun-fact support
	//=============================================================================

	m_bPickedUpDefuser = bPickedUp;

	//=============================================================================
	// HPE_END
	//=============================================================================
}
bool CHL2MP_Player::HasShield() const
{
#ifdef CS_SHIELD_ENABLED
	return m_bHasShield;
#else
	return false;
#endif
}
void CHL2MP_Player::Blind(float holdTime, float fadeTime, float startingAlpha)
{
	// Don't flash a spectator.
	color32 clr = { 255, 255, 255, 255 };

	clr.a = startingAlpha;

	// estimate when we can see again
	float oldBlindUntilTime = m_blindUntilTime;
	float oldBlindStartTime = m_blindStartTime;
	m_blindUntilTime = MAX(m_blindUntilTime, gpGlobals->curtime + holdTime + 0.5f * fadeTime);
	m_blindStartTime = gpGlobals->curtime;

	// Spectators get a lessened flash.
	if ((GetObserverMode() != OBS_MODE_NONE) && (GetObserverMode() != OBS_MODE_IN_EYE))
	{
		if (!mp_fadetoblack.GetBool())
		{
			clr.a = 150;

			fadeTime = MIN(fadeTime, 0.5f); // make sure the spectator flashbang time is 1/2 second or less.
			holdTime = MIN(holdTime, fadeTime * 0.5f); // adjust the hold time to match the fade time.
			UTIL_ScreenFade(this, clr, fadeTime, holdTime, FFADE_IN);
		}
	}
	else
	{
		fadeTime /= 1.4;

		if (gpGlobals->curtime > oldBlindUntilTime)
		{
			// The previous flashbang is wearing off, or completely gone
			m_flFlashDuration = fadeTime;
			m_flFlashMaxAlpha = startingAlpha;
		}
		else
		{
			// The previous flashbang is still going strong - only extend the duration
			float remainingDuration = oldBlindStartTime + m_flFlashDuration - gpGlobals->curtime;

			m_flFlashDuration = MAX(remainingDuration, fadeTime);
			m_flFlashMaxAlpha = MAX(m_flFlashMaxAlpha, startingAlpha);
		}

		// allow bots to react
		IGameEvent* event = gameeventmanager->CreateEvent("player_blind");
		if (event)
		{
			event->SetInt("userid", GetUserID());
			gameeventmanager->FireEvent(event);
		}
	}
}
void CHL2MP_Player::Radio(const char* pszRadioSound, const char* pszRadioText)
{
	if (!IsAlive())
		return;

	if (IsObserver())
		return;

	CRecipientFilter filter;
	ConstructRadioFilter(filter);

	if (pszRadioText)
	{
		const char* pszLocationText = CSGameRules()->GetChatLocation(true, this);
		if (pszLocationText && *pszLocationText)
		{
			UTIL_CSRadioMessage(filter, entindex(), HUD_PRINTTALK, "#Game_radio_location", GetPlayerName(), pszLocationText, pszRadioText);
		}
		else
		{
			UTIL_CSRadioMessage(filter, entindex(), HUD_PRINTTALK, "#Game_radio", GetPlayerName(), pszRadioText);
		}
	}

	UserMessageBegin(filter, "SendAudio");
	WRITE_STRING(pszRadioSound);
	MessageEnd();

	//icon over the head for teammates
	TE_RadioIcon(filter, 0.0, this);
}
void CHL2MP_Player::EmitPrivateSound(const char* soundName)
{
	CSoundParameters params;
	if (!GetParametersForSound(soundName, params, NULL))
		return;

	CSingleUserRecipientFilter filter(this);
	EmitSound(filter, entindex(), soundName);
}
bool CHL2MP_Player::IsVIP() const
{
	return m_isVIP;
}
void CHL2MP_Player::ResetRoundBasedAchievementVariables()
{
	m_KillingSpreeStartTime = -1;

	int numCTPlayers = 0, numTPlayers = 0;
	for (int i = 0; i < g_Teams.Count(); i++)
	{
		if (g_Teams[i])
		{
			if (g_Teams[i]->GetTeamNumber() == TEAM_CT)
				numCTPlayers = g_Teams[i]->GetNumPlayers();
			else if (g_Teams[i]->GetTeamNumber() == TEAM_TERRORIST)
				numTPlayers = g_Teams[i]->GetNumPlayers();
		}
	}
	m_NumEnemiesKilledThisRound = 0;
	if (GetTeamNumber() == TEAM_CT)
		m_NumEnemiesAtRoundStart = numTPlayers;
	else if (GetTeamNumber() == TEAM_TERRORIST)
		m_NumEnemiesAtRoundStart = numCTPlayers;


	//Clear the previous owner field for currently held weapons
	CWeaponHL2MPBase* pWeapon = dynamic_cast<CWeaponHL2MPBase*>(Weapon_GetSlot(WEAPON_SLOT_RIFLE));
	if (pWeapon)
	{
		pWeapon->SetPreviousOwner(NULL);
	}
	pWeapon = dynamic_cast<CWeaponHL2MPBase*>(Weapon_GetSlot(WEAPON_SLOT_PISTOL));
	if (pWeapon)
	{
		pWeapon->SetPreviousOwner(NULL);
	}

	//Clear list of weapons used to get kills
	m_killWeapons.RemoveAll();

	//Clear sliding window of kill times
	m_killTimes.RemoveAll();

	//clear round kills
	m_enemyPlayersKilledThisRound.RemoveAll();

	m_killsWhileBlind = 0;

	m_bSurvivedHeadshotDueToHelmet = false;

	m_gooseChaseStep = GC_NONE;
	m_defuseDefenseStep = DD_NONE;
	m_pGooseChaseDistractingPlayer = NULL;

	m_bMadeFootstepNoise = false;

	m_bombPickupTime = -1;

	m_bMadePurchseThisRound = false;

	m_bKilledDefuser = false;
	m_bKilledRescuer = false;
	m_maxGrenadeKills = 0;
	m_grenadeDamageTakenThisRound = 0;

	//=============================================================================
	// HPE_BEGIN:
	// [dwenger] Needed for fun-fact implementation
	//=============================================================================

	WieldingKnifeAndKilledByGun(false);

	m_WeaponTypesUsed.RemoveAll();

	m_bPickedUpDefuser = false;
	m_bDefusedWithPickedUpKit = false;

	//=============================================================================
	// HPE_END
	//=============================================================================
}
int CHL2MP_Player::GetNumEnemyDamagers()
{
	int numberOfEnemyDamagers = 0;
	FOR_EACH_LL(m_DamageTakenList, i)
	{
		for (int j = 1; j <= MAX_PLAYERS; j++)
		{
			CBasePlayer* pPlayer = UTIL_PlayerByIndex(j);

			if (pPlayer && V_strncmp(pPlayer->GetPlayerName(), m_DamageTakenList[i]->GetPlayerName(), MAX_PLAYER_NAME_LENGTH) == 0 &&
				pPlayer->GetTeamNumber() != GetTeamNumber())
			{
				numberOfEnemyDamagers++;
			}
		}
	}
	return numberOfEnemyDamagers;
}
int CHL2MP_Player::GetPercentageOfEnemyTeamKilled()
{
	if (m_NumEnemiesAtRoundStart > 0)
	{
		return (int)(((float)m_NumEnemiesKilledThisRound / (float)m_NumEnemiesAtRoundStart) * 100.0f);
	}

	return 0;
}
void CHL2MP_Player::PlayerUsedFirearm(CBaseCombatWeapon* pBaseWeapon)
{
	if (pBaseWeapon)
	{
		CWeaponHL2MPBase* pWeapon = dynamic_cast<CWeaponHL2MPBase*>(pBaseWeapon);

		if (pWeapon)
		{
			CSWeaponType weaponType = pWeapon->GetCSWpnData().m_WeaponType;
			CSWeaponID weaponID = pWeapon->GetWeaponID();

			if (weaponType != WEAPONTYPE_KNIFE && weaponType != WEAPONTYPE_C4 && weaponType != WEAPONTYPE_GRENADE)
			{
				if (m_WeaponTypesUsed.Find(weaponID) == -1)
				{
					// Add this weapon to the list of weapons used by the player
					m_WeaponTypesUsed.AddToTail(weaponID);
				}
			}
		}
	}
}
bool CHL2MP_Player::IsShieldDrawn() const
{
#ifdef CS_SHIELD_ENABLED
	return m_bShieldDrawn;
#else
	return false;
#endif
}
CHL2MP_Player* CHL2MP_Player::Instance(int iEnt)
{
	return dynamic_cast<CHL2MP_Player*>(CBaseEntity::Instance(INDEXENT(iEnt)));
}
void CHL2MP_Player::ObserverRoundRespawn()
{
	ClearFlashbangScreenFade();

	// did we change our name last round?
	if (m_szNewName[0] != 0)
	{
		// ... and force the name change now.  After this happens, the gamerules will get
		// a ClientSettingsChanged callback from the above ClientCommand, but the name
		// matches what we're setting here, so it will do nothing.
		ChangeName(m_szNewName);
		m_szNewName[0] = 0;
	}
}

void CHL2MP_Player::RoundRespawn()
{
	//MIKETODO: menus
	//if ( m_iMenu != Menu_ChooseAppearance )
	{
		// Put them back into the game.
		StopObserverMode();
		State_Transition(STATE_ACTIVE);
		respawn(this, false);
		m_nButtons = 0;
		SetNextThink(TICK_NEVER_THINK);
	}

	m_receivesMoneyNextRound = true; // reset this variable so they can receive their cash next round.

	//If they didn't die, this will print out their damage info
	OutputDamageGiven();
	OutputDamageTaken();
	ResetDamageCounters();
}

void CHL2MP_Player::CheckTKPunishment(void)
{
	// teamkill punishment..
	if ((m_bJustKilledTeammate == true) && mp_tkpunish.GetInt())
	{
		m_bJustKilledTeammate = false;
		m_bPunishedForTK = true;
		CommitSuicide();
	}
}
bool CHL2MP_Player::CanChangeName(void)
{
	if (IsBot())
		return true;

	// enforce the minimum interval
	if ((m_flNameChangeHistory[0] + MIN_NAME_CHANGE_INTERVAL) >= gpGlobals->curtime)
	{
		return false;
	}

	// enforce that we dont do more than NAME_CHANGE_HISTORY_SIZE
	// changes within NAME_CHANGE_HISTORY_INTERVAL
	if ((m_flNameChangeHistory[NAME_CHANGE_HISTORY_SIZE - 1] + NAME_CHANGE_HISTORY_INTERVAL) >= gpGlobals->curtime)
	{
		return false;
	}

	return true;
}

void CHL2MP_Player::ChangeName(const char* pszNewName)
{
	// make sure name is not too long
	char trimmedName[MAX_PLAYER_NAME_LENGTH];
	Q_strncpy(trimmedName, pszNewName, sizeof(trimmedName));

	const char* pszOldName = GetPlayerName();

	// send colored message to everyone
	CReliableBroadcastRecipientFilter filter;
	UTIL_SayText2Filter(filter, this, false, "#Cstrike_Name_Change", pszOldName, trimmedName);

	// broadcast event
	IGameEvent* event = gameeventmanager->CreateEvent("player_changename");
	if (event)
	{
		event->SetInt("userid", GetUserID());
		event->SetString("oldname", pszOldName);
		event->SetString("newname", trimmedName);
		gameeventmanager->FireEvent(event);
	}

	// change shared player name
	SetPlayerName(trimmedName);

	// tell engine to use new name
	engine->ClientCommand(edict(), "name \"%s\"", trimmedName);

	// remember time of name change
	for (int i = NAME_CHANGE_HISTORY_SIZE - 1; i > 0; i--)
	{
		m_flNameChangeHistory[i] = m_flNameChangeHistory[i - 1];
	}

	m_flNameChangeHistory[0] = gpGlobals->curtime; // last change
}
void CHL2MP_Player::SwitchTeam(int iTeamNum)
{
	if (!GetGlobalTeam(iTeamNum) || (iTeamNum != TEAM_CT && iTeamNum != TEAM_TERRORIST))
	{
		Warning("CHL2MP_Player::SwitchTeam( %d ) - invalid team index.\n", iTeamNum);
		return;
	}

	int iOldTeam = GetTeamNumber();

	// if this is our current team, just abort
	if (iTeamNum == iOldTeam)
		return;

	// Always allow a change to spectator, and don't count it as one of our team changes.
	// We now store the old team, so if a player changes once to one team, then to spectator,
	// they won't be able to change back to their old old team, but will still be able to join
	// the team they initially changed to.
	m_bTeamChanged = true;

	// do the team change:
	BaseClass::ChangeTeam(iTeamNum);

	if (HasDefuser())
	{
		RemoveDefuser();
	}

	//reset class
	switch (m_iClass)
	{
		// Terrorist -> CT
	case CS_CLASS_PHOENIX_CONNNECTION:
		m_iClass = (int)CS_CLASS_SEAL_TEAM_6;
		break;
	case CS_CLASS_L337_KREW:
		m_iClass = (int)CS_CLASS_GSG_9;
		break;
	case CS_CLASS_ARCTIC_AVENGERS:
		m_iClass = (int)CS_CLASS_SAS;
		break;
	case CS_CLASS_GUERILLA_WARFARE:
		m_iClass = (int)CS_CLASS_GIGN;
		break;

		// CT -> Terrorist
	case CS_CLASS_SEAL_TEAM_6:
		m_iClass = (int)CS_CLASS_PHOENIX_CONNNECTION;
		break;
	case CS_CLASS_GSG_9:
		m_iClass = (int)CS_CLASS_L337_KREW;
		break;
	case CS_CLASS_SAS:
		m_iClass = (int)CS_CLASS_ARCTIC_AVENGERS;
		break;
	case CS_CLASS_GIGN:
		m_iClass = (int)CS_CLASS_GUERILLA_WARFARE;
		break;

	case CS_CLASS_NONE:
	default:
		break;
	}

	// Initialize the player counts now that a player has switched teams
	int NumDeadCT, NumDeadTerrorist, NumAliveTerrorist, NumAliveCT;
	CSGameRules()->InitializePlayerCounts(NumAliveTerrorist, NumAliveCT, NumDeadTerrorist, NumDeadCT);
}

void CHL2MP_Player::ClearFlashbangScreenFade(void)
{
	if (IsBlind())
	{
		color32 clr = { 0, 0, 0, 0 };
		UTIL_ScreenFade(this, clr, 0.01, 0.0, FFADE_OUT | FFADE_PURGE);

		m_flFlashDuration = 0.0f;
		m_flFlashMaxAlpha = 255.0f;
	}

	// clear blind time (after screen fades are canceled)
	m_blindUntilTime = 0.0f;
	m_blindStartTime = 0.0f;
}
void CHL2MP_Player::ResetDamageCounters()
{
	m_DamageGivenList.PurgeAndDeleteElements();
	m_DamageTakenList.PurgeAndDeleteElements();
}
//=======================================================
// Output the damage that we dealt to other players
//=======================================================
void CHL2MP_Player::OutputDamageTaken(void)
{
	bool bPrintHeader = true;
	CDamageRecord* pRecord;
	char buf[64];
	int msg_dest = HUD_PRINTCONSOLE;

	FOR_EACH_LL(m_DamageTakenList, i)
	{
		if (bPrintHeader)
		{
			ClientPrint(this, msg_dest, "Player: %s1 - Damage Taken\n", GetPlayerName());
			ClientPrint(this, msg_dest, "-------------------------\n");
			bPrintHeader = false;
		}
		pRecord = m_DamageTakenList[i];

		if (pRecord)
		{
			if (pRecord->GetNumHits() == 1)
			{
				Q_snprintf(buf, sizeof(buf), "%d in %d hit", pRecord->GetDamage(), pRecord->GetNumHits());
			}
			else
			{
				Q_snprintf(buf, sizeof(buf), "%d in %d hits", pRecord->GetDamage(), pRecord->GetNumHits());
			}
			ClientPrint(this, msg_dest, "Damage Taken from \"%s1\" - %s2\n", pRecord->GetPlayerName(), buf);
		}
	}
}

//=======================================================
// Output the damage that we took from other players
//=======================================================
void CHL2MP_Player::OutputDamageGiven(void)
{
	bool bPrintHeader = true;
	CDamageRecord* pRecord;
	char buf[64];
	int msg_dest = HUD_PRINTCONSOLE;

	FOR_EACH_LL(m_DamageGivenList, i)
	{
		if (bPrintHeader)
		{
			ClientPrint(this, msg_dest, "Player: %s1 - Damage Given\n", GetPlayerName());
			ClientPrint(this, msg_dest, "-------------------------\n");
			bPrintHeader = false;
		}

		pRecord = m_DamageGivenList[i];

		if (pRecord)
		{
			if (pRecord->GetNumHits() == 1)
			{
				Q_snprintf(buf, sizeof(buf), "%d in %d hit", pRecord->GetDamage(), pRecord->GetNumHits());
			}
			else
			{
				Q_snprintf(buf, sizeof(buf), "%d in %d hits", pRecord->GetDamage(), pRecord->GetNumHits());
			}
			ClientPrint(this, msg_dest, "Damage Given to \"%s1\" - %s2\n", pRecord->GetPlayerName(), buf);
		}
	}
}
void CHL2MP_Player::MarkAsNotReceivingMoneyNextRound()
{
	m_receivesMoneyNextRound = false;
}

bool CHL2MP_Player::DoesPlayerGetRoundStartMoney()
{
	return m_receivesMoneyNextRound;
}
void CHL2MP_Player::CheckMaxGrenadeKills(int grenadeKills)
{
	if (grenadeKills > m_maxGrenadeKills)
	{
		m_maxGrenadeKills = grenadeKills;
	}
}
void CHL2MP_Player::SetClanTag(const char* pTag)
{
	if (pTag)
	{
		Q_strncpy(m_szClanTag, pTag, sizeof(m_szClanTag));
	}
}

void CHL2MP_Player::OnRoundEnd(int winningTeam, int reason)
{
	if (winningTeam == WINNER_CT || winningTeam == WINNER_TER)
	{
		int losingTeamId = (winningTeam == TEAM_CT) ? TEAM_TERRORIST : TEAM_CT;

		CTeam* losingTeam = GetGlobalTeam(losingTeamId);

		int losingTeamPlayers = 0;

		if (losingTeam)
		{
			losingTeamPlayers = losingTeam->GetNumPlayers();

			int ignoreCount = 0;
			for (int i = 1; i <= gpGlobals->maxClients; i++)
			{
				CHL2MP_Player* pPlayer = (CHL2MP_Player*)UTIL_PlayerByIndex(i);
				if (pPlayer)
				{
					int teamNum = pPlayer->GetTeamNumber();
					if (teamNum == losingTeamId)
					{
						if (pPlayer->WasNotKilledNaturally())
						{
							ignoreCount++;
						}
					}

				}
			}

			losingTeamPlayers -= ignoreCount;
		}

		//Check fast round win achievement
		if (IsAlive() &&
			gpGlobals->curtime - CSGameRules()->GetRoundStartTime() < AchievementConsts::FastRoundWin_Time &&
			GetTeamNumber() == winningTeam &&
			losingTeamPlayers >= AchievementConsts::DefaultMinOpponentsForAchievement)
		{
			AwardAchievement(CSFastRoundWin);
		}

		//Check goosechase achievement
		if (IsAlive() && reason == Target_Bombed && m_gooseChaseStep == GC_STOPPED_AFTER_GETTING_SHOT && m_pGooseChaseDistractingPlayer)
		{
			m_pGooseChaseDistractingPlayer->AwardAchievement(CSGooseChase);
		}

		//Check Defuse Defense achievement
		if (IsAlive() && reason == Bomb_Defused && m_defuseDefenseStep == DD_KILLED_TERRORIST)
		{
			AwardAchievement(CSDefuseDefense);
		}

		//Check silent win
		if (m_NumEnemiesKilledThisRound > 0 && GetTeamNumber() == winningTeam && !m_bMadeFootstepNoise)
		{
			AwardAchievement(CSSilentWin);
		}

		//Process && Check "win rounds without buying" achievement
		if (GetTeamNumber() == winningTeam && !m_bMadePurchseThisRound)
		{
			m_roundsWonWithoutPurchase++;
			if (m_roundsWonWithoutPurchase > AchievementConsts::WinRoundsWithoutBuying_Rounds)
			{
				AwardAchievement(CSWinRoundsWithoutBuying);
			}
		}
		else
		{
			m_roundsWonWithoutPurchase = 0;
		}
	}

	m_lastRoundResult = reason;
}

void CHL2MP_Player::OnPreResetRound()
{
	//Check headshot survival achievement
	if (IsAlive() && m_bSurvivedHeadshotDueToHelmet)
	{
		AwardAchievement(CSSurvivedHeadshotDueToHelmet);
	}

	if (IsAlive() && m_grenadeDamageTakenThisRound > AchievementConsts::SurviveGrenade_MinDamage)
	{
		AwardAchievement(CSSurviveGrenade);
	}


	//Check achievement for surviving attacks from multiple players.
	if (IsAlive())
	{
		int numberOfEnemyDamagers = GetNumEnemyDamagers();

		if (numberOfEnemyDamagers >= AchievementConsts::SurviveManyAttacks_NumberDamagingPlayers)
		{
			AwardAchievement(CSSurviveManyAttacks);
		}
	}
}
void CHL2MP_Player::Deafen(float flDistance)
{
	// Spectators don't get deafened
	if ((GetObserverMode() == OBS_MODE_NONE) || (GetObserverMode() == OBS_MODE_IN_EYE))
	{
		// dsp presets are defined in hl2/scripts/dsp_presets.txt

		int effect;

		if (flDistance < 600)
		{
			effect = 134;
		}
		else if (flDistance < 800)
		{
			effect = 135;
		}
		else if (flDistance < 1000)
		{
			effect = 136;
		}
		else
		{
			// too far for us to get an effect
			return;
		}

		CSingleUserRecipientFilter user(this);
		enginesound->SetPlayerDSP(user, effect, false);

		//TODO: bots can't hear sound for a while?
	}
}

int CHL2MP_Player::GetNumEnemiesDamaged()
{
	int numberOfEnemiesDamaged = 0;
	FOR_EACH_LL(m_DamageGivenList, i)
	{
		for (int j = 1; j <= MAX_PLAYERS; j++)
		{
			CBasePlayer* pPlayer = UTIL_PlayerByIndex(j);

			if (pPlayer && V_strncmp(pPlayer->GetPlayerName(), m_DamageGivenList[i]->GetPlayerName(), MAX_PLAYER_NAME_LENGTH) == 0 &&
				pPlayer->GetTeamNumber() != GetTeamNumber())
			{
				numberOfEnemiesDamaged++;
			}
		}
	}
	return numberOfEnemiesDamaged;
}


const char* RadioEventName[RADIO_NUM_EVENTS + 1] =
{
	"RADIO_INVALID",

	"EVENT_START_RADIO_1",

	"EVENT_RADIO_COVER_ME",
	"EVENT_RADIO_YOU_TAKE_THE_POINT",
	"EVENT_RADIO_HOLD_THIS_POSITION",
	"EVENT_RADIO_REGROUP_TEAM",
	"EVENT_RADIO_FOLLOW_ME",
	"EVENT_RADIO_TAKING_FIRE",

	"EVENT_START_RADIO_2",

	"EVENT_RADIO_GO_GO_GO",
	"EVENT_RADIO_TEAM_FALL_BACK",
	"EVENT_RADIO_STICK_TOGETHER_TEAM",
	"EVENT_RADIO_GET_IN_POSITION_AND_WAIT",
	"EVENT_RADIO_STORM_THE_FRONT",
	"EVENT_RADIO_REPORT_IN_TEAM",

	"EVENT_START_RADIO_3",

	"EVENT_RADIO_AFFIRMATIVE",
	"EVENT_RADIO_ENEMY_SPOTTED",
	"EVENT_RADIO_NEED_BACKUP",
	"EVENT_RADIO_SECTOR_CLEAR",
	"EVENT_RADIO_IN_POSITION",
	"EVENT_RADIO_REPORTING_IN",
	"EVENT_RADIO_GET_OUT_OF_THERE",
	"EVENT_RADIO_NEGATIVE",
	"EVENT_RADIO_ENEMY_DOWN",

	"EVENT_RADIO_END",

	NULL		// must be NULL-terminated
};


RadioType NameToRadioEvent(const char* name)
{
	for (int i = 0; RadioEventName[i]; ++i)
		if (!stricmp(RadioEventName[i], name))
			return static_cast<RadioType>(i);

	return RADIO_INVALID;
}
























