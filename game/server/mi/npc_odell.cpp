//========= Copyright © 2002, Valve LLC, All rights reserved. ============
//
// Purpose: Odell
//
//=============================================================================

#include "cbase.h"
#include "ai_basenpc.h"
#include "ai_schedule.h"
#include "ai_hull.h"
#include "ai_navigator.h"
#include "ai_motor.h"
#include "npc_talker.h"
#include "soundent.h"
#include "game.h"
#include "npcevent.h"
#include "entitylist.h"
#include "npc_odell.h"
#include "activitylist.h"
#include "soundflags.h"
#include "ai_goalentity.h"
#include "scripted.h"
#include "props.h"

//-----------------------------------------------------------------------------
//
// CNPC_Odell
//

LINK_ENTITY_TO_CLASS( npc_odell, CNPC_Odell );

BEGIN_DATADESC( CNPC_Odell )
	DEFINE_FIELD( welder, FIELD_EHANDLE ),
END_DATADESC()

//=========================================================
// Odell schedules
//=========================================================
enum
{
	// Lead
	SCHED_ODELL_STAND_LOOK = LAST_SHARED_SCHEDULE,

	LAST_ODELL_SCHEDULE,

};

//=========================================================
// Anim Events	
//=========================================================
#define ODELL_AE_REDUCE_BBOX		1	// shrink bounding box (for squishing against walls)
#define ODELL_AE_RESTORE_BBOX		2	// restore bounding box

static int AE_ODELL_WELDER_ATTACHMENT;
//-----------------------------------------------------------------------------

CNPC_Odell::CNPC_Odell()
{
}

bool CNPC_Odell::CreateBehaviors()
{
	AddBehavior( &m_LeadBehavior );
	AddBehavior( &m_FollowBehavior );
	
	return BaseClass::CreateBehaviors();
}

//-----------------------------------------------------------------------------
// Purpose: Initialize the custom schedules
//-----------------------------------------------------------------------------

void CNPC_Odell::Precache( void )
{
	PrecacheModel( "models/odell.mdl" );
	PrecacheModel( "models/props_scart/welding_torch.mdl" );
	BaseClass::Precache();
}
 
//-----------------------------------------------------------------------------
void CNPC_Odell::Spawn( void )
{
	Precache();
	BaseClass::Spawn();
	GetExpresser()->SetOuter( this );
	SetModel( "models/odell.mdl" );

	SetHullType(HULL_HUMAN);
	SetHullSizeNormal();

	CreateWelder();

	SetSolid( SOLID_BBOX );
	AddSolidFlags( FSOLID_NOT_STANDABLE );
	SetMoveType( MOVETYPE_STEP );
	SetBloodColor( BLOOD_COLOR_RED );
	m_iHealth			= 20;
	m_flFieldOfView		= 0.5;
	m_NPCState			= NPC_STATE_NONE;
	CapabilitiesAdd( bits_CAP_TURN_HEAD | bits_CAP_DOORS_GROUP | bits_CAP_MOVE_GROUND | bits_CAP_MOVE_CLIMB );
	CapabilitiesAdd( bits_CAP_USE_WEAPONS );
	CapabilitiesAdd( bits_CAP_ANIMATEDFACE );
	CapabilitiesAdd( bits_CAP_FRIENDLY_DMG_IMMUNE );

	// А вдруг кто нибудь додумается?
	AddEFlags( EFL_NO_DISSOLVE | EFL_NO_MEGAPHYSCANNON_RAGDOLL | EFL_NO_PHYSCANNON_INTERACTION );

	NPCInit();
}

float CNPC_Odell::MaxYawSpeed( void )
{
	if( IsMoving() )
	{
		return 20;
	}

	switch( GetActivity() )
	{
	case ACT_180_LEFT:
		return 30;
		break;

	case ACT_TURN_LEFT:
	case ACT_TURN_RIGHT:
		return 30;
		break;
	default:
		return 15;
		break;
	}
}

Class_T	CNPC_Odell::Classify ( void )
{
	return	CLASS_PLAYER_ALLY_VITAL;
}

