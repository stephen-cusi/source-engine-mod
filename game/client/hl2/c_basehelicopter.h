//========= Copyright Valve Corporation, All rights reserved. ============//
//
// Purpose: 
//
//=============================================================================//

#ifndef C_BASEHELICOPTER_H
#define C_BASEHELICOPTER_H
#ifdef _WIN32
#pragma once
#endif


#include "c_ai_basenpc.h"


class C_BaseHelicopter : public C_AI_BaseNPC
{
public:
	DECLARE_CLASS( C_BaseHelicopter, C_AI_BaseNPC );
	DECLARE_CLIENTCLASS();

	C_BaseHelicopter();

	float StartupTime() const { return m_flStartupTime; }

private:
	C_BaseHelicopter( const C_BaseHelicopter &other ) {}
	float m_flStartupTime;
};

// Потом переделаю по нормальному, но а пока - копипаста :P

class C_BaseHelicopter_HL1 : public C_AI_BaseNPC
{
public:
	DECLARE_CLASS( C_BaseHelicopter_HL1, C_AI_BaseNPC );
	DECLARE_CLIENTCLASS();

	C_BaseHelicopter_HL1();

	float StartupTime() const { return m_flStartupTime; }

private:
	C_BaseHelicopter_HL1( const C_BaseHelicopter &other ) {}
	float m_flStartupTime;
};

#endif // C_BASEHELICOPTER_H
