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

//-----------------------------------------------------------------------------
// HL2SB: client -> server net transport.
//
// Source usermessages only travel server -> client, so the client's write half
// rides the console-command channel instead: net.SendToServer() hex-encodes
// "name\0payload" and issues `hl2sb_netmsg <hex>`; the server decodes it in
// HL2SB_NetMsgCmd (server section below) and dispatches to the Lua
// net.Receive exactly like the "LuaNet" usermessage does in reverse.
//
// Size cap: clc stringcmd is ~1024 chars and hex doubles the bytes, so the
// payload is capped at 255 and the name at 63 (worst case ~653 chars).
// The minecraft SWEP's block-change message (one int) fits trivially.
//-----------------------------------------------------------------------------
static char g_cnetBuf[ HL2SB_NET_MAX_SIZE ];
static bf_write g_cnetWrite;
static CUtlString g_cnetName;
static bool g_cnetActive = false;

// net.Start( name )
static int net_Start( lua_State *L )
{
	const char *pszName = luaL_checkstring( L, 1 );
	g_cnetName = pszName;
	g_cnetWrite.StartWriting( g_cnetBuf, sizeof( g_cnetBuf ) );
	g_cnetActive = true;
	return 0;
}

// net.WriteBit( int )
static int net_WriteBit( lua_State *L )
{
	int val = luaL_checkint( L, 1 );
	g_cnetWrite.WriteOneBit( val ? 1 : 0 );
	return 0;
}

// net.WriteInt( int, bits )
static int net_WriteInt( lua_State *L )
{
	int val = luaL_checkint( L, 1 );
	int bits = luaL_optint( L, 2, 32 );
	g_cnetWrite.WriteSBitLong( val, bits );
	return 0;
}

// net.WriteUInt( uint, bits )
static int net_WriteUInt( lua_State *L )
{
	unsigned int val = (unsigned int)luaL_checkint( L, 1 );
	int bits = luaL_optint( L, 2, 32 );
	g_cnetWrite.WriteUBitLong( val, bits );
	return 0;
}

// net.WriteString( str )
static int net_WriteString( lua_State *L )
{
	const char *sz = luaL_checkstring( L, 1 );
	g_cnetWrite.WriteString( sz );
	return 0;
}

// net.WriteFloat( number )
static int net_WriteFloat( lua_State *L )
{
	g_cnetWrite.WriteFloat( (float)luaL_checknumber( L, 1 ) );
	return 0;
}

// net.WriteDouble( number )
static int net_WriteDouble( lua_State *L )
{
	g_cnetWrite.WriteLongLong( (int64)luaL_checknumber( L, 1 ) );
	return 0;
}

// net.WriteVector( vec )
static int net_WriteVector( lua_State *L )
{
	Vector v = luaL_checkvector( L, 1 );
	g_cnetWrite.WriteBitVec3Coord( v );
	return 0;
}

// net.WriteAngle( angle )
static int net_WriteAngle( lua_State *L )
{
	QAngle a = luaL_checkangle( L, 1 );
	g_cnetWrite.WriteBitAngles( a );
	return 0;
}

// net.WriteEntity( ent )
static int net_WriteEntity( lua_State *L )
{
	CBaseEntity *pEnt = luaL_checkentity( L, 1 );
	g_cnetWrite.WriteShort( pEnt ? pEnt->entindex() : 0 );
	return 0;
}

static const char *s_HexChars = "0123456789abcdef";

// HL2SB (2026-09-21): client->server command drip feed.  See net_SendToServer.
#define HL2SB_NETCMD_QUEUE_MAX  256
#define HL2SB_NETCMD_PER_FRAME  6
static CUtlVector<CUtlString> g_cnetCmdQueue;

