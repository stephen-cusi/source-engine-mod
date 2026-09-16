//========= Copyright Valve Corporation, All rights reserved. ============//
//
// Purpose: 
//
// $NoKeywords: $
//=============================================================================//

#ifndef IN_MAIN_H
#define IN_MAIN_H
#ifdef _WIN32
#pragma once
#endif


#include "kbutton.h"


extern kbutton_t in_commandermousemove;
extern kbutton_t in_ducktoggle;
// HL2SB: +duck (Ctrl by default) has to be readable from the player code so it can
// toggle the camera while riding a vehicle.
extern kbutton_t in_duck;

#endif // IN_MAIN_H
