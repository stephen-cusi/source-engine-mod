#include <cbase.h>

#include <vgui_int.h>
#include <luamanager.h>
#include "luasrclib.h"
#include <lColor.h>
#include <tier1/LKeyValues.h>
#include <vgui/LVGUI.h>

#include <scripted_controls/lTextEntry.h>
#include "scripted_controls/lPanel.h"

/*
** HL2SB: the OnTextChanged / OnEnter dispatches (declared in lTextEntry.h) push
** their self argument through lua_pushtextentry so callbacks can call
** self:GetValue() -- see HL2SB_CallLuaTextEntryMethod in the header.
*/


// memdbgon must be the last include file in a .cpp file!!!
#include <tier0/memdbgon.h>

using namespace vgui;

//-----------------------------------------------------------------------------
// Purpose: Constructor
//-----------------------------------------------------------------------------
LTextEntry::LTextEntry( Panel *parent, const char *panelName, lua_State *L /* = nullptr */ )
    : TextEntry( parent, panelName )
{
    m_lua_State = L;
#if defined( LUA_SDK )
    // HL2SB: Experiment keeps the Lua instance in their pudata cache; HL2SB's
    // scripted controls carry it on the panel, which is what
    // LUA_CALL_PANEL_METHOD_BEGIN looks at.
    m_lua_State = L;
    m_nTableReference = LUA_NOREF;
#endif
}

LTextEntry::~LTextEntry()
{
#if defined( LUA_SDK )
    lua_unref( m_lua_State, m_nTableReference );
#endif
}


/*
** access functions (stack -> C)
*/

LUA_API lua_TextEntry *lua_totextentry( lua_State *L, int idx )
{
    PHandle *phPanel =
        dynamic_cast< PHandle * >( ( PHandle * )lua_touserdata( L, idx ) );
    if ( phPanel == NULL )
        return NULL;
    return dynamic_cast< lua_TextEntry * >( phPanel->Get() );
}

/*
** push functions (C -> stack)
*/
LUALIB_API lua_TextEntry *luaL_checktextentry( lua_State *L, int narg )
{
    lua_TextEntry *d = lua_totextentry( L, narg );
    if ( d == NULL ) /* avoid extra test when d is not 0 */
        luaL_argerror( L, narg, "TextEntry expected, got INVALID_PANEL" );
    return d;
}

/*
** HL2SB: Experiment has no lua_pushtextentry; their bindings reach the panel
** through LUA_OVERRIDE_SINGLE_LUA_INSTANCE_METATABLE and their pudata cache.
** HL2SB's scripted controls each expose a free lua_push<panel> function.
*/
LUA_API void lua_pushtextentry (lua_State *L, vgui::TextEntry *pTextEntry) {
  PHandle *phPanel = (PHandle *)lua_newuserdata(L, sizeof(PHandle));
  phPanel->Set(pTextEntry);
  luaL_getmetatable(L, "TextEntry");
  lua_setmetatable(L, -2);
}

LUA_REGISTRATION_INIT( TextEntry )

LUA_BINDING_BEGIN( TextEntry, SetText, "class", "Sets the text of the text entry" )
{
    lua_TextEntry *textEntry = LUA_BINDING_ARGUMENT( luaL_checktextentry, 1, "textEntry" );
    const char *text = LUA_BINDING_ARGUMENT( luaL_checkstring, 2, "text" );
    textEntry->SetText( text );
    return 0;
}
LUA_BINDING_END()

/*
** HL2SB: disabled -- vgui::TextEntry in this tree has no GetCursorPos()/SetCursorPos().
** Experiment's vgui_controls/TextEntry does; HL2SB's is the stock Valve control, which
** exposes neither.  Not needed by Derma's DTextEntry, which drives the control through
** SetText/GetText/SetFont/OnTextChanged/AllowInput.
*/
#if 0
LUA_BINDING_BEGIN( TextEntry, GetCursorPosition, "class", "Gets the cursor position in the text entry" )
{
    lua_TextEntry *textEntry = LUA_BINDING_ARGUMENT( luaL_checktextentry, 1, "textEntry" );
    int position = textEntry->GetCursorPos();
    lua_pushinteger( L, position );
    return 1;
}
LUA_BINDING_END( "integer", "The cursor position" )

LUA_BINDING_BEGIN( TextEntry, SetCursorPosition, "class", "Sets the cursor position in the text entry" )
{
    lua_TextEntry *textEntry = LUA_BINDING_ARGUMENT( luaL_checktextentry, 1, "textEntry" );
    int position = LUA_BINDING_ARGUMENT( luaL_checknumber, 2, "position" );
    textEntry->SetCursorPos( position );
    return 0;
}
LUA_BINDING_END()
#endif

