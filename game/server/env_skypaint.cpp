//========= Copyright Valve Corporation, All rights reserved. ============//
//
// Purpose: HL2SB (2026-10-07): GMod's painted-sky controller entity
//          (classname "env_skypaint", shipped there as a scripted entity in
//          the base gamemode).  gm_construct and other maps spawn it from the
//          BSP; its networked colour state feeds the "SkyPaint" material
//          proxy that drives the skybox/painted* materials (g_sky shader).
//          Without this entity and the proxy the painted sky materials keep
//          their compiled defaults and the sky renders black.
//
//          Reference behavior (garrysmod base gamemode env_skypaint.lua):
//          the BSP keyvalues land in networked vars through the Lua
//          NetworkVar KeyName map; the defaults below are the Lua
//          SetupDataTables defaults; Think adopts the first env_sun and keeps
//          $sunnormal equal to its direction; the entity is always
//          transmitted so the client proxy can read it anywhere on the map.
//
// $NoKeywords: $
//===========================================================================//

#include "cbase.h"
#include "baseentity.h"
#include "entitylist.h"
#include "sendproxy.h"

// memdbgon must be the last include file in a .cpp file!!!
#include "tier0/memdbgon.h"

#define SKYPAINT_STARTEXTURE_SIZE 256

class CEnvSkyPaint : public CPointEntity
{
public:
	DECLARE_CLASS( CEnvSkyPaint, CPointEntity );
	DECLARE_SERVERCLASS();
	DECLARE_DATADESC();

	CEnvSkyPaint( void );

	virtual void	Spawn( void );
	virtual void	Think( void );
	virtual int		UpdateTransmitState( void );
	virtual bool	KeyValue( const char *szKeyName, const char *szValue );

	// Map inputs, one per networked var (GMod's generic
	// SetNetworkVarsFromMapInput accepts "Set<NetworkVarName>").
	void	InputSetTopColor( inputdata_t &inputdata );
	void	InputSetBottomColor( inputdata_t &inputdata );
	void	InputSetFadeBias( inputdata_t &inputdata );
	void	InputSetHDRScale( inputdata_t &inputdata );
	void	InputSetSunNormal( inputdata_t &inputdata );
	void	InputSetSunColor( inputdata_t &inputdata );
	void	InputSetSunSize( inputdata_t &inputdata );
	void	InputSetDuskColor( inputdata_t &inputdata );
	void	InputSetDuskScale( inputdata_t &inputdata );
	void	InputSetDuskIntensity( inputdata_t &inputdata );
	void	InputSetDrawStars( inputdata_t &inputdata );
	void	InputSetStarTexture( inputdata_t &inputdata );
	void	InputSetStarLayers( inputdata_t &inputdata );
	void	InputSetStarScale( inputdata_t &inputdata );
	void	InputSetStarFade( inputdata_t &inputdata );
	void	InputSetStarSpeed( inputdata_t &inputdata );

private:
	// Reads one FIELD_VECTOR datamap field of another entity by name (the
	// C++ equivalent of GMod's Entity:GetInternalVariable, used to track
	// env_sun's direction).
	static bool	ReadEntityVectorField( CBaseEntity *pEntity, const char *pszName, Vector &vecOut );

	CNetworkVector( m_TopColor );
	CNetworkVector( m_BottomColor );
	CNetworkVar( float, m_flFadeBias );
	CNetworkVar( float, m_flHDRScale );

	CNetworkVector( m_SunNormal );
	CNetworkVector( m_SunColor );
	CNetworkVar( float, m_flSunSize );

	CNetworkVector( m_DuskColor );
	CNetworkVar( float, m_flDuskScale );
	CNetworkVar( float, m_flDuskIntensity );

	CNetworkVar( bool, m_bDrawStars );
	CNetworkVar( int, m_nStarLayers );
	CNetworkVar( float, m_flStarScale );
	CNetworkVar( float, m_flStarFade );
	CNetworkVar( float, m_flStarSpeed );
	CNetworkString( m_szStarTexture, SKYPAINT_STARTEXTURE_SIZE );

	EHANDLE	m_hEnvSun;
	bool	m_bLookedForSun;
};

LINK_ENTITY_TO_CLASS( env_skypaint, CEnvSkyPaint );