int CNPC_Odell::TranslateSchedule( int scheduleType ) 
{
	return BaseClass::TranslateSchedule( scheduleType );
}

void CNPC_Odell::CreateWelder()
{
	welder = (CBaseAnimating*)CreateEntityByName( "prop_dynamic" );
	if ( welder )
	{
		welder->SetModel( "models/props_scart/welding_torch.mdl" );
		welder->SetName( AllocPooledString("ODEL_WELDER") );
		int iAttachment = LookupAttachment( "Welder_Holster" );
		welder->SetParent(this, iAttachment);
		welder->SetOwnerEntity(this);
		welder->SetSolid( SOLID_NONE );
		welder->SetLocalOrigin( Vector( 0, 0, 0 ) );
		welder->SetLocalAngles( QAngle( 0, 0, 0 ) );
		welder->AddSpawnFlags( SF_DYNAMICPROP_NO_VPHYSICS );
		welder->AddEffects( EF_PARENT_ANIMATES );
		welder->Spawn();
	}
}

void CNPC_Odell::HandleAnimEvent( animevent_t *pEvent )
{
	switch( pEvent->event )
	{
	case ODELL_AE_REDUCE_BBOX:
		// for now...
		AddSolidFlags( FSOLID_NOT_SOLID );
		break;

	case ODELL_AE_RESTORE_BBOX:
		// for now...
		RemoveSolidFlags( FSOLID_NOT_SOLID );
		break;

	default:
		BaseClass::HandleAnimEvent( pEvent );
		break;
	}
	
	if ( pEvent->event == AE_ODELL_WELDER_ATTACHMENT )
	{
		int iAttachment = LookupAttachment( pEvent->options );
		welder->SetParent(this, iAttachment);
		// потом
		welder->SetLocalAngles( vec3_angle );
		welder->SetLocalOrigin( vec3_origin );
	}

	if( !stricmp( pEvent->options, "Welder_Holster" ) )
	{
		welder->SetLocalOrigin( Vector( 0, 0, 0 ) );
		welder->SetLocalAngles( QAngle( 0, 0, 0 ) );
	}
}

//-----------------------------------------------------------------------------

int CNPC_Odell::SelectSchedule( void )
{
	BehaviorSelectSchedule();
		
	return BaseClass::SelectSchedule();
}

//-----------------------------------------------------------------------------

bool CNPC_Odell::OnBehaviorChangeStatus( CAI_BehaviorBase *pBehavior, bool fCanFinishSchedule )
{
	if ( pBehavior->CanSelectSchedule() && pBehavior != GetRunningBehavior() )
	{
		if ( pBehavior == &m_LeadBehavior || ( !IsLeading() && pBehavior == &m_FollowBehavior ) )
			return true;
	}
	
	return BaseClass::OnBehaviorChangeStatus( pBehavior, fCanFinishSchedule );
}

//-----------------------------------------------------------------------------

bool CNPC_OdellExpresser::Speak( AIConcept_t concept, const char *pszModifiers )
{
	// @Note (toml 09-12-02): the following code is placeholder until both tag support and scene groups are supported
	if ( pszModifiers && strcmp( concept, TLK_LEAD_CATCHUP ) == 0 )
	{
		if ( strcmp( pszModifiers, "look" ) == 0 )
		{
			const char *ppszValidLooks[] =
			{
				"scene:scenes/borealis/odell_lookit_01.vcd",
				"scene:scenes/borealis/odell_lookit_02.vcd",
			};
			
			pszModifiers = ppszValidLooks[ random->RandomInt( 0, ARRAYSIZE( ppszValidLooks ) - 1 ) ];
		}
	}

	if ( !CanSpeakConcept( concept ) )
		return false;
	
	return CAI_Expresser::Speak( concept, pszModifiers );
}

AI_BEGIN_CUSTOM_NPC( npc_odell, CNPC_Odell )

	DECLARE_USES_SCHEDULE_PROVIDER( CAI_LeadBehavior )
	DECLARE_USES_SCHEDULE_PROVIDER( CAI_FollowBehavior )
	DECLARE_ANIMEVENT( AE_ODELL_WELDER_ATTACHMENT )
AI_END_CUSTOM_NPC()