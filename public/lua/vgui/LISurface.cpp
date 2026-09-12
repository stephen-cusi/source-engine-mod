//===== Copyright © 1996-2005, Valve Corporation, All rights reserved. ======//
//
// Purpose: 
//
// $NoKeywords: $
//===========================================================================//

#define LISurface_cpp

#include "cbase.h"
#include "vgui/ISurface.h"
#include "vgui/ILocalize.h"
#include "vgui_controls/Controls.h"
#include "luamanager.h"
#include "luasrclib.h"
#include "vgui/LVGUI.h"
#include "vgui_controls/lPanel.h"
#include "materialsystem/imaterial.h"
#include "lua/materialsystem/limaterial.h"
#include "vgui/IInput.h"
#include "ienginevgui.h"

// memdbgon must be the last include file in a .cpp file!!!
#include "tier0/memdbgon.h"

using namespace vgui;


// ---------------------------------------------------------------------------
// HL2SB: fonts created through GMod's surface.CreateFont( name, fontData ).
//
// GMod keeps a global name -> font table so that surface.SetFont( name ) and
// draw.SimpleText( text, name, ... ) can resolve a font made at runtime.  Its
// engine also lets the font be found by name later.  Ours has no such registry,
// so this file keeps one and surface_SetFont() consults it first.
//
// There is one table per Lua state (client.dll's in-game state and GameUI.dll's
// menu state each have their own), which is what GMod does too -- a font created
// in the menu state is not visible in game and vice versa.
//
// Kept in the Lua registry rather than in a C++ container: it follows the state
// automatically, needs no cleanup at shutdown, and avoids constructing anything
// during DLL load.
// ---------------------------------------------------------------------------
#define LUA_FONTS_REGISTRY_KEY  "hl2sb_lua_fonts"

// Leaves the name -> HFont table on the stack, creating it on first use.
static void LuaFonts_Push (lua_State *L) {
  lua_getfield( L, LUA_REGISTRYINDEX, LUA_FONTS_REGISTRY_KEY );
  if ( !lua_istable( L, -1 ) ) {
    lua_pop( L, 1 );
    lua_newtable( L );
    lua_pushvalue( L, -1 );
    lua_setfield( L, LUA_REGISTRYINDEX, LUA_FONTS_REGISTRY_KEY );
  }
}

// 0 == INVALID_FONT (vgui/VGUI.h) when the name was never created here.
static HFont LuaFont_Find (lua_State *L, const char *szName) {
  if ( L == NULL )
    return 0;

  LuaFonts_Push( L );
  lua_getfield( L, -1, szName );
  HFont hFont = lua_isnumber( L, -1 ) ? (HFont)lua_tointeger( L, -1 ) : 0;
  lua_pop( L, 2 );
  return hFont;
}

static void LuaFont_Store (lua_State *L, const char *szName, HFont hFont) {
  LuaFonts_Push( L );
  lua_pushinteger( L, (lua_Integer)hFont );
  lua_setfield( L, -2, szName );
  lua_pop( L, 1 );
}

// ---------------------------------------------------------------------------
// HL2SB: name -> HFont, the ONE resolver.
//
// GMod's Lua API passes font NAMES to everything that takes a font
// (`self:SetFont( "DermaDefault" )`, surface.DrawSetTextFont( name ), ...),
// while this fork's bindings all demanded an HFont userdata because
// luaL_checkfont() was a bare luaL_checkudata.  That is a whole CLASS of
// "bad argument #1 to 'SetFont' (HFont expected, got string)", not one call
// site: it surfaced first as
//
//     lua/vgui/DTextEntry.lua:60   self:SetFont( "DermaDefault" )
//     Hook 'CreateSpawnMenu' (OnGamemodeLoaded) Failed: ... (HFont expected, got string)
//
// because DTextEntry's base is the engine's TextEntry, whose SetFont
// (game/client/lua/scripted_controls/lTextEntry.cpp:318) was NOT covered by the
// Lua-side wrappers that lua/includes/init.lua installs on the Panel and Label
// metatables.  Fixing it per-class would have needed one wrapper per engine
// class and would have missed the next one; this is the shared funnel instead
// (see luaL_checkfont in LVGUI.cpp), and surface.SetFont routes through it too
// so both spellings resolve identically.
//
// Order, matching surface.SetFont's long-standing behaviour:
//   1. the per-Lua-state registry that surface.CreateFont( name, fontData )
//      fills (LuaFont_Find above) -- this is the cache, and it is exactly what
//      DermaDefault / DermaDefaultBold / DermaLarge / GModNotify land in,
//      because lua/derma/init.lua creates them with the GMod table form;
//   2. the active scheme's font table (that is where "Default",
//      "DefaultSmall", "HL2MPTypeDeath" ... come from, via clientscheme.res);
//   3. the scheme's "Default", so an unknown name degrades to a visible font
//      instead of failing.
// ---------------------------------------------------------------------------
LUA_API lua_HFont LuaFont_ResolveByName (lua_State *L, const char *szName) {
  if ( szName == NULL )
    return 0;

  HFont hFont = LuaFont_Find( L, szName );
  if ( hFont != 0 )
    return hFont;

  vgui::IScheme *pScheme = scheme()->GetIScheme( scheme()->GetDefaultScheme() );
  if ( pScheme == NULL )
    return 0;

  hFont = pScheme->GetFont( szName, false );
  if ( hFont == 0 )
    hFont = pScheme->GetFont( "Default", false );

  return hFont;
}



static int surface_AddBitmapFontFile (lua_State *L) {
  lua_pushboolean(L, surface()->AddBitmapFontFile(luaL_checkstring(L, 1)));
  return 1;
}

// HL2SB: enabled - the sandbox gamemode's CreateDefaultPanels() registers its
// DIN-Light ttf through this, and without it that hook aborts.
static int surface_AddCustomFontFile (lua_State *L) {
  lua_pushboolean(L, surface()->AddCustomFontFile(luaL_checkstring(L, 1), luaL_checkstring(L, 2)));
  return 1;
}

static int surface_AddPanel (lua_State *L) {
  surface()->AddPanel(luaL_checkvpanel(L, 1));
  return 0;
}

