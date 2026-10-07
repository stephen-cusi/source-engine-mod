//========= Copyright Valve Corporation, All rights reserved. ============//
//
// Purpose: HL2SB (2026-10-07): client side of GMod's env_skypaint and the
//          "SkyPaint" material proxy.
//
//          GMod ships both halves in Lua: the base gamemode scripted entity
//          env_skypaint.lua (networked colour state, g_SkyPaint global) and
//          lua/matproxy/sky_paint.lua (a matproxy named "SkyPaint" that copies
//          the state into the skybox/painted* materials on every bind).  This
//          port keeps the same observable contract: the map's env_skypaint
//          keyvalues arrive networked, and every bind of a painted sky
//          material refreshes $topcolor/$bottomcolor/$dusk*/$fadebias/
//          $hdrscale/$sunnormal/$suncolor/$sunsize and the star-layer set
//          ($starlayers static combo, $starscale/$starfade/$starpos scroll,
//          $startexture).  Without it those materials keep their compiled
//          defaults (all zeroes) and the painted sky renders black.
//
// $NoKeywords: $
//===========================================================================//

#include "cbase.h"
#include "c_env_skypaint.h"
#include "materialsystem/imaterial.h"
#include "materialsystem/imaterialproxy.h"
#include "materialsystem/imaterialvar.h"
#include "materialsystem/imaterialsystem.h"
#include "texture_group_names.h"

// memdbgon must be the last include file in a .cpp file!!!
#include "tier0/memdbgon.h"

#define SKYPAINT_STARTEXTURE_SIZE 256

//-----------------------------------------------------------------------------
// C_EnvSkyPaint
//-----------------------------------------------------------------------------
class C_EnvSkyPaint : public C_BaseEntity
{
public:
	DECLARE_CLASS( C_EnvSkyPaint, C_BaseEntity );
	DECLARE_CLIENTCLASS();

	C_EnvSkyPaint( void );
	virtual ~C_EnvSkyPaint( void );

	virtual void	OnDataChanged( DataUpdateType_t updateType );
	virtual void	ClientThink( void );

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

private:
	static C_EnvSkyPaint *s_pEnvSkyPaint;

	friend C_EnvSkyPaint *HL2SB_GetEnvSkyPaint( void );
};

C_EnvSkyPaint *C_EnvSkyPaint::s_pEnvSkyPaint = NULL;

IMPLEMENT_CLIENTCLASS_DT( C_EnvSkyPaint, DT_EnvSkyPaint, CEnvSkyPaint )
	RecvPropVector( RECVINFO( m_TopColor ) ),
	RecvPropVector( RECVINFO( m_BottomColor ) ),
	RecvPropFloat( RECVINFO( m_flFadeBias ) ),
	RecvPropFloat( RECVINFO( m_flHDRScale ) ),
	RecvPropVector( RECVINFO( m_SunNormal ) ),
	RecvPropVector( RECVINFO( m_SunColor ) ),
	RecvPropFloat( RECVINFO( m_flSunSize ) ),
	RecvPropVector( RECVINFO( m_DuskColor ) ),
	RecvPropFloat( RECVINFO( m_flDuskScale ) ),
	RecvPropFloat( RECVINFO( m_flDuskIntensity ) ),
	RecvPropInt( RECVINFO( m_bDrawStars ) ),
	RecvPropInt( RECVINFO( m_nStarLayers ) ),
	RecvPropFloat( RECVINFO( m_flStarScale ) ),
	RecvPropFloat( RECVINFO( m_flStarFade ) ),
	RecvPropFloat( RECVINFO( m_flStarSpeed ) ),
	RecvPropString( RECVINFO( m_szStarTexture ) ),
END_RECV_TABLE()

C_EnvSkyPaint::C_EnvSkyPaint( void )
{
	// Compiled defaults; the networked values from the server's entity (which
	// starts from the same GMod defaults) arrive immediately after.
	m_TopColor.Init( 0.2f, 0.5f, 1.0f );
	m_BottomColor.Init( 0.8f, 1.0f, 1.0f );
	m_flFadeBias = 1.0f;
	m_flHDRScale = 0.66f;
	m_SunNormal.Init( 0.4f, 0.0f, 0.01f );
	m_SunColor.Init( 0.2f, 0.1f, 0.0f );
	m_flSunSize = 2.0f;
	m_DuskColor.Init( 1.0f, 0.2f, 0.0f );
	m_flDuskScale = 1.0f;
	m_flDuskIntensity = 1.0f;
	m_bDrawStars = true;
	m_nStarLayers = 1;
	m_flStarScale = 0.5f;
	m_flStarFade = 1.5f;
	m_flStarSpeed = 0.01f;
	m_szStarTexture.GetForModify()[0] = '\0';
}

