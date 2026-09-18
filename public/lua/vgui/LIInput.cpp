//===== Copyright © 1996-2005, Valve Corporation, All rights reserved. ======//
//
// Purpose: 
//
// $NoKeywords: $
//===========================================================================//

#define LIInput_cpp

#include <cbase.h>
#include "vgui/IInput.h"
#include "vgui_controls/Controls.h"
#include <luamanager.h>
#include "luasrclib.h"
#include "vgui_controls/lPanel.h"

// memdbgon must be the last include file in a .cpp file!!!
#include "tier0/memdbgon.h"

using namespace vgui;


static int input_CandidateListStartsAtOne (lua_State *L) {
  lua_pushboolean(L, input()->CandidateListStartsAtOne());
  return 1;
}

static int input_GetAppModalSurface (lua_State *L) {
  lua_pushpanel(L, input()->GetAppModalSurface());
  return 1;
}

static int input_GetCandidateListCount (lua_State *L) {
  lua_pushinteger(L, input()->GetCandidateListCount());
  return 1;
}

static int input_GetCandidateListPageSize (lua_State *L) {
  lua_pushinteger(L, input()->GetCandidateListPageSize());
  return 1;
}

static int input_GetCandidateListPageStart (lua_State *L) {
  lua_pushinteger(L, input()->GetCandidateListPageStart());
  return 1;
}

static int input_GetCandidateListSelectedItem (lua_State *L) {
  lua_pushinteger(L, input()->GetCandidateListSelectedItem());
  return 1;
}

static int input_GetCurrentIMEHandle (lua_State *L) {
  lua_pushinteger(L, input()->GetCurrentIMEHandle());
  return 1;
}

// HL2SB: GMod's input.GetCursorPos -- this was the ONE binding the GMod spawn
// menu cannot open without.
//
// The block below used to be commented out because it called
// input()->GetCursorPos__USE_VCR_MODE(), which this fork's IInput never
// declared (public/vgui/IInput.h only has GetCursorPos at :62).  The name was
// a GMod-ism; the plain one is what exists here.
//
// Why it is load-critical: GMod's lua/includes/util.lua:520 defines
//
//     function RememberCursorPosition()  local x, y = input.GetCursorPos() ...
//     function RestoreCursorPosition()   input.SetCursorPos( x, y ) ...
//
// and gamemodes/sandbox/gamemode/spawnmenu/spawnmenu.lua:90 calls
// RestoreCursorPosition() as the FIRST statement of PANEL:Open().  A nil
// input.GetCursorPos made RememberCursorPosition throw, so PANEL:Open() aborted
// before self:SetVisible( true ) / self:MakePopup() ever ran -- "+menu" would
// appear to do nothing at all.  (RememberCursorPosition is called from
// spawnmenu.lua:126 and contextmenu.lua:74 in the same way.)
static int input_GetCursorPos (lua_State *L) {
  int x, y;
  input()->GetCursorPos(x, y);
  lua_pushinteger(L, x);
  lua_pushinteger(L, y);
  return 2;
}

static int input_GetCursorPosition (lua_State *L) {
  int x, y;
  input()->GetCursorPosition(x, y);
  lua_pushinteger(L, x);
  lua_pushinteger(L, y);
  return 2;
}

static int input_GetEnglishIMEHandle (lua_State *L) {
  lua_pushinteger(L, input()->GetEnglishIMEHandle());
  return 1;
}

static int input_GetFocus (lua_State *L) {
  lua_pushpanel(L, input()->GetFocus());
  return 1;
}

static int input_GetModalSubTree (lua_State *L) {
  lua_pushpanel(L, input()->GetModalSubTree());
  return 1;
}

static int input_GetMouseCapture (lua_State *L) {
  lua_pushpanel(L, input()->GetMouseCapture());
  return 1;
}

static int input_GetMouseOver (lua_State *L) {
  lua_pushpanel(L, input()->GetMouseOver());
  return 1;
}

static int input_GetShouldInvertCompositionString (lua_State *L) {
  lua_pushboolean(L, input()->GetShouldInvertCompositionString());
  return 1;
}

static int input_IsKeyDown (lua_State *L) {
  lua_pushboolean(L, input()->IsKeyDown((KeyCode)luaL_checkint(L, 1)));
  return 1;
}

static int input_IsMouseDown (lua_State *L) {
  lua_pushboolean(L, input()->IsMouseDown((KeyCode)luaL_checkint(L, 1)));
  return 1;
}

static int input_OnChangeIME (lua_State *L) {
  input()->OnChangeIME(luaL_checkboolean(L, 1));
  return 0;
}

