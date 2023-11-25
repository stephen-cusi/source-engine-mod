//========= Copyright © 1996-2005, Valve Corporation, All rights reserved. ============//
//
// Purpose: NH2 Guard Zombie
//
// $NoKeywords: $
//=============================================================================//

#include "cbase.h"
#include "ai_basenpc.h"
#include "ai_default.h"
#include "ai_schedule.h"
#include "ai_hull.h"
#include "ai_motor.h"
#include "ai_memory.h"
#include "ai_route.h"
#include "ai_squad.h"
#include "soundent.h"
#include "game.h"
#include "npcevent.h"
#include "entitylist.h"
#include "ai_task.h"
#include "activitylist.h"
#include "engine/IEngineSound.h"
#include "npc_BaseZombie.h"
#include "movevars_shared.h"
#include "IEffects.h"
#include "props.h"
#include "physics_npc_solver.h"
#include "hl2_player.h"
#include "hl2_gamerules.h"

#include "basecombatweapon.h"
#include "basegrenade_shared.h"
#include "grenade_frag.h"

#include "ai_interactions.h"

// memdbgon must be the last include file in a .cpp file!!!
#include "tier0/memdbgon.h"

enum
{	
	SQUAD_SLOT_NH_GUARD_SPRINT1 = LAST_SHARED_SQUADSLOT,
	SQUAD_SLOT_NH_GUARD_SPRINT2,
};

#define MIN_SPRINT_TIME 3.5f
#define MAX_SPRINT_TIME 5.5f

#define MIN_SPRINT_DISTANCE 64.0f
#define MAX_SPRINT_DISTANCE 1024.0f

#define SPRINT_CHANCE_VALUE 10
#define SPRINT_CHANCE_VALUE_DARKNESS 50

#define GRENADE_PULL_MAX_DISTANCE 256.0f

#define NH_GUARD_MAX_GRENADES 1

int ACT_NH_GUARD_GRENADE_PULL;
int ACT_NH_GUARD_GRENADE_WALK;
int ACT_NH_GUARD_GRENADE_RUN;
int ACT_NH_GUARD_GRENADE_IDLE;
int ACT_NH_GUARD_ATTACK_FAST;
int ACT_NH_GUARD_GRENADE_FLINCH_BACK;
int ACT_NH_GUARD_GRENADE_FLINCH_FRONT;
int ACT_NH_GUARD_GRENADE_FLINCH_WEST;
int ACT_NH_GUARD_GRENADE_FLINCH_EAST;

int AE_NH_GUARD_PULLPIN;

extern bool IsAlyxInDarknessMode();

ConVar	sk_nh_guard_health( "sk_nh_guard_health","0");

float g_flNH_GuardGrenadeTimes = 0;

class CNPC_NH_Guard : public CAI_BlendingHost<CNPC_BaseZombie>, public CDefaultPlayerPickupVPhysics
{
	DECLARE_DATADESC();
	DECLARE_CLASS( CNPC_NH_Guard, CAI_BlendingHost<CNPC_BaseZombie> );

public:

	void Spawn( void );
	void Precache( void );

	void SetZombieModel( void );

	Vector HeadTarget( const Vector &posSrc );

	virtual void PrescheduleThink( void );
	virtual int SelectSchedule( void );
	virtual void BuildScheduleTestBits( void );

	virtual void HandleAnimEvent( animevent_t *pEvent );

	virtual const char *GetLegsModel( void );
	virtual const char *GetTorsoModel( void );
	virtual const char *GetHeadcrabClassname( void );
	virtual const char *GetHeadcrabModel( void );

	virtual void ReleaseHeadcrab( const Vector &vecOrigin, const Vector &vecVelocity, bool fRemoveHead, bool fRagdollBody, bool fRagdollCrab = false ) {};
	
	virtual void PainSound( const CTakeDamageInfo &info );
	virtual void DeathSound( const CTakeDamageInfo &info );
	virtual void AlertSound( void );
	virtual void IdleSound( void );
	virtual void AttackSound( void );
	virtual void AttackHitSound( void );
	virtual void AttackMissSound( void );
	virtual void FootstepSound( bool fRightFoot );
	virtual void FootscuffSound( bool fRightFoot );
	virtual void MoanSound( envelopePoint_t *pEnvelope, int iEnvelopeSize );

	virtual void Event_Killed( const CTakeDamageInfo &info );
	virtual void TraceAttack( const CTakeDamageInfo &info, const Vector &vecDir, trace_t *ptr, CDmgAccumulator *pAccumulator );
	virtual void RunTask( const Task_t *pTask );
	virtual int  MeleeAttack1Conditions ( float flDot, float flDist );