LUA_BINDING_BEGIN( TextEntry, GetText, "class", "Gets the text from the text entry" )
{
    lua_TextEntry *textEntry = LUA_BINDING_ARGUMENT( luaL_checktextentry, 1, "textEntry" );
    char buf[1024];
    textEntry->GetText( buf, sizeof( buf ) );
    lua_pushstring( L, buf );
    return 1;
}
LUA_BINDING_END( "string", "The text from the text entry" )

LUA_BINDING_BEGIN( TextEntry, GetTextLength, "class", "Gets the length of the text in the text entry" )
{
    lua_TextEntry *textEntry = LUA_BINDING_ARGUMENT( luaL_checktextentry, 1, "textEntry" );
    int length = textEntry->GetTextLength();
    lua_pushinteger( L, length );
    return 1;
}
LUA_BINDING_END( "integer", "The length of the text" )

LUA_BINDING_BEGIN( TextEntry, GetValue, "class", "Gets the value from the text entry" )
{
    lua_TextEntry *textEntry = LUA_BINDING_ARGUMENT( luaL_checktextentry, 1, "textEntry" );
    char buf[8092];
    textEntry->GetText( buf, sizeof( buf ) );
    lua_pushstring( L, buf );
    return 1;
}
LUA_BINDING_END( "string", "The value from the text entry" )

LUA_BINDING_BEGIN( TextEntry, GetValueAsFloat, "class", "Gets the value from the text entry as a float" )
{
    lua_TextEntry *textEntry = LUA_BINDING_ARGUMENT( luaL_checktextentry, 1, "textEntry" );
    float value = textEntry->GetValueAsFloat();
    lua_pushnumber( L, value );
    return 1;
}
LUA_BINDING_END( "number", "The value as a float" )

LUA_BINDING_BEGIN( TextEntry, GetValueAsInteger, "class", "Gets the value from the text entry as an integer" )
{
    lua_TextEntry *textEntry = LUA_BINDING_ARGUMENT( luaL_checktextentry, 1, "textEntry" );
    int value = textEntry->GetValueAsInt();
    lua_pushinteger( L, value );
    return 1;
}
LUA_BINDING_END( "integer", "The value as an integer" )

LUA_BINDING_BEGIN( TextEntry, IsTextFullySelected, "class", "Checks if the text is fully selected" )
{
    lua_TextEntry *textEntry = LUA_BINDING_ARGUMENT( luaL_checktextentry, 1, "textEntry" );
    lua_pushboolean( L, textEntry->IsTextFullySelected() );
    return 1;
}
LUA_BINDING_END( "boolean", "Whether the text is fully selected" )

LUA_BINDING_BEGIN( TextEntry, GotoLeft, "class", "Moves the cursor left" )
{
    lua_TextEntry *textEntry = LUA_BINDING_ARGUMENT( luaL_checktextentry, 1, "textEntry" );
    textEntry->GotoLeft();
    return 0;
}
LUA_BINDING_END()

LUA_BINDING_BEGIN( TextEntry, GotoRight, "class", "Moves the cursor right" )
{
    lua_TextEntry *textEntry = LUA_BINDING_ARGUMENT( luaL_checktextentry, 1, "textEntry" );
    textEntry->GotoRight();
    return 0;
}
LUA_BINDING_END()

LUA_BINDING_BEGIN( TextEntry, GotoUp, "class", "Moves the cursor up" )
{
    lua_TextEntry *textEntry = LUA_BINDING_ARGUMENT( luaL_checktextentry, 1, "textEntry" );
    textEntry->GotoUp();
    return 0;
}
LUA_BINDING_END()

LUA_BINDING_BEGIN( TextEntry, GotoDown, "class", "Moves the cursor down" )
{
    lua_TextEntry *textEntry = LUA_BINDING_ARGUMENT( luaL_checktextentry, 1, "textEntry" );
    textEntry->GotoDown();
    return 0;
}
LUA_BINDING_END()

LUA_BINDING_BEGIN( TextEntry, GotoWordRight, "class", "Moves the cursor to the next word on the right" )
{
    lua_TextEntry *textEntry = LUA_BINDING_ARGUMENT( luaL_checktextentry, 1, "textEntry" );
    textEntry->GotoWordRight();
    return 0;
}
LUA_BINDING_END()

LUA_BINDING_BEGIN( TextEntry, GotoWordLeft, "class", "Moves the cursor to the previous word on the left" )
{
    lua_TextEntry *textEntry = LUA_BINDING_ARGUMENT( luaL_checktextentry, 1, "textEntry" );
    textEntry->GotoWordLeft();
    return 0;
}
LUA_BINDING_END()

LUA_BINDING_BEGIN( TextEntry, GotoFirstOfLine, "class", "Moves the cursor to the beginning of the line" )
{
    lua_TextEntry *textEntry = LUA_BINDING_ARGUMENT( luaL_checktextentry, 1, "textEntry" );
    textEntry->GotoFirstOfLine();
    return 0;
}
LUA_BINDING_END()

