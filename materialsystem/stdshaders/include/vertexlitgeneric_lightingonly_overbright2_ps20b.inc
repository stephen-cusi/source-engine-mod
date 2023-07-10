#pragma once
#include "shaderlib/cshader.h"
class vertexlitgeneric_lightingonly_overbright2_ps20b_Static_Index
{
public:
	vertexlitgeneric_lightingonly_overbright2_ps20b_Static_Index( IShaderShadow* pShaderShadow, IMaterialVar** params )
	{
	}

	int GetIndex() const
	{
		return 0;
	}
};

#define shaderStaticTest_vertexlitgeneric_lightingonly_overbright2_ps20b 1


class vertexlitgeneric_lightingonly_overbright2_ps20b_Dynamic_Index
{
public:
	vertexlitgeneric_lightingonly_overbright2_ps20b_Dynamic_Index( IShaderDynamicAPI* pShaderAPI )
	{
	}

	int GetIndex() const
	{
		return 0;
	}
};

#define shaderDynamicTest_vertexlitgeneric_lightingonly_overbright2_ps20b 1

