//========= Copyright Valve Corporation, All rights reserved. ============//
//
// Purpose: Lua-bindable 3D model preview panel (HL2SB).
//
//=============================================================================//

#include "cbase.h"
#include <vgui_int.h>
#include <luamanager.h>
#include <vgui_controls/lPanel.h>
#include "lModelPanel.h"

// memdbgon must be the last include file in a .cpp file!!!
#include <tier0/memdbgon.h>

using namespace vgui;

//-----------------------------------------------------------------------------
// Purpose: Constructor
//-----------------------------------------------------------------------------
LModelPanel::LModelPanel( Panel *parent, const char *panelName, lua_State *L )
	: BaseClass( parent, panelName )
{
	m_lua_State = L;
	m_nTableReference = LUA_NOREF;
	m_nRefCount = 0;

	m_flYaw			= 180.0f;		// models face away from the camera by default
	m_flZoom		= 1.0f;
	m_flZoomMin		= 0.25f;
	m_flZoomMax		= 4.0f;
	m_flBaseDist	= 150.0f;
	m_vecBaseOffset.Init();

	// CModelPanel draws the model itself; the Lua side only draws its chrome.
	SetPaintBackgroundEnabled( false );
}

//-----------------------------------------------------------------------------
// Purpose: Destructor
//-----------------------------------------------------------------------------
LModelPanel::~LModelPanel()
{
#if defined( LUA_SDK )
	lua_unref( m_lua_State, m_nTableReference );
#endif
}

//-----------------------------------------------------------------------------
// Purpose: Create m_pModelInfo on first use.
//
// CModelPanel::ParseModelInfo() - the only thing that allocates m_pModelInfo -
// runs from ApplySettings() and only when the resource block contains a
// "model" sub-key.  A panel created in code therefore has no model info, and
// SwapModel() silently does nothing.  Feed it a synthetic block once.
//-----------------------------------------------------------------------------
void LModelPanel::EnsureModelInfo()
{
	if ( m_pModelInfo )
		return;

	KeyValues *pKV = new KeyValues( "ModelPanel" );
	pKV->SetInt( "fov", m_nFOV );

	KeyValues *pModel = pKV->FindKey( "model", true );
	pModel->SetString( "modelname", "models/player/group01/male_01.mdl" );
	pModel->SetInt( "skin", 0 );
	pModel->SetString( "angles_x", "0" );
	pModel->SetString( "angles_y", "180" );
	pModel->SetString( "angles_z", "0" );
	pModel->SetString( "origin_x", "150" );
	pModel->SetString( "origin_y", "0" );
	pModel->SetString( "origin_z", "0" );
	pModel->SetInt( "spotlight", 1 );

	ApplySettings( pKV );
	pKV->deleteThis();

	if ( m_pModelInfo )
	{
		m_pModelInfo->m_bUseSpotlight = true;
		m_pModelInfo->m_nSkin = 0;
	}
}