	virtual bool ShouldBecomeTorso( const CTakeDamageInfo &info, float flDamageThreshold );

	virtual void OnScheduleChange ( void );
	virtual bool CanRunAScriptedNPCInteraction( bool bForced );

	void GatherGrenadeConditions( void );

	virtual Activity NPC_TranslateActivity( Activity baseAct );

	const char *GetMoanSound( int nSound );

	bool AllowedToSprint( void );
	void Sprint( bool bMadSprint = false );
	void StopSprint( void );

	void DropGrenade( Vector vDir );

	bool IsSprinting( void ) { return m_flSprintTime > gpGlobals->curtime;	}
	bool HasGrenade( void ) { return m_hGrenade != NULL; }

	int TranslateSchedule( int scheduleType );

	void InputStartSprint ( inputdata_t &inputdata );
	void InputPullGrenade ( inputdata_t &inputdata );
	int OnTakeDamage_Alive( const CTakeDamageInfo &inputInfo );

	virtual CBaseEntity *OnFailedPhysGunPickup ( Vector vPhysgunPos );

	//Called when we want to let go of a grenade and let the physcannon pick it up.
	void ReleaseGrenade( Vector vPhysgunPos );

	virtual bool HandleInteraction( int interactionType, void *data, CBaseCombatCharacter *sourceEnt );

	enum
	{
		COND_NH_GUARD_GRENADE = LAST_BASE_ZOMBIE_CONDITION,
	};

	enum
	{
		SCHED_NH_GUARD_PULL_GRENADE = LAST_BASE_ZOMBIE_SCHEDULE,
	};

public:
	DEFINE_CUSTOM_AI;

private:

	float	m_flSprintTime;
	float	m_flSprintRestTime;

	float	m_flSuperFastAttackTime;
	float   m_flGrenadePullTime;
	
	int		m_iGrenadeCount;

	EHANDLE	m_hGrenade;

protected:
	static const char *pMoanSounds[];

};

LINK_ENTITY_TO_CLASS( npc_nh_guard, CNPC_NH_Guard );

BEGIN_DATADESC( CNPC_NH_Guard )
	DEFINE_FIELD( m_flSprintTime, FIELD_TIME ),
	DEFINE_FIELD( m_flSprintRestTime, FIELD_TIME ),
	DEFINE_FIELD( m_flSuperFastAttackTime, FIELD_TIME ),
	DEFINE_FIELD( m_hGrenade, FIELD_EHANDLE ),
	DEFINE_FIELD( m_flGrenadePullTime, FIELD_TIME ),
	DEFINE_FIELD( m_iGrenadeCount, FIELD_INTEGER ),
	DEFINE_INPUTFUNC( FIELD_VOID,	"StartSprint", InputStartSprint ),
	DEFINE_INPUTFUNC( FIELD_VOID,	"PullGrenade", InputPullGrenade ),
END_DATADESC()

//---------------------------------------------------------
//---------------------------------------------------------
const char *CNPC_NH_Guard::pMoanSounds[] =
{
	"ATV_engine_null",
};

void CNPC_NH_Guard::Spawn( void )
{
	Precache();

	m_fIsTorso = false;
	m_fIsHeadless = true;
	
	SetBloodColor( BLOOD_COLOR_RED );

	m_iHealth			= sk_nh_guard_health.GetFloat();
	SetMaxHealth( m_iHealth );

	m_flFieldOfView		= 0.2;

	CapabilitiesClear();

	BaseClass::Spawn();

	m_flSprintTime = 0.0f;
	m_flSprintRestTime = 0.0f;

	m_flNextMoanSound = gpGlobals->curtime + random->RandomFloat( 1.0, 4.0 );

	g_flNH_GuardGrenadeTimes = gpGlobals->curtime;
	m_flGrenadePullTime = gpGlobals->curtime;

	m_iGrenadeCount = NH_GUARD_MAX_GRENADES;
}

