#ifndef LUA_SQL_H
#define LUA_SQL_H
#ifdef _WIN32
#pragma once
#endif

#include <lua.h>

// HL2SB: SQLite-backed sql library (GMod compat).  Registers the global
// "sql" table with sql.Query / sql.QueryTyped and keeps sql.m_strError
// updated; the remaining GMod sql surface (SQLStr, TableExists, IndexExists,
// QueryRow, QueryValue, Begin, Commit, LastError) is Lua in
// lua/includes/util/sql.lua.
LUALIB_API int luaopen_Sql( lua_State *L );

#endif // LUA_SQL_H