static int surface_ApplyChanges (lua_State *L) {
  surface()->ApplyChanges();
  return 0;
}

static int surface_BringToFront (lua_State *L) {
  surface()->BringToFront(luaL_checkvpanel(L, 1));
  return 0;
}

static int surface_CalculateMouseVisible (lua_State *L) {
  surface()->CalculateMouseVisible();
  return 0;
}

static int surface_ClearTemporaryFontCache (lua_State *L) {
  surface()->ClearTemporaryFontCache();
  return 0;
}

// ---------------------------------------------------------------------------
// HL2SB: two calling conventions, matching GMod and the legacy HL2SB one.
//
//   surface.CreateFont( name, fontData )   -- GMod.  fontData is the FontData
//       structure (https://wiki.facepunch.com/gmod/Structures/FontData):
//         font (string, "Arial"), size (13), weight (500), blursize (0),
//         scanlines (0), antialias (true), and the booleans extended /
//         underline / italic / strikeout / symbol / rotary / shadow /
//         additive / outline (all false).
//       The resulting handle is filed under `name` so surface.SetFont( name )
//       and draw.* resolve it.  Returns the HFont as well (GMod returns
//       nothing; handing the handle back is a harmless superset).
//
//   surface.CreateFont()                   -- legacy HL2SB/Experiment form.
//       Returns a bare HFont for the caller to fill in with
//       surface.SetFontGlyphSet().  In-tree Lua still uses it
//       (lua/includes/modules/gmod_vgui.lua), so both forms have to live here.
// ---------------------------------------------------------------------------
static int surface_CreateFont (lua_State *L) {
  if ( !lua_istable( L, 2 ) )
  {
    lua_pushfont( L, surface()->CreateFont() );
    return 1;
  }

  const char *szName = luaL_checkstring( L, 1 );

  lua_getfield( L, 2, "font" );       const char *szFace  = luaL_optstring( L, -1, "Arial" ); lua_pop( L, 1 );
  lua_getfield( L, 2, "size" );       int iTall           = luaL_optint( L, -1, 13 );           lua_pop( L, 1 );
  lua_getfield( L, 2, "weight" );     int iWeight         = luaL_optint( L, -1, 500 );          lua_pop( L, 1 );
  lua_getfield( L, 2, "blursize" );   int iBlur           = luaL_optint( L, -1, 0 );            lua_pop( L, 1 );
  lua_getfield( L, 2, "scanlines" );  int iScanlines      = luaL_optint( L, -1, 0 );            lua_pop( L, 1 );
  lua_getfield( L, 2, "antialias" );  bool bAntialias     = luaL_optboolean( L, -1, true );     lua_pop( L, 1 );
  lua_getfield( L, 2, "extended" );   bool bExtended      = luaL_optboolean( L, -1, false );    lua_pop( L, 1 );
  lua_getfield( L, 2, "underline" );  bool bUnderline     = luaL_optboolean( L, -1, false );    lua_pop( L, 1 );
  lua_getfield( L, 2, "italic" );     bool bItalic        = luaL_optboolean( L, -1, false );    lua_pop( L, 1 );
  lua_getfield( L, 2, "strikeout" );  bool bStrikeout     = luaL_optboolean( L, -1, false );    lua_pop( L, 1 );
  lua_getfield( L, 2, "symbol" );     bool bSymbol        = luaL_optboolean( L, -1, false );    lua_pop( L, 1 );
  lua_getfield( L, 2, "rotary" );     bool bRotary        = luaL_optboolean( L, -1, false );    lua_pop( L, 1 );
  lua_getfield( L, 2, "shadow" );     bool bShadow        = luaL_optboolean( L, -1, false );    lua_pop( L, 1 );
  lua_getfield( L, 2, "additive" );   bool bAdditive      = luaL_optboolean( L, -1, false );    lua_pop( L, 1 );
  lua_getfield( L, 2, "outline" );    bool bOutline       = luaL_optboolean( L, -1, false );    lua_pop( L, 1 );

  int iFlags = ISurface::FONTFLAG_NONE;
  if ( bItalic )    iFlags |= ISurface::FONTFLAG_ITALIC;
  if ( bUnderline ) iFlags |= ISurface::FONTFLAG_UNDERLINE;
  if ( bStrikeout ) iFlags |= ISurface::FONTFLAG_STRIKEOUT;
  if ( bSymbol )    iFlags |= ISurface::FONTFLAG_SYMBOL;
  if ( bAntialias ) iFlags |= ISurface::FONTFLAG_ANTIALIAS;
  if ( bRotary )    iFlags |= ISurface::FONTFLAG_ROTARY;
  if ( bShadow )    iFlags |= ISurface::FONTFLAG_DROPSHADOW;
  if ( bAdditive )  iFlags |= ISurface::FONTFLAG_ADDITIVE;
  if ( bOutline )   iFlags |= ISurface::FONTFLAG_OUTLINE;

  // GMod's "extended" is deliberately NOT translated into an explicit glyph
  // range here, because in this engine the range arguments mean the opposite of
  // what they look like.  vgui2's CFontManager::SetFontGlyphSet()
  // (vgui2/vgui_surfacelib/FontManager.cpp:121-218) builds an AMALGAM:
  //
  //   range 0, 0 (default)  ->  requested face covers 0x0000-0x00FF,
  //                             the foreign fallback face covers 0x0100-0xFFFF
  //   range 0, 0xFFFF       ->  the requested face claims the WHOLE BMP and the
  //                             fallback is never consulted
  //
  // so passing the full range would make e.g. Tahoma claim CJK and render it as
  // missing-glyph boxes -- the exact opposite of "extended".  It also skips the
  // glyph load in FontManager.  A face that is itself foreign-language capable
  // (Microsoft YaHei, ...) gets the full range regardless of these arguments
  // (FontManager.cpp:162-168), so nothing is lost.
  //
  // Net: always 0, 0.  `extended` is accepted and ignored, as GMod code passes
  // it habitually.
  (void)bExtended;

  HFont hFont = surface()->CreateFont();
  if ( !surface()->SetFontGlyphSet( hFont, szFace, iTall, iWeight, iBlur, iScanlines, iFlags ) )
  {
    // The face name did not resolve.  Retry with a face that always exists so
    // the caller still gets a visible font instead of an empty one.
    surface()->SetFontGlyphSet( hFont, "Verdana", iTall, iWeight, iBlur, iScanlines, iFlags );
  }

  LuaFont_Store( L, szName, hFont );

  lua_pushfont( L, hFont );
  return 1;
}

