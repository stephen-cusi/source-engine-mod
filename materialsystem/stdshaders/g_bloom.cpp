//========= Copyright Valve Corporation, All rights reserved. ============//
//
// Purpose: HL2SB (2026-10-07): GMod private screenspace bloom shader, the
//          composite pass behind DrawBloom( ) from lua/postprocess/bloom.lua
//          (materials/pp/bloom.vmt).  Reference behavior: samples the base
//          framebuffer texture, blends each channel through the per-channel
//          $levelr/$levelg/$levelb gains with the $colormul crossfade, and
//          renders additively (ONE/ONE).  Compiled programs ship verbatim as
//          shaders/fxc/g_bloom_{vs20,ps20}.vcs.
//
// $NoKeywords: $
//===========================================================================//

#include "BaseVSShader.h"

BEGIN_VS_SHADER_FLAGS( g_bloom, "GMod bloom composite", SHADER_NOT_EDITABLE )
	BEGIN_SHADER_PARAMS
		SHADER_PARAM( LEVELR, SHADER_PARAM_TYPE_FLOAT, "0.5", "Red Level" )
		SHADER_PARAM( LEVELG, SHADER_PARAM_TYPE_FLOAT, "0.5", "Green Level" )
		SHADER_PARAM( LEVELB, SHADER_PARAM_TYPE_FLOAT, "0.5", "Blue Level" )
		SHADER_PARAM( COLORMUL, SHADER_PARAM_TYPE_FLOAT, "1.0", "Color Multiplier" )
	END_SHADER_PARAMS

	SHADER_FALLBACK
	{
		return 0;
	}

	SHADER_INIT
	{
		LoadTexture( BASETEXTURE );
	}

	SHADER_DRAW
	{
		SHADOW_STATE
		{
			pShaderShadow->EnableDepthWrites( false );
			pShaderShadow->EnableTexture( SHADER_SAMPLER0, true );

			int fmt = VERTEX_POSITION;
			pShaderShadow->VertexShaderVertexFormat( fmt, 1, 0, 0 );

			pShaderShadow->SetVertexShader( "g_bloom_vs20", 0 );
			pShaderShadow->SetPixelShader( "g_bloom_ps20", 0 );

			// Reference state: additive composite (ONE, ONE).
			pShaderShadow->EnableBlending( true );
			pShaderShadow->BlendFunc( SHADER_BLEND_ONE, SHADER_BLEND_ONE );
		}
		DYNAMIC_STATE
		{
			// c0..c3 = levelr levelg levelb colormul (ps reads .x only).
			float flLevels[4];
			flLevels[0] = params[LEVELR]->GetFloatValue();
			flLevels[1] = params[LEVELG]->GetFloatValue();
			flLevels[2] = params[LEVELB]->GetFloatValue();
			flLevels[3] = params[COLORMUL]->GetFloatValue();
			pShaderAPI->SetPixelShaderConstant( 0, flLevels, 1 );

			BindTexture( SHADER_SAMPLER0, BASETEXTURE, -1 );
			pShaderAPI->SetVertexShaderIndex( 0 );
		}
		Draw();
	}
END_SHADER