// Pumped once per frame from the client's per-frame point (scripted viewport
// paint).  Issues at most HL2SB_NETCMD_PER_FRAME queued hl2sb_netmsg commands.
LUA_API void HL2SB_NetCmdPump ( void )
{
	int nFire = MIN( HL2SB_NETCMD_PER_FRAME, g_cnetCmdQueue.Count() );
	for ( int i = 0; i < nFire; ++i )
	{
		engine->ClientCmd( g_cnetCmdQueue[ 0 ].Get() );
		g_cnetCmdQueue.Remove( 0 );
	}
}

// net.SendToServer()
static int net_SendToServer( lua_State *L )
{
	if ( !g_cnetActive )
	{
		Msg( "[net] SendToServer called without a Start\n" );
		return 0;
	}
	g_cnetActive = false;

	const char *pszName = g_cnetName.Get();
	int nNameLen = Q_strlen( pszName );
	int nBytes = g_cnetWrite.GetNumBytesWritten();

	// HL2SB: a zero-byte payload is a legal GMod message (the minecraft SWEP's
	// "MinecraftSwepBlockChange" carries its data in userinfo convars and sends
	// an empty net message purely as a notification).  Only the transport-size
	// caps are enforced.
	if ( nNameLen < 1 || nNameLen >= 64 || nBytes < 0 || nBytes > 255 )
	{
		Warning( "[net] SendToServer: message '%s' out of transport range (name %d, payload %d bytes)\n",
			pszName, nNameLen, nBytes );
		return 0;
	}

	// wire = name \0 payload, hex-encoded
	char szHex[ 2 * ( 64 + 1 + 255 ) + 1 ];
	int nTotal = nNameLen + 1 + nBytes;
	char szWire[ 64 + 1 + 255 ];
	Q_memcpy( szWire, pszName, nNameLen );
	szWire[ nNameLen ] = '\0';
	Q_memcpy( szWire + nNameLen + 1, g_cnetBuf, nBytes );

	for ( int i = 0; i < nTotal; ++i )
	{
		szHex[ 2 * i ]     = s_HexChars[ ( szWire[ i ] >> 4 ) & 0xF ];
		szHex[ 2 * i + 1 ] = s_HexChars[ szWire[ i ] & 0xF ];
	}
	szHex[ 2 * nTotal ] = '\0';

	char szCmd[ sizeof( szHex ) + 32 ];
	Q_snprintf( szCmd, sizeof( szCmd ), "hl2sb_netmsg %s", szHex );

	// HL2SB (2026-09-21): THROTTLE.  ClientCmd lands in the server's command
	// buffer (Cbuf) on a listen server, and scp173's render-group registration
	// pushes one of these per entity every 0.5s -- hundreds per second once a
	// map is populated.  The unbounded burst overflowed Cbuf
	// ("Cbuf_AddText: buffer overflow" × hundreds) and starved the server
	// main thread: the game froze solid.  Queue the command and drip-feed a
	// few per frame from HL2SB_NetCmdPump (scripted viewport paint) instead.
	if ( g_cnetCmdQueue.Count() >= HL2SB_NETCMD_QUEUE_MAX )
	{
		static bool s_bOverflowWarned = false;
		if ( !s_bOverflowWarned )
		{
			Warning( "[net] SendToServer: client->server queue full (%d) -- dropping '%s'; the transport is rate-limited to %d commands/frame\n",
				HL2SB_NETCMD_QUEUE_MAX, pszName, HL2SB_NETCMD_PER_FRAME );
			s_bOverflowWarned = true;
		}
		return 0;
	}
	g_cnetCmdQueue.AddToTail( CUtlString( szCmd ) );
	return 0;
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
	// HL2SB: real client -> server transport (was an accepted-and-ignored stub).
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
	{ "SendToServer", net_SendToServer },
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

// HL2SB: server side of the client -> server net transport.
//
// The client hex-encodes "name\0payload" and issues `hl2sb_netmsg <hex>`
// (see net_SendToServer in the client section).  Clients can execute any
// non-cheat server ConCommand, and IVEngineServer::GetCommandClient() tells us
// whose command this is -- so a plain ConCommand here is the whole bridge.
// The payload is presented to Lua through the same bf_read + g_pNetRead-style
// state the client's usermessage dispatch uses, so net.ReadInt / ReadString /
// ReadHeader behave identically on both realms.

static bf_read *g_pNetReadSv = NULL;

struct CNetReceiverSv
{
	CUtlString m_Name;
	int m_Ref; // registry reference to the Lua function
};
static CUtlVector< CNetReceiverSv > g_NetReceiversSv;

// net.Receive( name, func )
static int net_Receive( lua_State *L )
{
	const char *pszName = luaL_checkstring( L, 1 );
	luaL_checktype( L, 2, LUA_TFUNCTION );

	for ( int i = 0; i < g_NetReceiversSv.Count(); i++ )
	{
		if ( !Q_stricmp( g_NetReceiversSv[i].m_Name.Get(), pszName ) )
		{
			luaL_unref( L, LUA_REGISTRYINDEX, g_NetReceiversSv[i].m_Ref );
			lua_pushvalue( L, 2 );
			g_NetReceiversSv[i].m_Ref = luaL_ref( L, LUA_REGISTRYINDEX );
			return 0;
		}
	}

	CNetReceiverSv rec;
	rec.m_Name = pszName;
	lua_pushvalue( L, 2 );
	rec.m_Ref = luaL_ref( L, LUA_REGISTRYINDEX );
	g_NetReceiversSv.AddToTail( rec );
	return 0;
}

// net.ReadHeader() -> name (the wire still leads with name \0, like LuaNet)
static int net_ReadHeader( lua_State *L )
{
	if ( !g_pNetReadSv )
	{
		lua_pushnil( L );
		return 1;
	}
	char szName[ 256 ];
	int iNameLen = 255;
	g_pNetReadSv->ReadString( szName, iNameLen );
	lua_pushstring( L, szName );
	return 1;
}

static int net_ReadBit( lua_State *L )
{
	lua_pushinteger( L, g_pNetReadSv ? g_pNetReadSv->ReadOneBit() : 0 );
	return 1;
}

static int net_ReadInt( lua_State *L )
{
	int bits = luaL_optint( L, 1, 32 );
	lua_pushinteger( L, g_pNetReadSv ? g_pNetReadSv->ReadSBitLong( bits ) : 0 );
	return 1;
}

static int net_ReadUInt( lua_State *L )
{
	int bits = luaL_optint( L, 1, 32 );
	lua_pushinteger( L, g_pNetReadSv ? (int)g_pNetReadSv->ReadUBitLong( bits ) : 0 );
	return 1;
}

static int net_ReadString( lua_State *L )
{
	if ( !g_pNetReadSv )
	{
		lua_pushstring( L, "" );
		return 1;
	}
	char szBuf[ 512 ];
	int iLen = sizeof( szBuf ) - 1;
	g_pNetReadSv->ReadString( szBuf, iLen );
	lua_pushstring( L, szBuf );
	return 1;
}

static int net_ReadFloat( lua_State *L )
{
	lua_pushnumber( L, g_pNetReadSv ? g_pNetReadSv->ReadFloat() : 0.0f );
	return 1;
}

static int net_ReadDouble( lua_State *L )
{
	lua_pushnumber( L, g_pNetReadSv ? g_pNetReadSv->ReadLongLong() : 0.0 );
	return 1;
}

static int net_ReadVector( lua_State *L )
{
	Vector v = vec3_origin;
	if ( g_pNetReadSv )
		g_pNetReadSv->ReadBitVec3Coord( v );
	lua_pushvector( L, v );
	return 1;
}

static int net_ReadAngle( lua_State *L )
{
	QAngle a = vec3_angle;
	if ( g_pNetReadSv )
		g_pNetReadSv->ReadBitAngles( a );
	lua_pushangle( L, a );
	return 1;
}

static int net_ReadEntity( lua_State *L )
{
	int idx = g_pNetReadSv ? g_pNetReadSv->ReadShort() : 0;
	CBaseEntity *pEnt = idx ? UTIL_EntityByIndex( idx ) : NULL;
	lua_pushentity( L, pEnt );
	return 1;
}

static int HexVal( char c )
{
	if ( c >= '0' && c <= '9' ) return c - '0';
	if ( c >= 'a' && c <= 'f' ) return c - 'a' + 10;
	if ( c >= 'A' && c <= 'F' ) return c - 'A' + 10;
	return -1;
}

// ConCommand handler: hl2sb_netmsg <hex-of "name\0payload">
static void HL2SB_NetMsgCmd( const CCommand &args )
{
	if ( args.ArgC() < 2 || !L )
		return;

	CBasePlayer *pPlayer = UTIL_GetCommandClient();

	// decode hex into the wire buffer
	const char *pszHex = args[ 1 ];
	int nHexLen = Q_strlen( pszHex );
	if ( nHexLen < 2 || ( nHexLen & 1 ) || nHexLen > 2 * ( 64 + 1 + 255 ) )
		return;

	static char szWire[ 64 + 1 + 255 ];
	int nTotal = nHexLen / 2;
	for ( int i = 0; i < nTotal; ++i )
	{
		int hi = HexVal( pszHex[ 2 * i ] );
		int lo = HexVal( pszHex[ 2 * i + 1 ] );
		if ( hi < 0 || lo < 0 )
			return;
		szWire[ i ] = ( char )( ( hi << 4 ) | lo );
	}

	// name must be a NUL-terminated prefix; an empty payload (nTotal ==
	// nNameLen + 1) is legal -- see net_SendToServer on the client
	szWire[ sizeof( szWire ) - 1 ] = '\0';
	int nNameLen = Q_strlen( szWire );
	if ( nNameLen < 1 || nNameLen + 1 > nTotal )
		return;

	char szName[ 64 ];
	Q_strncpy( szName, szWire, sizeof( szName ) );

	static bf_read s_NetRead;
	s_NetRead.StartReading( szWire, nTotal );

	for ( int i = 0; i < g_NetReceiversSv.Count(); i++ )
	{
		if ( !Q_stricmp( g_NetReceiversSv[i].m_Name.Get(), szName ) )
		{
			// HL2SB (2026-09-21): the wire is "name\0payload", so the reader
			// must skip the name before Lua sees it.  Seek( 0 ) handed the
			// receiver the NAME BYTES as its first read: net.ReadType decoded
			// the message name's first character as a typeid and raised
			// "Couldn't read type" on every single client->server message
			// carrying anything but raw bytes (scp173's screen-dimensions
			// table: 359 failures in one session).  The client dispatcher
			// already does this right -- it ReadStrings the name out of the
			// usermessage before calling the receiver.
			s_NetRead.Seek( ( nNameLen + 1 ) * 8 );
			g_pNetReadSv = &s_NetRead;

			lua_rawgeti( L, LUA_REGISTRYINDEX, g_NetReceiversSv[i].m_Ref );
			if ( lua_isfunction( L, -1 ) )
			{
				// GMod contract: fn( length, player ).  length is the PAYLOAD
				// in BITS (it used to be the whole wire size in bytes).
				lua_pushinteger( L, ( nTotal - nNameLen - 1 ) * 8 );
				lua_pushentity( L, pPlayer );

				// luasrc_pcall consumes the function and its arguments.
				luasrc_pcall( L, 2, 0, 0 );
			}
			else
			{
				lua_pop( L, 1 );
			}

			g_pNetReadSv = NULL;
			return;
		}
	}
}

static ConCommand hl2sb_netmsg_cmd( "hl2sb_netmsg", HL2SB_NetMsgCmd,
	"HL2SB Lua net client->server transport (hex payload); do not call by hand.", 0 );

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
	// HL2SB: server half of the client -> server transport (see HL2SB_NetMsgCmd).
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