IMPLEMENT_SERVERCLASS_ST( CEnvSkyPaint, DT_EnvSkyPaint )
	SendPropVector( SENDINFO( m_TopColor ), 0, SPROP_NOSCALE ),
	SendPropVector( SENDINFO( m_BottomColor ), 0, SPROP_NOSCALE ),
	SendPropFloat( SENDINFO( m_flFadeBias ), 0, SPROP_NOSCALE ),
	SendPropFloat( SENDINFO( m_flHDRScale ), 0, SPROP_NOSCALE ),
	SendPropVector( SENDINFO( m_SunNormal ), 0, SPROP_NOSCALE ),
	SendPropVector( SENDINFO( m_SunColor ), 0, SPROP_NOSCALE ),
	SendPropFloat( SENDINFO( m_flSunSize ), 0, SPROP_NOSCALE ),
	SendPropVector( SENDINFO( m_DuskColor ), 0, SPROP_NOSCALE ),
	SendPropFloat( SENDINFO( m_flDuskScale ), 0, SPROP_NOSCALE ),
	SendPropFloat( SENDINFO( m_flDuskIntensity ), 0, SPROP_NOSCALE ),
	SendPropInt( SENDINFO( m_bDrawStars ), 1, SPROP_UNSIGNED ),
	SendPropInt( SENDINFO( m_nStarLayers ), 3, SPROP_UNSIGNED ),
	SendPropFloat( SENDINFO( m_flStarScale ), 0, SPROP_NOSCALE ),
	SendPropFloat( SENDINFO( m_flStarFade ), 0, SPROP_NOSCALE ),
	SendPropFloat( SENDINFO( m_flStarSpeed ), 0, SPROP_NOSCALE ),
	SendPropString( SENDINFO( m_szStarTexture ) ),
END_SEND_TABLE()

BEGIN_DATADESC( CEnvSkyPaint )
	DEFINE_KEYFIELD( m_TopColor, FIELD_VECTOR, "topcolor" ),
	DEFINE_KEYFIELD( m_BottomColor, FIELD_VECTOR, "bottomcolor" ),
	DEFINE_KEYFIELD( m_flFadeBias, FIELD_FLOAT, "fadebias" ),
	DEFINE_KEYFIELD( m_flHDRScale, FIELD_FLOAT, "hdrscale" ),
	DEFINE_KEYFIELD( m_SunNormal, FIELD_VECTOR, "sunnormal" ),
	DEFINE_KEYFIELD( m_SunColor, FIELD_VECTOR, "suncolor" ),
	DEFINE_KEYFIELD( m_flSunSize, FIELD_FLOAT, "sunsize" ),
	DEFINE_KEYFIELD( m_DuskColor, FIELD_VECTOR, "duskcolor" ),
	DEFINE_KEYFIELD( m_flDuskScale, FIELD_FLOAT, "duskscale" ),
	DEFINE_KEYFIELD( m_flDuskIntensity, FIELD_FLOAT, "duskintensity" ),
	DEFINE_KEYFIELD( m_bDrawStars, FIELD_BOOLEAN, "drawstars" ),
	DEFINE_KEYFIELD( m_nStarLayers, FIELD_INTEGER, "starlayers" ),
	DEFINE_KEYFIELD( m_flStarScale, FIELD_FLOAT, "starscale" ),
	DEFINE_KEYFIELD( m_flStarFade, FIELD_FLOAT, "starfade" ),
	DEFINE_KEYFIELD( m_flStarSpeed, FIELD_FLOAT, "starspeed" ),
	DEFINE_AUTO_ARRAY( m_szStarTexture, FIELD_CHARACTER ),
	DEFINE_FIELD( m_hEnvSun, FIELD_EHANDLE ),
	DEFINE_FIELD( m_bLookedForSun, FIELD_BOOLEAN ),

	DEFINE_INPUTFUNC( FIELD_VECTOR, "SetTopColor", InputSetTopColor ),
	DEFINE_INPUTFUNC( FIELD_VECTOR, "SetBottomColor", InputSetBottomColor ),
	DEFINE_INPUTFUNC( FIELD_FLOAT, "SetFadeBias", InputSetFadeBias ),
	DEFINE_INPUTFUNC( FIELD_FLOAT, "SetHDRScale", InputSetHDRScale ),
	DEFINE_INPUTFUNC( FIELD_VECTOR, "SetSunNormal", InputSetSunNormal ),
	DEFINE_INPUTFUNC( FIELD_VECTOR, "SetSunColor", InputSetSunColor ),
	DEFINE_INPUTFUNC( FIELD_FLOAT, "SetSunSize", InputSetSunSize ),
	DEFINE_INPUTFUNC( FIELD_VECTOR, "SetDuskColor", InputSetDuskColor ),
	DEFINE_INPUTFUNC( FIELD_FLOAT, "SetDuskScale", InputSetDuskScale ),
	DEFINE_INPUTFUNC( FIELD_FLOAT, "SetDuskIntensity", InputSetDuskIntensity ),
	DEFINE_INPUTFUNC( FIELD_BOOLEAN, "SetDrawStars", InputSetDrawStars ),
	DEFINE_INPUTFUNC( FIELD_STRING, "SetStarTexture", InputSetStarTexture ),
	DEFINE_INPUTFUNC( FIELD_INTEGER, "SetStarLayers", InputSetStarLayers ),
	DEFINE_INPUTFUNC( FIELD_FLOAT, "SetStarScale", InputSetStarScale ),
	DEFINE_INPUTFUNC( FIELD_FLOAT, "SetStarFade", InputSetStarFade ),
	DEFINE_INPUTFUNC( FIELD_FLOAT, "SetStarSpeed", InputSetStarSpeed ),
