#pragma once
#include "shaderlib/cshader.h"
class WaterCheapFresnel_ps20b_Static_Index
{
public:
	WaterCheapFresnel_ps20b_Static_Index( IShaderShadow* pShaderShadow, IMaterialVar** params )
	{
	}

	int GetIndex() const
	{
		return 0;
	}
};

#define shaderStaticTest_WaterCheapFresnel_ps20b 1


class WaterCheapFresnel_ps20b_Dynamic_Index
{
public:
	WaterCheapFresnel_ps20b_Dynamic_Index( IShaderDynamicAPI* pShaderAPI )
	{
	}

	int GetIndex() const
	{
		return 0;
	}
};

#define shaderDynamicTest_WaterCheapFresnel_ps20b 1