LUA_BINDING_BEGIN( TextEntry, GotoEndOfLine, "class", "Moves the cursor to the end of the line" )
{
    lua_TextEntry *textEntry = LUA_BINDING_ARGUMENT( luaL_checktextentry, 1, "textEntry" );
    textEntry->GotoEndOfLine();
    return 0;
}
LUA_BINDING_END()

LUA_BINDING_BEGIN( TextEntry, GotoTextStart, "class", "Moves the cursor to the start of the text" )
{
    lua_TextEntry *textEntry = LUA_BINDING_ARGUMENT( luaL_checktextentry, 1, "textEntry" );
    textEntry->GotoTextStart();
    return 0;
}
LUA_BINDING_END()

LUA_BINDING_BEGIN( TextEntry, GotoTextEnd, "class", "Moves the cursor to the end of the text" )
{
    lua_TextEntry *textEntry = LUA_BINDING_ARGUMENT( luaL_checktextentry, 1, "textEntry" );
    textEntry->GotoTextEnd();
    return 0;
}
LUA_BINDING_END()

LUA_BINDING_BEGIN( TextEntry, InsertString, "class", "Inserts a string at the cursor position" )
{
    lua_TextEntry *textEntry = LUA_BINDING_ARGUMENT( luaL_checktextentry, 1, "textEntry" );
    const char *str = LUA_BINDING_ARGUMENT( luaL_checkstring, 2, "str" );
    textEntry->InsertString( str );
    return 0;
}
LUA_BINDING_END()

LUA_BINDING_BEGIN( TextEntry, Backspace, "class", "Deletes the character before the cursor" )
{
    lua_TextEntry *textEntry = LUA_BINDING_ARGUMENT( luaL_checktextentry, 1, "textEntry" );
    textEntry->Backspace();
    return 0;
}
LUA_BINDING_END()

LUA_BINDING_BEGIN( TextEntry, Delete, "class", "Deletes the character after the cursor" )
{
    lua_TextEntry *textEntry = LUA_BINDING_ARGUMENT( luaL_checktextentry, 1, "textEntry" );
    textEntry->Delete();
    return 0;
}
LUA_BINDING_END()

LUA_BINDING_BEGIN( TextEntry, SelectNone, "class", "Deselects any selected text" )
{
    lua_TextEntry *textEntry = LUA_BINDING_ARGUMENT( luaL_checktextentry, 1, "textEntry" );
    textEntry->SelectNone();
    return 0;
}
LUA_BINDING_END()

LUA_BINDING_BEGIN( TextEntry, OpenEditMenu, "class", "Opens the edit menu for the text entry" )
{
    lua_TextEntry *textEntry = LUA_BINDING_ARGUMENT( luaL_checktextentry, 1, "textEntry" );
    textEntry->OpenEditMenu();
    return 0;
}
LUA_BINDING_END()

LUA_BINDING_BEGIN( TextEntry, DeleteSelected, "class", "Deletes the selected text" )
{
    lua_TextEntry *textEntry = LUA_BINDING_ARGUMENT( luaL_checktextentry, 1, "textEntry" );
    textEntry->DeleteSelected();
    return 0;
}
LUA_BINDING_END()

LUA_BINDING_BEGIN( TextEntry, Undo, "class", "Undoes the last action" )
{
    lua_TextEntry *textEntry = LUA_BINDING_ARGUMENT( luaL_checktextentry, 1, "textEntry" );
    textEntry->Undo();
    return 0;
}
LUA_BINDING_END()

LUA_BINDING_BEGIN( TextEntry, SaveUndoState, "class", "Saves the current state for undo" )
{
    lua_TextEntry *textEntry = LUA_BINDING_ARGUMENT( luaL_checktextentry, 1, "textEntry" );
    textEntry->SaveUndoState();
    return 0;
}
LUA_BINDING_END()

LUA_BINDING_BEGIN( TextEntry, SetFont, "class", "Sets the font for the text entry" )
{
    lua_TextEntry *textEntry = LUA_BINDING_ARGUMENT( luaL_checktextentry, 1, "textEntry" );
    lua_HFont font = LUA_BINDING_ARGUMENT( luaL_checkfont, 2, "font" );
    textEntry->SetFont( font );
    return 0;
}
LUA_BINDING_END()

LUA_BINDING_BEGIN( TextEntry, SetTextHidden, "class", "Sets whether the text is hidden" )
{
    lua_TextEntry *textEntry = LUA_BINDING_ARGUMENT( luaL_checktextentry, 1, "textEntry" );
    bool hidden = LUA_BINDING_ARGUMENT( lua_toboolean, 2, "hidden" );
    textEntry->SetTextHidden( hidden );
    return 0;
}
LUA_BINDING_END()

LUA_BINDING_BEGIN( TextEntry, SetEditable, "class", "Sets whether the text entry is editable" )
{
    lua_TextEntry *textEntry = LUA_BINDING_ARGUMENT( luaL_checktextentry, 1, "textEntry" );
    bool editable = LUA_BINDING_ARGUMENT( lua_toboolean, 2, "editable" );
    textEntry->SetEditable( editable );
    return 0;
}
LUA_BINDING_END()

