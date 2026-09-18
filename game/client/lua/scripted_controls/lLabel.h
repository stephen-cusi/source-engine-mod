#ifndef LLABEL_H
#define LLABEL_H

#ifdef _WIN32
#pragma once
#endif

#include <vgui_controls/Label.h>
// vgui_controls/lPanel.h declares lua_pushpanel, which the
// LUA_CALL_PANEL_METHOD_BEGIN expansion below needs; scripted_controls/lPanel.h
// is HL2SB's own LPanel and does not.
#include <vgui_controls/lPanel.h>
#include "scripted_controls/lPanel.h"

// Declared before the class: LLabel::PushLuaInstanceSafe below forwards to it.
LUA_API void lua_pushlabel ( lua_State *L, vgui::Label *pLabel );

namespace vgui
{

// HL2SB: standard look for every Lua-created text control (lLabel.cpp).  Fixes,
// at the root, the two stock-vgui defaults that made every Lua-built dialog
// look broken: northwest text alignment (the caption rides the top edge of
// buttons and rows) and the scheme's tiny "Default" bitmap font.
void HL2SB_ApplyTextDefaults( Label *pLabel );

class LLabel : public Label
{
    DECLARE_CLASS_SIMPLE( LLabel, Label );

    LUA_OVERRIDE_SINGLE_LUA_INSTANCE_METATABLE( LLabel, "Label" );

    public:
    // Experiment's bindings call Class::PushLuaInstanceSafe; HL2SB pushes the
    // panel through its own function, which also carries the table reference.
    static void PushLuaInstanceSafe( lua_State *L, LLabel *pLabel )
    {
        lua_pushlabel( L, pLabel );
    }

    public:
    LLabel( Panel *parent, const char *panelName, const char *text, lua_State *L = nullptr );
    ~LLabel();

    /*
    ** LUA_GET_REF_TABLE (luamanager.h) calls this when the panel has no Lua table
    ** yet, so the ported bindings can store Lua-side fields on the panel before any
    ** script has touched it.  Leaves nothing on the stack: the macro calls
    ** lua_getref straight after.
    */
    void SetupRefTable( lua_State *L )
    {
        lua_newtable( L );
        m_nTableReference = luaL_ref( L, LUA_REGISTRYINDEX );
    }

    /*
    ** HL2SB's vgui::Label has no GetContentAlignment -- Experiment's does -- so the
    ** value is remembered here.  The Lua binding calls SetContentAlignment, which
    ** resolves to this overload and forwards to the base.
    */
    void SetContentAlignment( Label::Alignment alignment )
    {
        m_iContentAlignment = alignment;
        BaseClass::SetContentAlignment( alignment );
    }

    Label::Alignment GetContentAlignment() const
    {
        return m_iContentAlignment;
    }

    public:
#if defined( LUA_SDK )
    lua_State          *m_lua_State;
    int                 m_nTableReference;
    Label::Alignment    m_iContentAlignment = Label::a_center;
#endif

    protected:
    virtual void ApplySchemeSettings( vgui::IScheme *pScheme )
    {
        LUA_CALL_PANEL_METHOD_BEGIN( "ApplySchemeSettings" );
        LUA_CALL_PANEL_METHOD_END( 0, 0 );

        BaseClass::ApplySchemeSettings( pScheme );
    }

    /*
    ** HL2SB: the rest of the Lua callbacks for Label-derived controls.
    **
    ** GMod's Derma builds DLabel on this class and DButton on top of DLabel
    ** (lua/vgui/dbutton.lua: derma.DefineControl( "DButton", ..., "DLabel" )), and
    ** a Lua method that C++ never dispatches simply does not run.  Only
    ** ApplySchemeSettings was wired up, so every DButton -- the player model
    ** panel's Apply button, a DListView's drag bar, every addon button -- was
    ** hoverable but unclickable: DButton:OnMousePressed/OnMouseReleased are what
    ** set Depressed and call DoClick.  Measured in game before this: hovering the
    ** button changed the cursor, clicking it produced no event at all, and
    ** DButton:Paint (the skin background) never ran either.
    **
    ** Paint dispatches BEFORE BaseClass::Paint() because Label::Paint is what
    ** draws the text: GMod's DButton:Paint paints the skin background first and
    ** the engine text lands on top of it.
    */
    virtual void Paint()
    {
        LUA_CALL_PANEL_METHOD_BEGIN( "Paint" );
            lua_pushinteger( m_lua_State, GetWide() );
            lua_pushinteger( m_lua_State, GetTall() );
        LUA_CALL_PANEL_METHOD_END( 2, 0 );

        BaseClass::Paint();
    }

