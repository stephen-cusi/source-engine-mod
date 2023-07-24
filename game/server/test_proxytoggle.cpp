//========= Copyright Valve Corporation, All rights reserved. ============//
//
// Purpose: 
//
// $NoKeywords: $
//=============================================================================//

#include "cbase.h"
#include "baseentity.h"

// memdbgon must be the last include file in a .cpp file!!!
#include "tier0/memdbgon.h"

class CTest_ProxyToggle_Networkable;
static CTest_ProxyToggle_Networkable *g_pTestObj = 0;
static bool g_bEnableProxy = true;


// ---------------------------------------------------------------------------------------- //
// CTest_ProxyToggle_Networkable
// ---------------------------------------------------------------------------------------- //

class CTest_ProxyToggle_Networkable : public CBaseEntity
{
public:
	DECLARE_CLASS( CTest_ProxyToggle_Networkable, CBaseEntity );
	DECLARE_SERVERCLASS();

			CTest_ProxyToggle_Networkable()
			{
				m_WithProxy = 1241;
				g_pTestObj = this;
			}

			~CTest_ProxyToggle_Networkable()
			{
				g_pTestObj = NULL;
			}

	int UpdateTransmitState()
	{
		return SetTransmitState( FL_EDICT_ALWAYS );
	}

	CNetworkVar( int, m_WithProxy );
};

void* SendProxy_TestProxyToggle( const SendProp *pProp, const void *pStructBase, const void *pData, CSendProxyRecipients *pRecipients, int objectID )
{
	if ( g_bEnableProxy )
	{
		return (void*)pData;
	}
	else
	{
		pRecipients->ClearAllRecipients();
		return NULL;
	}
}
REGISTER_SEND_PROXY_NON_MODIFIED_POINTER( SendProxy_TestProxyToggle );


// ---------------------------------------------------------------------------------------- //
// Datatables.
// ---------------------------------------------------------------------------------------- //

LINK_ENTITY_TO_CLASS( test_proxytoggle, CTest_ProxyToggle_Networkable );

BEGIN_SEND_TABLE_NOBASE( CTest_ProxyToggle_Networkable, DT_ProxyToggle_ProxiedData )
	SendPropInt( SENDINFO( m_WithProxy ) )
END_SEND_TABLE()

IMPLEMENT_SERVERCLASS_ST( CTest_ProxyToggle_Networkable, DT_ProxyToggle )
	SendPropDataTable( "blah", 0, &REFERENCE_SEND_TABLE( DT_ProxyToggle_ProxiedData ), SendProxy_TestProxyToggle )
END_SEND_TABLE()