static int surface_CreateNewTextureID (lua_State *L) {
  lua_pushinteger(L, surface()->CreateNewTextureID(luaL_optboolean(L, 1, false)));
  return 1;
}

static int surface_CreatePopup (lua_State *L) {
  surface()->CreatePopup(luaL_checkvpanel(L, 1), luaL_checkboolean(L, 2), luaL_optboolean(L, 3, true), luaL_optboolean(L, 4, false), luaL_optboolean(L, 5, true), luaL_optboolean(L, 6, true));
  return 0;
}

static int surface_DrawFilledRect (lua_State *L) {
  surface()->DrawFilledRect(luaL_checkint(L, 1), luaL_checkint(L, 2), luaL_checkint(L, 3), luaL_checkint(L, 4));
  return 0;
}

static int surface_DrawFilledRectFade (lua_State *L) {
  surface()->DrawFilledRectFade(luaL_checkint(L, 1), luaL_checkint(L, 2), luaL_checkint(L, 3), luaL_checkint(L, 4), luaL_checkint(L, 5), luaL_checkint(L, 6), luaL_checkboolean(L, 7));
  return 0;
}

static int surface_DrawFlushText (lua_State *L) {
  surface()->DrawFlushText();
  return 0;
}

static int surface_DrawGetAlphaMultiplier (lua_State *L) {
  lua_pushnumber(L, surface()->DrawGetAlphaMultiplier());
  return 1;
}

static int surface_DrawGetTextPos (lua_State *L) {
  int x, y;
  surface()->DrawGetTextPos(x, y);
  lua_pushinteger(L, x);
  lua_pushinteger(L, y);
  return 2;
}

static int surface_DrawGetTextureFile (lua_State *L) {
  char * filename = "";
  lua_pushboolean(L, surface()->DrawGetTextureFile(luaL_checkint(L, 1), filename, luaL_checkint(L, 2)));
  lua_pushstring(L, filename);
  return 2;
}

static int surface_DrawGetTextureId (lua_State *L) {
  lua_pushinteger(L, surface()->DrawGetTextureId(luaL_checkstring(L, 1)));
  return 1;
}

static int surface_DrawGetTextureSize (lua_State *L) {
  int wide, tall;
  surface()->DrawGetTextureSize(luaL_checkint(L, 1), wide, tall);
  lua_pushinteger(L, wide);
  lua_pushinteger(L, tall);
  return 2;
}

static int surface_DrawLine (lua_State *L) {
  surface()->DrawLine(luaL_checkint(L, 1), luaL_checkint(L, 2), luaL_checkint(L, 3), luaL_checkint(L, 4));
  return 0;
}

static int surface_DrawOutlinedCircle (lua_State *L) {
  surface()->DrawOutlinedCircle(luaL_checkint(L, 1), luaL_checkint(L, 2), luaL_checkint(L, 3), luaL_checkint(L, 4));
  return 0;
}

static int surface_DrawOutlinedRect (lua_State *L) {
  surface()->DrawOutlinedRect(luaL_checkint(L, 1), luaL_checkint(L, 2), luaL_checkint(L, 3), luaL_checkint(L, 4));
  return 0;
}

static int surface_DrawPrintText (lua_State *L) {
  const char *sz = luaL_checkstring(L, 1);
  int bufSize = (strlen( sz ) + 1 ) * sizeof(wchar_t);
  wchar_t *wbuf = static_cast<wchar_t *>( _alloca( bufSize ) );
  if ( wbuf )
  {
	  g_pVGuiLocalize->ConvertANSIToUnicode( sz, wbuf, bufSize);
	  surface()->DrawPrintText( wbuf, wcslen( wbuf ), (FontDrawType_t)luaL_optint(L, 3, FONT_DRAW_DEFAULT));
  }
  return 0;
}

static int surface_DrawSetAlphaMultiplier (lua_State *L) {
  surface()->DrawSetAlphaMultiplier(luaL_checknumber(L, 1));
  return 0;
}

static int surface_DrawSetColor (lua_State *L) {
  surface()->DrawSetColor(luaL_checkint(L, 1), luaL_checkint(L, 2), luaL_checkint(L, 3), luaL_checkint(L, 4));
  return 0;
}

static int surface_DrawSetTextColor (lua_State *L) {
  surface()->DrawSetTextColor(luaL_checkint(L, 1), luaL_checkint(L, 2), luaL_checkint(L, 3), luaL_checkint(L, 4));
  return 0;
}

static int surface_DrawSetTextFont (lua_State *L) {
  surface()->DrawSetTextFont(luaL_checkfont(L, 1));
  return 0;
}

static int surface_DrawSetTextPos (lua_State *L) {
  surface()->DrawSetTextPos(luaL_checkint(L, 1), luaL_checkint(L, 2));
  return 0;
}

static int surface_DrawSetTextScale (lua_State *L) {
  surface()->DrawSetTextScale(luaL_checknumber(L, 1), luaL_checknumber(L, 2));
  return 0;
}

static int surface_DrawSetTexture (lua_State *L) {
  surface()->DrawSetTexture(luaL_checkint(L, 1));
  return 0;
}

// GMod: surface.SetMaterial( material ) -- use a real IMaterial for the
// following DrawTexturedRect* calls.  That is how the Derma skin
// (lua/derma/derma_gwen.lua -> GWEN.CreateTextureBorder) and killicon.lua draw.
//
// ISurface has no DrawSetTextureMaterial() in this engine's public interface (it
// lives on IMatSystemSurface, and extending the ISurface vtable would break the
// prebuilt engine.dll), so bind by name: DrawSetTextureFile() resolves the name
// through FindMaterial() and hands the dictionary slot the very material we were
// passed.  One slot is reused for the life of the process, so painting every
// frame does not leak texture ids.
// HL2SB: the vgui texture slot used by surface.SetMaterial is created in
// luaopen_surface(), NOT here: creating it lazily means doing it mid-frame while
// a Derma panel paints (the undo notice is the first one that paints through the
// skin), and creating surface resources inside the render context is where this
// build faults.  The fallback below only covers "library open did not run".
static int s_nMaterialDrawTextureID = -1;