//-----------------------------------------------------------------------------
// Purpose: Frame the whole model, with padding, centred in the viewport.
//
// Bounds source order matters: ported/repacked models frequently ship a bogus
// static render bound, which puts the camera inside the mesh.  The studio hull
// is what the engine itself trusts, so it wins; the static render bounds are a
// last resort only.
//-----------------------------------------------------------------------------
void LModelPanel::FitCameraToModel()
{
	if ( !m_pModelInfo || !m_hModel.Get() )
		return;

	Vector vecMin( 0, 0, 0 ), vecMax( 0, 0, 0 );

	CStudioHdr *pHdr = m_hModel->GetModelPtr();
	if ( pHdr )
	{
		vecMin = pHdr->hull_min();
		vecMax = pHdr->hull_max();

		Vector vecViewMin = pHdr->view_bbmin();
		Vector vecViewMax = pHdr->view_bbmax();
		if ( !VectorCompare( vec3_origin, vecViewMin ) || !VectorCompare( vec3_origin, vecViewMax ) )
		{
			VectorMin( vecViewMin, vecMin, vecMin );
			VectorMax( vecViewMax, vecMax, vecMax );
		}
	}

	Vector vecSize = vecMax - vecMin;
	float flMaxDim = MAX( vecSize.x, MAX( vecSize.y, vecSize.z ) );

	if ( flMaxDim < 1.0f )
	{
		const model_t *pModel = modelinfo->GetModel( m_hModel->GetModelIndex() );
		if ( pModel )
		{
			modelinfo->GetModelRenderBounds( pModel, vecMin, vecMax );
			vecSize = vecMax - vecMin;
			flMaxDim = MAX( vecSize.x, MAX( vecSize.y, vecSize.z ) );
		}
	}

	if ( flMaxDim < 1.0f )
	{
		flMaxDim = 72.0f;			// standard HL2 humanoid height
		vecMin.Init( -16, -16, 0 );
		vecMax.Init( 16, 16, 72 );
	}

	Vector vecCenter = ( vecMax + vecMin ) * 0.5f;

	float flHalfFOV = DEG2RAD( m_nFOV * 0.5f );
	float flDist = ( flMaxDim * 0.5f ) / tan( flHalfFOV );
	if ( flDist < 30.0f )
		flDist = 30.0f;

	m_flBaseDist = flDist;
	m_vecBaseOffset.y = -vecCenter.y;
	m_vecBaseOffset.z = -vecCenter.z;

	ApplyCamera();

	Msg( "[HL2SB] ModelPanel fit: dim=%.1f dist=%.1f center=(%.1f %.1f %.1f)\n",
		flMaxDim, flDist, vecCenter.x, vecCenter.y, vecCenter.z );
}

//-----------------------------------------------------------------------------
// Purpose: Push yaw + zoom into the model info the renderer reads.
//-----------------------------------------------------------------------------
void LModelPanel::ApplyCamera()
{
	if ( !m_pModelInfo )
		return;

	m_pModelInfo->m_vecAbsAngles.Init( 0, m_flYaw, 0 );
	m_pModelInfo->m_vecOriginOffset.x = m_flBaseDist * m_flZoom;
	m_pModelInfo->m_vecOriginOffset.y = m_vecBaseOffset.y;
	m_pModelInfo->m_vecOriginOffset.z = m_vecBaseOffset.z;
	m_pModelInfo->m_vecViewportOffset.Init();

	SetPanelDirty();
}

//-----------------------------------------------------------------------------
// Purpose: Load a model.  Returns false when the client has not precached it.
//
// IVEngineClient::LoadModel() is deliberately NOT called: it is documented as
// a model-*hooking* entry point and disturbing the caches here made world
// models come back as the purple ERROR material.  The server precaches every
// cfg/playermodel entry, so anything reachable through the menu is already
// known to the client.
//-----------------------------------------------------------------------------
bool LModelPanel::LoadModel( const char *pszModel )
{
	if ( !pszModel || !pszModel[0] )
		return false;

	// HL2SB: do NOT reject models the client has not precached.  The player model
	// configs load GMod/workshop models from custom/* that are not all present in
	// the server's precache transfer on a solo listen server, so modelinfo->
	// GetModelIndex() returns -1 for many of them and the thumbnail grid showed
	// mostly empty cells.  CModelPanel::SwapModel() -> InitializeAsClientEntity()
	// loads a client-side model by path anyway, so let it try rather than bailing.

	EnsureModelInfo();

	SwapModel( pszModel );
	SetPanelDirty();

	FitCameraToModel();

	return true;
}

const char *LModelPanel::GetModelPath() const
{
	return ( m_pModelInfo && m_pModelInfo->m_pszModelName ) ? m_pModelInfo->m_pszModelName : "";
}

void LModelPanel::SetYaw( float flYaw )
{
	m_flYaw = flYaw;
	ApplyCamera();
}

void LModelPanel::SetZoom( float flZoom )
{
	m_flZoom = clamp( flZoom, m_flZoomMin, m_flZoomMax );
	ApplyCamera();
}

void LModelPanel::SetZoomLimits( float flMin, float flMax )
{
	m_flZoomMin = ( flMin > 0.0f ) ? flMin : 0.01f;
	m_flZoomMax = ( flMax > m_flZoomMin ) ? flMax : m_flZoomMin + 0.01f;
	SetZoom( m_flZoom );
}

