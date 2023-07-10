#pragma once
#include "shaderlib/cshader.h"
class WaterCheapPerVertexFresnel_vs20_Static_Index
{
public:
	WaterCheapPerVertexFresnel_vs20_Static_Index( IShaderShadow* pShaderShadow, IMaterialVar** params )
	{
	}

	int GetIndex() const
	{
		return 0;
	}
};

#define shaderStaticTest_WaterCheapPerVertexFresnel_vs20 1


class WaterCheapPerVertexFresnel_vs20_Dynamic_Index
{
public:
	WaterCheapPerVertexFresnel_vs20_Dynamic_Index( IShaderDynamicAPI* pShaderAPI )
	{
	}

	int GetIndex() const
	{
		return 0;
	}
};

#define shaderDynamicTest_WaterCheapPerVertexFresnel_vs20 1