static int surface_SetMaterial (lua_State *L) {
  int &nMaterialDrawTextureID = s_nMaterialDrawTextureID;

  if ( nMaterialDrawTextureID == -1 ) {
    nMaterialDrawTextureID = surface()->CreateNewTextureID();
  }

  IMaterial *pMaterial = luaL_checkmaterial(L, 1);

  // NOTE: IMatSystemSurface::DrawSetTextureMaterial would be the direct call, but
  // going through the g_pMatSystemSurface global from client.dll access-violates
  // this fork (the deployed vgui2/MatSystemSurface is an older interface version,
  // so the virtual slot does not match) -- verified with a minidump.  Bind by
  // name instead: DrawSetTextureFile resolves the material through FindMaterial
  // and hands that very material to the dictionary slot.
  surface()->DrawSetTextureFile( nMaterialDrawTextureID, pMaterial->GetName(), true, false );
  surface()->DrawSetTexture( nMaterialDrawTextureID );
  return 0;
}

// GMod: DisableClipping( bDisable ) -> boolean (the PREVIOUS state).
//
// lua/skins/default.lua:344 draws the DFrame shadow outside the panel with
//
//     local wasEnabled = DisableClipping( true )
//     self.tex.Shadow( -4, -4, w + 10, h + 10 )
//     DisableClipping( wasEnabled )
//
// so the return value matters.  The engine side of this is
// IMatSystemSurface::DisableClipping, and reaching it through the
// g_pMatSystemSurface global from client.dll access-violates (older interface
// version in the deployed vgui2 -- same crash as DrawSetTextureMaterial above),
// so the state is tracked here and reported back faithfully.  The only casualty
// is that the 10px shadow bleed gets clipped at the frame's bounds.
static int HL2SB_DisableClipping (lua_State *L) {
  static bool s_bClippingDisabled = false;

  bool bDisable = luaL_checkboolean(L, 1);
  bool bWasDisabled = s_bClippingDisabled;

  s_bClippingDisabled = bDisable;
  lua_pushboolean(L, bWasDisabled ? 1 : 0);
  return 1;
}

// ---------------------------------------------------------------------------
// HL2SB: GMod's gui.* library (client only).  See luaopen_surface.
// ---------------------------------------------------------------------------
static void HL2SB_GetCursorPos (int &x, int &y) {
  x = y = 0;

  if ( vgui::input() != NULL ) {
    vgui::input()->GetCursorPos(x, y);
  }
}

static int HL2SB_gui_MouseX (lua_State *L) {
  int x = 0, y = 0;
  HL2SB_GetCursorPos(x, y);
  lua_pushinteger(L, x);
  return 1;
}

static int HL2SB_gui_MouseY (lua_State *L) {
  int x = 0, y = 0;
  HL2SB_GetCursorPos(x, y);
  lua_pushinteger(L, y);
  return 1;
}

static int HL2SB_gui_ScreenWidth (lua_State *L) {
  int w = 0, h = 0;

  if ( surface() != NULL ) {
    surface()->GetScreenSize(w, h);
  }

  lua_pushinteger(L, w);
  return 1;
}

static int HL2SB_gui_ScreenHeight (lua_State *L) {
  int w = 0, h = 0;

  if ( surface() != NULL ) {
    surface()->GetScreenSize(w, h);
  }

  lua_pushinteger(L, h);
  return 1;
}

static int HL2SB_gui_IsConsoleVisible (lua_State *L) {
  // GMod scripts use this to stop drawing while the console is up; the engine
  // console's visibility is not exposed through ISurface, so answer "no".
  lua_pushboolean(L, 0);
  return 1;
}

static int HL2SB_gui_IsGameUIVisible (lua_State *L) {
  lua_pushboolean(L, (enginevgui != NULL && enginevgui->IsGameUIVisible()) ? 1 : 0);
  return 1;
}

static int surface_DrawSetTextureFile (lua_State *L) {
  surface()->DrawSetTextureFile(luaL_checkint(L, 1), luaL_checkstring(L, 2), luaL_checkint(L, 3), luaL_checkboolean(L, 4));
  return 0;
}

static int surface_DrawTexturedRect (lua_State *L) {
  surface()->DrawTexturedRect(luaL_checkint(L, 1), luaL_checkint(L, 2), luaL_checkint(L, 3), luaL_checkint(L, 4));
  return 0;
}

static int surface_DrawTexturedSubRect (lua_State *L) {
  surface()->DrawTexturedSubRect(luaL_checkint(L, 1), luaL_checkint(L, 2), luaL_checkint(L, 3), luaL_checkint(L, 4), luaL_checknumber(L, 5), luaL_checknumber(L, 6), luaL_checknumber(L, 7), luaL_checknumber(L, 8));
  return 0;
}

// ---------------------------------------------------------------------------
// HL2SB: GMod's surface.DrawTexturedRectUV( x, y, w, h, u0, v0, u1, v1 ).
//
// The stock binding DrawTexturedSubRect() takes the far CORNER instead of a
// size, and its texture coordinates are 0..1 normalised (vguimatsurface's
// CMatSystemSurface::DrawTexturedSubRect rescales them into the bound texture's
// real range), which is exactly what GMod's version means -- so this is only an
// argument-shape translation.  draw.RoundedBox() builds its corners with it.
// ---------------------------------------------------------------------------
static int surface_DrawTexturedRectUV (lua_State *L) {
  int x = luaL_checkint(L, 1);
  int y = luaL_checkint(L, 2);
  int w = luaL_checkint(L, 3);
  int h = luaL_checkint(L, 4);
  surface()->DrawTexturedSubRect(x, y, x + w, y + h, luaL_checknumber(L, 5), luaL_checknumber(L, 6), luaL_checknumber(L, 7), luaL_checknumber(L, 8));
  return 0;
}

