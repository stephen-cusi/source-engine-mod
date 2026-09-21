//========= Copyright © 1996-2005, Valve Corporation, All rights reserved. ============//
//
// Purpose: 
//
// $NoKeywords: $
//=============================================================================//

#ifndef LBASEPLAYER_SHARED_H
#define LBASEPLAYER_SHARED_H
#ifdef _WIN32
#pragma once
#endif

/* type for CBasePlayer functions */
typedef CBasePlayer lua_CBasePlayer;



/*
** access functions (stack -> C)
*/

LUA_API lua_CBasePlayer     *(lua_toplayer) (lua_State *L, int idx);


/*
** push functions (C -> stack)
*/
LUA_API void  (lua_pushplayer) (lua_State *L, lua_CBasePlayer *pPlayer);



LUALIB_API lua_CBasePlayer *(luaL_checkplayer) (lua_State *L, int narg);
LUALIB_API lua_CBasePlayer *(luaL_optplayer) (lua_State *L, int narg,
                                                            lua_CBasePlayer *def);


/*
** HL2SB GMod compat (2026-09-22 physgun audit): the per-player frozen-object
** list behind Player:AddFrozenPhysicsObject / PhysgunUnfreeze /
** UnfreezePhysicsObjects.  SERVER only (defined under #ifndef CLIENT_DLL).
** The physgun's freeze path records bodies here; its R path drains them.
*/
class CBaseEntity;
class IPhysicsObject;
void HL2SB_PlayerAddFrozenObject( CBasePlayer *pPlayer, CBaseEntity *pEnt, IPhysicsObject *pPhys );
int  HL2SB_PlayerUnfreezeAimed( CBasePlayer *pPlayer );
int  HL2SB_PlayerUnfreezeAll( CBasePlayer *pPlayer );


#endif // LBASEPLAYER_SHARED_H
