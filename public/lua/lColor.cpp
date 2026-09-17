//========= Copyright © 1996-2005, Valve Corporation, All rights reserved. ============//
//
// Purpose: Lua Color, in Garry's Mod's shape.
//
//          Garry's Mod's Color is a plain Lua *table* with r/g/b/a fields, not a
//          userdata holding a C++ Color:
//
//              Color( 255, 0, 0 ).r        -> 255          (a number, a real field)
//              type( Color( 0, 0, 0 ) )    -> "table"
//
//          HL2SB inherited the Team Sandbox era userdata version, where .r was a
//          *method* -- so `col.r` returned a function and every GMod script that
//          reads a colour component broke.  Confirmed by dumping GMod's own
//          environment: `Color() -> type = table`.
//
//          Two consequences worth spelling out:
//
//            * The metatable deliberately has NO __type.  HL2SB's type() reads
//              __type off any value that has a metatable, so setting it would
//              report "Color" where GMod reports "table".  MetaName and MetaID are
//              still set, because GMod's Color metatable carries them (MetaID 44)
//              and FindMetaTable/TypeID read those directly.
//
//            * `col:r()` no longer works -- it cannot, since `col.r` is now a
//              number.  The five places in HL2SB's own Lua that used it were
//              updated to read the fields.
//
// $NoKeywords: $
//=============================================================================//

#include "cbase.h"
#include "lua.hpp"

#include "lColor.h"
#include "luasrclib.h"
#include "luamanager.h"

// memdbgon must be the last include file in a .cpp file!!!
#include "tier0/memdbgon.h"

#define LUA_COLOR_FIELD_R "r"
#define LUA_COLOR_FIELD_G "g"
#define LUA_COLOR_FIELD_B "b"
#define LUA_COLOR_FIELD_A "a"

static void lua_pushcolor_field (lua_State *L, const char *pszField, int iValue) {
  lua_pushinteger(L, iValue);
  lua_setfield(L, -2, pszField);
}

static int lua_color_field (lua_State *L, int idx, const char *pszField, int iDefault) {
  if (idx < 0)
    idx = lua_gettop(L) + idx + 1;
  lua_getfield(L, idx, pszField);
  int iValue = lua_isnumber(L, -1) ? (int)lua_tointeger(L, -1) : iDefault;
  lua_pop(L, 1);
  return iValue;
}

/*
** access functions (stack -> C)
**
** By value, not by reference: the colour lives in the table's fields rather than
** in a C++ object the stack points at, so there is nothing to hand back a
** reference to.  Callers use the result immediately (luaL_checkcolor(L,1).a()),
** which is why this is not a source-level problem.
*/

LUA_API lua_Color lua_tocolor (lua_State *L, int idx) {
  luaL_checktype(L, idx, LUA_TTABLE);
  return Color(lua_color_field(L, idx, LUA_COLOR_FIELD_R, 255),
               lua_color_field(L, idx, LUA_COLOR_FIELD_G, 255),
               lua_color_field(L, idx, LUA_COLOR_FIELD_B, 255),
               lua_color_field(L, idx, LUA_COLOR_FIELD_A, 255));
}

LUALIB_API lua_Color luaL_checkcolor (lua_State *L, int narg) {
  if (!lua_istable(L, narg))
    luaL_argerror(L, narg, "Color expected, got " LUA_QL("table"));
  return lua_tocolor(L, narg);
}


/*
** push functions (C -> stack)
*/

LUA_API void lua_pushcolor (lua_State *L, const lua_Color &clr) {
  lua_newtable(L);
  lua_pushcolor_field(L, LUA_COLOR_FIELD_R, clr.r());
  lua_pushcolor_field(L, LUA_COLOR_FIELD_G, clr.g());
  lua_pushcolor_field(L, LUA_COLOR_FIELD_B, clr.b());
  lua_pushcolor_field(L, LUA_COLOR_FIELD_A, clr.a());
  luaL_getmetatable(L, LUA_COLORLIBNAME);
  lua_setmetatable(L, -2);
}


/* HL2SB: ported from Experiment: Source (see lColor.h for the by-value return). */
LUALIB_API lua_Color luaL_optcolor (lua_State *L, int narg, lua_Color def) {
  if (lua_isnoneornil(L, narg))
    return def;
  return luaL_checkcolor(L, narg);
}


LUALIB_API bool lua_iscolor (lua_State *L, int narg) {
  if (!lua_istable(L, narg))
    return false;
  if (!lua_getmetatable(L, narg))
    return false;
  luaL_getmetatable(L, LUA_COLORLIBNAME);
  bool bIsColor = lua_rawequal(L, -1, -2) != 0;
  lua_pop(L, 2);
  return bIsColor;
}


