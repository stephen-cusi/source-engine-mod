//===== Copyright © 1996-2005, Valve Corporation, All rights reserved. ======//
//
// Purpose: 
//
// $NoKeywords: $
//===========================================================================//

#define lfilesystem_cpp

#include "cbase.h"
#include "filesystem.h"
#include "luamanager.h"
#include "luasrclib.h"
#include "lfilesystem.h"
#include "tier1/utlvector.h"

// memdbgon must be the last include file in a .cpp file!!!
#include "tier0/memdbgon.h"



/*
** access functions (stack -> C)
*/


LUA_API lua_FileHandle_t &lua_tofilehandle (lua_State *L, int idx) {
  lua_FileHandle_t *phFile = (lua_FileHandle_t *)luaL_checkudata(L, idx, "FileHandle_t");
  return *phFile;
}



/*
** push functions (C -> stack)
*/


LUA_API void lua_pushfilehandle (lua_State *L, lua_FileHandle_t hFile) {
  lua_FileHandle_t *phFile = (lua_FileHandle_t *)lua_newuserdata(L, sizeof(lua_FileHandle_t));
  *phFile = hFile;
  luaL_getmetatable(L, "FileHandle_t");
  lua_setmetatable(L, -2);
}


LUALIB_API lua_FileHandle_t &luaL_checkfilehandle (lua_State *L, int narg) {
  lua_FileHandle_t *d = (lua_FileHandle_t *)luaL_checkudata(L, narg, "FileHandle_t");
  if (*d == FILESYSTEM_INVALID_HANDLE)  /* avoid extra test when d is not 0 */
    luaL_argerror(L, narg, "FileHandle_t expected, got FILESYSTEM_INVALID_HANDLE");
  return *d;
}


static int filesystem_AddPackFile (lua_State *L) {
  lua_pushboolean(L, filesystem->AddPackFile(luaL_checkstring(L, 1), luaL_checkstring(L, 2)));
  return 1;
}

static int filesystem_AddSearchPath (lua_State *L) {
  char fullpath[ 512 ] = { 0 };
  bool bGetCurrentDirectory = V_GetCurrentDirectory( fullpath, sizeof( fullpath ) );
  if ( bGetCurrentDirectory )
  {
#ifdef CLIENT_DLL
    const char *gamePath = engine->GetGameDirectory();
#else
    char gamePath[ 256 ];
    engine->GetGameDir( gamePath, 256 );
#endif
    V_SetCurrentDirectory( gamePath );
  }
  filesystem->AddSearchPath(luaL_checkstring(L, 1), luaL_checkstring(L, 2), (SearchPathAdd_t)luaL_optint(L, 3, PATH_ADD_TO_TAIL));
  if ( bGetCurrentDirectory )
	  V_SetCurrentDirectory( fullpath );
  return 0;
}

static int filesystem_BeginMapAccess (lua_State *L) {
  filesystem->BeginMapAccess();
  return 0;
}

static int filesystem_CancelWaitForResources (lua_State *L) {
  filesystem->CancelWaitForResources(luaL_checkint(L, 1));
  return 0;
}

static int filesystem_Close (lua_State *L) {
  filesystem->Close(luaL_checkfilehandle(L, 1));
  // Andrew; this isn't standard behavior or usage, but we do this for the sake
  // of things being safe in Lua
  luaL_checkfilehandle(L, 1) = FILESYSTEM_INVALID_HANDLE;
  return 0;
}

static int filesystem_CreateDirHierarchy (lua_State *L) {
  filesystem->CreateDirHierarchy(luaL_checkstring(L, 1), luaL_optstring(L, 2, 0));
  return 0;
}

static int filesystem_DiscardPreloadData (lua_State *L) {
  filesystem->DiscardPreloadData();
  return 0;
}

static int filesystem_Disconnect (lua_State *L) {
  filesystem->Disconnect();
  return 0;
}

static int filesystem_EnableWhitelistFileTracking (lua_State *L) {
  filesystem->EnableWhitelistFileTracking(luaL_checkboolean(L, 1), luaL_checkboolean(L, 2), luaL_checkboolean(L, 3));
  return 0;
}

static int filesystem_EndMapAccess (lua_State *L) {
  filesystem->EndMapAccess();
  return 0;
}

static int filesystem_EndOfFile (lua_State *L) {
  lua_pushboolean(L, filesystem->EndOfFile(luaL_checkfilehandle(L, 1)));
  return 1;
}

static int filesystem_FileExists (lua_State *L) {
  lua_pushboolean(L, filesystem->FileExists(luaL_checkstring(L, 1), luaL_optstring(L, 2, 0)));
  return 1;
}

static int filesystem_Flush (lua_State *L) {
  filesystem->Flush(luaL_checkfilehandle(L, 1));
  return 0;
}

static int filesystem_GetDVDMode (lua_State *L) {
  lua_pushinteger(L, filesystem->GetDVDMode());
  return 1;
}

static int filesystem_GetLocalCopy (lua_State *L) {
  filesystem->GetLocalCopy(luaL_checkstring(L, 1));
  return 0;
}

static int filesystem_GetWhitelistSpewFlags (lua_State *L) {
  lua_pushinteger(L, filesystem->GetWhitelistSpewFlags());
  return 1;
}

static int filesystem_HintResourceNeed (lua_State *L) {
  lua_pushinteger(L, filesystem->HintResourceNeed(luaL_checkstring(L, 1), luaL_checkint(L, 2)));
  return 1;
}

static int filesystem_IsDirectory (lua_State *L) {
  lua_pushboolean(L, filesystem->IsDirectory(luaL_checkstring(L, 1), luaL_optstring(L, 2, 0)));
  return 1;
}

static int filesystem_IsFileImmediatelyAvailable (lua_State *L) {
  lua_pushboolean(L, filesystem->IsFileImmediatelyAvailable(luaL_checkstring(L, 1)));
  return 1;
}

static int filesystem_IsFileWritable (lua_State *L) {
  lua_pushboolean(L, filesystem->IsFileWritable(luaL_checkstring(L, 1), luaL_optstring(L, 2, 0)));
  return 1;
}

static int filesystem_IsOk (lua_State *L) {
  lua_pushboolean(L, filesystem->IsOk(luaL_checkfilehandle(L, 1)));
  return 1;
}

static int filesystem_IsSteam (lua_State *L) {
  lua_pushboolean(L, filesystem->IsSteam());
  return 1;
}

static int filesystem_LoadCompiledKeyValues (lua_State *L) {
  filesystem->LoadCompiledKeyValues((IFileSystem::KeyValuesPreloadType_t)luaL_checkint(L, 1), luaL_checkstring(L, 2));
  return 0;
}

static int filesystem_MarkAllCRCsUnverified (lua_State *L) {
  filesystem->MarkAllCRCsUnverified();
  return 0;
}

static int filesystem_MarkPathIDByRequestOnly (lua_State *L) {
  filesystem->MarkPathIDByRequestOnly(luaL_checkstring(L, 1), luaL_checkboolean(L, 2));
  return 0;
}

static int filesystem_MountSteamContent (lua_State *L) {
  lua_pushinteger(L, filesystem->MountSteamContent(luaL_optint(L, 1, -1)));
  return 1;
}

static int filesystem_Open (lua_State *L) {
  lua_pushfilehandle(L, filesystem->Open(luaL_checkstring(L, 1), luaL_checkstring(L, 2), luaL_optstring(L, 3, 0)));
  return 1;
}

static int filesystem_Precache (lua_State *L) {
  lua_pushboolean(L, filesystem->Precache(luaL_checkstring(L, 1), luaL_optstring(L, 2, 0)));
  return 1;
}

static int filesystem_PrintOpenedFiles (lua_State *L) {
  filesystem->PrintOpenedFiles();
  return 0;
}

static int filesystem_PrintSearchPaths (lua_State *L) {
  filesystem->PrintSearchPaths();
  return 0;
}

static int filesystem_Read (lua_State *L) {
  byte *buffer;

  FileHandle_t file;
  file = luaL_checkfilehandle(L, 2);

  int size = luaL_checkint(L, 1);
  buffer = new byte[ size + 1 ];
  if ( !buffer )
  {
  	Warning( "filesystem.Read:  Couldn't allocate buffer of size %i for file\n", size + 1 );
  	lua_pushinteger(L, -1);
  	lua_pushstring(L, NULL);
  	return 2;
  }
  int bytesRead = filesystem->Read( buffer, size, file );

  if ( bytesRead )
  {
	  // Ensure null terminator
	  buffer[ bytesRead ] =0;
  }
  else
  {
	  *buffer = 0;
  }

  lua_pushinteger(L, size);
  lua_pushstring(L, (const char *)buffer);
  delete buffer;

  return 2;
}

static int filesystem_RemoveAllSearchPaths (lua_State *L) {
  filesystem->RemoveAllSearchPaths();
  return 0;
}

static int filesystem_RemoveFile (lua_State *L) {
  filesystem->RemoveFile(luaL_checkstring(L, 1), luaL_optstring(L, 2, 0));
  return 0;
}

static int filesystem_RemoveSearchPath (lua_State *L) {
  lua_pushboolean(L, filesystem->RemoveSearchPath(luaL_checkstring(L, 1), luaL_optstring(L, 2, 0)));
  return 1;
}

static int filesystem_RemoveSearchPaths (lua_State *L) {
  filesystem->RemoveSearchPaths(luaL_checkstring(L, 1));
  return 0;
}

static int filesystem_RenameFile (lua_State *L) {
  lua_pushboolean(L, filesystem->RenameFile(luaL_checkstring(L, 1), luaL_checkstring(L, 2), luaL_optstring(L, 3, 0)));
  return 1;
}