// ---------------------------------------------------------------------------
// HL2SB: GMod's surface.DrawTexturedRectRotated( x, y, w, h, rotation ).
//
//   * x, y are the CENTRE of the rectangle, not its top-left corner.
//   * rotation is in degrees.
//     (https://wiki.facepunch.com/gmod/surface.DrawTexturedRectRotated)
//
// GMod's engine gained a real ISurface entry point for this; Source 2013's
// vgui::ISurface does not have one.  It does, however, expose
// ISurface::DrawTexturedPolygon(), so the quad is built here and handed to it
// -- no interface change and no vguimatsurface/engine rebuild, the binding
// lives entirely in client.dll and GameUI.dll.
//
// Direction: positive rotation is counter-clockwise on screen (y grows down).
// That is not a guess -- GMod's own HUD depends on it.  cl_hudpickup.lua blits
// the same gui/corner8 four times at 0/90/180/270 and the rounding only lands in
// the correct screen corner with this sign; it is the same mapping
// draw.RoundedBoxEx() gets from flipping its UVs
// (TL 0,0,1,1 / TR 1,0,0,1 / BL 0,1,1,0 / BR 1,1,0,0).
//
// Texture coordinates are 0..1 from corner to corner.  That is correct here
// because CTextureDictionary::GetTextureTexCoords() only deviates from 0..1 for
// fonts, which live in a shared page and are never drawn through this path.
// ---------------------------------------------------------------------------
static int surface_DrawTexturedRectRotated (lua_State *L) {
  const float flX  = (float)luaL_checknumber(L, 1);
  const float flY  = (float)luaL_checknumber(L, 2);
  const float flW  = (float)luaL_checknumber(L, 3);
  const float flH  = (float)luaL_checknumber(L, 4);
  const float flRot = (float)luaL_checknumber(L, 5);

  const float flRad = flRot * 0.017453292519943295f;   // degrees -> radians
  const float flCos = cosf(flRad);
  const float flSin = sinf(flRad);

  const float flHW = flW * 0.5f;
  const float flHH = flH * 0.5f;

  // Local corner offsets and their texture coordinates, clockwise from top-left.
  static const float pflCX[4] = { -1.0f,  1.0f,  1.0f, -1.0f };
  static const float pflCY[4] = { -1.0f, -1.0f,  1.0f,  1.0f };
  static const float pflCU[4] = {  0.0f,  1.0f,  1.0f,  0.0f };
  static const float pflCV[4] = {  0.0f,  0.0f,  1.0f,  1.0f };

  Vertex_t verts[4];
  for ( int i = 0; i < 4; ++i )
  {
    const float flPX = pflCX[i] * flHW;
    const float flPY = pflCY[i] * flHH;

    verts[i].m_Position.x = flX + ( flPX * flCos + flPY * flSin );
    verts[i].m_Position.y = flY + ( -flPX * flSin + flPY * flCos );
    verts[i].m_TexCoord.x = pflCU[i];
    verts[i].m_TexCoord.y = pflCV[i];
  }

  surface()->DrawTexturedPolygon( 4, verts, true );
  return 0;
}

static int surface_EnableMouseCapture (lua_State *L) {
  surface()->EnableMouseCapture(luaL_checkvpanel(L, 1), luaL_checkboolean(L, 2));
  return 0;
}

static int surface_FlashWindow (lua_State *L) {
  surface()->FlashWindow(luaL_checkvpanel(L, 1), luaL_checkboolean(L, 2));
  return 0;
}

static int surface_GetAbsoluteWindowBounds (lua_State *L) {
  int x, y, wide, tall;
  surface()->GetAbsoluteWindowBounds(x, y, wide, tall);
  lua_pushinteger(L, x);
  lua_pushinteger(L, y);
  lua_pushinteger(L, wide);
  lua_pushinteger(L, tall);
  return 4;
}

static int surface_GetBitmapFontName (lua_State *L) {
  lua_pushstring(L, surface()->GetBitmapFontName(luaL_checkstring(L, 1)));
  return 1;
}

static int surface_GetCharABCwide (lua_State *L) {
  int a, b, c;
  surface()->GetCharABCwide(luaL_checkfont(L, 1), luaL_checkint(L, 2), a, b, c);
  lua_pushinteger(L, a);
  lua_pushinteger(L, b);
  lua_pushinteger(L, c);
  return 3;
}

static int surface_GetCharacterWidth (lua_State *L) {
  lua_pushinteger(L, surface()->GetCharacterWidth(luaL_checkfont(L, 1), luaL_checkint(L, 2)));
  return 1;
}

static int surface_GetEmbeddedPanel (lua_State *L) {
  lua_pushpanel(L, surface()->GetEmbeddedPanel());
  return 1;
}

static int surface_GetFontAscent (lua_State *L) {
  wchar_t wch[1];
  g_pVGuiLocalize->ConvertANSIToUnicode(luaL_checkstring(L, 2), wch, sizeof(wch));
  lua_pushinteger(L, surface()->GetFontAscent(luaL_checkfont(L, 1), wch[1]));
  return 1;
}

static int surface_GetFontTall (lua_State *L) {
  lua_pushinteger(L, surface()->GetFontTall(luaL_checkfont(L, 1)));
  return 1;
}

static int surface_GetModalPanel (lua_State *L) {
  lua_pushpanel(L, surface()->GetModalPanel());
  return 1;
}

static int surface_GetNotifyPanel (lua_State *L) {
  lua_pushpanel(L, surface()->GetNotifyPanel());
  return 1;
}

static int surface_GetPopup (lua_State *L) {
  lua_pushpanel(L, surface()->GetPopup(luaL_checkint(L, 1)));
  return 1;
}

static int surface_GetPopupCount (lua_State *L) {
  lua_pushinteger(L, surface()->GetPopupCount());
  return 1;
}

static int surface_GetProportionalBase (lua_State *L) {
  int width, height;
  surface()->GetProportionalBase(width, height);
  lua_pushinteger(L, width);
  lua_pushinteger(L, height);
  return 2;
}

