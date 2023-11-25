//========= Copyright © 1996-2005, Valve Corporation, All rights reserved. ============//
//
// Purpose: A slow-moving, once-human headcrab victim with only melee attacks.
//
//=============================================================================//

#include "cbase.h"

#include "doors.h"

#include "simtimer.h"
#include "npc_BaseZombie.h"
#include "ai_hull.h"
#include "ai_navigator.h"
#include "ai_memory.h"
#include "gib.h"
#include "soundenvelope.h"
#include "engine/IEngineSound.h"
#include "ammodef.h"
#include "particle_parse.h"

// memdbgon must be the last include file in a .cpp file!!!
#include "tier0/memdbgon.h"

// ACT_FLINCH_PHYSICS

#define RANDOM_BODYGROUP -1

ConVar	sk_nh_zombie_health( "sk_nh_zombie_health","50");
ConVar	sk_nh_torso_damage_threshold("sk_nh_torso_damage_threshold","30");
ConVar	sk_nh_zombie_head_height("sk_nh_zombie_head_height","50");
extern ConVar sk_plr_dmg_hatchet;

envelopePoint_t envNHZombieMoanVolumeFast[] =
{
	{	7.0f, 7.0f,
		0.1f, 0.1f,
	},
	{	0.0f, 0.0f,
		0.2f, 0.3f,
	},
};

envelopePoint_t envNHZombieMoanVolume[] =
{
	{	1.0f, 1.0f,
		0.1f, 0.1f,
	},
	{	1.0f, 1.0f,
		0.2f, 0.2f,
	},
	{	0.0f, 0.0f,
		0.3f, 0.4f,
	},
};

envelopePoint_t envNHZombieMoanVolumeLong[] =
{
	{	1.0f, 1.0f,
		0.3f, 0.5f,
	},
	{	1.0f, 1.0f,
		0.6f, 1.0f,
	},
	{	0.0f, 0.0f,
		0.3f, 0.4f,
	},
};

envelopePoint_t envNHZombieMoanIgnited[] =
{
	{	1.0f, 1.0f,
		0.5f, 1.0f,
	},
	{	1.0f, 1.0f,
		30.0f, 30.0f,
	},
	{	0.0f, 0.0f,
		0.5f, 1.0f,
	},
};


//=============================================================================
//=============================================================================

class CNH_Zombie : public CAI_BlendingHost<CNPC_BaseZombie>
{
	DECLARE_DATADESC();
	DECLARE_CLASS( CNH_Zombie, CAI_BlendingHost<CNPC_BaseZombie> );

public:
	CNH_Zombie()
	 : m_DurationDoorBash( 2, 6),
	   m_NextTimeToStartDoorBash( 3.0 ), m_nHeadModel(RANDOM_BODYGROUP), m_bCanBeTorso(true)
	{
	}

	void Spawn( void );
	void Precache( void );

	void SetZombieModel( void );
	void MoanSound( envelopePoint_t *pEnvelope, int iEnvelopeSize );
	bool ShouldBecomeTorso( const CTakeDamageInfo &info, float flDamageThreshold );
	bool CanBecomeLiveTorso() { return true; }

	void GatherConditions( void );

	int SelectFailSchedule( int failedSchedule, int failedTask, AI_TaskFailureCode_t taskFailCode );
	int TranslateSchedule( int scheduleType );

#ifndef HL2_EPISODIC
	void CheckFlinches() {} // Zombie has custom flinch code
#endif // HL2_EPISODIC

	Activity NPC_TranslateActivity( Activity newActivity );

	void OnStateChange( NPC_STATE OldState, NPC_STATE NewState );

	void StartTask( const Task_t *pTask );
	void RunTask( const Task_t *pTask );

	virtual const char *GetLegsModel( void );
	virtual const char *GetTorsoModel( void );
	virtual const char *GetHeadcrabClassname( void );
	virtual const char *GetHeadcrabModel( void );
	virtual void ReleaseHeadcrab( const Vector &vecOrigin, const Vector &vecVelocity, bool fRemoveHead, bool fRagdollBody, bool fRagdollCrab = false ) {};

	virtual bool OnObstructingDoor( AILocalMoveGoal_t *pMoveGoal, 
								 CBaseDoor *pDoor,
								 float distClear, 
								 AIMoveResult_t *pResult );

	Activity SelectDoorBash();

	void Ignite( float flFlameLifetime, bool bNPCOnly = true, float flSize = 0.0f, bool bCalledByLevelDesigner = false );
	void Extinguish();
	int OnTakeDamage_Alive( const CTakeDamageInfo &inputInfo );
	int OnTakeDamage_Dead( const CTakeDamageInfo &inputInfo );
	bool IsHeavyDamage( const CTakeDamageInfo &info );
	bool IsSquashed( const CTakeDamageInfo &info );
	void BuildScheduleTestBits( void );
	
	HeadcrabRelease_t ShouldReleaseHeadcrab( const CTakeDamageInfo &info, float flDamageThreshold );

	float GetHitgroupDamageMultiplier( int iHitGroup, const CTakeDamageInfo &info );

	void PrescheduleThink( void );
	int SelectSchedule ( void );

	void PainSound( const CTakeDamageInfo &info );
	void DeathSound( const CTakeDamageInfo &info );
	void AlertSound( void );
	void IdleSound( void );
	void AttackSound( void );
	void AttackHitSound( void );
	void AttackMissSound( void );
	void FootstepSound( bool fRightFoot );
	void FootscuffSound( bool fRightFoot );
	Vector HeadTarget( const Vector &posSrc );

	const char *GetMoanSound( int nSound );
	
public:
	DEFINE_CUSTOM_AI;

protected:
	static const char *pMoanSounds[];


private:
	CHandle< CBaseDoor > m_hBlockingDoor;
	float				 m_flDoorBashYaw;
	
	CRandSimTimer 		 m_DurationDoorBash;
	CSimTimer 	  		 m_NextTimeToStartDoorBash;

	Vector				 m_vPositionCharged;

	int					m_nHeadModel;
	int					m_nBodyModel;
	bool				m_bCanBeTorso;
};

