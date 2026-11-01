//========== HL2SB ===========//
//
// Purpose: Lua `net` library - server<->client messaging.
//
//          Reference behavior: Garry's Mod's net library.  Every message is a
//          self-contained buffer of [16-bit pooled name id][payload bits] and
//          every send is one immediate engine call - there is no queue and no
//          per-frame rate limiting anywhere in the chain.
//
//          Transport per realm:
//
//          client -> server : dedicated reliable engine packet
//            (clc_LuaNetMessage, see protocol.h).  net.SendToServer hands the
//            buffer to IVEngineClient::SendLuaNetMessage; the server routes it
//            through IServerGameClients::ProcessLuaNetMessage back into this
//            file, which calls Lua's net.Incoming( length, player ).
//
//          server -> client : variable-sized usermessage "LuaNet"
//            (payload = the same [id][payload] layout).  The client hook also
//            ends in Lua net.Incoming( length ).
//
//          Message names are pooled in the engine network string table
//          "LuaNetStrings" (created server side in gameinterface.cpp), which
//          replicates to clients automatically - util.AddNetworkString on the
//          server and net.Start on both realms resolve ids through it, so an
//          unpooled name raises the same error the reference implementation
//          raises.
//
//          The Lua side (includes/extensions/net.lua) mirrors the reference
//          structure: net.Receive stores into net.Receivers (lowercased names)
//          and the default net.Incoming reads the header and dispatches; both
//          are plain Lua and can be overridden by scripts.
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
#include "mathlib/vmatrix.h"
#include "mathlib/lvmatrix.h"
#include <lColor.h>
#include "usermessages.h"
#include "tier1/bitbuf.h"

static void HL2SB_RegisterNetConstants( lua_State *L )
{
    static const struct { const char *name; int value; } types[] = {
        { "TYPE_NIL", 0 }, { "TYPE_BOOL", 1 }, { "TYPE_LIGHTUSERDATA", 2 },
        { "TYPE_NUMBER", 3 }, { "TYPE_STRING", 4 }, { "TYPE_TABLE", 5 },
        { "TYPE_FUNCTION", 6 }, { "TYPE_USERDATA", 7 }, { "TYPE_THREAD", 8 },
        { "TYPE_ENTITY", 9 }, { "TYPE_VECTOR", 10 }, { "TYPE_ANGLE", 11 },
        { "TYPE_MATRIX", 29 }, { "TYPE_COLOR", 255 }
    };
    for ( int i = 0; i < ARRAYSIZE( types ); ++i )
    {
        lua_pushinteger( L, types[i].value );
        lua_setglobal( L, types[i].name );
    }
    lua_pushinteger( L, MAX_EDICT_BITS );
    lua_setglobal( L, "MAX_EDICT_BITS" );
    int playerBits = 0;
    for ( unsigned int limit = ABSOLUTE_PLAYER_LIMIT; limit; limit >>= 1 ) ++playerBits;
    lua_pushinteger( L, playerBits );
    lua_setglobal( L, "MAX_PLAYER_BITS" );
}

// HL2SB GMod compat: color support for net.WriteColor
// HL2SB GMod compat (2026-09-25): net.WriteTable / net.ReadTable reuse the
// util.TableToJSON / util.JSONToTable implementations (local extern per the
// "don't touch headers for one consumer" rule).
int luasrc_UTIL_TableToJSON( lua_State *L );
int luasrc_UTIL_JSONToTable( lua_State *L );

#ifdef CLIENT_DLL
#include "c_baseentity.h"
#include "networkstringtable_clientdll.h"
#else
#include "recipientfilter.h"
#include "igameevents.h"
#include "player.h"
#include "networkstringtable_gamedll.h"
#include "lrecipientfilter.h"
#endif

// memdbgon must be the last include file in a .cpp file!!!
#include "tier0/memdbgon.h"

// The global Lua state (declared extern in luamanager.h).  On the client this
// is the shared client lua_State used by the HUD hooks.
extern lua_State *L;

// Reference behavior: GMod allocates a 64 kB buffer for every net message and
// caps net.WriteData at 65536 / net.ReadData at 65535 bytes.
#define HL2SB_NET_MAX_SIZE		65536
// Bits on the wire taken by the pooled name id.
#define HL2SB_NET_HEADER_BITS	16

//-----------------------------------------------------------------------------
// Shared: the pooled message-name table ("LuaNetStrings").
//
// Lua-visible ids are (string table index + 1) so that 0 can mean "unknown
// name"; the raw index values never leave this file except inside messages.
//-----------------------------------------------------------------------------

static INetworkStringTable *Net_NameTable( void )
{
	return networkstringtable ? networkstringtable->FindTable( "LuaNetStrings" ) : NULL;
}

// 0 = not pooled (client realm only sees names the server has added)
unsigned short HL2SB_NetStringToID( const char *pszName )
{
	INetworkStringTable *pTable = Net_NameTable();
	if ( !pTable || !pszName || !pszName[0] )
		return 0;

	int iIndex = pTable->FindStringIndex( pszName );
	if ( iIndex == INVALID_STRING_INDEX )
		return 0;

	return (unsigned short)( iIndex + 1 );
}

// NULL = unknown id
const char *HL2SB_NetworkIDToString( unsigned short nID )
{
	if ( nID == 0 )
		return NULL;

	INetworkStringTable *pTable = Net_NameTable();
	if ( !pTable )
		return NULL;

	int iIndex = (int)nID - 1;
	if ( iIndex < 0 || iIndex >= pTable->GetNumStrings() )
		return NULL;

	return pTable->GetString( iIndex );
}

// Server: pools the name and returns its id.  Client realm: names can only be
// pooled by the server, so this resolves an already-replicated name and fails
// (0) otherwise - the shared builtin content calls this on both realms.
unsigned short HL2SB_AddNetworkString( const char *pszName )
{
	INetworkStringTable *pTable = Net_NameTable();
	if ( !pTable || !pszName || !pszName[0] )
		return 0;

	int iIndex = pTable->FindStringIndex( pszName );
	if ( iIndex != INVALID_STRING_INDEX )
		return (unsigned short)( iIndex + 1 );

#ifdef CLIENT_DLL
	return 0;
#else
	iIndex = pTable->AddString( true, pszName );
	if ( iIndex < 0 || iIndex == INVALID_STRING_INDEX )
		return 0;
	return (unsigned short)( iIndex + 1 );
#endif
}

// net.ReadHeader() -> the 16-bit id the message leads with
static int net_ReadHeader( lua_State *L );

// bf_read of the message currently being dispatched (NULL outside dispatch)
#ifdef CLIENT_DLL
static bf_read *g_pNetRead = NULL;
#else
static bf_read *g_pNetReadSv = NULL;
#endif