//-----------------------------------------------------------------------------
// Purpose: Change the FOV and re-fit, so the model stays framed.
//-----------------------------------------------------------------------------
void LModelPanel::SetFOV( int nFOV )
{
	BaseClass::SetFOV( nFOV );
	FitCameraToModel();
}

void LModelPanel::RefitCamera()
{
	// HL2SB: LoadModel() prepares both things the fit needs - it calls EnsureModelInfo()
	// and SwapModel() before FitCameraToModel() - but the RefitCamera() binding skipped
	// them, and FitCameraToModel() returns SILENTLY when either is missing:
	//
	//     if ( !m_pModelInfo || !m_hModel.Get() ) return;
	//
	// That is why the spawn-icon thumbnails kept the default camera and framed the head
	// only, and why not one "ModelPanel fit" line ever appeared in the log with
	// RefitCamera() being called after every SetModel (2026-09-17).  Prepare them here so
	// the binding can stand on its own.
	EnsureModelInfo();

	if ( !m_hModel.Get() && m_pModelInfo != NULL && m_pModelInfo->m_pszModelName[0] )
	{
		SwapModel( m_pModelInfo->m_pszModelName );
	}

	FitCameraToModel();
}

//-----------------------------------------------------------------------------
// Purpose: Play a sequence by name.  Returns false when the model has none.
//-----------------------------------------------------------------------------
bool LModelPanel::PlaySequence( const char *pszSequence )
{
	if ( !m_hModel.Get() || !pszSequence || !pszSequence[0] )
		return false;

	int nSequence = m_hModel->LookupSequence( pszSequence );
	if ( nSequence < 0 )
		return false;

	m_hModel->ResetSequence( nSequence );
	m_hModel->SetCycle( 0.0f );
	m_hModel->SetPlaybackRate( 1.0f );

	return true;
}

int LModelPanel::GetSequenceCount()
{
	if ( !m_hModel.Get() )
		return 0;

	CStudioHdr *pHdr = m_hModel->GetModelPtr();
	return pHdr ? pHdr->GetNumSeq() : 0;
}

const char *LModelPanel::GetSequenceName( int nIndex )
{
	if ( !m_hModel.Get() )
		return NULL;

	const char *pszName = m_hModel->GetSequenceName( nIndex );
	return ( pszName && pszName[0] ) ? pszName : NULL;
}

//-----------------------------------------------------------------------------
// Purpose: CModelPanel::Paint() binds the default cubemap and then restores it
// to NULL rather than to whatever the world pass had bound, leaves the colour
// modulation / blend set, and when "spotlight" is enabled hands
// g_pStudioRender the address of a *stack local* LightDesc_t.  In the main menu
// that goes unnoticed; in-game it leaks into the world render and turns every
// model and brush into the purple ERROR material / NaN dither noise.  Save and
// restore everything, and clear the studio local lights so the dangling
// pointer is never read.
//-----------------------------------------------------------------------------
void LModelPanel::Paint()
{
	CMatRenderContextPtr pRenderContext( materials );

	ITexture *pPrevCubemap = pRenderContext->GetLocalCubemap();

	float flPrevColor[3] = { 1.0f, 1.0f, 1.0f };
	render->GetColorModulation( flPrevColor );
	float flPrevBlend = render->GetBlend();

	BaseClass::Paint();

	pRenderContext->BindLocalCubemap( pPrevCubemap );
	render->SetColorModulation( flPrevColor );
	render->SetBlend( flPrevBlend );

	g_pStudioRender->SetLocalLights( 0, NULL );

#ifdef LUA_SDK
	// HL2SB: GMod passes ( w, h ) to the scripted Paint ( see lPanel.cpp ).
	BEGIN_LUA_CALL_PANEL_METHOD( "Paint" );
		lua_pushinteger( m_lua_State, GetWide() );
		lua_pushinteger( m_lua_State, GetTall() );
	END_LUA_CALL_PANEL_METHOD( 2, 0 );
#endif
}