    virtual void PerformLayout()
    {
        BaseClass::PerformLayout();

        LUA_CALL_PANEL_METHOD_BEGIN( "PerformLayout" );
            lua_pushinteger( m_lua_State, GetWide() );
            lua_pushinteger( m_lua_State, GetTall() );
        LUA_CALL_PANEL_METHOD_END( 2, 0 );
    }

    virtual void ApplySettings( KeyValues *pKeyValues )
    {
        BaseClass::ApplySettings( pKeyValues );

        LUA_CALL_PANEL_METHOD_BEGIN( "ApplySettings" );
        LUA_CALL_PANEL_METHOD_END( 0, 0 );
    }

    virtual void OnThink()
    {
        BaseClass::OnThink();

        LUA_CALL_PANEL_METHOD_BEGIN( "OnThink" );
        LUA_CALL_PANEL_METHOD_END( 0, 0 );
    }

    virtual void OnMousePressed( MouseCode code )
    {
        BaseClass::OnMousePressed( code );

        LUA_CALL_PANEL_METHOD_BEGIN( "OnMousePressed" );
            lua_pushinteger( m_lua_State, code );
        LUA_CALL_PANEL_METHOD_END( 1, 0 );
    }

    virtual void OnMouseReleased( MouseCode code )
    {
        BaseClass::OnMouseReleased( code );

        LUA_CALL_PANEL_METHOD_BEGIN( "OnMouseReleased" );
            lua_pushinteger( m_lua_State, code );
        LUA_CALL_PANEL_METHOD_END( 1, 0 );
    }

    virtual void OnMouseDoublePressed( MouseCode code )
    {
        BaseClass::OnMouseDoublePressed( code );

        LUA_CALL_PANEL_METHOD_BEGIN( "OnMouseDoublePressed" );
            lua_pushinteger( m_lua_State, code );
        LUA_CALL_PANEL_METHOD_END( 1, 0 );
    }

    virtual void OnMouseTriplePressed( MouseCode code )
    {
        BaseClass::OnMouseTriplePressed( code );

        LUA_CALL_PANEL_METHOD_BEGIN( "OnMouseTriplePressed" );
            lua_pushinteger( m_lua_State, code );
        LUA_CALL_PANEL_METHOD_END( 1, 0 );
    }

    // Panel::OnMouseWheeled returns void in this fork (LPanel's override matches).
    virtual void OnMouseWheeled( int delta )
    {
        BaseClass::OnMouseWheeled( delta );

        LUA_CALL_PANEL_METHOD_BEGIN( "OnMouseWheeled" );
            lua_pushinteger( m_lua_State, delta );
        LUA_CALL_PANEL_METHOD_END( 1, 1 );

        RETURN_LUA_PANEL_NONE();
    }

    virtual void OnCursorEntered()
    {
        BaseClass::OnCursorEntered();

        LUA_CALL_PANEL_METHOD_BEGIN( "OnCursorEntered" );
        LUA_CALL_PANEL_METHOD_END( 0, 0 );
    }

    virtual void OnCursorExited()
    {
        BaseClass::OnCursorExited();

        LUA_CALL_PANEL_METHOD_BEGIN( "OnCursorExited" );
        LUA_CALL_PANEL_METHOD_END( 0, 0 );
    }

    virtual void OnCursorMoved( int x, int y )
    {
        BaseClass::OnCursorMoved( x, y );

        LUA_CALL_PANEL_METHOD_BEGIN( "OnCursorMoved" );
            lua_pushinteger( m_lua_State, x );
            lua_pushinteger( m_lua_State, y );
        LUA_CALL_PANEL_METHOD_END( 2, 0 );
    }