static int filesystem_SetFileWritable (lua_State *L) {
  lua_pushboolean(L, filesystem->SetFileWritable(luaL_checkstring(L, 1), luaL_checkboolean(L, 2), luaL_optstring(L, 3, 0)));
  return 1;
}

static int filesystem_SetupPreloadData (lua_State *L) {
  filesystem->SetupPreloadData();
  return 0;
}

static int filesystem_SetWarningLevel (lua_State *L) {
  filesystem->SetWarningLevel((FileWarningLevel_t)luaL_checkint(L, 1));
  return 0;
}

static int filesystem_SetWhitelistSpewFlags (lua_State *L) {
  filesystem->SetWhitelistSpewFlags(luaL_checkint(L, 1));
  return 0;
}

static int filesystem_Shutdown (lua_State *L) {
  filesystem->Shutdown();
  return 0;
}

static int filesystem_Size (lua_State *L) {
  switch(lua_type(L, 1)) {
    case LUA_TSTRING:
      lua_pushinteger(L, filesystem->Size(luaL_checkstring(L, 1), luaL_optstring(L, 2, 0)));
      break;
    case LUA_TUSERDATA:
    default:
      lua_pushinteger(L, filesystem->Size(luaL_checkfilehandle(L, 1)));
      break;
  }
  return 1;
}

static int filesystem_UnzipFile (lua_State *L) {
  lua_pushboolean(L, filesystem->UnzipFile(luaL_checkstring(L, 1), luaL_checkstring(L, 2), luaL_checkstring(L, 3)));
  return 1;
}

static int filesystem_WaitForResources (lua_State *L) {
  lua_pushinteger(L, filesystem->WaitForResources(luaL_checkstring(L, 1)));
  return 1;
}

static int filesystem_Write (lua_State *L) {
  size_t l;
  const char *pInput = luaL_checklstring(L, 1, &l);
  lua_pushinteger(L, filesystem->Write(pInput, l, luaL_checkfilehandle(L, 2)));
  return 1;
}


static const luaL_Reg filesystemlib[] = {
  {"AddPackFile",   filesystem_AddPackFile},
  {"AddSearchPath",   filesystem_AddSearchPath},
  {"BeginMapAccess",   filesystem_BeginMapAccess},
  {"CancelWaitForResources",   filesystem_CancelWaitForResources},
  {"Close",   filesystem_Close},
  {"CreateDirHierarchy",   filesystem_CreateDirHierarchy},
  {"DiscardPreloadData",   filesystem_DiscardPreloadData},
  {"Disconnect",   filesystem_Disconnect},
  {"EnableWhitelistFileTracking",   filesystem_EnableWhitelistFileTracking},
  {"EndMapAccess",   filesystem_EndMapAccess},
  {"EndOfFile",   filesystem_EndOfFile},
  {"FileExists",   filesystem_FileExists},
  {"Flush",   filesystem_Flush},
  {"GetDVDMode",   filesystem_GetDVDMode},
  {"GetLocalCopy",   filesystem_GetLocalCopy},
  {"GetWhitelistSpewFlags",   filesystem_GetWhitelistSpewFlags},
  {"HintResourceNeed",   filesystem_HintResourceNeed},
  {"IsDirectory",   filesystem_IsDirectory},
  {"IsFileImmediatelyAvailable",   filesystem_IsFileImmediatelyAvailable},
  {"IsFileWritable",   filesystem_IsFileWritable},
  {"IsOk",   filesystem_IsOk},
  {"IsSteam",   filesystem_IsSteam},
  {"LoadCompiledKeyValues",   filesystem_LoadCompiledKeyValues},
  {"MarkAllCRCsUnverified",   filesystem_MarkAllCRCsUnverified},
  {"MarkPathIDByRequestOnly",   filesystem_MarkPathIDByRequestOnly},
  {"MountSteamContent",   filesystem_MountSteamContent},
  {"Open",   filesystem_Open},
  {"Precache",   filesystem_Precache},
  {"PrintOpenedFiles",   filesystem_PrintOpenedFiles},
  {"PrintSearchPaths",   filesystem_PrintSearchPaths},
  {"Read",   filesystem_Read},
  {"RemoveAllSearchPaths",   filesystem_RemoveAllSearchPaths},
  {"RemoveFile",   filesystem_RemoveFile},
  {"RemoveSearchPath",   filesystem_RemoveSearchPath},
  {"RemoveSearchPaths",   filesystem_RemoveSearchPaths},
  {"RenameFile",   filesystem_RenameFile},
  {"SetFileWritable",   filesystem_SetFileWritable},
  {"SetupPreloadData",   filesystem_SetupPreloadData},
  {"SetWarningLevel",   filesystem_SetWarningLevel},
  {"SetWhitelistSpewFlags",   filesystem_SetWhitelistSpewFlags},
  {"Size",   filesystem_Size},
  {"UnzipFile",   filesystem_UnzipFile},
  {"WaitForResources",   filesystem_WaitForResources},
  {"Write",   filesystem_Write},
  {NULL, NULL}
};


static int FileHandle_t___gc (lua_State *L) {
  FileHandle_t hFile = lua_tofilehandle(L, 1);
  if (hFile != FILESYSTEM_INVALID_HANDLE)
    filesystem->Close(hFile);
  return 0;
}

static int FileHandle_t___tostring (lua_State *L) {
  FileHandle_t hFile = lua_tofilehandle(L, 1);
  if (hFile == FILESYSTEM_INVALID_HANDLE)
    lua_pushstring(L, "FILESYSTEM_INVALID_HANDLE");
  else
    lua_pushfstring(L, "FileHandle_t: %p", lua_tofilehandle(L, 1));
  return 1;
}


/*
** HL2SB: GMod's File object methods.
**
** lua/includes/extensions/file.lua (Garry's Mod's own file extension, shipped
** here verbatim) rebuilds file.Read / file.Write on top of file.Open, and then
** does `local str = f:Read( f:Size() )` / `f:Close()`.  The FileHandle_t
** metatable used to carry nothing but __gc and __tostring, so loading that
** extension failed on both realms:
**
**   [Lua] FAILED lua/includes/extensions/player_auth.lua:
**         file.lua:10: attempt to call a nil value (method 'Size')
*/
static int FileHandle_Close (lua_State *L) {
  FileHandle_t &hFile = luaL_checkfilehandle(L, 1);

  if (hFile != FILESYSTEM_INVALID_HANDLE) {
    filesystem->Close(hFile);
    hFile = FILESYSTEM_INVALID_HANDLE;
  }

  return 0;
}

static int FileHandle_EndOfFile (lua_State *L) {
  lua_pushboolean(L, filesystem->EndOfFile(luaL_checkfilehandle(L, 1)));
  return 1;
}

static int FileHandle_Flush (lua_State *L) {
  filesystem->Flush(luaL_checkfilehandle(L, 1));
  return 0;
}

// f:Read( [bytes] ) -> string  (the rest of the file by default)
static int FileHandle_Read (lua_State *L) {
  FileHandle_t hFile = luaL_checkfilehandle(L, 1);
  int nBytes = luaL_optint(L, 2, filesystem->Size(hFile));

  if (nBytes <= 0) {
    lua_pushstring(L, "");
    return 1;
  }

  byte *pBuffer = new byte[nBytes + 1];
  int nRead = filesystem->Read(pBuffer, nBytes, hFile);

  if (nRead <= 0) {
    delete[] pBuffer;
    lua_pushstring(L, "");
    return 1;
  }

  pBuffer[nRead] = 0;
  lua_pushlstring(L, (const char *)pBuffer, nRead);
  delete[] pBuffer;
  return 1;
}

// f:Seek( offset, whence ) -> number.  GMod's whence is a string ("set"/"cur"/
// "end"); the plain 0/1/2 numbers C uses are accepted too.
static int FileHandle_Seek (lua_State *L) {
  FileHandle_t hFile = luaL_checkfilehandle(L, 1);
  int nOffset = luaL_checkint(L, 2);
  FileSystemSeek_t nWhence = FILESYSTEM_SEEK_HEAD;

  if (lua_type(L, 3) == LUA_TSTRING) {
    const char *pWhence = lua_tostring(L, 3);

    if (!V_stricmp(pWhence, "cur") || !V_stricmp(pWhence, "current"))
      nWhence = FILESYSTEM_SEEK_CURRENT;
    else if (!V_stricmp(pWhence, "end") || !V_stricmp(pWhence, "tail"))
      nWhence = FILESYSTEM_SEEK_TAIL;
  } else if (lua_isnumber(L, 3)) {
    int nWhence = lua_tointeger(L, 3);
    nWhence = (nWhence == 1) ? FILESYSTEM_SEEK_CURRENT : (nWhence == 2) ? FILESYSTEM_SEEK_TAIL : FILESYSTEM_SEEK_HEAD;
  }

  // IFileSystem::Seek returns void in this fork, so report the new position the
  // way GMod's File:Seek does.
  filesystem->Seek(hFile, nOffset, nWhence);
  lua_pushinteger(L, filesystem->Tell(hFile));
  return 1;
}

static int FileHandle_Size (lua_State *L) {
  lua_pushinteger(L, filesystem->Size(luaL_checkfilehandle(L, 1)));
  return 1;
}

static int FileHandle_Tell (lua_State *L) {
  lua_pushinteger(L, filesystem->Tell(luaL_checkfilehandle(L, 1)));
  return 1;
}

static int FileHandle_Write (lua_State *L) {
  size_t nLength = 0;
  const char *pData = luaL_checklstring(L, 2, &nLength);

  // IFileSystem::Write returns void in this fork; report what we handed it.
  filesystem->Write(pData, (int)nLength, luaL_checkfilehandle(L, 1));
  lua_pushinteger(L, (lua_Integer)nLength);
  return 1;
}

