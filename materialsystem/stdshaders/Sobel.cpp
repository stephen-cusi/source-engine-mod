//========= Copyright Valve Corporation, All rights reserved. ============//
//
// Purpose: HL2SB (2026-10-07): GMod private sobel edge detector, the core
//          of DrawSobel( ) from lua/postprocess/sobel.lua
//          (materials/pp/sobel.vmt, VMT shader name "Sobel").  Reference
//          behavior: nine taps on a fixed 1/640 UV stencil, luminance
//          (0.3/0.59/0.11) gradient magnitude compared against $threshold
//          (fed in c2), and the base framebuffer sample multiplied by the
//          resulting mask.  Picks ps_2_b when available, else ps_2_0.
//          Compiled programs ship verbatim as
//          shaders/fxc/g_sobel_{vs20,ps20,ps20b}.vcs.
//
// $NoKeywords: $
//===========================================================================//

#include "BaseVSShader.h"

BEGIN_VS_SHADER_FLAGS( Sobel, "GMod sobel edge detect", SHADER_NOT_EDITABLE )
	BEGIN_SHADER_PARAMS
		SHADER_PARAM( FBTEXTURE, SHADER_PARAM_TYPE_TEXTURE, "_rt_FullFrameFB", "FBTEXTURE" )
		SHADER_PARAM( THRESHOLD, SHADER_PARAM_TYPE_FLOAT, "0.11", "threshold" )
	END_SHADER_PARAMS

	SHADER_FALLBACK
	{
		return 0;
	}

	SHADER_INIT
	{
		if ( params[FBTEXTURE]->IsTexture() )
		{
			LoadTexture( FBTEXTURE );
		}
	}

	SHADER_DRAW
	{
		SHADOW_STATE
		{
			pShaderShadow->EnableDepthWrites( false );
			pShaderShadow->EnableTexture( SHADER_SAMPLER0, true );

			int fmt = VERTEX_POSITION;
			pShaderShadow->VertexShaderVertexFormat( fmt, 1, 0, 0 );

			pShaderShadow->SetVertexShader( "g_sobel_vs20", 0 );
			if ( g_pHardwareConfig->SupportsPixelShaders_2_b() )
			{
				pShaderShadow->SetPixelShader( "g_sobel_ps20b", 0 );
			}
			else
			{
				pShaderShadow->SetPixelShader( "g_sobel_ps20", 0 );
			}
		}
		DYNAMIC_STATE
		{
			// c2 = $threshold (the program defines its stencil weights).
			float flThreshold[4] = { params[THRESHOLD]->GetFloatValue(), 0.0f, 0.0f, 0.0f };
			pShaderAPI->SetPixelShaderConstant( 2, flThreshold, 1 );

			BindTexture( SHADER_SAMPLER0, FBTEXTURE, -1 );
			pShaderAPI->SetVertexShaderIndex( 0 );
			pShaderAPI->SetPixelShaderIndex( 0 );
		}
		Draw();
	}
END_SHADER
