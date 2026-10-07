//========= Copyright Valve Corporation, All rights reserved. ============//
//
// Purpose: HL2SB (2026-10-07): GMod private sunbeams screenspace effect, the
//          core of DrawSunBeams( ) from lua/postprocess/sunbeams.lua
//          (materials/pp/sunbeams.vmt).  Reference behavior: walks eight
//          taps from the pixel toward the sun position ($sunx, $suny) up to
//          $sunsize UV units, thresholds each tap by $darken (clamped at
//          10), weights the accumulation by distance to the sun and adds it
//          to the base framebuffer sample scaled by $multiply.  Compiled
//          programs ship verbatim as
//          shaders/fxc/g_sunbeams_{vs20,ps20}.vcs.
//
// $NoKeywords: $
//===========================================================================//

#include "BaseVSShader.h"

BEGIN_VS_SHADER_FLAGS( g_sunbeams, "GMod sunbeams", SHADER_NOT_EDITABLE )
	BEGIN_SHADER_PARAMS
		SHADER_PARAM( FBTEXTURE, SHADER_PARAM_TYPE_TEXTURE, "_rt_FullFrameFB", "FBTEXTURE" )
		SHADER_PARAM( DARKEN, SHADER_PARAM_TYPE_FLOAT, "0.9", "darken" )
		SHADER_PARAM( MULTIPLY, SHADER_PARAM_TYPE_FLOAT, "0.6", "multiply" )
		SHADER_PARAM( SUNSIZE, SHADER_PARAM_TYPE_FLOAT, "0.06", "sunsize" )
		SHADER_PARAM( SUNX, SHADER_PARAM_TYPE_FLOAT, "0.5", "sunx" )
		SHADER_PARAM( SUNY, SHADER_PARAM_TYPE_FLOAT, "0.2", "suny" )
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

			pShaderShadow->SetVertexShader( "g_sunbeams_vs20", 0 );
			pShaderShadow->SetPixelShader( "g_sunbeams_ps20", 0 );
		}
		DYNAMIC_STATE
		{
			// c0 = $darken, c1 = $multiply, c2 = $sunsize, c3 = $sunx, c4 = $suny.
			float flConsts[5][4];
			flConsts[0][0] = params[DARKEN]->GetFloatValue();
			flConsts[1][0] = params[MULTIPLY]->GetFloatValue();
			flConsts[2][0] = params[SUNSIZE]->GetFloatValue();
			flConsts[3][0] = params[SUNX]->GetFloatValue();
			flConsts[4][0] = params[SUNY]->GetFloatValue();
			for ( int i = 0; i < 5; ++i )
			{
				flConsts[i][1] = flConsts[i][2] = flConsts[i][3] = 0.0f;
				pShaderAPI->SetPixelShaderConstant( i, flConsts[i], 1 );
			}

			BindTexture( SHADER_SAMPLER0, FBTEXTURE, -1 );
			pShaderAPI->SetVertexShaderIndex( 0 );
		}
		Draw();
	}
END_SHADER