//LINK_ENTITY_TO_CLASS( npc_nh_zombie, CNH_Zombie );
//LINK_ENTITY_TO_CLASS( npc_nh_zombie_torso, CNH_Zombie );
LINK_ENTITY_TO_CLASS( npc_nh_doctor, CNH_Zombie );
LINK_ENTITY_TO_CLASS( npc_nh_janitor, CNH_Zombie );
LINK_ENTITY_TO_CLASS( npc_nh_patient, CNH_Zombie );
LINK_ENTITY_TO_CLASS( npc_nh_surgeon, CNH_Zombie );
LINK_ENTITY_TO_CLASS( npc_nh_worker, CNH_Zombie );
LINK_ENTITY_TO_CLASS( npc_nh_cook, CNH_Zombie );

LINK_ENTITY_TO_CLASS( npc_nh_doctor_torso, CNH_Zombie );
LINK_ENTITY_TO_CLASS( npc_nh_janitor_torso, CNH_Zombie );
LINK_ENTITY_TO_CLASS( npc_nh_patient_torso, CNH_Zombie );

//---------------------------------------------------------
//---------------------------------------------------------
const char *CNH_Zombie::pMoanSounds[] =
{
	 "NPC_Basenhzombie.Moan1",
	 "NPC_Basenhzombie.Moan2",
	 "NPC_Basenhzombie.Moan3",
	 "NPC_Basenhzombie.Moan4",
};

//=========================================================
// Conditions
//=========================================================
enum
{
	COND_BLOCKED_BY_DOOR = LAST_BASE_ZOMBIE_CONDITION,
	COND_DOOR_OPENED,
	COND_ZOMBIE_CHARGE_TARGET_MOVED,
};

//=========================================================
// Schedules
//=========================================================
enum
{
	SCHED_ZOMBIE_BASH_DOOR = LAST_BASE_ZOMBIE_SCHEDULE,
	SCHED_ZOMBIE_WANDER_ANGRILY,
	SCHED_ZOMBIE_CHARGE_ENEMY,
	SCHED_ZOMBIE_FAIL,
};

//=========================================================
// Tasks
//=========================================================
enum
{
	TASK_ZOMBIE_EXPRESS_ANGER = LAST_BASE_ZOMBIE_TASK,
	TASK_ZOMBIE_YAW_TO_DOOR,
	TASK_ZOMBIE_ATTACK_DOOR,
	TASK_ZOMBIE_CHARGE_ENEMY,
};

//-----------------------------------------------------------------------------

int ACT_NH_ZOMBIE_TANTRUM;
int ACT_NH_ZOMBIE_WALLPOUND;

BEGIN_DATADESC( CNH_Zombie )

	DEFINE_FIELD( m_hBlockingDoor, FIELD_EHANDLE ),
	DEFINE_FIELD( m_flDoorBashYaw, FIELD_FLOAT ),
	DEFINE_EMBEDDED( m_DurationDoorBash ),
	DEFINE_EMBEDDED( m_NextTimeToStartDoorBash ),
	DEFINE_FIELD( m_vPositionCharged, FIELD_POSITION_VECTOR ),
	DEFINE_KEYFIELD( m_nHeadModel, FIELD_INTEGER, "headmodel"),
	DEFINE_KEYFIELD( m_nBodyModel, FIELD_INTEGER, "bodymodel"),
	DEFINE_KEYFIELD( m_bCanBeTorso, FIELD_BOOLEAN, "canbetorso"),

END_DATADESC()


//-----------------------------------------------------------------------------
// Purpose: 
//-----------------------------------------------------------------------------
void CNH_Zombie::Precache( void )
{
	BaseClass::Precache();

	//PrecacheModel( "models/zombie/classic.mdl" );
	//PrecacheModel( "models/zombie/classic_torso.mdl" );
	//PrecacheModel( "models/zombie/classic_legs.mdl" );

	/*
	PrecacheScriptSound( "Zombie.FootstepRight" );
	PrecacheScriptSound( "Zombie.FootstepLeft" );
	PrecacheScriptSound( "Zombie.FootstepLeft" );
	PrecacheScriptSound( "Zombie.ScuffRight" );
	PrecacheScriptSound( "Zombie.ScuffLeft" );
	PrecacheScriptSound( "Zombie.AttackHit" );
	PrecacheScriptSound( "Zombie.AttackMiss" );
	PrecacheScriptSound( "Zombie.Pain" );
	PrecacheScriptSound( "Zombie.Die" );
	PrecacheScriptSound( "Zombie.Alert" );
	PrecacheScriptSound( "Zombie.Idle" );
	PrecacheScriptSound( "Zombie.Attack" );

	PrecacheScriptSound( "NPC_BaseZombie.Moan1" );
	PrecacheScriptSound( "NPC_BaseZombie.Moan2" );
	PrecacheScriptSound( "NPC_BaseZombie.Moan3" );
	PrecacheScriptSound( "NPC_BaseZombie.Moan4" );
	*/

	PrecacheScriptSound( "nhzombie.headexplode" );
	PrecacheScriptSound( "nhzombie.headexplode_jet" );
	PrecacheScriptSound( "nhzombie.FootstepRight" );
	PrecacheScriptSound( "nhzombie.FootstepLeft" );
	PrecacheScriptSound( "nhzombie.FootstepLeft" );
	PrecacheScriptSound( "nhzombie.ScuffRight" );
	PrecacheScriptSound( "nhzombie.ScuffLeft" );
	PrecacheScriptSound( "nhzombie.AttackHit" );
	PrecacheScriptSound( "nhzombie.AttackMiss" );
	PrecacheScriptSound( "nhzombie.Pain" );
	PrecacheScriptSound( "nhzombie.Die" );
	PrecacheScriptSound( "nhzombie.Alert" );
	PrecacheScriptSound( "nhzombie.Idle" );
	PrecacheScriptSound( "nhzombie.Attack" );

	PrecacheScriptSound( "NPC_Basenhzombie.Moan1" );
	PrecacheScriptSound( "NPC_Basenhzombie.Moan2" );
	PrecacheScriptSound( "NPC_Basenhzombie.Moan3" );
	PrecacheScriptSound( "NPC_Basenhzombie.Moan4" );


	//PrecacheScriptSound( "General.BurningFlesh" );

	PrecacheModel( "models/NH2Zombies/doctor01.mdl" );

	PrecacheModel( "models/NH2Zombies/janitor01.mdl" );
	PrecacheModel( "models/NH2Zombies/janitor02.mdl" );

	PrecacheModel( "models/NH2Zombies/patient01.mdl" );
	PrecacheModel( "models/NH2Zombies/patient02.mdl" );
	PrecacheModel( "models/NH2Zombies/patient03.mdl" );

	PrecacheModel( "models/NH2Zombies/surgeon01.mdl" );
	PrecacheModel( "models/NH2Zombies/surgeon02.mdl" );

	PrecacheModel( "models/NH2Zombies/worker01.mdl" );
	
	PrecacheModel( "models/NH2Zombies/cook.mdl" );

	PrecacheModel( "models/NH2Zombies/gibs/doctor_torso.mdl" );
	PrecacheModel( "models/NH2Zombies/gibs/janitor_torso.mdl" );
	PrecacheModel( "models/NH2Zombies/gibs/patient_torso.mdl" );

	PrecacheModel( "models/NH2Zombies/gibs/doctor_legs.mdl" );
	PrecacheModel( "models/NH2Zombies/gibs/janitor_legs.mdl" );
	PrecacheModel( "models/NH2Zombies/gibs/patient_legs.mdl" );

	PrecacheParticleSystem( "blood_advisor_puncture_withdraw" );
}


