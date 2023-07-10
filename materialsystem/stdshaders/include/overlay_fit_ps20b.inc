#pragma once
#include "shaderlib/cshader.h"
class overlay_fit_ps20b_Static_Index
{
public:
	overlay_fit_ps20b_Static_Index( IShaderShadow* pShaderShadow, IMaterialVar** params )
	{
	}

	int GetIndex() const
	{
		return 0;
	}
};

#define shaderStaticTest_overlay_fit_ps20b 1


class overlay_fit_ps20b_Dynamic_Index
{
public:
	overlay_fit_ps20b_Dynamic_Index( IShaderDynamicAPI* pShaderAPI )
	{
	}

	int GetIndex() const
	{
		return 0;
	}
};

#define shaderDynamicTest_overlay_fit_ps20b 1

