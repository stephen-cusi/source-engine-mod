#pragma once
#include "shaderlib/cshader.h"
class overlay_fit_vs20_Static_Index
{
public:
	overlay_fit_vs20_Static_Index( IShaderShadow* pShaderShadow, IMaterialVar** params )
	{
	}

	int GetIndex() const
	{
		return 0;
	}
};

#define shaderStaticTest_overlay_fit_vs20 1


class overlay_fit_vs20_Dynamic_Index
{
public:
	overlay_fit_vs20_Dynamic_Index( IShaderDynamicAPI* pShaderAPI )
	{
	}

	int GetIndex() const
	{
		return 0;
	}
};

#define shaderDynamicTest_overlay_fit_vs20 1