Vector CNH_Zombie::HeadTarget( const Vector &posSrc )
{
	int iCrabAttachment = LookupAttachment( "mouth" );
	Assert( iCrabAttachment > 0 );

	Vector vecPosition;

	GetAttachment( iCrabAttachment, vecPosition );

	return vecPosition;
}


//-----------------------------------------------------------------------------
//-----------------------------------------------------------------------------
void CNH_Zombie::Spawn( void )
{
	Precache();

	//models/NH2Zombies/gibs/doctor_torso.mdl

	m_fIsTorso = false;
	
	if( FClassnameIs( this, "npc_nh_doctor_torso" ) )
		m_fIsTorso = true;

	if( FClassnameIs( this, "npc_nh_janitor_torso" ) )
		m_fIsTorso = true;

	if( FClassnameIs( this, "npc_nh_patient_torso" ) )
		m_fIsTorso = true;


	m_fIsHeadless = true; // No headcrab

	SetBloodColor( BLOOD_COLOR_RED );

	m_iHealth			= sk_nh_zombie_health.GetFloat();
	m_flFieldOfView		= 0.2;

	CapabilitiesClear();

	//GetNavigator()->SetRememberStaleNodes( false );

	BaseClass::Spawn();

	m_flNextMoanSound = gpGlobals->curtime + random->RandomFloat( 1.0, 4.0 );
}

//-----------------------------------------------------------------------------
//-----------------------------------------------------------------------------
void CNH_Zombie::PrescheduleThink( void )
{
  	if( gpGlobals->curtime > m_flNextMoanSound )
  	{
  		if( CanPlayMoanSound() )
  		{
			// Classic guy idles instead of moans.
			IdleSound();

  			m_flNextMoanSound = gpGlobals->curtime + random->RandomFloat( 2.0, 5.0 );
  		}
  		else
 		{
  			m_flNextMoanSound = gpGlobals->curtime + random->RandomFloat( 1.0, 2.0 );
  		}
  	}

	BaseClass::PrescheduleThink();
}

//-----------------------------------------------------------------------------
//-----------------------------------------------------------------------------
int CNH_Zombie::SelectSchedule ( void )
{
	if( HasCondition( COND_PHYSICS_DAMAGE ) && !m_ActBusyBehavior.IsActive() )
	{
		return SCHED_FLINCH_PHYSICS;
	}

	return BaseClass::SelectSchedule();
}

//-----------------------------------------------------------------------------
// Purpose: Sound of a footstep
//-----------------------------------------------------------------------------
void CNH_Zombie::FootstepSound( bool fRightFoot )
{
	if( fRightFoot )
	{
		EmitSound(  "nhzombie.FootstepRight" );
	}
	else
	{
		EmitSound( "nhzombie.FootstepLeft" );
	}
}

//-----------------------------------------------------------------------------
// Purpose: Sound of a foot sliding/scraping
//-----------------------------------------------------------------------------
void CNH_Zombie::FootscuffSound( bool fRightFoot )
{
	if( fRightFoot )
	{
		EmitSound( "nhzombie.ScuffRight" );
	}
	else
	{
		EmitSound( "nhzombie.ScuffLeft" );
	}
}

//-----------------------------------------------------------------------------
// Purpose: Play a random attack hit sound
//-----------------------------------------------------------------------------
void CNH_Zombie::AttackHitSound( void )
{
	EmitSound( "nhzombie.AttackHit" );
}

//-----------------------------------------------------------------------------
// Purpose: Play a random attack miss sound
//-----------------------------------------------------------------------------
void CNH_Zombie::AttackMissSound( void )
{
	// Play a random attack miss sound
	EmitSound( "nhzombie.AttackMiss" );
}

//-----------------------------------------------------------------------------
// Purpose: 
//-----------------------------------------------------------------------------
void CNH_Zombie::PainSound( const CTakeDamageInfo &info )
{
	// We're constantly taking damage when we are on fire. Don't make all those noises!
	if ( IsOnFire() )
	{
		return;
	}

	EmitSound( "nhzombie.Pain" );
}

//-----------------------------------------------------------------------------
//-----------------------------------------------------------------------------
void CNH_Zombie::DeathSound( const CTakeDamageInfo &info ) 
{
	EmitSound( "nhzombie.Die" );
}

//-----------------------------------------------------------------------------
// Purpose: 
//-----------------------------------------------------------------------------
void CNH_Zombie::AlertSound( void )
{
	EmitSound( "nhzombie.Alert" );

	// Don't let a moan sound cut off the alert sound.
	m_flNextMoanSound += random->RandomFloat( 2.0, 4.0 );
}

//-----------------------------------------------------------------------------
// Purpose: Returns a moan sound for this class of zombie.
//-----------------------------------------------------------------------------
const char *CNH_Zombie::GetMoanSound( int nSound )
{
	return pMoanSounds[ nSound % ARRAYSIZE( pMoanSounds ) ];
}

//-----------------------------------------------------------------------------
// Purpose: Play a random idle sound.
//-----------------------------------------------------------------------------
void CNH_Zombie::IdleSound( void )
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

	EmitSound( "nhzombie.Idle" );
	MakeAISpookySound( 360.0f );
}

//-----------------------------------------------------------------------------
// Purpose: Play a random attack sound.
//-----------------------------------------------------------------------------
void CNH_Zombie::AttackSound( void )
{
	EmitSound( "nhzombie.Attack" );
}

