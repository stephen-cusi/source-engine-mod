#pragma once
#include "shaderlib/cshader.h"
class WorldVertexTransition_ps20b_Static_Index
{
public:
	WorldVertexTransition_ps20b_Static_Index( IShaderShadow* pShaderShadow, IMaterialVar** params )
	{
	}

	int GetIndex() const
	{
		return 0;
	}
};

#define shaderStaticTest_WorldVertexTransition_ps20b 1


class WorldVertexTransition_ps20b_Dynamic_Index
{
public:
	WorldVertexTransition_ps20b_Dynamic_Index( IShaderDynamicAPI* pShaderAPI )
	{
	}

	int GetIndex() const
	{
		return 0;
	}
};

#define shaderDynamicTest_WorldVertexTransition_ps20b 1