/*
** HL2SB: GMod's DTextEntry spells "editable" as SetValue / SetReadOnly.  Both are
** thin aliases over the stock control -- SetValue sets the buffer (GMod's name for
** it), SetReadOnly is SetEditable's inverse -- so the custom panel layer can use
** GMod's names without a Lua-side wrapper per call.
*/
LUA_BINDING_BEGIN( TextEntry, SetValue, "class", "Sets the text of the text entry (GMod alias of SetText)" )
{
    lua_TextEntry *textEntry = LUA_BINDING_ARGUMENT( luaL_checktextentry, 1, "textEntry" );
    const char *text = LUA_BINDING_ARGUMENT( luaL_checkstring, 2, "text" );
    textEntry->SetText( text );
    return 0;
}
LUA_BINDING_END()

LUA_BINDING_BEGIN( TextEntry, SetReadOnly, "class", "Sets whether the text entry is read-only" )
{
    lua_TextEntry *textEntry = LUA_BINDING_ARGUMENT( luaL_checktextentry, 1, "textEntry" );
    bool bReadOnly = LUA_BINDING_ARGUMENT( lua_toboolean, 2, "readOnly" );
    textEntry->SetEditable( !bReadOnly );
    return 0;
}
LUA_BINDING_END()

LUA_BINDING_BEGIN( TextEntry, IsEditable, "class", "Checks if the text entry is editable" )
{
    lua_TextEntry *textEntry = LUA_BINDING_ARGUMENT( luaL_checktextentry, 1, "textEntry" );
    lua_pushboolean( L, textEntry->IsEditable() );
    return 1;
}
LUA_BINDING_END( "boolean", "Whether the text entry is editable" )

LUA_BINDING_BEGIN( TextEntry, MoveCursor, "class", "Moves the cursor by a given offset" )
{
    lua_TextEntry *textEntry = LUA_BINDING_ARGUMENT( luaL_checktextentry, 1, "textEntry" );
    float offsetX = LUA_BINDING_ARGUMENT( luaL_checknumber, 2, "offsetX" );
    float offsetY = LUA_BINDING_ARGUMENT( luaL_checknumber, 3, "offsetY" );
    textEntry->MoveCursor( offsetX, offsetY );
    return 0;
}
LUA_BINDING_END()

/*
** HL2SB: disabled -- vgui::TextEntry in this tree has no PaintText().
** Experiment's vgui_controls/TextEntry does; HL2SB's is the stock Valve control,
** which exposes neither the cursor position accessors nor the PaintText hook.
** Not needed by Derma's DTextEntry, which drives the control through
** SetText/GetText/SetFont/OnTextChanged/AllowInput.
*/
#if 0
LUA_BINDING_BEGIN( TextEntry, PaintText, "class", "Paints the text with specified colors" )
{
    lua_TextEntry *textEntry = LUA_BINDING_ARGUMENT( luaL_checktextentry, 1, "textEntry" );
    // HL2SB: by value - see the note on the live bindings below.
    lua_Color color1 = LUA_BINDING_ARGUMENT( luaL_checkcolor, 2, "color1" );
    lua_Color color2 = LUA_BINDING_ARGUMENT( luaL_checkcolor, 3, "color2" );
    lua_Color color3 = LUA_BINDING_ARGUMENT( luaL_checkcolor, 4, "color3" );
    textEntry->PaintText( color1, color2, color3 );
    return 0;
}
LUA_BINDING_END()
#endif

LUA_BINDING_BEGIN( TextEntry, SetDisabledBgColor, "class", "Sets the background color when disabled" )
{
    lua_TextEntry *textEntry = LUA_BINDING_ARGUMENT( luaL_checktextentry, 1, "textEntry" );
    // HL2SB: luaL_checkcolor() returns lua_Color *by value* (the colour lives in
    // the Lua table's fields, see public/lua/lColor.cpp), so gcc/clang reject
    // binding it to a non-const lvalue reference.  MSVC allows it; be explicit.
    lua_Color color = LUA_BINDING_ARGUMENT( luaL_checkcolor, 2, "color" );
    textEntry->SetDisabledBgColor( color );
    return 0;
}
LUA_BINDING_END()

LUA_BINDING_BEGIN( TextEntry, SetMultiline, "class", "Sets whether the text entry is multiline" )
{
    lua_TextEntry *textEntry = LUA_BINDING_ARGUMENT( luaL_checktextentry, 1, "textEntry" );
    bool multiline = LUA_BINDING_ARGUMENT( lua_toboolean, 2, "multiline" );
    textEntry->SetMultiline( multiline );
    return 0;
}
LUA_BINDING_END()