static int input_OnChangeIMEByHandle (lua_State *L) {
  input()->OnChangeIMEByHandle(luaL_checkint(L, 1));
  return 0;
}

static int input_OnChangeIMEConversionModeByHandle (lua_State *L) {
  input()->OnChangeIMEConversionModeByHandle(luaL_checkint(L, 1));
  return 0;
}

static int input_OnChangeIMESentenceModeByHandle (lua_State *L) {
  input()->OnChangeIMESentenceModeByHandle(luaL_checkint(L, 1));
  return 0;
}

static int input_OnIMEChangeCandidates (lua_State *L) {
  input()->OnIMEChangeCandidates();
  return 0;
}

static int input_OnIMECloseCandidates (lua_State *L) {
  input()->OnIMECloseCandidates();
  return 0;
}

static int input_OnIMEComposition (lua_State *L) {
  input()->OnIMEComposition(luaL_checkint(L, 1));
  return 0;
}

static int input_OnIMEEndComposition (lua_State *L) {
  input()->OnIMEEndComposition();
  return 0;
}

static int input_OnIMERecomputeModes (lua_State *L) {
  input()->OnIMERecomputeModes();
  return 0;
}

static int input_OnIMEShowCandidates (lua_State *L) {
  input()->OnIMEShowCandidates();
  return 0;
}

static int input_OnIMEStartComposition (lua_State *L) {
  input()->OnIMEStartComposition();
  return 0;
}

static int input_OnInputLanguageChanged (lua_State *L) {
  input()->OnInputLanguageChanged();
  return 0;
}

static int input_OnKeyCodeUnhandled (lua_State *L) {
  input()->OnKeyCodeUnhandled(luaL_checkint(L, 1));
  return 0;
}

static int input_RegisterKeyCodeUnhandledListener (lua_State *L) {
  input()->RegisterKeyCodeUnhandledListener(luaL_checkvpanel(L, 1));
  return 0;
}

static int input_ReleaseAppModalSurface (lua_State *L) {
  input()->ReleaseAppModalSurface();
  return 0;
}

static int input_ReleaseModalSubTree (lua_State *L) {
  input()->ReleaseModalSubTree();
  return 0;
}

static int input_SetAppModalSurface (lua_State *L) {
  input()->SetAppModalSurface(luaL_checkvpanel(L, 1));
  return 0;
}

static int input_SetCandidateListPageStart (lua_State *L) {
  input()->SetCandidateListPageStart(luaL_checkint(L, 1));
  return 0;
}

static int input_SetCandidateWindowPos (lua_State *L) {
  input()->SetCandidateWindowPos(luaL_checkint(L, 1), luaL_checkint(L, 2));
  return 0;
}

static int input_SetCursorPos (lua_State *L) {
  input()->SetCursorPos(luaL_checkint(L, 1), luaL_checkint(L, 2));
  return 0;
}

static int input_SetModalSubTree (lua_State *L) {
  input()->SetModalSubTree(luaL_checkvpanel(L, 1), luaL_checkvpanel(L, 2), luaL_optboolean(L, 3, true));
  return 0;
}

static int input_SetModalSubTreeReceiveMessages (lua_State *L) {
  input()->SetModalSubTreeReceiveMessages(luaL_checkboolean(L, 1));
  return 0;
}

static int input_SetMouseCapture (lua_State *L) {
  input()->SetMouseCapture(luaL_checkvpanel(L, 1));
  return 0;
}

static int input_SetMouseCaptureEx (lua_State *L) {
  input()->SetMouseCaptureEx(luaL_checkvpanel(L, 1), (MouseCode)luaL_checkint(L, 2));
  return 0;
}

static int input_SetMouseFocus (lua_State *L) {
  input()->SetMouseFocus(luaL_checkvpanel(L, 1));
  return 0;
}

static int input_ShouldModalSubTreeReceiveMessages (lua_State *L) {
  lua_pushboolean(L, input()->ShouldModalSubTreeReceiveMessages());
  return 1;
}

static int input_UnregisterKeyCodeUnhandledListener (lua_State *L) {
  input()->UnregisterKeyCodeUnhandledListener(luaL_checkvpanel(L, 1));
  return 0;
}

static int input_WasKeyPressed (lua_State *L) {
  lua_pushboolean(L, input()->WasKeyPressed((KeyCode)luaL_checkint(L, 1)));
  return 1;
}

static int input_WasKeyReleased (lua_State *L) {
  lua_pushboolean(L, input()->WasKeyReleased((KeyCode)luaL_checkint(L, 1)));
  return 1;
}

