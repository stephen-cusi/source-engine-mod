// hands_model_mapping.h
// Maps player models to their corresponding hands/arms models
// Used by c_viewmodel_attachment system
// Now uses hl2sb_model_config system for dynamic configuration

#ifndef HANDS_MODEL_MAPPING_H
#define HANDS_MODEL_MAPPING_H

#ifdef _WIN32
#pragma once
#endif

#include "convar.h"

// Default hands model when no mapping is found
#define HANDS_MODEL_DEFAULT "models/arms/hands.mdl"

// ConVar for overriding hands model
extern ConVar cl_hands_model;

// Get the hands model path for a given player model
// Returns NULL if no hands should be shown
// Uses hl2sb_model_config system for dynamic configuration
inline const char *Hands_GetModelForPlayerModel( const char *pszPlayerModel )
{
	// If ConVar override is set, use it
	const char *pszOverride = cl_hands_model.GetString();
	if ( pszOverride && pszOverride[0] != '\0' && Q_strcmp( pszOverride, "auto" ) != 0 )
	{
		return pszOverride;
	}

	if ( !pszPlayerModel )
		return NULL;

	// Use the model config system to find hands
	// This will be called from hl2sb_model_config.cpp
	// For now, return NULL to use the config system
	return NULL;
}

#endif // HANDS_MODEL_MAPPING_H