/*
** HL2SB: the binary accessors of GMod's File object
** (https://wiki.facepunch.com/gmod/file_class).
**
** file.Open hands back this metatable and a whole ecosystem of addons leans on
** it -- save systems write floats/longs, mapcycle-style parsers call ReadLine,
** SteamID64 tooling calls ReadUInt64.  The metatable used to stop at
** Read/Write/Seek/Size/Tell/Close/Flush/EndOfFile, so every one of those addons
** died on "attempt to call a nil value (method 'ReadLong')".
**
** All multi-byte values are little-endian (the wiki pins GMod's default
** endianness) and short reads at end-of-file answer zero.
*/

static bool FileHandle_ReadBytes ( lua_State *L, void *pOut, int nBytes )
{
  FileHandle_t hFile = luaL_checkfilehandle(L, 1);
  return filesystem->Read(pOut, nBytes, hFile) == nBytes;
}

// File:ReadBool() -> boolean.  One byte, true when non-zero.
static int FileHandle_ReadBool (lua_State *L) {
  byte b = 0;
  FileHandle_ReadBytes(L, &b, 1);
  lua_pushboolean(L, b != 0);
  return 1;
}

// File:ReadByte() -> number.  Unsigned 8-bit.
static int FileHandle_ReadByte (lua_State *L) {
  byte b = 0;
  FileHandle_ReadBytes(L, &b, 1);
  lua_pushinteger(L, b);
  return 1;
}

static unsigned short FileHandle_ReadLE16 (lua_State *L) {
  byte b[2] = { 0, 0 };
  FileHandle_ReadBytes(L, b, 2);
  return (unsigned short)( b[0] | ( b[1] << 8 ) );
}

static unsigned int FileHandle_ReadLE32 (lua_State *L) {
  byte b[4] = { 0, 0, 0, 0 };
  FileHandle_ReadBytes(L, b, 4);
  return (unsigned int)b[0] | ( (unsigned int)b[1] << 8 ) |
         ( (unsigned int)b[2] << 16 ) | ( (unsigned int)b[3] << 24 );
}

// File:ReadShort() -> number.  Signed little-endian 16-bit.
static int FileHandle_ReadShort (lua_State *L) {
  lua_pushinteger(L, (short)FileHandle_ReadLE16(L));
  return 1;
}

// File:ReadUShort() -> number.  Unsigned little-endian 16-bit.
static int FileHandle_ReadUShort (lua_State *L) {
  lua_pushinteger(L, FileHandle_ReadLE16(L));
  return 1;
}

// File:ReadLong() -> number.  Signed little-endian 32-bit.
static int FileHandle_ReadLong (lua_State *L) {
  lua_pushinteger(L, (int)FileHandle_ReadLE32(L));
  return 1;
}

// File:ReadULong() -> number.  Unsigned little-endian 32-bit; pushed as a
// double so values above 2^31 keep their magnitude.
static int FileHandle_ReadULong (lua_State *L) {
  lua_pushnumber(L, (double)FileHandle_ReadLE32(L));
  return 1;
}

// File:ReadFloat() -> number.  4-byte IEEE-754 little-endian float; the host
// is little-endian, so the raw bytes copy straight into the value.
static int FileHandle_ReadFloat (lua_State *L) {
  byte b[4] = { 0, 0, 0, 0 };
  FileHandle_ReadBytes(L, b, 4);
  float f = 0.0f;
  memcpy(&f, b, 4);
  lua_pushnumber(L, (double)f);
  return 1;
}

// File:ReadDouble() -> number.  8-byte IEEE-754 little-endian double.
static int FileHandle_ReadDouble (lua_State *L) {
  byte b[8] = { 0 };
  FileHandle_ReadBytes(L, b, 8);
  double d = 0.0;
  memcpy(&d, b, 8);
  lua_pushnumber(L, d);
  return 1;
}

// File:ReadUInt64() -> string.  Lua numbers cannot hold a full 64-bit range
// (wiki: "mainly aimed at usage with Player:SteamID64"), so the answer is the
// decimal spelling of the unsigned 64-bit value.
static int FileHandle_ReadUInt64 (lua_State *L) {
  byte b[8] = { 0 };
  FileHandle_ReadBytes(L, b, 8);
  unsigned long long v = 0;
  for ( int i = 7; i >= 0; i-- )
    v = ( v << 8 ) | b[i];
  char szBuf[32];
  Q_snprintf(szBuf, sizeof(szBuf), "%llu", v);
  lua_pushstring(L, szBuf);
  return 1;
}

// File:ReadLine() -> string.  Reads up to and including the '\n'; '\r'
// characters are skipped entirely, a '\0' ends the line but still yields a
// trailing newline, and the result never exceeds 8192 characters.
static int FileHandle_ReadLine (lua_State *L) {
  FileHandle_t hFile = luaL_checkfilehandle(L, 1);

  char szLine[8192];
  int nLen = 0;

  while (nLen < (int)sizeof(szLine)) {
    char c = 0;
    if (filesystem->Read(&c, 1, hFile) != 1)
      break;  // end of file

    if (c == '\r')
      continue;

    // wiki: "will stop at a \0 or NULL character, but will add a new line
    // instead" -- the NUL never reaches the buffer, its newline does.
    if (c == '\0')
      c = '\n';

    szLine[nLen++] = c;
    if (c == '\n')
      break;
  }

  lua_pushlstring(L, szLine, nLen);
  return 1;
}

// File:Skip( amount ) -> amount.  Moves the pointer relative to the current
// position (negative skips backwards); the wiki documents the return as the
// amount itself.
static int FileHandle_Skip (lua_State *L) {
  FileHandle_t hFile = luaL_checkfilehandle(L, 1);
  int nAmount = luaL_checkint(L, 2);
  filesystem->Seek(hFile, nAmount, FILESYSTEM_SEEK_CURRENT);
  lua_pushinteger(L, nAmount);
  return 1;
}

static void FileHandle_WriteBytes (lua_State *L, const void *pData, int nBytes) {
  filesystem->Write(pData, nBytes, luaL_checkfilehandle(L, 1));
}

static void FileHandle_WriteLE16 (lua_State *L, unsigned short v) {
  byte b[2] = { (byte)( v & 0xFF ), (byte)( ( v >> 8 ) & 0xFF ) };
  FileHandle_WriteBytes(L, b, 2);
}

static void FileHandle_WriteLE32 (lua_State *L, unsigned int v) {
  byte b[4] = { (byte)( v & 0xFF ), (byte)( ( v >> 8 ) & 0xFF ),
                (byte)( ( v >> 16 ) & 0xFF ), (byte)( ( v >> 24 ) & 0xFF ) };
  FileHandle_WriteBytes(L, b, 4);
}

// File:WriteBool( bool ).  One byte, 1 or 0.
static int FileHandle_WriteBool (lua_State *L) {
  byte b = (lua_toboolean(L, 2) != 0) ? 1 : 0;
  FileHandle_WriteBytes(L, &b, 1);
  return 0;
}

// File:WriteByte( uint8 ).
static int FileHandle_WriteByte (lua_State *L) {
  byte b = (byte)((unsigned int)luaL_checknumber(L, 2) & 0xFFu);
  FileHandle_WriteBytes(L, &b, 1);
  return 0;
}

// File:WriteShort( int16 ).
static int FileHandle_WriteShort (lua_State *L) {
  FileHandle_WriteLE16(L, (unsigned short)(short)luaL_checknumber(L, 2));
  return 0;
}

// File:WriteUShort( uint16 ).
static int FileHandle_WriteUShort (lua_State *L) {
  FileHandle_WriteLE16(L, (unsigned short)(unsigned int)luaL_checknumber(L, 2));
  return 0;
}

// File:WriteLong( int32 ).
static int FileHandle_WriteLong (lua_State *L) {
  FileHandle_WriteLE32(L, (unsigned int)(int)luaL_checknumber(L, 2));
  return 0;
}

// File:WriteULong( uint32 ).
static int FileHandle_WriteULong (lua_State *L) {
  FileHandle_WriteLE32(L, (unsigned int)(unsigned long long)luaL_checknumber(L, 2));
  return 0;
}

// File:WriteFloat( float ).  4-byte IEEE-754 little-endian.
static int FileHandle_WriteFloat (lua_State *L) {
  float f = (float)luaL_checknumber(L, 2);
  byte b[4];
  memcpy(b, &f, 4);
  FileHandle_WriteBytes(L, b, 4);
  return 0;
}

// File:WriteDouble( double ).  8-byte IEEE-754 little-endian.
static int FileHandle_WriteDouble (lua_State *L) {
  double d = luaL_checknumber(L, 2);
  byte b[8];
  memcpy(b, &d, 8);
  FileHandle_WriteBytes(L, b, 8);
  return 0;
}

// File:WriteUInt64( string uint64 ).  The value arrives as its decimal
// spelling (the mirror of ReadUInt64); numbers are accepted too.
static int FileHandle_WriteUInt64 (lua_State *L) {
  const char *pszValue = luaL_checkstring(L, 2);
  unsigned long long v = V_strtoui64(pszValue, NULL, 10);
  byte b[8];
  for ( int i = 0; i < 8; i++ )
    b[i] = (byte)( ( v >> ( 8 * i ) ) & 0xFF );
  FileHandle_WriteBytes(L, b, 8);
  return 0;
}

