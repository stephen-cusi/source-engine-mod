#ifndef LTEXTENTRY_H
#define LTEXTENTRY_H

#ifdef _WIN32
#pragma once
#endif

#include <vgui_controls/TextEntry.h>
// vgui_controls/lPanel.h declares lua_pushpanel, which the
// LUA_CALL_PANEL_METHOD_BEGIN expansion below needs; scripted_controls/lPanel.h
// is HL2SB's own LPanel and does not.
#include <vgui_controls/lPanel.h>
#include "scripted_controls/lPanel.h"

// Declared before the class: LTextEntry::PushLuaInstanceSafe below forwards to it.
LUA_API void lua_pushtextentry ( lua_State *L, vgui::TextEntry *pTextEntry );

namespace vgui
{

class LTextEntry : public TextEntry
{
    DECLARE_CLASS_SIMPLE( LTextEntry, TextEntry );

    LUA_OVERRIDE_SINGLE_LUA_INSTANCE_METATABLE( LTextEntry, "TextEntry" );

    public:
    LTextEntry( Panel *parent, const char *panelName, lua_State *L = NULL );
    ~LTextEntry();

    // Experiment's bindings call Class::PushLuaInstanceSafe; HL2SB pushes the panel
    // through its own function, which also carries the Lua table reference.
    static void PushLuaInstanceSafe( lua_State *L, LTextEntry *pTextEntry )
    {
        lua_pushtextentry( L, pTextEntry );
    }

    // LUA_GET_REF_TABLE (luamanager.h) calls this when the panel has no Lua table
    // yet.  Leaves nothing on the stack: the macro calls lua_getref straight after.
    void SetupRefTable( lua_State *L )
    {
        lua_newtable( L );
        m_nTableReference = luaL_ref( L, LUA_REGISTRYINDEX );
    }

    public:
#if defined( LUA_SDK )
    lua_State          *m_lua_State;
    int                 m_nTableReference;
#endif

    protected:
    virtual void ApplySchemeSettings( vgui::IScheme *pScheme )
    {
        /*
        ** HL2SB: base first, then Lua -- the opposite of Experiment's order.
        **
        ** Their order is Lua, then BaseClass, which means the scheme's default font
        ** is applied *after* anything the script set, so a Lua SetFont was always
        ** overwritten and the entry rendered at the scheme's size while every Label
        ** next to it was drawn at ours.  Running the base first lets the Lua
        ** ApplySchemeSettings hook have the last word, which is what Derma code
        ** expects: DTextEntry:ApplySchemeSettings() is where it picks its font.
        */
        BaseClass::ApplySchemeSettings( pScheme );

        LUA_CALL_PANEL_METHOD_BEGIN( "ApplySchemeSettings" );
        LUA_CALL_PANEL_METHOD_END( 0, 0 );
    }

    /*
    ** HL2SB: forward the mouse press to Lua.
    **
    ** LPanel does this and the scripted controls rely on it, but the ported TextEntry
    ** only ever overrode ApplySchemeSettings, so a Lua OnMousePressed never ran on it.
    ** That is what stopped Derma-style code from calling RequestFocus() -- and a vgui
    ** control only receives typed characters once it is the focused panel, so the entry
    ** selected its text on click and then ignored every keystroke.
    */
    virtual void OnMousePressed( MouseCode code )
    {
        BaseClass::OnMousePressed( code );

        LUA_CALL_PANEL_METHOD_BEGIN( "OnMousePressed" );
            lua_pushinteger( m_lua_State, ( int )code );
        LUA_CALL_PANEL_METHOD_END( 1, 0 );
    }

    /*
    ** HL2SB: OnTextChanged / OnEnter dispatch for the custom Derma layer.
    **
    ** The stock control reports edits only as vgui action signals ("TextChanged"
    ** posted by FireActionSignal to AddActionSignalTarget listeners), which a Lua
    ** panel never sees.  FireActionSignal is the single choke point every edit
    ** path funnels through -- typing (OnKeyTyped / OnKeyCodeTyped), paste,
    ** delete, undo, IME replacement all end there -- so overriding it covers
    ** every way the buffer can change without polling.  The base still posts
    ** its signal, so C++ consumers keep working; the Lua hook just runs after.
    **
    ** Both hooks push the self argument through lua_pushtextentry rather than the
    ** BEGIN_LUA_CALL_PANEL_METHOD macro: the macro hands out a "Panel"-metatable
    ** userdata, and a callback doing self:GetValue() -- GMod's DTextEntry idiom --
    ** would resolve through the Panel chain only and hit nil, because GetValue /
    ** GetText live on the TextEntry metatable.
    */
    virtual void FireActionSignal()
    {
        BaseClass::FireActionSignal();
        HL2SB_CallLuaTextEntryMethod( "OnTextChanged" );
    }

    /*
    ** Enter has no action signal of its own: the base either swallows it
    ** (single-line) or turns it into a newline (multiline / _sendNewLines).
    ** Intercept the key before the base consumes it so an OnEnter hook always
    ** runs, then let the base do its normal thing.
    */
    virtual void OnKeyCodeTyped( KeyCode code )
    {
        if ( code == KEY_ENTER )
            HL2SB_CallLuaTextEntryMethod( "OnEnter" );

        BaseClass::OnKeyCodeTyped( code );
    }

    void HL2SB_CallLuaTextEntryMethod( const char *pszName )
    {
#if defined( LUA_SDK )
        if ( !lua_isrefvalid( m_lua_State, m_nTableReference ) )
            return;

        lua_getref( m_lua_State, m_nTableReference );
        lua_getfield( m_lua_State, -1, pszName );
        lua_remove( m_lua_State, -2 );
        if ( lua_isfunction( m_lua_State, -1 ) )
        {
            lua_pushtextentry( m_lua_State, this );
            luasrc_pcall( m_lua_State, 1, 0, 0 );
        }
        else
        {
            lua_pop( m_lua_State, 1 );
        }
#endif
    }
};

}  // namespace vgui

/* type for TextEntry functions */
typedef LTextEntry lua_TextEntry;

/*
** access functions (stack -> C)
*/

LUA_API lua_TextEntry *( lua_totextentry )( lua_State *L, int idx );

/*
** push functions (C -> stack)
*/
LUALIB_API lua_TextEntry *( luaL_checktextentry )( lua_State *L, int narg );

#endif  // LTEXTENTRY_H
