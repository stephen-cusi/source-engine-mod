// ALL SKIP STATEMENTS THAT AFFECT THIS SHADER!!!
// defined $HDRTYPE && defined $HDRENABLED && !$HDRTYPE && $HDRENABLED
// defined $PIXELFOGTYPE && defined $WRITEWATERFOGTODESTALPHA && ( $PIXELFOGTYPE != 1 ) && $WRITEWATERFOGTODESTALPHA
// defined $LIGHTING_PREVIEW && defined $HDRTYPE && $LIGHTING_PREVIEW && $HDRTYPE != 0
// defined $LIGHTING_PREVIEW && defined $FASTPATHENVMAPTINT && $LIGHTING_PREVIEW && $FASTPATHENVMAPTINT
// defined $LIGHTING_PREVIEW && defined $FASTPATHENVMAPCONTRAST && $LIGHTING_PREVIEW && $FASTPATHENVMAPCONTRAST
// defined $LIGHTING_PREVIEW && defined $FASTPATH && $LIGHTING_PREVIEW && $FASTPATH
// ($FLASHLIGHT || $FLASHLIGHTSHADOWS) && $LIGHTING_PREVIEW

#pragma once
#include "shaderlib/cshader.h"
class IntroScreenSpaceEffect_ps20b_Static_Index
{
	unsigned int m_nCONVERT_TO_SRGB : 2;
	unsigned int m_nLINEAR_TO_SRGB : 2;
#ifdef _DEBUG
	bool m_bLINEAR_TO_SRGB : 1;
#endif	// _DEBUG
public:
	void SetCONVERT_TO_SRGB( int i )
	{
		Assert( i >= 0 && i <= 1 );
		m_nCONVERT_TO_SRGB = i;
	}

	void SetLINEAR_TO_SRGB( int i )
	{
		Assert( i >= 0 && i <= 1 );
		m_nLINEAR_TO_SRGB = i;
#ifdef _DEBUG
		m_bLINEAR_TO_SRGB = true;
#endif	// _DEBUG
	}

	IntroScreenSpaceEffect_ps20b_Static_Index( IShaderShadow* pShaderShadow, IMaterialVar** params )
	{
		m_nCONVERT_TO_SRGB = g_pHardwareConfig->NeedsShaderSRGBConversion();
		m_nLINEAR_TO_SRGB = 0;
#ifdef _DEBUG
		m_bLINEAR_TO_SRGB = false;
#endif	// _DEBUG
	}

	int GetIndex() const
	{
		Assert( m_bLINEAR_TO_SRGB );
		return ( 10 * m_nCONVERT_TO_SRGB ) + ( 20 * m_nLINEAR_TO_SRGB ) + 0;
	}
};

#define shaderStaticTest_IntroScreenSpaceEffect_ps20b psh_forgot_to_set_static_LINEAR_TO_SRGB


class IntroScreenSpaceEffect_ps20b_Dynamic_Index
{
	unsigned int m_nMODE : 4;
#ifdef _DEBUG
	bool m_bMODE : 1;
#endif	// _DEBUG
public:
	void SetMODE( int i )
	{
		Assert( i >= 0 && i <= 9 );
		m_nMODE = i;
#ifdef _DEBUG
		m_bMODE = true;
#endif	// _DEBUG
	}

	IntroScreenSpaceEffect_ps20b_Dynamic_Index( IShaderDynamicAPI* pShaderAPI )
	{
		m_nMODE = 0;
#ifdef _DEBUG
		m_bMODE = false;
#endif	// _DEBUG
	}

	int GetIndex() const
	{
		Assert( m_bMODE );
		return ( 1 * m_nMODE ) + 0;
	}
};

#define shaderDynamicTest_IntroScreenSpaceEffect_ps20b psh_forgot_to_set_dynamic_MODE

