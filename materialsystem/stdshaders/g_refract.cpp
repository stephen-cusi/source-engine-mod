//========= Copyright Valve Corporation, All rights reserved. ============//
//
// Purpose: HL2SB (2026-10-07): GMod private refract helper used by the
//          morph refract pass (materials/pp/morph/refract.vmt).  Reference
//          behavior: five-tap normal fetch from $normalmap (center weighted
//          double), remapped around 0.5, then the framebuffer is resampled
//          at uv + normal * $refractamount * 0.001.  Note the stock material
//          never sets $normalmap, so the shadertest default from the param
//          table applies exactly as in the reference build.  Compiled
//          programs ship verbatim as shaders/fxc/g_refract_{vs20,ps20}.vcs.
//
// $NoKeywords: $
//===========================================================================//

#include "BaseVSShader.h"

BEGIN_VS_SHADER_FLAGS( g_refract, "GMod refract", SHADER_NOT_EDITABLE )
	BEGIN_SHADER_PARAMS
		SHADER_PARAM( FBTEXTURE, SHADER_PARAM_TYPE_TEXTURE, "_rt_FullFrameFB", "FBTEXTURE" )
		SHADER_PARAM( REFRACTAMOUNT, SHADER_PARAM_TYPE_FLOAT, "0.5", "Refract Amount" )
		SHADER_PARAM( NORMALMAP, SHADER_PARAM_TYPE_TEXTURE, "shadertest/WorldDiffuseBumpMap_bump", "Huh texture" )
	END_SHADER_PARAMS

	SHADER_FALLBACK
	{
		return 0;
	}

	SHADER_INIT
	{
		LoadTexture( NORMALMAP );
		LoadTexture( FBTEXTURE );
	}

	SHADER_DRAW
	{
		SHADOW_STATE
		{
			pShaderShadow->EnableTexture( SHADER_SAMPLER0, true );
			pShaderShadow->EnableTexture( SHADER_SAMPLER1, true );
			pShaderShadow->EnableDepthWrites( false );

			int fmt = VERTEX_POSITION;
			pShaderShadow->VertexShaderVertexFormat( fmt, 1, 0, 0 );

			pShaderShadow->SetVertexShader( "g_refract_vs20", 0 );
			pShaderShadow->SetPixelShader( "g_refract_ps20", 0 );

			pShaderShadow->EnableSRGBRead( SHADER_SAMPLER0, false );
			pShaderShadow->EnableSRGBWrite( false );
		}
		DYNAMIC_STATE
		{
			float flAmount[4] = { params[REFRACTAMOUNT]->GetFloatValue(), 0.0f, 0.0f, 0.0f };
			pShaderAPI->SetPixelShaderConstant( 0, flAmount, 1 );

			// s0 = $normalmap, s1 = $fbtexture (sample order as compiled).
			BindTexture( SHADER_SAMPLER0, NORMALMAP, -1 );
			BindTexture( SHADER_SAMPLER1, FBTEXTURE, -1 );
			pShaderAPI->SetVertexShaderIndex( 0 );
		}
		Draw();
	}
END_SHADER
