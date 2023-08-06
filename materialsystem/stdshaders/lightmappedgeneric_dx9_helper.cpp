//========= Copyright Valve Corporation, All rights reserved. ============//
//
// Purpose: Lightmap only shader
//
// $Header: $
// $NoKeywords: $
//=============================================================================

#include "lightmappedgeneric_dx9_helper.h"
#include "BaseVSShader.h"
#include "commandbuilder.h"
#include "convar.h"
#include "lightmappedgeneric_ps20.inc"
#include "lightmappedgeneric_vs20.inc"
#include "lightmappedgeneric_ps20b.inc"
// Includes for PS30
#include "pbr_vs30.inc"
#include "pbr_ps30.inc"

// Includes for PS20b
#include "pbr_vs20b.inc"
#include "pbr_ps20b.inc"

#include "cpp_shader_constant_register_map.h"
#include "tier0/memdbgon.h"

// Defining samplers
const Sampler_t SAMPLER_BASETEXTURE = SHADER_SAMPLER0;
const Sampler_t SAMPLER_NORMAL = SHADER_SAMPLER1;
const Sampler_t SAMPLER_ENVMAP = SHADER_SAMPLER2;
const Sampler_t SAMPLER_SHADOWDEPTH = SHADER_SAMPLER4;
const Sampler_t SAMPLER_RANDOMROTATION = SHADER_SAMPLER5;
const Sampler_t SAMPLER_FLASHLIGHT = SHADER_SAMPLER6;
const Sampler_t SAMPLER_LIGHTMAP = SHADER_SAMPLER7;
const Sampler_t SAMPLER_MRAO = SHADER_SAMPLER10;
const Sampler_t SAMPLER_EMISSIVE = SHADER_SAMPLER11;
const Sampler_t SAMPLER_SPECULAR = SHADER_SAMPLER12;

ConVar mat_disable_lightwarp( "mat_disable_lightwarp", "0" );
ConVar mat_disable_fancy_blending( "mat_disable_fancy_blending", "0" );
ConVar mat_fullbright( "mat_fullbright","0", FCVAR_CHEAT );
ConVar my_mat_fullbright( "mat_fullbright","0", FCVAR_CHEAT );


static ConVar mat_pbr_force_20b("mat_pbr_force_20b", "0", FCVAR_CHEAT);
static ConVar mat_pbr_parallaxmap("mat_pbr_parallaxmap", "1");
static ConVar mat_specular("mat_specular", "1");
static ConVar mat_reducefillrate("mat_reducefillrate","2");

extern ConVar r_flashlight_version2;

class CLightmappedGeneric_DX9_Context : public CBasePerMaterialContextData
{
public:
	uint8 *m_pStaticCmds;
	CCommandBufferBuilder< CFixedCommandStorageBuffer< 1000 > > m_SemiStaticCmdsOut;

	bool m_bVertexShaderFastPath;
	bool m_bPixelShaderFastPath;
	bool m_bPixelShaderForceFastPathBecauseOutline;
	bool m_bFullyOpaque;
	bool m_bFullyOpaqueWithoutAlphaTest;

	void ResetStaticCmds( void )
	{
		if ( m_pStaticCmds )
		{
			delete[] m_pStaticCmds;
			m_pStaticCmds = NULL;
		}
	}

	CLightmappedGeneric_DX9_Context( void )
	{
		m_pStaticCmds = NULL;
	}

	~CLightmappedGeneric_DX9_Context( void )
	{
		ResetStaticCmds();
	}

};


void InitParamsLightmappedGeneric_DX9( CBaseVSShader *pShader, IMaterialVar** params, const char *pMaterialName, LightmappedGeneric_DX9_Vars_t &info )
{
	if ( g_pHardwareConfig->SupportsBorderColor() )
	{
		params[FLASHLIGHTTEXTURE]->SetStringValue( "effects/flashlight_border" );
	}
	else
	{
		params[FLASHLIGHTTEXTURE]->SetStringValue( "effects/flashlight001" );
	}

	// Write over $basetexture with $albedo if we are going to be using diffuse normal mapping.
	if( g_pConfig->UseBumpmapping() && params[info.m_nBumpmap]->IsDefined() && params[info.m_nAlbedo]->IsDefined() &&
		params[info.m_nBaseTexture]->IsDefined() && 
		!( params[info.m_nNoDiffuseBumpLighting]->IsDefined() && params[info.m_nNoDiffuseBumpLighting]->GetIntValue() ) )
	{
		params[info.m_nBaseTexture]->SetStringValue( params[info.m_nAlbedo]->GetStringValue() );
	}

	if( pShader->IsUsingGraphics() && params[info.m_nEnvmap]->IsDefined() && !pShader->CanUseEditorMaterials() )
	{
		if( stricmp( params[info.m_nEnvmap]->GetStringValue(), "env_cubemap" ) == 0 )
		{
			Warning( "env_cubemap used on world geometry without rebuilding map. . ignoring: %s\n", pMaterialName );
			params[info.m_nEnvmap]->SetUndefined();
		}
	}
	
	if ( (mat_disable_lightwarp.GetBool() ) &&
		 (info.m_nLightWarpTexture != -1) )
	{
		params[info.m_nLightWarpTexture]->SetUndefined();
	}
	if ( (mat_disable_fancy_blending.GetBool() ) &&
		 (info.m_nBlendModulateTexture != -1) )
	{
		params[info.m_nBlendModulateTexture]->SetUndefined();
	}

	if( !params[info.m_nEnvmapTint]->IsDefined() )
		params[info.m_nEnvmapTint]->SetVecValue( 1.0f, 1.0f, 1.0f );

	if( !params[info.m_nNoDiffuseBumpLighting]->IsDefined() )
		params[info.m_nNoDiffuseBumpLighting]->SetIntValue( 0 );

	if( !params[info.m_nSelfIllumTint]->IsDefined() )
		params[info.m_nSelfIllumTint]->SetVecValue( 1.0f, 1.0f, 1.0f );

	if( !params[info.m_nDetailScale]->IsDefined() )
		params[info.m_nDetailScale]->SetFloatValue( 4.0f );

	if ( !params[info.m_nDetailTint]->IsDefined() )
		params[info.m_nDetailTint]->SetVecValue( 1.0f, 1.0f, 1.0f, 1.0f );

	InitFloatParam( info.m_nDetailTextureBlendFactor, params, 1.0 );
	InitIntParam( info.m_nDetailTextureCombineMode, params, 0 );

	if( !params[info.m_nFresnelReflection]->IsDefined() )
		params[info.m_nFresnelReflection]->SetFloatValue( 1.0f );

	if( !params[info.m_nEnvmapMaskFrame]->IsDefined() )
		params[info.m_nEnvmapMaskFrame]->SetIntValue( 0 );
	
	if( !params[info.m_nEnvmapFrame]->IsDefined() )
		params[info.m_nEnvmapFrame]->SetIntValue( 0 );

	if( !params[info.m_nBumpFrame]->IsDefined() )
		params[info.m_nBumpFrame]->SetIntValue( 0 );

	if( !params[info.m_nDetailFrame]->IsDefined() )
		params[info.m_nDetailFrame]->SetIntValue( 0 );

	if( !params[info.m_nEnvmapContrast]->IsDefined() )
		params[info.m_nEnvmapContrast]->SetFloatValue( 0.0f );
	
	if( !params[info.m_nEnvmapSaturation]->IsDefined() )
		params[info.m_nEnvmapSaturation]->SetFloatValue( 1.0f );
	
	InitFloatParam( info.m_nAlphaTestReference, params, 0.0f );

	// No texture means no self-illum or env mask in base alpha
	if ( !params[info.m_nBaseTexture]->IsDefined() )
	{
		CLEAR_FLAGS( MATERIAL_VAR_SELFILLUM );
		CLEAR_FLAGS( MATERIAL_VAR_BASEALPHAENVMAPMASK );
	}

	
	if (mat_reducefillrate.GetInt() > 1)
	{
		if (!params[info.m_nBumpmap]->IsDefined())
		{
			params[info.m_nBumpmap]->SetStringValue("dev/flat_normal");
		}

		// Set a good default mrao texture
		if (!params[info.mraoTexture]->IsDefined())
			params[info.mraoTexture]->SetStringValue("dev/pbr_mraotexture");

		// PBR relies heavily on envmaps
		if (!params[info.m_nEnvmap]->IsDefined())
			params[info.m_nEnvmap]->SetStringValue("env_cubemap");
	}
	else 
	{
		if (params[info.m_nBumpmap]->IsDefined())
		{
			params[info.m_nEnvmapMask]->SetUndefined();
		}
	}
	// If in decal mode, no debug override...
	if (IS_FLAG_SET(MATERIAL_VAR_DECAL))
	{
		SET_FLAGS( MATERIAL_VAR_NO_DEBUG_OVERRIDE );
	}

	SET_FLAGS2( MATERIAL_VAR2_LIGHTING_LIGHTMAP );
	if( g_pConfig->UseBumpmapping() && params[info.m_nBumpmap]->IsDefined() && (params[info.m_nNoDiffuseBumpLighting]->GetIntValue() == 0) )
	{
		SET_FLAGS2( MATERIAL_VAR2_LIGHTING_BUMPED_LIGHTMAP );
	}


	if( !params[info.m_nBaseTextureNoEnvmap]->IsDefined() )
	{
		params[info.m_nBaseTextureNoEnvmap]->SetIntValue( 0 );
	}
	if( !params[info.m_nBaseTexture2NoEnvmap]->IsDefined() )
	{
		params[info.m_nBaseTexture2NoEnvmap]->SetIntValue( 0 );
	}

	if( ( info.m_nSelfShadowedBumpFlag != -1 ) &&
		( !params[info.m_nSelfShadowedBumpFlag]->IsDefined() )
		)
	{
		params[info.m_nSelfShadowedBumpFlag]->SetIntValue( 0 );
	}
	// handle line art parms
	InitFloatParam( info.m_nEdgeSoftnessStart, params, 0.5 );
	InitFloatParam( info.m_nEdgeSoftnessEnd, params, 0.5 );
	InitFloatParam( info.m_nOutlineAlpha, params, 1.0 );
}

