//========== HL2SB ===========//
//
// Purpose: Lua `net` library - server<->client messaging.
//
//          HL2SB has no net system, so this implements a GMod-compatible core
//          on top of Source's usermessage primitives (UserMessageBegin /
//          MessageEnd / bf_write / bf_read).  A single variable-sized
//          usermessage "LuaNet" carries the logical message name as its first
//          field, then the payload written by the Lua script.
//
//          Server side (#ifndef CLIENT_DLL):
//            net.Start(name) / net.WriteInt / net.WriteUInt / net.WriteString /
//            net.WriteBit / net.WriteFloat / net.WriteDouble / net.WriteVector /
//            net.WriteAngle / net.WriteEntity / net.Send(ply) / net.Broadcast()
//
//          Client side (#ifdef CLIENT_DLL):
//            net.Receive(name, fn) / net.ReadInt / net.ReadUInt / net.ReadString /
//            net.ReadBit / net.ReadFloat / net.ReadDouble / net.ReadVector /
//            net.ReadAngle / net.ReadEntity / net.ReadHeader
//
//          The richer GMod sugar (WriteBool/Color/Table/Type/Player) is ported
//          in Lua (includes/extensions/net.lua) on top of these primitives.
//
//===========================================================================//

#include "cbase.h"
#include "lua.hpp"
#include "luasrclib.h"
#include "lbaseentity_shared.h"
#include "lbaseplayer_shared.h"
#include "lauxlib.h"
#include "lobject.h"
#include "luamanager.h"
#include "mathlib/lvector.h"

// memdbgon must be the last include file in a .cpp file!!!
#include "tier0/memdbgon.h"

#ifdef CLIENT_DLL
#include "usermessages.h"
#include "c_baseentity.h"
#else
#include "usermessages.h"
#include "recipientfilter.h"
#include "igameevents.h"
#include "player.h"
#endif

// memdbgon must be the last include file in a .cpp file!!!
#include "tier0/memdbgon.h"

// The global Lua state (declared extern in luamanager.h).  On the client this
// is the shared client lua_State used by the HUD hooks.
extern lua_State *L;

// Buffer size for one net message (bytes).  Source usermessages are limited by
// MAX_USER_MSG_DATA at the transport layer; keep this modest.
#define HL2SB_NET_MAX_SIZE 512

//-----------------------------------------------------------------------------
// Shared message-name registry (both sides keep the same ordered list so that
// names can be looked up by index, but we simply send the name string inline,
// which is the most robust and avoids any ordering requirement).
//-----------------------------------------------------------------------------

//-----------------------------------------------------------------------------
// Shared message-name registry (both sides keep the same ordered list so that
// names can be looked up by index, but we simply send the name string inline,
// which is the most robust and avoids any ordering requirement).
//-----------------------------------------------------------------------------

#ifdef CLIENT_DLL
//-----------------------------------------------------------------------------
// Client side
//-----------------------------------------------------------------------------

static bf_read *g_pNetRead = NULL;

// The Lua receiver table: name (lowercase) -> function.
// We key it with a registry reference so it survives across L state resets within
// a level; simplest is a Lua-side table, but we store the callbacks in a
// C++ map keyed by name and call them directly.
struct CNetReceiver
{
	CUtlString m_Name;
	int m_Ref; // registry reference to the Lua function
};
static CUtlVector< CNetReceiver > g_NetReceivers;

