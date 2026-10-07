//========= Copyright Valve Corporation, All rights reserved. ============//
//
// Purpose: HL2SB (2026-10-07): GMod private screenspace sharpen, the core
//          of DrawSharpen( ) from lua/postprocess/sharpen.lua
//          (materials/pp/sharpen.vmt).  Reference behavior: two diagonal
//          taps at +/- $distance, output = center + $contrast * ( tapPlus -
//          tapMinus ), depth state off, sRGB read/write off.  Compiled
//          programs ship verbatim as shaders/fxc/g_sharpen_{vs20,ps20}.vcs.
//
// $NoKeywords: $
//===========================================================================//

#include "BaseVSShader.h"

BEGIN_VS_SHADER_FLAGS( g_sharpen, "GMod screenspace sharpen", SHADER_NOT_EDITABLE )
	BEGIN_SHADER_PARAMS
		SHADER_PARAM( FBTEXTURE, SHADER_PARAM_TYPE_TEXTURE, "_rt_FullFrameFB", "FBTEXTURE" )
		SHADER_PARAM( CONTRAST, SHADER_PARAM_TYPE_FLOAT, "1.0", "contrast" )
		SHADER_PARAM( DISTANCE, SHADER_PARAM_TYPE_FLOAT, "1.0", "distance" )
	END_SHADER_PARAMS

	SHADER_FALLBACK
	{
		return 0;
	}

	SHADER_INIT
	{
		LoadTexture( FBTEXTURE );
	}

	SHADER_DRAW
	{
		SHADOW_STATE
		{
			pShaderShadow->EnableTexture( SHADER_SAMPLER0, true );
			pShaderShadow->EnableSRGBRead( SHADER_SAMPLER0, false );
			pShaderShadow->EnableSRGBWrite( false );
			pShaderShadow->EnableDepthWrites( false );

			int fmt = VERTEX_POSITION;
			pShaderShadow->VertexShaderVertexFormat( fmt, 1, 0, 0 );

			pShaderShadow->SetVertexShader( "g_sharpen_vs20", 0 );
			pShaderShadow->SetPixelShader( "g_sharpen_ps20", 0 );
		}
		DYNAMIC_STATE
		{
			// c0 = $contrast, c1 = $distance (ps reads .x only).
			float flContrast[4] = { params[CONTRAST]->GetFloatValue(), 0.0f, 0.0f, 0.0f };
			float flDistance[4] = { params[DISTANCE]->GetFloatValue(), 0.0f, 0.0f, 0.0f };
			pShaderAPI->SetPixelShaderConstant( 0, flContrast, 1 );
			pShaderAPI->SetPixelShaderConstant( 1, flDistance, 1 );

			BindTexture( SHADER_SAMPLER0, FBTEXTURE, -1 );
			pShaderAPI->SetVertexShaderIndex( 0 );
		}
		Draw();
	}
END_SHADER