static bf_read *Net_GetRead( void )
{
#ifdef CLIENT_DLL
	return g_pNetRead;
#else
	return g_pNetReadSv;
#endif
}

static void Net_SetRead( bf_read *pRead )
{
#ifdef CLIENT_DLL
	g_pNetRead = pRead;
#else
	g_pNetReadSv = pRead;
#endif
}

static int net_ReadHeader( lua_State *L )
{
	bf_read *pRead = Net_GetRead();
	if ( !pRead )
	{
		lua_pushinteger( L, 0 );
		return 1;
	}

	// reference behavior: the header is the 16-bit pooled name id
	lua_pushinteger( L, (lua_Integer)pRead->ReadUBitLong( HL2SB_NET_HEADER_BITS ) );
	return 1;
}

#ifdef CLIENT_DLL
//-----------------------------------------------------------------------------
// Client side
//-----------------------------------------------------------------------------

// Buffer for the message being composed (64 kB like the reference library).
static char g_cnetBuf[ HL2SB_NET_MAX_SIZE ];
static bf_write g_cnetWrite;
static CUtlString g_cnetName;
static bool g_cnetActive = false;
static bool g_cnetReliable = true;

// Reference behavior: the C side never calls receivers directly; it calls Lua
// net.Incoming( length ), which reads the header and dispatches to whatever
// Lua has installed (default: net.Receivers).  length covers the 16-bit id
// plus the payload, exactly like the reference dispatcher.
static void MsgFunc_LuaNet( bf_read &msg )
{
	if ( !L )
	{
		luasrc_LuaInfoMsgF( "[net] no lua state for receive\n" );
		return;
	}

	Net_SetRead( &msg );

	lua_getglobal( L, "net" );
	if ( lua_istable( L, -1 ) )
	{
		lua_getfield( L, -1, "Incoming" );
		if ( lua_isfunction( L, -1 ) )
		{
			lua_pushnumber( L, (lua_Number)msg.GetNumBitsLeft() );
			luasrc_pcall( L, 1, 0, 0 );
		}
		else
		{
			lua_pop( L, 1 );
		}
	}
	lua_pop( L, 1 );

	Net_SetRead( NULL );
}

static void EnsureLuaNetHook()
{
	static bool bHooked = false;
	if ( bHooked )
		return;
	usermessages->HookMessage( "LuaNet", MsgFunc_LuaNet );
	bHooked = true;
}

// net.Receive( name, func ) lives in Lua (net.lua); nothing to do here.

// net.ReadBit() -> int (0/1)
static int net_ReadBit( lua_State *L )
{
	bf_read *pRead = Net_GetRead();
	lua_pushinteger( L, pRead ? pRead->ReadOneBit() : 0 );
	return 1;
}

// net.ReadInt( bits ) / net.ReadUInt( bits ) - signed/unsigned N-bit read
static int net_ReadInt( lua_State *L )
{
	int bits = luaL_optint( L, 1, 32 );
	bf_read *pRead = Net_GetRead();
	lua_pushinteger( L, pRead ? pRead->ReadSBitLong( bits ) : 0 );
	return 1;
}

static int net_ReadUInt( lua_State *L )
{
	int bits = luaL_optint( L, 1, 32 );
	bf_read *pRead = Net_GetRead();
	lua_pushinteger( L, pRead ? (int)pRead->ReadUBitLong( bits ) : 0 );
	return 1;
}

// net.ReadString() -> string
static int net_ReadString( lua_State *L )
{
	if ( !Net_GetRead() )
	{
		lua_pushstring( L, "" );
		return 1;
	}
	char szBuf[ 512 ];
	int iLen = sizeof( szBuf ) - 1;
	Net_GetRead()->ReadString( szBuf, iLen );
	lua_pushstring( L, szBuf );
	return 1;
}

// net.ReadFloat() -> number
static int net_ReadFloat( lua_State *L )
{
	bf_read *pRead = Net_GetRead();
	lua_pushnumber( L, pRead ? pRead->ReadFloat() : 0.0f );
	return 1;
}

// net.ReadDouble() -> number
static int net_ReadDouble( lua_State *L )
{
	bf_read *pRead = Net_GetRead();
	if ( !pRead ) return 0;
	double value = 0.0;
	pRead->ReadBits( &value, 64 );
	lua_pushnumber( L, value );
	return 1;
}

// net.ReadVector() -> Vector
static int net_ReadMatrix( lua_State *L )
{
    bf_read *reader = Net_GetRead();
    if ( !reader ) return 0;
    VMatrix matrix;
    for ( int row = 0; row < 4; ++row )
        for ( int col = 0; col < 4; ++col )
            matrix[row][col] = reader->ReadFloat();
    lua_pushvmatrix( L, matrix );
    return 1;
}

static int net_ReadVector( lua_State *L )
{
	Vector v;
	bf_read *pRead = Net_GetRead();
	if ( pRead )
		pRead->ReadBitVec3Coord( v );
	else
		v = vec3_origin;
	lua_pushvector( L, v );
	return 1;
}

// net.ReadAngle() -> Angle
static int net_ReadNormal( lua_State *L )
{
    bf_read *reader = Net_GetRead();
    if (!reader) return 0;
    Vector value = vec3_origin;
    reader->ReadBitVec3Normal(value);
    lua_pushvector(L, value);
    return 1;
}

static int net_ReadAngle( lua_State *L )
{
	QAngle a;
	bf_read *pRead = Net_GetRead();
	if ( pRead )
		pRead->ReadBitAngles( a );
	else
		a = vec3_angle;
	lua_pushangle( L, a );
	return 1;
}

// net.ReadEntity() -> entity
static int net_ReadEntity( lua_State *L )
{
	bf_read *pRead = Net_GetRead();
	int idx = pRead ? pRead->ReadShort() : 0;
	C_BaseEntity *pEnt = idx ? C_BaseEntity::Instance( idx ) : NULL;
	lua_pushentity( L, pEnt );
	return 1;
}

// --- HL2SB GMod compat: wiki-checked members (client realm) ---

static int net_ReadBool( lua_State *L )
{
	bf_read *pRead = Net_GetRead();
	lua_pushboolean( L, pRead ? pRead->ReadOneBit() != 0 : false );
	return 1;
}

static int net_ReadData( lua_State *L )
{
	int nBytes = luaL_checkint( L, 1 );

	// reference behavior: lengths above 65535 raise a Lua error
	if ( nBytes <= 0 )
	{
		lua_pushstring( L, "" );
		return 1;
	}
	if ( nBytes > 0xffff )
		return luaL_error( L, "net.ReadData: Invalid length %i!", nBytes );

	bf_read *pRead = Net_GetRead();
	if ( !pRead )
	{
		lua_pushstring( L, "" );
		return 1;
	}

	static char s_pBuf[ 0xffff ];
	pRead->ReadBytes( s_pBuf, nBytes );
	lua_pushlstring( L, s_pBuf, nBytes );
	return 1;
}