END_DATADESC()

//-----------------------------------------------------------------------------
// Constructor: GMod's env_skypaint.lua SetupDataTables server defaults.  BSP
// keyvalues are applied after this, so a map that sets its own colours wins.
//-----------------------------------------------------------------------------
CEnvSkyPaint::CEnvSkyPaint( void )
{
	m_TopColor.Init( 0.2f, 0.5f, 1.0f );
	m_BottomColor.Init( 0.8f, 1.0f, 1.0f );
	m_flFadeBias = 1.0f;

	m_SunNormal.Init( 0.4f, 0.0f, 0.01f );
	m_SunColor.Init( 0.2f, 0.1f, 0.0f );
	m_flSunSize = 2.0f;

	m_DuskColor.Init( 1.0f, 0.2f, 0.0f );
	m_flDuskScale = 1.0f;
	m_flDuskIntensity = 1.0f;

	m_bDrawStars = true;
	m_nStarLayers = 1;
	m_flStarSpeed = 0.01f;
	m_flStarScale = 0.5f;
	m_flStarFade = 1.5f;
	m_szStarTexture.GetForModify()[0] = '\0';
	Q_strncpy( m_szStarTexture.GetForModify(), "skybox/starfield", SKYPAINT_STARTEXTURE_SIZE );

	m_flHDRScale = 0.66f;

	m_hEnvSun = NULL;
	m_bLookedForSun = false;

	AddEFlags( EFL_FORCE_CHECK_TRANSMIT );
}

void CEnvSkyPaint::Spawn( void )
{
	BaseClass::Spawn();

	SetSolid( SOLID_NONE );
	AddEffects( EF_NODRAW );

	SetThink( &CEnvSkyPaint::Think );
	SetNextThink( gpGlobals->curtime + 0.05f );
}

int CEnvSkyPaint::UpdateTransmitState( void )
{
	// GMod: UpdateTransmitState() == TRANSMIT_ALWAYS -- the sky is global, so
	// the client proxy must see this entity even out of the PVS.
	return SetTransmitState( FL_EDICT_ALWAYS );
}

bool CEnvSkyPaint::KeyValue( const char *szKeyName, const char *szValue )
{
	// The star texture lives in a CNetworkString (string_t + SendPropString
	// would need a custom proxy), so route this one key by hand.  GMod maps
	// it through the NetworkVar KeyName "startexture".
	if ( FStrEq( szKeyName, "startexture" ) )
	{
		Q_strncpy( m_szStarTexture.GetForModify(), szValue, SKYPAINT_STARTEXTURE_SIZE );
		return true;
	}

	// "sunposmethod" is unimplemented in GMod too (TODO in the reference
	// script); everything else falls into the datadesc keyfields.
	return BaseClass::KeyValue( szKeyName, szValue );
}

