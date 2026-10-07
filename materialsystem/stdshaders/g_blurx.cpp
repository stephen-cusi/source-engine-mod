//========= Copyright Valve Corporation, All rights reserved. ============//
//
// Purpose: HL2SB (2026-10-07): GMod private horizontal screenspace blur, the
//          half of render.BlurRenderTarget( ) / bloom blur passes driven by
//          materials/pp/blurx.vmt.  Reference behavior: 13-tap separable
//          gaussian along X, tap pitch = $size / backbuffer width fed as c0.
//          Compiled programs ship verbatim as
//          shaders/fxc/g_blurx_{vs20,ps20}.vcs.
//
// $NoKeywords: $
//===========================================================================//

#include "BaseVSShader.h"

BEGIN_VS_SHADER_FLAGS( g_blurx, "GMod screenspace blur X", SHADER_NOT_EDITABLE )
	BEGIN_SHADER_PARAMS
		SHADER_PARAM( SIZE, SHADER_PARAM_TYPE_FLOAT, "4.0", "Blur Size" )
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
			pShaderShadow->EnableTexture( SHADER_SAMPLER0, true );
			pShaderShadow->EnableDepthWrites( false );

			int fmt = VERTEX_POSITION;
			pShaderShadow->VertexShaderVertexFormat( fmt, 1, 0, 0 );

			pShaderShadow->SetVertexShader( "g_blurx_vs20", 0 );
			pShaderShadow->SetPixelShader( "g_blurx_ps20", 0 );
		}
		DYNAMIC_STATE
		{
			// c0.x = $size / width (horizontal tap pitch in UV space).
			int nWidth, nHeight;
			pShaderAPI->GetBackBufferDimensions( nWidth, nHeight );
			float flPitch[4] = { 0.0f, 0.0f, 0.0f, 0.0f };
			if ( nWidth > 0 )
			{
				flPitch[0] = params[SIZE]->GetFloatValue() / (float)nWidth;
			}
			pShaderAPI->SetPixelShaderConstant( 0, flPitch, 1 );

			BindTexture( SHADER_SAMPLER0, BASETEXTURE, -1 );
			pShaderAPI->SetVertexShaderIndex( 0 );
		}
		Draw();
	}
END_SHADER
