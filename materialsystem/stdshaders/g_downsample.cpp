//========= Copyright Valve Corporation, All rights reserved. ============//
//
// Purpose: HL2SB (2026-10-07): GMod private screenspace downsample used by
//          DrawBloom( ) (materials/pp/downsample.vmt) and the video scale
//          path (materials/pp/videoscale.vmt).  Reference behavior: nine
//          taps of the framebuffer at +/- $scale pixels (normalized by the
//          texture's mapping size), center weighted double, then
//          color = max( sum * 0.1 - $darken, 0 ) * $multiply.  Compiled
//          programs ship verbatim as
//          shaders/fxc/g_downsample_{vs20,ps20}.vcs.
//
// $NoKeywords: $
//===========================================================================//

#include "BaseVSShader.h"

BEGIN_VS_SHADER_FLAGS( g_downsample, "GMod downsample", SHADER_NOT_EDITABLE )
	BEGIN_SHADER_PARAMS
		SHADER_PARAM( FBTEXTURE, SHADER_PARAM_TYPE_TEXTURE, "_rt_FullFrameFB", "FBTEXTURE" )
		SHADER_PARAM( DARKEN, SHADER_PARAM_TYPE_FLOAT, "0", "Amount to darken" )
		SHADER_PARAM( MULTIPLY, SHADER_PARAM_TYPE_FLOAT, "1", "Amount to multiply (to contrast)" )
		SHADER_PARAM( SCALE, SHADER_PARAM_TYPE_FLOAT, "1", "Sample Scale" )
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
			pShaderShadow->EnableDepthWrites( false );

			int fmt = VERTEX_POSITION;
			pShaderShadow->VertexShaderVertexFormat( fmt, 1, 0, 0 );

			pShaderShadow->SetVertexShader( "g_downsample_vs20", 0 );
			pShaderShadow->SetPixelShader( "g_downsample_ps20", 0 );

			pShaderShadow->EnableSRGBRead( SHADER_SAMPLER0, false );
			pShaderShadow->EnableSRGBWrite( false );
		}
		DYNAMIC_STATE
		{
			float flDarken[4] = { params[DARKEN]->GetFloatValue(), 0.0f, 0.0f, 0.0f };
			float flMultiply[4] = { params[MULTIPLY]->GetFloatValue(), 0.0f, 0.0f, 0.0f };
			pShaderAPI->SetPixelShaderConstant( 0, flDarken, 1 );
			pShaderAPI->SetPixelShaderConstant( 1, flMultiply, 1 );

			// c2/c3 = $scale normalized by the source texture size; the ps
			// uses them as the +/- pixel tap offsets.
			ITexture *pTex = params[FBTEXTURE]->GetTextureValue();
			float flScale = params[SCALE]->GetFloatValue();
			float flTapX[4] = { 0.0f, 0.0f, 0.0f, 0.0f };
			float flTapY[4] = { 0.0f, 0.0f, 0.0f, 0.0f };
			if ( pTex )
			{
				int nTexW = pTex->GetMappingWidth();
				int nTexH = pTex->GetMappingHeight();
				if ( nTexW > 0 ) flTapX[0] = flScale / (float)nTexW;
				if ( nTexH > 0 ) flTapY[0] = flScale / (float)nTexH;
			}
			pShaderAPI->SetPixelShaderConstant( 2, flTapX, 1 );
			pShaderAPI->SetPixelShaderConstant( 3, flTapY, 1 );

			BindTexture( SHADER_SAMPLER0, FBTEXTURE, -1 );
			pShaderAPI->SetVertexShaderIndex( 0 );
		}
		Draw();
	}
END_SHADER
