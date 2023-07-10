#pragma once
#include "shaderlib/cshader.h"
class flesh_interior_blended_pass_dx8_vs20_Static_Index
{
public:
	flesh_interior_blended_pass_dx8_vs20_Static_Index( IShaderShadow* pShaderShadow, IMaterialVar** params )
	{
	}

	int GetIndex() const
	{
		return 0;
	}
};

#define shaderStaticTest_flesh_interior_blended_pass_dx8_vs20 1


class flesh_interior_blended_pass_dx8_vs20_Dynamic_Index
{
public:
	flesh_interior_blended_pass_dx8_vs20_Dynamic_Index( IShaderDynamicAPI* pShaderAPI )
	{
	}

	int GetIndex() const
	{
		return 0;
	}
};

#define shaderDynamicTest_flesh_interior_blended_pass_dx8_vs20 1