void CNPC_NH_Guard::Precache( void )
{
	BaseClass::Precache();

	/*
	PrecacheModel( "models/zombie/zombie_soldier.mdl" );

	
	PrecacheScriptSound( "Zombie.FootstepRight" );
	PrecacheScriptSound( "Zombie.FootstepLeft" );
	PrecacheScriptSound( "Zombine.ScuffRight" );
	PrecacheScriptSound( "Zombine.ScuffLeft" );
	PrecacheScriptSound( "Zombie.AttackHit" );
	PrecacheScriptSound( "Zombie.AttackMiss" );
	PrecacheScriptSound( "Zombine.Pain" );
	PrecacheScriptSound( "Zombine.Die" );
	PrecacheScriptSound( "Zombine.Alert" );
	PrecacheScriptSound( "Zombine.Idle" );
	PrecacheScriptSound( "Zombine.ReadyGrenade" );

	PrecacheScriptSound( "ATV_engine_null" );
	PrecacheScriptSound( "Zombine.Charge" );
	PrecacheScriptSound( "Zombie.Attack" );
	*/

	PrecacheScriptSound( "nh_guard.FootstepRight" );
	PrecacheScriptSound( "nh_guard.FootstepLeft" );
	PrecacheScriptSound( "nh_guard.ScuffRight" );
	PrecacheScriptSound( "nh_guard.ScuffLeft" );
	PrecacheScriptSound( "nh_guard.AttackHit" );
	PrecacheScriptSound( "nh_guard.AttackMiss" );
	PrecacheScriptSound( "nh_guard.Pain" );
	PrecacheScriptSound( "nh_guard.Die" );
	PrecacheScriptSound( "nh_guard.Alert" );
	PrecacheScriptSound( "nh_guard.Idle" );
	PrecacheScriptSound( "nh_guard.ReadyGrenade" );

	PrecacheScriptSound( "ATV_engine_null" );
	PrecacheScriptSound( "nh_guard.Charge" );
	PrecacheScriptSound( "nh_guard.Attack" );



	PrecacheModel( "models/NH2Zombies/security01.mdl" );
}

void CNPC_NH_Guard::SetZombieModel( void )
{
	SetModel( "models/NH2Zombies/security01.mdl" );
	SetHullType( HULL_HUMAN );

	SetBodygroup( ZOMBIE_BODYGROUP_HEADCRAB, !m_fIsHeadless );

	SetHullSizeNormal( true );
	SetDefaultEyeOffset();
	SetActivity( ACT_IDLE );
}

void CNPC_NH_Guard::PrescheduleThink( void )
{
	GatherGrenadeConditions();

	if( gpGlobals->curtime > m_flNextMoanSound )
	{
		if( CanPlayMoanSound() )
		{
			// Classic guy idles instead of moans.
			IdleSound();

			m_flNextMoanSound = gpGlobals->curtime + random->RandomFloat( 10.0, 15.0 );
		}
		else
		{
			m_flNextMoanSound = gpGlobals->curtime + random->RandomFloat( 2.5, 5.0 );
		}
	}

	if ( HasGrenade () )
	{
		CSoundEnt::InsertSound ( SOUND_DANGER, GetAbsOrigin() + GetSmoothedVelocity() * 0.5f , 256, 0.1, this, SOUNDENT_CHANNEL_ZOMBINE_GRENADE );

		if( IsSprinting() && GetEnemy() && GetEnemy()->Classify() == CLASS_PLAYER_ALLY_VITAL && HasCondition( COND_SEE_ENEMY ) )
		{
			if( GetAbsOrigin().DistToSqr(GetEnemy()->GetAbsOrigin()) < Square( 144 ) )
			{
				StopSprint();
			}
		}
	}

	BaseClass::PrescheduleThink();
}

void CNPC_NH_Guard::OnScheduleChange( void )
{
	if ( HasCondition( COND_CAN_MELEE_ATTACK1 ) && IsSprinting() == true )
	{
		m_flSuperFastAttackTime = gpGlobals->curtime + 1.0f;
	}

	BaseClass::OnScheduleChange();
}
bool CNPC_NH_Guard::CanRunAScriptedNPCInteraction( bool bForced )
{
	if ( HasGrenade() == true )
		return false;

	return BaseClass::CanRunAScriptedNPCInteraction( bForced );
}

int CNPC_NH_Guard::SelectSchedule( void )
{
	if ( GetHealth() <= 0 )
		return BaseClass::SelectSchedule();

	if ( HasCondition( COND_NH_GUARD_GRENADE ) )
	{
		ClearCondition( COND_NH_GUARD_GRENADE );
		
		return SCHED_NH_GUARD_PULL_GRENADE;
	}

	return BaseClass::SelectSchedule();
}

void CNPC_NH_Guard::BuildScheduleTestBits( void )
{
	BaseClass::BuildScheduleTestBits();

	SetCustomInterruptCondition( COND_NH_GUARD_GRENADE );
}