//-----------------------------------------------------------------------------
// Purpose: Returns the classname (ie "npc_headcrab") to spawn when our headcrab bails.
//-----------------------------------------------------------------------------
const char *CNH_Zombie::GetHeadcrabClassname( void )
{
	return "npc_headcrab";
}

//-----------------------------------------------------------------------------
//-----------------------------------------------------------------------------
const char *CNH_Zombie::GetHeadcrabModel( void )
{
	return "models/headcrabclassic.mdl";
}

//-----------------------------------------------------------------------------
// Purpose: 
//-----------------------------------------------------------------------------
const char *CNH_Zombie::GetLegsModel( void )
{
	//return "models/zombie/classic_legs.mdl";
	if( FClassnameIs( this, "npc_nh_doctor" ) )
		return "models/NH2Zombies/gibs/doctor_legs.mdl"; 

	if( FClassnameIs( this, "npc_nh_patient" ) )
		return "models/NH2Zombies/gibs/patient_legs.mdl"; 

	if( FClassnameIs( this, "npc_nh_janitor" ) )
		return "models/NH2Zombies/gibs/janitor_legs.mdl";

	return "models/NH2Zombies/gibs/doctor_legs.mdl";
}

//-----------------------------------------------------------------------------
//-----------------------------------------------------------------------------
const char *CNH_Zombie::GetTorsoModel( void )
{
	if( FClassnameIs( this, "npc_nh_doctor" ) || FClassnameIs( this, "npc_nh_doctor_torso" ) )
		return "models/NH2Zombies/gibs/doctor_torso.mdl"; 

	if( FClassnameIs( this, "npc_nh_patient" ) || FClassnameIs( this, "npc_nh_patient_torso" )  )
		return "models/NH2Zombies/gibs/patient_torso.mdl"; 

	if( FClassnameIs( this, "npc_nh_janitor" ) || FClassnameIs( this, "npc_nh_janitor_torso" )  )
		return "models/NH2Zombies/gibs/janitor_torso.mdl";

	return "models/NH2Zombies/gibs/doctor_torso.mdl";
}


//---------------------------------------------------------
//---------------------------------------------------------
void CNH_Zombie::SetZombieModel( void )
{
	Hull_t lastHull = GetHullType();

	if ( !m_fIsTorso )
	{

		if ( FClassnameIs( this, "npc_nh_doctor" ) )
		{
			SetModel( "models/NH2Zombies/doctor01.mdl" );
	
			if ( m_nHeadModel == RANDOM_BODYGROUP || m_nHeadModel == 13 )
				m_nHeadModel = random->RandomInt( 2, 12 );
			SetBodygroup ( 1, m_nHeadModel );
	
			// Random neck tags
			SetBodygroup ( 2, random->RandomInt( 0, 2 ) );


		}

		if ( FClassnameIs( this, "npc_nh_cook" ) )
		{
			SetModel( "models/NH2Zombies/cook.mdl" );
			SetBodygroup ( 1, 2 );
		}

		if ( FClassnameIs( this, "npc_nh_worker" ) )
		{
			SetModel( "models/NH2Zombies/worker01.mdl" );
	
			if ( m_nHeadModel == RANDOM_BODYGROUP || m_nHeadModel == 16 )
				m_nHeadModel = random->RandomInt( 2, 15 );
			SetBodygroup ( 1, m_nHeadModel );
			
			// Random gloves
			int gloves = random->RandomInt( 0, 2 );
			if (gloves == 2)
			{
				SetBodygroup ( 2, random->RandomInt( 0, 1 ));
				SetBodygroup ( 3, random->RandomInt( 0, 1 ));
			}
			else
			{
				SetBodygroup ( 2, gloves);
				SetBodygroup ( 3, gloves);
			}
			
			//random watch
			int watch = random->RandomInt( 0, 1 );
			SetBodygroup ( 4, watch );

			//random radio
			int radio = random->RandomInt( 0, 1 );
			SetBodygroup ( 5, radio );

			//random skin
			m_nSkin = random->RandomInt( 0, 1 );
			

		}

		if ( FClassnameIs( this, "npc_nh_janitor" ) )
		{
			if ( m_nBodyModel == 0 )
				m_nBodyModel = random->RandomInt( 1, 2 );
	
			if ( m_nBodyModel == 1 )
				SetModel( "models/NH2Zombies/janitor01.mdl" );
			if ( m_nBodyModel == 2 )
				SetModel( "models/NH2Zombies/janitor02.mdl" );
			
			if ( m_nHeadModel == RANDOM_BODYGROUP || m_nHeadModel == 15 )
				m_nHeadModel = random->RandomInt( 2, 14 );
	
			SetBodygroup ( 1, m_nHeadModel );
	
			// Random tag
			SetBodygroup ( 2, random->RandomInt( 0, 1 ) );
	
			// Random rag
			SetBodygroup ( 3, random->RandomInt( 0, 1 ) );
	
			// If head is black, the hands should be black too!
			if ( m_nHeadModel == 2 || m_nHeadModel == 3 ) 
				SetBodygroup ( 4, 0 );
			else
				SetBodygroup( 4, 1 );
	
		}
	
		if ( FClassnameIs( this, "npc_nh_patient" ) )
		{
			
			if ( m_nBodyModel == 1 )
				SetModel( "models/NH2Zombies/patient01.mdl" );
			if ( m_nBodyModel == 2 )
				SetModel( "models/NH2Zombies/patient02.mdl" );
			if ( m_nBodyModel == 3 )
				SetModel( "models/NH2Zombies/patient03.mdl" );
	
			if ( m_nBodyModel == 0 )
			{
				m_nBodyModel = random->RandomInt( 1, 11 );
				if ( m_nBodyModel == 10 )
				{
					SetModel( "models/NH2Zombies/patient03.mdl" ); // 1/11 times
					m_nBodyModel = 3;
				}
				else if ( m_nBodyModel > 5 )
				{
					SetModel( "models/NH2Zombies/patient02.mdl" ); // 5/11 times
					m_nBodyModel = 2;
				}
				else
				{
					SetModel( "models/NH2Zombies/patient01.mdl" ); // 5/11 times
					m_nBodyModel = 3;
				}
			}


		
			if ( m_nHeadModel == RANDOM_BODYGROUP || m_nHeadModel == 15 )
				m_nHeadModel = random->RandomInt( 2, 14 );
			SetBodygroup ( 1, m_nHeadModel );
			
			// If head is black, the hands should be black too!
			if ( m_nHeadModel == 2 || m_nHeadModel == 9 ) 
				SetBodygroup ( 2, 0 );
			else //If head is white, hands should be white!
				SetBodygroup ( 2, 1 );
				
			//Random cast on right arm.
			if ( random->RandomInt( 0, 9 ) == 9 )
				SetBodygroup( 3, 0 );
			else
				SetBodygroup( 3, 1 );
	
			//Random cast on left arm for patients who have a left arm
			if ( random->RandomInt( 0, 9 ) == 9 && m_nBodyModel != 3 )
				SetBodygroup( 4, 0 );
			else
				SetBodygroup( 4, 1 );
		}
	
		if ( FClassnameIs( this, "npc_nh_surgeon" ) )
		{
			if ( m_nBodyModel == 0 )
				m_nBodyModel = random->RandomInt( 1, 2 );
	
			if ( m_nBodyModel == 1 )
				SetModel( "models/NH2Zombies/surgeon01.mdl" );
			if ( m_nBodyModel == 2 )
				SetModel( "models/NH2Zombies/surgeon02.mdl" );

			//Random head
			if ( m_nHeadModel == RANDOM_BODYGROUP || m_nHeadModel == 15 )
				m_nHeadModel = random->RandomInt( 2, 12 );

			SetBodygroup ( 1, m_nHeadModel );

			// Random neck tags
			SetBodygroup ( 2, random->RandomInt( 0, 2 ) );
		}

	}


	if ( m_fIsTorso )
	{
		SetModel( this->GetTorsoModel() );

		if ( /*FClassnameIs( this, "npc_nh_patient" ) ||*/ FClassnameIs( this, "npc_nh_patient_torso" ) )
		{
			if ( m_nHeadModel == RANDOM_BODYGROUP || m_nHeadModel == 15 )
				m_nHeadModel = random->RandomInt( 2, 14 );
	
			// If head is black, the hands should be black too!
			if ( m_nHeadModel == 2 ) 
				SetBodygroup ( 2, 0 );
			else //If head is white, hands should be white!
				SetBodygroup ( 2, 1 );
	
			//Random cast on right arm.
			if ( random->RandomInt( 0, 9 ) == 9 )
				SetBodygroup( 3, 0 );
			else
				SetBodygroup( 3, 1 );
	
			//Random cast on left arm
			if ( random->RandomInt( 0, 9 ) == 9 )
				SetBodygroup( 4, 0 );
			else
				SetBodygroup( 4, 1 );
		}

		if ( FClassnameIs( this, "npc_nh_doctor" ) || FClassnameIs( this, "npc_nh_doctor_torso" ) )
		{
			if ( m_nHeadModel == RANDOM_BODYGROUP || m_nHeadModel == 13 )
				m_nHeadModel = random->RandomInt( 2, 12 );
		}

		if ( FClassnameIs( this, "npc_nh_janitor" ) || FClassnameIs( this, "npc_nh_janitor_torso" ) )
		{
			if ( m_nHeadModel == RANDOM_BODYGROUP || m_nHeadModel == 15 )
				m_nHeadModel = random->RandomInt( 2, 14 );
		}

		SetBodygroup ( 1, m_nHeadModel );			


		SetHullType( HULL_TINY );
	}
	else
	{
		SetHullType( HULL_HUMAN );
	}

	
	

	SetHullSizeNormal( true );
	SetDefaultEyeOffset();
	SetActivity( ACT_IDLE );

	// hull changed size, notify vphysics
	// UNDONE: Solve this generally, systematically so other
	// NPCs can change size
	if ( lastHull != GetHullType() )
	{
		if ( VPhysicsGetObject() )
		{
			SetupVPhysicsHull();
		}
	}
}

