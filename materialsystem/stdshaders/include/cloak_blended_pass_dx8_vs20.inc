#pragma once
#include "shaderlib/cshader.h"
class cloak_blended_pass_dx8_vs20_Static_Index
{
public:
	cloak_blended_pass_dx8_vs20_Static_Index( IShaderShadow* pShaderShadow, IMaterialVar** params )
	{
	}

	int GetIndex() const
	{
		return 0;
	}
};

#define shaderStaticTest_cloak_blended_pass_dx8_vs20 1


class cloak_blended_pass_dx8_vs20_Dynamic_Index
{
public:
	cloak_blended_pass_dx8_vs20_Dynamic_Index( IShaderDynamicAPI* pShaderAPI )
	{
	}

	int GetIndex() const
	{
		return 0;
	}
};

#define shaderDynamicTest_cloak_blended_pass_dx8_vs20 1

