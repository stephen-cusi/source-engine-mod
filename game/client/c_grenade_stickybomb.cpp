//========= Copyright � 1996-2003, Valve LLC, All rights reserved. ============
//
// Purpose: 
//
//=============================================================================

#include "cbase.h"
#include "basehlcombatweapon_shared.h"

class C_StickyBomb : public C_BaseAnimating
{
	DECLARE_CLASS( C_StickyBomb, C_BaseAnimating );
public:
	C_StickyBomb() {}

	int InternalDrawModel( int flags )
	{
		if ( IsFollowingEntity() && m_boneIndexAttached >= 0 )
		{
			C_BaseAnimating *follow = FindFollowedEntity();
			if ( follow )
			{
				matrix3x4_t boneToWorld, localSpace;
				follow->GetCachedBoneMatrix( m_boneIndexAttached, boneToWorld );
				AngleMatrix( m_boneAngles, m_bonePosition, localSpace );
				ConcatTransforms( boneToWorld, localSpace, GetBoneForWrite(0));

				// UNDONE: For some reason, this won't work without
				// writing the coordinate system back to the origin
				// of the entity
				// I suspect it's a static prop issue
				Vector absOrigin;
				MatrixGetColumn( GetBoneForWrite( 0 ), 3, absOrigin );
				SetAbsOrigin( absOrigin );
			}
		}
		return BaseClass::InternalDrawModel( flags );
	}

	DECLARE_CLIENTCLASS();

private:
	int m_boneIndexAttached;
	Vector m_bonePosition;
	QAngle m_boneAngles;
};

IMPLEMENT_CLIENTCLASS_DT( C_StickyBomb, DT_StickyBomb, CStickyBomb)
	RecvPropInt( RECVINFO(m_boneIndexAttached) ),
	RecvPropVector( RECVINFO(m_bonePosition) ),
	RecvPropFloat(RECVINFO(m_boneAngles[0]) ),
	RecvPropFloat(RECVINFO(m_boneAngles[1]) ),
	RecvPropFloat(RECVINFO(m_boneAngles[2]) ),
END_RECV_TABLE()