static int surface_GetResolutionKey (lua_State *L) {
  lua_pushstring(L, surface()->GetResolutionKey());
  return 1;
}

static int surface_GetScreenSize (lua_State *L) {
  int wide, tall;
  surface()->GetScreenSize(wide, tall);
  lua_pushinteger(L, wide);
  lua_pushinteger(L, tall);
  return 2;
}

static int surface_GetTextSize (lua_State *L) {
  const char *sz = luaL_checkstring(L, 2);
  int wide = 0;
  int tall = 0;
  int bufSize = (strlen( sz ) + 1 ) * sizeof(wchar_t);
  wchar_t *wbuf = static_cast<wchar_t *>( _alloca( bufSize ) );
  if ( wbuf )
  {
	  // HL2SB: this conversion was missing, so GetTextSize measured an
	  // uninitialised stack buffer and returned a garbage width.  Every Lua
	  // layout that measures text (the kill feed especially) was positioned
	  // from those bogus values.
	  g_pVGuiLocalize->ConvertANSIToUnicode( sz, wbuf, bufSize );
	  surface()->GetTextSize(luaL_checkfont(L, 1), wbuf, wide, tall);
  }
  lua_pushinteger(L, wide);
  lua_pushinteger(L, tall);
  return 2;
}

static int surface_GetTitle (lua_State *L) {
  char szTitle[256];
  const wchar_t *wszTitle = surface()->GetTitle(luaL_checkvpanel(L, 1));
  g_pVGuiLocalize->ConvertUnicodeToANSI( wszTitle, szTitle, sizeof( szTitle ) );
  lua_pushstring(L, szTitle);
  return 1;
}

static int surface_GetTopmostPopup (lua_State *L) {
  lua_pushpanel(L, surface()->GetTopmostPopup());
  return 1;
}

static int surface_GetWorkspaceBounds (lua_State *L) {
  int x, y, wide, tall;
  surface()->GetWorkspaceBounds(x, y, wide, tall);
  lua_pushinteger(L, x);
  lua_pushinteger(L, y);
  lua_pushinteger(L, wide);
  lua_pushinteger(L, tall);
  return 4;
}

static int surface_GetZPos (lua_State *L) {
  lua_pushnumber(L, surface()->GetZPos());
  return 1;
}

static int surface_HasCursorPosFunctions (lua_State *L) {
  lua_pushboolean(L, surface()->HasCursorPosFunctions());
  return 1;
}

static int surface_HasFocus (lua_State *L) {
  lua_pushboolean(L, surface()->HasFocus());
  return 1;
}

static int surface_Invalidate (lua_State *L) {
  surface()->Invalidate(luaL_checkvpanel(L, 1));
  return 0;
}

static int surface_IsCursorLocked (lua_State *L) {
  lua_pushboolean(L, surface()->IsCursorLocked());
  return 1;
}

static int surface_IsCursorVisible (lua_State *L) {
  lua_pushboolean(L, surface()->IsCursorVisible());
  return 1;
}

static int surface_IsFontAdditive (lua_State *L) {
  lua_pushboolean(L, surface()->IsFontAdditive(luaL_checkfont(L, 1)));
  return 1;
}

static int surface_IsMinimized (lua_State *L) {
  lua_pushboolean(L, surface()->IsMinimized(luaL_checkvpanel(L, 1)));
  return 1;
}

static int surface_IsTextureIDValid (lua_State *L) {
  lua_pushboolean(L, surface()->IsTextureIDValid(luaL_checkint(L, 1)));
  return 1;
}

static int surface_IsWithin (lua_State *L) {
  lua_pushboolean(L, surface()->IsWithin(luaL_checkint(L, 1), luaL_checkint(L, 2)));
  return 1;
}

static int surface_LockCursor (lua_State *L) {
  surface()->LockCursor();
  return 0;
}

static int surface_MovePopupToBack (lua_State *L) {
  surface()->MovePopupToBack(luaL_checkvpanel(L, 1));
  return 0;
}

static int surface_MovePopupToFront (lua_State *L) {
  surface()->MovePopupToFront(luaL_checkvpanel(L, 1));
  return 0;
}

static int surface_NeedKBInput (lua_State *L) {
  lua_pushboolean(L, surface()->NeedKBInput());
  return 1;
}

static int surface_OnScreenSizeChanged (lua_State *L) {
  surface()->OnScreenSizeChanged(luaL_checkint(L, 1), luaL_checkint(L, 2));
  return 0;
}

static int surface_PaintTraverse (lua_State *L) {
  surface()->PaintTraverse(luaL_checkvpanel(L, 1));
  return 0;
}

static int surface_PaintTraverseEx (lua_State *L) {
  surface()->PaintTraverseEx(luaL_checkvpanel(L, 1), luaL_optboolean(L, 2, false));
  return 0;
}

static int surface_PlaySound (lua_State *L) {
  surface()->PlaySound(luaL_checkstring(L, 1));
  return 0;
}

static int surface_PopMakeCurrent (lua_State *L) {
  surface()->PopMakeCurrent(luaL_checkvpanel(L, 1));
  return 0;
}

static int surface_RunFrame (lua_State *L) {
  surface()->RunFrame();
  return 0;
}

static int surface_SetAllowHTMLJavaScript (lua_State *L) {
  surface()->SetAllowHTMLJavaScript(luaL_checkboolean(L, 1));
  return 0;
}

static int surface_SetBitmapFontName (lua_State *L) {
  surface()->SetBitmapFontName(luaL_checkstring(L, 1), luaL_checkstring(L, 2));
  return 0;
}