LUA_BINDING_BEGIN( TextEntry, IsMultiline, "class", "Checks if the text entry is multiline" )
{
    lua_TextEntry *textEntry = LUA_BINDING_ARGUMENT( luaL_checktextentry, 1, "textEntry" );
    lua_pushboolean( L, textEntry->IsMultiline() );
    return 1;
}
LUA_BINDING_END( "boolean", "Whether the text entry is multiline" )

LUA_BINDING_BEGIN( TextEntry, SetVerticalScrollbar, "class", "Sets whether the vertical scrollbar is enabled" )
{
    lua_TextEntry *textEntry = LUA_BINDING_ARGUMENT( luaL_checktextentry, 1, "textEntry" );
    bool enable = LUA_BINDING_ARGUMENT( lua_toboolean, 2, "enable" );
    textEntry->SetVerticalScrollbar( enable );
    return 0;
}
LUA_BINDING_END()

LUA_BINDING_BEGIN( TextEntry, SetCatchEnterKey, "class", "Sets whether the enter key is caught by the text entry" )
{
    lua_TextEntry *textEntry = LUA_BINDING_ARGUMENT( luaL_checktextentry, 1, "textEntry" );
    bool catchEnter = LUA_BINDING_ARGUMENT( lua_toboolean, 2, "catchEnter" );
    textEntry->SetCatchEnterKey( catchEnter );
    return 0;
}
LUA_BINDING_END()

LUA_BINDING_BEGIN( TextEntry, SendNewLine, "class", "Sets whether a new line is sent when enter is pressed" )
{
    lua_TextEntry *textEntry = LUA_BINDING_ARGUMENT( luaL_checktextentry, 1, "textEntry" );
    bool sendNewLine = LUA_BINDING_ARGUMENT( lua_toboolean, 2, "sendNewLine" );
    textEntry->SendNewLine( sendNewLine );
    return 0;
}
LUA_BINDING_END()

LUA_BINDING_BEGIN( TextEntry, SetMaximumCharacterCount, "class", "Sets the maximum character count" )
{
    lua_TextEntry *textEntry = LUA_BINDING_ARGUMENT( luaL_checktextentry, 1, "textEntry" );
    int maxCount = LUA_BINDING_ARGUMENT( luaL_checknumber, 2, "maxCount" );
    textEntry->SetMaximumCharCount( maxCount );
    return 0;
}
LUA_BINDING_END()

LUA_BINDING_BEGIN( TextEntry, GetMaximumCharacterCount, "class", "Gets the maximum character count" )
{
    lua_TextEntry *textEntry = LUA_BINDING_ARGUMENT( luaL_checktextentry, 1, "textEntry" );
    lua_pushinteger( L, textEntry->GetMaximumCharCount() );
    return 1;
}
LUA_BINDING_END( "integer", "The maximum character count" )

LUA_BINDING_BEGIN( TextEntry, SetAutoProgressOnHittingCharLimit, "class", "Sets auto-progress on hitting character limit" )
{
    lua_TextEntry *textEntry = LUA_BINDING_ARGUMENT( luaL_checktextentry, 1, "textEntry" );
    bool autoProgress = LUA_BINDING_ARGUMENT( lua_toboolean, 2, "autoProgress" );
    textEntry->SetAutoProgressOnHittingCharLimit( autoProgress );
    return 0;
}
LUA_BINDING_END()

LUA_BINDING_BEGIN( TextEntry, SetWrap, "class", "Sets whether text wrapping is enabled" )
{
    lua_TextEntry *textEntry = LUA_BINDING_ARGUMENT( luaL_checktextentry, 1, "textEntry" );
    bool wrap = LUA_BINDING_ARGUMENT( lua_toboolean, 2, "wrap" );
    textEntry->SetWrap( wrap );
    return 0;
}
LUA_BINDING_END()

LUA_BINDING_BEGIN( TextEntry, RecalculateLineBreaks, "class", "Recalculates the line breaks in the text entry" )
{
    lua_TextEntry *textEntry = LUA_BINDING_ARGUMENT( luaL_checktextentry, 1, "textEntry" );
    textEntry->RecalculateLineBreaks();
    return 0;
}
LUA_BINDING_END()

LUA_BINDING_BEGIN( TextEntry, LayoutVerticalScrollBarSlider, "class", "Layouts the vertical scrollbar slider" )
{
    lua_TextEntry *textEntry = LUA_BINDING_ARGUMENT( luaL_checktextentry, 1, "textEntry" );
    textEntry->LayoutVerticalScrollBarSlider();
    return 0;
}
LUA_BINDING_END()

LUA_BINDING_BEGIN( TextEntry, SetToFullHeight, "class", "Sets the text entry to full height" )
{
    lua_TextEntry *textEntry = LUA_BINDING_ARGUMENT( luaL_checktextentry, 1, "textEntry" );
    textEntry->SetToFullHeight();
    return 0;
}
LUA_BINDING_END()