    virtual void OnKeyCodePressed( ButtonCode_t code )
    {
        BaseClass::OnKeyCodePressed( code );

        LUA_CALL_PANEL_METHOD_BEGIN( "OnKeyCodePressed" );
            lua_pushinteger( m_lua_State, code );
        LUA_CALL_PANEL_METHOD_END( 1, 0 );
    }

    virtual void OnKeyCodeReleased( ButtonCode_t code )
    {
        BaseClass::OnKeyCodeReleased( code );

        LUA_CALL_PANEL_METHOD_BEGIN( "OnKeyCodeReleased" );
            lua_pushinteger( m_lua_State, code );
        LUA_CALL_PANEL_METHOD_END( 1, 0 );
    }

    virtual void OnKeyCodeTyped( ButtonCode_t code )
    {
        BaseClass::OnKeyCodeTyped( code );

        LUA_CALL_PANEL_METHOD_BEGIN( "OnKeyCodeTyped" );
            lua_pushinteger( m_lua_State, code );
        LUA_CALL_PANEL_METHOD_END( 1, 0 );
    }

    virtual void OnSetFocus()
    {
        BaseClass::OnSetFocus();

        LUA_CALL_PANEL_METHOD_BEGIN( "OnSetFocus" );
        LUA_CALL_PANEL_METHOD_END( 0, 0 );
    }

    virtual void OnKillFocus()
    {
        BaseClass::OnKillFocus();

        LUA_CALL_PANEL_METHOD_BEGIN( "OnKillFocus" );
        LUA_CALL_PANEL_METHOD_END( 0, 0 );
    }

    virtual void OnMouseFocusTicked()
    {
        BaseClass::OnMouseFocusTicked();

        LUA_CALL_PANEL_METHOD_BEGIN( "OnMouseFocusTicked" );
        LUA_CALL_PANEL_METHOD_END( 0, 0 );
    }

    virtual void OnKeyFocusTicked()
    {
        BaseClass::OnKeyFocusTicked();

        LUA_CALL_PANEL_METHOD_BEGIN( "OnKeyFocusTicked" );
        LUA_CALL_PANEL_METHOD_END( 0, 0 );
    }

    virtual void OnMouseCaptureLost()
    {
        BaseClass::OnMouseCaptureLost();

        LUA_CALL_PANEL_METHOD_BEGIN( "OnMouseCaptureLost" );
        LUA_CALL_PANEL_METHOD_END( 0, 0 );
    }

    virtual void OnSizeChanged( int nNewWide, int nNewTall )
    {
        BaseClass::OnSizeChanged( nNewWide, nNewTall );

        LUA_CALL_PANEL_METHOD_BEGIN( "OnSizeChanged" );
            lua_pushinteger( m_lua_State, nNewWide );
            lua_pushinteger( m_lua_State, nNewTall );
        LUA_CALL_PANEL_METHOD_END( 2, 0 );
    }

    virtual void OnScreenSizeChanged( int nOldWide, int nOldTall )
    {
        BaseClass::OnScreenSizeChanged( nOldWide, nOldTall );

        LUA_CALL_PANEL_METHOD_BEGIN( "OnScreenSizeChanged" );
            lua_pushinteger( m_lua_State, nOldWide );
            lua_pushinteger( m_lua_State, nOldTall );
        LUA_CALL_PANEL_METHOD_END( 2, 0 );
    }

    virtual void OnCommand( const char *command )
    {
        LUA_CALL_PANEL_METHOD_BEGIN( "OnCommand" );
            lua_pushstring( m_lua_State, command );
        LUA_CALL_PANEL_METHOD_END( 1, 1 );

        RETURN_LUA_PANEL_NONE();

        BaseClass::OnCommand( command );
    }
};

}  // namespace vgui

/* type for Label functions */
typedef LLabel lua_Label;

/*
** access functions (stack -> C)
*/

LUA_API lua_Label *( lua_tolabel )( lua_State *L, int idx );

/*
** push functions (C -> stack)
*/
LUALIB_API lua_Label *( luaL_checklabel )( lua_State *L, int narg );

#endif  // LLABEL_H