void InitLightmappedGeneric_DX9( CBaseVSShader *pShader, IMaterialVar** params, LightmappedGeneric_DX9_Vars_t &info )
{
	if (mat_reducefillrate.GetInt() > 1)
	{
		Assert(info.m_nFlashlightTexture >= 0);
		pShader->LoadTexture(info.m_nFlashlightTexture, TEXTUREFLAGS_SRGB);

		Assert(info.m_nBumpmap >= 0);
		pShader->LoadBumpMap(info.m_nBumpmap);

		Assert(info.envMap >= 0);
		int envMapFlags = g_pHardwareConfig->GetHDRType() == HDR_TYPE_NONE ? TEXTUREFLAGS_SRGB : 0;
		envMapFlags |= TEXTUREFLAGS_ALL_MIPS;
		pShader->LoadCubeMap(info.m_nEnvmap, envMapFlags);

		if (info.emissionTexture >= 0 && params[info.emissionTexture]->IsDefined())
			pShader->LoadTexture(info.emissionTexture, TEXTUREFLAGS_SRGB);

		Assert(info.mraoTexture >= 0);
		pShader->LoadTexture(info.mraoTexture, 0);

		if (params[info.m_nBaseTexture]->IsDefined())
		{
			pShader->LoadTexture(info.m_nBaseTexture, TEXTUREFLAGS_SRGB);
		}

		if (params[info.specularTexture]->IsDefined())
		{
			pShader->LoadTexture(info.specularTexture, TEXTUREFLAGS_SRGB);
		}

		if (IS_FLAG_SET(MATERIAL_VAR_MODEL)) // Set material var2 flags specific to models
		{
			SET_FLAGS2(MATERIAL_VAR2_SUPPORTS_HW_SKINNING);             // Required for skinning
			SET_FLAGS2(MATERIAL_VAR2_DIFFUSE_BUMPMAPPED_MODEL);         // Required for dynamic lighting
			SET_FLAGS2(MATERIAL_VAR2_NEEDS_TANGENT_SPACES);             // Required for dynamic lighting
			SET_FLAGS2(MATERIAL_VAR2_LIGHTING_VERTEX_LIT);              // Required for dynamic lighting
			SET_FLAGS2(MATERIAL_VAR2_NEEDS_BAKED_LIGHTING_SNAPSHOTS);   // Required for ambient cube
			SET_FLAGS2(MATERIAL_VAR2_SUPPORTS_FLASHLIGHT);              // Required for flashlight
			SET_FLAGS2(MATERIAL_VAR2_USE_FLASHLIGHT);                   // Required for flashlight
		}
		else // Set material var2 flags specific to brushes
		{
			SET_FLAGS2(MATERIAL_VAR2_LIGHTING_LIGHTMAP);                // Required for lightmaps
			SET_FLAGS2(MATERIAL_VAR2_LIGHTING_BUMPED_LIGHTMAP);         // Required for lightmaps
			SET_FLAGS2(MATERIAL_VAR2_SUPPORTS_FLASHLIGHT);              // Required for flashlight
			SET_FLAGS2(MATERIAL_VAR2_USE_FLASHLIGHT);                   // Required for flashlight
		}
	}
	else
	{
		if (g_pConfig->UseBumpmapping() && params[info.m_nBumpmap]->IsDefined())
		{
			pShader->LoadBumpMap(info.m_nBumpmap);
		}

		if (g_pConfig->UseBumpmapping() && params[info.m_nBumpmap2]->IsDefined())
		{
			pShader->LoadBumpMap(info.m_nBumpmap2);
		}

		if (g_pConfig->UseBumpmapping() && params[info.m_nBumpMask]->IsDefined())
		{
			pShader->LoadBumpMap(info.m_nBumpMask);
		}

		if (params[info.m_nBaseTexture]->IsDefined())
		{
			pShader->LoadTexture(info.m_nBaseTexture, TEXTUREFLAGS_SRGB);

			if (!params[info.m_nBaseTexture]->GetTextureValue()->IsTranslucent())
			{
				CLEAR_FLAGS(MATERIAL_VAR_SELFILLUM);
				CLEAR_FLAGS(MATERIAL_VAR_BASEALPHAENVMAPMASK);
			}
		}

		if (params[info.m_nBaseTexture2]->IsDefined())
		{
			pShader->LoadTexture(info.m_nBaseTexture2, TEXTUREFLAGS_SRGB);
		}

		if (params[info.m_nLightWarpTexture]->IsDefined())
		{
			pShader->LoadTexture(info.m_nLightWarpTexture);
		}

		if ((info.m_nBlendModulateTexture != -1) &&
			(params[info.m_nBlendModulateTexture]->IsDefined()))
		{
			pShader->LoadTexture(info.m_nBlendModulateTexture);
		}

		if (params[info.m_nDetail]->IsDefined())
		{
			int nDetailBlendMode = (info.m_nDetailTextureCombineMode == -1) ? 0 : params[info.m_nDetailTextureCombineMode]->GetIntValue();
			nDetailBlendMode = nDetailBlendMode > 1 ? 1 : nDetailBlendMode;

			pShader->LoadTexture(info.m_nDetail, nDetailBlendMode != 0 ? TEXTUREFLAGS_SRGB : 0);
		}

		pShader->LoadTexture(info.m_nFlashlightTexture, TEXTUREFLAGS_SRGB);

		// Don't alpha test if the alpha channel is used for other purposes
		if (IS_FLAG_SET(MATERIAL_VAR_SELFILLUM) || IS_FLAG_SET(MATERIAL_VAR_BASEALPHAENVMAPMASK))
		{
			CLEAR_FLAGS(MATERIAL_VAR_ALPHATEST);
		}

		if (params[info.m_nEnvmap]->IsDefined())
		{
			if (!IS_FLAG_SET(MATERIAL_VAR_ENVMAPSPHERE))
			{
				pShader->LoadCubeMap(info.m_nEnvmap, g_pHardwareConfig->GetHDRType() == HDR_TYPE_NONE ? TEXTUREFLAGS_SRGB : 0);
			}
			else
			{
				pShader->LoadTexture(info.m_nEnvmap);
			}

			if (!g_pHardwareConfig->SupportsCubeMaps())
			{
				SET_FLAGS(MATERIAL_VAR_ENVMAPSPHERE);
			}

			if (params[info.m_nEnvmapMask]->IsDefined())
			{
				pShader->LoadTexture(info.m_nEnvmapMask);
			}
		}
		else
		{
			params[info.m_nEnvmapMask]->SetUndefined();
		}

		// We always need this because of the flashlight.
		SET_FLAGS2(MATERIAL_VAR2_NEEDS_TANGENT_SPACES);
	}
}