/*
** Methods.  GMod spells the accessors GetR/GetG/GetB/GetA and SetR/SetG/SetB/SetA;
** the old lowercase r()/g()/b()/a() methods are gone because the fields of the same
** name shadow them.
*/

#define LUA_COLOR_GETTER( methodName, field, upperName )        \
  static int Color_##methodName (lua_State *L) {                \
    luaL_checktype(L, 1, LUA_TTABLE);                           \
    lua_getfield(L, 1, LUA_COLOR_FIELD_##upperName);            \
    if (!lua_isnumber(L, -1)) {                                 \
      lua_pop(L, 1);                                            \
      lua_pushinteger(L, 255);                                  \
    }                                                           \
    return 1;                                                   \
  }

LUA_COLOR_GETTER(GetR, r, R)
LUA_COLOR_GETTER(GetG, g, G)
LUA_COLOR_GETTER(GetB, b, B)
LUA_COLOR_GETTER(GetA, a, A)

#define LUA_COLOR_SETTER( methodName, field, upperName )   \
  static int Color_##methodName (lua_State *L) {           \
    luaL_checktype(L, 1, LUA_TTABLE);                      \
    lua_pushinteger(L, luaL_checkint(L, 2));               \
    lua_setfield(L, 1, LUA_COLOR_FIELD_##upperName);       \
    return 0;                                              \
  }

LUA_COLOR_SETTER(SetR, r, R)
LUA_COLOR_SETTER(SetG, g, G)
LUA_COLOR_SETTER(SetB, b, B)
LUA_COLOR_SETTER(SetA, a, A)

/* HL2SB legacy: return all four at once. */
static int Color_GetColor (lua_State *L) {
  Color clr = luaL_checkcolor(L, 1);
  lua_pushinteger(L, clr.r());
  lua_pushinteger(L, clr.g());
  lua_pushinteger(L, clr.b());
  lua_pushinteger(L, clr.a());
  return 4;
}

static int Color_SetColor (lua_State *L) {
  luaL_checktype(L, 1, LUA_TTABLE);
  lua_pushinteger(L, luaL_checkint(L, 2));
  lua_setfield(L, 1, LUA_COLOR_FIELD_R);
  lua_pushinteger(L, luaL_checkint(L, 3));
  lua_setfield(L, 1, LUA_COLOR_FIELD_G);
  lua_pushinteger(L, luaL_checkint(L, 4));
  lua_setfield(L, 1, LUA_COLOR_FIELD_B);
  lua_pushinteger(L, luaL_optint(L, 5, 255));
  lua_setfield(L, 1, LUA_COLOR_FIELD_A);
  return 0;
}

static int Color_GetRawColor (lua_State *L) {
  lua_pushinteger(L, luaL_checkcolor(L, 1).GetRawColor());
  return 1;
}

static int Color_SetRawColor (lua_State *L) {
  Color clr;
  clr.SetRawColor(luaL_checkint(L, 2));
  lua_pushcolor_field(L, LUA_COLOR_FIELD_R, clr.r());
  lua_pushcolor_field(L, LUA_COLOR_FIELD_G, clr.g());
  lua_pushcolor_field(L, LUA_COLOR_FIELD_B, clr.b());
  lua_pushcolor_field(L, LUA_COLOR_FIELD_A, clr.a());
  return 0;
}

/* GMod's Color:ToTable() returns a fresh plain table, and Unpack returns the four. */
static int Color_ToTable (lua_State *L) {
  if (lua_istable(L, 1)) {
    lua_pushvalue(L, 1);
    return 1;
  }
  return 0;
}

static int Color_Copy (lua_State *L) {
  lua_pushcolor(L, luaL_checkcolor(L, 1));
  return 1;
}

static int Color_Unpack (lua_State *L) {
  return Color_GetColor(L);
}

static int Color___tostring (lua_State *L) {
  Color color = luaL_checkcolor(L, 1);
  // A plain buffer rather than CFmtStr: this file is built for both realms and the
  // tier1 helper is not always reachable through the headers that are included here.
  char szBuffer[64];
  Q_snprintf(szBuffer, sizeof(szBuffer), "(%i, %i, %i, %i)", color.r(), color.g(), color.b(), color.a());
  lua_pushfstring(L, "Color: %s", szBuffer);
  return 1;
}

static int Color___eq (lua_State *L) {
  lua_pushboolean(L, luaL_checkcolor(L, 1) == luaL_checkcolor(L, 2));
  return 1;
}