Activity CNPC_NH_Guard::NPC_TranslateActivity( Activity baseAct )
{
	if ( baseAct == ACT_MELEE_ATTACK1 )
	{
		if ( m_flSuperFastAttackTime > gpGlobals->curtime || HasGrenade() )
		{
			return (Activity)ACT_NH_GUARD_ATTACK_FAST;
		}
	}

	if ( baseAct == ACT_IDLE )
	{
		if ( HasGrenade() )
		{
			return (Activity)ACT_NH_GUARD_GRENADE_IDLE;
		}
	}

	return BaseClass::NPC_TranslateActivity( baseAct );
}

int CNPC_NH_Guard::MeleeAttack1Conditions ( float flDot, float flDist )
{
	int iBase = BaseClass::MeleeAttack1Conditions( flDot, flDist );

	if( HasGrenade() )
	{
		//Adrian: stop spriting if we get close enough to melee and we have a grenade
		//this gives NPCs time to move away from you (before it was almost impossible cause of the high sprint speed)
		if ( iBase == COND_CAN_MELEE_ATTACK1 )
		{
			StopSprint();
		}
	}

	return iBase;
}

void CNPC_NH_Guard::GatherGrenadeConditions( void )
{
	return; // nh2, let's not throw a grenade... ever

	if ( m_iGrenadeCount <= 0 )
		return;

	if ( g_flNH_GuardGrenadeTimes > gpGlobals->curtime )
		return;

	if ( m_flGrenadePullTime > gpGlobals->curtime )
		return;

	if ( m_flSuperFastAttackTime >= gpGlobals->curtime )
		return;
	
	if ( HasGrenade() )
		return;

	if ( GetEnemy() == NULL )
		return;

	if ( FVisible( GetEnemy() ) == false )
		return;

	if ( IsSprinting() )
		return;

	if ( IsOnFire() )
		return;
	
	if ( IsRunningDynamicInteraction() == true )
		return;

	if ( m_ActBusyBehavior.IsActive() )
		return;

	CBasePlayer *pPlayer = AI_GetSinglePlayer();

	if ( pPlayer && pPlayer->FVisible( this ) )
	{
		float flLengthToPlayer = (pPlayer->GetAbsOrigin() - GetAbsOrigin()).Length();
		float flLengthToEnemy = flLengthToPlayer;

		if ( pPlayer != GetEnemy() )
		{
			flLengthToEnemy = ( GetEnemy()->GetAbsOrigin() - GetAbsOrigin()).Length();
		}

		if ( flLengthToPlayer <= GRENADE_PULL_MAX_DISTANCE && flLengthToEnemy <= GRENADE_PULL_MAX_DISTANCE )
		{
			float flPullChance = 1.0f - ( flLengthToEnemy / GRENADE_PULL_MAX_DISTANCE );
			m_flGrenadePullTime = gpGlobals->curtime + 0.5f;

			if ( flPullChance >= random->RandomFloat( 0.0f, 1.0f ) )
			{
				g_flNH_GuardGrenadeTimes = gpGlobals->curtime + 10.0f;
				SetCondition( COND_NH_GUARD_GRENADE );
			}
		}
	}
}

int CNPC_NH_Guard::TranslateSchedule( int scheduleType ) 
{
	return BaseClass::TranslateSchedule( scheduleType );
}

void CNPC_NH_Guard::DropGrenade( Vector vDir )
{
	if ( m_hGrenade == NULL )
		 return;

	m_hGrenade->SetParent( NULL );
	m_hGrenade->SetOwnerEntity( NULL );

	Vector vGunPos;
	QAngle angles;
	GetAttachment( "grenade_attachment", vGunPos, angles );

	IPhysicsObject *pPhysObj = m_hGrenade->VPhysicsGetObject();

	if ( pPhysObj == NULL )
	{
		m_hGrenade->SetMoveType( MOVETYPE_VPHYSICS );
		m_hGrenade->SetSolid( SOLID_VPHYSICS );
		m_hGrenade->SetCollisionGroup( COLLISION_GROUP_WEAPON );

		m_hGrenade->CreateVPhysics();
	}

	if ( pPhysObj )
	{
		pPhysObj->Wake();
		pPhysObj->SetPosition( vGunPos, angles, true );
		pPhysObj->ApplyForceCenter( vDir * 0.2f );

		pPhysObj->RecheckCollisionFilter();
	}

	m_hGrenade = NULL;
}