void LModelPanel::PerformLayout()
{
	BaseClass::PerformLayout();

#ifdef LUA_SDK
	// GMod's Panel:PerformLayout( w, h ), same as LPanel.
	BEGIN_LUA_CALL_PANEL_METHOD( "PerformLayout" );
		lua_pushinteger( m_lua_State, GetWide() );
		lua_pushinteger( m_lua_State, GetTall() );
	END_LUA_CALL_PANEL_METHOD( 2, 0 );
#endif
}

void LModelPanel::ApplySchemeSettings( IScheme *pScheme )
{
	BaseClass::ApplySchemeSettings( pScheme );

#ifdef LUA_SDK
	BEGIN_LUA_CALL_PANEL_METHOD( "ApplySchemeSettings" );
	END_LUA_CALL_PANEL_METHOD( 0, 0 );
#endif
}

void LModelPanel::OnMousePressed( MouseCode code )
{
#ifdef LUA_SDK
	BEGIN_LUA_CALL_PANEL_METHOD( "OnMousePressed" );
		lua_pushinteger( m_lua_State, (int)code );
	END_LUA_CALL_PANEL_METHOD( 1, 1 );

	RETURN_LUA_PANEL_NONE();
#endif

	BaseClass::OnMousePressed( code );
}

void LModelPanel::OnMouseReleased( MouseCode code )
{
#ifdef LUA_SDK
	BEGIN_LUA_CALL_PANEL_METHOD( "OnMouseReleased" );
		lua_pushinteger( m_lua_State, (int)code );
	END_LUA_CALL_PANEL_METHOD( 1, 1 );

	RETURN_LUA_PANEL_NONE();
#endif

	BaseClass::OnMouseReleased( code );
}

void LModelPanel::OnCursorMoved( int x, int y )
{
#ifdef LUA_SDK
	BEGIN_LUA_CALL_PANEL_METHOD( "OnCursorMoved" );
		lua_pushinteger( m_lua_State, x );
		lua_pushinteger( m_lua_State, y );
	END_LUA_CALL_PANEL_METHOD( 2, 1 );

	RETURN_LUA_PANEL_NONE();
#endif

	BaseClass::OnCursorMoved( x, y );
}

void LModelPanel::OnMouseWheeled( int delta )
{
#ifdef LUA_SDK
	BEGIN_LUA_CALL_PANEL_METHOD( "OnMouseWheeled" );
		lua_pushinteger( m_lua_State, delta );
	END_LUA_CALL_PANEL_METHOD( 1, 1 );

	RETURN_LUA_PANEL_NONE();
#endif

	BaseClass::OnMouseWheeled( delta );
}

void LModelPanel::OnThink()
{
#ifdef LUA_SDK
	BEGIN_LUA_CALL_PANEL_METHOD( "OnThink" );
	END_LUA_CALL_PANEL_METHOD( 0, 0 );
#endif

	BaseClass::OnThink();
}

//=============================================================================
// Lua bindings
//
// Same shape as the Frame binding: the panel gets its own metatable, and
// __index falls back through EditablePanel then Panel so every inherited
// vgui method (SetPos, SetVisible, ...) still resolves.
//=============================================================================

LUA_API lua_ModelPanel *lua_tomodelpanel( lua_State *L, int idx )
{
	PHandle *phPanel = dynamic_cast< PHandle * >( (PHandle *)lua_touserdata( L, idx ) );
	if ( phPanel == NULL )
		return NULL;

	return dynamic_cast< lua_ModelPanel * >( phPanel->Get() );
}

LUA_API void lua_pushmodelpanel( lua_State *L, lua_ModelPanel *pPanel )
{
	LModelPanel *plPanel = dynamic_cast< LModelPanel * >( pPanel );
	if ( plPanel )
		++plPanel->m_nRefCount;

	PHandle *phPanel = (PHandle *)lua_newuserdata( L, sizeof( PHandle ) );
	phPanel->Set( pPanel );

	luaL_getmetatable( L, "ModelPanel" );
	lua_setmetatable( L, -2 );
}

LUALIB_API lua_ModelPanel *luaL_checkmodelpanel( lua_State *L, int narg )
{
	lua_ModelPanel *d = lua_tomodelpanel( L, narg );
	if ( d == NULL )
		luaL_argerror( L, narg, "ModelPanel expected, got INVALID_PANEL" );

	return d;
}

