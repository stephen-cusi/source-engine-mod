// HL2SB: GMod's sql library -- SQLite-backed sql.Query / sql.QueryTyped.
//
// GMod static-links SQLite into both game DLLs and binds exactly two C
// functions on the global "sql" table; every other member (SQLStr,
// TableExists, IndexExists, QueryRow, QueryValue, Begin, Commit, LastError)
// is plain Lua in lua/includes/util/sql.lua, which includes/init.lua loads
// right after util.lua.  This file mirrors that split.
//
// Reference behavior baked in below:
//   * the database file lives in the game dir (sv.db server side, cl.db
//     client side); an unopenable file degrades to an in-memory database
//     instead of failing every query forever
//   * on open: synchronous = OFF, read_uncommitted = 1, temp_store = MEMORY,
//     and DEFENSIVE mode (ordinary SQL may not write to the schema tables)
//   * sql.Query runs one or more semicolon-separated statements, appends all
//     rows of all statements into one result table keyed by column name with
//     every value as a string (SQL NULL as the literal string "NULL"); a
//     successful statement with no rows returns nil, a failed one returns
//     false and records sql.m_strError
//   * sql.QueryTyped runs a single statement with "?" parameters bound from
//     the Lua arguments (nil, boolean, integer, number, string) and returns
//     typed values, always a table (empty when nothing matched); an INTEGER
//     column whose declared type is bool/boolean comes back as a boolean,
//     and integers beyond double precision come back formatted as strings
//
// Only plain C library surface is used (sqlite3 + the engine game dir), so
// this builds unchanged on every platform the game DLLs build for.
// sqlite3.c itself is the official amalgamation (public domain).

#include "cbase.h"
#include "luamanager.h"
#include "lsql.h"
#include "sqlite/sqlite3.h"

#ifdef CLIENT_DLL
#include "cdll_client_int.h"
#else
#include "enginecallback.h"
#endif

// memdbgon must be the last include file in a .cpp file!!!
#include "tier0/memdbgon.h"

// One database handle per realm DLL: server.dll opens <gamedir>/sv.db,
// client.dll <gamedir>/cl.db.
static sqlite3 *g_pSQLDatabase = NULL;

// Row counter shared between sql_Query and its exec callback: > 0 after a
// successful exec means at least one row landed in the result table, which
// is what decides table-vs-nil.
static int g_nSQLQueryRows = 0;

static void SQL_SetLuaError( lua_State *L, const char *pszError )
{
	// Reference keeps the message on the sql table as the m_strError field;
	// the Lua shell's sql.LastError() just reads that field back.
	lua_getglobal( L, "sql" );
	if ( lua_istable( L, -1 ) )
	{
		lua_pushstring( L, pszError );
		lua_setfield( L, -2, "m_strError" );
	}
	lua_pop( L, 1 );
}

static void SQL_OpenDatabase( void )
{
#ifdef CLIENT_DLL
	const char *pszFile = "cl.db";
#else
	const char *pszFile = "sv.db";
#endif
	char szGameDir[512];
	char szPath[640];

#ifdef CLIENT_DLL
	Q_strncpy( szGameDir, engine->GetGameDirectory(), sizeof( szGameDir ) );
#else
	engine->GetGameDir( szGameDir, sizeof( szGameDir ) );
#endif
	Q_snprintf( szPath, sizeof( szPath ), "%s/%s", szGameDir, pszFile );

	if ( sqlite3_open( szPath, &g_pSQLDatabase ) != SQLITE_OK )
	{
		// Reference fallback: in-memory database when the file cannot be
		// opened, so the API keeps answering (nothing persists).
		sqlite3_close( g_pSQLDatabase );
		g_pSQLDatabase = NULL;
		sqlite3_open( ":memory:", &g_pSQLDatabase );
	}

	if ( g_pSQLDatabase == NULL )
		return;

	sqlite3_exec( g_pSQLDatabase,
		"PRAGMA synchronous = OFF; PRAGMA read_uncommitted = 1; PRAGMA temp_store = MEMORY;",
		NULL, NULL, NULL );

	// Same hardening as the reference: ordinary SQL must not write to the
	// schema tables.
	int iUnused = 0;
	sqlite3_db_config( g_pSQLDatabase, SQLITE_DBCONFIG_DEFENSIVE, 1, &iUnused );
}