//-----------------------------------------------------------------------------
// Purpose: dispatch the received "LuaNet" usermessage to the right Lua receiver.
//-----------------------------------------------------------------------------
static void MsgFunc_LuaNet( bf_read &msg )
{
	// The buffer passed to a usermessage hook cannot be Seek()ed to 0 reliably
	// (it is already positioned after the usermessage header on some paths),
	// so we copy it into our own bf_read that we can seek on.
	char szName[ 256 ];
	int iNameLen = 255;
	msg.ReadString( szName, iNameLen );
	if ( !szName[0] )
		return;

	// Find the receiver.
	for ( int i = 0; i < g_NetReceivers.Count(); i++ )
	{
		if ( !Q_stricmp( g_NetReceivers[i].m_Name.Get(), szName ) )
		{
			g_pNetRead = &msg;

			if ( !L )
			{
				Msg( "[net] no lua state for receive\n" );
				g_pNetRead = NULL;
				return;
			}

			lua_rawgeti( L, LUA_REGISTRYINDEX, g_NetReceivers[i].m_Ref );
			if ( lua_isfunction( L, -1 ) )
			{
				// Call the receiver with (len, client).  len is approximate.
				lua_pushinteger( L, msg.GetNumBytesLeft() );
				lua_pushnil( L ); // client handle unsupported

				// luasrc_pcall CONSUMES the receiver along with its two
				// arguments, so nothing may be popped afterwards.
				//
				// This used to be an unconditional lua_pop( L, 1 ) placed AFTER
				// the if -- on the function path that popped one slot below the
				// C function's own frame.  lua_settop then underflowed, making
				// `L->tbclist.p >= newtop` true, so luaF_close tried to close a
				// slot that had never been a to-be-closed variable and raised
				// "attempt to call a nil value" OUTSIDE any protected call --
				// and with no panic function installed that aborted the process
				// through __fastfail.  Every LuaNet message (i.e. every
				// spawnmenu action) reached it, which is why the whole smenu
				// killed the game with client.dll / 0xC0000409.
				luasrc_pcall( L, 2, 0, 0 );
			}
			else
			{
				lua_pop( L, 1 );	// drop the non-function value rawgeti pushed
			}

			g_pNetRead = NULL;
			return;
		}
	}
}

//-----------------------------------------------------------------------------
// Purpose: register the LuaNet usermessage hook once.
//-----------------------------------------------------------------------------
static void EnsureLuaNetHook()
{
	static bool bHooked = false;
	if ( bHooked )
		return;
	usermessages->HookMessage( "LuaNet", MsgFunc_LuaNet );
	bHooked = true;
}

// net.Receive( name, func )
static int net_Receive( lua_State *L )
{
	const char *pszName = luaL_checkstring( L, 1 );
	luaL_checktype( L, 2, LUA_TFUNCTION );

	EnsureLuaNetHook();

	// Replace if already registered.
	for ( int i = 0; i < g_NetReceivers.Count(); i++ )
	{
		if ( !Q_stricmp( g_NetReceivers[i].m_Name.Get(), pszName ) )
		{
			luaL_unref( L, LUA_REGISTRYINDEX, g_NetReceivers[i].m_Ref );
			lua_pushvalue( L, 2 );
			g_NetReceivers[i].m_Ref = luaL_ref( L, LUA_REGISTRYINDEX );
			return 0;
		}
	}

	CNetReceiver rec;
	rec.m_Name = pszName;
	lua_pushvalue( L, 2 );
	rec.m_Ref = luaL_ref( L, LUA_REGISTRYINDEX );
	g_NetReceivers.AddToTail( rec );
	return 0;
}

// net.ReadHeader() -> name
static int net_ReadHeader( lua_State *L )
{
	if ( !g_pNetRead || !g_pNetRead->Seek( 0 ) )
	{
		lua_pushnil( L );
		return 1;
	}
	char szName[ 256 ];
	int iNameLen = 255;
	g_pNetRead->ReadString( szName, iNameLen );
	lua_pushstring( L, szName );
	return 1;
}

// net.ReadBit() -> int (0/1)
static int net_ReadBit( lua_State *L )
{
	lua_pushinteger( L, g_pNetRead ? g_pNetRead->ReadOneBit() : 0 );
	return 1;
}

// net.ReadInt( bits ) / net.ReadUInt( bits ) - signed/unsigned N-bit read
static int net_ReadInt( lua_State *L )
{
	int bits = luaL_optint( L, 1, 32 );
	lua_pushinteger( L, g_pNetRead ? g_pNetRead->ReadSBitLong( bits ) : 0 );
	return 1;
}

static int net_ReadUInt( lua_State *L )
{
	int bits = luaL_optint( L, 1, 32 );
	lua_pushinteger( L, g_pNetRead ? (int)g_pNetRead->ReadUBitLong( bits ) : 0 );
	return 1;
}

// net.ReadString() -> string
static int net_ReadString( lua_State *L )
{
	if ( !g_pNetRead )
	{
		lua_pushstring( L, "" );
		return 1;
	}
	char szBuf[ 512 ];
	int iLen = sizeof( szBuf ) - 1;
	g_pNetRead->ReadString( szBuf, iLen );
	lua_pushstring( L, szBuf );
	return 1;
}

