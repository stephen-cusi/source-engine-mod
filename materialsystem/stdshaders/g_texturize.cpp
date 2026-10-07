//========= Copyright Valve Corporation, All rights reserved. ============//
//
// Purpose: HL2SB (2026-10-07): GMod private texturize screenspace effect,
//          the core of DrawTexturize( ) from lua/postprocess/texturize.lua
//          (materials/pp/texturize.vmt).  Reference behavior: takes the
//          framebuffer luminance as a posterized band index and wraps the
//          base UVs by $scalex / $scaley, then samples the pattern texture
//          ($basetexture) at those coordinates.  Compiled programs ship
//          verbatim as shaders/fxc/g_texturize_{vs20,ps20}.vcs.
//
// $NoKeywords: $
//===========================================================================//

#include "BaseVSShader.h"

BEGIN_VS_SHADER_FLAGS( g_texturize, "GMod texturize", SHADER_NOT_EDITABLE )
	BEGIN_SHADER_PARAMS
		SHADER_PARAM( FBTEXTURE, SHADER_PARAM_TYPE_TEXTURE, "_rt_FullFrameFB", "FBTEXTURE" )
		SHADER_PARAM( BASETEXTURE, SHADER_PARAM_TYPE_TEXTURE, "_rt_FullFrameFB", "BASETEXTURE" )
		SHADER_PARAM( SCALEX, SHADER_PARAM_TYPE_FLOAT, "30", "scalex" )
		SHADER_PARAM( SCALEY, SHADER_PARAM_TYPE_FLOAT, "30", "scaley" )
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
			pShaderShadow->EnableTexture( SHADER_SAMPLER1, true );
			pShaderShadow->EnableDepthWrites( false );

			int fmt = VERTEX_POSITION;
			pShaderShadow->VertexShaderVertexFormat( fmt, 1, 0, 0 );

			pShaderShadow->SetVertexShader( "g_texturize_vs20", 0 );
			pShaderShadow->SetPixelShader( "g_texturize_ps20", 0 );

			pShaderShadow->EnableSRGBRead( SHADER_SAMPLER0, false );
			pShaderShadow->EnableSRGBWrite( false );
		}
		DYNAMIC_STATE
		{
			// c0 = $scalex, c1 = $scaley (ps reads .x only).
			float flScale[4];
			flScale[0] = params[SCALEX]->GetFloatValue();
			flScale[1] = 0.0f;
			flScale[2] = 0.0f;
			flScale[3] = 0.0f;
			pShaderAPI->SetPixelShaderConstant( 0, flScale, 1 );
			flScale[0] = params[SCALEY]->GetFloatValue();
			pShaderAPI->SetPixelShaderConstant( 1, flScale, 1 );

			// s0 = $fbtexture (luminance source), s1 = $basetexture (pattern).
			BindTexture( SHADER_SAMPLER0, FBTEXTURE, -1 );
			BindTexture( SHADER_SAMPLER1, BASETEXTURE, -1 );
			pShaderAPI->SetVertexShaderIndex( 0 );
		}
		Draw();
	}
END_SHADER