//-----------------------------------------------------------------------------
// Think: adopt the first env_sun once, then keep the sun normal synced to its
// direction.  GMod queries ents.FindByClass("env_sun") exactly once (the
// "closure only runs once" comment in the reference script) and only refreshes
// while the handle stays valid.
//-----------------------------------------------------------------------------
bool CEnvSkyPaint::ReadEntityVectorField( CBaseEntity *pEntity, const char *pszName, Vector &vecOut )
{
	if ( pEntity == NULL )
		return false;

	for ( datamap_t *pMap = pEntity->GetDataDescMap(); pMap != NULL; pMap = pMap->baseMap )
	{
		for ( int i = 0; i < pMap->dataNumFields; ++i )
		{
			typedescription_t &desc = pMap->dataDesc[ i ];
			if ( desc.fieldName == NULL || Q_stricmp( desc.fieldName, pszName ) != 0 )
				continue;
			if ( desc.fieldType != FIELD_VECTOR || desc.fieldSize != 1 )
				continue;
			if ( desc.flags & FTYPEDESC_PTR )
				continue;

			vecOut = *reinterpret_cast< const Vector * >( reinterpret_cast< const char * >( pEntity ) + desc.fieldOffset[ 0 ] );
			return true;
		}
	}
	return false;
}

void CEnvSkyPaint::Think( void )
{
	if ( !m_bLookedForSun )
	{
		// so this only happens once - even if it fails (GMod parity)
		m_bLookedForSun = true;
		CBaseEntity *pSun = gEntList.FindEntityByClassname( NULL, "env_sun" );
		if ( pSun != NULL )
			m_hEnvSun = pSun;
	}

	if ( m_hEnvSun != NULL )
	{
		Vector vecDirection;
		if ( ReadEntityVectorField( m_hEnvSun, "m_vDirection", vecDirection ) )
		{
			m_SunNormal = vecDirection;
		}
	}

	SetNextThink( gpGlobals->curtime + 0.05f );
}

//-----------------------------------------------------------------------------
// Input handlers.  The variant already parsed the string form per its field
// type, so just copy through.
//-----------------------------------------------------------------------------
void CEnvSkyPaint::InputSetTopColor( inputdata_t &inputdata )
{
	Vector vecValue;
	inputdata.value.Vector3D( vecValue );
	m_TopColor = vecValue;
}

void CEnvSkyPaint::InputSetBottomColor( inputdata_t &inputdata )
{
	Vector vecValue;
	inputdata.value.Vector3D( vecValue );
	m_BottomColor = vecValue;
}

void CEnvSkyPaint::InputSetSunNormal( inputdata_t &inputdata )
{
	Vector vecValue;
	inputdata.value.Vector3D( vecValue );
	m_SunNormal = vecValue;
}

void CEnvSkyPaint::InputSetSunColor( inputdata_t &inputdata )
{
	Vector vecValue;
	inputdata.value.Vector3D( vecValue );
	m_SunColor = vecValue;
}

void CEnvSkyPaint::InputSetDuskColor( inputdata_t &inputdata )
{
	Vector vecValue;
	inputdata.value.Vector3D( vecValue );
	m_DuskColor = vecValue;
}

void CEnvSkyPaint::InputSetFadeBias( inputdata_t &inputdata )		{ m_flFadeBias = inputdata.value.Float(); }
void CEnvSkyPaint::InputSetHDRScale( inputdata_t &inputdata )		{ m_flHDRScale = inputdata.value.Float(); }
void CEnvSkyPaint::InputSetSunSize( inputdata_t &inputdata )		{ m_flSunSize = inputdata.value.Float(); }
void CEnvSkyPaint::InputSetDuskScale( inputdata_t &inputdata )		{ m_flDuskScale = inputdata.value.Float(); }
void CEnvSkyPaint::InputSetDuskIntensity( inputdata_t &inputdata )	{ m_flDuskIntensity = inputdata.value.Float(); }
void CEnvSkyPaint::InputSetDrawStars( inputdata_t &inputdata )		{ m_bDrawStars = inputdata.value.Bool(); }
void CEnvSkyPaint::InputSetStarLayers( inputdata_t &inputdata )		{ m_nStarLayers = inputdata.value.Int(); }
void CEnvSkyPaint::InputSetStarScale( inputdata_t &inputdata )		{ m_flStarScale = inputdata.value.Float(); }
void CEnvSkyPaint::InputSetStarFade( inputdata_t &inputdata )		{ m_flStarFade = inputdata.value.Float(); }
void CEnvSkyPaint::InputSetStarSpeed( inputdata_t &inputdata )		{ m_flStarSpeed = inputdata.value.Float(); }

void CEnvSkyPaint::InputSetStarTexture( inputdata_t &inputdata )
{
	const char *pszTexture = inputdata.value.String();
	if ( pszTexture != NULL )
	{
		Q_strncpy( m_szStarTexture.GetForModify(), pszTexture, SKYPAINT_STARTEXTURE_SIZE );
	}
}