//---------------------------------------------------------
// Classic zombie only uses moan sound if on fire.
//---------------------------------------------------------
void CNH_Zombie::MoanSound( envelopePoint_t *pEnvelope, int iEnvelopeSize )
{
	if( IsOnFire() )
	{
		BaseClass::MoanSound( pEnvelope, iEnvelopeSize );
	}
}

//---------------------------------------------------------
//---------------------------------------------------------
bool CNH_Zombie::ShouldBecomeTorso( const CTakeDamageInfo &info, float flDamageThreshold )
{
	 if ( FClassnameIs( this, "npc_nh_surgeon" ) )
		 return false;

	 if ( FClassnameIs( this, "npc_nh_worker" ) )
		 return false;

	 if ( FClassnameIs( this, "npc_nh_cook" ) )
		 return false;

	 if ( FClassnameIs( this, "npc_nh_patient" ) && m_nBodyModel == 3 )
		 return false;

	 if ( GetBodygroup( 1 ) == 0 || GetBodygroup( 1 ) == 1 )
		 return false; // headless zombies shouldn't become torsos, it's bad for their health ;) (nh2)

	 if ( !m_bCanBeTorso )
		 return false;


	if( IsSlumped() ) 
	{
		// Never break apart a slouched zombie. This is because the most fun
		// slouched zombies to kill are ones sleeping leaning against explosive
		// barrels. If you break them in half in the blast, the force of being
		// so close to the explosion makes the body pieces fly at ridiculous 
		// velocities because the pieces weigh less than the whole.
		return false;
	}

	if (!m_bHeadShot && info.GetDamageType() == DMG_SLASH)
	{
		if(info.GetDamage() < sk_nh_torso_damage_threshold.GetFloat())
			return random->RandomInt(1, 100) <= 25;
		else
			return random->RandomInt(1, 100) <= 35;
	}

	return BaseClass::ShouldBecomeTorso( info, flDamageThreshold );
}

//---------------------------------------------------------
//---------------------------------------------------------
void CNH_Zombie::GatherConditions( void )
{
	BaseClass::GatherConditions();

	static int conditionsToClear[] = 
	{
		COND_BLOCKED_BY_DOOR,
		COND_DOOR_OPENED,
		COND_ZOMBIE_CHARGE_TARGET_MOVED,
	};

	ClearConditions( conditionsToClear, ARRAYSIZE( conditionsToClear ) );

	if ( m_hBlockingDoor == NULL || 
		 ( m_hBlockingDoor->m_toggle_state == TS_AT_TOP || 
		   m_hBlockingDoor->m_toggle_state == TS_GOING_UP )  )
	{
		ClearCondition( COND_BLOCKED_BY_DOOR );
		if ( m_hBlockingDoor != NULL )
		{
			SetCondition( COND_DOOR_OPENED );
			m_hBlockingDoor = NULL;
		}
	}
	else
		SetCondition( COND_BLOCKED_BY_DOOR );

	if ( ConditionInterruptsCurSchedule( COND_ZOMBIE_CHARGE_TARGET_MOVED ) )
	{
		if ( GetNavigator()->IsGoalActive() )
		{
			const float CHARGE_RESET_TOLERANCE = 60.0;
			if ( !GetEnemy() ||
				 ( m_vPositionCharged - GetEnemyLKP()  ).Length() > CHARGE_RESET_TOLERANCE )
			{
				SetCondition( COND_ZOMBIE_CHARGE_TARGET_MOVED );
			}
				 
		}
	}
}