static const luaL_Reg FileHandle_tmeta[] = {
  {"Close", FileHandle_Close},
  {"EndOfFile", FileHandle_EndOfFile},
  {"Flush", FileHandle_Flush},
  {"Read", FileHandle_Read},
  {"ReadBool", FileHandle_ReadBool},
  {"ReadByte", FileHandle_ReadByte},
  {"ReadDouble", FileHandle_ReadDouble},
  {"ReadFloat", FileHandle_ReadFloat},
  {"ReadLine", FileHandle_ReadLine},
  {"ReadLong", FileHandle_ReadLong},
  {"ReadShort", FileHandle_ReadShort},
  {"ReadUInt64", FileHandle_ReadUInt64},
  {"ReadULong", FileHandle_ReadULong},
  {"ReadUShort", FileHandle_ReadUShort},
  {"Seek", FileHandle_Seek},
  {"Size", FileHandle_Size},
  {"Skip", FileHandle_Skip},
  {"Tell", FileHandle_Tell},
  {"Write", FileHandle_Write},
  {"WriteBool", FileHandle_WriteBool},
  {"WriteByte", FileHandle_WriteByte},
  {"WriteDouble", FileHandle_WriteDouble},
  {"WriteFloat", FileHandle_WriteFloat},
  {"WriteLong", FileHandle_WriteLong},
  {"WriteShort", FileHandle_WriteShort},
  {"WriteUInt64", FileHandle_WriteUInt64},
  {"WriteULong", FileHandle_WriteULong},
  {"WriteUShort", FileHandle_WriteUShort},
  {"__gc", FileHandle_t___gc},
  {"__tostring", FileHandle_t___tostring},
  {NULL, NULL}
};


/*
** The FileHandle_t metatable -- f:Read / f:Write / f:Close / f:Seek / ...
**
** Both libraries below hand out those handles, so each of them needs the
** metatable to exist first.  It used to be created only by luaopen_filesystem,
** which is fine in the game realms (both libraries are opened there) but not in
** the main menu state, whose library list is hand-picked and opens `file` only
** -- file.Open there would hand back a userdata with no methods.
**
** Calling it twice is harmless: luaL_newmetatable leaves an existing metatable
** alone, and the fields below are re-set to the same values.
*/
static void HL2SB_RegisterFileHandleMeta (lua_State *L) {
  luaL_newmetatable(L, "FileHandle_t");
  luaL_register(L, NULL, FileHandle_tmeta);
  lua_pushvalue(L, -1);  /* push metatable */
  lua_setfield(L, -2, "__index");  /* metatable.__index = metatable */
  lua_pushstring(L, "filehandle");
  lua_setfield(L, -2, "__type");  /* metatable.__type = "filehandle" */
  lua_pop(L, 1);
}

/*
** Open filesystem library
*/
LUALIB_API int luaopen_filesystem (lua_State *L) {
  HL2SB_RegisterFileHandleMeta(L);
  lua_pushfilehandle(L, FILESYSTEM_INVALID_HANDLE);
  lua_setglobal(L, "FILESYSTEM_INVALID_HANDLE");
  luaL_register(L, LUA_FILESYSTEMLIBNAME, filesystemlib);

  // HL2SB: filesystem.AddSearchPath's third argument.  These were never
  // published, so every caller that passed PATH_ADD_TO_HEAD was handing the
  // binding a nil global and silently getting the default (PATH_ADD_TO_TAIL) --
  // the mount happened, just at the wrong priority.  lua/autorun/mount_games.lua
  // is the caller that matters: an extra game mounted at the tail loses to the
  // base content it is supposed to override.
  LUA_SET_ENUM_LIB_BEGIN( L, "FILESYSTEM" );
  lua_pushenum( L, PATH_ADD_TO_HEAD, "PATH_ADD_TO_HEAD" );
  lua_pushenum( L, PATH_ADD_TO_TAIL, "PATH_ADD_TO_TAIL" );
  LUA_SET_ENUM_LIB_END( L );

  return 1;
}


//=============================================================================
// HL2SB: GMod's file library -- luaopen_Files
//
// luaopen_Files was DECLARED in luasrclib.h:217 and LUA_FILESLIBNAME is "Files",
// but the registration in lsrcinit.cpp was commented out and there was no
// implementation anywhere, so the lib alias at lsrcinit.cpp:520
// ("file" -> LUA_FILESLIBNAME) silently skipped and every GMod file that uses it
// failed:
//
//     [Lua] FAILED lua/includes/extensions/file.lua:2: attempt to index a nil value (global 'file')
//     [Lua] FAILED lua/includes/extensions/player_auth.lua:77: ... (global 'file')
//
// Lives in lfilesystem.cpp on purpose: that file is already listed in BOTH
// client_lua.vpc and server_lua.vpc, so neither .vpc needs touching and both
// realms get it.
//
// Names are relative to the game folder, the way GMod's file.* are relative to
// garrysmod/.  The path argument of every binding below is a GMod path ID and is
// translated by the resolvers in this block -- it used to be ignored outright
// (everything was hardcoded to "MOD"), which is why file.Write never reached the
// data/ folder GMod writes to and file.Open( .., "DATA" ) wrote to the mod root.
//=============================================================================

static IFileSystem *HL2SB_FileSystem( void )
{
	return g_pFullFileSystem ? g_pFullFileSystem : filesystem;
}

//=============================================================================
// GMod path IDs -> this engine's search paths.
//
// Two of GMod's IDs name a SUBTREE of the game folder rather than a search
// path -- "LUA" is garrysmod/lua and "DATA" is garrysmod/data -- so they become
// a PREFIX on top of the mod tree here.  That also matches GMod's on-disk tree,
// where data/ and lua/ sit inside the game folder and are therefore reachable
// through "GAME" as "data/..." and "lua/...".
//
// Everything else is a path ID this engine already registered: gameinfo.txt
// registers MOD, MOD_WRITE, GAME_WRITE, DEFAULT_WRITE_PATH, GAMEBIN, DOWNLOAD
// and PLATFORM, and the engine adds BSP plus every mounted game name.
//
// https://wiki.facepunch.com/gmod/File_Search_Paths is the list being matched.
//=============================================================================
struct HL2SBGModPathID_t
{
	const char *pGModName;
	const char *pPathID;	// this engine's ID
	const char *pPrefix;	// folder GMod implies on top of it
};

static const HL2SBGModPathID_t s_GModPathIDs[] =
{
	{ "",			"GAME",	"" },
	{ "GAME",		"GAME",	"" },
	{ "ALL",		"GAME",	"" },
	{ "NULL",		"GAME",	"" },
	// GMod's MOD is "the garrysmod folder, addons excluded".  Addons are mounted
	// into the mod tree here, so the mod search path is the closest equivalent.
	{ "MOD",		"MOD",	"" },
	{ "garrysmod",	"MOD",	"" },
	{ "LUA",		"GAME",	"lua/" },
	// GMod splits LUA per realm (lcl / lsv / LuaMenu).  This engine has one lua
	// tree and both realms read all of it, so all four spellings are one path.
	{ "lcl",		"GAME",	"lua/" },
	{ "lsv",		"GAME",	"lua/" },
	{ "LuaMenu",	"GAME",	"lua/" },
	{ "DATA",		"MOD",	"data/" },
	{ "WRITE",		"MOD",	"data/" },
	// GMod's addon-only view.  .gma addons are mounted straight out of
	// hl2sb/addons/, so that folder is the equivalent; WORKSHOP folds in here
	// too because this engine has no separate workshop tree.
	{ "THIRDPARTY",	"MOD",	"addons/" },
	{ "WORKSHOP",	"MOD",	"addons/" },
};

static const HL2SBGModPathID_t *HL2SB_FindGModPathID( const char *pRequest )
{
	if ( !pRequest )
		pRequest = "";

	for ( int i = 0; i < ARRAYSIZE( s_GModPathIDs ); i++ )
	{
		if ( !V_stricmp( s_GModPathIDs[ i ].pGModName, pRequest ) )
			return &s_GModPathIDs[ i ];
	}

	return NULL;
}

// True when the engine has a search path registered under this ID, so that IDs
// this engine really has (a mounted game, "CONFIG", ...) keep working.
//
// GetSearchPath() writes the concatenated search paths into the buffer and
// returns their length PLUS ONE, so an ID nobody registered answers 1 -- hence
// ">1" rather than ">0" (basefilesystem.cpp).
static bool HL2SB_PathIDExists( IFileSystem *pFS, const char *pPathID )
{
	char szScratch[ 8 ];
	return pFS->GetSearchPath( pPathID, true, szScratch, sizeof( szScratch ) ) > 1;
}

// One GMod path ID -> the engine ID to hand to IFileSystem, plus the prefix the
// caller has to put in front of its relative name.
static const char *HL2SB_ResolveFilePathID( IFileSystem *pFS, const char *pRequest,
											char *pPrefix, int nPrefixSize )
{
	pPrefix[ 0 ] = 0;

	if ( !pRequest || !pRequest[ 0 ] )
		return "GAME";

	const HL2SBGModPathID_t *pEntry = HL2SB_FindGModPathID( pRequest );

	if ( pEntry )
	{
		V_strncpy( pPrefix, pEntry->pPrefix, nPrefixSize );
		return pEntry->pPathID;
	}

	if ( HL2SB_PathIDExists( pFS, pRequest ) )
		return pRequest;

	// Two kinds of caller land here: one naming a GMod path ID this engine has
	// no equivalent for, and one using GMod's dynamic IDs (addons are addressed
	// by title -- duplicator.lua:421 passes addon.title to file.Exists).  The
	// binding used to ignore the argument entirely, so searching every path
	// keeps those working instead of reporting a miss only this engine sees.
	return NULL;
}

// GMod's extensions/file.lua spells "the game tree" as file.Read( name, true )
// (lua/menu/mainmenu.lua:236 does exactly that), so booleans are part of the
// path contract and not just a truthiness accident.
static const char *HL2SB_GetPathArg( lua_State *L, int nArg, const char *pDefault );

