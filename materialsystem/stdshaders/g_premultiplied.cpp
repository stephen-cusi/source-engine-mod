//========= Copyright Valve Corporation, All rights reserved. ============//
//
// Purpose: HL2SB (2026-10-07): GMod private premultiplied-alpha blit.  No
//          stock material references it directly; the reference build keeps
//          it registered for content that composites premultiplied buffers.
//          Reference behavior: plain sample of $basetexture with
//          ONE / ONE_MINUS_SRC_ALPHA blending plus a separate-alpha blend of
//          SRC_ALPHA / ONE_MINUS_SRC_ALPHA, opaque writes otherwise.  The
//          compiled program ships verbatim as
//          shaders/fxc/g_premultiplied_{vs20,ps20}.vcs (ps is a passthrough
//          copy; the blending state carries the semantics).
//
// $NoKeywords: $
//===========================================================================//

#include "BaseVSShader.h"

BEGIN_VS_SHADER_FLAGS( g_premultiplied, "GMod premultiplied blit", SHADER_NOT_EDITABLE )
	BEGIN_SHADER_PARAMS
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

			pShaderShadow->SetVertexShader( "g_premultiplied_vs20", 0 );
			pShaderShadow->SetPixelShader( "g_premultiplied_ps20", 0 );

			pShaderShadow->EnableBlending( true );
			pShaderShadow->BlendFunc( SHADER_BLEND_ONE, SHADER_BLEND_ONE_MINUS_SRC_ALPHA );
			pShaderShadow->EnableBlendingSeparateAlpha( true );
			pShaderShadow->BlendFuncSeparateAlpha( SHADER_BLEND_SRC_ALPHA, SHADER_BLEND_ONE_MINUS_SRC_ALPHA );
		}
		DYNAMIC_STATE
		{
			BindTexture( SHADER_SAMPLER0, BASETEXTURE, -1 );
			pShaderAPI->SetVertexShaderIndex( 0 );
		}
		Draw();
	}
END_SHADER
