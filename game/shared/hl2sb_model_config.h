// hl2sb_model_config.h
// HL2SB Player Model Configuration System
// Loads model configs from cfg/playermodel/*.cfg

#ifndef HL2SB_MODEL_CONFIG_H
#define HL2SB_MODEL_CONFIG_H

#ifdef _WIN32
#pragma once
#endif

#define HL2SB_MAX_MODEL_NAME 64
#define HL2SB_MAX_MODEL_PATH 128
#define HL2SB_MAX_MODELS 64

// Model config structure
struct HL2SB_ModelConfig_t
{
	char szName[HL2SB_MAX_MODEL_NAME];          // Display name (e.g., "Miku")
	char szPlayerModel[HL2SB_MAX_MODEL_PATH];   // Player model path
	char szHandsModel[HL2SB_MAX_MODEL_PATH];    // Hands model path (empty = no hands)
	char szConfigFile[MAX_PATH];                // Config file path
};

// Global model configs
extern HL2SB_ModelConfig_t g_HL2SB_ModelConfigs[HL2SB_MAX_MODELS];
extern int g_nHL2SB_ModelConfigCount;

// Load all model configs from cfg/playermodel/
void HL2SB_LoadAllModelConfigs( void );

// Lazy-load the model config table if it hasn't been populated yet.
// On the client the table is only populated by HL2SB_LoadAllModelConfigs()
// (the server does it during Precache), so call this before reading the table
// from any console command.  Safe to call more than once.
void HL2SB_EnsureModelConfigsLoaded( void );

// Load a specific model config by name
// Returns true if config was loaded successfully
bool HL2SB_LoadModelConfig( const char *pszConfigName );

// Get model config by index
const HL2SB_ModelConfig_t *HL2SB_GetModelConfig( int nIndex );

// Get model config by name
const HL2SB_ModelConfig_t *HL2SB_GetModelConfigByName( const char *pszName );

// Find model config by player model path
const HL2SB_ModelConfig_t *HL2SB_FindModelConfigByPath( const char *pszPlayerModelPath );

// Get hands model for a given player model
// Returns NULL if no hands should be shown
const char *HL2SB_GetHandsModelForPlayer( const char *pszPlayerModelPath );

#endif // HL2SB_MODEL_CONFIG_H