// net.ReadFloat() -> number
static int net_ReadFloat( lua_State *L )
{
	lua_pushnumber( L, g_pNetRead ? g_pNetRead->ReadFloat() : 0.0f );
	return 1;
}

// net.ReadDouble() -> number
static int net_ReadDouble( lua_State *L )
{
	lua_pushnumber( L, g_pNetRead ? g_pNetRead->ReadLongLong() : 0.0f );
	return 1;
}

// net.ReadVector() -> Vector
static int net_ReadVector( lua_State *L )
{
	Vector v;
	if ( g_pNetRead )
		g_pNetRead->ReadBitVec3Coord( v );
	else
		v = vec3_origin;
	lua_pushvector( L, v );
	return 1;
}

// net.ReadAngle() -> Angle
static int net_ReadAngle( lua_State *L )
{
	QAngle a;
	if ( g_pNetRead )
		g_pNetRead->ReadBitAngles( a );
	else
		a = vec3_angle;
	lua_pushangle( L, a );
	return 1;
}

// net.ReadEntity() -> entity
static int net_ReadEntity( lua_State *L )
{
	int idx = g_pNetRead ? g_pNetRead->ReadShort() : 0;
	C_BaseEntity *pEnt = idx ? C_BaseEntity::Instance( idx ) : NULL;
	lua_pushentity( L, pEnt );
	return 1;
}

static const luaL_Reg net_funcs[] = {
	{ "Receive",     net_Receive },
	{ "ReadHeader",  net_ReadHeader },
	{ "ReadBit",     net_ReadBit },
	{ "ReadInt",     net_ReadInt },
	{ "ReadUInt",    net_ReadUInt },
	{ "ReadString",  net_ReadString },
	{ "ReadFloat",   net_ReadFloat },
	{ "ReadDouble",  net_ReadDouble },
	{ "ReadVector",  net_ReadVector },
	{ "ReadAngle",   net_ReadAngle },
	{ "ReadEntity",  net_ReadEntity },
	{ NULL, NULL }
};

LUALIB_API int luaopen_net( lua_State *L )
{
	// Client: also expose a static net.Receivers table so Lua sugar can use it.
	luaL_register( L, "net", net_funcs );
	lua_newtable( L ); // net.Receivers
	lua_setfield( L, -2, "Receivers" );
	return 1;
}

#else // !CLIENT_DLL
//-----------------------------------------------------------------------------
// Server side
//-----------------------------------------------------------------------------

static char g_netBuf[ HL2SB_NET_MAX_SIZE ];
static bf_write g_netWrite;
static CUtlString g_netName;
static bool g_netActive = false;

// net.Start( name )
static int net_Start( lua_State *L )
{
	const char *pszName = luaL_checkstring( L, 1 );
	g_netName = pszName;
	g_netWrite.StartWriting( g_netBuf, sizeof( g_netBuf ) );
	g_netActive = true;
	return 0;
}

// net.WriteBit( int )
static int net_WriteBit( lua_State *L )
{
	int val = luaL_checkint( L, 1 );
	g_netWrite.WriteOneBit( val ? 1 : 0 );
	return 0;
}

// net.WriteInt( int, bits )
static int net_WriteInt( lua_State *L )
{
	int val = luaL_checkint( L, 1 );
	int bits = luaL_optint( L, 2, 32 );
	g_netWrite.WriteSBitLong( val, bits );
	return 0;
}

// net.WriteUInt( uint, bits )
static int net_WriteUInt( lua_State *L )
{
	unsigned int val = (unsigned int)luaL_checkint( L, 1 );
	int bits = luaL_optint( L, 2, 32 );
	g_netWrite.WriteUBitLong( val, bits );
	return 0;
}

// net.WriteString( str )
static int net_WriteString( lua_State *L )
{
	const char *sz = luaL_checkstring( L, 1 );
	g_netWrite.WriteString( sz );
	return 0;
}

// net.WriteFloat( number )
static int net_WriteFloat( lua_State *L )
{
	g_netWrite.WriteFloat( (float)luaL_checknumber( L, 1 ) );
	return 0;
}

// net.WriteDouble( number )
static int net_WriteDouble( lua_State *L )
{
	g_netWrite.WriteLongLong( (int64)luaL_checknumber( L, 1 ) );
	return 0;
}