// The read-only bindings (Exists / IsDir / Size / Time, and Open for a read)
// share this: an omitted path argument searches every search path.
//
// The wiki lists their path argument as required and every example passes one,
// so omitting it is off-contract.  Being generous cannot break a GMod script,
// being strict can -- and these bindings used to ignore the argument entirely.
static const char *HL2SB_ResolveQueryPathID( lua_State *L, int nArg, IFileSystem *pFS,
											 const char *pDefaultIfOmitted,
											 char *pPrefix, int nPrefixSize )
{
	pPrefix[ 0 ] = 0;

	if ( lua_isnoneornil( L, nArg ) )
	{
		if ( !pDefaultIfOmitted )
			return NULL;

		return HL2SB_ResolveFilePathID( pFS, pDefaultIfOmitted, pPrefix, nPrefixSize );
	}

	return HL2SB_ResolveFilePathID( pFS, HL2SB_GetPathArg( L, nArg, "GAME" ), pPrefix, nPrefixSize );
}

static const char *HL2SB_GetPathArg( lua_State *L, int nArg, const char *pDefault )
{
	if ( lua_isnoneornil( L, nArg ) )
		return pDefault;
	if ( lua_isboolean( L, nArg ) )
		return lua_toboolean( L, nArg ) ? "GAME" : "DATA";
	return lua_tostring( L, nArg );
}

// Prefix + relative name, into the caller's buffer.
static void HL2SB_MakeFilePath( char *pOut, int nOutSize, const char *pPrefix,
								const char *pPath, bool bLowerCase )
{
	V_strncpy( pOut, pPrefix ? pPrefix : "", nOutSize );

	char szName[ MAX_PATH ];
	V_strncpy( szName, pPath ? pPath : "", sizeof( szName ) );
	if ( bLowerCase )
		V_strlower( szName );

	V_strncat( pOut, szName, nOutSize );
}

// GMod's write side forces data/-relative names to lowercase -- its file.Write
// and file.Rename pages both document it.  Nothing else is case folded.
static bool HL2SB_LowerForData( const char *pPrefix )
{
	return !V_strcmp( pPrefix, "data/" );
}

// https://wiki.facepunch.com/gmod/file.Open : "it returns nil ... the file
// extension is '.db; .mdmp; .dmp' or the file is 'server.cfg; autoexec.cfg;
// config.cfg; listenserver.cfg; mount.cfg'".  Applies to READS too, not just
// writes -- GMod blocks reading those so addons cannot snoop config.cfg.
static bool HL2SB_OpenBlocked( const char *pPath )
{
	if ( !pPath || !pPath[ 0 ] )
		return false;

	char szLower[ MAX_PATH ];
	V_strncpy( szLower, pPath, sizeof( szLower ) );
	V_strlower( szLower );

	const char *pBase = V_UnqualifiedFileName( szLower );

	static const char *const s_pBlockedNames[] =
	{
		"server.cfg", "autoexec.cfg", "config.cfg", "listenserver.cfg", "mount.cfg"
	};
	for ( int i = 0; i < ARRAYSIZE( s_pBlockedNames ); i++ )
	{
		if ( !V_strcmp( pBase, s_pBlockedNames[ i ] ) )
			return true;
	}

	static const char *const s_pBlockedExts[] = { ".db", ".mdmp", ".dmp" };
	for ( int i = 0; i < ARRAYSIZE( s_pBlockedExts ); i++ )
	{
		size_t nLen = V_strlen( szLower );
		size_t nExt = V_strlen( s_pBlockedExts[ i ] );
		if ( nLen > nExt && !V_strcmp( szLower + nLen - nExt, s_pBlockedExts[ i ] ) )
			return true;
	}

	return false;
}

// https://wiki.facepunch.com/gmod/file.Write : the filename "must end with one
// of the following" -- the write-side extension whitelist.  GMod enforces it on
// every write it considers legal, and its own Lua wrapper (file.Write /
// file.Append -> file.Open "wb"/"ab" "DATA") inherits the answer, so the check
// lives here in file.Open.  Only the DATA side enforces it: writes outside
// data/ are already impossible in GMod, so this fork's extra reach stays
// unrestricted.
static bool HL2SB_WriteExtAllowed( const char *pPath )
{
	if ( !pPath || !pPath[ 0 ] )
		return false;

	char szLower[ MAX_PATH ];
	V_strncpy( szLower, pPath, sizeof( szLower ) );
	V_strlower( szLower );

	static const char *const s_pAllowedExts[] =
	{
		".txt", ".dat", ".json", ".xml", ".csv", ".dem", ".vcd", ".gma",
		".mdl", ".phy", ".vvd", ".vtx", ".ani", ".vtf", ".vmt",
		".png", ".jpg", ".jpeg", ".mp3", ".wav", ".ogg"
	};

	for ( int i = 0; i < ARRAYSIZE( s_pAllowedExts ); i++ )
	{
		size_t nLen = V_strlen( szLower );
		size_t nExt = V_strlen( s_pAllowedExts[ i ] );
		if ( nLen > nExt && !V_strcmp( szLower + nLen - nExt, s_pAllowedExts[ i ] ) )
			return true;
	}

	return false;
}

// Open() does not build the directory tree (basefilesystem.cpp:2686 is a FIXME
// saying exactly that), so a write to data/settings/x.txt needs data/settings
// first.  RenameFile builds the destination's tree for itself.
static void HL2SB_MakeParentDirs( IFileSystem *pFS, const char *pPath, const char *pPathID )
{
	if ( !strchr( pPath, '/' ) && !strchr( pPath, '\\' ) )
		return;

	char szDir[ MAX_PATH ];
	V_strncpy( szDir, pPath, sizeof( szDir ) );
	V_StripFilename( szDir );

	if ( szDir[ 0 ] )
		pFS->CreateDirHierarchy( szDir, pPathID );
}

// Whole-file read into a malloc'd NUL-terminated buffer (caller frees).  NULL
// when the file cannot be opened or read, which is what GMod reports as
// FSASYNC_ERR_FILEOPEN.
static char *HL2SB_ReadFileContents( IFileSystem *pFS, const char *pPath,
									 const char *pPathID, int *pnLen )
{
	*pnLen = 0;

	FileHandle_t fh = pFS->Open( pPath, "rb", pPathID );
	if ( !fh )
		return NULL;

	int nSize = pFS->Size( fh );
	if ( nSize < 0 )
	{
		pFS->Close( fh );
		return NULL;
	}

	char *pBuf = (char *)malloc( nSize + 1 );
	int nRead = ( nSize > 0 ) ? pFS->Read( pBuf, nSize, fh ) : 0;
	pFS->Close( fh );

	if ( nRead < 0 )
	{
		free( pBuf );
		return NULL;
	}

	pBuf[ nRead ] = '\0';
	*pnLen = nRead;
	return pBuf;
}

// file.Read( path [, gamePath] ) -> string | nil
//
// DATA is the default GMod's extensions/file.lua uses, and its own
// gamemodes/sandbox/gamemode/persistence.lua reads back what file.Write stored.
static int file_Read (lua_State *L) {
  const char *pszPath = luaL_checkstring(L, 1);
  IFileSystem *pFS = HL2SB_FileSystem();

  char szPrefix[ 16 ];
  const char *pszPathID = HL2SB_ResolveFilePathID( pFS, HL2SB_GetPathArg( L, 2, "DATA" ),
                                                   szPrefix, sizeof( szPrefix ) );

  char szFull[ MAX_PATH ];
  HL2SB_MakeFilePath( szFull, sizeof( szFull ), szPrefix, pszPath, false );

  int nLen = 0;
  char *pBuf = HL2SB_ReadFileContents( pFS, szFull, pszPathID, &nLen );
  if ( !pBuf ) { lua_pushnil(L); return 1; }

  lua_pushlstring(L, pBuf, nLen);
  free(pBuf);
  return 1;
}

// file.Write / file.Append: GMod writes into data/ (extensions/file.lua opens
// "wb"/"ab" against "DATA") and lowercases the name.  The third argument is an
// extension this binding has always accepted; GMod's own signature stops at two.
static int file_WriteInternal (lua_State *L, const char *pszMode) {
  const char *pszPath = luaL_checkstring(L, 1);
  size_t nLen = 0;
  const char *pszData = luaL_checklstring(L, 2, &nLen);

  IFileSystem *pFS = HL2SB_FileSystem();

  char szPrefix[ 16 ];
  const char *pszPathID = HL2SB_ResolveFilePathID( pFS, HL2SB_GetPathArg( L, 3, "DATA" ),
                                                   szPrefix, sizeof( szPrefix ) );

  char szFull[ MAX_PATH ];
  HL2SB_MakeFilePath( szFull, sizeof( szFull ), szPrefix, pszPath, HL2SB_LowerForData( szPrefix ) );
  HL2SB_MakeParentDirs( pFS, szFull, pszPathID );

  FileHandle_t fh = pFS->Open(szFull, pszMode, pszPathID);
  if (!fh) { lua_pushboolean(L, false); return 1; }

  int nWritten = (nLen > 0) ? pFS->Write(pszData, (int)nLen, fh) : 0;
  pFS->Close(fh);

  lua_pushboolean(L, (int)nLen == nWritten);
  return 1;
}

static int file_Write  (lua_State *L) { return file_WriteInternal(L, "wb"); }
static int file_Append (lua_State *L) { return file_WriteInternal(L, "ab"); }