//---------------------------------------------------------
//---------------------------------------------------------

int CNH_Zombie::SelectFailSchedule( int failedSchedule, int failedTask, AI_TaskFailureCode_t taskFailCode )
{
	if ( HasCondition( COND_BLOCKED_BY_DOOR ) && m_hBlockingDoor != NULL )
	{
		ClearCondition( COND_BLOCKED_BY_DOOR );
		if ( m_NextTimeToStartDoorBash.Expired() && failedSchedule != SCHED_ZOMBIE_BASH_DOOR )
			return SCHED_ZOMBIE_BASH_DOOR;
		m_hBlockingDoor = NULL;
	}

	if ( failedSchedule != SCHED_ZOMBIE_CHARGE_ENEMY && 
		 IsPathTaskFailure( taskFailCode ) &&
		 random->RandomInt( 1, 100 ) < 50 )
	{
		return SCHED_ZOMBIE_CHARGE_ENEMY;
	}

	if ( failedSchedule != SCHED_ZOMBIE_WANDER_ANGRILY &&
		 ( failedSchedule == SCHED_TAKE_COVER_FROM_ENEMY || 
		   failedSchedule == SCHED_CHASE_ENEMY_FAILED ) )
	{
		return SCHED_ZOMBIE_WANDER_ANGRILY;
	}

	return BaseClass::SelectFailSchedule( failedSchedule, failedTask, taskFailCode );
}

//---------------------------------------------------------
//---------------------------------------------------------

int CNH_Zombie::TranslateSchedule( int scheduleType )
{
	if ( scheduleType == SCHED_COMBAT_FACE && IsUnreachable( GetEnemy() ) )
		return SCHED_TAKE_COVER_FROM_ENEMY;

	if ( !m_fIsTorso && scheduleType == SCHED_FAIL )
		return SCHED_ZOMBIE_FAIL;

	return BaseClass::TranslateSchedule( scheduleType );
}

//---------------------------------------------------------

Activity CNH_Zombie::NPC_TranslateActivity( Activity newActivity )
{
	newActivity = BaseClass::NPC_TranslateActivity( newActivity );

	if ( newActivity == ACT_RUN )
		return ACT_WALK;
		
	if ( newActivity == ACT_NH_ZOMBIE_TANTRUM )
		return ACT_IDLE;

	return newActivity;
}

//---------------------------------------------------------
//---------------------------------------------------------
void CNH_Zombie::OnStateChange( NPC_STATE OldState, NPC_STATE NewState )
{
	BaseClass::OnStateChange( OldState, NewState );
}

//---------------------------------------------------------
//---------------------------------------------------------

void CNH_Zombie::StartTask( const Task_t *pTask )
{
	switch( pTask->iTask )
	{
	case TASK_ZOMBIE_EXPRESS_ANGER:
		{
			if ( random->RandomInt( 1, 4 ) == 2 )
			{
				SetIdealActivity( (Activity)ACT_NH_ZOMBIE_TANTRUM );
			}
			else
			{
				TaskComplete();
			}

			break;
		}

	case TASK_ZOMBIE_YAW_TO_DOOR:
		{
			AssertMsg( m_hBlockingDoor != NULL, "Expected condition handling to break schedule before landing here" );
			if ( m_hBlockingDoor != NULL )
			{
				GetMotor()->SetIdealYaw( m_flDoorBashYaw );
			}
			TaskComplete();
			break;
		}

	case TASK_ZOMBIE_ATTACK_DOOR:
		{
		 	m_DurationDoorBash.Reset();
			SetIdealActivity( SelectDoorBash() );
			break;
		}

	case TASK_ZOMBIE_CHARGE_ENEMY:
		{
			if ( !GetEnemy() )
				TaskFail( FAIL_NO_ENEMY );
			else if ( GetNavigator()->SetVectorGoalFromTarget( GetEnemy()->GetLocalOrigin() ) )
			{
				m_vPositionCharged = GetEnemy()->GetLocalOrigin();
				TaskComplete();
			}
			else
				TaskFail( FAIL_NO_ROUTE );
			break;
		}

	default:
		BaseClass::StartTask( pTask );
		break;
	}
}

//---------------------------------------------------------
//---------------------------------------------------------

void CNH_Zombie::RunTask( const Task_t *pTask )
{
	switch( pTask->iTask )
	{
	case TASK_ZOMBIE_ATTACK_DOOR:
		{
			if ( IsActivityFinished() )
			{
				if ( m_DurationDoorBash.Expired() )
				{
					TaskComplete();
					m_NextTimeToStartDoorBash.Reset();
				}
				else
					ResetIdealActivity( SelectDoorBash() );
			}
			break;
		}

	case TASK_ZOMBIE_CHARGE_ENEMY:
		{
			break;
		}

	case TASK_ZOMBIE_EXPRESS_ANGER:
		{
			if ( IsActivityFinished() )
			{
				TaskComplete();
			}
			break;
		}

	default:
		BaseClass::RunTask( pTask );
		break;
	}
}

//---------------------------------------------------------
//---------------------------------------------------------

bool CNH_Zombie::OnObstructingDoor( AILocalMoveGoal_t *pMoveGoal, CBaseDoor *pDoor, 
							  float distClear, AIMoveResult_t *pResult )
{
	if ( BaseClass::OnObstructingDoor( pMoveGoal, pDoor, distClear, pResult ) )
	{
		if  ( IsMoveBlocked( *pResult ) && pMoveGoal->directTrace.vHitNormal != vec3_origin )
		{
			m_hBlockingDoor = pDoor;
			m_flDoorBashYaw = UTIL_VecToYaw( pMoveGoal->directTrace.vHitNormal * -1 );	
		}
		return true;
	}

	return false;
}