// HL2SB: GMod-style SetFont by string name.  GMod scripts (and the ported
// draw.lua) call surface.SetFont("DermaDefault") / surface.SetFont("Default")
// where the argument is a face NAME, not an HFont handle.  The stock binding
// only has SetFontGlyphSet(hfont,...).  We resolve the name through the default
// scheme to an HFont, then select it for text drawing.  Falls back to the
// scheme's "Default" font when the name is unknown (HL2SB's clientscheme only
// defines Default / DefaultSmall / DefaultVerySmall).
static int surface_SetFont (lua_State *L) {
  const char *szName = luaL_checkstring(L, 1);

  // HL2SB: the shared resolver (LuaFont_ResolveByName above) -- registry first
  // (fonts made at runtime with the GMod form of surface.CreateFont must win
  // over the scheme, which cannot know about them), then the scheme, then the
  // scheme's "Default".
  HFont hFont = LuaFont_ResolveByName( L, szName );

  if ( hFont != 0 )
  {
    surface()->DrawSetTextFont( hFont );
  }

  // Push the resolved HFont back so a Lua caller can cache it.
  lua_pushfont( L, hFont );
  return 1;
}

static int surface_SetEmbeddedPanel (lua_State *L) {
  surface()->SetEmbeddedPanel(luaL_checkvpanel(L, 1));
  return 0;
}

static int surface_SetFontGlyphSet (lua_State *L) {
  lua_pushboolean(L, surface()->SetFontGlyphSet(luaL_checkfont(L, 1), luaL_checkstring(L, 2), luaL_checkint(L, 3), luaL_checkint(L, 4), luaL_checkint(L, 5), luaL_checkint(L, 6), luaL_checkint(L, 7), luaL_optint(L, 8, 0), luaL_optint(L, 9, 0)));
  return 1;
}

static int surface_SetTranslateExtendedKeys (lua_State *L) {
  surface()->SetTranslateExtendedKeys(luaL_checkboolean(L, 1));
  return 0;
}

static int surface_SetWorkspaceInsets (lua_State *L) {
  surface()->SetWorkspaceInsets(luaL_checkint(L, 1), luaL_checkint(L, 2), luaL_checkint(L, 3), luaL_checkint(L, 4));
  return 0;
}

static int surface_SupportsFeature (lua_State *L) {
  surface()->SupportsFeature((ISurface::SurfaceFeature_e)luaL_checkint(L, 1));
  return 0;
}

static int surface_SurfaceGetCursorPos (lua_State *L) {
  int x, y;
  surface()->SurfaceGetCursorPos(x, y);
  lua_pushinteger(L, x);
  lua_pushinteger(L, y);
  return 2;
}

static int surface_SurfaceSetCursorPos (lua_State *L) {
  surface()->SurfaceSetCursorPos(luaL_checkint(L, 1), luaL_checkint(L, 2));
  return 0;
}

static int surface_UnlockCursor (lua_State *L) {
  surface()->UnlockCursor();
  return 0;
}


static const luaL_Reg surfacelib[] = {
  {"AddBitmapFontFile",   surface_AddBitmapFontFile},
  {"AddCustomFontFile",   surface_AddCustomFontFile},
  {"AddPanel",   surface_AddPanel},
  {"ApplyChanges",   surface_ApplyChanges},
  {"BringToFront",   surface_BringToFront},
  {"CalculateMouseVisible",   surface_CalculateMouseVisible},
  {"ClearTemporaryFontCache",   surface_ClearTemporaryFontCache},
  {"CreateFont",   surface_CreateFont},
  {"CreateNewTextureID",   surface_CreateNewTextureID},
  {"CreatePopup",   surface_CreatePopup},
  {"DrawFilledRect",   surface_DrawFilledRect},
  {"DrawFilledRectFade",   surface_DrawFilledRectFade},
  {"DrawFlushText",   surface_DrawFlushText},
  {"DrawGetAlphaMultiplier",   surface_DrawGetAlphaMultiplier},
  {"DrawGetTextPos",   surface_DrawGetTextPos},
  {"DrawGetTextureFile",   surface_DrawGetTextureFile},
  {"DrawGetTextureId",   surface_DrawGetTextureId},
  {"DrawGetTextureSize",   surface_DrawGetTextureSize},
  {"DrawLine",   surface_DrawLine},
  {"DrawOutlinedCircle",   surface_DrawOutlinedCircle},
  {"DrawOutlinedRect",   surface_DrawOutlinedRect},
  {"DrawPrintText",   surface_DrawPrintText},
  {"DrawSetAlphaMultiplier",   surface_DrawSetAlphaMultiplier},
  {"DrawSetColor",   surface_DrawSetColor},
  {"DrawSetTextColor",   surface_DrawSetTextColor},
  {"DrawSetTextFont",   surface_DrawSetTextFont},
  {"DrawSetTextPos",   surface_DrawSetTextPos},
  {"DrawSetTextScale",   surface_DrawSetTextScale},
  {"DrawSetTexture",   surface_DrawSetTexture},
  {"DrawSetTextureFile",   surface_DrawSetTextureFile},
  {"DrawTexturedRect",   surface_DrawTexturedRect},
  {"DrawTexturedSubRect",   surface_DrawTexturedSubRect},
  // HL2SB: GMod spellings.  Both are pure translations onto the stock bindings
  // (DrawTexturedSubRect / DrawTexturedPolygon), added so GMod's own draw.lua
  // and cl_hudpickup.lua run unmodified.
  {"DrawTexturedRectUV",   surface_DrawTexturedRectUV},
  {"DrawTexturedRectRotated",   surface_DrawTexturedRectRotated},
  {"EnableMouseCapture",   surface_EnableMouseCapture},
  {"FlashWindow",   surface_FlashWindow},
  {"GetAbsoluteWindowBounds",   surface_GetAbsoluteWindowBounds},
  {"GetBitmapFontName",   surface_GetBitmapFontName},
  {"GetCharABCwide",   surface_GetCharABCwide},
  {"GetCharacterWidth",   surface_GetCharacterWidth},
  {"GetEmbeddedPanel",   surface_GetEmbeddedPanel},
  {"GetFontAscent",   surface_GetFontAscent},
  {"GetFontTall",   surface_GetFontTall},
  {"GetModalPanel",   surface_GetModalPanel},
  {"GetNotifyPanel",   surface_GetNotifyPanel},
  {"GetPopup",   surface_GetPopup},
  {"GetPopupCount",   surface_GetPopupCount},
  {"GetProportionalBase",   surface_GetProportionalBase},
  {"GetResolutionKey",   surface_GetResolutionKey},
  {"GetScreenSize",   surface_GetScreenSize},
  {"GetTextSize",   surface_GetTextSize},
  {"GetTitle",   surface_GetTitle},
  {"GetTopmostPopup",   surface_GetTopmostPopup},
  {"GetWorkspaceBounds",   surface_GetWorkspaceBounds},
  {"GetZPos",   surface_GetZPos},
  {"HasCursorPosFunctions",   surface_HasCursorPosFunctions},
  {"HasFocus",   surface_HasFocus},
  {"Invalidate",   surface_Invalidate},
  {"IsCursorLocked",   surface_IsCursorLocked},
  {"IsCursorVisible",   surface_IsCursorVisible},
  {"IsFontAdditive",   surface_IsFontAdditive},
  {"IsMinimized",   surface_IsMinimized},
  {"IsTextureIDValid",   surface_IsTextureIDValid},
  {"IsWithin",   surface_IsWithin},
  {"LockCursor",   surface_LockCursor},
  {"MovePopupToBack",   surface_MovePopupToBack},
  {"MovePopupToFront",   surface_MovePopupToFront},
  {"NeedKBInput",   surface_NeedKBInput},
  {"OnScreenSizeChanged",   surface_OnScreenSizeChanged},
  {"PaintTraverse",   surface_PaintTraverse},
  {"PaintTraverseEx",   surface_PaintTraverseEx},
  {"PlaySound",   surface_PlaySound},
  {"PopMakeCurrent",   surface_PopMakeCurrent},
  {"RunFrame",   surface_RunFrame},
  {"SetAllowHTMLJavaScript",   surface_SetAllowHTMLJavaScript},
  {"SetBitmapFontName",   surface_SetBitmapFontName},
  {"SetFont",   surface_SetFont},
  {"SetEmbeddedPanel",   surface_SetEmbeddedPanel},
  {"SetFontGlyphSet",   surface_SetFontGlyphSet},
  {"SetMaterial",   surface_SetMaterial},
  {"SetTranslateExtendedKeys",   surface_SetTranslateExtendedKeys},
  {"SetWorkspaceInsets",   surface_SetWorkspaceInsets},
  {"SupportsFeature",   surface_SupportsFeature},
  {"SurfaceGetCursorPos",   surface_SurfaceGetCursorPos},
  {"SurfaceSetCursorPos",   surface_SurfaceSetCursorPos},
  {"UnlockCursor",   surface_UnlockCursor},
  {NULL, NULL}
};


