#pragma once
#include "shaderlib/cshader.h"
class WaterCheapOpaque_ps20b_Static_Index
{
public:
	WaterCheapOpaque_ps20b_Static_Index( IShaderShadow* pShaderShadow, IMaterialVar** params )
	{
	}

	int GetIndex() const
	{
		return 0;
	}
};

#define shaderStaticTest_WaterCheapOpaque_ps20b 1


class WaterCheapOpaque_ps20b_Dynamic_Index
{
public:
	WaterCheapOpaque_ps20b_Dynamic_Index( IShaderDynamicAPI* pShaderAPI )
	{
	}

	int GetIndex() const
	{
		return 0;
	}
};

#define shaderDynamicTest_WaterCheapOpaque_ps20b 1

