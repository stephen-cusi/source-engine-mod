#pragma once
#include "shaderlib/cshader.h"
class VertexLitGeneric_EnvmappedBumpmapV2_MultByAlpha_ps20b_Static_Index
{
public:
	VertexLitGeneric_EnvmappedBumpmapV2_MultByAlpha_ps20b_Static_Index( IShaderShadow* pShaderShadow, IMaterialVar** params )
	{
	}

	int GetIndex() const
	{
		return 0;
	}
};

#define shaderStaticTest_VertexLitGeneric_EnvmappedBumpmapV2_MultByAlpha_ps20b 1


class VertexLitGeneric_EnvmappedBumpmapV2_MultByAlpha_ps20b_Dynamic_Index
{
public:
	VertexLitGeneric_EnvmappedBumpmapV2_MultByAlpha_ps20b_Dynamic_Index( IShaderDynamicAPI* pShaderAPI )
	{
	}

	int GetIndex() const
	{
		return 0;
	}
};

#define shaderDynamicTest_VertexLitGeneric_EnvmappedBumpmapV2_MultByAlpha_ps20b 1

