//========= Copyright Valve Corporation, All rights reserved. ============//
//
// Purpose: 
//
//=============================================================================//

#ifndef C_HL1_GAMEMOVEMENT_H
#define C_HL1_GAMEMOVEMENT_H
#ifdef _WIN32
#pragma once
#endif

#ifdef CLIENT_DLL
	#include "c_hl2mp_player.h"
#else
	#include "hl2mp_player.h"
#endif

#if defined( CLIENT_DLL )
	class CHL2MP_Player;
	#define CHL2MP_Player C_HL1_Player
#endif

#define PLAYER_LONGJUMP_SPEED 350 // how fast we longjump

class CHL1GameMovement : public CGameMovement
{
public:
	DECLARE_CLASS( CHL1GameMovement, CGameMovement );

	virtual bool CheckJumpButton( void );

	// Duck
	virtual void Duck( void );
	virtual void HandleDuckingSpeedCrop();
	virtual void CheckParameters( void );

protected:
	CHL2MP_Player *m_pHL1Player;
};

#endif //C_HL1_GAMEMOVEMENT_H