static const luaL_Reg Colormeta[] = {
  {"GetR", Color_GetR},
  {"GetG", Color_GetG},
  {"GetB", Color_GetB},
  {"GetA", Color_GetA},
  {"SetR", Color_SetR},
  {"SetG", Color_SetG},
  {"SetB", Color_SetB},
  {"SetA", Color_SetA},
  {"GetColor", Color_GetColor},
  {"SetColor", Color_SetColor},
  {"GetRawColor", Color_GetRawColor},
  {"SetRawColor", Color_SetRawColor},
  {"ToTable", Color_ToTable},
  {"Copy", Color_Copy},
  {"Unpack", Color_Unpack},
  {"__tostring", Color___tostring},
  {"__eq", Color___eq},
  {NULL, NULL}
};


static int luasrc_Color (lua_State *L) {
  Color clr = Color(luaL_checkint(L, 1), luaL_checkint(L, 2), luaL_checkint(L, 3), luaL_optint(L, 4, 255));
  lua_pushcolor(L, clr);
  return 1;
}


static const luaL_Reg Color_funcs[] = {
  {"Color", luasrc_Color},
  {NULL, NULL}
};


/*
** HL2SB: GMod's ColorToHSV( color ) -> h, s, v
**   h in [0, 360], s in [0, 1], v in [0, 1].
** Required by DColorCube / DColorMixer (lua/vgui/DColorCube.lua:81 etc).
*/
static int HL2SB_ColorToHSV (lua_State *L) {
  lua_Color c = luaL_checkcolor(L, 1);
  float r = c.r() / 255.0f;
  float g = c.g() / 255.0f;
  float b = c.b() / 255.0f;

  float max = r, min = r;
  if (g > max) max = g;
  if (b > max) max = b;
  if (g < min) min = g;
  if (b < min) min = b;

  float h = 0.0f, s = 0.0f, v = max;
  float delta = max - min;

  if (delta > 1e-6f) {
    s = delta / max;
    if (r >= max)        h = 60.0f * (g - b) / delta;
    else if (g >= max)   h = 120.0f + 60.0f * (b - r) / delta;
    else                 h = 240.0f + 60.0f * (r - g) / delta;
    if (h < 0.0f)        h += 360.0f;
  }

  lua_pushnumber(L, h);
  lua_pushnumber(L, s);
  lua_pushnumber(L, v);
  return 3;
}

/*
** HL2SB: GMod's HSVToColor( h, s, v [, a] ) -> Color table
*/
static int HL2SB_HSVToColor (lua_State *L) {
  float h = (float)luaL_checknumber(L, 1);
  float s = (float)luaL_checknumber(L, 2);
  float v = (float)luaL_checknumber(L, 3);
  int   a = (int)luaL_optnumber(L, 4, 255);

  if (s < 0.0f) s = 0.0f; else if (s > 1.0f) s = 1.0f;
  if (v < 0.0f) v = 0.0f; else if (v > 1.0f) v = 1.0f;

  float c = v * s;
  float hp = fmodf(h, 360.0f) / 60.0f;
  float x  = c * (1.0f - fabsf(fmodf(hp, 2.0f) - 1.0f));
  float m  = v - c;

  float r = 0, g = 0, b = 0;
  if      (hp < 1.0f) { r = c; g = x; b = 0; }
  else if (hp < 2.0f) { r = x; g = c; b = 0; }
  else if (hp < 3.0f) { r = 0; g = c; b = x; }
  else if (hp < 4.0f) { r = 0; g = x; b = c; }
  else if (hp < 5.0f) { r = x; g = 0; b = c; }
  else                { r = c; g = 0; b = x; }

  lua_newtable(L);
  lua_pushcolor_field(L, LUA_COLOR_FIELD_R, (int)((r + m) * 255.0f + 0.5f));
  lua_pushcolor_field(L, LUA_COLOR_FIELD_G, (int)((g + m) * 255.0f + 0.5f));
  lua_pushcolor_field(L, LUA_COLOR_FIELD_B, (int)((b + m) * 255.0f + 0.5f));
  lua_pushcolor_field(L, LUA_COLOR_FIELD_A, a);
  return 1;
}

/*
** HL2SB: GMod's ColorToHSL( color ) -> h, s, l
**   h in [0, 360], s in [0, 1], l in [0, 1].
*/
static int HL2SB_ColorToHSL (lua_State *L) {
  lua_Color c = luaL_checkcolor(L, 1);
  float r = c.r() / 255.0f;
  float g = c.g() / 255.0f;
  float b = c.b() / 255.0f;

  float max = r, min = r;
  if (g > max) max = g;
  if (b > max) max = b;
  if (g < min) min = g;
  if (b < min) min = b;

  float h = 0.0f, s = 0.0f, l = (max + min) * 0.5f;
  float delta = max - min;

  if (delta > 1e-6f) {
    s = (l > 0.5f) ? delta / (2.0f - max - min) : delta / (max + min);
    if (r >= max)        h = 60.0f * (g - b) / delta;
    else if (g >= max)   h = 120.0f + 60.0f * (b - r) / delta;
    else                 h = 240.0f + 60.0f * (r - g) / delta;
    if (h < 0.0f)        h += 360.0f;
  }

  lua_pushnumber(L, h);
  lua_pushnumber(L, s);
  lua_pushnumber(L, l);
  return 3;
}