C_EnvSkyPaint::~C_EnvSkyPaint( void )
{
	if ( s_pEnvSkyPaint == this )
	{
		s_pEnvSkyPaint = NULL;
	}
}

void C_EnvSkyPaint::OnDataChanged( DataUpdateType_t updateType )
{
	BaseClass::OnDataChanged( updateType );

	SetNextClientThink( CLIENT_THINK_ALWAYS );
}

void C_EnvSkyPaint::ClientThink( void )
{
	// GMod Think: "Become the active sky again if we're not already" --
	// g_SkyPaint only moves when the current one goes invalid (the dtor
	// clears it), so the first live entity wins.
	if ( s_pEnvSkyPaint == NULL )
	{
		s_pEnvSkyPaint = this;
	}

	SetNextClientThink( CLIENT_THINK_ALWAYS );
}

C_EnvSkyPaint *HL2SB_GetEnvSkyPaint( void )
{
	return C_EnvSkyPaint::s_pEnvSkyPaint;
}

//-----------------------------------------------------------------------------
// SkyPaint material proxy
//-----------------------------------------------------------------------------
class CSkyPaintProxy : public IMaterialProxy
{
public:
	CSkyPaintProxy( void );
	virtual ~CSkyPaintProxy( void ) { }

	virtual bool	Init( IMaterial *pMaterial, KeyValues *pKeyValues );
	virtual void	OnBind( void *pBindableObject );
	virtual void	Release( void );
	virtual IMaterial *GetMaterial( void ) { return m_pMaterial; }

private:
	IMaterial		*m_pMaterial;
	IMaterialVar	*m_pTopColor;
	IMaterialVar	*m_pBottomColor;
	IMaterialVar	*m_pDuskColor;
	IMaterialVar	*m_pDuskScale;
	IMaterialVar	*m_pDuskIntensity;
	IMaterialVar	*m_pFadeBias;
	IMaterialVar	*m_pHDRScale;
	IMaterialVar	*m_pSunNormal;
	IMaterialVar	*m_pSunColor;
	IMaterialVar	*m_pSunSize;
	IMaterialVar	*m_pStarLayers;
	IMaterialVar	*m_pStarScale;
	IMaterialVar	*m_pStarFade;
	IMaterialVar	*m_pStarPos;
	IMaterialVar	*m_pStarTexture;

	char			m_szAppliedStarTexture[ SKYPAINT_STARTEXTURE_SIZE ];
};

CSkyPaintProxy::CSkyPaintProxy( void ) : m_pMaterial( NULL ),
	m_pTopColor( NULL ), m_pBottomColor( NULL ), m_pDuskColor( NULL ),
	m_pDuskScale( NULL ), m_pDuskIntensity( NULL ), m_pFadeBias( NULL ),
	m_pHDRScale( NULL ), m_pSunNormal( NULL ), m_pSunColor( NULL ),
	m_pSunSize( NULL ), m_pStarLayers( NULL ), m_pStarScale( NULL ),
	m_pStarFade( NULL ), m_pStarPos( NULL ), m_pStarTexture( NULL )
{
	m_szAppliedStarTexture[0] = '\0';
}

bool CSkyPaintProxy::Init( IMaterial *pMaterial, KeyValues *pKeyValues )
{
	m_pMaterial = pMaterial;
	if ( m_pMaterial == NULL )
		return false;

	// Every var is optional; a painted sky material that misses one just
	// keeps that channel at its own default (GMod's proxy writes through
	// IMaterial:Set* which no-ops the same way on unknown vars).
	bool bFound = false;
	m_pTopColor		= m_pMaterial->FindVar( "$topcolor", &bFound, false );
	m_pBottomColor	= m_pMaterial->FindVar( "$bottomcolor", &bFound, false );
	m_pDuskColor	= m_pMaterial->FindVar( "$duskcolor", &bFound, false );
	m_pDuskScale	= m_pMaterial->FindVar( "$duskscale", &bFound, false );
	m_pDuskIntensity = m_pMaterial->FindVar( "$duskintensity", &bFound, false );
	m_pFadeBias		= m_pMaterial->FindVar( "$fadebias", &bFound, false );
	m_pHDRScale		= m_pMaterial->FindVar( "$hdrscale", &bFound, false );
	m_pSunNormal	= m_pMaterial->FindVar( "$sunnormal", &bFound, false );
	m_pSunColor		= m_pMaterial->FindVar( "$suncolor", &bFound, false );
	m_pSunSize		= m_pMaterial->FindVar( "$sunsize", &bFound, false );
	m_pStarLayers	= m_pMaterial->FindVar( "$starlayers", &bFound, false );
	m_pStarScale	= m_pMaterial->FindVar( "$starscale", &bFound, false );
	m_pStarFade		= m_pMaterial->FindVar( "$starfade", &bFound, false );
	m_pStarPos		= m_pMaterial->FindVar( "$starpos", &bFound, false );
	m_pStarTexture	= m_pMaterial->FindVar( "$startexture", &bFound, false );
	return true;
}

