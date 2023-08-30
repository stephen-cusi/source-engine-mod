//========= Copyright © 1996-2001, Valve LLC, All rights reserved. ============
//
// Purpose:
//
// $NoKeywords: $
//=============================================================================

void LaunchStickyBomb( CBaseCombatCharacter *pOwner, const Vector &origin, const Vector &direction );

class CStickyBomb : public CBaseAnimating
{
	DECLARE_DATADESC();
	DECLARE_CLASS( CStickyBomb, CBaseAnimating );
public:
	CStickyBomb();
	~CStickyBomb();
	void Precache();
	void Spawn();
	void SetVelocity( const Vector &velocity, const AngularImpulse &angVelocity );
	void Detonate();
	void Think();
	void NotifySystemEvent( CBaseEntity *pNotify, notify_system_event_t eventType, const notify_system_event_params_t &params );
	void Touch( CBaseEntity *pOther );
	unsigned int PhysicsSolidMaskForEntity( void ) const { return BaseClass::PhysicsSolidMaskForEntity() | CONTENTS_HITBOX; }

	static void DetonateByOperator( CBaseEntity *pOperator );
	void SetBombOrigin();

	DECLARE_SERVERCLASS();

private:
	static CUtlVector<CStickyBomb *> m_stickyList;

	EHANDLE	m_hOperator;
	CNetworkVar( int, m_boneIndexAttached );
	CNetworkVector( m_bonePosition );
	CNetworkQAngle( m_boneAngles );
public:
	const char *modelName;
};