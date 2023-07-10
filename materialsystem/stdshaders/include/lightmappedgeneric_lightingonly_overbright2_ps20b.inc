#pragma once
#include "shaderlib/cshader.h"
class lightmappedgeneric_lightingonly_overbright2_ps20b_Static_Index
{
public:
	lightmappedgeneric_lightingonly_overbright2_ps20b_Static_Index( IShaderShadow* pShaderShadow, IMaterialVar** params )
	{
	}

	int GetIndex() const
	{
		return 0;
	}
};

#define shaderStaticTest_lightmappedgeneric_lightingonly_overbright2_ps20b 1


class lightmappedgeneric_lightingonly_overbright2_ps20b_Dynamic_Index
{
public:
	lightmappedgeneric_lightingonly_overbright2_ps20b_Dynamic_Index( IShaderDynamicAPI* pShaderAPI )
	{
	}

	int GetIndex() const
	{
		return 0;
	}
};

#define shaderDynamicTest_lightmappedgeneric_lightingonly_overbright2_ps20b 1