LUA_BINDING_BEGIN( TextEntry, SetToFullWidth, "class", "Sets the text entry to full width" )
{
    lua_TextEntry *textEntry = LUA_BINDING_ARGUMENT( luaL_checktextentry, 1, "textEntry" );
    textEntry->SetToFullWidth();
    return 0;
}
LUA_BINDING_END()

LUA_BINDING_BEGIN( TextEntry, GetNumberOfLines, "class", "Gets the number of lines in the text entry" )
{
    lua_TextEntry *textEntry = LUA_BINDING_ARGUMENT( luaL_checktextentry, 1, "textEntry" );
    lua_pushinteger( L, textEntry->GetNumLines() );
    return 1;
}
LUA_BINDING_END( "integer", "The number of lines in the text entry" )

LUA_BINDING_BEGIN( TextEntry, SelectAllText, "class", "Selects all text in the text entry" )
{
    lua_TextEntry *textEntry = LUA_BINDING_ARGUMENT( luaL_checktextentry, 1, "textEntry" );
    bool selectAll = LUA_BINDING_ARGUMENT( lua_toboolean, 2, "selectAll" );
    textEntry->SelectAllText( selectAll );
    return 0;
}
LUA_BINDING_END()

LUA_BINDING_BEGIN( TextEntry, SetDrawWidth, "class", "Sets the draw width of the text entry" )
{
    lua_TextEntry *textEntry = LUA_BINDING_ARGUMENT( luaL_checktextentry, 1, "textEntry" );
    float width = LUA_BINDING_ARGUMENT( luaL_checknumber, 2, "width" );
    textEntry->SetDrawWidth( width );
    return 0;
}
LUA_BINDING_END()

LUA_BINDING_BEGIN( TextEntry, GetDrawWidth, "class", "Gets the draw width of the text entry" )
{
    lua_TextEntry *textEntry = LUA_BINDING_ARGUMENT( luaL_checktextentry, 1, "textEntry" );
    lua_pushinteger( L, textEntry->GetDrawWidth() );
    return 1;
}
LUA_BINDING_END( "integer", "The draw width of the text entry" )

LUA_BINDING_BEGIN( TextEntry, SetHorizontalScrolling, "class", "Sets whether horizontal scrolling is enabled" )
{
    lua_TextEntry *textEntry = LUA_BINDING_ARGUMENT( luaL_checktextentry, 1, "textEntry" );
    bool enable = LUA_BINDING_ARGUMENT( lua_toboolean, 2, "enable" );
    textEntry->SetHorizontalScrolling( enable );
    return 0;
}
LUA_BINDING_END()

LUA_BINDING_BEGIN( TextEntry, SetAllowNonAsciiCharacters, "class", "Sets whether non-ASCII characters are allowed" )
{
    lua_TextEntry *textEntry = LUA_BINDING_ARGUMENT( luaL_checktextentry, 1, "textEntry" );
    bool allow = LUA_BINDING_ARGUMENT( lua_toboolean, 2, "allow" );
    textEntry->SetAllowNonAsciiCharacters( allow );
    return 0;
}
LUA_BINDING_END()

LUA_BINDING_BEGIN( TextEntry, SetAllowNumericInputOnly, "class", "Sets whether only numeric input is allowed" )
{
    lua_TextEntry *textEntry = LUA_BINDING_ARGUMENT( luaL_checktextentry, 1, "textEntry" );
    bool allow = LUA_BINDING_ARGUMENT( lua_toboolean, 2, "allow" );
    textEntry->SetAllowNumericInputOnly( allow );
    return 0;
}
LUA_BINDING_END()

LUA_BINDING_BEGIN( TextEntry, SetDrawLanguageIdAtLeft, "class", "Sets whether the language ID is drawn on the left" )
{
    lua_TextEntry *textEntry = LUA_BINDING_ARGUMENT( luaL_checktextentry, 1, "textEntry" );
    bool draw = LUA_BINDING_ARGUMENT( lua_toboolean, 2, "draw" );
    textEntry->SetDrawLanguageIDAtLeft( draw );
    return 0;
}
LUA_BINDING_END()

LUA_BINDING_BEGIN( TextEntry, IsDroppable, "class", "Checks if the text entry is droppable" )
{
    lua_TextEntry *textEntry = LUA_BINDING_ARGUMENT( luaL_checktextentry, 1, "textEntry" );
    CUtlVector< KeyValues * > data;
    textEntry->IsDroppable( data );
    lua_pushboolean( L, data.Count() > 0 );
    return 1;
}
LUA_BINDING_END( "boolean", "Whether the text entry is droppable" )

LUA_BINDING_BEGIN( TextEntry, OnPanelDropped, "class", "Handles the event when a panel is dropped" )
{
    lua_TextEntry *textEntry = LUA_BINDING_ARGUMENT( luaL_checktextentry, 1, "textEntry" );
    CUtlVector< KeyValues * > data;
    textEntry->OnPanelDropped( data );
    return 0;
}
LUA_BINDING_END()

