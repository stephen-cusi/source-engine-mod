//========= Copyright Valve Corporation, All rights reserved. ============//
//
// Purpose: HL2SB (2026-09-25): the weapon_physgun CLIENT entity is the real
// shared class now (game/shared/hl2sb/weapon_physgun.cpp, client build via
// IMPLEMENT_NETWORKCLASS_ALIASED + LINK_ENTITY_TO_CLASS).  This file used to
// register a STUB C_WeaponGravityGun (c_weapon__stubs.h) whose layout had no
// m_hObject -- it won the duplicate ClientClass registration, so the client
// entity was the stub and every shared-layout read (IsHolding/GetHeldEntity,
// the wheel push/pull branch, E-rotate view intercept, the dlight and the
// weapon-coloured beam) read past-the-object garbage or ran dead code.
//
// Only the muzzle-flash material precache still lives here.
//
// $NoKeywords: $
//===========================================================================//

#include "cbase.h"
#include "clienteffectprecachesystem.h"

// memdbgon must be the last include file in a .cpp file!!!
#include "tier0/memdbgon.h"

CLIENTEFFECT_REGISTER_BEGIN( PrecacheEffectGravityGun )
CLIENTEFFECT_MATERIAL( "sprites/physbeam" )
CLIENTEFFECT_REGISTER_END()
