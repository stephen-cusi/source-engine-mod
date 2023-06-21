//========= Copyright Valve Corporation, All rights reserved. ============//
//
// Purpose: 
//
//=============================================================================//

#ifndef HL1_PLAYER_SHARED_H
#define HL1_PLAYER_SHARED_H
#ifdef _WIN32
#pragma once
#endif

#include "cbase.h"
#include "base_playeranimstate.h"

// Shared header file for players
#if defined( CLIENT_DLL )
//#define CHL2MP_Player C_BaseHLPlayer	//FIXME: Lovely naming job between server and client here...
class C_HL2MP_Player;
#define CHL2MP_Player C_HL2MP_Player
#else
//#include "hl2mp_player.h"
class CHL2MP_Player;
#endif

#include "studio.h"

enum PlayerAnimEvent_t
{
	PLAYERANIMEVENT_FIRE_GUN=0,
	PLAYERANIMEVENT_THROW_GRENADE,
	PLAYERANIMEVENT_JUMP,
	PLAYERANIMEVENT_RELOAD,
	PLAYERANIMEVENT_DIE,
	
	PLAYERANIMEVENT_COUNT
};

class ICSPlayerAnimState : virtual public IPlayerAnimState
{
public:
	// This is called by both the client and the server in the same way to trigger events for
	// players firing, jumping, throwing grenades, etc.
	virtual void DoAnimationEvent( PlayerAnimEvent_t event, int nData ) = 0;
};

ICSPlayerAnimState* CreatePlayerAnimState( CHL2MP_Player *pPlayer );

// If this is set, then the game code needs to make sure to send player animation events
// to the local player if he's the one being watched.
extern ConVar cl_showanimstate;

#endif // HL1_PLAYER_SHARED_H
