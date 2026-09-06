// cl_hl2sb_modelmenu.cpp
// Client-side player model menu for HL2SB
// Simplified version - uses console commands

#include "cbase.h"
#include "hl2sb_model_config.h"
#include "hl2sb_model_scan.h"

// memdbgon must be the last include file in a .cpp file!!!
#include "tier0/memdbgon.h"

void HL2SB_ShowModelMenu( void )
{
	// List all available models
	Msg( "=== HL2SB Player Models ===\n" );
	Msg( "Use: hl2sb_setmodel <name>\n\n" );

	for ( int i = 0; i < g_nHL2SB_ModelConfigCount; i++ )
	{
		const HL2SB_ModelConfig_t &config = g_HL2SB_ModelConfigs[i];
		Msg( "  %s - %s\n", config.szName, config.szPlayerModel );
	}
	Msg( "\n" );
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