// net.WriteVector( vec )
static int net_WriteVector( lua_State *L )
{
	Vector v = luaL_checkvector( L, 1 );
	g_netWrite.WriteBitVec3Coord( v );
	return 0;
}

// net.WriteAngle( angle )
static int net_WriteAngle( lua_State *L )
{
	QAngle a = luaL_checkangle( L, 1 );
	g_netWrite.WriteBitAngles( a );
	return 0;
}

// net.WriteEntity( ent ) - writes the entity index as a short.
static int net_WriteEntity( lua_State *L )
{
	CBaseEntity *pEnt = luaL_checkentity( L, 1 );
	g_netWrite.WriteShort( pEnt ? pEnt->entindex() : 0 );
	return 0;
}

//-----------------------------------------------------------------------------
// Purpose: common send to a recipient filter.
//-----------------------------------------------------------------------------
static void SendNetMessage( IRecipientFilter &filter )
{
	if ( !g_netActive )
	{
		Msg( "[net] Send called without a Start\n" );
		return;
	}

	UserMessageBegin( filter, "LuaNet" );
	MessageWriteString( g_netName.Get() );
	// Push the buffered payload straight into the engine message buffer.
	MessageWriteBits( g_netWrite.GetData(), g_netWrite.GetNumBitsWritten() );
	MessageEnd();

	g_netActive = false;
}

// net.Send( ply )
static int net_Send( lua_State *L )
{
	CBasePlayer *pPlayer = luaL_checkplayer( L, 1 );
	CRecipientFilter filter;
	filter.AddRecipient( pPlayer );
	SendNetMessage( filter );
	return 0;
}

// net.Broadcast()
static int net_Broadcast( lua_State *L )
{
	CBroadcastRecipientFilter filter;
	SendNetMessage( filter );
	return 0;
}

// HL2SB GMod compat: net.SendToServer().
//
// GMod's client -> server channel.  This fork's net library is the server ->
// client one (net.Start ... net.Send/Broadcast): there is no message the client
// can send back, so this accepts the call and does nothing.  Addons that use it
// for a request -- the windgrin_npc nextbot asks the server to generate a nav
// mesh -- therefore stay alive instead of dying on a nil method; the request is
// simply not delivered.
static int net_SendToServer( lua_State *L )
{
	return 0;
}

static const luaL_Reg net_funcs[] = {
	{ "Start",       net_Start },
	{ "WriteBit",    net_WriteBit },
	{ "WriteInt",    net_WriteInt },
	{ "WriteUInt",   net_WriteUInt },
	{ "WriteString", net_WriteString },
	{ "WriteFloat",  net_WriteFloat },
	{ "WriteDouble", net_WriteDouble },
	{ "WriteVector", net_WriteVector },
	{ "WriteAngle",  net_WriteAngle },
	{ "WriteEntity", net_WriteEntity },
	{ "Send",        net_Send },
	{ "Broadcast",   net_Broadcast },
	// HL2SB GMod compat: accepted and ignored (see net_SendToServer above).
	{ "SendToServer", net_SendToServer },
	{ NULL, NULL }
};

LUALIB_API int luaopen_net( lua_State *L )
{
	luaL_register( L, "net", net_funcs );
	lua_newtable( L );
	lua_setfield( L, -2, "Receivers" );
	return 1;
}

#endif // CLIENT_DLL

//-----------------------------------------------------------------------------
// HL2SB: drop every net.Receive registration.
//
// Each receiver stores a luaL_ref() -- a registry INDEX into the Lua state that
// is about to be closed.  Nothing cleared that list on a level change, so after
// luasrc_shutdown()/luasrc_init() the next LuaNet usermessage looked the stale
// index up in the NEW state's registry, where it can hold a completely
// unrelated value.  That is exactly what happened at level init: a net message
// arrived during the transition, the dispatch walked into a state that was not
// the one the refs came from, and the resulting error was raised outside any
// protected call -- which, with no panic function installed, killed the process
// via __fastfail.
//
// Clearing here means the transitional window simply has no receivers, so
// nothing is dispatched until the new level's scripts register again.
//-----------------------------------------------------------------------------
LUA_API void luasrc_net_reset( void )
{
#ifdef CLIENT_DLL
	g_NetReceivers.RemoveAll();
	g_pNetRead = NULL;
#endif
}
