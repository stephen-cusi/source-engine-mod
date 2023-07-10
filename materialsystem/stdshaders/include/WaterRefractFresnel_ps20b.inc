#pragma once
#include "shaderlib/cshader.h"
class WaterRefractFresnel_ps20b_Static_Index
{
public:
	WaterRefractFresnel_ps20b_Static_Index( IShaderShadow* pShaderShadow, IMaterialVar** params )
	{
	}

	int GetIndex() const
	{
		return 0;
	}
};

#define shaderStaticTest_WaterRefractFresnel_ps20b 1


class WaterRefractFresnel_ps20b_Dynamic_Index
{
public:
	WaterRefractFresnel_ps20b_Dynamic_Index( IShaderDynamicAPI* pShaderAPI )
	{
	}

	int GetIndex() const
	{
		return 0;
	}
};

#define shaderDynamicTest_WaterRefractFresnel_ps20b 1

