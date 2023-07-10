#pragma once
#include "shaderlib/cshader.h"
class WaterReflect_ps20b_Static_Index
{
public:
	WaterReflect_ps20b_Static_Index( IShaderShadow* pShaderShadow, IMaterialVar** params )
	{
	}

	int GetIndex() const
	{
		return 0;
	}
};

#define shaderStaticTest_WaterReflect_ps20b 1


class WaterReflect_ps20b_Dynamic_Index
{
public:
	WaterReflect_ps20b_Dynamic_Index( IShaderDynamicAPI* pShaderAPI )
	{
	}

	int GetIndex() const
	{
		return 0;
	}
};

#define shaderDynamicTest_WaterReflect_ps20b 1

