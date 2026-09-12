//========= Copyright Valve Corporation, All rights reserved. ============//
//
// Purpose: 
//
// $Workfile:     $
// $Date:         $
// $NoKeywords: $
//===========================================================================//
#include "cbase.h"
#include "c_basetempentity.h"
#include "networkstringtable_clientdll.h"
#include "effect_dispatch_data.h"
#include "c_te_effect_dispatch.h"
#include "tier1/KeyValues.h"
#include "toolframework_client.h"
#include "tier0/vprof.h"

// HL2SB: GMod's lua/effects/*.lua runtime.  Declared here rather than included
// because this file is compiled from client_hl2mp.vpc, which does not carry
// game/client/lua on its include path (client_lua.vpc does).
// Implementation: game/client/lua/lua_effects.cpp
bool HL2SB_CreateLuaEffect( const char *pszName, const CEffectData &data );

// memdbgon must be the last include file in a .cpp file!!!
#include "tier0/memdbgon.h"

//-----------------------------------------------------------------------------
// CClientEffectRegistration registration
//-----------------------------------------------------------------------------

CClientEffectRegistration *CClientEffectRegistration::s_pHead = NULL;

CClientEffectRegistration::CClientEffectRegistration( const char *pEffectName, ClientEffectCallback fn )
{
	m_pEffectName = pEffectName;
	m_pFunction = fn;
	m_pNext = s_pHead;
	s_pHead = this;
}


//-----------------------------------------------------------------------------
// Purpose: EffectDispatch TE
//-----------------------------------------------------------------------------
class C_TEEffectDispatch : public C_BaseTempEntity
{
public:
	DECLARE_CLASS( C_TEEffectDispatch, C_BaseTempEntity );
	DECLARE_CLIENTCLASS();

					C_TEEffectDispatch( void );
	virtual			~C_TEEffectDispatch( void );

	virtual void	PostDataUpdate( DataUpdateType_t updateType );

public:
	CEffectData m_EffectData;
};

//-----------------------------------------------------------------------------
// Purpose: 
//-----------------------------------------------------------------------------
C_TEEffectDispatch::C_TEEffectDispatch( void )
{
}

//-----------------------------------------------------------------------------
// Purpose: 
//-----------------------------------------------------------------------------
C_TEEffectDispatch::~C_TEEffectDispatch( void )
{
}

//-----------------------------------------------------------------------------
// Purpose: 
//-----------------------------------------------------------------------------
void DispatchEffectToCallback( const char *pEffectName, const CEffectData &m_EffectData )
{
	// HL2SB: GMod's lua/effects/<name>.lua come first.  Every effect name ends up
	// here -- server-sent ones via TE_DispatchEffect below, client-side ones via
	// DispatchEffect()/TE_DispatchEffect -- so one hook covers both paths, and a
	// scripted effect shadows an engine callback of the same name exactly as it
	// does in GMod.
	if ( HL2SB_CreateLuaEffect( pEffectName, m_EffectData ) )
	{
		return;
	}

	// HL2SB debug: one-shot per effect name so we can see whether a custom
	// tracer (rb655_nyan_tracer) is reaching the client at all.
	{
		static char s_szSeen[8][128];
		static int s_nSeen = 0;
		bool bNew = true;
		for ( int i = 0; i < s_nSeen; ++i )
		{
			if ( !Q_stricmp( s_szSeen[i], pEffectName ) ) { bNew = false; break; }
		}
		if ( bNew && s_nSeen < 8 )
		{
			Q_strncpy( s_szSeen[s_nSeen], pEffectName, sizeof( s_szSeen[0] ) );
			++s_nSeen;
			DevMsg( "[HL2SB] DispatchEffect '%s' -> no Lua effect, trying engine callbacks\n", pEffectName );
		}
	}

	// Look through all the registered callbacks
	for ( CClientEffectRegistration *pReg = CClientEffectRegistration::s_pHead; pReg; pReg = pReg->m_pNext )
	{
		// If the name matches, call it
		if ( Q_stricmp( pReg->m_pEffectName, pEffectName ) == 0 )
		{
			pReg->m_pFunction( m_EffectData );
			return;
		}
	}

	DevMsg("DispatchEffect: effect '%s' not found on client\n", pEffectName );

}