/*
** HL2SB: GMod's HSLToColor( h, s, l [, a] ) -> Color table
*/
static int HL2SB_HSLToColor (lua_State *L) {
  float h = (float)luaL_checknumber(L, 1);
  float s = (float)luaL_checknumber(L, 2);
  float l = (float)luaL_checknumber(L, 3);
  int   a = (int)luaL_optnumber(L, 4, 255);

  if (s < 0.0f) s = 0.0f; else if (s > 1.0f) s = 1.0f;
  if (l < 0.0f) l = 0.0f; else if (l > 1.0f) l = 1.0f;

  float c = (1.0f - fabsf(2.0f * l - 1.0f)) * s;
  float hp = fmodf(h, 360.0f) / 60.0f;
  float x  = c * (1.0f - fabsf(fmodf(hp, 2.0f) - 1.0f));
  float m  = l - c * 0.5f;

  float r = 0, g = 0, b = 0;
  if      (hp < 1.0f) { r = c; g = x; b = 0; }
  else if (hp < 2.0f) { r = x; g = c; b = 0; }
  else if (hp < 3.0f) { r = 0; g = c; b = x; }
  else if (hp < 4.0f) { r = 0; g = x; b = c; }
  else if (hp < 5.0f) { r = x; g = 0; b = c; }
  else                { r = c; g = 0; b = x; }

  lua_newtable(L);
  lua_pushcolor_field(L, LUA_COLOR_FIELD_R, (int)((r + m) * 255.0f + 0.5f));
  lua_pushcolor_field(L, LUA_COLOR_FIELD_G, (int)((g + m) * 255.0f + 0.5f));
  lua_pushcolor_field(L, LUA_COLOR_FIELD_B, (int)((b + m) * 255.0f + 0.5f));
  lua_pushcolor_field(L, LUA_COLOR_FIELD_A, a);
  return 1;
}

/*
** Open Color object
*/
LUALIB_API int luaopen_Color (lua_State *L) {
  luaL_newmetatable(L, LUA_COLORLIBNAME);
  luaL_register(L, NULL, Colormeta);
  lua_pushvalue(L, -1);  /* push metatable */
  lua_setfield(L, -2, "__index");  /* metatable.__index = metatable */
  /*
  ** No __type here, on purpose: HL2SB's type() reports __type for anything with a
  ** metatable, and GMod reports "table" for a Color.  MetaName/MetaID are stamped
  ** by lsrcinit.cpp's type table, which knows to leave __type alone for it.
  */
  luaL_register(L, "_G", Color_funcs);
  lua_pop(L, 1);

  /*
  ** HL2SB: GMod's named colour globals.  They were never published, so every
  ** stock GMod script that passes color_white around -- killicon.Add( name,
  ** icon, color_white ), surface.SetDrawColor( color_white ),
  ** render.DrawQuadEasy( ..., color_white, ... ), util.SpriteTrail( ... ) -- was
  ** handing nil to a function that needs a colour.  GMod declares these three in
  ** C, with the same values.
  */
  lua_Color white( 255, 255, 255, 255 );
  lua_Color black( 0, 0, 0, 255 );
  lua_Color transparent( 255, 255, 255, 0 );

  lua_pushcolor( L, white );
  lua_setglobal( L, "color_white" );
  lua_pushcolor( L, black );
  lua_setglobal( L, "color_black" );
  lua_pushcolor( L, transparent );
  lua_setglobal( L, "color_transparent" );

  // HL2SB: GMod's colour-space conversion globals.  DColorCube / DColorMixer
  // (and any GMod addon that builds HSV pickers) call these unconditionally.
  lua_pushcfunction( L, HL2SB_ColorToHSV );
  lua_setglobal( L, "ColorToHSV" );
  lua_pushcfunction( L, HL2SB_HSVToColor );
  lua_setglobal( L, "HSVToColor" );
  lua_pushcfunction( L, HL2SB_ColorToHSL );
  lua_setglobal( L, "ColorToHSL" );
  lua_pushcfunction( L, HL2SB_HSLToColor );
  lua_setglobal( L, "HSLToColor" );

  return 1;
}
