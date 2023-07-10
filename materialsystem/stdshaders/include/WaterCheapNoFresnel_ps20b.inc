#pragma once
#include "shaderlib/cshader.h"
class WaterCheapNoFresnel_ps20b_Static_Index
{
public:
	WaterCheapNoFresnel_ps20b_Static_Index( IShaderShadow* pShaderShadow, IMaterialVar** params )
	{
	}

	int GetIndex() const
	{
		return 0;
	}
};

#define shaderStaticTest_WaterCheapNoFresnel_ps20b 1


class WaterCheapNoFresnel_ps20b_Dynamic_Index
{
public:
	WaterCheapNoFresnel_ps20b_Dynamic_Index( IShaderDynamicAPI* pShaderAPI )
	{
	}

	int GetIndex() const
	{
		return 0;
	}
};

#define shaderDynamicTest_WaterCheapNoFresnel_ps20b 1