static int net_ReadColor( lua_State *L )
{
	if ( !Net_GetRead() )
	{
		lua_pushnil( L );
		return 1;
	}
	int r = Net_GetRead()->ReadByte();
	int g = Net_GetRead()->ReadByte();
	int b = Net_GetRead()->ReadByte();
	int a = Net_GetRead()->ReadByte();
	lua_pushcolor( L, Color( r, g, b, a ) );
	return 1;
}

static int net_ReadTable( lua_State *L )
{
	char szBuf[ 4096 ];
	int iLen = sizeof( szBuf ) - 1;
	if ( Net_GetRead() )
	{
		Net_GetRead()->ReadString( szBuf, iLen );
		szBuf[ iLen ] = '\0';
	}
	else
	{
		szBuf[ 0 ] = '\0';
	}
	while ( lua_gettop( L ) < 1 ) lua_pushnil( L );
	lua_pushstring( L, szBuf );
	lua_replace( L, 1 );
	return luasrc_UTIL_JSONToTable( L );
}

static int net_BytesLeft( lua_State *L )
{
	bf_read *pRead = Net_GetRead();
	lua_pushinteger( L, pRead ? pRead->GetNumBytesLeft() : 0 );
	return 1;
}

static int net_Abort( lua_State *L )
{
	g_cnetActive = false;
	return 0;
}

// net.Start( name, [unreliable=false] ) -> bool
static int net_Start( lua_State *L )
{
	const char *pszName = luaL_checkstring( L, 1 );

	unsigned short nID = HL2SB_NetStringToID( pszName );
	if ( nID == 0 )
	{
		// reference behavior: same error text for an unpooled name
		return luaL_error( L, "Calling net.Start with unpooled message name! (Did you forget to call util.AddNetworkString serverside?)" );
	}

	if ( g_cnetActive )
	{
		luasrc_LuaWarnMsgF( "Warning! A net message (%s) is already started! Discarding in favor of the new message! (%s)\n",
			g_cnetName.Get(), pszName );
	}

	g_cnetName = pszName;
	g_cnetWrite.StartWriting( g_cnetBuf, sizeof( g_cnetBuf ) );
	g_cnetWrite.WriteByte( 0 );
	g_cnetWrite.WriteUBitLong( nID, HL2SB_NET_HEADER_BITS );
	g_cnetActive = true;

	// reference behavior: messages are reliable unless the caller marks them
	g_cnetReliable = ( lua_toboolean( L, 2 ) == 0 );

	return 1;
}

// every writer silently does nothing when no message is started (reference
// behavior: the write bindings check the started flag first)
static int net_WriteBit( lua_State *L )
{
	if ( !g_cnetActive )
		return 0;

	// GMod wiki: net.WriteBit( boolean ) - addons pass true/false here,
	// stock code passes 0/1 numbers.  luaL_checkint rejects booleans,
	// which killed the hitnumbers handlers mid-message.
	int val;
	if ( lua_type( L, 1 ) == LUA_TBOOLEAN )
		val = lua_toboolean( L, 1 ) ? 1 : 0;
	else
		val = luaL_checkint( L, 1 );
	g_cnetWrite.WriteOneBit( val ? 1 : 0 );
	return 0;
}

static int net_WriteInt( lua_State *L )
{
	if ( !g_cnetActive )
		return 0;
	int val = luaL_checkint( L, 1 );
	int bits = luaL_optint( L, 2, 32 );
	g_cnetWrite.WriteSBitLong( val, bits );
	return 0;
}

static int net_WriteUInt( lua_State *L )
{
	if ( !g_cnetActive )
		return 0;
	unsigned int val = (unsigned int)luaL_checkint( L, 1 );
	int bits = luaL_optint( L, 2, 32 );
	g_cnetWrite.WriteUBitLong( val, bits );
	return 0;
}

static int net_WriteString( lua_State *L )
{
	if ( !g_cnetActive )
		return 0;
	const char *sz = luaL_checkstring( L, 1 );
	g_cnetWrite.WriteString( sz );
	return 0;
}

static int net_WriteFloat( lua_State *L )
{
	if ( !g_cnetActive )
		return 0;
	g_cnetWrite.WriteFloat( (float)luaL_checknumber( L, 1 ) );
	return 0;
}

static int net_WriteDouble( lua_State *L )
{
	if ( !g_cnetActive )
		return 0;
	const double value = luaL_checknumber( L, 1 );
	g_cnetWrite.WriteBits( &value, 64 );
	return 0;
}

static int net_WriteMatrix( lua_State *L )
{
    if ( !g_cnetActive ) return 0;
    const VMatrix &matrix = luaL_checkvmatrix( L, 1 );
    for ( int row = 0; row < 4; ++row )
        for ( int col = 0; col < 4; ++col )
            g_cnetWrite.WriteFloat( matrix[row][col] );
    return 0;
}

static int net_WriteVector( lua_State *L )
{
	if ( !g_cnetActive )
		return 0;
	Vector v = luaL_checkvector( L, 1 );
	g_cnetWrite.WriteBitVec3Coord( v );
	return 0;
}

static int net_WriteNormal( lua_State *L )
{
    if (!g_cnetActive) return 0;
    const Vector value = luaL_checkvector(L, 1);
    g_cnetWrite.WriteBitVec3Normal(value);
    return 0;
}

static int net_WriteAngle( lua_State *L )
{
	if ( !g_cnetActive )
		return 0;
	QAngle a = luaL_checkangle( L, 1 );
	g_cnetWrite.WriteBitAngles( a );
	return 0;
}

static int net_WriteEntity( lua_State *L )
{
	if ( !g_cnetActive )
		return 0;
	CBaseEntity *pEnt = luaL_checkentity( L, 1 );
	g_cnetWrite.WriteShort( pEnt ? pEnt->entindex() : 0 );
	return 0;
}

static int net_WriteBool( lua_State *L )
{
	if ( !g_cnetActive )
		return 0;
	g_cnetWrite.WriteOneBit( lua_toboolean( L, 1 ) ? 1 : 0 );
	return 0;
}

static int net_WriteData( lua_State *L )
{
	if ( !g_cnetActive )
		return 0;

	size_t nLen = 0;
	const char *pszData = luaL_checklstring( L, 1, &nLen );
	int nBytes = (int)luaL_optint( L, 2, (int)nLen );

	// reference behavior: length above 65536 is rejected with a warning
	if ( nBytes > 0x10000 )
	{
		luasrc_LuaWarnMsgF( "net.WriteData: Invalid length %i!\n", nBytes );
		return 0;
	}
	if ( nBytes <= 0 )
		return 0;
	if ( nBytes > (int)nLen )
	{
		luasrc_LuaWarnMsgF( "net.WriteData (%s): Warning! Given length (%i) is longer than length of provided data (%i)! Clamping!\n",
			g_cnetName.Get(), nBytes, (int)nLen );
		nBytes = (int)nLen;
	}

	g_cnetWrite.WriteBytes( pszData, nBytes );
	return 0;
}

