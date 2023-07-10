#pragma once
#include "shaderlib/cshader.h"
class WaterCheapNoFresnelOpaque_ps20b_Static_Index
{
public:
	WaterCheapNoFresnelOpaque_ps20b_Static_Index( IShaderShadow* pShaderShadow, IMaterialVar** params )
	{
	}

	int GetIndex() const
	{
		return 0;
	}
};

#define shaderStaticTest_WaterCheapNoFresnelOpaque_ps20b 1


class WaterCheapNoFresnelOpaque_ps20b_Dynamic_Index
{
public:
	WaterCheapNoFresnelOpaque_ps20b_Dynamic_Index( IShaderDynamicAPI* pShaderAPI )
	{
	}

	int GetIndex() const
	{
		return 0;
	}
};

#define shaderDynamicTest_WaterCheapNoFresnelOpaque_ps20b 1

