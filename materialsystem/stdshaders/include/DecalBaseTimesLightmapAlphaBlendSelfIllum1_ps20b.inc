#pragma once
#include "shaderlib/cshader.h"
class DecalBaseTimesLightmapAlphaBlendSelfIllum1_ps20b_Static_Index
{
public:
	DecalBaseTimesLightmapAlphaBlendSelfIllum1_ps20b_Static_Index( IShaderShadow* pShaderShadow, IMaterialVar** params )
	{
	}

	int GetIndex() const
	{
		return 0;
	}
};

#define shaderStaticTest_DecalBaseTimesLightmapAlphaBlendSelfIllum1_ps20b 1


class DecalBaseTimesLightmapAlphaBlendSelfIllum1_ps20b_Dynamic_Index
{
public:
	DecalBaseTimesLightmapAlphaBlendSelfIllum1_ps20b_Dynamic_Index( IShaderDynamicAPI* pShaderAPI )
	{
	}

	int GetIndex() const
	{
		return 0;
	}
};

#define shaderDynamicTest_DecalBaseTimesLightmapAlphaBlendSelfIllum1_ps20b 1