void CNPC_NH_Guard::Event_Killed( const CTakeDamageInfo &info )
{
	BaseClass::Event_Killed( info );

	if ( HasGrenade() )
	{
		DropGrenade( vec3_origin );
	}
}

//-----------------------------------------------------------------------------
// Purpose:  This is a generic function (to be implemented by sub-classes) to
//			 handle specific interactions between different types of characters
//			 (For example the barnacle grabbing an NPC)
// Input  :  Constant for the type of interaction
// Output :	 true  - if sub-class has a response for the interaction
//			 false - if sub-class has no response
//-----------------------------------------------------------------------------
bool CNPC_NH_Guard::HandleInteraction( int interactionType, void *data, CBaseCombatCharacter *sourceEnt )
{
	if ( interactionType == g_interactionBarnacleVictimGrab )
	{
		if ( HasGrenade() )
		{
			DropGrenade( vec3_origin );
		}
	}

	return BaseClass::HandleInteraction( interactionType, data, sourceEnt );
}

void CNPC_NH_Guard::TraceAttack( const CTakeDamageInfo &info, const Vector &vecDir, trace_t *ptr, CDmgAccumulator *pAccumulator  )
{
	BaseClass::TraceAttack( info, vecDir, ptr, pAccumulator );
	
	//Only knock grenades off their hands if it's a player doing the damage.
	if ( info.GetAttacker() && info.GetAttacker()->IsNPC() )
		return;

	if ( info.GetDamageType() & ( DMG_BULLET | DMG_CLUB ) )
	{
		if ( ptr->hitgroup == HITGROUP_LEFTARM )
		{
			if ( HasGrenade() )
			{
				DropGrenade( info.GetDamageForce() );
				StopSprint();
			}
		}
	}
}

void CNPC_NH_Guard::HandleAnimEvent( animevent_t *pEvent )
{
	if ( pEvent->event == AE_NH_GUARD_PULLPIN )
	{
		Vector vecStart;
		QAngle angles;
		GetAttachment( "grenade_attachment", vecStart, angles );

		CBaseGrenade *pGrenade = Fraggrenade_Create( vecStart, vec3_angle, vec3_origin, AngularImpulse( 0, 0, 0 ), this, 3.5f, true );

		if ( pGrenade )
		{
			// Move physobject to shadow
			IPhysicsObject *pPhysicsObject = pGrenade->VPhysicsGetObject();

			if ( pPhysicsObject )
			{
				pGrenade->VPhysicsDestroyObject();

				int iAttachment = LookupAttachment( "grenade_attachment");

				pGrenade->SetMoveType( MOVETYPE_NONE );
				pGrenade->SetSolid( SOLID_NONE );
				pGrenade->SetCollisionGroup( COLLISION_GROUP_DEBRIS );

				pGrenade->SetAbsOrigin( vecStart );
				pGrenade->SetAbsAngles( angles );

				pGrenade->SetParent( this, iAttachment );

				pGrenade->SetDamage( 200.0f );
				m_hGrenade = pGrenade;
				
				EmitSound( "nh_guard.ReadyGrenade" );

				// Tell player allies nearby to regard me!
				CAI_BaseNPC **ppAIs = g_AI_Manager.AccessAIs();
				CAI_BaseNPC *pNPC;
				for ( int i = 0; i < g_AI_Manager.NumAIs(); i++ )
				{
					pNPC = ppAIs[i];

					if( pNPC->Classify() == CLASS_PLAYER_ALLY || pNPC->Classify() == CLASS_PLAYER_ALLY_VITAL && pNPC->FVisible(this) )
					{
						int priority;
						Disposition_t disposition;

						priority = pNPC->IRelationPriority(this);
						disposition = pNPC->IRelationType(this);

						pNPC->AddEntityRelationship( this, disposition, priority + 1 );
					}
				}
			}

			m_iGrenadeCount--;
		}

		return;
	}

	if ( pEvent->event == AE_NPC_ATTACK_BROADCAST )
	{
		if ( HasGrenade() )
			return;
	}

	BaseClass::HandleAnimEvent( pEvent );
}