static int ModelPanel_SetModel( lua_State *L )
{
	lua_pushboolean( L, luaL_checkmodelpanel( L, 1 )->LoadModel( luaL_checkstring( L, 2 ) ) );
	return 1;
}

static int ModelPanel_GetModel( lua_State *L )
{
	lua_pushstring( L, luaL_checkmodelpanel( L, 1 )->GetModelPath() );
	return 1;
}

static int ModelPanel_SetYaw( lua_State *L )
{
	luaL_checkmodelpanel( L, 1 )->SetYaw( (float)luaL_checknumber( L, 2 ) );
	return 0;
}

static int ModelPanel_GetYaw( lua_State *L )
{
	lua_pushnumber( L, luaL_checkmodelpanel( L, 1 )->GetYaw() );
	return 1;
}

static int ModelPanel_SetZoom( lua_State *L )
{
	luaL_checkmodelpanel( L, 1 )->SetZoom( (float)luaL_checknumber( L, 2 ) );
	return 0;
}

static int ModelPanel_GetZoom( lua_State *L )
{
	lua_pushnumber( L, luaL_checkmodelpanel( L, 1 )->GetZoom() );
	return 1;
}

static int ModelPanel_SetZoomLimits( lua_State *L )
{
	luaL_checkmodelpanel( L, 1 )->SetZoomLimits( (float)luaL_checknumber( L, 2 ),
												 (float)luaL_checknumber( L, 3 ) );
	return 0;
}

static int ModelPanel_SetFOV( lua_State *L )
{
	luaL_checkmodelpanel( L, 1 )->SetFOV( luaL_checkint( L, 2 ) );
	return 0;
}

static int ModelPanel_PlaySequence( lua_State *L )
{
	lua_pushboolean( L, luaL_checkmodelpanel( L, 1 )->PlaySequence( luaL_checkstring( L, 2 ) ) );
	return 1;
}

static int ModelPanel_GetSequenceCount( lua_State *L )
{
	lua_pushinteger( L, luaL_checkmodelpanel( L, 1 )->GetSequenceCount() );
	return 1;
}

static int ModelPanel_GetSequenceName( lua_State *L )
{
	const char *pszName = luaL_checkmodelpanel( L, 1 )->GetSequenceName( luaL_checkint( L, 2 ) );

	if ( pszName )
		lua_pushstring( L, pszName );
	else
		lua_pushnil( L );

	return 1;
}

static int ModelPanel_RefitCamera( lua_State *L )
{
	luaL_checkmodelpanel( L, 1 )->RefitCamera();
	return 0;
}

static int ModelPanel___index( lua_State *L )
{
	lua_ModelPanel *pPanel = lua_tomodelpanel( L, 1 );
	if ( pPanel == NULL )
	{
		lua_Debug ar1;
		lua_getstack( L, 1, &ar1 );
		lua_getinfo( L, "fl", &ar1 );
		lua_Debug ar2;
		lua_getinfo( L, ">S", &ar2 );
		/* HL2SB: indexing a deleted panel yields nil, the way GMod's engine behaves
        (see lua/includes/util.lua:314-322, GMod's IsValid). */
        lua_pushnil( L );
        return 1;
	}

	LModelPanel *plPanel = dynamic_cast< LModelPanel * >( pPanel );

	if ( plPanel && plPanel->m_nTableReference != LUA_NOREF )
	{
		lua_getref( L, plPanel->m_nTableReference );
		lua_pushvalue( L, 2 );
		lua_gettable( L, -2 );

		if ( lua_isnil( L, -1 ) )
		{
			lua_pop( L, 2 );
			lua_getmetatable( L, 1 );
			lua_pushvalue( L, 2 );
			lua_gettable( L, -2 );

			if ( lua_isnil( L, -1 ) )
			{
				lua_pop( L, 2 );
				luaL_getmetatable( L, "EditablePanel" );
				lua_pushvalue( L, 2 );
				lua_gettable( L, -2 );

				if ( lua_isnil( L, -1 ) )
				{
					lua_pop( L, 2 );
					luaL_getmetatable( L, "Panel" );
					lua_pushvalue( L, 2 );
					lua_gettable( L, -2 );
				}
			}
		}
	}
	else
	{
		lua_getmetatable( L, 1 );
		lua_pushvalue( L, 2 );
		lua_gettable( L, -2 );

		if ( lua_isnil( L, -1 ) )
		{
			lua_pop( L, 2 );
			luaL_getmetatable( L, "EditablePanel" );
			lua_pushvalue( L, 2 );
			lua_gettable( L, -2 );

			if ( lua_isnil( L, -1 ) )
			{
				lua_pop( L, 2 );
				luaL_getmetatable( L, "Panel" );
				lua_pushvalue( L, 2 );
				lua_gettable( L, -2 );
			}
		}
	}

	return 1;
}

