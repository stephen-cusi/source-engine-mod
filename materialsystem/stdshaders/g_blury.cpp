//========= Copyright Valve Corporation, All rights reserved. ============//
//
// Purpose: HL2SB (2026-10-07): GMod private vertical screenspace blur, the
//          half of render.BlurRenderTarget( ) / bloom blur passes driven by
//          materials/pp/blury.vmt.  Reference behavior: 13-tap separable
//          gaussian along Y, tap pitch = $size / backbuffer height fed as c0.
//          Compiled programs ship verbatim as
//          shaders/fxc/g_blury_{vs20,ps20}.vcs.
//
// $NoKeywords: $
//===========================================================================//

#include "BaseVSShader.h"

BEGIN_VS_SHADER_FLAGS( g_blury, "GMod screenspace blur Y", SHADER_NOT_EDITABLE )
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

			pShaderShadow->SetVertexShader( "g_blury_vs20", 0 );
			pShaderShadow->SetPixelShader( "g_blury_ps20", 0 );
		}
		DYNAMIC_STATE
		{
			// c0.x = $size / height (vertical tap pitch in UV space).
			int nWidth, nHeight;
			pShaderAPI->GetBackBufferDimensions( nWidth, nHeight );
			float flPitch[4] = { 0.0f, 0.0f, 0.0f, 0.0f };
			if ( nHeight > 0 )
			{
				flPitch[0] = params[SIZE]->GetFloatValue() / (float)nHeight;
			}
			pShaderAPI->SetPixelShaderConstant( 0, flPitch, 1 );

			BindTexture( SHADER_SAMPLER0, BASETEXTURE, -1 );
			pShaderAPI->SetVertexShaderIndex( 0 );
		}
		Draw();
	}
END_SHADER