bool CNPC_NH_Guard::AllowedToSprint( void )
{
	if ( IsOnFire() )
		return false;
	
	//If you're sprinting then there's no reason to sprint again.
	if ( IsSprinting() )
		return false;

	int iChance = SPRINT_CHANCE_VALUE;

	CHL2_Player *pPlayer = dynamic_cast <CHL2_Player*> ( AI_GetSinglePlayer() );

	if ( pPlayer )
	{
		if ( HL2GameRules()->IsAlyxInDarknessMode() && pPlayer->FlashlightIsOn() == false )
		{
			iChance = SPRINT_CHANCE_VALUE_DARKNESS;
		}

		//Bigger chance of this happening if the player is not looking at the zombie
		if ( pPlayer->FInViewCone( this ) == false )
		{
			iChance *= 2;
		}
	}

	if ( HasGrenade() ) 
	{
		iChance *= 4;
	}

	//Below 25% health they'll always sprint
	if ( ( GetHealth() > GetMaxHealth() * 0.5f ) )
	{
		if ( IsStrategySlotRangeOccupied( SQUAD_SLOT_NH_GUARD_SPRINT1, SQUAD_SLOT_NH_GUARD_SPRINT2 ) == true )
			return false;
		
		if ( random->RandomInt( 0, 100 ) > iChance )
			return false;
		
		if ( m_flSprintRestTime > gpGlobals->curtime )
			return false;
	}

	float flLength = ( GetEnemy()->WorldSpaceCenter() - WorldSpaceCenter() ).Length();

	if ( flLength > MAX_SPRINT_DISTANCE )
		return false;

	return true;
}

void CNPC_NH_Guard::StopSprint( void )
{
	GetNavigator()->SetMovementActivity( ACT_WALK );

	m_flSprintTime = gpGlobals->curtime;
	m_flSprintRestTime = m_flSprintTime + random->RandomFloat( 2.5f, 5.0f );
}

void CNPC_NH_Guard::Sprint( bool bMadSprint )
{
	if ( IsSprinting() )
		return;

	OccupyStrategySlotRange( SQUAD_SLOT_NH_GUARD_SPRINT1, SQUAD_SLOT_NH_GUARD_SPRINT2 );
	GetNavigator()->SetMovementActivity( ACT_RUN );

	float flSprintTime = random->RandomFloat( MIN_SPRINT_TIME, MAX_SPRINT_TIME );

	//If holding a grenade then sprint until it blows up.
	if ( HasGrenade() || bMadSprint == true )
	{
		flSprintTime = 9999;
	}

	m_flSprintTime = gpGlobals->curtime + flSprintTime;

	//Don't sprint for this long after I'm done with this sprint run.
	m_flSprintRestTime = m_flSprintTime + random->RandomFloat( 2.5f, 5.0f );

	EmitSound( "nh_guard.Charge" );
}

void CNPC_NH_Guard::RunTask( const Task_t *pTask )
{
	switch ( pTask->iTask )
	{
		case TASK_WAIT_FOR_MOVEMENT_STEP:
		case TASK_WAIT_FOR_MOVEMENT:
		{
			BaseClass::RunTask( pTask );

			if ( IsOnFire() && IsSprinting() )
			{
				StopSprint();
			}

			//Only do this if I have an enemy
			if ( GetEnemy() )
			{
				if ( AllowedToSprint() == true )
				{
					Sprint( ( GetHealth() <= GetMaxHealth() * 0.5f ) );
					return;
				}

				if ( HasGrenade() )
				{
					if ( IsSprinting() )
					{
						GetNavigator()->SetMovementActivity( (Activity)ACT_NH_GUARD_GRENADE_RUN );
					}
					else
					{
						GetNavigator()->SetMovementActivity( (Activity)ACT_NH_GUARD_GRENADE_WALK );
					}

					return;
				}

				if ( GetNavigator()->GetMovementActivity() != ACT_WALK )
				{
					if ( IsSprinting() == false )
					{
						GetNavigator()->SetMovementActivity( ACT_WALK );
					}
				}
			}
			else
			{
				GetNavigator()->SetMovementActivity( ACT_WALK );
			}
		
			break;
		}
		default:
		{
			BaseClass::RunTask( pTask );
			break;
		}
	}
}

void CNPC_NH_Guard::InputStartSprint ( inputdata_t &inputdata )
{
	Sprint();
}

void CNPC_NH_Guard::InputPullGrenade ( inputdata_t &inputdata )
{
	g_flNH_GuardGrenadeTimes = gpGlobals->curtime + 5.0f;
	SetCondition( COND_NH_GUARD_GRENADE );
}

//-----------------------------------------------------------------------------
// Purpose: Returns a moan sound for this class of zombie.
//-----------------------------------------------------------------------------
const char *CNPC_NH_Guard::GetMoanSound( int nSound )
{
	return pMoanSounds[ nSound % ARRAYSIZE( pMoanSounds ) ];
}

