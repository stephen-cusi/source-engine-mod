//========= Copyright Valve Corporation, All rights reserved. ============//
//
// Purpose: HL2SB (2026-10-02): GMod's private general screenspace shader
//          (VMT shader name "GMODScreenspace"), re-declared natively for
//          lua/postprocess/motion_blur.lua (pp/motionblur, pp/fb) and any
//          addon materials using it.  Samples base $basetexture (no
//          $fbtexture), modulates c0 = $alpha, and when $BLUR is set feeds
//          c1..c8 with backbuffer-scaled 8-tap offsets.  Programs ship
//          verbatim as shaders/fxc/g_screenspace_{vs20,vs11,ps20,ps11}.vcs.
//
// $NoKeywords: $
//===========================================================================//

#include "BaseVSShader.h"

BEGIN_VS_SHADER_FLAGS( gmodscreenspace, "GMod screenspace", SHADER_NOT_EDITABLE )
	BEGIN_SHADER_PARAMS
		SHADER_PARAM( TEXTUREALPHA, SHADER_PARAM_TYPE_BOOL, "1", "Use Texture Alpha" )
		SHADER_PARAM( VERTEXALPHA, SHADER_PARAM_TYPE_BOOL, "1", "Use Vertex Alpha" )
		SHADER_PARAM( VERTEXCOLOR, SHADER_PARAM_TYPE_BOOL, "1", "Use Vertex Color" )
		SHADER_PARAM( BLUR, SHADER_PARAM_TYPE_FLOAT, "0", "Blur this buffer" )
	END_SHADER_PARAMS

	SHADER_INIT_PARAMS()
	{
		// The original InitShaderInstance forces these material flags
		// unconditionally; the ps20 combo selector reads VERTEXALPHA from the
		// material flags, not from the $vertexalpha var.
		SET_FLAGS( static_cast< MaterialVarFlags_t >( MATERIAL_VAR_VERTEXCOLOR | MATERIAL_VAR_VERTEXALPHA ) );
	}

	SHADER_INIT
	{
		if ( params[BASETEXTURE]->IsDefined() )
		{
			LoadTexture( BASETEXTURE );
		}
	}

	SHADER_DRAW
	{
		SHADOW_STATE
		{
			pShaderShadow->EnableDepthTest( false );
			pShaderShadow->EnableDepthWrites( false );
			pShaderShadow->EnableAlphaWrites( true );
			pShaderShadow->EnableCulling( false );

			// POSITION | COLOR: the compiled vs20 reads a modulated quad.
			int fmt = VERTEX_POSITION | VERTEX_COLOR;
			pShaderShadow->VertexShaderVertexFormat( fmt, 1, 0, 0 );

			pShaderShadow->EnableTexture( SHADER_SAMPLER0, true );
			pShaderShadow->EnableAlphaPipe( true );
			pShaderShadow->EnableConstantColor( IsColorModulating() );
			pShaderShadow->EnableConstantAlpha( IsAlphaModulating() );
			pShaderShadow->EnableVertexAlpha( IS_FLAG_SET( MATERIAL_VAR_VERTEXALPHA ) );
			pShaderShadow->EnableTextureAlpha( SHADER_TEXTURE_STAGE0,
				params[TEXTUREALPHA]->GetIntValue() != 0 && params[BASETEXTURE]->IsTexture() );

			pShaderShadow->EnableBlending( true );
			pShaderShadow->BlendFunc( SHADER_BLEND_SRC_ALPHA, SHADER_BLEND_ONE_MINUS_SRC_ALPHA );
			pShaderShadow->EnableBlendingSeparateAlpha( true );
			pShaderShadow->BlendFuncSeparateAlpha( SHADER_BLEND_SRC_ALPHA, SHADER_BLEND_ONE_MINUS_SRC_ALPHA );
			if ( IS_FLAG_SET( MATERIAL_VAR_ADDITIVE ) )
			{
				pShaderShadow->BlendFunc( SHADER_BLEND_ONE, SHADER_BLEND_ONE );
			}

			if ( g_pHardwareConfig->SupportsPixelShaders_2_0() )
			{
				pShaderShadow->SetVertexShader( "g_screenspace_vs20", 0 );
			}
			else
			{
				pShaderShadow->SetVertexShader( "g_screenspace_vs11", 0 );
			}

			int iTexAlpha = params[TEXTUREALPHA]->GetIntValue() != 0 ? 1 : 0;
			int iVertexAlpha = IS_FLAG_SET( MATERIAL_VAR_VERTEXALPHA ) ? 1 : 0;
			if ( g_pHardwareConfig->SupportsPixelShaders_2_0() )
			{
				int iCombo = iTexAlpha + ( iVertexAlpha + ( params[BLUR]->GetFloatValue() != 0.0f ? 2 : 0 ) ) * 2;
				pShaderShadow->SetPixelShader( "g_screenspace_ps20", iCombo );
			}
			else
			{
				int iCombo = iTexAlpha + iVertexAlpha * 2;
				pShaderShadow->SetPixelShader( "g_screenspace_ps11", iCombo );
			}

			pShaderShadow->EnableSRGBRead( SHADER_SAMPLER0, false );
			pShaderShadow->EnableSRGBWrite( false );
		}
		DYNAMIC_STATE
		{
			BindTexture( SHADER_SAMPLER0, BASETEXTURE, -1 );

			// c0 = $alpha modulation, exactly like the original dynamic pass.
			float flAlpha[4] = { params[ALPHA]->GetFloatValue(), 0.0f, 0.0f, 0.0f };
			pShaderAPI->SetPixelShaderConstant( 0, flAlpha );

			float flBlur = params[BLUR]->GetFloatValue();
			if ( flBlur != 0.0f )
			{
				int nWidth, nHeight;
				pShaderAPI->GetBackBufferDimensions( nWidth, nHeight );
				float w = flBlur / ( nWidth > 0 ? nWidth : 1 );
				float h = flBlur / ( nHeight > 0 ? nHeight : 1 );
				float taps[8][4] =
				{
					{ -w, -h, 0.0f, 0.0f }, {  w, -h, 0.0f, 0.0f },
					{ -w,  h, 0.0f, 0.0f }, {  w,  h, 0.0f, 0.0f },
					{ -w,  0, 0.0f, 0.0f }, {  0, -h, 0.0f, 0.0f },
					{  w,  0, 0.0f, 0.0f }, {  0,  h, 0.0f, 0.0f },
				};
				for ( int i = 0; i < 8; ++i )
				{
					pShaderAPI->SetPixelShaderConstant( 1 + i, taps[i] );
				}
			}

			pShaderAPI->SetVertexShaderIndex( 0 );
			pShaderAPI->SetPixelShaderIndex( 0 );
		}
		Draw();
	}
END_SHADER
