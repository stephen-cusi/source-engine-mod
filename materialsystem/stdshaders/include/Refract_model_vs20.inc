#pragma once
#include "shaderlib/cshader.h"
class Refract_model_vs20_Static_Index
{
public:
	Refract_model_vs20_Static_Index( IShaderShadow* pShaderShadow, IMaterialVar** params )
	{
	}

	int GetIndex() const
	{
		return 0;
	}
};

#define shaderStaticTest_Refract_model_vs20 1


class Refract_model_vs20_Dynamic_Index
{
public:
	Refract_model_vs20_Dynamic_Index( IShaderDynamicAPI* pShaderAPI )
	{
	}

	int GetIndex() const
	{
		return 0;
	}
};

#define shaderDynamicTest_Refract_model_vs20 1

