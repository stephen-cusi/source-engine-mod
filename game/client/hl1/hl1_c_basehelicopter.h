//========= Copyright Valve Corporation, All rights reserved. ============//
//
// Purpose: 
//
//=============================================================================//

#ifndef C_HL1MPBASEHELICOPTER_H
#define C_HL1MPBASEHELICOPTER_H
#ifdef _WIN32
#pragma once
#endif


#include "c_ai_basenpc.h"


class C_HL1MPBaseHelicopter : public C_AI_BaseNPC
{
public:
	DECLARE_CLASS( C_HL1MPBaseHelicopter, C_AI_BaseNPC );
	DECLARE_CLIENTCLASS();

	C_HL1MPBaseHelicopter();

	float StartupTime() const { return m_flStartupTime; }

private:
	C_HL1MPBaseHelicopter( const C_HL1MPBaseHelicopter &other ) {}
	float m_flStartupTime;
};


#endif // C_BASEHELICOPTER_H
