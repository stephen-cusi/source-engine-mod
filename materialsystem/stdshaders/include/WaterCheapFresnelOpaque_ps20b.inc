#pragma once
#include "shaderlib/cshader.h"
class WaterCheapFresnelOpaque_ps20b_Static_Index
{
public:
	WaterCheapFresnelOpaque_ps20b_Static_Index( IShaderShadow* pShaderShadow, IMaterialVar** params )
	{
	}

	int GetIndex() const
	{
		return 0;
	}
};

#define shaderStaticTest_WaterCheapFresnelOpaque_ps20b 1


class WaterCheapFresnelOpaque_ps20b_Dynamic_Index
{
public:
	WaterCheapFresnelOpaque_ps20b_Dynamic_Index( IShaderDynamicAPI* pShaderAPI )
	{
	}

	int GetIndex() const
	{
		return 0;
	}
};

#define shaderDynamicTest_WaterCheapFresnelOpaque_ps20b 1