//-----------------------------------------------------------------------------
// Purpose: Sound of a footstep
//-----------------------------------------------------------------------------
void CNPC_NH_Guard::FootstepSound( bool fRightFoot )
{
	if( fRightFoot )
	{
		EmitSound( "nhzombie.FootstepRight" );
	}
	else
	{
		EmitSound( "nhzombie.FootstepLeft" );
	}
}

//-----------------------------------------------------------------------------
// Purpose: Overloaded so that explosions don't split the zombine in twain.
//-----------------------------------------------------------------------------
bool CNPC_NH_Guard::ShouldBecomeTorso( const CTakeDamageInfo &info, float flDamageThreshold )
{
	return false;
}

//-----------------------------------------------------------------------------
// Purpose: Sound of a foot sliding/scraping
//-----------------------------------------------------------------------------
void CNPC_NH_Guard::FootscuffSound( bool fRightFoot )
{
	if( fRightFoot )
	{
		EmitSound( "nh_guard.ScuffRight" );
	}
	else
	{
		EmitSound( "nh_guard.ScuffLeft" );
	}
}

//-----------------------------------------------------------------------------
// Purpose: Play a random attack hit sound
//-----------------------------------------------------------------------------
void CNPC_NH_Guard::AttackHitSound( void )
{
	EmitSound( "nhzombie.AttackHit" );
}

//-----------------------------------------------------------------------------
// Purpose: Play a random attack miss sound
//-----------------------------------------------------------------------------
void CNPC_NH_Guard::AttackMissSound( void )
{
	// Play a random attack miss sound
	EmitSound( "nhzombie.AttackMiss" );
}

//-----------------------------------------------------------------------------
// Purpose: 
//-----------------------------------------------------------------------------
void CNPC_NH_Guard::PainSound( const CTakeDamageInfo &info )
{
	// We're constantly taking damage when we are on fire. Don't make all those noises!
	if ( IsOnFire() )
	{
		return;
	}

	EmitSound( "nh_guard.Pain" );
}

//-----------------------------------------------------------------------------
//-----------------------------------------------------------------------------
void CNPC_NH_Guard::DeathSound( const CTakeDamageInfo &info ) 
{
	EmitSound( "nh_guard.Die" );
}

//-----------------------------------------------------------------------------
// Purpose: 
//-----------------------------------------------------------------------------
void CNPC_NH_Guard::AlertSound( void )
{
	EmitSound( "nh_guard.Alert" );

	// Don't let a moan sound cut off the alert sound.
	m_flNextMoanSound += random->RandomFloat( 2.0, 4.0 );
}

//-----------------------------------------------------------------------------
// Purpose: Play a random idle sound.
//-----------------------------------------------------------------------------
void CNPC_NH_Guard::IdleSound( void )
{
	if( GetState() == NPC_STATE_IDLE && random->RandomFloat( 0, 1 ) == 0 )
	{
		// Moan infrequently in IDLE state.
		return;
	}

	if( IsSlumped() )
	{
		// Sleeping zombies are quiet.
		return;
	}

	EmitSound( "nh_guard.Idle" );
	MakeAISpookySound( 360.0f );
}

//-----------------------------------------------------------------------------
// Purpose: Play a random attack sound.
//-----------------------------------------------------------------------------
void CNPC_NH_Guard::AttackSound( void )
{
	
}

//-----------------------------------------------------------------------------
//-----------------------------------------------------------------------------
const char *CNPC_NH_Guard::GetHeadcrabModel( void )
{
	return "models/headcrabclassic.mdl";
}

//-----------------------------------------------------------------------------
// Purpose: 
//-----------------------------------------------------------------------------
const char *CNPC_NH_Guard::GetLegsModel( void )
{
	return "models/zombie/zombie_soldier_legs.mdl";
}

//-----------------------------------------------------------------------------
//-----------------------------------------------------------------------------
const char *CNPC_NH_Guard::GetTorsoModel( void )
{
	return "models/zombie/zombie_soldier_torso.mdl";
}

//---------------------------------------------------------
// Classic zombie only uses moan sound if on fire.
//---------------------------------------------------------
void CNPC_NH_Guard::MoanSound( envelopePoint_t *pEnvelope, int iEnvelopeSize )
{
	if( IsOnFire() )
	{
		BaseClass::MoanSound( pEnvelope, iEnvelopeSize );
	}
}