static int net_WriteColor( lua_State *L )
{
	if ( !g_cnetActive )
		return 0;
	Color clr = luaL_checkcolor( L, 1 );
	bool bWriteAlpha = lua_isnone( L, 2 ) ? true : ( lua_toboolean( L, 2 ) != 0 );
	g_cnetWrite.WriteByte( clr.r() );
	g_cnetWrite.WriteByte( clr.g() );
	g_cnetWrite.WriteByte( clr.b() );
	if ( bWriteAlpha ) g_cnetWrite.WriteByte( clr.a() );
	return 0;
}

static int net_WriteTable( lua_State *L )
{
	if ( !g_cnetActive )
		return 0;
	luasrc_UTIL_TableToJSON( L );	// reads stack slot 1 (the table), pushes JSON
	const char *pszJson = lua_tostring( L, -1 );
	g_cnetWrite.WriteString( pszJson ? pszJson : "" );
	lua_pop( L, 1 );
	return 0;
}

static int net_BytesWritten( lua_State *L )
{
	lua_pushinteger( L, g_cnetActive ? g_cnetWrite.GetNumBytesWritten() : 0 );
	return 1;
}

// net.SendToServer()
//
// Reference behavior: one immediate engine call per message - no queue, no
// per-frame rate limit.  An overflowed buffer is warned about (every time)
// and the message still goes out with whatever made it into the buffer.
static int net_SendToServer( lua_State *L )
{
	if ( !g_cnetActive )
		return 0;

	g_cnetActive = false;

	if ( g_cnetWrite.IsOverflowed() )
	{
		luasrc_LuaWarnMsgF( "Trying to send an overflowed net message!\n" );
	}

	// A refused send used to be silently dropped; every property action rides
	// this path, so report it instead of leaving a dead click with no trace.
	if ( !engine->SendLuaNetMessage( g_cnetWrite.GetData(), g_cnetWrite.GetNumBitsWritten(), g_cnetReliable ) )
	{
		luasrc_LuaWarnMsgF( "net.SendToServer: engine refused message '%s' (%d bits)\n",
			g_cnetName.Get(), g_cnetWrite.GetNumBitsWritten() );
	}
	return 0;
}

static const luaL_Reg net_funcs[] = {
	{ "Start",        net_Start },
	{ "SendToServer", net_SendToServer },
	{ "WriteBit",     net_WriteBit },
	{ "WriteInt",     net_WriteInt },
	{ "WriteUInt",    net_WriteUInt },
	{ "WriteString",  net_WriteString },
	{ "WriteFloat",   net_WriteFloat },
	{ "WriteDouble",  net_WriteDouble },
	{ "WriteMatrix",  net_WriteMatrix },
	{ "WriteVector",  net_WriteVector },
	{ "WriteNormal",  net_WriteNormal },
	{ "WriteAngle",   net_WriteAngle },
	{ "WriteEntity",  net_WriteEntity },
	{ "WriteBool",    net_WriteBool },
	{ "ReadBool",     net_ReadBool },
	{ "WriteData",    net_WriteData },
	{ "ReadData",     net_ReadData },
	{ "WriteColor",   net_WriteColor },
	{ "ReadColor",    net_ReadColor },
	{ "WriteTable",   net_WriteTable },
	{ "ReadTable",    net_ReadTable },
	{ "BytesWritten", net_BytesWritten },
	{ "BytesLeft",    net_BytesLeft },
	{ "Abort",        net_Abort },
	{ "ReadHeader",   net_ReadHeader },
	{ "ReadBit",      net_ReadBit },
	{ "ReadInt",      net_ReadInt },
	{ "ReadUInt",     net_ReadUInt },
	{ "ReadString",   net_ReadString },
	{ "ReadFloat",    net_ReadFloat },
	{ "ReadDouble",   net_ReadDouble },
	{ "ReadMatrix",   net_ReadMatrix },
	{ "ReadVector",   net_ReadVector },
	{ "ReadNormal",   net_ReadNormal },
	{ "ReadAngle",    net_ReadAngle },
	{ "ReadEntity",   net_ReadEntity },
	{ NULL, NULL }
};

LUALIB_API int luaopen_net( lua_State *L )
{
	HL2SB_RegisterNetConstants( L );
	// net.Receive / net.Receivers / net.Incoming are created by the Lua
	// extension (includes/extensions/net.lua), like the reference library.
	// The client also arms the "LuaNet" usermessage hook here: nothing else
	// in C knows when Lua receivers appear, and the hook is per-dll state.
	EnsureLuaNetHook();
	luaL_register( L, "net", net_funcs );
	return 1;
}

#else // !CLIENT_DLL
//-----------------------------------------------------------------------------
// Server side
//-----------------------------------------------------------------------------

static char g_netBuf[ HL2SB_NET_MAX_SIZE ];
static bf_write g_netWrite;
static CUtlString g_netName;
static unsigned short g_netID = 0;
static bool g_netActive = false;
// GMod: net.Start( name, unreliable = false ) - messages are RELIABLE unless
// the caller explicitly marks them unreliable.  This flag mirrors that second
// argument; SendNetMessage applies it to the recipient filter.
static bool g_netUnreliable = false;

// Legacy usermessages have their own composition state, independent of net.
// Keep the existing client transport while resolving recipients natively.
static char s_UserMessageBuffer[MAX_USER_MSG_DATA];
static bf_write s_UserMessageWrite;
static CRecipientFilter s_UserMessageRecipients;
static bool s_UserMessageActive = false;

static CBaseEntity *umsg_CheckEntity( lua_State *L, int index )
{
	if ( lua_isnoneornil( L, index ) )
		return NULL;
	bool entityType = false;
	if ( lua_type( L, index ) == LUA_TUSERDATA && lua_getmetatable( L, index ) )
	{
		lua_pushliteral( L, "MetaID" );
		lua_rawget( L, -2 );
		entityType = lua_tointeger( L, -1 ) == 9;
		lua_pop( L, 2 );
	}
	if ( !entityType )
		luaL_argerror( L, index, "Entity expected" );
	return lua_toentity( L, index );
}

static CBasePlayer *umsg_CheckPlayer( lua_State *L, int index )
{
	CBaseEntity *entity = umsg_CheckEntity( L, index );
	if ( !entity || !entity->IsPlayer() )
		luaL_argerror( L, index, "Player expected" );
	return static_cast<CBasePlayer *>(entity);
}