static int input_WasKeyTyped (lua_State *L) {
  lua_pushboolean(L, input()->WasKeyTyped((KeyCode)luaL_checkint(L, 1)));
  return 1;
}

static int input_WasMouseDoublePressed (lua_State *L) {
  lua_pushboolean(L, input()->WasMouseDoublePressed((MouseCode)luaL_checkint(L, 1)));
  return 1;
}

static int input_WasMousePressed (lua_State *L) {
  lua_pushboolean(L, input()->WasMousePressed((MouseCode)luaL_checkint(L, 1)));
  return 1;
}

static int input_WasMouseReleased (lua_State *L) {
  lua_pushboolean(L, input()->WasMouseReleased((MouseCode)luaL_checkint(L, 1)));
  return 1;
}


// ---------------------------------------------------------------------------
// HL2SB: GMod's input key-trapping API.
//
// lua/vgui/dbinder.lua is its main user (StartKeyTrapping on click, then
// IsKeyTrapping + CheckKeyTrapping every frame until a key arrives), and
// gamemodes/base/gamemode/cl_spawnmenu.lua:15 calls input.IsKeyTrapping() too.
//
// GMod added these to its own copy of vgui's IInput; this fork's
// public/vgui/IInput.h has none of them, and the only definitions in the tree
// lived in modules/gmod_compatibility/sh_init.lua, which is inert behind
// GMOD_COMPATIBILITY = false.  The primitives do exist on the engine side:
// IVEngineClient::StartKeyTrapMode / CheckDoneKeyTrapping (public/cdll_int.h:278-279)
// and IInput::GetKeyCodeText (public/vgui/IInput.h:53).
//
// GMod's semantics, all kept:
//   input.StartKeyTrapping() - start trapping the keyboard
//   input.IsKeyTrapping()    - true until the trapped key has been read
//   input.CheckKeyTrapping() - the pressed key's code, nil while no key has been
//                              pressed; reading it ends the trap and the key is
//                              consumed (GMod's function does the same)
//   input.GetKeyName( code ) - the key's display name, nil for a code that is not
//                              a keyboard key
// ---------------------------------------------------------------------------
static bool s_bHL2SBKeyTrapping = false;

static int input_StartKeyTrapping (lua_State *L) {
  engine->StartKeyTrapMode();
  s_bHL2SBKeyTrapping = true;
  return 0;
}

static int input_IsKeyTrapping (lua_State *L) {
  lua_pushboolean(L, s_bHL2SBKeyTrapping);
  return 1;
}

static int input_CheckKeyTrapping (lua_State *L) {
  if ( !s_bHL2SBKeyTrapping ) return 0;

  ButtonCode_t code = BUTTON_CODE_INVALID;
  if ( !engine->CheckDoneKeyTrapping(code) ) return 0;

  s_bHL2SBKeyTrapping = false;

  if ( code == BUTTON_CODE_INVALID ) return 0;

  lua_pushinteger(L, (int)code);
  return 1;
}

static int input_GetKeyName (lua_State *L) {
  char buf[ 256 ];
  buf[ 0 ] = '\0';
  input()->GetKeyCodeText((KeyCode)luaL_checkint(L, 1), buf, sizeof( buf ));

  if ( buf[ 0 ] == '\0' ) {
    lua_pushnil(L);
    return 1;
  }

  lua_pushstring(L, buf);
  return 1;
}

/*
** HL2SB: GMod's input.LookupBinding( binding, exact ).
**
** The engine primitive is right there - IVEngineClient::Key_LookupBinding strips a leading
** '+' ("+use" -> "E"), Key_LookupBindingExact keeps it (public/cdll_int.h:272 + :549) - and
** public/lua/lcdll_int.cpp:333 already exposes it as engine.Key_LookupBinding.  GMod spells
** it on the input library, which is why the call was a nil method here.
**
** ⚠️ cod_c4 reads the pick-up key with it while drawing its HUD prompt
** (addons/cod_c4/lua/entities/cod-c4/cl_init.lua:60):
**     local useKey = input.LookupBinding( "+reload" ) or "R"
** As a nil method the call raised instead of returning nil, so the HUD callback died on
** that line and the "Press R to Pick Up C4" text was never drawn.
*/
static int input_LookupBinding (lua_State *L) {
  const char *pszBinding = luaL_checkstring(L, 1);
  const char *pszKey;

  if ( lua_toboolean(L, 2) )
    pszKey = engine->Key_LookupBindingExact(pszBinding);
  else
    pszKey = engine->Key_LookupBinding(pszBinding);

  if ( pszKey == NULL )
    lua_pushnil(L);
  else
    lua_pushstring(L, pszKey);

  return 1;
}


