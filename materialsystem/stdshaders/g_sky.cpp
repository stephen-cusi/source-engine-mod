//========= Copyright Valve Corporation, All rights reserved. ============//
//
// Purpose: HL2SB (2026-10-07): GMod private painted-skybox shader used by
//          the materials/skybox/painted*.vmt family.  Reference behavior:
//          vertex-color gradient between $topcolor and $bottomcolor around
//          the camera direction, sun disc toward $sunnormal, dusk band from
//          $duskcolor/$duskscale/$duskintensity, all scaled by $hdrscale;
//          on ps_2_b hardware the star layer pass ($startexture,
//          $starscale reciprocal in c12, $starpos in c13) is added and the
//          pixel shader runs with the STARLAYERS static combination.  Lower
//          profiles fall back through ps_2_0 to a constant ps_1_1 blend.
//          Compiled programs ship verbatim as
//          shaders/fxc/g_sky_{vs20,vs11,ps20,ps20b,ps11}.vcs.
//
// $NoKeywords: $
//===========================================================================//

#include "BaseVSShader.h"

BEGIN_VS_SHADER_FLAGS( g_sky, "GMod painted skybox", SHADER_NOT_EDITABLE )
	BEGIN_SHADER_PARAMS
		SHADER_PARAM( TOPCOLOR, SHADER_PARAM_TYPE_VEC3, "[0 0 1]", "" )
		SHADER_PARAM( BOTTOMCOLOR, SHADER_PARAM_TYPE_VEC3, "[0 1 1]", "" )
		SHADER_PARAM( FADEBIAS, SHADER_PARAM_TYPE_FLOAT, "1.0", "" )
		SHADER_PARAM( HDRSCALE, SHADER_PARAM_TYPE_FLOAT, "1.0", "" )
		SHADER_PARAM( SUNNORMAL, SHADER_PARAM_TYPE_VEC3, "[0 1 0.5]", "" )
		SHADER_PARAM( DUSKCOLOR, SHADER_PARAM_TYPE_VEC3, "[1 0.4 0]", "" )
		SHADER_PARAM( DUSKSCALE, SHADER_PARAM_TYPE_FLOAT, "1.0", "" )
		SHADER_PARAM( DUSKINTENSITY, SHADER_PARAM_TYPE_FLOAT, "1.0", "" )
		SHADER_PARAM( SUNCOLOR, SHADER_PARAM_TYPE_VEC3, "[1 1 1]", "" )
		SHADER_PARAM( SUNSIZE, SHADER_PARAM_TYPE_FLOAT, "1", "" )
		SHADER_PARAM( STARTEXTURE, SHADER_PARAM_TYPE_TEXTURE, "skybox/stars", "" )
		SHADER_PARAM( STARFADE, SHADER_PARAM_TYPE_FLOAT, "1", "" )
		SHADER_PARAM( STARSCALE, SHADER_PARAM_TYPE_FLOAT, "1", "" )
		SHADER_PARAM( STARPOS, SHADER_PARAM_TYPE_FLOAT, "1", "" )
		SHADER_PARAM( STARLAYERS, SHADER_PARAM_TYPE_INTEGER, "0", "" )
	END_SHADER_PARAMS

	SHADER_FALLBACK
	{
		return 0;
	}

	SHADER_INIT
	{
		LoadTexture( STARTEXTURE );
	}

	SHADER_DRAW
	{
		SHADOW_STATE
		{
			pShaderShadow->EnableDepthWrites( false );

			int fmt = VERTEX_POSITION;
			pShaderShadow->VertexShaderVertexFormat( fmt, 1, 0, 0 );

			pShaderShadow->EnableBlending( false );
			pShaderShadow->BlendFunc( SHADER_BLEND_ONE, SHADER_BLEND_ONE );

			if ( g_pHardwareConfig->SupportsPixelShaders_2_b() )
			{
				// Static combination = number of star layers.
				pShaderShadow->SetPixelShader( "g_sky_ps20b", params[STARLAYERS]->GetIntValue() );
				pShaderShadow->SetVertexShader( "g_sky_vs20", 0 );
				pShaderShadow->EnableTexture( SHADER_SAMPLER0, true );
				pShaderShadow->EnableSRGBRead( SHADER_SAMPLER0, false );
			}
			else if ( g_pHardwareConfig->SupportsPixelShaders_2_0() )
			{
				pShaderShadow->SetPixelShader( "g_sky_ps20", 0 );
				pShaderShadow->SetVertexShader( "g_sky_vs20", 0 );
				pShaderShadow->EnableTexture( SHADER_SAMPLER0, false );
			}
			else
			{
				pShaderShadow->SetPixelShader( "g_sky_ps11", 0 );
				pShaderShadow->SetVertexShader( "g_sky_vs11", 0 );
				pShaderShadow->EnableTexture( SHADER_SAMPLER0, false );
			}

			pShaderShadow->EnableSRGBWrite( true );
			pShaderShadow->EnableAlphaWrites( true );
		}
		DYNAMIC_STATE
		{
			pShaderAPI->SetVertexShaderIndex( 0 );
			pShaderAPI->SetPixelShaderIndex( 0 );

			// c0 = camera eye position.
			float flEye[4] = { 0.0f, 0.0f, 0.0f, 0.0f };
			pShaderAPI->GetWorldSpaceCameraPosition( flEye );
			pShaderAPI->SetPixelShaderConstant( 0, flEye, 1 );

			// c1/c2 = gradient colors, c3 = fade bias.
			float flVec[4];
			params[TOPCOLOR]->GetVecValue( flVec, 3 );
			flVec[3] = 0.0f;
			pShaderAPI->SetPixelShaderConstant( 1, flVec, 1 );
			params[BOTTOMCOLOR]->GetVecValue( flVec, 3 );
			flVec[3] = 0.0f;
			pShaderAPI->SetPixelShaderConstant( 2, flVec, 1 );
			float flFade[4] = { params[FADEBIAS]->GetFloatValue(), 0.0f, 0.0f, 0.0f };
			pShaderAPI->SetPixelShaderConstant( 3, flFade, 1 );

			if ( g_pHardwareConfig->SupportsPixelShaders_2_0() )
			{
				// c4 = $hdrscale, forced to 1 when HDR is off.
				float flHdr[4] = { params[HDRSCALE]->GetFloatValue(), 0.0f, 0.0f, 0.0f };
				if ( g_pHardwareConfig->GetHDRType() == HDR_TYPE_NONE )
				{
					flHdr[0] = 1.0f;
				}
				pShaderAPI->SetPixelShaderConstant( 4, flHdr, 1 );

				params[SUNNORMAL]->GetVecValue( flVec, 3 );
				flVec[3] = 0.0f;
				pShaderAPI->SetPixelShaderConstant( 5, flVec, 1 );
				params[DUSKCOLOR]->GetVecValue( flVec, 3 );
				flVec[3] = 0.0f;
				pShaderAPI->SetPixelShaderConstant( 6, flVec, 1 );
				float flOne[4];
				flOne[0] = params[DUSKSCALE]->GetFloatValue();
				flOne[1] = params[DUSKINTENSITY]->GetFloatValue();
				flOne[2] = 0.0f;
				flOne[3] = 0.0f;
				pShaderAPI->SetPixelShaderConstant( 7, &flOne[0], 1 );
				float flDuskInt[4] = { flOne[1], 0.0f, 0.0f, 0.0f };
				pShaderAPI->SetPixelShaderConstant( 8, flDuskInt, 1 );
				params[SUNCOLOR]->GetVecValue( flVec, 3 );
				flVec[3] = 0.0f;
				pShaderAPI->SetPixelShaderConstant( 9, flVec, 1 );
				float flSun[4] = { params[SUNSIZE]->GetFloatValue(), 0.0f, 0.0f, 0.0f };
				pShaderAPI->SetPixelShaderConstant( 10, flSun, 1 );
				float flStarFade[4] = { params[STARFADE]->GetFloatValue(), 0.0f, 0.0f, 0.0f };
				pShaderAPI->SetPixelShaderConstant( 11, flStarFade, 1 );
			}

			if ( g_pHardwareConfig->SupportsPixelShaders_2_b() )
			{
				// c12 = reciprocal of $starscale (reference skips divide by 0).
				float flStarScale = params[STARSCALE]->GetFloatValue();
				float flRecip[4] = { 0.0f, 0.0f, 0.0f, 0.0f };
				if ( flStarScale != 0.0f )
				{
					flRecip[0] = 1.0f / flStarScale;
				}
				pShaderAPI->SetPixelShaderConstant( 12, flRecip, 1 );
				float flStarPos[4] = { params[STARPOS]->GetFloatValue(), 0.0f, 0.0f, 0.0f };
				pShaderAPI->SetPixelShaderConstant( 13, flStarPos, 1 );

				BindTexture( SHADER_SAMPLER0, STARTEXTURE, -1 );
			}
		}
		Draw();
	}
END_SHADER