void DrawLightmappedGeneric_DX9_Internal(CBaseVSShader *pShader, IMaterialVar** params, bool hasFlashlight, 
								 IShaderDynamicAPI *pShaderAPI, IShaderShadow* pShaderShadow, 
								 LightmappedGeneric_DX9_Vars_t &info,
								 CBasePerMaterialContextData **pContextDataPtr
								 )
{
	CLightmappedGeneric_DX9_Context *pContextData = reinterpret_cast< CLightmappedGeneric_DX9_Context *> ( *pContextDataPtr );
	if ( pShaderShadow || ( ! pContextData ) || pContextData->m_bMaterialVarsChanged  || hasFlashlight )
	{
		bool hasBaseTexture = params[info.m_nBaseTexture]->IsTexture();
		int nAlphaChannelTextureVar = hasBaseTexture ? (int)info.m_nBaseTexture : (int)info.m_nEnvmapMask;
		BlendType_t nBlendType = pShader->EvaluateBlendRequirements( nAlphaChannelTextureVar, hasBaseTexture );
		bool bIsAlphaTested = IS_FLAG_SET( MATERIAL_VAR_ALPHATEST ) != 0;
		bool bFullyOpaqueWithoutAlphaTest = (nBlendType != BT_BLENDADD) && (nBlendType != BT_BLEND) && (!hasFlashlight || IsX360()); //dest alpha is free for special use
		bool bFullyOpaque = bFullyOpaqueWithoutAlphaTest && !bIsAlphaTested;
		bool bNeedRegenStaticCmds = (! pContextData ) || pShaderShadow;

		if ( ! pContextData )								// make sure allocated
		{
			pContextData = new CLightmappedGeneric_DX9_Context;
			*pContextDataPtr = pContextData;
		}

		bool hasBump = ( params[info.m_nBumpmap]->IsTexture() ) && ( !g_pHardwareConfig->PreferReducedFillrate() );
		bool hasSSBump = hasBump && (info.m_nSelfShadowedBumpFlag != -1) &&	( params[info.m_nSelfShadowedBumpFlag]->GetIntValue() );
		bool hasBaseTexture2 = hasBaseTexture && params[info.m_nBaseTexture2]->IsTexture();
		bool hasLightWarpTexture = params[info.m_nLightWarpTexture]->IsTexture();
		bool hasBump2 = hasBump && params[info.m_nBumpmap2]->IsTexture();
		bool hasDetailTexture = params[info.m_nDetail]->IsTexture();
		bool hasSelfIllum = IS_FLAG_SET( MATERIAL_VAR_SELFILLUM );
		bool hasBumpMask = hasBump && hasBump2 && params[info.m_nBumpMask]->IsTexture() && !hasSelfIllum &&
			!hasDetailTexture && !hasBaseTexture2 && (params[info.m_nBaseTextureNoEnvmap]->GetIntValue() == 0);
		bool bHasBlendModulateTexture = 
			(info.m_nBlendModulateTexture != -1) &&
			(params[info.m_nBlendModulateTexture]->IsTexture() );
		bool hasNormalMapAlphaEnvmapMask = IS_FLAG_SET( MATERIAL_VAR_NORMALMAPALPHAENVMAPMASK );

		if ( hasFlashlight && !IsX360() )				
		{
			// !!speed!! do this in the caller so we don't build struct every time
			CBaseVSShader::DrawFlashlight_dx90_Vars_t vars;
			vars.m_bBump = hasBump;
			vars.m_nBumpmapVar = info.m_nBumpmap;
			vars.m_nBumpmapFrame = info.m_nBumpFrame;
			vars.m_nBumpTransform = info.m_nBumpTransform;
			vars.m_nFlashlightTextureVar = info.m_nFlashlightTexture;
			vars.m_nFlashlightTextureFrameVar = info.m_nFlashlightTextureFrame;
			vars.m_bLightmappedGeneric = true;
			vars.m_bWorldVertexTransition = hasBaseTexture2;
			vars.m_nBaseTexture2Var = info.m_nBaseTexture2;
			vars.m_nBaseTexture2FrameVar = info.m_nBaseTexture2Frame;
			vars.m_nBumpmap2Var = info.m_nBumpmap2;
			vars.m_nBumpmap2Frame = info.m_nBumpFrame2;
			vars.m_nBump2Transform = info.m_nBumpTransform2;
			vars.m_nAlphaTestReference = info.m_nAlphaTestReference;
			vars.m_bSSBump = hasSSBump;
			vars.m_nDetailVar = info.m_nDetail;
			vars.m_nDetailScale = info.m_nDetailScale;
			vars.m_nDetailTextureCombineMode = info.m_nDetailTextureCombineMode;
			vars.m_nDetailTextureBlendFactor = info.m_nDetailTextureBlendFactor;
			vars.m_nDetailTint = info.m_nDetailTint;

			if ( ( info.m_nSeamlessMappingScale != -1 ) )
				vars.m_fSeamlessScale = params[info.m_nSeamlessMappingScale]->GetFloatValue();
			else
				vars.m_fSeamlessScale = 0.0;
			pShader->DrawFlashlight_dx90( params, pShaderAPI, pShaderShadow, vars );
			return;
		}

		pContextData->m_bFullyOpaque = bFullyOpaque;
		pContextData->m_bFullyOpaqueWithoutAlphaTest = bFullyOpaqueWithoutAlphaTest;

		bool bHasOutline = IsBoolSet( info.m_nOutline, params );
		pContextData->m_bPixelShaderForceFastPathBecauseOutline = bHasOutline;
		bool bHasSoftEdges = IsBoolSet( info.m_nSoftEdges, params );
		bool hasEnvmapMask = params[info.m_nEnvmapMask]->IsTexture();
		
		
		float fDetailBlendFactor = GetFloatParam( info.m_nDetailTextureBlendFactor, params, 1.0 );

		if ( pShaderShadow || bNeedRegenStaticCmds )
		{
			bool hasVertexColor = IS_FLAG_SET( MATERIAL_VAR_VERTEXCOLOR );
			bool hasDiffuseBumpmap = hasBump && (params[info.m_nNoDiffuseBumpLighting]->GetIntValue() == 0);

			bool hasEnvmap = params[info.m_nEnvmap]->IsTexture();

			bool bSeamlessMapping = ( ( info.m_nSeamlessMappingScale != -1 ) && 
									  ( params[info.m_nSeamlessMappingScale]->GetFloatValue() != 0.0 ) );
			
			if ( bNeedRegenStaticCmds )
			{
				pContextData->ResetStaticCmds();
				CCommandBufferBuilder< CFixedCommandStorageBuffer< 5000 > > staticCmdsBuf;


				if( !hasBaseTexture )
				{
					if( hasEnvmap )
					{
						// if we only have an envmap (no basetexture), then we want the albedo to be black.
						staticCmdsBuf.BindStandardTexture( SHADER_SAMPLER0, TEXTURE_BLACK );
					}
					else
					{
						staticCmdsBuf.BindStandardTexture( SHADER_SAMPLER0, TEXTURE_WHITE );
					}
				}
				staticCmdsBuf.BindStandardTexture( SHADER_SAMPLER1, TEXTURE_LIGHTMAP );

				if ( bSeamlessMapping )
				{
					staticCmdsBuf.SetVertexShaderConstant4(
						VERTEX_SHADER_SHADER_SPECIFIC_CONST_0,
						params[info.m_nSeamlessMappingScale]->GetFloatValue(),0,0,0 );
				}
				staticCmdsBuf.StoreEyePosInPixelShaderConstant( 10 );
				staticCmdsBuf.SetPixelShaderFogParams( 11 );
				staticCmdsBuf.End();
				// now, copy buf
				pContextData->m_pStaticCmds = new uint8[staticCmdsBuf.Size()];
				memcpy( pContextData->m_pStaticCmds, staticCmdsBuf.Base(), staticCmdsBuf.Size() );
			}
			if ( pShaderShadow )
			{

				// Alpha test: FIXME: shouldn't this be handled in Shader_t::SetInitialShadowState
				pShaderShadow->EnableAlphaTest( bIsAlphaTested );
				if ( info.m_nAlphaTestReference != -1 && params[info.m_nAlphaTestReference]->GetFloatValue() > 0.0f )
				{
					pShaderShadow->AlphaFunc( SHADER_ALPHAFUNC_GEQUAL, params[info.m_nAlphaTestReference]->GetFloatValue() );
				}

				pShader->SetDefaultBlendingShadowState( nAlphaChannelTextureVar, hasBaseTexture );

				unsigned int flags = VERTEX_POSITION;

				// base texture
				pShaderShadow->EnableTexture( SHADER_SAMPLER0, true );
				pShaderShadow->EnableSRGBRead( SHADER_SAMPLER0, true );

				if ( hasLightWarpTexture )
				{
					pShaderShadow->EnableTexture( SHADER_SAMPLER6, true );
					pShaderShadow->EnableSRGBRead( SHADER_SAMPLER6, false );
				}
				if ( bHasBlendModulateTexture )
				{
					pShaderShadow->EnableTexture( SHADER_SAMPLER3, true );
					pShaderShadow->EnableSRGBRead( SHADER_SAMPLER3, false );
				}

				if ( hasBaseTexture2 )
				{
					pShaderShadow->EnableTexture( SHADER_SAMPLER7, true );
					pShaderShadow->EnableSRGBRead( SHADER_SAMPLER7, true );
				}
//		if( hasLightmap )
				pShaderShadow->EnableTexture( SHADER_SAMPLER1, true );
				if( g_pHardwareConfig->GetHDRType() == HDR_TYPE_NONE )
				{
					pShaderShadow->EnableSRGBRead( SHADER_SAMPLER1, true );
				}
				else
				{
					pShaderShadow->EnableSRGBRead( SHADER_SAMPLER1, false );
				}

				if( hasEnvmap || ( IsX360() && hasFlashlight ) )
				{
					if( hasEnvmap )
					{
						pShaderShadow->EnableTexture( SHADER_SAMPLER2, true );
						if( g_pHardwareConfig->GetHDRType() == HDR_TYPE_NONE )
						{
							pShaderShadow->EnableSRGBRead( SHADER_SAMPLER2, true );
						}
					}
					flags |= VERTEX_TANGENT_S | VERTEX_TANGENT_T | VERTEX_NORMAL;
				}

				int nDetailBlendMode = 0;
				if ( hasDetailTexture )
				{
					nDetailBlendMode = GetIntParam( info.m_nDetailTextureCombineMode, params );
					ITexture *pDetailTexture = params[info.m_nDetail]->GetTextureValue();
					if ( pDetailTexture->GetFlags() & TEXTUREFLAGS_SSBUMP )
					{
						if ( hasBump )
							nDetailBlendMode = 10;					// ssbump
						else
							nDetailBlendMode = 11;					// ssbump_nobump
					}
				}

				if( hasDetailTexture )
				{
					pShaderShadow->EnableTexture( SHADER_SAMPLER12, true );
					bool bSRGBState = ( nDetailBlendMode == 1 );
					pShaderShadow->EnableSRGBRead( SHADER_SAMPLER12, bSRGBState );
				}

				if( hasBump || hasNormalMapAlphaEnvmapMask )
				{
					pShaderShadow->EnableTexture( SHADER_SAMPLER4, true );
				}
				if( hasBump2 )
				{
					pShaderShadow->EnableTexture( SHADER_SAMPLER5, true );
				}
				if( hasBumpMask )
				{
					pShaderShadow->EnableTexture( SHADER_SAMPLER8, true );
				}
				if( hasEnvmapMask )
				{
					pShaderShadow->EnableTexture( SHADER_SAMPLER5, true );
				}

				if( hasFlashlight && IsX360() )
				{
					pShaderShadow->EnableTexture( SHADER_SAMPLER13, true );
					pShaderShadow->EnableTexture( SHADER_SAMPLER14, true );
					pShaderShadow->SetShadowDepthFiltering( SHADER_SAMPLER14 );
					pShaderShadow->EnableTexture( SHADER_SAMPLER15, true );
				}

				if( hasVertexColor || hasBaseTexture2 || hasBump2 )
				{
					flags |= VERTEX_COLOR;
				}

				// texcoord0 : base texcoord
				// texcoord1 : lightmap texcoord
				// texcoord2 : lightmap texcoord offset
				int numTexCoords = 2;
				if( hasBump )
				{
					numTexCoords = 3;
				}
		
				pShaderShadow->VertexShaderVertexFormat( flags, numTexCoords, 0, 0 );

				// Pre-cache pixel shaders
				bool hasBaseAlphaEnvmapMask = IS_FLAG_SET( MATERIAL_VAR_BASEALPHAENVMAPMASK );

				int bumpmap_variant=(hasSSBump) ? 2 : hasBump;
				bool bMaskedBlending=( (info.m_nMaskedBlending != -1) &&
									   (params[info.m_nMaskedBlending]->GetIntValue() != 0) );

				DECLARE_STATIC_VERTEX_SHADER( lightmappedgeneric_vs20 );
				SET_STATIC_VERTEX_SHADER_COMBO( ENVMAP_MASK,  hasEnvmapMask );
				SET_STATIC_VERTEX_SHADER_COMBO( TANGENTSPACE,  params[info.m_nEnvmap]->IsTexture() );
				SET_STATIC_VERTEX_SHADER_COMBO( BUMPMAP,  hasBump );
				SET_STATIC_VERTEX_SHADER_COMBO( DIFFUSEBUMPMAP, hasDiffuseBumpmap );
				SET_STATIC_VERTEX_SHADER_COMBO( VERTEXCOLOR, IS_FLAG_SET( MATERIAL_VAR_VERTEXCOLOR ) );
				SET_STATIC_VERTEX_SHADER_COMBO( VERTEXALPHATEXBLENDFACTOR, hasBaseTexture2 || hasBump2 );
				SET_STATIC_VERTEX_SHADER_COMBO( BUMPMASK, hasBumpMask );

				bool bReliefMapping = false; //( bumpmap_variant == 2 ) && ( ! bSeamlessMapping );
				SET_STATIC_VERTEX_SHADER_COMBO( RELIEF_MAPPING, false );//bReliefMapping );
				SET_STATIC_VERTEX_SHADER_COMBO( SEAMLESS, bSeamlessMapping );
#ifdef _X360
				SET_STATIC_VERTEX_SHADER_COMBO( FLASHLIGHT, hasFlashlight);
#endif
				SET_STATIC_VERTEX_SHADER( lightmappedgeneric_vs20 );

				if ( g_pHardwareConfig->SupportsPixelShaders_2_b() )
				{
					DECLARE_STATIC_PIXEL_SHADER( lightmappedgeneric_ps20b );
					SET_STATIC_PIXEL_SHADER_COMBO( BASETEXTURE2, hasBaseTexture2 );
					SET_STATIC_PIXEL_SHADER_COMBO( DETAILTEXTURE, hasDetailTexture );
					SET_STATIC_PIXEL_SHADER_COMBO( BUMPMAP,  bumpmap_variant );
					SET_STATIC_PIXEL_SHADER_COMBO( BUMPMAP2, hasBump2 );
					SET_STATIC_PIXEL_SHADER_COMBO( BUMPMASK, hasBumpMask );
					SET_STATIC_PIXEL_SHADER_COMBO( DIFFUSEBUMPMAP,  hasDiffuseBumpmap );
					SET_STATIC_PIXEL_SHADER_COMBO( CUBEMAP,  hasEnvmap );
					SET_STATIC_PIXEL_SHADER_COMBO( ENVMAPMASK,  hasEnvmapMask );
					SET_STATIC_PIXEL_SHADER_COMBO( BASEALPHAENVMAPMASK,  hasBaseAlphaEnvmapMask );
					SET_STATIC_PIXEL_SHADER_COMBO( SELFILLUM,  hasSelfIllum );
					SET_STATIC_PIXEL_SHADER_COMBO( NORMALMAPALPHAENVMAPMASK,  hasNormalMapAlphaEnvmapMask );
					SET_STATIC_PIXEL_SHADER_COMBO( BASETEXTURENOENVMAP,  params[info.m_nBaseTextureNoEnvmap]->GetIntValue() );
					SET_STATIC_PIXEL_SHADER_COMBO( BASETEXTURE2NOENVMAP, params[info.m_nBaseTexture2NoEnvmap]->GetIntValue() );
					SET_STATIC_PIXEL_SHADER_COMBO( WARPLIGHTING, hasLightWarpTexture );
					SET_STATIC_PIXEL_SHADER_COMBO( FANCY_BLENDING, bHasBlendModulateTexture );
					SET_STATIC_PIXEL_SHADER_COMBO( MASKEDBLENDING, bMaskedBlending);
					SET_STATIC_PIXEL_SHADER_COMBO( RELIEF_MAPPING, bReliefMapping );
					SET_STATIC_PIXEL_SHADER_COMBO( SEAMLESS, bSeamlessMapping );
					SET_STATIC_PIXEL_SHADER_COMBO( OUTLINE, bHasOutline );
					SET_STATIC_PIXEL_SHADER_COMBO( SOFTEDGES, bHasSoftEdges );
					SET_STATIC_PIXEL_SHADER_COMBO( DETAIL_BLEND_MODE, nDetailBlendMode );
					SET_STATIC_PIXEL_SHADER_COMBO( NORMAL_DECODE_MODE, (int)  NORMAL_DECODE_NONE );
					SET_STATIC_PIXEL_SHADER_COMBO( NORMALMASK_DECODE_MODE, (int) NORMAL_DECODE_NONE );
#ifdef _X360
					SET_STATIC_PIXEL_SHADER_COMBO( FLASHLIGHT, hasFlashlight);
#endif
					SET_STATIC_PIXEL_SHADER( lightmappedgeneric_ps20b );
				}
				else
				{
					DECLARE_STATIC_PIXEL_SHADER( lightmappedgeneric_ps20 );
					SET_STATIC_PIXEL_SHADER_COMBO( BASETEXTURE2, hasBaseTexture2 );
					SET_STATIC_PIXEL_SHADER_COMBO( DETAILTEXTURE, hasDetailTexture );
					SET_STATIC_PIXEL_SHADER_COMBO( BUMPMAP,  bumpmap_variant );
					SET_STATIC_PIXEL_SHADER_COMBO( BUMPMAP2, hasBump2 );
					SET_STATIC_PIXEL_SHADER_COMBO( BUMPMASK, hasBumpMask );
					SET_STATIC_PIXEL_SHADER_COMBO( DIFFUSEBUMPMAP,  hasDiffuseBumpmap );
					SET_STATIC_PIXEL_SHADER_COMBO( CUBEMAP,  hasEnvmap );
					SET_STATIC_PIXEL_SHADER_COMBO( ENVMAPMASK,  hasEnvmapMask );
					SET_STATIC_PIXEL_SHADER_COMBO( BASEALPHAENVMAPMASK,  hasBaseAlphaEnvmapMask );
					SET_STATIC_PIXEL_SHADER_COMBO( SELFILLUM,  hasSelfIllum );
					SET_STATIC_PIXEL_SHADER_COMBO( NORMALMAPALPHAENVMAPMASK,  hasNormalMapAlphaEnvmapMask );
					SET_STATIC_PIXEL_SHADER_COMBO( BASETEXTURENOENVMAP,  params[info.m_nBaseTextureNoEnvmap]->GetIntValue() );
					SET_STATIC_PIXEL_SHADER_COMBO( BASETEXTURE2NOENVMAP, params[info.m_nBaseTexture2NoEnvmap]->GetIntValue() );
					SET_STATIC_PIXEL_SHADER_COMBO( WARPLIGHTING, hasLightWarpTexture );
					SET_STATIC_PIXEL_SHADER_COMBO( FANCY_BLENDING, bHasBlendModulateTexture );
					SET_STATIC_PIXEL_SHADER_COMBO( MASKEDBLENDING, bMaskedBlending);
					SET_STATIC_PIXEL_SHADER_COMBO( SEAMLESS, bSeamlessMapping );
					SET_STATIC_PIXEL_SHADER_COMBO( OUTLINE, bHasOutline );
					SET_STATIC_PIXEL_SHADER_COMBO( SOFTEDGES, bHasSoftEdges );
					SET_STATIC_PIXEL_SHADER_COMBO( DETAIL_BLEND_MODE, nDetailBlendMode );
					SET_STATIC_PIXEL_SHADER_COMBO( NORMAL_DECODE_MODE, 0 );					// No normal compression with ps_2_0	(yikes!)
					SET_STATIC_PIXEL_SHADER_COMBO( NORMALMASK_DECODE_MODE, 0 );				// No normal compression with ps_2_0
					SET_STATIC_PIXEL_SHADER( lightmappedgeneric_ps20 );
				}
				// HACK HACK HACK - enable alpha writes all the time so that we have them for
				// underwater stuff and writing depth to dest alpha
				// But only do it if we're not using the alpha already for translucency
				pShaderShadow->EnableAlphaWrites( bFullyOpaque );

				pShaderShadow->EnableSRGBWrite( true );

				pShader->DefaultFog();


			} // end shadow state
		} // end shadow || regen display list
		if ( pShaderAPI && pContextData->m_bMaterialVarsChanged )
		{
			// need to regenerate the semistatic cmds
			pContextData->m_SemiStaticCmdsOut.Reset();
			pContextData->m_bMaterialVarsChanged = false;

			bool bHasBlendMaskTransform= (
				(info.m_nBlendMaskTransform != -1) &&
				(info.m_nMaskedBlending != -1) &&
				(params[info.m_nMaskedBlending]->GetIntValue() ) &&
				( ! (params[info.m_nBumpTransform]->MatrixIsIdentity() ) ) );
			
			// If we don't have a texture transform, we don't have
			// to set vertex shader constants or run vertex shader instructions
			// for the texture transform.
			bool bHasTextureTransform = 
				!( params[info.m_nBaseTextureTransform]->MatrixIsIdentity() &&
				   params[info.m_nBumpTransform]->MatrixIsIdentity() &&
				   params[info.m_nBumpTransform2]->MatrixIsIdentity() &&
				   params[info.m_nEnvmapMaskTransform]->MatrixIsIdentity() );
			
			bHasTextureTransform |= bHasBlendMaskTransform;
			
			pContextData->m_bVertexShaderFastPath = !bHasTextureTransform;

			if( params[info.m_nDetail]->IsTexture() )
			{
				pContextData->m_bVertexShaderFastPath = false;
			}
			if (bHasBlendMaskTransform)
			{
				pContextData->m_SemiStaticCmdsOut.SetVertexShaderTextureTransform( 
					VERTEX_SHADER_SHADER_SPECIFIC_CONST_10, info.m_nBlendMaskTransform );
			}

			if ( ! pContextData->m_bVertexShaderFastPath )
			{
				bool bSeamlessMapping = ( ( info.m_nSeamlessMappingScale != -1 ) && 
										  ( params[info.m_nSeamlessMappingScale]->GetFloatValue() != 0.0 ) );
				bool hasEnvmapMask = params[info.m_nEnvmapMask]->IsTexture();
				if (!bSeamlessMapping )
					pContextData->m_SemiStaticCmdsOut.SetVertexShaderTextureTransform( VERTEX_SHADER_SHADER_SPECIFIC_CONST_0, info.m_nBaseTextureTransform );
				// If we have a detail texture, then the bump texcoords are the same as the base texcoords.
				if( hasBump && !hasDetailTexture )
				{
					pContextData->m_SemiStaticCmdsOut.SetVertexShaderTextureTransform( VERTEX_SHADER_SHADER_SPECIFIC_CONST_2, info.m_nBumpTransform );
				}
				if( hasEnvmapMask )
				{
					pContextData->m_SemiStaticCmdsOut.SetVertexShaderTextureTransform( VERTEX_SHADER_SHADER_SPECIFIC_CONST_4, info.m_nEnvmapMaskTransform );
				}
				else if ( hasBump2 )
				{
					pContextData->m_SemiStaticCmdsOut.SetVertexShaderTextureTransform( VERTEX_SHADER_SHADER_SPECIFIC_CONST_4, info.m_nBumpTransform2 );
				}
			}
			pContextData->m_SemiStaticCmdsOut.SetEnvMapTintPixelShaderDynamicState( 0, info.m_nEnvmapTint );
			// set up shader modulation color
			float color[4] = { 1.0, 1.0, 1.0, 1.0 };
			pShader->ComputeModulationColor( color );
			float flLScale = pShaderAPI->GetLightMapScaleFactor();
			color[0] *= flLScale;
			color[1] *= flLScale;
			color[2] *= flLScale;

			pContextData->m_SemiStaticCmdsOut.SetVertexShaderConstant( VERTEX_SHADER_MODULATION_COLOR, color );

			color[3] *= ( IS_PARAM_DEFINED( info.m_nAlpha2 ) && params[ info.m_nAlpha2 ]->GetFloatValue() > 0.0f ) ? params[ info.m_nAlpha2 ]->GetFloatValue() : 1.0f;
			pContextData->m_SemiStaticCmdsOut.SetPixelShaderConstant( 12, color );
			
			if ( hasDetailTexture )
			{
				float detailTintAndBlend[4] = {1, 1, 1, 1};
				
				if ( info.m_nDetailTint != -1 )
				{
					params[info.m_nDetailTint]->GetVecValue( detailTintAndBlend, 3 );
				}
				
				detailTintAndBlend[3] = fDetailBlendFactor;
				pContextData->m_SemiStaticCmdsOut.SetPixelShaderConstant( 8, detailTintAndBlend );
			}
			
			float envmapTintVal[4];
			float selfIllumTintVal[4];
			params[info.m_nEnvmapTint]->GetVecValue( envmapTintVal, 3 );
			params[info.m_nSelfIllumTint]->GetVecValue( selfIllumTintVal, 3 );
			float envmapContrast = params[info.m_nEnvmapContrast]->GetFloatValue();
			float envmapSaturation = params[info.m_nEnvmapSaturation]->GetFloatValue();
			float fresnelReflection = params[info.m_nFresnelReflection]->GetFloatValue();
			bool hasEnvmap = params[info.m_nEnvmap]->IsTexture();

			pContextData->m_bPixelShaderFastPath = true;
			bool bUsingContrast = hasEnvmap && ( (envmapContrast != 0.0f) && (envmapContrast != 1.0f) ) && (envmapSaturation != 1.0f);
			bool bUsingFresnel = hasEnvmap && (fresnelReflection != 1.0f);
			bool bUsingSelfIllumTint = IS_FLAG_SET(MATERIAL_VAR_SELFILLUM) && (selfIllumTintVal[0] != 1.0f || selfIllumTintVal[1] != 1.0f || selfIllumTintVal[2] != 1.0f); 
			if ( bUsingContrast || bUsingFresnel || bUsingSelfIllumTint || !g_pConfig->bShowSpecular )
			{
				pContextData->m_bPixelShaderFastPath = false;
			}
			if( !pContextData->m_bPixelShaderFastPath )
			{
				pContextData->m_SemiStaticCmdsOut.SetPixelShaderConstants( 2, 3 );
				pContextData->m_SemiStaticCmdsOut.OutputConstantData( params[info.m_nEnvmapContrast]->GetVecValue() );
				pContextData->m_SemiStaticCmdsOut.OutputConstantData( params[info.m_nEnvmapSaturation]->GetVecValue() );
				float flFresnel = params[info.m_nFresnelReflection]->GetFloatValue();
				// [ 0, 0, 1-R(0), R(0) ]
				pContextData->m_SemiStaticCmdsOut.OutputConstantData4( 0., 0., 1.0 - flFresnel, flFresnel );
				
				pContextData->m_SemiStaticCmdsOut.SetPixelShaderConstant( 7, params[info.m_nSelfIllumTint]->GetVecValue() );
			}
			else
			{
				if ( bHasOutline )
				{
					float flOutlineParms[8] = { GetFloatParam( info.m_nOutlineStart0, params ),
												GetFloatParam( info.m_nOutlineStart1, params ),
												GetFloatParam( info.m_nOutlineEnd0, params ),
												GetFloatParam( info.m_nOutlineEnd1, params ),
												0,0,0,
												GetFloatParam( info.m_nOutlineAlpha, params ) };
					if ( info.m_nOutlineColor != -1 )
					{
						params[info.m_nOutlineColor]->GetVecValue( flOutlineParms + 4, 3 );
					}
					pContextData->m_SemiStaticCmdsOut.SetPixelShaderConstant( 2, flOutlineParms, 2 );
				}
				
				if ( bHasSoftEdges )
				{
					pContextData->m_SemiStaticCmdsOut.SetPixelShaderConstant4( 
						4, GetFloatParam( info.m_nEdgeSoftnessStart, params ),
						GetFloatParam( info.m_nEdgeSoftnessEnd, params ),
						0,0 );
				}
			}
			// texture binds
			if( hasBaseTexture )
			{
				pContextData->m_SemiStaticCmdsOut.BindTexture( pShader, SHADER_SAMPLER0, info.m_nBaseTexture, info.m_nBaseTextureFrame );
			}
			// handle mat_fullbright 2
			bool bLightingOnly = mat_fullbright.GetInt() == 2 && !IS_FLAG_SET( MATERIAL_VAR_NO_DEBUG_OVERRIDE );
			if( bLightingOnly )
			{
				// BASE TEXTURE
				if( hasSelfIllum )
				{
					pContextData->m_SemiStaticCmdsOut.BindStandardTexture( SHADER_SAMPLER0, TEXTURE_GREY_ALPHA_ZERO );
				}
				else
				{
					pContextData->m_SemiStaticCmdsOut.BindStandardTexture( SHADER_SAMPLER0, TEXTURE_GREY );
				}
				
				// BASE TEXTURE 2	
				if( hasBaseTexture2 )
				{
					pContextData->m_SemiStaticCmdsOut.BindStandardTexture( SHADER_SAMPLER7, TEXTURE_GREY );
				}

				// DETAIL TEXTURE
				if( hasDetailTexture )
				{
					pContextData->m_SemiStaticCmdsOut.BindStandardTexture( SHADER_SAMPLER12, TEXTURE_GREY );
				}

				// disable color modulation
				float color[4] = { 0.0f, 0.0f, 0.0f, 0.0f };
				pContextData->m_SemiStaticCmdsOut.SetVertexShaderConstant( VERTEX_SHADER_MODULATION_COLOR, color );

				// turn off environment mapping
				envmapTintVal[0] = 0.0f;
				envmapTintVal[1] = 0.0f;
				envmapTintVal[2] = 0.0f;
			}

			// always set the transform for detail textures since I'm assuming that you'll
			// always have a detailscale.
			if( hasDetailTexture )
			{
				pContextData->m_SemiStaticCmdsOut.SetVertexShaderTextureScaledTransform( VERTEX_SHADER_SHADER_SPECIFIC_CONST_2, info.m_nBaseTextureTransform, info.m_nDetailScale );
			}
			
			if( hasBaseTexture2 )
			{
				pContextData->m_SemiStaticCmdsOut.BindTexture( pShader, SHADER_SAMPLER7, info.m_nBaseTexture2, info.m_nBaseTexture2Frame );
			}
			if( hasDetailTexture )
			{
				pContextData->m_SemiStaticCmdsOut.BindTexture( pShader, SHADER_SAMPLER12, info.m_nDetail, info.m_nDetailFrame );
			}

			if( hasBump || hasNormalMapAlphaEnvmapMask )
			{
				if( !g_pConfig->m_bFastNoBump )
				{
					pContextData->m_SemiStaticCmdsOut.BindTexture( pShader, SHADER_SAMPLER4, info.m_nBumpmap, info.m_nBumpFrame );
				}
				else
				{
					pContextData->m_SemiStaticCmdsOut.BindStandardTexture( SHADER_SAMPLER4, TEXTURE_NORMALMAP_FLAT );
				}
			}
			if( hasBump2 )
			{
				if( !g_pConfig->m_bFastNoBump )
				{
					pContextData->m_SemiStaticCmdsOut.BindTexture( pShader, SHADER_SAMPLER5, info.m_nBumpmap2, info.m_nBumpFrame2 );
				}
				else
				{
					pContextData->m_SemiStaticCmdsOut.BindStandardTexture( SHADER_SAMPLER5, TEXTURE_NORMALMAP_FLAT );
				}
			}
			if( hasBumpMask )
			{
				if( !g_pConfig->m_bFastNoBump )
				{
					pContextData->m_SemiStaticCmdsOut.BindTexture( pShader, SHADER_SAMPLER8, info.m_nBumpMask, -1 );
				}
				else
				{
					pContextData->m_SemiStaticCmdsOut.BindStandardTexture( SHADER_SAMPLER8, TEXTURE_NORMALMAP_FLAT );
				}
			}
			
			if( hasEnvmapMask )
			{
				pContextData->m_SemiStaticCmdsOut.BindTexture( pShader, SHADER_SAMPLER5, info.m_nEnvmapMask, info.m_nEnvmapMaskFrame );
			}
			
			if ( hasLightWarpTexture )
			{
				pContextData->m_SemiStaticCmdsOut.BindTexture( pShader, SHADER_SAMPLER6, info.m_nLightWarpTexture, -1 );
			}
			
			if ( bHasBlendModulateTexture )
			{
				pContextData->m_SemiStaticCmdsOut.BindTexture( pShader, SHADER_SAMPLER3, info.m_nBlendModulateTexture, -1 );
			}

			pContextData->m_SemiStaticCmdsOut.End();
		}
	}
	DYNAMIC_STATE
	{
		CCommandBufferBuilder< CFixedCommandStorageBuffer< 1000 > > DynamicCmdsOut;
		DynamicCmdsOut.Call( pContextData->m_pStaticCmds );
		DynamicCmdsOut.Call( pContextData->m_SemiStaticCmdsOut.Base() );

		bool hasEnvmap = params[info.m_nEnvmap]->IsTexture();

		if( hasEnvmap )
		{
			DynamicCmdsOut.BindTexture( pShader, SHADER_SAMPLER2, info.m_nEnvmap, info.m_nEnvmapFrame );
		}
		int nFixedLightingMode = pShaderAPI->GetIntRenderingParameter( INT_RENDERPARM_ENABLE_FIXED_LIGHTING );

		bool bVertexShaderFastPath = pContextData->m_bVertexShaderFastPath;

		if( nFixedLightingMode != 0 )
		{
			if ( pContextData->m_bPixelShaderForceFastPathBecauseOutline )
				nFixedLightingMode = 0;
			else
				bVertexShaderFastPath = false;
		}

		MaterialFogMode_t fogType = pShaderAPI->GetSceneFogMode();
		DECLARE_DYNAMIC_VERTEX_SHADER( lightmappedgeneric_vs20 );
		SET_DYNAMIC_VERTEX_SHADER_COMBO( DOWATERFOG,  fogType == MATERIAL_FOG_LINEAR_BELOW_FOG_Z );
		SET_DYNAMIC_VERTEX_SHADER_COMBO( FASTPATH,  bVertexShaderFastPath );
		SET_DYNAMIC_VERTEX_SHADER_COMBO( 
			LIGHTING_PREVIEW, 
			(nFixedLightingMode)?1:0
			);
		SET_DYNAMIC_VERTEX_SHADER_CMD( DynamicCmdsOut, lightmappedgeneric_vs20 );

		bool bPixelShaderFastPath = pContextData->m_bPixelShaderFastPath;
		if( nFixedLightingMode !=0 )
		{
			bPixelShaderFastPath = false;
		}
		bool bWriteDepthToAlpha;
		bool bWriteWaterFogToAlpha;
		if(  pContextData->m_bFullyOpaque ) 
		{
			bWriteDepthToAlpha = pShaderAPI->ShouldWriteDepthToDestAlpha();
			bWriteWaterFogToAlpha = (fogType == MATERIAL_FOG_LINEAR_BELOW_FOG_Z);
			AssertMsg( !(bWriteDepthToAlpha && bWriteWaterFogToAlpha), "Can't write two values to alpha at the same time." );
		}
		else
		{
			//can't write a special value to dest alpha if we're actually using as-intended alpha
			bWriteDepthToAlpha = false;
			bWriteWaterFogToAlpha = false;
		}

		float envmapContrast = params[info.m_nEnvmapContrast]->GetFloatValue();
		if ( g_pHardwareConfig->SupportsPixelShaders_2_b() )
		{
			DECLARE_DYNAMIC_PIXEL_SHADER( lightmappedgeneric_ps20b );
			SET_DYNAMIC_PIXEL_SHADER_COMBO( FASTPATH,  bPixelShaderFastPath || pContextData->m_bPixelShaderForceFastPathBecauseOutline );
 			SET_DYNAMIC_PIXEL_SHADER_COMBO( FASTPATHENVMAPCONTRAST,  bPixelShaderFastPath && envmapContrast == 1.0f );
			SET_DYNAMIC_PIXEL_SHADER_COMBO( PIXELFOGTYPE, pShaderAPI->GetPixelFogCombo() );
			
			// Don't write fog to alpha if we're using translucency
			SET_DYNAMIC_PIXEL_SHADER_COMBO( WRITE_DEPTH_TO_DESTALPHA, bWriteDepthToAlpha );
			SET_DYNAMIC_PIXEL_SHADER_COMBO( WRITEWATERFOGTODESTALPHA, bWriteWaterFogToAlpha );
			SET_DYNAMIC_PIXEL_SHADER_COMBO( LIGHTING_PREVIEW, nFixedLightingMode );
			
			SET_DYNAMIC_PIXEL_SHADER_CMD( DynamicCmdsOut, lightmappedgeneric_ps20b );
		}
		else
		{
			DECLARE_DYNAMIC_PIXEL_SHADER( lightmappedgeneric_ps20 );
			SET_DYNAMIC_PIXEL_SHADER_COMBO( FASTPATH,  bPixelShaderFastPath );
 			SET_DYNAMIC_PIXEL_SHADER_COMBO( FASTPATHENVMAPCONTRAST,  bPixelShaderFastPath && envmapContrast == 1.0f );
			SET_DYNAMIC_PIXEL_SHADER_COMBO( PIXELFOGTYPE, pShaderAPI->GetPixelFogCombo() );
			
			// Don't write fog to alpha if we're using translucency
			SET_DYNAMIC_PIXEL_SHADER_COMBO( WRITEWATERFOGTODESTALPHA, bWriteWaterFogToAlpha );
			SET_DYNAMIC_PIXEL_SHADER_COMBO(	LIGHTING_PREVIEW, nFixedLightingMode );
			
			SET_DYNAMIC_PIXEL_SHADER_CMD( DynamicCmdsOut, lightmappedgeneric_ps20 );
		}

		if( hasFlashlight && IsX360() )
		{
			VMatrix worldToTexture;
			ITexture *pFlashlightDepthTexture;
			FlashlightState_t flashlightState = pShaderAPI->GetFlashlightStateEx( worldToTexture, &pFlashlightDepthTexture );

			DynamicCmdsOut.SetVertexShaderConstant( VERTEX_SHADER_SHADER_SPECIFIC_CONST_6, worldToTexture.Base(), 4 );

			SetFlashLightColorFromState( flashlightState, pShaderAPI );

			float atten[4], pos[4];
			atten[0] = flashlightState.m_fConstantAtten;		// Set the flashlight attenuation factors
			atten[1] = flashlightState.m_fLinearAtten;
			atten[2] = flashlightState.m_fQuadraticAtten;
			atten[3] = flashlightState.m_FarZ;
			DynamicCmdsOut.SetPixelShaderConstant( 13, atten, 1 );

			pos[0] = flashlightState.m_vecLightOrigin[0];		// Set the flashlight origin
			pos[1] = flashlightState.m_vecLightOrigin[1];
			pos[2] = flashlightState.m_vecLightOrigin[2];
			DynamicCmdsOut.SetPixelShaderConstant( 14, pos, 1 );

			pShader->BindTexture( SHADER_SAMPLER13, flashlightState.m_pSpotlightTexture, flashlightState.m_nSpotlightTextureFrame );

			if( pFlashlightDepthTexture && g_pConfig->ShadowDepthTexture() && flashlightState.m_bEnableShadows )
			{
				pShader->BindTexture( SHADER_SAMPLER14, pFlashlightDepthTexture, 0 );
				DynamicCmdsOut.BindStandardTexture( SHADER_SAMPLER15, TEXTURE_SHADOW_NOISE_2D );

				// Tweaks associated with a given flashlight
				float tweaks[4];
				tweaks[0] = ShadowFilterFromState( flashlightState );
				tweaks[1] = ShadowAttenFromState( flashlightState );
				pShader->HashShadow2DJitter( flashlightState.m_flShadowJitterSeed, &tweaks[2], &tweaks[3] );
				DynamicCmdsOut.SetPixelShaderConstant( 19, tweaks, 1 );

				// Dimensions of screen, used for screen-space noise map sampling
				float vScreenScale[4] = {1280.0f / 32.0f, 720.0f / 32.0f, 0, 0};
				int nWidth, nHeight;
				pShaderAPI->GetBackBufferDimensions( nWidth, nHeight );
				vScreenScale[0] = (float) nWidth  / 32.0f;
				vScreenScale[1] = (float) nHeight / 32.0f;
				DynamicCmdsOut.SetPixelShaderConstant( 31, vScreenScale, 1 );
			}
		}

		DynamicCmdsOut.End();
		pShaderAPI->ExecuteCommandBuffer( DynamicCmdsOut.Base() );
	}
	pShader->Draw();

	if( IsPC() && (IS_FLAG_SET( MATERIAL_VAR_ALPHATEST ) != 0) && pContextData->m_bFullyOpaqueWithoutAlphaTest )
	{
		//Alpha testing makes it so we can't write to dest alpha
		//Writing to depth makes it so later polygons can't write to dest alpha either
		//This leads to situations with garbage in dest alpha.

		//Fix it now by converting depth to dest alpha for any pixels that just wrote.
		pShader->DrawEqualDepthToDestAlpha();
	}
}

