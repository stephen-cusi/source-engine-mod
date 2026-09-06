// hl2sb_model_scan.h
// HL2SB Model validation (no auto-scan)

#ifndef HL2SB_MODEL_SCAN_H
#define HL2SB_MODEL_SCAN_H

#ifdef _WIN32
#pragma once
#endif

// Validate player model path (check if file exists)
bool HL2SB_IsValidPlayerModel( const char *pszModelPath );

// Apply player model with validation
bool HL2SB_ApplyPlayerModel( void *pPlayer, const char *pszRequestedModel, const char *pszFallbackModel );

#endif // HL2SB_MODEL_SCAN_H
