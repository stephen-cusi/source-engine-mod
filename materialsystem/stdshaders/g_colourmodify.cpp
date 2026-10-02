//========= Copyright Valve Corporation, All rights reserved. ============//
//
// Purpose: HL2SB (2026-10-02): GMod's private colour-modify screenspace
//          shader, re-declared natively so the global DrawColorModify( table )
//          from lua/postprocess/color_modify.lua drives real pixel math
//          through the $pp_colour_* material vars.  The compiled programs
//          ship verbatim as shaders/fxc/g_colourmodify_{vs20,ps20}.vcs and
//          this declaration mirrors the original render state: opaque
//          framebuffer copy, depth writes off, sRGB off, pixel constants
//          c0..c9 = addr addg addb brightness contrast colour mulr mulg mulb
//          inv in exactly that order.
//
// $NoKeywords: $
//===========================================================================//

#include "BaseVSShader.h"

BEGIN_VS_SHADER_FLAGS( g_colourmodify, "GMod colour modify", SHADER_NOT_EDITABLE )
	BEGIN_SHADER_PARAMS
		SHADER_PARAM( FBTEXTURE, SHADER_PARAM_TYPE_TEXTURE, "_rt_FullFrameFB", "FBTEXTURE" )
		SHADER_PARAM( PP_COLOUR_ADDR, SHADER_PARAM_TYPE_FLOAT, "0.0", "pp_colour_addr" )
		SHADER_PARAM( PP_COLOUR_ADDG, SHADER_PARAM_TYPE_FLOAT, "0.0", "pp_colour_addg" )
		SHADER_PARAM( PP_COLOUR_ADDB, SHADER_PARAM_TYPE_FLOAT, "0.0", "pp_colour_addb" )
		SHADER_PARAM( PP_COLOUR_BRIGHTNESS, SHADER_PARAM_TYPE_FLOAT, "0.0", "pp_colour_brightness" )
		SHADER_PARAM( PP_COLOUR_CONTRAST, SHADER_PARAM_TYPE_FLOAT, "1", "pp_colour_contrast" )
		SHADER_PARAM( PP_COLOUR_COLOUR, SHADER_PARAM_TYPE_FLOAT, "1", "pp_colour_colour" )
		SHADER_PARAM( PP_COLOUR_MULR, SHADER_PARAM_TYPE_FLOAT, "0", "pp_colour_mulr" )
		SHADER_PARAM( PP_COLOUR_MULG, SHADER_PARAM_TYPE_FLOAT, "0", "pp_colour_mulg" )
		SHADER_PARAM( PP_COLOUR_MULB, SHADER_PARAM_TYPE_FLOAT, "0", "pp_colour_mulb" )
		SHADER_PARAM( PP_COLOUR_INV, SHADER_PARAM_TYPE_FLOAT, "0", "pp_colour_inv" )
	END_SHADER_PARAMS

	SHADER_FALLBACK
	{
		if ( g_pHardwareConfig->GetDXSupportLevel() < 90 )
			return "wireframe";
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

			pShaderShadow->SetVertexShader( "g_colourmodify_vs20", 0 );
			pShaderShadow->SetPixelShader( "g_colourmodify_ps20", 0 );

			pShaderShadow->EnableSRGBRead( SHADER_SAMPLER0, false );
			pShaderShadow->EnableSRGBWrite( false );
		}
		DYNAMIC_STATE
		{
			BindTexture( SHADER_SAMPLER0, FBTEXTURE, -1 );

			// c0..c9 in the original constant order; the pixel shader reads
			// only the .x of each register.
			IMaterialVar *ppConsts[10] =
			{
				params[PP_COLOUR_ADDR],       params[PP_COLOUR_ADDG],
				params[PP_COLOUR_ADDB],       params[PP_COLOUR_BRIGHTNESS],
				params[PP_COLOUR_CONTRAST],   params[PP_COLOUR_COLOUR],
				params[PP_COLOUR_MULR],       params[PP_COLOUR_MULG],
				params[PP_COLOUR_MULB],       params[PP_COLOUR_INV],
			};
			for ( int i = 0; i < 10; ++i )
			{
				float v[4] = { ppConsts[i]->GetFloatValue(), 0.0f, 0.0f, 0.0f };
				pShaderAPI->SetPixelShaderConstant( i, v );
			}

			pShaderAPI->SetVertexShaderIndex( 0 );
		}
		Draw();
	}
END_SHADER