static int umsg_Start( lua_State *L )
{
	const char *name = luaL_checkstring( L, 1 );
	s_UserMessageActive = false;
	s_UserMessageRecipients.Reset();
	if ( lua_isnoneornil( L, 2 ) )
	{
		s_UserMessageRecipients.AddAllPlayers();
	}
	else if ( luaL_testudata( L, 2, "CRecipientFilter" ) || luaL_testudata( L, 2, "CPASFilter" ) )
	{
		s_UserMessageRecipients.CopyFrom( luaL_checkrecipientfilter( L, 2 ) );
	}
	else if ( lua_istable( L, 2 ) )
	{
		lua_pushnil( L );
		while ( lua_next( L, 2 ) != 0 )
		{
			s_UserMessageRecipients.AddRecipient( umsg_CheckPlayer( L, -1 ) );
			lua_pop( L, 1 );
		}
	}
	else
	{
		s_UserMessageRecipients.AddRecipient( umsg_CheckPlayer( L, 2 ) );
	}
	const unsigned short id = HL2SB_AddNetworkString( "hl2sb_umsg" );
	if ( !id )
		return luaL_error( L, "Unable to register usermessage transport" );
	s_UserMessageWrite.StartWriting( s_UserMessageBuffer, sizeof(s_UserMessageBuffer) );
	s_UserMessageWrite.SetAssertOnOverflow( false );
	s_UserMessageWrite.WriteUBitLong( id, HL2SB_NET_HEADER_BITS );
	s_UserMessageWrite.WriteString( name );
	s_UserMessageActive = true;
	return 0;
}

static int umsg_End( lua_State *L )
{
	if ( !s_UserMessageActive )
		return luaL_error( L, "umsg.End called without umsg.Start" );
	s_UserMessageActive = false;
	if ( s_UserMessageWrite.IsOverflowed() )
	{
		luasrc_LuaWarnMsgF( "Usermessage exceeded the transport limit; message discarded\n" );
		return 0;
	}
	s_UserMessageRecipients.MakeReliable();
	UserMessageBegin( s_UserMessageRecipients, "LuaNet" );
	MessageWriteBits( s_UserMessageWrite.GetData(), s_UserMessageWrite.GetNumBitsWritten() );
	MessageEnd();
	return 0;
}

static int umsg_WriteInteger( lua_State *L )
{
	const int value = (int)luaL_checknumber( L, 1 );
	if ( s_UserMessageActive )
		s_UserMessageWrite.WriteSBitLong( value, (int)lua_tointeger( L, lua_upvalueindex(1) ) );
	return 0;
}

static int umsg_WriteBool( lua_State *L )
{
	if ( s_UserMessageActive ) s_UserMessageWrite.WriteOneBit( lua_toboolean( L, 1 ) );
	return 0;
}

static int umsg_WriteFloat( lua_State *L )
{
	const float value = (float)luaL_checknumber( L, 1 );
	if ( s_UserMessageActive ) s_UserMessageWrite.WriteFloat( value );
	return 0;
}

static int umsg_WriteString( lua_State *L )
{
	const char *value = luaL_checkstring( L, 1 );
	if ( s_UserMessageActive ) s_UserMessageWrite.WriteString( value );
	return 0;
}

static int umsg_WriteEntity( lua_State *L )
{
	CBaseEntity *entity = umsg_CheckEntity( L, 1 );
	if ( s_UserMessageActive ) s_UserMessageWrite.WriteShort( entity ? entity->entindex() : 0 );
	return 0;
}

static int umsg_WriteVector( lua_State *L )
{
	const Vector value = luaL_checkvector( L, 1 );
	if ( s_UserMessageActive ) s_UserMessageWrite.WriteBitVec3Coord( value );
	return 0;
}

static int umsg_WriteAngle( lua_State *L )
{
	const QAngle value = luaL_checkangle( L, 1 );
	if ( s_UserMessageActive ) s_UserMessageWrite.WriteBitAngles( value );
	return 0;
}

void HL2SB_InstallNativeUserMessages( lua_State *L )
{
	static const luaL_Reg functions[] = {
		{ "Start", umsg_Start }, { "End", umsg_End },
		{ "Bool", umsg_WriteBool }, { "Float", umsg_WriteFloat },
		{ "String", umsg_WriteString }, { "Entity", umsg_WriteEntity },
		{ "Vector", umsg_WriteVector }, { "Angle", umsg_WriteAngle },
		{ NULL, NULL }
	};
	luaL_register( L, "umsg", functions );
	const char *names[] = { "Char", "Short", "Long" };
	const int bits[] = { 8, 16, 32 };
	for ( int i = 0; i < ARRAYSIZE(names); ++i )
	{
		lua_pushinteger( L, bits[i] );
		lua_pushcclosure( L, umsg_WriteInteger, 1 );
		lua_setfield( L, -2, names[i] );
	}
	lua_pop( L, 1 );
}

// net.Start( name, [unreliable=false] ) -> bool
static int net_Start( lua_State *L )
{
	const char *pszName = luaL_checkstring( L, 1 );

	unsigned short nID = HL2SB_NetStringToID( pszName );
	if ( nID == 0 )
	{
		// reference behavior: same error text for an unpooled name
		return luaL_error( L, "Calling net.Start with unpooled message name! (Did you forget to call util.AddNetworkString serverside?)" );
	}

	if ( g_netActive )
	{
		luasrc_LuaWarnMsgF( "Warning! A net message (%s) is already started! Discarding in favor of the new message! (%s)\n",
			g_netName.Get(), pszName );
	}

	g_netName = pszName;
	g_netID = nID;
	g_netWrite.StartWriting( g_netBuf, sizeof( g_netBuf ) );
	g_netWrite.WriteUBitLong( nID, HL2SB_NET_HEADER_BITS );
	g_netActive = true;
	g_netUnreliable = ( lua_toboolean( L, 2 ) != 0 );

	return 1;
}

static int net_WriteBit( lua_State *L )
{
	if ( !g_netActive )
		return 0;

	// GMod wiki: net.WriteBit( boolean ) - addons pass true/false here,
	// stock code passes 0/1 numbers.  luaL_checkint rejects booleans,
	// which killed the hitnumbers handlers mid-message.
	int val;
	if ( lua_type( L, 1 ) == LUA_TBOOLEAN )
		val = lua_toboolean( L, 1 ) ? 1 : 0;
	else
		val = luaL_checkint( L, 1 );
	g_netWrite.WriteOneBit( val ? 1 : 0 );
	return 0;
}

static int net_WriteInt( lua_State *L )
{
	if ( !g_netActive )
		return 0;
	int val = luaL_checkint( L, 1 );
	int bits = luaL_optint( L, 2, 32 );
	g_netWrite.WriteSBitLong( val, bits );
	return 0;
}

