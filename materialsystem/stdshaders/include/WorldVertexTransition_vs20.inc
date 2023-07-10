#pragma once
#include "shaderlib/cshader.h"
class WorldVertexTransition_vs20_Static_Index
{
public:
	WorldVertexTransition_vs20_Static_Index( IShaderShadow* pShaderShadow, IMaterialVar** params )
	{
	}

	int GetIndex() const
	{
		return 0;
	}
};

#define shaderStaticTest_WorldVertexTransition_vs20 1


class WorldVertexTransition_vs20_Dynamic_Index
{
public:
	WorldVertexTransition_vs20_Dynamic_Index( IShaderDynamicAPI* pShaderAPI )
	{
	}

	int GetIndex() const
	{
		return 0;
	}
};

#define shaderDynamicTest_WorldVertexTransition_vs20 1