void CSkyPaintProxy::Release( void )
{
	m_pMaterial = NULL;
}

void CSkyPaintProxy::OnBind( void *pBindableObject )
{
	if ( m_pMaterial == NULL )
		return;

	C_EnvSkyPaint *pSky = HL2SB_GetEnvSkyPaint();
	if ( pSky == NULL )
		return;

	if ( m_pTopColor )
		m_pTopColor->SetVecValue( pSky->m_TopColor.Get().x, pSky->m_TopColor.Get().y, pSky->m_TopColor.Get().z );
	if ( m_pBottomColor )
		m_pBottomColor->SetVecValue( pSky->m_BottomColor.Get().x, pSky->m_BottomColor.Get().y, pSky->m_BottomColor.Get().z );
	if ( m_pDuskColor )
		m_pDuskColor->SetVecValue( pSky->m_DuskColor.Get().x, pSky->m_DuskColor.Get().y, pSky->m_DuskColor.Get().z );
	if ( m_pDuskScale )
		m_pDuskScale->SetFloatValue( pSky->m_flDuskScale );
	if ( m_pDuskIntensity )
		m_pDuskIntensity->SetFloatValue( pSky->m_flDuskIntensity );
	if ( m_pFadeBias )
		m_pFadeBias->SetFloatValue( pSky->m_flFadeBias );
	if ( m_pHDRScale )
		m_pHDRScale->SetFloatValue( pSky->m_flHDRScale );

	if ( m_pSunNormal )
		m_pSunNormal->SetVecValue( pSky->m_SunNormal.Get().x, pSky->m_SunNormal.Get().y, pSky->m_SunNormal.Get().z );
	if ( m_pSunColor )
		m_pSunColor->SetVecValue( pSky->m_SunColor.Get().x, pSky->m_SunColor.Get().y, pSky->m_SunColor.Get().z );
	if ( m_pSunSize )
		m_pSunSize->SetFloatValue( pSky->m_flSunSize );

	// GMod: "if ( not skyPaint:GetDTBool(0) ) then return mat:SetInt( $STARLAYERS, 0 ) end"
	// -- with stars off only the layer count is touched, the rest of the
	// star state stays as-is.
	if ( !pSky->m_bDrawStars )
	{
		if ( m_pStarLayers )
			m_pStarLayers->SetIntValue( 0 );
		return;
	}

	if ( m_pStarLayers )
		m_pStarLayers->SetIntValue( pSky->m_nStarLayers );

	if ( m_pStarScale )
		m_pStarScale->SetFloatValue( pSky->m_flStarScale );
	if ( m_pStarFade )
		m_pStarFade->SetFloatValue( pSky->m_flStarFade );
	if ( m_pStarPos )
		m_pStarPos->SetFloatValue( pSky->m_flStarSpeed * Plat_FloatTime() );

	if ( m_pStarTexture )
	{
		const char *pszTexture = pSky->m_szStarTexture.Get();
		if ( pszTexture[0] != '\0' && Q_stricmp( pszTexture, m_szAppliedStarTexture ) != 0 )
		{
			// Only resolve on change: FindTexture inside a bind is legal but
			// not free, and the shader re-reads the var every draw anyway.
			ITexture *pTexture = materials->FindTexture( pszTexture, TEXTURE_GROUP_SKYBOX );
			if ( pTexture != NULL )
			{
				m_pStarTexture->SetTextureValue( pTexture );
				Q_strncpy( m_szAppliedStarTexture, pszTexture, SKYPAINT_STARTEXTURE_SIZE );
			}
		}
	}
}

IMaterialProxy *HL2SB_CreateSkyPaintProxy( void )
{
	return new CSkyPaintProxy;
}