static int net_WriteUInt( lua_State *L )
{
	if ( !g_netActive )
		return 0;
	unsigned int val = (unsigned int)luaL_checkint( L, 1 );
	int bits = luaL_optint( L, 2, 32 );
	g_netWrite.WriteUBitLong( val, bits );
	return 0;
}

static int net_WriteString( lua_State *L )
{
	if ( !g_netActive )
		return 0;
	const char *sz = luaL_checkstring( L, 1 );
	g_netWrite.WriteString( sz );
	return 0;
}

static int net_WriteFloat( lua_State *L )
{
	if ( !g_netActive )
		return 0;
	g_netWrite.WriteFloat( (float)luaL_checknumber( L, 1 ) );
	return 0;
}

static int net_WriteDouble( lua_State *L )
{
	if ( !g_netActive )
		return 0;
	const double value = luaL_checknumber( L, 1 );
	g_netWrite.WriteBits( &value, 64 );
	return 0;
}

static int net_WriteMatrix( lua_State *L )
{
    if ( !g_netActive ) return 0;
    const VMatrix &matrix = luaL_checkvmatrix( L, 1 );
    for ( int row = 0; row < 4; ++row )
        for ( int col = 0; col < 4; ++col )
            g_netWrite.WriteFloat( matrix[row][col] );
    return 0;
}

static int net_WriteVector( lua_State *L )
{
	if ( !g_netActive )
		return 0;
	Vector v = luaL_checkvector( L, 1 );
	g_netWrite.WriteBitVec3Coord( v );
	return 0;
}

static int net_WriteNormal( lua_State *L )
{
    if (!g_netActive) return 0;
    const Vector value = luaL_checkvector(L, 1);
    g_netWrite.WriteBitVec3Normal(value);
    return 0;
}

static int net_WriteAngle( lua_State *L )
{
	if ( !g_netActive )
		return 0;
	QAngle a = luaL_checkangle( L, 1 );
	g_netWrite.WriteBitAngles( a );
	return 0;
}

static int net_WriteEntity( lua_State *L )
{
	if ( !g_netActive )
		return 0;
	CBaseEntity *pEnt = luaL_checkentity( L, 1 );
	g_netWrite.WriteShort( pEnt ? pEnt->entindex() : 0 );
	return 0;
}

static int net_WriteBool( lua_State *L )
{
	if ( !g_netActive )
		return 0;
	g_netWrite.WriteOneBit( lua_toboolean( L, 1 ) ? 1 : 0 );
	return 0;
}

static int net_WriteData( lua_State *L )
{
	if ( !g_netActive )
		return 0;

	size_t nLen = 0;
	const char *pszData = luaL_checklstring( L, 1, &nLen );
	int nBytes = (int)luaL_optint( L, 2, (int)nLen );

	// reference behavior: length above 65536 is rejected with a warning
	if ( nBytes > 0x10000 )
	{
		luasrc_LuaWarnMsgF( "net.WriteData: Invalid length %i!\n", nBytes );
		return 0;
	}
	if ( nBytes <= 0 )
		return 0;
	if ( nBytes > (int)nLen )
	{
		luasrc_LuaWarnMsgF( "net.WriteData (%s): Warning! Given length (%i) is longer than length of provided data (%i)! Clamping!\n",
			g_netName.Get(), nBytes, (int)nLen );
		nBytes = (int)nLen;
	}

	g_netWrite.WriteBytes( pszData, nBytes );
	return 0;
}

static int net_WriteColor( lua_State *L )
{
	if ( !g_netActive )
		return 0;
	Color clr = luaL_checkcolor( L, 1 );
	bool bWriteAlpha = lua_isnone( L, 2 ) ? true : ( lua_toboolean( L, 2 ) != 0 );
	g_netWrite.WriteByte( clr.r() );
	g_netWrite.WriteByte( clr.g() );
	g_netWrite.WriteByte( clr.b() );
	if ( bWriteAlpha ) g_netWrite.WriteByte( clr.a() );
	return 0;
}

static int net_WriteTable( lua_State *L )
{
	if ( !g_netActive )
		return 0;
	luasrc_UTIL_TableToJSON( L );	// reads stack slot 1 (the table), pushes JSON
	const char *pszJson = lua_tostring( L, -1 );
	g_netWrite.WriteString( pszJson ? pszJson : "" );
	lua_pop( L, 1 );
	return 0;
}

static int net_WritePlayer( lua_State *L )
{
	if ( !g_netActive )
		return 0;
	CBasePlayer *pPlayer = luaL_checkplayer( L, 1 );
	g_netWrite.WriteShort( pPlayer ? pPlayer->entindex() : 0 );
	return 0;
}

static int net_BytesWritten( lua_State *L )
{
	lua_pushinteger( L, g_netActive ? g_netWrite.GetNumBytesWritten() : 0 );
	return 1;
}

//-----------------------------------------------------------------------------
// Purpose: common send to a recipient filter.
//
// GMod delivers net messages to the listen-server host locally - no network
// hop, nothing to drop - so even net.Start( name, true ) (unreliable)
// messages always arrive in singleplayer.  A plain CRecipientFilter is
// unreliable, and unreliable usermessages to the listen-server host get
// dropped on this branch.  Every Lua net send is therefore forced reliable;
// real-network unreliable semantics are the one reference behavior traded
// away (documented fork deviation).
//-----------------------------------------------------------------------------
static void SendNetMessage( CRecipientFilter &filter )
{
	if ( !g_netActive )
		return;		// reference behavior: sending without Start is ignored

	// reference behavior: an overflowed buffer is warned about every time and
	// the message still goes out (with the truncated content)
	if ( g_netWrite.IsOverflowed() )
	{
		luasrc_LuaWarnMsgF( "Trying to send an overflowed net message!\n" );
	}

	// The sv->cl leg rides a usermessage, whose payload is capped by
	// MAX_USER_MSG_DATA at the engine layer; refuse oversize instead of
	// letting the engine silently truncate mid-payload.
	int nPayloadBytes = ( g_netWrite.GetNumBitsWritten() + 7 ) >> 3;
	if ( nPayloadBytes > MAX_USER_MSG_DATA )
	{
		luasrc_LuaWarnMsgF( "[net] message '%s' (%i bytes) is larger than the %i byte sv->cl transport limit - dropped\n",
			g_netName.Get(), nPayloadBytes, (int)MAX_USER_MSG_DATA );
		g_netActive = false;
		g_netUnreliable = false;
		return;
	}

	filter.MakeReliable();

	UserMessageBegin( filter, "LuaNet" );
	// Reference behavior: Lua sees exactly ONE 16-bit pooled id per message -
	// net.Incoming reads it through net.ReadHeader and the receiver starts on
	// the payload.  net.Start already wrote that id into the buffer, so send
	// the buffer as-is; prepending it a second time shifts every sv->cl
	// payload by 16 bits and desyncs all receivers.
	MessageWriteBits( g_netWrite.GetData(), g_netWrite.GetNumBitsWritten() );
	MessageEnd();

	g_netActive = false;
	g_netUnreliable = false;
}