static int ModelPanel___newindex( lua_State *L )
{
	lua_ModelPanel *pPanel = lua_tomodelpanel( L, 1 );
	if ( pPanel == NULL )
	{
		lua_Debug ar1;
		lua_getstack( L, 1, &ar1 );
		lua_getinfo( L, "fl", &ar1 );
		lua_Debug ar2;
		lua_getinfo( L, ">S", &ar2 );
		/* HL2SB: indexing a deleted panel yields nil, the way GMod's engine behaves
        (see lua/includes/util.lua:314-322, GMod's IsValid). */
        lua_pushnil( L );
        return 1;
	}

	LModelPanel *plPanel = dynamic_cast< LModelPanel * >( pPanel );
	if ( plPanel )
	{
		if ( plPanel->m_nTableReference == LUA_NOREF )
		{
			lua_newtable( L );
			plPanel->m_nTableReference = luaL_ref( L, LUA_REGISTRYINDEX );
		}

		lua_getref( L, plPanel->m_nTableReference );
		lua_pushvalue( L, 3 );
		lua_setfield( L, -2, luaL_checkstring( L, 2 ) );
		lua_pop( L, 1 );
		return 0;
	}

	lua_Debug ar1;
	lua_getstack( L, 1, &ar1 );
	lua_getinfo( L, "fl", &ar1 );
	lua_Debug ar2;
	lua_getinfo( L, ">S", &ar2 );
	lua_pushfstring( L, "%s:%d: attempt to index a non-scripted panel", ar2.short_src, ar1.currentline );
	return lua_error( L );
}

// HL2SB: a dying Lua handle must NOT destroy the C++ panel.
//
// This used to `delete` the panel as soon as the last Lua handle was collected.  But
// a panel that lives in the vgui tree is OWNED BY ITS PARENT (Panel::~Panel deletes
// its autodelete children), and the Lua GC runs during allocation - i.e. it can run
// in the middle of Panel::PaintTraverse's children loop.  Deleting a live, parented
// panel there leaves the traversal reading freed entries, and the crash is a jump to
// NULL (or, when the memory was reused by a string, into that string) inside
// vgui::Panel::PaintTraverse -- exactly what every 2026-09-17 minidump shows.  It is
// also why the Lua spawnmenu kept finding "dead handles" in its panel pools.
//
// GMod never does this either: its Lua handle is a weak PHandle and vgui owns the
// lifetime.  So: a parented panel is handed to the parent (SetAutoDelete, same as
// GMod's vgui.Create) and kept alive; only a panel nothing owns any more - no parent
// and no other handle - is queued for deletion.
static int ModelPanel___gc (lua_State *L) {
  LModelPanel *plPanel = dynamic_cast<LModelPanel *>(lua_tomodelpanel(L, 1));
  if (plPanel) {
    --plPanel->m_nRefCount;

    if (plPanel->GetVParent() != 0) {
      static int s_nParentedGc = 0;
      if (s_nParentedGc < 12) {
        ++s_nParentedGc;
        Msg("[HL2SB] panel gc: '%s' (%p) was still parented (handles left=%d) - kept alive\n",
          plPanel->GetName(), (void *)plPanel, plPanel->m_nRefCount);
      }

      plPanel->SetAutoDelete(true);
    }
    else if (plPanel->m_nRefCount <= 0) {
      plPanel->MarkForDeletion();
    }
  }
  return 0;
}

