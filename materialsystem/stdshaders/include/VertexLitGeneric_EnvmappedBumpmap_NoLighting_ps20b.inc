#pragma once
#include "shaderlib/cshader.h"
class VertexLitGeneric_EnvmappedBumpmap_NoLighting_ps20b_Static_Index
{
public:
	VertexLitGeneric_EnvmappedBumpmap_NoLighting_ps20b_Static_Index( IShaderShadow* pShaderShadow, IMaterialVar** params )
	{
	}

	int GetIndex() const
	{
		return 0;
	}
};

#define shaderStaticTest_VertexLitGeneric_EnvmappedBumpmap_NoLighting_ps20b 1


class VertexLitGeneric_EnvmappedBumpmap_NoLighting_ps20b_Dynamic_Index
{
public:
	VertexLitGeneric_EnvmappedBumpmap_NoLighting_ps20b_Dynamic_Index( IShaderDynamicAPI* pShaderAPI )
	{
	}

	int GetIndex() const
	{
		return 0;
	}
};

#define shaderDynamicTest_VertexLitGeneric_EnvmappedBumpmap_NoLighting_ps20b 1