/*
** Open surface library
*/
LUALIB_API int luaopen_surface (lua_State *L) {
  luaL_register(L, LUA_SURFACELIBNAME, surfacelib);

  // HL2SB: create the surface.SetMaterial texture slot here, outside of any paint.
  if ( s_nMaterialDrawTextureID == -1 ) {
    s_nMaterialDrawTextureID = surface()->CreateNewTextureID();
  }

  // HL2SB: GMod's client global that lives on IMatSystemSurface.
  lua_pushcfunction(L, HL2SB_DisableClipping);
  lua_setglobal(L, "DisableClipping");

  // HL2SB: GMod's gui.* library.
  //
  // lua/vgui/DFrame.lua:218 (DFrame:OnMousePressed) calls gui.MouseX()/MouseY()
  // to start a drag, so a DFrame could not be dragged at all:
  //
  //   lua/vgui/DFrame.lua:218: attempt to index a nil value (global 'gui')
  //
  // Nothing else in this engine owns the name (the error above proves it was
  // nil), so the table is built here, next to the other client-only globals.
  lua_newtable(L);
  lua_pushcfunction(L, HL2SB_gui_MouseX);
  lua_setfield(L, -2, "MouseX");
  lua_pushcfunction(L, HL2SB_gui_MouseY);
  lua_setfield(L, -2, "MouseY");
  lua_pushcfunction(L, HL2SB_gui_ScreenWidth);
  lua_setfield(L, -2, "ScreenWidth");
  lua_pushcfunction(L, HL2SB_gui_ScreenHeight);
  lua_setfield(L, -2, "ScreenHeight");
  lua_pushcfunction(L, HL2SB_gui_IsConsoleVisible);
  lua_setfield(L, -2, "IsConsoleVisible");
  lua_pushcfunction(L, HL2SB_gui_IsGameUIVisible);
  lua_setfield(L, -2, "IsGameUIVisible");
  lua_setglobal(L, "gui");

  return 1;
}


/*
** Open FONTFLAG library
*/
LUALIB_API int luaopen_FONTFLAG (lua_State *L) {
  BEGIN_LUA_SET_ENUM_LIB(L, LUA_FONTFLAGLIBNAME);
    lua_pushenum(L, vgui::ISurface::FONTFLAG_NONE, "NONE");
    lua_pushenum(L, vgui::ISurface::FONTFLAG_ITALIC, "ITALIC");
    lua_pushenum(L, vgui::ISurface::FONTFLAG_UNDERLINE, "UNDERLINE");
    lua_pushenum(L, vgui::ISurface::FONTFLAG_STRIKEOUT, "STRIKEOUT");
    lua_pushenum(L, vgui::ISurface::FONTFLAG_SYMBOL, "SYMBOL");
    lua_pushenum(L, vgui::ISurface::FONTFLAG_ANTIALIAS, "ANTIALIAS");
    lua_pushenum(L, vgui::ISurface::FONTFLAG_GAUSSIANBLUR, "GAUSSIANBLUR");
    lua_pushenum(L, vgui::ISurface::FONTFLAG_ROTARY, "ROTARY");
    lua_pushenum(L, vgui::ISurface::FONTFLAG_DROPSHADOW, "DROPSHADOW");
    lua_pushenum(L, vgui::ISurface::FONTFLAG_ADDITIVE, "ADDITIVE");
    lua_pushenum(L, vgui::ISurface::FONTFLAG_OUTLINE, "OUTLINE");
    lua_pushenum(L, vgui::ISurface::FONTFLAG_CUSTOM, "CUSTOM");
    lua_pushenum(L, vgui::ISurface::FONTFLAG_BITMAP, "BITMAP");
  END_LUA_SET_ENUM_LIB(L);
  return 0;
}

