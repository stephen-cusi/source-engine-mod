#pragma once
#include "shaderlib/cshader.h"
class WaterRefract_ps20b_Static_Index
{
public:
	WaterRefract_ps20b_Static_Index( IShaderShadow* pShaderShadow, IMaterialVar** params )
	{
	}

	int GetIndex() const
	{
		return 0;
	}
};

#define shaderStaticTest_WaterRefract_ps20b 1


class WaterRefract_ps20b_Dynamic_Index
{
public:
	WaterRefract_ps20b_Dynamic_Index( IShaderDynamicAPI* pShaderAPI )
	{
	}

	int GetIndex() const
	{
		return 0;
	}
};

#define shaderDynamicTest_WaterRefract_ps20b 1