LUA_BINDING_BEGIN( TextEntry, OnCreateDragData, "class", "Creates drag data for the text entry" )
{
    lua_TextEntry *textEntry = LUA_BINDING_ARGUMENT( luaL_checktextentry, 1, "textEntry" );
    KeyValues *dragData = LUA_BINDING_ARGUMENT( luaL_checkkeyvalues, 2, "dragData" );
    textEntry->OnCreateDragData( dragData );
    return 0;
}
LUA_BINDING_END()

LUA_BINDING_BEGIN( TextEntry, SelectAllOnFocusAlways, "class", "Sets whether to always select all text on focus" )
{
    lua_TextEntry *textEntry = LUA_BINDING_ARGUMENT( luaL_checktextentry, 1, "textEntry" );
    bool selectAll = LUA_BINDING_ARGUMENT( lua_toboolean, 2, "selectAll" );
    textEntry->SelectAllOnFocusAlways( selectAll );
    return 0;
}
LUA_BINDING_END()

LUA_BINDING_BEGIN( TextEntry, SetSelectionTextColor, "class", "Sets the text color for selected text" )
{
    lua_TextEntry *textEntry = LUA_BINDING_ARGUMENT( luaL_checktextentry, 1, "textEntry" );
    lua_Color color = LUA_BINDING_ARGUMENT( luaL_checkcolor, 2, "color" );
    textEntry->SetSelectionTextColor( color );
    return 0;
}
LUA_BINDING_END()

LUA_BINDING_BEGIN( TextEntry, SetSelectionBackgroundColor, "class", "Sets the background color for selected text" )
{
    lua_TextEntry *textEntry = LUA_BINDING_ARGUMENT( luaL_checktextentry, 1, "textEntry" );
    lua_Color color = LUA_BINDING_ARGUMENT( luaL_checkcolor, 2, "color" );
    textEntry->SetSelectionBgColor( color );
    return 0;
}
LUA_BINDING_END()

LUA_BINDING_BEGIN( TextEntry, SetSelectionUnfocusedBackgroundColor, "class", "Sets the background color for unfocused selected text" )
{
    lua_TextEntry *textEntry = LUA_BINDING_ARGUMENT( luaL_checktextentry, 1, "textEntry" );
    lua_Color color = LUA_BINDING_ARGUMENT( luaL_checkcolor, 2, "color" );
    textEntry->SetSelectionUnfocusedBgColor( color );
    return 0;
}
LUA_BINDING_END()

LUA_BINDING_BEGIN( TextEntry, SetUseFallbackFont, "class", "Sets the fallback font for the text entry" )
{
    lua_TextEntry *textEntry = LUA_BINDING_ARGUMENT( luaL_checktextentry, 1, "textEntry" );
    bool useFallback = LUA_BINDING_ARGUMENT( lua_toboolean, 2, "useFallback" );
    lua_HFont fallbackFont = LUA_BINDING_ARGUMENT( luaL_checkfont, 3, "fallbackFont" );
    textEntry->SetUseFallbackFont( useFallback, fallbackFont );
    return 0;
}
LUA_BINDING_END()

// static int TextEntry_GetDropContextMenu( lua_State *L )
//{
//     Menu *menu = luaL_checkmenu( L, 2 ); // TODO: Low priority, used nowhere
//     CUtlVector< KeyValues * > data;
//     luaL_checktextentry( L, 1 )->GetDropContextMenu( menu, data );
//     lua_pushboolean( L, data.Count() > 0 );
//     return 1;
// }

LUA_BINDING_BEGIN( TextEntry, __index, "class", "Metamethod called when a non-existent field is indexed" )
{
    TextEntry *pTextEntry = LUA_BINDING_ARGUMENT( lua_totextentry, 1, "textEntry" );
    LUA_METATABLE_INDEX_CHECK_VALID( L, PanelIsValid );
    LUA_METATABLE_INDEX_CHECK( L, pTextEntry );

    // const char *field = LUA_BINDING_ARGUMENT( luaL_checkstring, 2, "field" );

    LTextEntry *plTextEntry = dynamic_cast< LTextEntry * >( pTextEntry );
    LUA_METATABLE_INDEX_CHECK_REF_TABLE( L, plTextEntry );

    if ( lua_getmetatable( L, 1 ) )
    {
        LUA_METATABLE_INDEX_CHECK_TABLE( L );
    }

    luaL_getmetatable( L, "TextEntry" );
    LUA_METATABLE_INDEX_CHECK_TABLE( L );

    LUA_METATABLE_INDEX_DERIVE_INDEX( L, "Panel" );

    lua_pushnil( L );
    return 1;
}
LUA_BINDING_END( "any", "The value of the field" )

