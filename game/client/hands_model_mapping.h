// hands_model_mapping.h
//
// HL2SB (2026-09-27): the legacy C_ViewmodelAttachment c_hands renderer was
// deleted (GMod's gmod_hands entity, drawn by GM:PostDrawViewModel, owns the
// first-person arms now).  What remains here are the per-player colour
// accessors the PlayerColor / PlayerWeaponColor material proxies need
// (implemented in game/shared/lua/lbaseplayer_shared.cpp, bound by
// c_viewmodel_attachment.cpp).

#ifndef HANDS_MODEL_MAPPING_H
#define HANDS_MODEL_MAPPING_H

#ifdef _WIN32
#pragma once
#endif

#include "Color.h"

// Per-player sleeve/weapon colour (GMod player:GetPlayerColor/SetPlayerColor).
Color HL2SB_GetPlayerColor( int iUserID );
void HL2SB_SetPlayerColor( int iUserID, const Color &clr );

#endif // HANDS_MODEL_MAPPING_H