// net.Send( ply|table ) - GMod accepts a single player or a table of players.
static int net_Send( lua_State *L )
{
	CRecipientFilter filter;

	if ( lua_istable( L, 1 ) )
	{
		int n = luaL_len( L, 1 );
		for ( int i = 1; i <= n; i++ )
		{
			lua_rawgeti( L, 1, i );
			if ( !lua_isnil( L, -1 ) )
				filter.AddRecipient( luaL_checkplayer( L, -1 ) );
			lua_pop( L, 1 );
		}
	}
	else
	{
		filter.AddRecipient( luaL_checkplayer( L, 1 ) );
	}

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

// net.SendOmit( ply|entity|table ) - GMod: send to everyone EXCEPT the given
// recipient(s).  One filter, one send: g_netActive is consumed by
// SendNetMessage, so a Lua-side loop over net.Send would drop every message
// after the first.  Non-player entities are accepted and simply ignored when
// omitting (addons pass NPCs/props here - hitnumbers omits the damage target
// unconditionally).
static int net_SendOmit( lua_State *L )
{
	CRecipientFilter filter;

	for ( int i = 1; i <= gpGlobals->maxClients; i++ )
	{
		CBasePlayer *pPlayer = UTIL_PlayerByIndex( i );
		if ( pPlayer )
			filter.AddRecipient( pPlayer );
	}

	bool bIsTable = lua_istable( L, 1 ) != 0;
	int nCount = bIsTable ? (int)luaL_len( L, 1 ) : 1;

	for ( int i = 1; i <= nCount; i++ )
	{
		if ( bIsTable )
			lua_rawgeti( L, 1, i );

		if ( !lua_isnil( L, -1 ) )
		{
			CBaseEntity *pEnt = luaL_checkentity( L, -1 );
			if ( pEnt && pEnt->IsPlayer() )
				filter.RemoveRecipient( static_cast<CBasePlayer *>( pEnt ) );
		}

		if ( bIsTable )
			lua_pop( L, 1 );
	}

	SendNetMessage( filter );
	return 0;
}

//-----------------------------------------------------------------------------
// Server side of the client -> server transport.
//
// The engine hands each incoming clc_LuaNetMessage here (via
// IServerGameClients::ProcessLuaNetMessage).  Reference behavior: resolve
// nothing in C - just expose the payload through bf_read and call Lua's
// net.Incoming( length, player ); the default Lua implementation reads the
// 16-bit header and dispatches to net.Receivers.
//-----------------------------------------------------------------------------
void HL2SB_LuaNetMessageReceived( edict_t *pEdict, bf_read &msg )
{
	if ( !L || msg.IsOverflowed() )
		return;

	int nBits = msg.GetNumBitsLeft();

	CBasePlayer *pPlayer = NULL;
	int iEntIndex = pEdict ? ENTINDEX( pEdict ) : 0;
	if ( iEntIndex >= 1 && iEntIndex <= gpGlobals->maxClients )
		pPlayer = UTIL_PlayerByIndex( iEntIndex );

	Net_SetRead( &msg );

	lua_getglobal( L, "net" );
	if ( lua_istable( L, -1 ) )
	{
		lua_getfield( L, -1, "Incoming" );
		if ( lua_isfunction( L, -1 ) )
		{
			lua_pushnumber( L, (lua_Number)nBits );
			if ( pPlayer )
				lua_pushplayer( L, pPlayer );
			else
				lua_pushnil( L );
			luasrc_pcall( L, 2, 0, 0 );
		}
		else
		{
			lua_pop( L, 1 );
			// A dropped dispatch used to be silent; report it once so a dead
			// client->server action leaves a trace instead of nothing.
			static bool s_bReportedNoIncoming = false;
			if ( !s_bReportedNoIncoming )
			{
				s_bReportedNoIncoming = true;
				luasrc_LuaWarnMsgF( "LuaNetMessage: net.Incoming is not a function -- every Lua net message from clients is being dropped\n" );
			}
		}
	}
	lua_pop( L, 1 );

	Net_SetRead( NULL );
}

// net.Receive( name, func ) lives in Lua (net.lua); nothing to do here.

// net.ReadBit() -> int (0/1)
static int net_ReadBit( lua_State *L )
{
	bf_read *pRead = Net_GetRead();
	lua_pushinteger( L, pRead ? pRead->ReadOneBit() : 0 );
	return 1;
}

static int net_ReadInt( lua_State *L )
{
	int bits = luaL_optint( L, 1, 32 );
	bf_read *pRead = Net_GetRead();
	lua_pushinteger( L, pRead ? pRead->ReadSBitLong( bits ) : 0 );
	return 1;
}

static int net_ReadUInt( lua_State *L )
{
	int bits = luaL_optint( L, 1, 32 );
	bf_read *pRead = Net_GetRead();
	lua_pushinteger( L, pRead ? (int)pRead->ReadUBitLong( bits ) : 0 );
	return 1;
}

static int net_ReadString( lua_State *L )
{
	if ( !Net_GetRead() )
	{
		lua_pushstring( L, "" );
		return 1;
	}
	char szBuf[ 512 ];
	int iLen = sizeof( szBuf ) - 1;
	Net_GetRead()->ReadString( szBuf, iLen );
	lua_pushstring( L, szBuf );
	return 1;
}

static int net_ReadFloat( lua_State *L )
{
	bf_read *pRead = Net_GetRead();
	lua_pushnumber( L, pRead ? pRead->ReadFloat() : 0.0f );
	return 1;
}

static int net_ReadDouble( lua_State *L )
{
	bf_read *pRead = Net_GetRead();
	if ( !pRead ) return 0;
	double value = 0.0;
	pRead->ReadBits( &value, 64 );
	lua_pushnumber( L, value );
	return 1;
}

static int net_ReadMatrix( lua_State *L )
{
    bf_read *reader = Net_GetRead();
    if ( !reader ) return 0;
    VMatrix matrix;
    for ( int row = 0; row < 4; ++row )
        for ( int col = 0; col < 4; ++col )
            matrix[row][col] = reader->ReadFloat();
    lua_pushvmatrix( L, matrix );
    return 1;
}

static int net_ReadVector( lua_State *L )
{
	Vector v = vec3_origin;
	bf_read *pRead = Net_GetRead();
	if ( pRead )
		pRead->ReadBitVec3Coord( v );
	lua_pushvector( L, v );
	return 1;
}

static int net_ReadNormal( lua_State *L )
{
    bf_read *reader = Net_GetRead();
    if (!reader) return 0;
    Vector value = vec3_origin;
    reader->ReadBitVec3Normal(value);
    lua_pushvector(L, value);
    return 1;
}

static int net_ReadAngle( lua_State *L )
{
	QAngle a = vec3_angle;
	bf_read *pRead = Net_GetRead();
	if ( pRead )
		pRead->ReadBitAngles( a );
	lua_pushangle( L, a );
	return 1;
}

static int net_ReadEntity( lua_State *L )
{
	bf_read *pRead = Net_GetRead();
	int idx = pRead ? pRead->ReadShort() : 0;
	CBaseEntity *pEnt = idx ? UTIL_EntityByIndex( idx ) : NULL;
	lua_pushentity( L, pEnt );
	return 1;
}

// --- HL2SB GMod compat: wiki-checked members ---

static int net_ReadBool( lua_State *L )
{
	bf_read *pRead = Net_GetRead();
	lua_pushboolean( L, pRead ? pRead->ReadOneBit() != 0 : false );
	return 1;
}

// net.ReadData( length )
static int net_ReadData( lua_State *L )
{
	int nBytes = luaL_checkint( L, 1 );

	// reference behavior: lengths above 65535 raise a Lua error
	if ( nBytes <= 0 )
	{
		lua_pushstring( L, "" );
		return 1;
	}
	if ( nBytes > 0xffff )
		return luaL_error( L, "net.ReadData: Invalid length %i!", nBytes );

	bf_read *pRead = Net_GetRead();
	if ( !pRead )
	{
		lua_pushstring( L, "" );
		return 1;
	}

	static char s_pBuf[ 0xffff ];
	pRead->ReadBytes( s_pBuf, nBytes );
	lua_pushlstring( L, s_pBuf, nBytes );
	return 1;
}

static int net_ReadColor( lua_State *L )
{
	if ( !Net_GetRead() )
	{
		lua_pushnil( L );
		return 1;
	}
	int r = Net_GetRead()->ReadByte();
	int g = Net_GetRead()->ReadByte();
	int b = Net_GetRead()->ReadByte();
	int a = Net_GetRead()->ReadByte();
	lua_pushcolor( L, Color( r, g, b, a ) );
	return 1;
}

static int net_ReadTable( lua_State *L )
{
	char szBuf[ 4096 ];
	int iLen = sizeof( szBuf ) - 1;
	if ( Net_GetRead() )
	{
		Net_GetRead()->ReadString( szBuf, iLen );
		szBuf[ iLen ] = '\0';
	}
	else
	{
		szBuf[ 0 ] = '\0';
	}
	// reuse util.JSONToTable's decoder: it reads the JSON string from slot 1
	while ( lua_gettop( L ) < 1 ) lua_pushnil( L );
	lua_pushstring( L, szBuf );
	lua_replace( L, 1 );
	return luasrc_UTIL_JSONToTable( L );
}

static int net_ReadPlayer( lua_State *L )
{
	bf_read *pRead = Net_GetRead();
	int idx = pRead ? pRead->ReadShort() : 0;
	CBasePlayer *pPlayer = idx ? UTIL_PlayerByIndex( idx ) : NULL;
	if ( pPlayer )
		lua_pushplayer( L, pPlayer );
	else
		lua_pushnil( L );
	return 1;
}

static int net_BytesLeft( lua_State *L )
{
	bf_read *pRead = Net_GetRead();
	lua_pushinteger( L, pRead ? pRead->GetNumBytesLeft() : 0 );
	return 1;
}

// net.Abort() - discard the in-progress message
static int net_Abort( lua_State *L )
{
	g_netActive = false;
	g_netUnreliable = false;
	return 0;
}

static const luaL_Reg net_funcs[] = {
	{ "Start",        net_Start },
	{ "WriteBit",     net_WriteBit },
	{ "WriteInt",     net_WriteInt },
	{ "WriteUInt",    net_WriteUInt },
	{ "WriteString",  net_WriteString },
	{ "WriteFloat",   net_WriteFloat },
	{ "WriteDouble",  net_WriteDouble },
	{ "WriteMatrix",  net_WriteMatrix },
	{ "WriteVector",  net_WriteVector },
	{ "WriteNormal",  net_WriteNormal },
	{ "WriteAngle",   net_WriteAngle },
	{ "WriteEntity",  net_WriteEntity },
	{ "WriteBool",    net_WriteBool },
	{ "ReadBool",     net_ReadBool },
	{ "WriteData",    net_WriteData },
	{ "ReadData",     net_ReadData },
	{ "WriteColor",   net_WriteColor },
	{ "ReadColor",    net_ReadColor },
	{ "WriteTable",   net_WriteTable },
	{ "ReadTable",    net_ReadTable },
	{ "WritePlayer",  net_WritePlayer },
	{ "ReadPlayer",   net_ReadPlayer },
	{ "BytesWritten", net_BytesWritten },
	{ "BytesLeft",    net_BytesLeft },
	{ "Abort",        net_Abort },
	{ "Send",         net_Send },
	{ "Broadcast",    net_Broadcast },
	{ "SendOmit",     net_SendOmit },
	{ "ReadHeader",   net_ReadHeader },
	{ "ReadBit",      net_ReadBit },
	{ "ReadInt",      net_ReadInt },
	{ "ReadUInt",     net_ReadUInt },
	{ "ReadString",   net_ReadString },
	{ "ReadFloat",    net_ReadFloat },
	{ "ReadDouble",   net_ReadDouble },
	{ "ReadMatrix",   net_ReadMatrix },
	{ "ReadVector",   net_ReadVector },
	{ "ReadNormal",   net_ReadNormal },
	{ "ReadAngle",    net_ReadAngle },
	{ "ReadEntity",   net_ReadEntity },
	{ NULL, NULL }
};

LUALIB_API int luaopen_net( lua_State *L )
{
	HL2SB_RegisterNetConstants( L );
	// net.Receive / net.Receivers / net.Incoming are created by the Lua
	// extension (includes/extensions/net.lua), like the reference library.
	luaL_register( L, "net", net_funcs );
	return 1;
}

#endif // CLIENT_DLL

//-----------------------------------------------------------------------------
// HL2SB: drop the read state on a Lua state reset.
//
// The dispatch window around luasrc_shutdown()/luasrc_init() simply has no
// readers: nothing is dispatched until the new level's scripts hook in again
// (see the history in luamanager.cpp for why transitional dispatches must not
// touch a half-torn-down state).
//-----------------------------------------------------------------------------
LUA_API void luasrc_net_reset( void )
{
	Net_SetRead( NULL );
#ifndef CLIENT_DLL
	s_UserMessageActive = false;
	s_UserMessageRecipients.Reset();
#endif
}
