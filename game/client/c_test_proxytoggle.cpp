//========= Copyright Valve Corporation, All rights reserved. ============//
//
// Purpose: 
//
// $NoKeywords: $
//=============================================================================//
#include "cbase.h"
#include "c_baseentity.h"

// memdbgon must be the last include file in a .cpp file!!!
#include "tier0/memdbgon.h"

class C_Test_ProxyToggle_Networkable;
static C_Test_ProxyToggle_Networkable *g_pTestObj = 0;


// ---------------------------------------------------------------------------------------- //
// C_Test_ProxyToggle_Networkable
// ---------------------------------------------------------------------------------------- //

class C_Test_ProxyToggle_Networkable : public C_BaseEntity
{
public:
	DECLARE_CLASS( C_Test_ProxyToggle_Networkable, C_BaseEntity );
	DECLARE_CLIENTCLASS();

			C_Test_ProxyToggle_Networkable()
			{
				g_pTestObj = this;
			}

			~C_Test_ProxyToggle_Networkable()
			{
				g_pTestObj = 0;
			}

	int		m_WithProxy;
};


// ---------------------------------------------------------------------------------------- //
// Datatables.
// ---------------------------------------------------------------------------------------- //

BEGIN_RECV_TABLE_NOBASE( C_Test_ProxyToggle_Networkable, DT_ProxyToggle_ProxiedData )
	RecvPropInt( RECVINFO( m_WithProxy ) )
END_RECV_TABLE()

IMPLEMENT_CLIENTCLASS_DT( C_Test_ProxyToggle_Networkable, DT_ProxyToggle, CTest_ProxyToggle_Networkable )
	RecvPropDataTable( "blah", 0, 0, &REFERENCE_RECV_TABLE( DT_ProxyToggle_ProxiedData ) )
END_RECV_TABLE()