LUA_BINDING_BEGIN( TextEntry, __newindex, "class", "Metamethod called when a new field is added" )
{
    TextEntry *pTextEntry = LUA_BINDING_ARGUMENT( lua_totextentry, 1, "textEntry" );

    if ( pTextEntry == NULL )
    { /* avoid extra test when d is not 0 */
        lua_Debug ar1;
        lua_getstack( L, 1, &ar1 );
        lua_getinfo( L, "fl", &ar1 );
        lua_Debug ar2;
        lua_getinfo( L, ">S", &ar2 );
        /* HL2SB: indexing a deleted panel yields nil, the way GMod's engine behaves
        (see lua/includes/util.lua:314-322, GMod's IsValid). */
        lua_pushnil( L );
        return 1;
    }

    LTextEntry *plTextEntry = dynamic_cast< LTextEntry * >( pTextEntry );

    LUA_GET_REF_TABLE( L, plTextEntry );
    lua_pushvalue( L, 3 );
    lua_setfield( L, -2, LUA_BINDING_ARGUMENT( luaL_checkstring, 2, "field" ) );
    lua_pop( L, 1 );
    return 0;
}
LUA_BINDING_END()

LUA_BINDING_BEGIN( TextEntry, __eq, "class", "Metamethod called when two TextEntry objects are compared" )
{
    lua_pushboolean( L, LUA_BINDING_ARGUMENT( lua_totextentry, 1, "textEntry" ) == LUA_BINDING_ARGUMENT( lua_totextentry, 2, "other" ) );
    return 1;
}
LUA_BINDING_END( "boolean", "Whether the TextEntry objects are equal" )

LUA_BINDING_BEGIN( TextEntry, __tostring, "class", "Metamethod called when the TextEntry object is converted to a string" )
{
    TextEntry *pTextEntry = LUA_BINDING_ARGUMENT( lua_totextentry, 1, "textEntry" );
    if ( pTextEntry == NULL )
        lua_pushstring( L, "INVALID_PANEL" );
    else
    {
        const char *pName = pTextEntry->GetName();
        if ( Q_strcmp( pName, "" ) == 0 )
            pName = "(no name)";
        lua_pushfstring( L, "TextEntry: \"%s\"", pName );
    }
    return 1;
}
LUA_BINDING_END( "string", "The string representation of the TextEntry object" )

LUA_REGISTRATION_INIT( Panels )

LUA_BINDING_BEGIN( Panels, TextEntry, "library", "Creates a new TextEntry panel" )
{
    Panel *parent = LUA_BINDING_ARGUMENT_WITH_DEFAULT( luaL_optpanel, 1, VGui_GetClientLuaRootPanel(), "parent" );
    const char *name = LUA_BINDING_ARGUMENT_WITH_DEFAULT( luaL_optstring, 2, "TextEntry", "name" );

    LTextEntry *pPanel = new LTextEntry( parent, name, L );
    LTextEntry::PushLuaInstanceSafe( L, pPanel );
    return 1;
}
LUA_BINDING_END( "TextEntry", "The new TextEntry Panel" )

/*
** Open TextEntry object
*/
/*
** HL2SB: Experiment registers the factory into their Panels library; HL2SB's
** scripted controls sit on the vgui library, so this is vgui.TextEntry( parent,
** name ).  The metatable keeps the name "TextEntry", which is what FindMetaTable
** and GMod's vgui.Register look up.
*/
static int luasrc_TextEntry (lua_State *L) {
  TextEntry *pPanel = new LTextEntry( luaL_optpanel( L, 1, VGui_GetClientLuaRootPanel() ),
                                      luaL_optstring( L, 2, "TextEntry" ), L );
  lua_pushtextentry( L, pPanel );
  return 1;
}


static const luaL_Reg TextEntry_funcs[] = {
  {"TextEntry", luasrc_TextEntry},
  {NULL, NULL}
};


LUALIB_API int luaopen_TextEntry( lua_State *L )
{
    LUA_PUSH_NEW_METATABLE( L, "TextEntry" );

    LUA_REGISTRATION_COMMIT( TextEntry );

    lua_pushstring( L, LUA_PANELMETANAME );
    lua_setfield( L, -2, "__type" ); /* metatable.__type = "Panel" */

    luaL_register( L, "vgui", TextEntry_funcs );

    lua_pop( L, 1 );  // Pop the panel library off the stack
    lua_pop( L, 1 );  // Pop the TextEntry metatable

    /*
    ** HL2SB: LTextEntry now dispatches Paint / PerformLayout / OnThink / the focus pair /
    ** mouse / cursor / key hooks to Lua (see lTextEntry.h), which is what GMod's
    ** DTextEntry writes against.  This global tells the Lua layer so, and it is set here
    ** -- during library registration, before any script is loaded -- because
    ** lua/vgui/DTextEntry.lua decides at load time whether it needs its old
    ** hook.Add("Think") focus poller (it does not, any more).
    */
    lua_pushboolean( L, true );
    lua_setglobal( L, "HL2SB_TEXTENTRY_DISPATCH" );

    return 0;
}