static const luaL_Reg inputlib[] = {
  {"CandidateListStartsAtOne",   input_CandidateListStartsAtOne},
  {"CheckKeyTrapping",   input_CheckKeyTrapping},
  {"GetKeyName",   input_GetKeyName},
  {"LookupBinding",   input_LookupBinding},
  {"IsKeyTrapping",   input_IsKeyTrapping},
  {"StartKeyTrapping",   input_StartKeyTrapping},
  {"GetAppModalSurface",   input_GetAppModalSurface},
  {"GetCandidateListCount",   input_GetCandidateListCount},
  {"GetCandidateListPageSize",   input_GetCandidateListPageSize},
  {"GetCandidateListPageStart",   input_GetCandidateListPageStart},
  {"GetCandidateListSelectedItem",   input_GetCandidateListSelectedItem},
  {"GetCurrentIMEHandle",   input_GetCurrentIMEHandle},
//  {"GetCursorPos__USE_VCR_MODE",   input_GetCursorPos__USE_VCR_MODE},
  // HL2SB: GMod's spelling.  See the comment on input_GetCursorPos above.
  {"GetCursorPos",   input_GetCursorPos},
  {"GetCursorPosition",   input_GetCursorPosition},
  {"GetEnglishIMEHandle",   input_GetEnglishIMEHandle},
  {"GetFocus",   input_GetFocus},
  {"GetModalSubTree",   input_GetModalSubTree},
  {"GetMouseCapture",   input_GetMouseCapture},
  {"GetMouseOver",   input_GetMouseOver},
  {"GetShouldInvertCompositionString",   input_GetShouldInvertCompositionString},
  {"IsKeyDown",   input_IsKeyDown},
  {"IsMouseDown",   input_IsMouseDown},
  {"OnChangeIME",   input_OnChangeIME},
  {"OnChangeIMEByHandle",   input_OnChangeIMEByHandle},
  {"OnChangeIMEConversionModeByHandle",   input_OnChangeIMEConversionModeByHandle},
  {"OnChangeIMESentenceModeByHandle",   input_OnChangeIMESentenceModeByHandle},
  {"OnIMEChangeCandidates",   input_OnIMEChangeCandidates},
  {"OnIMECloseCandidates",   input_OnIMECloseCandidates},
  {"OnIMEComposition",   input_OnIMEComposition},
  {"OnIMEEndComposition",   input_OnIMEEndComposition},
  {"OnIMERecomputeModes",   input_OnIMERecomputeModes},
  {"OnIMEShowCandidates",   input_OnIMEShowCandidates},
  {"OnIMEStartComposition",   input_OnIMEStartComposition},
  {"OnInputLanguageChanged",   input_OnInputLanguageChanged},
  {"OnKeyCodeUnhandled",   input_OnKeyCodeUnhandled},
  {"RegisterKeyCodeUnhandledListener",   input_RegisterKeyCodeUnhandledListener},
  {"ReleaseAppModalSurface",   input_ReleaseAppModalSurface},
  {"ReleaseModalSubTree",   input_ReleaseModalSubTree},
  {"SetAppModalSurface",   input_SetAppModalSurface},
  {"SetCandidateListPageStart",   input_SetCandidateListPageStart},
  {"SetCandidateWindowPos",   input_SetCandidateWindowPos},
  {"SetCursorPos",   input_SetCursorPos},
  {"SetModalSubTree",   input_SetModalSubTree},
  {"SetModalSubTreeReceiveMessages",   input_SetModalSubTreeReceiveMessages},
  {"SetMouseCapture",   input_SetMouseCapture},
  {"SetMouseCaptureEx",   input_SetMouseCaptureEx},
  {"SetMouseFocus",   input_SetMouseFocus},
  {"ShouldModalSubTreeReceiveMessages",   input_ShouldModalSubTreeReceiveMessages},
  {"UnregisterKeyCodeUnhandledListener",   input_UnregisterKeyCodeUnhandledListener},
  {"WasKeyPressed",   input_WasKeyPressed},
  {"WasKeyReleased",   input_WasKeyReleased},
  {"WasKeyTyped",   input_WasKeyTyped},
  {"WasMouseDoublePressed",   input_WasMouseDoublePressed},
  {"WasMousePressed",   input_WasMousePressed},
  {"WasMouseReleased",   input_WasMouseReleased},
  {NULL, NULL}
};


/*
** Open input library
*/
LUALIB_API int luaopen_input (lua_State *L) {
  luaL_register(L, LUA_INPUTLIBNAME, inputlib);
  return 1;
}