//---------------------------------------------------------
//---------------------------------------------------------

Activity CNH_Zombie::SelectDoorBash()
{
	if ( random->RandomInt( 1, 3 ) == 1 )
		return ACT_MELEE_ATTACK1;
	return (Activity)ACT_NH_ZOMBIE_WALLPOUND;
}

//---------------------------------------------------------
// Zombies should scream continuously while burning, so long
// as they are alive... but NOT IN GERMANY!
//---------------------------------------------------------
void CNH_Zombie::Ignite( float flFlameLifetime, bool bNPCOnly, float flSize, bool bCalledByLevelDesigner )
{
 	if( !IsOnFire() && IsAlive() )
	{
		BaseClass::Ignite( flFlameLifetime, bNPCOnly, flSize, bCalledByLevelDesigner );

		if ( !UTIL_IsLowViolence() )
		{
			RemoveSpawnFlags( SF_NPC_GAG );

			MoanSound( envNHZombieMoanIgnited, ARRAYSIZE( envNHZombieMoanIgnited ) );

			if ( m_pMoanSound )
			{
				ENVELOPE_CONTROLLER.SoundChangePitch( m_pMoanSound, 120, 1.0 );
				ENVELOPE_CONTROLLER.SoundChangeVolume( m_pMoanSound, 1, 1.0 );
			}
		}
	}
}

//---------------------------------------------------------
// If a zombie stops burning and hasn't died, quiet him down
//---------------------------------------------------------
void CNH_Zombie::Extinguish()
{
	if( m_pMoanSound )
	{
		ENVELOPE_CONTROLLER.SoundChangeVolume( m_pMoanSound, 0, 2.0 );
		ENVELOPE_CONTROLLER.SoundChangePitch( m_pMoanSound, 100, 2.0 );
		m_flNextMoanSound = gpGlobals->curtime + random->RandomFloat( 2.0, 4.0 );
	}

	BaseClass::Extinguish();
}

//---------------------------------------------------------
//---------------------------------------------------------
int CNH_Zombie::OnTakeDamage_Alive( const CTakeDamageInfo &inputInfo )
{
#ifndef HL2_EPISODIC
	if ( inputInfo.GetDamageType() & DMG_BUCKSHOT )
	{
		if( !m_fIsTorso && inputInfo.GetDamage() > (m_iMaxHealth/3) )
		{
			// Always flinch if damaged a lot by buckshot, even if not shot in the head.
			// The reason for making sure we did at least 1/3rd of the zombie's max health
			// is so the zombie doesn't flinch every time the odd shotgun pellet hits them,
			// and so the maximum number of times you'll see a zombie flinch like this is 2.(sjb)
			AddGesture( ACT_GESTURE_FLINCH_HEAD );
		}
	}
#endif // HL2_EPISODIC

	float dmgPos = inputInfo.GetDamagePosition().z - GetAbsOrigin().z;
	if (m_bHeadShot || dmgPos > sk_nh_zombie_head_height.GetFloat())
	{
		bool bShouldExplode = false;

		if (GetBodygroup(1) > 1 && inputInfo.GetDamage() < GetHealth())
		{
			if( inputInfo.GetDamageType() & DMG_SLASH)
			{
				if (inputInfo.GetDamage() < 40)
					bShouldExplode = random->RandomInt( 1, 10 ) == 9;
				else
					bShouldExplode = true;
			}
			else if (inputInfo.GetDamageType() & (DMG_BULLET|DMG_BUCKSHOT))
			{
				if (inputInfo.GetDamage() < 10)
					bShouldExplode = random->RandomInt( 1, 10 ) == 1;
				else if (inputInfo.GetDamage() < 30)
					bShouldExplode = random->RandomInt( 1, 8 ) == 1;
				else
					bShouldExplode = random->RandomInt( 1, 3 ) == 1;
			}
		}
		
		if(bShouldExplode)
		{
			SetBodygroup( 1, random->RandomInt( 0, 1 ) ); // half a head left
			
			EmitSound_t params;
			params.m_pSoundName = "nhzombie.headexplode_jet";
			//params.m_flSoundTime = 0.0;
			CPASAttenuationFilter filter( this, params.m_pSoundName );
			filter.AddAllPlayers();
			EmitSound( filter, entindex(), params );

			DispatchParticleEffect( "blood_advisor_puncture_withdraw", PATTACH_POINT_FOLLOW, dynamic_cast<CBaseEntity*>(this), "eyes" );
			
		}


	}

	if ( inputInfo.GetDamage() >= GetHealth() )
	{
		StopParticleEffects(this);

		if ( m_bHeadShot )
		{
			if (GetBodygroup( 1 ) <= 1) //has lost the head
				SetBodygroup( 1, 1 ); //no head left
			else
				SetBodygroup( 1, random->RandomInt( 0, 1 ) );
			
			EmitSound( "nhzombie.headexplode" );

			//SpawnBlood( Vector vecSpot, const Vector &vecDir, int bloodColor, float flDamage)
			Vector vecSpot, vecDir;
			QAngle absAngles;
			GetAttachment( "eyes", vecSpot, absAngles );
			//vecDir = /// absAngles to vector?

			SpawnBlood( vecSpot, Vector( 0, 0, 0 ), BLOOD_COLOR_RED, inputInfo.GetDamage() );
		}

	}

	return BaseClass::OnTakeDamage_Alive( inputInfo );
}

int CNH_Zombie::OnTakeDamage_Dead( const CTakeDamageInfo &inputInfo )
{
	if ( m_bHeadShot )
	{
		if ( random->RandomInt( 1, 15 ) == 9 )
			{
				if (GetBodygroup( 1 ) <= 1) //has lost the head
					SetBodygroup( 1, 1 ); //no head left
				else
					SetBodygroup( 1, random->RandomInt( 0, 1 ) );
			
				EmitSound( "nhzombie.headexplode" );
			}
	}

	return BaseClass::OnTakeDamage_Dead( inputInfo );
}
//-----------------------------------------------------------------------------
//-----------------------------------------------------------------------------
bool CNH_Zombie::IsHeavyDamage( const CTakeDamageInfo &info )
{
#ifdef HL2_EPISODIC
	if ( info.GetDamageType() & DMG_BUCKSHOT )
	{
		if ( !m_fIsTorso && info.GetDamage() > (m_iMaxHealth/3) )
			return true;
	}

	// Randomly treat all damage as heavy
	if ( info.GetDamageType() & (DMG_BULLET | DMG_BUCKSHOT) )
	{
		// Don't randomly flinch if I'm melee attacking
		if ( !HasCondition(COND_CAN_MELEE_ATTACK1) && (RandomFloat() > 0.5) )
		{
			// Randomly forget I've flinched, so that I'll be forced to play a big flinch
			// If this doesn't happen, it means I may not fully flinch if I recently flinched
			if ( RandomFloat() > 0.75 )
			{
				Forget(bits_MEMORY_FLINCHED);
			}

			return true;
		}
	}
#endif // HL2_EPISODIC

	return BaseClass::IsHeavyDamage(info);
}

