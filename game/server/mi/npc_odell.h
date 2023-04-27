//========= Copyright Valve Corporation, All rights reserved. ==========
//
// Purpose:
//
// $NoKeywords: $
//=============================================================================

#ifndef	NPC_ODELL_H
#define	NPC_ODELL_H

#include "ai_baseactor.h"
#include "ai_behavior.h"
#include "ai_behavior_lead.h"
#include "ai_behavior_follow.h"

class CNPC_Odell;

class CNPC_OdellExpresser : public CAI_Expresser
{
public:
	CNPC_OdellExpresser()
	{
	}
	
	virtual bool Speak( AIConcept_t concept, const char *pszModifiers = NULL );
	
private:
};

//-----------------------------------------------------------------------------
// CNPC_Odell
//
//-----------------------------------------------------------------------------


class CNPC_Odell : public CAI_BaseActor
{
	DECLARE_CLASS( CNPC_Odell, CAI_BaseActor );
public:
	CNPC_Odell();

	bool CreateBehaviors();
	void Spawn( void );
	void Precache( void );

	void CreateWelder();
	void WelderHolster();

	Class_T		Classify( void );
	float		MaxYawSpeed( void );
	int			ObjectCaps( void ) { return UsableNPCObjectCaps( BaseClass::ObjectCaps() ); }
	int			TranslateSchedule( int scheduleType );
	int			SelectSchedule ( void );
	bool 		OnBehaviorChangeStatus(  CAI_BehaviorBase *pBehavior, bool fCanFinishSchedule );

	void        HandleAnimEvent( animevent_t *pEvent );

	bool		IsLeading( void ) { return ( GetRunningBehavior() == &m_LeadBehavior && m_LeadBehavior.HasGoal() ); }
	bool		IsFollowing( void ) { return ( GetRunningBehavior() == &m_FollowBehavior && m_FollowBehavior.GetFollowTarget() ); }

private:
	virtual CAI_Expresser *CreateExpresser() { return new CNPC_OdellExpresser; }
	
	EHANDLE	welder;
	DECLARE_DATADESC();
	
	DEFINE_CUSTOM_AI;
	
	CAI_LeadBehavior m_LeadBehavior;
	CAI_FollowBehavior m_FollowBehavior;
};

//-----------------------------------------------------------------------------

#endif	//NPC_ODELL_H