// file.Exists( name [, path] )
//
// wiki: "Returns a boolean of whether the file or directory exists or not" --
// directories count (the page's own example is file.Exists("data", "GAME")
// answering true), and this engine's FileExists only knows about files.
static int file_Exists (lua_State *L) {
  IFileSystem *pFS = HL2SB_FileSystem();

  char szPrefix[ 16 ];
  const char *pszPathID = HL2SB_ResolveQueryPathID( L, 2, pFS, NULL, szPrefix, sizeof( szPrefix ) );

  char szFull[ MAX_PATH ];
  HL2SB_MakeFilePath( szFull, sizeof( szFull ), szPrefix, luaL_checkstring(L, 1), false );

  lua_pushboolean(L, pFS->FileExists(szFull, pszPathID) || pFS->IsDirectory(szFull, pszPathID));
  return 1;
}

// file.Delete( name [, path] ) -> boolean
//
// GMod documents path as "DATA" here and reports whether the delete worked;
// RemoveFile is void, so that answer is "it was there, and now it is gone".
// A missing name must stay SILENT (GMod answers false without console noise),
// so RemoveFile only runs when something was actually there -- the engine's
// RemoveFile spews "Unable to remove" when it misses.  Directories are checked
// in the success test too: engine FileExists answers false for them, and this
// engine has no RemoveDirectory, so an empty folder honestly reports false.
static int file_Delete (lua_State *L) {
  IFileSystem *pFS = HL2SB_FileSystem();

  char szPrefix[ 16 ];
  const char *pszPathID = HL2SB_ResolveFilePathID( pFS, HL2SB_GetPathArg( L, 2, "DATA" ),
                                                   szPrefix, sizeof( szPrefix ) );

  char szFull[ MAX_PATH ];
  HL2SB_MakeFilePath( szFull, sizeof( szFull ), szPrefix, luaL_checkstring(L, 1),
                      HL2SB_LowerForData( szPrefix ) );

  bool bExisted = pFS->FileExists( szFull, pszPathID ) || pFS->IsDirectory( szFull, pszPathID );

  // RemoveFile only knows files: on a directory (or a miss) it fails and
  // spews "Unable to remove", neither of which GMod does.
  if ( pFS->FileExists( szFull, pszPathID ) )
    pFS->RemoveFile( szFull, pszPathID );

  lua_pushboolean( L, bExisted && !pFS->FileExists( szFull, pszPathID ) &&
                        !pFS->IsDirectory( szFull, pszPathID ) );
  return 1;
}

// file.Time( path [, gamePath] ) -> unix seconds, 0 when the file is missing.
// wiki file.Time: "For workshop content 1 is returned" -- packed files (mounted
// .gma / VPK members here) carry no timestamp at all, GetFileTime answers 0 for
// them, so an existing but timestamp-less file reports 1 the way GMod reports
// its workshop copies.
static int file_Time (lua_State *L) {
  IFileSystem *pFS = HL2SB_FileSystem();

  char szPrefix[ 16 ];
  const char *pszPathID = HL2SB_ResolveQueryPathID( L, 2, pFS, NULL, szPrefix, sizeof( szPrefix ) );

  char szFull[ MAX_PATH ];
  HL2SB_MakeFilePath( szFull, sizeof( szFull ), szPrefix, luaL_checkstring(L, 1), false );

  int nTime = pFS->GetFileTime(szFull, pszPathID);
  if ( pFS->FileExists( szFull, pszPathID ) || pFS->IsDirectory( szFull, pszPathID ) )
  {
    // packed content: timestamp-less, report the workshop stand-in
    if ( nTime <= 0 )
      nTime = 1;
  }
  else
  {
    // wiki: 0 if the file is not found (GetFileTime may answer -1)
    nTime = 0;
  }

  lua_pushnumber(L, (double)nTime);
  return 1;
}

static int file_Size (lua_State *L) {
  IFileSystem *pFS = HL2SB_FileSystem();

  char szPrefix[ 16 ];
  const char *pszPathID = HL2SB_ResolveQueryPathID( L, 2, pFS, NULL, szPrefix, sizeof( szPrefix ) );

  char szFull[ MAX_PATH ];
  HL2SB_MakeFilePath( szFull, sizeof( szFull ), szPrefix, luaL_checkstring(L, 1), false );

  FileHandle_t fh = pFS->Open(szFull, "rb", pszPathID);
  // wiki file.Size: "-1 if the file is not found" (0 used to hide the miss
  // behind a plausible-looking answer)
  if (!fh) { lua_pushnumber(L, -1); return 1; }
  lua_pushnumber(L, pFS->Size(fh));
  pFS->Close(fh);
  return 1;
}

static int file_IsDir (lua_State *L) {
  IFileSystem *pFS = HL2SB_FileSystem();

  char szPrefix[ 16 ];
  const char *pszPathID = HL2SB_ResolveQueryPathID( L, 2, pFS, NULL, szPrefix, sizeof( szPrefix ) );

  char szFull[ MAX_PATH ];
  HL2SB_MakeFilePath( szFull, sizeof( szFull ), szPrefix, luaL_checkstring(L, 1), false );

  lua_pushboolean(L, pFS->IsDirectory(szFull, pszPathID));
  return 1;
}

// file.CreateDir( name ) -- relative to data/, and every '/' level is created
// (the wiki shows file.CreateDir("a/b/c/d/e/f/g") doing exactly that).
static int file_CreateDir (lua_State *L) {
  IFileSystem *pFS = HL2SB_FileSystem();

  char szPrefix[ 16 ];
  const char *pszPathID = HL2SB_ResolveFilePathID( pFS, HL2SB_GetPathArg( L, 2, "DATA" ),
                                                   szPrefix, sizeof( szPrefix ) );

  char szFull[ MAX_PATH ];
  HL2SB_MakeFilePath( szFull, sizeof( szFull ), szPrefix, luaL_checkstring(L, 1),
                      HL2SB_LowerForData( szPrefix ) );

  pFS->CreateDirHierarchy( szFull, pszPathID );
  return 0;
}

// file.Rename( oldName, newName ) -> boolean
//
// The wiki: constrained to the data/ folder, both names forced to lowercase,
// and no path argument.  RenameFile creates the destination's directories.
static int file_Rename (lua_State *L) {
  const char *pszOld = luaL_checkstring(L, 1);
  const char *pszNew = luaL_checkstring(L, 2);

  IFileSystem *pFS = HL2SB_FileSystem();

  char szPrefix[ 16 ];
  const char *pszPathID = HL2SB_ResolveFilePathID( pFS, "DATA", szPrefix, sizeof( szPrefix ) );

  char szOld[ MAX_PATH ];
  char szNew[ MAX_PATH ];
  HL2SB_MakeFilePath( szOld, sizeof( szOld ), szPrefix, pszOld, HL2SB_LowerForData( szPrefix ) );
  HL2SB_MakeFilePath( szNew, sizeof( szNew ), szPrefix, pszNew, HL2SB_LowerForData( szPrefix ) );

  lua_pushboolean( L, pFS->RenameFile( szOld, szNew, pszPathID ) );
  return 1;
}

// file.AsyncRead( fileName, gamePath, callback [, sync] ) -> FSASYNC status
//
// GMod reports through the callback: callback( fileName, gamePath, status,
// data ).  This engine's filesystem has no asynchronous path, and `sync` is
// GMod's own way of asking for the synchronous one, so the read happens inline
// and the callback is still called with GMod's arguments and status codes.
static int file_AsyncRead (lua_State *L) {
  const char *pszPath = luaL_checkstring(L, 1);
  const char *pszPathArg = HL2SB_GetPathArg( L, 2, "DATA" );
  luaL_checktype( L, 3, LUA_TFUNCTION );

  IFileSystem *pFS = HL2SB_FileSystem();

  // wiki file.AsyncRead: "All limitations of file.Read also apply" -- file.Read
  // rides on file.Open in GMod's Lua extension, so the block list rides here.
  if ( HL2SB_OpenBlocked( pszPath ) )
  {
    lua_pushvalue( L, 3 );
    lua_pushvalue( L, 1 );
    lua_pushvalue( L, 2 );
    lua_pushinteger( L, FSASYNC_ERR_FILEOPEN );
    lua_pushnil( L );
    lua_call( L, 4, 0 );
    lua_pushinteger( L, FSASYNC_ERR_FILEOPEN );
    return 1;
  }

  char szPrefix[ 16 ];
  const char *pszPathID = HL2SB_ResolveFilePathID( pFS, pszPathArg, szPrefix, sizeof( szPrefix ) );

  char szFull[ MAX_PATH ];
  HL2SB_MakeFilePath( szFull, sizeof( szFull ), szPrefix, pszPath, false );

  int nLen = 0;
  char *pBuf = HL2SB_ReadFileContents( pFS, szFull, pszPathID, &nLen );

  int nStatus = pBuf ? FSASYNC_OK : FSASYNC_ERR_FILEOPEN;

  // [func] [fileName] [gamePath] [status] [data]
  lua_pushvalue( L, 3 );
  lua_pushvalue( L, 1 );
  lua_pushvalue( L, 2 );
  lua_pushinteger( L, nStatus );
  if ( pBuf )
    lua_pushlstring( L, pBuf, nLen );
  else
    lua_pushnil( L );

  free( pBuf );

  lua_call( L, 4, 0 );

  lua_pushinteger( L, nStatus );
  return 1;
}