//-----------------------------------------------------------------------------
// Record effects
//-----------------------------------------------------------------------------
static void RecordEffect( const char *pEffectName, const CEffectData &data )
{
	if ( !ToolsEnabled() )
		return;

	if ( clienttools->IsInRecordingMode() && ( (data.m_fFlags & EFFECTDATA_NO_RECORD) == 0 ) )
	{
		KeyValues *msg = new KeyValues( "TempEntity" );

		const char *pSurfacePropName = physprops->GetPropName( data.m_nSurfaceProp );

		char pName[1024];
		Q_snprintf( pName, sizeof(pName), "TE_DispatchEffect %s %s", pEffectName, pSurfacePropName );

 		msg->SetInt( "te", TE_DISPATCH_EFFECT );
 		msg->SetString( "name", pName );
		msg->SetFloat( "time", gpGlobals->curtime );
		msg->SetFloat( "originx", data.m_vOrigin.x );
		msg->SetFloat( "originy", data.m_vOrigin.y );
		msg->SetFloat( "originz", data.m_vOrigin.z );
		msg->SetFloat( "startx", data.m_vStart.x );
		msg->SetFloat( "starty", data.m_vStart.y );
		msg->SetFloat( "startz", data.m_vStart.z );
		msg->SetFloat( "normalx", data.m_vNormal.x );
		msg->SetFloat( "normaly", data.m_vNormal.y );
		msg->SetFloat( "normalz", data.m_vNormal.z );
		msg->SetFloat( "anglesx", data.m_vAngles.x );
		msg->SetFloat( "anglesy", data.m_vAngles.y );
		msg->SetFloat( "anglesz", data.m_vAngles.z );
		msg->SetInt( "flags", data.m_fFlags );
		msg->SetFloat( "scale", data.m_flScale );
		msg->SetFloat( "magnitude", data.m_flMagnitude );
		msg->SetFloat( "radius", data.m_flRadius );
		msg->SetString( "surfaceprop", pSurfacePropName );
		msg->SetInt( "color", data.m_nColor );
		msg->SetInt( "damagetype", data.m_nDamageType );
		msg->SetInt( "hitbox", data.m_nHitBox );
 		msg->SetString( "effectname", pEffectName );

		// FIXME: Need to write the attachment name here
 		msg->SetInt( "attachmentindex", data.m_nAttachmentIndex );

		// NOTE: Ptrs are our way of indicating it's an entindex
		msg->SetPtr( "entindex", (void*)(intp)data.entindex() );

		ToolFramework_PostToolMessage( HTOOLHANDLE_INVALID, msg );
		msg->deleteThis();
	}
}


//-----------------------------------------------------------------------------
// Purpose: 
//-----------------------------------------------------------------------------
void C_TEEffectDispatch::PostDataUpdate( DataUpdateType_t updateType )
{
	VPROF( "C_TEEffectDispatch::PostDataUpdate" );

	// Find the effect name.
	const char *pEffectName = g_StringTableEffectDispatch->GetString( m_EffectData.GetEffectNameIndex() );
	if ( pEffectName )
	{
		DispatchEffectToCallback( pEffectName, m_EffectData );
		RecordEffect( pEffectName, m_EffectData );
	}
}


IMPLEMENT_CLIENTCLASS_EVENT_DT( C_TEEffectDispatch, DT_TEEffectDispatch, CTEEffectDispatch )
	
	RecvPropDataTable( RECVINFO_DT( m_EffectData ), 0, &REFERENCE_RECV_TABLE( DT_EffectData ) )
			
END_RECV_TABLE()

//-----------------------------------------------------------------------------
// Purpose: Clientside version
//-----------------------------------------------------------------------------
void TE_DispatchEffect( IRecipientFilter& filter, float delay, const Vector &pos, const char *pName, const CEffectData &data )
{
	DispatchEffectToCallback( pName, data );
	RecordEffect( pName, data );
}

// Client version of dispatch effect, for predicted weapons
void DispatchEffect( const char *pName, const CEffectData &data )
{
	CPASFilter filter( data.m_vOrigin );
	te->DispatchEffect( filter, 0.0, data.m_vOrigin, pName, data );
}


//-----------------------------------------------------------------------------
// Playback
//-----------------------------------------------------------------------------
void TE_DispatchEffect( IRecipientFilter& filter, float delay, KeyValues *pKeyValues )
{
	CEffectData data;
	data.m_nMaterial = 0;
		  
	data.m_vOrigin.x = pKeyValues->GetFloat( "originx" );
	data.m_vOrigin.y = pKeyValues->GetFloat( "originy" );
	data.m_vOrigin.z = pKeyValues->GetFloat( "originz" );
	data.m_vStart.x = pKeyValues->GetFloat( "startx" );
	data.m_vStart.y = pKeyValues->GetFloat( "starty" );
	data.m_vStart.z = pKeyValues->GetFloat( "startz" );
	data.m_vNormal.x = pKeyValues->GetFloat( "normalx" );
	data.m_vNormal.y = pKeyValues->GetFloat( "normaly" );
	data.m_vNormal.z = pKeyValues->GetFloat( "normalz" );
	data.m_vAngles.x = pKeyValues->GetFloat( "anglesx" );
	data.m_vAngles.y = pKeyValues->GetFloat( "anglesy" );
	data.m_vAngles.z = pKeyValues->GetFloat( "anglesz" );
	data.m_fFlags = pKeyValues->GetInt( "flags" );
	data.m_flScale = pKeyValues->GetFloat( "scale" );
	data.m_flMagnitude = pKeyValues->GetFloat( "magnitude" );
	data.m_flRadius = pKeyValues->GetFloat( "radius" );
	const char *pSurfaceProp = pKeyValues->GetString( "surfaceprop" );
	data.m_nSurfaceProp = physprops->GetSurfaceIndex( pSurfaceProp );
	data.m_nDamageType = pKeyValues->GetInt( "damagetype" );
	data.m_nHitBox = pKeyValues->GetInt( "hitbox" );
	data.m_nColor = pKeyValues->GetInt( "color" );
	data.m_nAttachmentIndex = pKeyValues->GetInt( "attachmentindex" );

	// NOTE: Ptrs are our way of indicating it's an entindex
	ClientEntityHandle_t hWorld = ClientEntityList().EntIndexToHandle( 0 );
	data.m_hEntity = (intp)pKeyValues->GetPtr( "entindex", (void*)(intp)hWorld.ToInt() );

	const char *pEffectName = pKeyValues->GetString( "effectname" );

	TE_DispatchEffect( filter, 0.0f, data.m_vOrigin, pEffectName, data );
}
