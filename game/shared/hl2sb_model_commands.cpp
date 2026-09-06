// hl2sb_model_commands.cpp
// Console commands for HL2SB player model system

#include "cbase.h"
#include "hl2sb_model_config.h"
#include "hl2sb_model_scan.h"

// memdbgon must be the last include file in a .cpp file!!!
#include "tier0/memdbgon.h"

#if defined( CLIENT_DLL )
extern IVEngineClient *engine;
#endif

//-----------------------------------------------------------------------------
// Purpose: Console command to load a model config
// Usage: hl2sb_setmodel <configname>
// Example: hl2sb_setmodel miku
//-----------------------------------------------------------------------------
void CC_HL2SB_SetModel( const CCommand &args )
{
	// Make sure the config table is populated before reading it
	HL2SB_EnsureModelConfigsLoaded();

	if ( args.ArgC() < 2 )
	{
		Msg( "Usage: hl2sb_setmodel <configname>\n" );
		Msg( "Available configs:\n" );

		for ( int i = 0; i < g_nHL2SB_ModelConfigCount; i++ )
		{
			Msg( "  %s - %s\n", g_HL2SB_ModelConfigs[i].szName, g_HL2SB_ModelConfigs[i].szPlayerModel );
		}
		return;
	}

	const char *pszConfigName = args[1];

	// Try to load the config if not already loaded
	const HL2SB_ModelConfig_t *pConfig = HL2SB_GetModelConfigByName( pszConfigName );
	if ( !pConfig )
	{
		if ( HL2SB_LoadModelConfig( pszConfigName ) )
		{
			pConfig = HL2SB_GetModelConfigByName( pszConfigName );
		}
	}

	if ( !pConfig )
	{
		Warning( "[HL2SB] Model config '%s' not found\n", pszConfigName );
		Msg( "Available configs:\n" );
		for ( int i = 0; i < g_nHL2SB_ModelConfigCount; i++ )
		{
			Msg( "  %s\n", g_HL2SB_ModelConfigs[i].szName );
		}
		return;
	}

#if defined( CLIENT_DLL )
	// Client: send to server via console command
	char szCmd[256];
	Q_snprintf( szCmd, sizeof(szCmd), "cl_playermodel %s\n", pConfig->szPlayerModel );
	engine->ClientCmd( szCmd );
#else
	// Server: apply directly
	Msg( "[HL2SB] Server-side model change not implemented yet\n" );
#endif

	Msg( "[HL2SB] Applied model: %s -> %s\n", pConfig->szName, pConfig->szPlayerModel );
}

//-----------------------------------------------------------------------------
// Purpose: Console command to list available model configs
//-----------------------------------------------------------------------------
void CC_HL2SB_ListModels( const CCommand &args )
{
	// Make sure the config table is populated before reading it
	HL2SB_PrintModelList();
}

//-----------------------------------------------------------------------------
// Purpose: Console command to reload all model configs
//-----------------------------------------------------------------------------
void CC_HL2SB_ReloadModels( const CCommand &args )
{
	Msg( "[HL2SB] Reloading model configs...\n" );
	HL2SB_LoadAllModelConfigs();
	Msg( "[HL2SB] Reloaded %d model configs\n", g_nHL2SB_ModelConfigCount );
}

// Register console commands.
// This file is compiled into BOTH client.dll and server.dll (see client_hl2mp.vpc
// and server_hl2mp.vpc).  If these commands were registered unconditionally, each
// DLL would register them once and the console would show the command / autocomplete
// hint twice.  These commands are client-side only (they resolve a config and fire
// `cl_playermodel` at the server), so register them only in the client DLL.
#if defined( CLIENT_DLL )
static ConCommand hl2sb_setmodel( "hl2sb_setmodel", CC_HL2SB_SetModel, 
	"Load and apply a player model config\n"
	"Usage: hl2sb_setmodel <configname>\n"
	"Example: hl2sb_setmodel miku" );

static ConCommand hl2sb_listmodels( "hl2sb_listmodels", CC_HL2SB_ListModels, 
	"List all available player model configs" );

static ConCommand hl2sb_reloadmodels( "hl2sb_reloadmodels", CC_HL2SB_ReloadModels, 
	"Reload all player model configs from cfg/playermodel/" );
#endif // CLIENT_DLL