// GMod's file.Find( name, pathID, sorting ).
//
// The previous version took one argument, hardcoded pathID "MOD", always appended
// "/*" and returned whatever order the OS handed back.  Every real caller here
// passes a GLOB, so that build was broken on all of them:
//
//     file.Find( dir .. "/*", "LUA" )                    -> glob  "*.lua/*"
//     file.Find( "settings/spawnlist/*.txt", "GAME" )    -> glob  "*.txt/*"
//     file.Find( folder .. "/*.lua", "GAME" )            -> glob  "*.lua/*"
//
// (see backup_addons/Project HL2 MMod Weapon Replacements/lua/autorun/*.lua and
//  experiment-source's gmod_compatibility/sh_init.lua) -- each found nothing.
//
// Sorting modes, from https://wiki.facepunch.com/gmod/file.Find.  Name ascending
// is GMod's default.
enum HL2SBFileSort_e
{
	SORT_NAME_ASC = 0,	SORT_NAME_DESC,
	SORT_TIME_ASC,		SORT_TIME_DESC,
	SORT_SIZE_ASC,		SORT_SIZE_DESC,
	SORT_TYPE_ASC,		SORT_TYPE_DESC,		// TYPE_ASC groups files before dirs
	SORT_EXT_ASC,		SORT_EXT_DESC
};

struct HL2SBFindItem_t
{
	char		name[ 260 ];
	bool		bDir;
	int			nSize;
	time_t		tTime;
};

// Shared with the comparator: CUtlVector::Sort passes no context pointer.
static HL2SBFileSort_e	s_eFindSort      = SORT_NAME_ASC;
static const char	   *s_pFindPathID[ 2 ] = { "GAME", "MOD" };

// Bare names come back from FindFirstEx, so the last dot is the extension dot.
static const char *HL2SB_FindExt( const char *pName )
{
	const char *pDot = strrchr( pName, '.' );
	return ( pDot && pDot[ 1 ] ) ? pDot : pName;
}

// Primary key per the requested mode, name as the tie-breaker throughout.
static int __cdecl HL2SB_FindCmp( const HL2SBFindItem_t *pA, const HL2SBFindItem_t *pB )
{
	bool bDesc = ( s_eFindSort == SORT_TIME_DESC || s_eFindSort == SORT_SIZE_DESC ||
				   s_eFindSort == SORT_TYPE_DESC || s_eFindSort == SORT_EXT_DESC );

	int iCmp = 0;

	switch ( s_eFindSort )
	{
	case SORT_TIME_ASC:
	case SORT_TIME_DESC:
		if ( pA->tTime != pB->tTime )
			iCmp = ( pA->tTime < pB->tTime ) ? -1 : 1;
		break;

	case SORT_SIZE_ASC:
	case SORT_SIZE_DESC:
		if ( pA->nSize != pB->nSize )
			iCmp = ( pA->nSize < pB->nSize ) ? -1 : 1;
		break;

	case SORT_TYPE_ASC:
	case SORT_TYPE_DESC:
		// TYPE_ASC groups files before dirs
		if ( pA->bDir != pB->bDir )
			iCmp = pA->bDir ? 1 : -1;
		break;

	case SORT_EXT_ASC:
	case SORT_EXT_DESC:
		iCmp = Q_stricmp( HL2SB_FindExt( pA->name ), HL2SB_FindExt( pB->name ) );
		break;

	default:
		break;
	}

	if ( iCmp != 0 )
		return bDesc ? -iCmp : iCmp;

	iCmp = Q_stricmp( pA->name, pB->name );

	return ( s_eFindSort == SORT_NAME_DESC ) ? -iCmp : iCmp;
}

// Maps a GMod path ID onto the engine search paths file.Find should walk.
//
// Returns how many IDs to search, and 0 for a path GMod would call invalid --
// file.Find answers nil, nil for those (that is the documented contract, and it
// is why this used to be wrong: an unregistered ID was handed to FindFirstEx,
// which quietly found nothing and looked like an empty folder).
//
// Also writes the prefix GMod implies on top of the search paths: its "LUA" and
// "DATA" are subtrees of the game folder, not search paths (see s_GModPathIDs).
static int HL2SB_ResolveFindPathID( const char *pRequest, char *pPrefix, int nPrefixSize )
{
	pPrefix[ 0 ] = 0;

	// An omitted path searches everything.  GMod's signature marks the argument
	// required, so this is leniency rather than contract.
	if ( !pRequest || !pRequest[ 0 ] )
	{
		s_pFindPathID[ 0 ] = "GAME";	s_pFindPathID[ 1 ] = "MOD";
		return 2;
	}

	const HL2SBGModPathID_t *pEntry = HL2SB_FindGModPathID( pRequest );

	if ( pEntry )
	{
		V_strncpy( pPrefix, pEntry->pPrefix, nPrefixSize );

		// GAME and MOD are searched as a pair: gameinfo.txt mounts the mod tree
		// as "game+mod" but garrysmod.vpk as "mod" only, so neither one alone is
		// everything.
		// MOD-first for the IDs whose writable side is the mod tree (DATA,
		// THIRDPARTY); GAME-first otherwise, so the game's copy of a file wins
		// over a mounted addon's -- the priority order gameinfo.txt sets up.
		bool bModFirst = ( !V_stricmp( pEntry->pPathID, "MOD" ) );
		s_pFindPathID[ 0 ] = bModFirst ? "MOD"  : "GAME";
		s_pFindPathID[ 1 ] = bModFirst ? "GAME" : "MOD";
		return 2;
	}

	// A path ID this engine registered under GMod's own name: a mounted game
	// ("hl2", "cstrike"), PLATFORM, CONFIG, GAMEBIN, DOWNLOAD, or "BSP" (which
	// basefilesystem.cpp routes to the current map's pack file).
	if ( HL2SB_PathIDExists( HL2SB_FileSystem(), pRequest ) )
	{
		s_pFindPathID[ 0 ] = pRequest;
		return 1;
	}

	return 0;
}

// Name + kind only, used to de-duplicate before the requested sort runs.
static int __cdecl HL2SB_FindNameCmp( const HL2SBFindItem_t *pA, const HL2SBFindItem_t *pB )
{
	if ( pA->bDir != pB->bDir )
		return pA->bDir ? 1 : -1;

	return Q_stricmp( pA->name, pB->name );
}

static int file_Find (lua_State *L) {
	const char *pszRequest = luaL_checkstring( L, 1 );

	// GMod's first argument is either a bare folder or a folder plus a glob; a
	// bare folder gets "*" appended as its pattern.  Split on the last slash.
	char szIn[ MAX_PATH ];
	V_strncpy( szIn, pszRequest, sizeof( szIn ) );
	V_FixSlashes( szIn, '/' );

	char szFolder[ MAX_PATH ];
	char szGlob[ MAX_PATH ];
	bool bHasGlob = ( strchr( szIn, '*' ) != NULL );
	char *pLastSlash = strrchr( szIn, '/' );

	if ( !bHasGlob )
	{
		V_strncpy( szFolder, szIn, sizeof( szFolder ) );
		V_strncat( szFolder, "/", sizeof( szFolder ) );
		V_strncpy( szGlob, "*", sizeof( szGlob ) );
	}
	else if ( pLastSlash )
	{
		V_strncpy( szFolder, szIn, (int)( pLastSlash - szIn ) + 1 );	// keeps the slash
		V_strncpy( szGlob, pLastSlash + 1, sizeof( szGlob ) );
	}
	else
	{
		szFolder[ 0 ] = 0;
		V_strncpy( szGlob, szIn, sizeof( szGlob ) );
	}

	// trim trailing slashes; the pattern below re-adds exactly one
	int nFolder = V_strlen( szFolder );
	while ( nFolder > 0 && szFolder[ nFolder - 1 ] == '/' )
		szFolder[ --nFolder ] = 0;

	char szPrefix[ 16 ];
	int nPathIDs = HL2SB_ResolveFindPathID( lua_tostring( L, 2 ), szPrefix, sizeof( szPrefix ) );

	char szPattern[ MAX_PATH ];
	if ( nFolder > 0 )
		Q_snprintf( szPattern, sizeof( szPattern ), "%s%s/%s", szPrefix, szFolder, szGlob );
	else
		Q_snprintf( szPattern, sizeof( szPattern ), "%s%s", szPrefix, szGlob );

	// GMod: "A table of found files, or nil if the path is invalid."  The two
	// nils are the point of the check -- GMod's own Lua everywhere guards with
	// `if ( !files ) then return end`, and an empty table sails straight past
	// that guard and dies on files[1].
	if ( nPathIDs <= 0 )
	{
		lua_pushnil( L );
		lua_pushnil( L );
		return 2;
	}

	// "sorting" is optional; an unrecognised value falls back to GMod's default
	// instead of erroring, because addons pass all kinds of things.
	s_eFindSort = SORT_NAME_ASC;
	const char *pszSort = lua_tostring( L, 3 );
	if ( pszSort && pszSort[ 0 ] )
	{
		char szKey[ 32 ];
		V_strncpy( szKey, pszSort, sizeof( szKey ) );
		V_strlower( szKey );

		bool bDesc = ( strstr( szKey, "desc" ) != NULL );

		if ( strstr( szKey, "time" ) || strstr( szKey, "date" ) )
			s_eFindSort = bDesc ? SORT_TIME_DESC : SORT_TIME_ASC;
		else if ( strstr( szKey, "size" ) )
			s_eFindSort = bDesc ? SORT_SIZE_DESC : SORT_SIZE_ASC;
		else if ( strstr( szKey, "type" ) )
			s_eFindSort = bDesc ? SORT_TYPE_DESC : SORT_TYPE_ASC;
		else if ( strstr( szKey, "ext" ) )
			s_eFindSort = bDesc ? SORT_EXT_DESC : SORT_EXT_ASC;
		else if ( bDesc )
			s_eFindSort = SORT_NAME_DESC;
	}

	IFileSystem *pFS = HL2SB_FileSystem();

	// Rebuilt per hit as <folder>/<name> for the size and time sorts.
	char szFolderFull[ MAX_PATH ];
	if ( nFolder > 0 )
		Q_snprintf( szFolderFull, sizeof( szFolderFull ), "%s%s/", szPrefix, szFolder );
	else
		V_strncpy( szFolderFull, szPrefix, sizeof( szFolderFull ) );

	bool bNeedStats = ( s_eFindSort >= SORT_TIME_ASC );

	CUtlVector< HL2SBFindItem_t > items;

	for ( int iPathID = 0; iPathID < nPathIDs; iPathID++ )
	{
		FileFindHandle_t fh;
		const char *pszFound = pFS->FindFirstEx( szPattern, s_pFindPathID[ iPathID ], &fh );

		while ( pszFound )
		{
			// GMod reports neither "." / ".." nor dotfiles here
			if ( pszFound[ 0 ] != '.' )
			{
				HL2SBFindItem_t it;
				V_strncpy( it.name, pszFound, sizeof( it.name ) );
				it.bDir  = pFS->FindIsDirectory( fh );
				it.nSize = 0;
				it.tTime = 0;

				if ( bNeedStats )
				{
					char szFull[ MAX_PATH ];
					Q_snprintf( szFull, sizeof( szFull ), "%s%s", szFolderFull, pszFound );
					it.nSize = (int)pFS->Size( szFull, s_pFindPathID[ iPathID ] );
					it.tTime = pFS->GetFileTime( szFull, s_pFindPathID[ iPathID ] );
				}

				items.AddToTail( it );
			}

			pszFound = pFS->FindNext( fh );
		}

		// FindFirstEx sets *pHandle = -1 when it finds nothing and FindClose
		// rejects negative handles, so this is also the empty-result path.
		pFS->FindClose( fh );
	}

	// De-duplicate BEFORE the requested sort.  The same relative name can come
	// back from both GAME and MOD (the mod tree is mounted into both), and under
	// dateasc/sizeasc the two copies are ordered by time/size rather than by
	// name, so they are not neighbours and the second one used to be listed
	// twice.  Sorting by name first makes them adjacent; every copy carries the
	// same name, so which one survives is irrelevant to the result.
	items.Sort( HL2SB_FindNameCmp );

	int nKept = 0;
	for ( int i = 0; i < items.Count(); i++ )
	{
		if ( nKept > 0 && items[ nKept - 1 ].bDir == items[ i ].bDir &&
			 !V_stricmp( items[ nKept - 1 ].name, items[ i ].name ) )
			continue;

		items[ nKept++ ] = items[ i ];
	}
	items.RemoveMultiple( nKept, items.Count() - nKept );

	items.Sort( HL2SB_FindCmp );

	lua_newtable( L );					// files, returned first
	int iFiles = 0;
	for ( int i = 0; i < items.Count(); i++ )
	{
		if ( items[ i ].bDir )
			continue;

		lua_pushstring( L, items[ i ].name );
		lua_rawseti( L, -2, ++iFiles );
	}

	lua_newtable( L );					// dirs, returned second
	int iDirs = 0;
	for ( int i = 0; i < items.Count(); i++ )
	{
		if ( !items[ i ].bDir )
			continue;

		lua_pushstring( L, items[ i ].name );
		lua_rawseti( L, -2, ++iDirs );
	}

	return 2;
}