static int ModelPanel___eq( lua_State *L )
{
	lua_pushboolean( L, lua_tomodelpanel( L, 1 ) == lua_tomodelpanel( L, 2 ) );	return 1;
}

static int ModelPanel___tostring( lua_State *L )
{
	lua_ModelPanel *pPanel = lua_tomodelpanel( L, 1 );
	if ( pPanel == NULL )
	{
		lua_pushstring( L, "INVALID_PANEL" );
	}
	else
	{
		const char *pName = pPanel->GetName();
		if ( Q_strcmp( pName, "" ) == 0 )
			pName = "(no name)";

		lua_pushfstring( L, "ModelPanel: \"%s\"", pName );
	}

	return 1;
}

static int ModelPanel_GetRefTable( lua_State *L )
{
	// The inherited Panel_GetRefTable() does dynamic_cast<LPanel*>, which fails
	// for this class (it derives from CModelPanel, not LPanel) and returns nil.
	// vgui.register()'s factory calls GetRefTable() to merge the script table in,
	// so without this override any vgui.register(..., "ModelPanel") panel comes
	// out with no Lua table - the factory errors and the caller's build step
	// aborts halfway, leaving an empty window.
	LModelPanel *plPanel = dynamic_cast< LModelPanel * >( luaL_checkpanel( L, 1 ) );
	if ( plPanel )
	{
		if ( plPanel->m_nTableReference == LUA_NOREF )
		{
			lua_newtable( L );
			plPanel->m_nTableReference = luaL_ref( L, LUA_REGISTRYINDEX );
		}
		lua_getref( L, plPanel->m_nTableReference );
	}
	else
	{
		lua_pushnil( L );
	}

	return 1;
}

static const luaL_Reg ModelPanelmeta[] = {
	{"GetRefTable",			ModelPanel_GetRefTable},
	{"SetModel",			ModelPanel_SetModel},
	{"GetModel",			ModelPanel_GetModel},
	{"SetYaw",				ModelPanel_SetYaw},
	{"GetYaw",				ModelPanel_GetYaw},
	{"SetZoom",				ModelPanel_SetZoom},
	{"GetZoom",				ModelPanel_GetZoom},
	{"SetZoomLimits",		ModelPanel_SetZoomLimits},
	{"SetFOV",				ModelPanel_SetFOV},
	{"PlaySequence",		ModelPanel_PlaySequence},
	{"GetSequenceCount",	ModelPanel_GetSequenceCount},
	{"GetSequenceName",		ModelPanel_GetSequenceName},
	{"RefitCamera",			ModelPanel_RefitCamera},
	{"__index",				ModelPanel___index},
	{"__newindex",			ModelPanel___newindex},
	{"__gc",				ModelPanel___gc},
	{"__eq",				ModelPanel___eq},
	{"__tostring",			ModelPanel___tostring},
	{NULL, NULL}
};

static int luasrc_ModelPanel( lua_State *L )
{
	lua_ModelPanel *pPanel = new LModelPanel( luaL_optpanel( L, 1, VGui_GetClientLuaRootPanel() ),
										  luaL_optstring( L, 2, NULL ), L );
	lua_pushmodelpanel( L, pPanel );
	return 1;
}

static const luaL_Reg ModelPanel_funcs[] = {
	{"ModelPanel", luasrc_ModelPanel},
	{"ModelImage", luasrc_ModelPanel},
	{NULL, NULL}
};

/*
** Open ModelPanel object
*/
LUALIB_API int luaopen_vgui_ModelPanel( lua_State *L )
{
	// Register the PlayerColor material proxy factory (defined in
	// c_viewmodel_attachment.cpp) now that the client Lua env is up, so any vmt
	// "PlayerColor" proxy (player model body / sleeve tint) finds a handler
	// before those materials compile.
	extern void RegisterPlayerColorProxyFactory();
	RegisterPlayerColorProxyFactory();

	luaL_newmetatable( L, "ModelPanel" );
	luaL_register( L, NULL, ModelPanelmeta );
	lua_pushstring( L, "panel" );
	lua_setfield( L, -2, "__type" );		/* metatable.__type = "panel" */
	luaL_register( L, "vgui", ModelPanel_funcs );
	lua_pop( L, 2 );
	return 1;
}