static int sql_QueryExecCallback( void *pContext, int nColumns, char **ppValues, char **ppNames )
{
	lua_State *L = ( lua_State * )pContext;

	// The result table sits at stack index 2 for the whole exec.
	lua_newtable( L );
	for ( int i = 0; i < nColumns; ++i )
	{
		// Every value is a string; SQL NULL becomes the literal "NULL".
		lua_pushstring( L, ppNames[i] );
		lua_pushstring( L, ppValues[i] != NULL ? ppValues[i] : "NULL" );
		lua_settable( L, -3 );
	}
	lua_rawseti( L, 2, ++g_nSQLQueryRows );
	return 0;
}

static int sql_Query( lua_State *L )
{
	const char *pszSQL = luaL_optstring( L, 1, "" );

	if ( g_pSQLDatabase == NULL )
		SQL_OpenDatabase();

	if ( *pszSQL == '\0' )
	{
		SQL_SetLuaError( L, "No Query" );
		lua_pushboolean( L, false );
		return 1;
	}

	g_nSQLQueryRows = 0;
	lua_newtable( L );		// [1] = sql, [2] = result table

	char *pszError = NULL;
	if ( sqlite3_exec( g_pSQLDatabase, pszSQL, sql_QueryExecCallback, L, &pszError ) != SQLITE_OK )
	{
		if ( pszError != NULL )
		{
			SQL_SetLuaError( L, pszError );
			sqlite3_free( pszError );
		}
		lua_pop( L, 1 );
		lua_pushboolean( L, false );
		return 1;
	}

	if ( g_nSQLQueryRows == 0 )
	{
		// Reference returns nothing (nil in Lua) when no rows came back.
		lua_pop( L, 1 );
		return 0;
	}
	return 1;
}