// file.Open( path [, mode] ) -> FileHandle
//
// lua/includes/extensions/file.lua:7 is implemented ON TOP of this (GMod's file
// extension wraps file.Open), so leaving it out made that whole file fail and
// took extensions/player_auth.lua down with it:
//
//     extensions/player_auth.lua -> lua/includes/extensions/file.lua:7:
//         attempt to call a nil value (field 'Open')
//
// lfilesystem.cpp already binds the FileHandle_t metatable (Close / Read / Write /
// Size / Seek / EndOfFile / Flush / IsOk) plus lua_pushfilehandle, so the handle
// GMod's file.lua expects is exactly the one this engine already has.  Opened
// through the same filesystem pointer those methods use, so open and close
// cannot end up on different interfaces.  GMod's mode strings ("r", "w", "a",
// "rb", "wb", "ab", "r+", ...) are Source's own, so they pass straight through.
//
// The third argument is GMod's gamePath -- a path ID such as "DATA", not a
// folder -- and it used to be dropped on the floor.
static int file_Open (lua_State *L) {
  const char *pszPath = luaL_checkstring(L, 1);
  const char *pszMode = luaL_optstring(L, 2, "r");

  IFileSystem *pFS = HL2SB_FileSystem();

  // The write side is GMod's data/ folder (extensions/file.lua opens "wb"/"ab"
  // against "DATA") and picks up its lowercase naming rule.  The read side has
  // no documented default, so an omitted path searches every search path --
  // strictly more than the "MOD" this used to hardcode.
  const bool bWriting = ( strchr( pszMode, 'w' ) != NULL || strchr( pszMode, 'a' ) != NULL );

  // GMod's block list (wiki file.Open): extensions ".db/.mdmp/.dmp" and the
  // five config file names answer nil on every mode, reads included.
  if ( HL2SB_OpenBlocked( pszPath ) )
  {
    lua_pushnil(L);
    return 1;
  }

  char szPrefix[ 16 ];
  const char *pszPathID = HL2SB_ResolveQueryPathID( L, 3, pFS, bWriting ? "DATA" : NULL,
                                                   szPrefix, sizeof( szPrefix ) );

  char szFull[ MAX_PATH ];
  HL2SB_MakeFilePath( szFull, sizeof( szFull ), szPrefix, pszPath,
                      bWriting && HL2SB_LowerForData( szPrefix ) );

  // GMod's write-side extension whitelist (wiki file.Write), enforced on the
  // DATA side -- the only write path GMod legally allows.  A blocked name
  // answers nil, which the Lua file.Write/file.Append wrappers report as false.
  if ( bWriting && HL2SB_LowerForData( szPrefix ) && !HL2SB_WriteExtAllowed( pszPath ) )
  {
    lua_pushnil(L);
    return 1;
  }

  if ( bWriting )
    HL2SB_MakeParentDirs( pFS, szFull, pszPathID );

  FileHandle_t hFile = pFS->Open(szFull, pszMode, pszPathID);

  // HL2SB: GMod's file.Open returns nil when the file cannot be opened, and
  // lua/includes/extensions/file.lua relies on that ("if ( !f ) then return nil
  // end") before calling f:Size().  Pushing a FileHandle_t wrapping
  // FILESYSTEM_INVALID_HANDLE turned the miss into
  //
  //   file.lua:10: calling 'Size' on bad self (FileHandle_t expected, got FILESYSTEM_INVALID_HANDLE)
  if (hFile == FILESYSTEM_INVALID_HANDLE) {
    lua_pushnil(L);
    return 1;
  }

  lua_pushfilehandle(L, hFile);
  return 1;
}


static const luaL_Reg file_funcs[] = {
  {"Open",      file_Open},
  {"Read",      file_Read},
  {"Write",     file_Write},
  {"Append",    file_Append},
  {"Exists",    file_Exists},
  {"Delete",    file_Delete},
  {"Rename",    file_Rename},
  {"Time",      file_Time},
  {"Size",      file_Size},
  {"IsDir",     file_IsDir},
  {"CreateDir", file_CreateDir},
  {"Find",      file_Find},
  {"AsyncRead", file_AsyncRead},
  {NULL, NULL}
};

// FSASYNC status codes as plain globals, the way GMod ships them -- the wiki's
// file.AsyncRead example compares `status == FSASYNC_OK` directly, so addons
// expect these names to exist without any require.
static void HL2SB_PushFSAsyncGlobals (lua_State *L) {
  static const struct { const char *pszName; int nValue; } s_Enums[] =
  {
    { "FSASYNC_ERR_NOT_MINE",     FSASYNC_ERR_NOT_MINE },
    { "FSASYNC_ERR_RETRY_LATER",  FSASYNC_ERR_RETRY_LATER },
    { "FSASYNC_ERR_ALIGNMENT",    FSASYNC_ERR_ALIGNMENT },
    { "FSASYNC_ERR_FAILURE",      FSASYNC_ERR_FAILURE },
    { "FSASYNC_ERR_READING",      FSASYNC_ERR_READING },
    { "FSASYNC_ERR_NOMEMORY",     FSASYNC_ERR_NOMEMORY },
    { "FSASYNC_ERR_UNKNOWNID",    FSASYNC_ERR_UNKNOWNID },
    { "FSASYNC_ERR_FILEOPEN",     FSASYNC_ERR_FILEOPEN },
    { "FSASYNC_OK",               FSASYNC_OK },
    { "FSASYNC_STATUS_PENDING",   FSASYNC_STATUS_PENDING },
    { "FSASYNC_STATUS_INPROGRESS",FSASYNC_STATUS_INPROGRESS },
    { "FSASYNC_STATUS_ABORTED",   FSASYNC_STATUS_ABORTED },
    { "FSASYNC_STATUS_UNSERVICED",FSASYNC_STATUS_UNSERVICED },
  };

  for ( int i = 0; i < ARRAYSIZE( s_Enums ); i++ )
  {
    // Only seed a missing global; an addon or a future engine lib may have
    // set its own value already.
    lua_getglobal( L, s_Enums[ i ].pszName );
    if ( lua_isnil( L, -1 ) )
    {
      lua_pop( L, 1 );
      lua_pushinteger( L, s_Enums[ i ].nValue );
      lua_setglobal( L, s_Enums[ i ].pszName );
    }
    else
    {
      lua_pop( L, 1 );
    }
  }
}

LUALIB_API int luaopen_Files (lua_State *L) {
  // Self-sufficient: the handle metatable normally arrives with
  // luaopen_filesystem, but the main menu state opens this library on its own.
  HL2SB_RegisterFileHandleMeta(L);
  luaL_register(L, LUA_FILESLIBNAME, file_funcs);
  HL2SB_PushFSAsyncGlobals(L);
  return 1;
}
