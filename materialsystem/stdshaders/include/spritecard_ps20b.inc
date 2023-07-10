// ALL SKIP STATEMENTS THAT AFFECT THIS SHADER!!!
// $USEALPHAASRGB && $ADDSELF
// $USEALPHAASRGB && $ADDBASETEXTURE2
// defined $HDRTYPE && defined $HDRENABLED && !$HDRTYPE && $HDRENABLED
// defined $PIXELFOGTYPE && defined $WRITEWATERFOGTODESTALPHA && ( $PIXELFOGTYPE != 1 ) && $WRITEWATERFOGTODESTALPHA
// defined $LIGHTING_PREVIEW && defined $HDRTYPE && $LIGHTING_PREVIEW && $HDRTYPE != 0
// defined $LIGHTING_PREVIEW && defined $FASTPATHENVMAPTINT && $LIGHTING_PREVIEW && $FASTPATHENVMAPTINT
// defined $LIGHTING_PREVIEW && defined $FASTPATHENVMAPCONTRAST && $LIGHTING_PREVIEW && $FASTPATHENVMAPCONTRAST
// defined $LIGHTING_PREVIEW && defined $FASTPATH && $LIGHTING_PREVIEW && $FASTPATH
// ($FLASHLIGHT || $FLASHLIGHTSHADOWS) && $LIGHTING_PREVIEW

#pragma once
#include "shaderlib/cshader.h"
class spritecard_ps20b_Static_Index
{
	unsigned int m_nADDBASETEXTURE2 : 2;
	unsigned int m_nADDSELF : 2;
	unsigned int m_nUSEALPHAASRGB : 2;
#ifdef _DEBUG
	bool m_bADDBASETEXTURE2 : 1;
	bool m_bADDSELF : 1;
	bool m_bUSEALPHAASRGB : 1;
#endif	// _DEBUG
public:
	void SetADDBASETEXTURE2( int i )
	{
		Assert( i >= 0 && i <= 1 );
		m_nADDBASETEXTURE2 = i;
#ifdef _DEBUG
		m_bADDBASETEXTURE2 = true;
#endif	// _DEBUG
	}

	void SetADDSELF( int i )
	{
		Assert( i >= 0 && i <= 1 );
		m_nADDSELF = i;
#ifdef _DEBUG
		m_bADDSELF = true;
#endif	// _DEBUG
	}

	void SetUSEALPHAASRGB( int i )
	{
		Assert( i >= 0 && i <= 1 );
		m_nUSEALPHAASRGB = i;
#ifdef _DEBUG
		m_bUSEALPHAASRGB = true;
#endif	// _DEBUG
	}

	spritecard_ps20b_Static_Index( IShaderShadow* pShaderShadow, IMaterialVar** params )
	{
		m_nADDBASETEXTURE2 = 0;
		m_nADDSELF = 0;
		m_nUSEALPHAASRGB = 0;
#ifdef _DEBUG
		m_bADDBASETEXTURE2 = false;
		m_bADDSELF = false;
		m_bUSEALPHAASRGB = false;
#endif	// _DEBUG
	}

	int GetIndex() const
	{
		Assert( m_bADDBASETEXTURE2 && m_bADDSELF && m_bUSEALPHAASRGB );
		AssertMsg( !( m_nUSEALPHAASRGB && m_nADDSELF ), "Invalid combo combination ( USEALPHAASRGB && ADDSELF )" );
		AssertMsg( !( m_nUSEALPHAASRGB && m_nADDBASETEXTURE2 ), "Invalid combo combination ( USEALPHAASRGB && ADDBASETEXTURE2 )" );
		return ( 1 * m_nADDBASETEXTURE2 ) + ( 2 * m_nADDSELF ) + ( 4 * m_nUSEALPHAASRGB ) + 0;
	}
};

#define shaderStaticTest_spritecard_ps20b psh_forgot_to_set_static_ADDBASETEXTURE2 + psh_forgot_to_set_static_ADDSELF + psh_forgot_to_set_static_USEALPHAASRGB


class spritecard_ps20b_Dynamic_Index
{
public:
	spritecard_ps20b_Dynamic_Index( IShaderDynamicAPI* pShaderAPI )
	{
	}

	int GetIndex() const
	{
		return 0;
	}
};

#define shaderDynamicTest_spritecard_ps20b 1

