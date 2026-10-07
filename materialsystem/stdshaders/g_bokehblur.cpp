//========= Copyright Valve Corporation, All rights reserved. ============//
//
// Purpose: HL2SB (2026-10-07): GMod private bokeh depth-of-field blur, the
//          core of DrawBokehDepthOfField / lua/postprocess/bokeh_dof.lua
//          (materials/pp/bokehblur.vmt).  Reference behavior: ps_3_0 disc
//          gather whose per-tap weight comes from the circle of confusion
//          ($depthtexture vs $focus, exponent $focusradius, scale $size)
//          over the base framebuffer texture.  Compiled programs ship
//          verbatim as shaders/fxc/g_bokehblur_{vs30,ps30}.vcs (single
//          combination; hardware without ps_3_0 support draws no fallback,
//          matching the reference build).
//
// $NoKeywords: $
//===========================================================================//

#include "BaseVSShader.h"

BEGIN_VS_SHADER_FLAGS( g_bokehblur, "GMod bokeh depth of field", SHADER_NOT_EDITABLE )
	BEGIN_SHADER_PARAMS
		SHADER_PARAM( DEPTHTEXTURE, SHADER_PARAM_TYPE_TEXTURE, "_rt_FullFrameFB", "FBTEXTURE" )
		SHADER_PARAM( SIZE, SHADER_PARAM_TYPE_FLOAT, "7.0", "Blur Size" )
		SHADER_PARAM( FOCUS, SHADER_PARAM_TYPE_FLOAT, "0.1", "Blur Size" )
		SHADER_PARAM( FOCUSRADIUS, SHADER_PARAM_TYPE_FLOAT, "1.0", "Blur Size" )
	END_SHADER_PARAMS

	SHADER_FALLBACK
	{
		return 0;
	}

	SHADER_INIT
	{
		// Reference behavior: no texture preload for this shader; textures
		// arrive through the per-frame SetTexture calls of the consumer.
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

			pShaderShadow->SetVertexShader( "g_bokehblur_vs30", 0 );
			pShaderShadow->SetPixelShader( "g_bokehblur_ps30", 0 );
		}
		DYNAMIC_STATE
		{
			int nWidth, nHeight;
			pShaderAPI->GetBackBufferDimensions( nWidth, nHeight );

			// c0 = ( 1/width, 1/height ) tap pitch helper.
			float flInvBB[4] = { 0.0f, 0.0f, 0.0f, 0.0f };
			if ( nWidth > 0 ) flInvBB[0] = 1.0f / (float)nWidth;
			if ( nHeight > 0 ) flInvBB[1] = 1.0f / (float)nHeight;
			pShaderAPI->SetPixelShaderConstant( 0, flInvBB, 1 );

			// c1 = $size, c2 = $focus, c3 = $focusradius.
			float flSize[4] = { params[SIZE]->GetFloatValue(), 0.0f, 0.0f, 0.0f };
			float flFocus[4] = { params[FOCUS]->GetFloatValue(), 0.0f, 0.0f, 0.0f };
			float flRadius[4] = { params[FOCUSRADIUS]->GetFloatValue(), 0.0f, 0.0f, 0.0f };
			pShaderAPI->SetPixelShaderConstant( 1, flSize, 1 );
			pShaderAPI->SetPixelShaderConstant( 2, flFocus, 1 );
			pShaderAPI->SetPixelShaderConstant( 3, flRadius, 1 );

			BindTexture( SHADER_SAMPLER0, BASETEXTURE, -1 );
			BindTexture( SHADER_SAMPLER1, DEPTHTEXTURE, -1 );
			pShaderAPI->SetVertexShaderIndex( 0 );
		}
		Draw();
	}
END_SHADER
