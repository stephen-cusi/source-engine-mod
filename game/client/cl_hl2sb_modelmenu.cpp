// cl_hl2sb_modelmenu.cpp
// Client-side player model status & menu commands for HL2SB.

#include "cbase.h"
#include "c_baseplayer.h"
#include "hl2sb_model_config.h"
#include "hl2sb_model_scan.h"
#include "hands_model_mapping.h"

// memdbgon must be the last include file in a .cpp file!!!
#include "tier0/memdbgon.h"

// Master switch for the hands system, defined in c_viewmodel_attachment.cpp
extern ConVar cl_hands;
// Hands-model override cvar, defined in c_viewmodel_attachment.cpp
extern ConVar cl_hands_model;

//-----------------------------------------------------------------------------
// Purpose: Print the current player model / c_hands status.
//-----------------------------------------------------------------------------
void CC_HL2SB_Status( const CCommand &args )
{
	C_BasePlayer *pPlayer = C_BasePlayer::GetLocalPlayer();

	Msg( "=== HL2SB Status ===\n" );

	// Player model: read from the local player entity (the requested cl_playermodel
	// cvar is file-static, so the authoritative value is the resolved model).
	if ( pPlayer && pPlayer->GetModel() )
	{
		const char *pszModel = modelinfo->GetModelName( pPlayer->GetModel() );
		Msg( "  Player Model: %s\n", pszModel ? pszModel : "none" );
	}
	else
	{
		Msg( "  Player Model: none (no local player)\n" );
	}

	Msg( "  cl_hands: %s\n", cl_hands.GetBool() ? "1" : "0" );

	const char *pszOverride = cl_hands_model.GetString();
	if ( pszOverride && pszOverride[0] )
	{
		Msg( "  cl_hands_model: %s\n", pszOverride );
	}
	else
	{
		Msg( "  cl_hands_model: auto\n" );
	}

	// Actual attached hands model
	const char *pszActive = HL2SB_GetActiveHandsModel();
	if ( pszActive && pszActive[0] )
	{
		Msg( "  Active Hands: %s\n", pszActive );
	}
	else
	{
		// No c_hands attached this session -> the viewmodel draws its own stock
		// arms (hl2 default) or hands are disabled.
		const char *pszWhy = cl_hands.GetBool() ? "hl2 default" : "none (cl_hands 0)";
		Msg( "  Active Hands: none / %s\n", pszWhy );
	}

	Msg( "\n" );
}

static ConCommand hl2sb_status( "hl2sb_status", CC_HL2SB_Status,
	"Show current player model and c_hands status" );

//-----------------------------------------------------------------------------
// Purpose: List all available player models.  Shared implementation lives in
//          HL2SB_PrintModelList() (also used by hl2sb_listmodels).
//-----------------------------------------------------------------------------
void HL2SB_ShowModelMenu( void )
{
	HL2SB_PrintModelList();
}

void HL2SB_HideModelMenu( void )
{
	// Nothing to hide in console mode
}

void CC_HL2SB_ModelMenu( const CCommand &args )
{
	HL2SB_ShowModelMenu();
}

static ConCommand hl2sb_modelmenu( "hl2sb_modelmenu", CC_HL2SB_ModelMenu,
	"List available player models" );
