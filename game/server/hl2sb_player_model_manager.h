// hl2sb_player_model_manager.h
// Server-side player model manager for HL2SB

#ifndef HL2SB_PLAYER_MODEL_MANAGER_H
#define HL2SB_PLAYER_MODEL_MANAGER_H

#ifdef _WIN32
#pragma once
#endif

#include "hl2sb_model_scan.h"

// GMod-style: player model changes apply only after respawn (see .cpp).
extern ConVar hl2sb_model_respawn_only;

// Forward declaration
class CBasePlayer;

// Initialize model manager (call from gamerules Init)
void HL2SB_ModelManager_Init( void );

// Handle player spawn (apply model on spawn)
void HL2SB_ModelManager_PlayerSpawn( CBasePlayer *pPlayer );

// Handle client settings change (apply model when cl_playermodel changes)
void HL2SB_ModelManager_ClientSettingsChanged( CBasePlayer *pPlayer );

// Apply cl_playerbodygroups / cl_playerskin / cl_playercolor / cl_weaponcolor to a player.
//
// HL2SB: MUST be called after the model was set - SetModel() resets the bodygroups to the
// model's own defaults - and it is what CHL2MP_Player::SetPlayerModel() now calls, because
// the two entry points above are not called from anywhere in the tree and the bodygroup
// sliders of the player model selector therefore never reached the world model (2026-09-17).
void HL2SB_ModelManager_ApplyAppearance( CBasePlayer *pPlayer );

// Get the default model for a team
const char *HL2SB_GetDefaultModelForTeam( int iTeam );

#endif // HL2SB_PLAYER_MODEL_MANAGER_H
