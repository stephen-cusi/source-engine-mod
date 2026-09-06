// hl2sb_player_model_manager.h
// Server-side player model manager for HL2SB

#ifndef HL2SB_PLAYER_MODEL_MANAGER_H
#define HL2SB_PLAYER_MODEL_MANAGER_H

#ifdef _WIN32
#pragma once
#endif

#include "hl2sb_model_scan.h"

// Forward declaration
class CBasePlayer;

// Initialize model manager (call from gamerules Init)
void HL2SB_ModelManager_Init( void );

// Handle player spawn (apply model on spawn)
void HL2SB_ModelManager_PlayerSpawn( CBasePlayer *pPlayer );

// Handle client settings change (apply model when cl_playermodel changes)
void HL2SB_ModelManager_ClientSettingsChanged( CBasePlayer *pPlayer );

// Get the default model for a team
const char *HL2SB_GetDefaultModelForTeam( int iTeam );

#endif // HL2SB_PLAYER_MODEL_MANAGER_H