void DrawLightmappedGeneric_DX9(CBaseVSShader *pShader, IMaterialVar** params,
										 IShaderDynamicAPI *pShaderAPI, IShaderShadow* pShaderShadow, 
										 LightmappedGeneric_DX9_Vars_t &info,
										 CBasePerMaterialContextData **pContextDataPtr,
										 VertexCompressionType_t vertexCompression)
{
	if (mat_reducefillrate.GetInt() < 2)
	{
		bool hasFlashlight = pShader->UsingFlashlight(params);
		DrawLightmappedGeneric_DX9_Internal(pShader, params, hasFlashlight, pShaderAPI, pShaderShadow, info, pContextDataPtr);
		return;
	}
	// Setting up booleans
	bool bHasBaseTexture = (info.m_nBaseTexture != -1) && params[info.m_nBaseTexture]->IsTexture();
	bool bHasNormalTexture = (info.m_nBumpmap != -1) && params[info.m_nBumpmap]->IsTexture();
	bool bHasMraoTexture = (info.mraoTexture != -1) && params[info.mraoTexture]->IsTexture();
	bool bHasEmissionTexture = (info.emissionTexture != -1) && params[info.emissionTexture]->IsTexture();
	bool bHasEnvTexture = (info.m_nEnvmap != -1) && params[info.m_nEnvmap]->IsTexture();
	bool bIsAlphaTested = IS_FLAG_SET(MATERIAL_VAR_ALPHATEST) != 0;
	bool bHasFlashlight = pShader->UsingFlashlight(params);
	bool bHasColor = (info.baseColor != -1) && params[info.baseColor]->IsDefined();
	bool bLightMapped = !IS_FLAG_SET(MATERIAL_VAR_MODEL) && !IS_FLAG_SET(MATERIAL_VAR_DECAL);
	//bool bUseEnvAmbient = (info.useEnvAmbient != -1) && (params[info.useEnvAmbient]->GetIntValue() == 1);
	bool bUseEnvAmbient = true;
	bool bHasSpecularTexture = (info.specularTexture != -1) && params[info.specularTexture]->IsTexture();
	bool hasEnvmapMask = (info.m_nEnvmapMask != -1) && params[info.m_nEnvmapMask]->IsTexture();
	bool hasNormalMapAlphaEnvmapMask = IS_FLAG_SET(MATERIAL_VAR_NORMALMAPALPHAENVMAPMASK);

	// Determining whether we're dealing with a fully opaque material
	BlendType_t nBlendType = pShader->EvaluateBlendRequirements(info.m_nBaseTexture, true);
	bool bFullyOpaque = (nBlendType != BT_BLENDADD) && (nBlendType != BT_BLEND) && !bIsAlphaTested;
	CLightmappedGeneric_DX9_Context* pContextData = reinterpret_cast<CLightmappedGeneric_DX9_Context*> (*pContextDataPtr);

	if (pShader->IsSnapshotting() || (!pContextData) || pContextData->m_bMaterialVarsChanged)
	{
		bool bNeedRegenStaticCmds = (!pContextData) && pShaderShadow;
		if (!pContextData)								// make sure allocated
		{
			pContextData = new CLightmappedGeneric_DX9_Context;
			*pContextDataPtr = pContextData;
		}
		if (pShaderShadow || bNeedRegenStaticCmds)
		{
			// If alphatest is on, enable it
			pShaderShadow->EnableAlphaTest(bIsAlphaTested);

			if (info.alphaTestReference != -1 && params[info.alphaTestReference]->GetFloatValue() > 0.0f)
			{
				pShaderShadow->AlphaFunc(SHADER_ALPHAFUNC_GEQUAL, params[info.alphaTestReference]->GetFloatValue());
			}

			if (bHasFlashlight)
			{
				pShaderShadow->EnableBlending(true);
				pShaderShadow->BlendFunc(SHADER_BLEND_ONE, SHADER_BLEND_ONE); // Additive blending
			}
			else
			{
				pShader->SetDefaultBlendingShadowState(info.m_nBaseTexture, true);
			}

			int nShadowFilterMode = bHasFlashlight ? g_pHardwareConfig->GetShadowFilterMode() : 0;

			// Setting up samplers
			pShaderShadow->EnableTexture(SAMPLER_BASETEXTURE, true);    // Basecolor texture
			pShaderShadow->EnableSRGBRead(SAMPLER_BASETEXTURE, true);   // Basecolor is sRGB
			pShaderShadow->EnableTexture(SAMPLER_EMISSIVE, true);       // Emission texture
			pShaderShadow->EnableSRGBRead(SAMPLER_EMISSIVE, true);      // Emission is sRGB
			pShaderShadow->EnableTexture(SAMPLER_LIGHTMAP, true);       // Lightmap texture
			pShaderShadow->EnableSRGBRead(SAMPLER_LIGHTMAP, false);     // Lightmaps aren't sRGB
			pShaderShadow->EnableTexture(SAMPLER_MRAO, true);           // MRAO texture
			pShaderShadow->EnableSRGBRead(SAMPLER_MRAO, false);         // MRAO isn't sRGB
			pShaderShadow->EnableTexture(SAMPLER_NORMAL, true);         // Normal texture
			pShaderShadow->EnableSRGBRead(SAMPLER_NORMAL, false);       // Normals aren't sRGB
			pShaderShadow->EnableTexture(SAMPLER_SPECULAR, true);       // Specular F0 texture
			pShaderShadow->EnableSRGBRead(SAMPLER_SPECULAR, true);      // Specular F0 is sRGB

			// If the flashlight is on, set up its textures
			if (bHasFlashlight)
			{
				pShaderShadow->EnableTexture(SAMPLER_SHADOWDEPTH, true);        // Shadow depth map
				pShaderShadow->SetShadowDepthFiltering(SAMPLER_SHADOWDEPTH);
				pShaderShadow->EnableSRGBRead(SAMPLER_SHADOWDEPTH, false);
				pShaderShadow->EnableTexture(SAMPLER_RANDOMROTATION, true);     // Noise map
				pShaderShadow->EnableTexture(SAMPLER_FLASHLIGHT, true);         // Flashlight cookie
				pShaderShadow->EnableSRGBRead(SAMPLER_FLASHLIGHT, true);
			}

			// Setting up envmap
			if (bHasEnvTexture)
			{
				pShaderShadow->EnableTexture(SAMPLER_ENVMAP, true); // Envmap
				if (g_pHardwareConfig->GetHDRType() == HDR_TYPE_NONE)
				{
					pShaderShadow->EnableSRGBRead(SAMPLER_ENVMAP, true); // Envmap is only sRGB with HDR disabled?
				}
			}

			// Enabling sRGB writing
			// See common_ps_fxc.h line 349
			// PS2b shaders and up write sRGB
			pShaderShadow->EnableSRGBWrite(true);

			if (IS_FLAG_SET(MATERIAL_VAR_MODEL))
			{
				// We only need the position and surface normal
				unsigned int flags = VERTEX_POSITION | VERTEX_NORMAL | VERTEX_FORMAT_COMPRESSED | VERTEX_COLOR;
				// We need three texcoords, all in the default float2 size
				pShaderShadow->VertexShaderVertexFormat(flags, 1, 0, 0);
			}
			else
			{
				// We need the position, surface normal, and vertex compression format
				unsigned int flags = VERTEX_POSITION | VERTEX_NORMAL | VERTEX_TANGENT_S | VERTEX_COLOR;
				// We only need one texcoord, in the default float2 size
				pShaderShadow->VertexShaderVertexFormat(flags, 3, 0, 0);
			}

			int useParallax = 0;
			if (info.useParallax != -1 && mat_pbr_parallaxmap.GetBool())
			{
				useParallax = params[info.useParallax]->GetIntValue();
			}



			if (!g_pHardwareConfig->SupportsShaderModel_3_0() || mat_pbr_force_20b.GetBool())
			{
				// Setting up static vertex shader
				DECLARE_STATIC_VERTEX_SHADER(pbr_vs20b);
				SET_STATIC_VERTEX_SHADER(pbr_vs20b);

				// Setting up static pixel shader
				DECLARE_STATIC_PIXEL_SHADER(pbr_ps20b);
				SET_STATIC_PIXEL_SHADER_COMBO(FLASHLIGHT, bHasFlashlight);
				SET_STATIC_PIXEL_SHADER_COMBO(FLASHLIGHTDEPTHFILTERMODE, nShadowFilterMode);
				SET_STATIC_PIXEL_SHADER_COMBO(LIGHTMAPPED, bLightMapped);
				SET_STATIC_PIXEL_SHADER_COMBO(EMISSIVE, bHasEmissionTexture);
				SET_STATIC_PIXEL_SHADER_COMBO(SPECULAR, 0);
				SET_STATIC_PIXEL_SHADER(pbr_ps20b);
			}
			else
			{
				// Setting up static vertex shader
				DECLARE_STATIC_VERTEX_SHADER(pbr_vs30);
				SET_STATIC_VERTEX_SHADER(pbr_vs30);

				// Setting up static pixel shader
				DECLARE_STATIC_PIXEL_SHADER(pbr_ps30);
				SET_STATIC_PIXEL_SHADER_COMBO(FLASHLIGHT, bHasFlashlight);
				SET_STATIC_PIXEL_SHADER_COMBO(FLASHLIGHTDEPTHFILTERMODE, nShadowFilterMode);
				SET_STATIC_PIXEL_SHADER_COMBO(LIGHTMAPPED, bLightMapped);
				SET_STATIC_PIXEL_SHADER_COMBO(USEENVAMBIENT, bUseEnvAmbient);
				SET_STATIC_PIXEL_SHADER_COMBO(EMISSIVE, bHasEmissionTexture);
				SET_STATIC_PIXEL_SHADER_COMBO(SPECULAR, bHasSpecularTexture);
				SET_STATIC_PIXEL_SHADER_COMBO(PARALLAXOCCLUSION, useParallax);
				SET_STATIC_PIXEL_SHADER_COMBO(NORMALMAPALPHAENVMAPMASK, hasNormalMapAlphaEnvmapMask);
				SET_STATIC_PIXEL_SHADER(pbr_ps30);
			}

			// Setting up fog
			pShader->DefaultFog(); // I think this is correct

			// HACK HACK HACK - enable alpha writes all the time so that we have them for underwater stuff
			pShaderShadow->EnableAlphaWrites(bFullyOpaque);
		}
		if (pShaderAPI && pContextData->m_bMaterialVarsChanged)
		{
			// need to regenerate the semistatic cmds
			pContextData->m_SemiStaticCmdsOut.Reset();
			pContextData->m_bMaterialVarsChanged = false;


			pContextData->m_bVertexShaderFastPath = IS_FLAG_SET(MATERIAL_VAR_DECAL);
		}
	}
	else // Not snapshotting -- begin dynamic state
	{
		bool bLightingOnly = mat_fullbright.GetInt() == 2 && !IS_FLAG_SET(MATERIAL_VAR_NO_DEBUG_OVERRIDE);

		// Setting up albedo texture
		if (bHasBaseTexture)
		{
			pShader->BindTexture(SAMPLER_BASETEXTURE, info.m_nBaseTexture, info.m_nBaseTextureFrame);
		}
		else
		{
			pShaderAPI->BindStandardTexture(SAMPLER_BASETEXTURE, TEXTURE_GREY);
		}

		// Setting up vmt color
		Vector color;
		if (bHasColor)
		{
			params[info.baseColor]->GetVecValue(color.Base(), 3);
		}
		else
		{
			color = Vector{ 1.f, 1.f, 1.f };
		}
		pShaderAPI->SetPixelShaderConstant(PSREG_SELFILLUMTINT, color.Base());

		// Setting up environment map
		if (bHasEnvTexture)
		{
			pShader->BindTexture(SAMPLER_ENVMAP, info.m_nEnvmap, 0);
		}
		else
		{
			pShaderAPI->BindStandardTexture(SAMPLER_ENVMAP, TEXTURE_BLACK);
		}

		// Setting up emissive texture
		if (bHasEmissionTexture)
		{
			pShader->BindTexture(SAMPLER_EMISSIVE, info.emissionTexture, 0);
		}
		else
		{
			pShaderAPI->BindStandardTexture(SAMPLER_EMISSIVE, TEXTURE_BLACK);
		}

		// Setting up normal map
		if (bHasNormalTexture)
		{
			pShader->BindTexture(SAMPLER_NORMAL, info.m_nBumpmap, 0);
		}
		else
		{
			pShaderAPI->BindStandardTexture(SAMPLER_NORMAL, TEXTURE_NORMALMAP_FLAT);
		}

		// Setting up mrao map
		if (bHasMraoTexture)
		{
			pShader->BindTexture(SAMPLER_MRAO, info.mraoTexture, 0);
		}
		else
		{
			pShaderAPI->BindStandardTexture(SAMPLER_MRAO, TEXTURE_WHITE);
		}

		if (bHasSpecularTexture)
		{
			pShader->BindTexture(SAMPLER_SPECULAR, info.specularTexture, 0);
		}
		else
		{
			pShaderAPI->BindStandardTexture(SAMPLER_SPECULAR, TEXTURE_BLACK);
		}

		if (hasEnvmapMask)
		{
			pShader->BindTexture(SHADER_SAMPLER13, info.m_nEnvmapMask, info.m_nEnvmapMaskFrame);
		}
		else
		{
			pShaderAPI->BindStandardTexture(SHADER_SAMPLER13, TEXTURE_BLACK);
		}
		// Getting the light state
		LightState_t lightState;
		pShaderAPI->GetDX9LightState(&lightState);

		// Brushes don't need ambient cubes or dynamic lights
		//if (!IS_FLAG_SET(MATERIAL_VAR_MODEL))
		//{
		//	lightState.m_bAmbientLight = false;
		//	lightState.m_nNumLights = 0;
		//}

		// Setting up the flashlight related textures and variables
		bool bFlashlightShadows = false;
		if (bHasFlashlight)
		{
			Assert(info.flashlightTexture >= 0 && info.flashlightTextureFrame >= 0);
			Assert(params[info.flashlightTexture]->IsTexture());
			pShader->BindTexture(SAMPLER_FLASHLIGHT, info.m_nFlashlightTexture, info.m_nFlashlightTextureFrame);
			VMatrix worldToTexture;
			ITexture* pFlashlightDepthTexture;
			FlashlightState_t state = pShaderAPI->GetFlashlightStateEx(worldToTexture, &pFlashlightDepthTexture);
			bFlashlightShadows = state.m_bEnableShadows && (pFlashlightDepthTexture != NULL);

			SetFlashLightColorFromState(state, pShaderAPI, PSREG_FLASHLIGHT_COLOR);

			if (pFlashlightDepthTexture && g_pConfig->ShadowDepthTexture() && state.m_bEnableShadows)
			{
				pShader->BindTexture(SAMPLER_SHADOWDEPTH, pFlashlightDepthTexture, 0);
				pShaderAPI->BindStandardTexture(SAMPLER_RANDOMROTATION, TEXTURE_SHADOW_NOISE_2D);
			}
		}

		// Getting fog info
		MaterialFogMode_t fogType = pShaderAPI->GetSceneFogMode();
		int fogIndex = (fogType == MATERIAL_FOG_LINEAR_BELOW_FOG_Z) ? 1 : 0;

		// Getting skinning info
		int numBones = pShaderAPI->GetCurrentNumBones();

		// Some debugging stuff
		bool bWriteDepthToAlpha = false;
		bool bWriteWaterFogToAlpha = false;
		if (bFullyOpaque)
		{
			bWriteDepthToAlpha = pShaderAPI->ShouldWriteDepthToDestAlpha();
			bWriteWaterFogToAlpha = (fogType == MATERIAL_FOG_LINEAR_BELOW_FOG_Z);
			AssertMsg(!(bWriteDepthToAlpha && bWriteWaterFogToAlpha),
				"Can't write two values to alpha at the same time.");
		}

		float vEyePos_SpecExponent[4];
		pShaderAPI->GetWorldSpaceCameraPosition(vEyePos_SpecExponent);

		// Determining the max level of detail for the envmap
		int iEnvMapLOD = 6;
		auto envTexture = params[info.m_nEnvmap]->GetTextureValue();
		if (envTexture)
		{
			// Get power of 2 of texture width
			int width = envTexture->GetMappingWidth();
			int mips = 0;
			while (width >>= 1)
				++mips;

			// Cubemap has 4 sides so 2 mips less
			iEnvMapLOD = mips;
		}

		// Dealing with very high and low resolution cubemaps
		if (iEnvMapLOD > 12)
			iEnvMapLOD = 12;
		if (iEnvMapLOD < 4)
			iEnvMapLOD = 4;

		// This has some spare space
		vEyePos_SpecExponent[3] = iEnvMapLOD;
		pShaderAPI->SetPixelShaderConstant(PSREG_EYEPOS_SPEC_EXPONENT, vEyePos_SpecExponent, 1);


		// Setting lightmap texture
		pShaderAPI->BindStandardTexture(SAMPLER_LIGHTMAP, TEXTURE_LIGHTMAP_BUMPED);

		int nFixedLightingMode = pShaderAPI->GetIntRenderingParameter(INT_RENDERPARM_ENABLE_FIXED_LIGHTING);
		bool bVertexShaderFastPath = pContextData->m_bVertexShaderFastPath;

		if (nFixedLightingMode != 0)
		{
			if (pContextData->m_bPixelShaderForceFastPathBecauseOutline)
				nFixedLightingMode = 0;
			else
				bVertexShaderFastPath = false;
		}

		if (!g_pHardwareConfig->SupportsShaderModel_3_0() || mat_pbr_force_20b.GetBool())
		{
			// Setting up dynamic vertex shader
			DECLARE_DYNAMIC_VERTEX_SHADER(pbr_vs20b);
			SET_DYNAMIC_VERTEX_SHADER_COMBO(DOWATERFOG, fogIndex);
			SET_DYNAMIC_VERTEX_SHADER_COMBO(SKINNING, numBones > 0);
			SET_DYNAMIC_VERTEX_SHADER_COMBO(LIGHTING_PREVIEW, pShaderAPI->GetIntRenderingParameter(INT_RENDERPARM_ENABLE_FIXED_LIGHTING) != 0);
			SET_DYNAMIC_VERTEX_SHADER_COMBO(COMPRESSED_VERTS, (int)vertexCompression);
			SET_DYNAMIC_VERTEX_SHADER_COMBO(NUM_LIGHTS, lightState.m_nNumLights);
			SET_DYNAMIC_VERTEX_SHADER_COMBO(FASTPATH, bVertexShaderFastPath);
			SET_DYNAMIC_VERTEX_SHADER(pbr_vs20b);

			// Setting up dynamic pixel shader
			DECLARE_DYNAMIC_PIXEL_SHADER(pbr_ps20b);
			SET_DYNAMIC_PIXEL_SHADER_COMBO(NUM_LIGHTS, lightState.m_nNumLights);
			SET_DYNAMIC_PIXEL_SHADER_COMBO(WRITEWATERFOGTODESTALPHA, bWriteWaterFogToAlpha);
			SET_DYNAMIC_PIXEL_SHADER_COMBO(WRITE_DEPTH_TO_DESTALPHA, bWriteDepthToAlpha);
			SET_DYNAMIC_PIXEL_SHADER_COMBO(PIXELFOGTYPE, pShaderAPI->GetPixelFogCombo());
			SET_DYNAMIC_PIXEL_SHADER_COMBO(FLASHLIGHTSHADOWS, bFlashlightShadows);
			SET_DYNAMIC_PIXEL_SHADER(pbr_ps20b);
		}
		else
		{
			// Setting up dynamic vertex shader
			DECLARE_DYNAMIC_VERTEX_SHADER(pbr_vs30);
			SET_DYNAMIC_VERTEX_SHADER_COMBO(DOWATERFOG, fogIndex);
			SET_DYNAMIC_VERTEX_SHADER_COMBO(SKINNING, numBones > 0);
			SET_DYNAMIC_VERTEX_SHADER_COMBO(LIGHTING_PREVIEW, pShaderAPI->GetIntRenderingParameter(INT_RENDERPARM_ENABLE_FIXED_LIGHTING) != 0);
			SET_DYNAMIC_VERTEX_SHADER_COMBO(COMPRESSED_VERTS, (int)vertexCompression);
			SET_DYNAMIC_VERTEX_SHADER_COMBO(NUM_LIGHTS, lightState.m_nNumLights);
			SET_DYNAMIC_VERTEX_SHADER_COMBO(FASTPATH, bVertexShaderFastPath);
			SET_DYNAMIC_VERTEX_SHADER(pbr_vs30);

			// Setting up dynamic pixel shader
			DECLARE_DYNAMIC_PIXEL_SHADER(pbr_ps30);
			SET_DYNAMIC_PIXEL_SHADER_COMBO(NUM_LIGHTS, lightState.m_nNumLights);
			SET_DYNAMIC_PIXEL_SHADER_COMBO(WRITEWATERFOGTODESTALPHA, bWriteWaterFogToAlpha);
			SET_DYNAMIC_PIXEL_SHADER_COMBO(WRITE_DEPTH_TO_DESTALPHA, bWriteDepthToAlpha);
			SET_DYNAMIC_PIXEL_SHADER_COMBO(PIXELFOGTYPE, pShaderAPI->GetPixelFogCombo());
			SET_DYNAMIC_PIXEL_SHADER_COMBO(FLASHLIGHTSHADOWS, bFlashlightShadows);
			SET_DYNAMIC_PIXEL_SHADER(pbr_ps30);
		}

		// Setting up base texture transform
		pShader->SetVertexShaderTextureTransform(VERTEX_SHADER_SHADER_SPECIFIC_CONST_0, info.m_nBaseTextureTransform);

		// This is probably important
		pShader->SetModulationPixelShaderDynamicState_LinearColorSpace(1);

		// Send ambient cube to the pixel shader, force to black if not available
		pShaderAPI->SetPixelShaderStateAmbientLightCube(PSREG_AMBIENT_CUBE, !lightState.m_bAmbientLight);

		// Send lighting array to the pixel shader
		pShaderAPI->CommitPixelShaderLighting(PSREG_LIGHT_INFO_ARRAY);

		// Handle mat_fullbright 2 (diffuse lighting only)
		if (bLightingOnly)
		{
			pShaderAPI->BindStandardTexture(SAMPLER_BASETEXTURE, TEXTURE_GREY); // Basecolor
		}

		// Handle mat_specular 0 (no envmap reflections)
		if (!mat_specular.GetBool())
		{
			pShaderAPI->BindStandardTexture(SAMPLER_ENVMAP, TEXTURE_BLACK); // Envmap
		}

		// Sending fog info to the pixel shader
		pShaderAPI->SetPixelShaderFogParams(PSREG_FOG_PARAMS);

		// Set up shader modulation color
		float modulationColor[4] = { 1.0, 1.0, 1.0, 1.0 };
		pShader->ComputeModulationColor(modulationColor);
		float flLScale = pShaderAPI->GetLightMapScaleFactor();
		modulationColor[0] *= flLScale;
		modulationColor[1] *= flLScale;
		modulationColor[2] *= flLScale;
		pShaderAPI->SetPixelShaderConstant(PSREG_DIFFUSE_MODULATION, modulationColor);

		// More flashlight related stuff
		if (bHasFlashlight)
		{
			VMatrix worldToTexture;
			float atten[4], pos[4], tweaks[4];

			const FlashlightState_t& flashlightState = pShaderAPI->GetFlashlightState(worldToTexture);
			SetFlashLightColorFromState(flashlightState, pShaderAPI, PSREG_FLASHLIGHT_COLOR);

			pShader->BindTexture(SAMPLER_FLASHLIGHT, flashlightState.m_pSpotlightTexture, flashlightState.m_nSpotlightTextureFrame);

			// Set the flashlight attenuation factors
			atten[0] = flashlightState.m_fConstantAtten;
			atten[1] = flashlightState.m_fLinearAtten;
			atten[2] = flashlightState.m_fQuadraticAtten;
			atten[3] = flashlightState.m_FarZ;
			pShaderAPI->SetPixelShaderConstant(PSREG_FLASHLIGHT_ATTENUATION, atten, 1);

			// Set the flashlight origin
			pos[0] = flashlightState.m_vecLightOrigin[0];
			pos[1] = flashlightState.m_vecLightOrigin[1];
			pos[2] = flashlightState.m_vecLightOrigin[2];
			pShaderAPI->SetPixelShaderConstant(PSREG_FLASHLIGHT_POSITION_RIM_BOOST, pos, 1);

			pShaderAPI->SetPixelShaderConstant(PSREG_FLASHLIGHT_TO_WORLD_TEXTURE, worldToTexture.Base(), 4);

			// Tweaks associated with a given flashlight
			tweaks[0] = ShadowFilterFromState(flashlightState);
			tweaks[1] = ShadowAttenFromState(flashlightState);
			pShader->HashShadow2DJitter(flashlightState.m_flShadowJitterSeed, &tweaks[2], &tweaks[3]);
			pShaderAPI->SetPixelShaderConstant(PSREG_ENVMAP_TINT__SHADOW_TWEAKS, tweaks, 1);
		}

		float flParams[4] = { 0.0f, 0.0f, 0.0f, 0.0f };
		// Parallax Depth (the strength of the effect)
		flParams[0] = GetFloatParam(info.parallaxDepth, params, 3.0f);
		// Parallax Center (the height at which it's not moved)
		flParams[1] = GetFloatParam(info.parallaxCenter, params, 3.0f);
		pShaderAPI->SetPixelShaderConstant(27, flParams, 1);

	}

	// Actually draw the shader
	pShader->Draw();
}