//-----------------------------------------------------------------------------
// Purpose: Returns the classname (ie "npc_headcrab") to spawn when our headcrab bails.
//-----------------------------------------------------------------------------
const char *CNPC_NH_Guard::GetHeadcrabClassname( void )
{
	return "npc_headcrab";
}

void CNPC_NH_Guard::ReleaseGrenade( Vector vPhysgunPos )
{
	Sprint();
	return;


	if ( HasGrenade() == false )
		return;

	Vector vDir = vPhysgunPos - m_hGrenade->GetAbsOrigin();
	VectorNormalize( vDir );

	Activity aActivity;

	Vector vForward, vRight;
	GetVectors( &vForward, &vRight, NULL );

	float flDotForward	= DotProduct( vForward, vDir );
	float flDotRight	= DotProduct( vRight, vDir );

	bool bNegativeForward = false;
	bool bNegativeRight = false;

	if ( flDotForward < 0.0f )
	{
		bNegativeForward = true;
		flDotForward = flDotForward * -1;
	}

	if ( flDotRight < 0.0f )
	{
		bNegativeRight = true;
		flDotRight = flDotRight * -1;
	}

	if ( flDotRight > flDotForward )
	{
		if ( bNegativeRight == true )
			aActivity = (Activity)ACT_NH_GUARD_GRENADE_FLINCH_WEST;
		else 
			aActivity = (Activity)ACT_NH_GUARD_GRENADE_FLINCH_EAST;
	}
	else
	{
		if ( bNegativeForward == true )
			aActivity = (Activity)ACT_NH_GUARD_GRENADE_FLINCH_BACK;
		else 
			aActivity = (Activity)ACT_NH_GUARD_GRENADE_FLINCH_FRONT;
	}

	AddGesture( aActivity );

	DropGrenade( vec3_origin );

	if ( IsSprinting() )
	{
		StopSprint();
	}
	else
	{
		Sprint();
	}
}

CBaseEntity *CNPC_NH_Guard::OnFailedPhysGunPickup( Vector vPhysgunPos )
{
	CBaseEntity *pGrenade = m_hGrenade;
	ReleaseGrenade( vPhysgunPos );
	return pGrenade;
}

//-----------------------------------------------------------------------------
//
// Schedules
//
//-----------------------------------------------------------------------------

AI_BEGIN_CUSTOM_NPC( npc_nh_guard, CNPC_NH_Guard )

	//Squad slots
	DECLARE_SQUADSLOT( SQUAD_SLOT_NH_GUARD_SPRINT1 )
	DECLARE_SQUADSLOT( SQUAD_SLOT_NH_GUARD_SPRINT2 )

	DECLARE_CONDITION( COND_NH_GUARD_GRENADE )

	DECLARE_ACTIVITY( ACT_NH_GUARD_GRENADE_PULL )
	DECLARE_ACTIVITY( ACT_NH_GUARD_GRENADE_WALK )
	DECLARE_ACTIVITY( ACT_NH_GUARD_GRENADE_RUN )
	DECLARE_ACTIVITY( ACT_NH_GUARD_GRENADE_IDLE )
	DECLARE_ACTIVITY( ACT_NH_GUARD_ATTACK_FAST )
	DECLARE_ACTIVITY( ACT_NH_GUARD_GRENADE_FLINCH_BACK )
	DECLARE_ACTIVITY( ACT_NH_GUARD_GRENADE_FLINCH_FRONT )
	DECLARE_ACTIVITY( ACT_NH_GUARD_GRENADE_FLINCH_WEST)
	DECLARE_ACTIVITY( ACT_NH_GUARD_GRENADE_FLINCH_EAST )

	DECLARE_ANIMEVENT( AE_NH_GUARD_PULLPIN )


	DEFINE_SCHEDULE
	(
	SCHED_NH_GUARD_PULL_GRENADE,

	"	Tasks"
	"		TASK_PLAY_SEQUENCE					ACTIVITY:ACT_NH_GUARD_GRENADE_PULL"


	"	Interrupts"

	)

AI_END_CUSTOM_NPC()



int CNPC_NH_Guard::OnTakeDamage_Alive( const CTakeDamageInfo &inputInfo )
{
	
	return BaseClass::OnTakeDamage_Alive( inputInfo );
}

Vector CNPC_NH_Guard::HeadTarget( const Vector &posSrc )
{
	int iCrabAttachment = LookupAttachment( "mouth" );
	Assert( iCrabAttachment > 0 );

	Vector vecPosition;

	GetAttachment( iCrabAttachment, vecPosition );

	return vecPosition;
}