static int sql_QueryTyped( lua_State *L )
{
	const char *pszSQL = luaL_optstring( L, 1, "" );

	if ( g_pSQLDatabase == NULL )
		SQL_OpenDatabase();

	sqlite3_stmt *pStmt = NULL;
	if ( sqlite3_prepare_v2( g_pSQLDatabase, pszSQL, -1, &pStmt, NULL ) != SQLITE_OK )
	{
		SQL_SetLuaError( L, sqlite3_errmsg( g_pSQLDatabase ) );
		lua_pushboolean( L, false );
		return 1;
	}

	int nParams = sqlite3_bind_parameter_count( pStmt );
	if ( nParams != lua_gettop( L ) - 1 )
	{
		sqlite3_finalize( pStmt );
		SQL_SetLuaError( L, "incorrect number of parameters provided" );
		lua_pushboolean( L, false );
		return 1;
	}

	for ( int i = 1; i <= nParams; ++i )
	{
		int rc = SQLITE_OK;
		switch ( lua_type( L, i + 1 ) )
		{
			case LUA_TNIL:
				rc = sqlite3_bind_null( pStmt, i );
				break;
			case LUA_TBOOLEAN:
				// Reference binds booleans as 0/1 integers.
				rc = sqlite3_bind_int( pStmt, i, lua_toboolean( L, i + 1 ) ? 1 : 0 );
				break;
			case LUA_TNUMBER:
				// Integral values bind as 64-bit ints, fractional as double.
				if ( lua_isinteger( L, i + 1 ) )
					rc = sqlite3_bind_int64( pStmt, i, lua_tointeger( L, i + 1 ) );
				else
					rc = sqlite3_bind_double( pStmt, i, lua_tonumber( L, i + 1 ) );
				break;
			case LUA_TSTRING:
			{
				size_t nLen = 0;
				const char *pszValue = lua_tolstring( L, i + 1, &nLen );
				rc = sqlite3_bind_text( pStmt, i, pszValue, ( int )nLen, SQLITE_TRANSIENT );
				break;
			}
			default:
				sqlite3_finalize( pStmt );
				SQL_SetLuaError( L, "unsupported parameter type for binding" );
				lua_pushboolean( L, false );
				return 1;
		}
		if ( rc != SQLITE_OK )
		{
			sqlite3_finalize( pStmt );
			SQL_SetLuaError( L, sqlite3_errmsg( g_pSQLDatabase ) );
			lua_pushboolean( L, false );
			return 1;
		}
	}

	// QueryTyped always returns a table, even when nothing matched.
	lua_newtable( L );		// [nParams+2] = result table
	int iResult = lua_gettop( L );
	int nRow = 0;
	int rc = SQLITE_OK;
	while ( ( rc = sqlite3_step( pStmt ) ) == SQLITE_ROW )
	{
		lua_newtable( L );
		int iRow = lua_gettop( L );
		int nColumns = sqlite3_column_count( pStmt );
		for ( int c = 0; c < nColumns; ++c )
		{
			const char *pszName = sqlite3_column_name( pStmt, c );
			switch ( sqlite3_column_type( pStmt, c ) )
			{
				case SQLITE_INTEGER:
				{
					sqlite3_int64 iValue = sqlite3_column_int64( pStmt, c );
					// Reference quirk kept: an INTEGER column whose DECLARED
					// type (sqlite3_column_decltype) is "bool"/"boolean",
					// case-insensitively, comes back as a Lua boolean.
					const char *pszType = sqlite3_column_decltype( pStmt, c );
					if ( pszType != NULL &&
						( Q_stricmp( pszType, "bool" ) == 0 ||
						  Q_stricmp( pszType, "boolean" ) == 0 ) )
					{
						lua_pushboolean( L, iValue != 0 );
					}
					else if ( iValue >= -( ( sqlite3_int64 )1 << 53 ) && iValue <= ( ( sqlite3_int64 )1 << 53 ) )
					{
						lua_pushnumber( L, ( lua_Number )iValue );
					}
					else
					{
						// Beyond double precision: formatted as a string,
						// since a Lua float could not hold it exactly.
						char szInt[32];
						Q_snprintf( szInt, sizeof( szInt ), "%lld", ( long long )iValue );
						lua_pushstring( L, szInt );
					}
					break;
				}
				case SQLITE_FLOAT:
					lua_pushnumber( L, sqlite3_column_double( pStmt, c ) );
					break;
				case SQLITE_TEXT:
				case SQLITE_BLOB:
				{
					// Reference reads both as the text pointer plus the
					// byte length, which preserves embedded NULs.
					const unsigned char *pData = sqlite3_column_text( pStmt, c );
					int nBytes = sqlite3_column_bytes( pStmt, c );
					lua_pushlstring( L, pData != NULL ? ( const char * )pData : "", nBytes );
					break;
				}
				case SQLITE_NULL:
				default:
					// NULL columns are simply absent from the row.
					continue;
			}
			lua_pushstring( L, pszName );
			lua_insert( L, -2 );			// [name][value]
			lua_settable( L, iRow );		// row[name] = value
		}
		++nRow;
		lua_rawseti( L, iResult, nRow );	// pops the row table
	}
	sqlite3_finalize( pStmt );

	if ( rc == SQLITE_DONE )
		return 1;		// result table on top

	SQL_SetLuaError( L, sqlite3_errmsg( g_pSQLDatabase ) );
	lua_pop( L, 1 );		// drop the result table
	lua_pushboolean( L, false );
	return 1;
}

static const luaL_Reg s_SqlFuncs[] =
{
	{ "Query",		sql_Query },
	{ "QueryTyped",	sql_QueryTyped },
	{ NULL,			NULL },
};

LUALIB_API int luaopen_Sql( lua_State *L )
{
	// luasrc_openlibs calls each luaopen with the library name and discards
	// return values, so this installs the global itself, exactly like
	// LUA_REGISTRATION_COMMIT_LIBRARY does for the other libraries.
	luaL_register( L, "sql", s_SqlFuncs );
	lua_pop( L, 1 );
	return 0;
}