//---------------------------------------------------------
//---------------------------------------------------------
#define ZOMBIE_SQUASH_MASS	300.0f  // Anything this heavy or heavier squashes a zombie good. (show special fx)
bool CNH_Zombie::IsSquashed( const CTakeDamageInfo &info )
{
	if( GetHealth() > 0 )
	{
		return false;
	}

	if( info.GetDamageType() & DMG_CRUSH )
	{
		IPhysicsObject *pCrusher = info.GetInflictor()->VPhysicsGetObject();
		if( pCrusher && pCrusher->GetMass() >= ZOMBIE_SQUASH_MASS && info.GetInflictor()->WorldSpaceCenter().z > EyePosition().z )
		{
			// This heuristic detects when a zombie has been squashed from above by a heavy
			// item. Done specifically so we can add gore effects to Ravenholm cartraps.
			// The zombie must take physics damage from a 300+kg object that is centered above its eyes (comes from above)
			return true;
		}
	}

	return false;
}

//---------------------------------------------------------
//---------------------------------------------------------
void CNH_Zombie::BuildScheduleTestBits( void )
{
	BaseClass::BuildScheduleTestBits();

	if( !m_fIsTorso && !IsCurSchedule( SCHED_FLINCH_PHYSICS ) && !m_ActBusyBehavior.IsActive() )
	{
		SetCustomInterruptCondition( COND_PHYSICS_DAMAGE );
	}
}

HeadcrabRelease_t CNH_Zombie::ShouldReleaseHeadcrab( const CTakeDamageInfo &info, float flDamageThreshold )
{
	return RELEASE_NO;
}

float CNH_Zombie::GetHitgroupDamageMultiplier( int iHitGroup, const CTakeDamageInfo &info )
{
	return BaseClass::GetHitgroupDamageMultiplier(iHitGroup, info);
}

//=============================================================================

AI_BEGIN_CUSTOM_NPC( npc_nh_zombie, CNH_Zombie )

	DECLARE_CONDITION( COND_BLOCKED_BY_DOOR )
	DECLARE_CONDITION( COND_DOOR_OPENED )
	DECLARE_CONDITION( COND_ZOMBIE_CHARGE_TARGET_MOVED )

	DECLARE_TASK( TASK_ZOMBIE_EXPRESS_ANGER )
	DECLARE_TASK( TASK_ZOMBIE_YAW_TO_DOOR )
	DECLARE_TASK( TASK_ZOMBIE_ATTACK_DOOR )
	DECLARE_TASK( TASK_ZOMBIE_CHARGE_ENEMY )
	
	DECLARE_ACTIVITY( ACT_NH_ZOMBIE_TANTRUM );
	DECLARE_ACTIVITY( ACT_NH_ZOMBIE_WALLPOUND );

	DEFINE_SCHEDULE
	( 
		SCHED_ZOMBIE_BASH_DOOR,

		"	Tasks"
		"		TASK_SET_ACTIVITY				ACTIVITY:ACT_NH_ZOMBIE_TANTRUM"
		"		TASK_SET_FAIL_SCHEDULE			SCHEDULE:SCHED_TAKE_COVER_FROM_ENEMY"
		"		TASK_ZOMBIE_YAW_TO_DOOR			0"
		"		TASK_FACE_IDEAL					0"
		"		TASK_ZOMBIE_ATTACK_DOOR			0"
		""
		"	Interrupts"
		"		COND_ZOMBIE_RELEASECRAB"
		"		COND_ENEMY_DEAD"
		"		COND_NEW_ENEMY"
		"		COND_DOOR_OPENED"
	)

	DEFINE_SCHEDULE
	(
		SCHED_ZOMBIE_WANDER_ANGRILY,

		"	Tasks"
		"		TASK_WANDER						480240" // 48 units to 240 units.
		"		TASK_WALK_PATH					0"
		"		TASK_WAIT_FOR_MOVEMENT			4"
		""
		"	Interrupts"
		"		COND_ZOMBIE_RELEASECRAB"
		"		COND_ENEMY_DEAD"
		"		COND_NEW_ENEMY"
		"		COND_DOOR_OPENED"
	)

	DEFINE_SCHEDULE
	(
		SCHED_ZOMBIE_CHARGE_ENEMY,


		"	Tasks"
		"		TASK_ZOMBIE_CHARGE_ENEMY		0"
		"		TASK_WALK_PATH					0"
		"		TASK_WAIT_FOR_MOVEMENT			0"
		"		TASK_PLAY_SEQUENCE				ACTIVITY:ACT_NH_ZOMBIE_TANTRUM" /* placeholder until frustration/rage/fence shake animation available */
		""
		"	Interrupts"
		"		COND_ZOMBIE_RELEASECRAB"
		"		COND_ENEMY_DEAD"
		"		COND_NEW_ENEMY"
		"		COND_DOOR_OPENED"
		"		COND_ZOMBIE_CHARGE_TARGET_MOVED"
	)

	DEFINE_SCHEDULE
	(
		SCHED_ZOMBIE_FAIL,

		"	Tasks"
		"		TASK_STOP_MOVING		0"
		"		TASK_SET_ACTIVITY		ACTIVITY:ACT_NH_ZOMBIE_TANTRUM"
		"		TASK_WAIT				1"
		"		TASK_WAIT_PVS			0"
		""
		"	Interrupts"
		"		COND_CAN_RANGE_ATTACK1 "
		"		COND_CAN_RANGE_ATTACK2 "
		"		COND_CAN_MELEE_ATTACK1 "
		"		COND_CAN_MELEE_ATTACK2"
		"		COND_GIVE_WAY"
		"		COND_DOOR_OPENED"
	)

AI_END_CUSTOM_NPC()

//=============================================================================
