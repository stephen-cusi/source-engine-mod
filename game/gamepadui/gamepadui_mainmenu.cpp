#include "gamepadui_interface.h"
#include "gamepadui_basepanel.h"
#include "gamepadui_mainmenu.h"

#include "vgui/ISurface.h"
#include "vgui/ILocalize.h"
#include "vgui/IVGui.h"

#include "KeyValues.h"
#include "filesystem.h"

#include "tier0/memdbgon.h"

// command line
#include "tier0/icommandline.h"

#define GAMEPADUI_MAINMENU_SCHEME GAMEPADUI_RESOURCE_FOLDER "schememainmenu.res"
#define GAMEPADUI_MAINMENU_FILE GAMEPADUI_RESOURCE_FOLDER "mainmenu.res"

GamepadUIMainMenu::GamepadUIMainMenu( vgui::Panel* pParent )
    : BaseClass( pParent, "MainMenu" )
{
    vgui::HScheme hScheme = vgui::scheme()->LoadSchemeFromFileEx( GamepadUI::GetInstance().GetSizingVPanel(), GAMEPADUI_MAINMENU_SCHEME, "SchemeMainMenu" );
    SetScheme( hScheme );

    KeyValues* pModData = new KeyValues( "ModData" );
    if ( pModData )
    {
        if ( pModData->LoadFromFile( g_pFullFileSystem, "gameinfo.txt" ) )
        {
            m_LogoText[ 0 ] = pModData->GetString( "gamepadui_title", pModData->GetString( "title" ) );
            m_LogoText[ 1 ] = pModData->GetString( "gamepadui_title2", pModData->GetString( "title2" ) );
        }
        pModData->deleteThis();
    }

    LoadMenuButtons();
    // we don t need this funtions
    // SetFooterButtons( FooterButtons::Select, FooterButtons::Select );
}

void GamepadUIMainMenu::UpdateGradients()
{
    const float flTime = GamepadUI::GetInstance().GetTime();
    GamepadUI::GetInstance().GetGradientHelper()->ResetTargets( flTime );
#ifdef GAMEPADUI_GAME_EZ2
    // E:Z2 reduces the gradient so that the background map can be more easily seen
    GamepadUI::GetInstance().GetGradientHelper()->SetTargetGradient( GradientSide::Left, { 1.0f, GamepadUI::GetInstance().IsInBackgroundLevel() ? 0.333f : 0.666f }, flTime );
#else
    GamepadUI::GetInstance().GetGradientHelper()->SetTargetGradient( GradientSide::Left, { 1.0f, 0.666f }, flTime );
#endif

    // In case a controller is added mid-game
    // SetFooterButtons( FooterButtons::Select, FooterButtons::Select );
}

void GamepadUIMainMenu::LoadMenuButtons()
{
    KeyValues* pDataFile = new KeyValues( "MainMenuScript" );
    if ( pDataFile )
    {
        if ( pDataFile->LoadFromFile( g_pFullFileSystem, GAMEPADUI_MAINMENU_FILE ) )
        {
            for ( KeyValues* pData = pDataFile->GetFirstSubKey(); pData != NULL; pData = pData->GetNextKey() )
            {
                GamepadUIButton* pButton = new GamepadUIButton(
                    this, this,
                    GAMEPADUI_MAINMENU_SCHEME,
                    pData->GetString( "command" ),
                    pData->GetString( "text", "Sample Text" ),
                    pData->GetString( "description", "" ) );
                pButton->SetName( pData->GetName() );
                pButton->SetPriority( V_atoi( pData->GetString( "priority", "1" ) ) );
                pButton->SetVisible( true );

                const char* pFamily = pData->GetString( "family", "all" );
                
                if ( !V_strcmp( pFamily, "ingame" ) || !V_strcmp( pFamily, "all" ) )

                    m_Buttons[ GamepadUIMenuStates::InGame ].AddToTail( pButton );

                if ( !V_strcmp( pFamily, "mainmenu" ) || !V_strcmp( pFamily, "all" ) )

                    m_Buttons[ GamepadUIMenuStates::MainMenu ].AddToTail( pButton );
            }
        }

        pDataFile->deleteThis();
    }

 
    // 判断是否显示控制台按钮 pwd ZZHlife
    bool bShowConsoleButton = CommandLine()->FindParm("-console");
    SetConsoleButtonVisibility(bShowConsoleButton);
    UpdateButtonVisibility();
}
// fun 判断是否显示控制台按钮 pwd ZZHlife
// 注意 无论是否显示控制台按钮，都会创建一个控制台按钮，只是设置可见性
// 否则 会 有内存泄漏 导致崩溃 
// add .
void GamepadUIMainMenu::SetConsoleButtonVisibility(bool bVisible)
{
    if (!m_pSwitchToOldUIButton)
    {
        m_pSwitchToOldUIButton = new GamepadUIButton(this, this,GAMEPADUI_RESOURCE_FOLDER "schememainmenu_olduibutton.res", "cmd gamemenucommand openconsole","#GameUI_Console", "");
        m_pSwitchToOldUIButton->SetPriority(0); // 优先级
    }
     m_pSwitchToOldUIButton = new GamepadUIButton(this, this,GAMEPADUI_RESOURCE_FOLDER "schememainmenu_olduibutton.res","cmd gamemenucommand openconsole","#GameUI_Console", "");
     m_pSwitchToOldUIButton->SetVisible(bVisible); // 设置可见性
     // 设置优先级
}

void GamepadUIMainMenu::ApplySchemeSettings( vgui::IScheme* pScheme )
{
    BaseClass::ApplySchemeSettings( pScheme );
    int nParentW, nParentH;
	GetParent()->GetSize( nParentW, nParentH );
    SetBounds( 0, 0, nParentW, nParentH );

    const char *pImage = pScheme->GetResourceString( "Logo.Image" );
    if ( pImage && *pImage )
        m_LogoImage.SetImage( pImage );
    m_hLogoFont = pScheme->GetFont( "Logo.Font", true );


}

void GamepadUIMainMenu::LayoutMainMenu()
{
    m_flOldUIButtonOffsetX = 20.0f; 
    m_flOldUIButtonOffsetY = 20.0f; 
    int nY = GetCurrentButtonOffset();
    CUtlVector<GamepadUIButton*>& currentButtons = GetCurrentButtons();
    for ( GamepadUIButton *pButton : currentButtons )
    {
        nY += pButton->GetTall();
        pButton->SetPos( m_flButtonsOffsetX, GetTall() - nY );
        nY += m_flButtonSpacing;
    }
     int nParentW, nParentH;
     GetParent()->GetSize( nParentW, nParentH );
     m_pSwitchToOldUIButton->SetPos( m_flOldUIButtonOffsetX, nParentH - m_pSwitchToOldUIButton->m_flHeight - m_flOldUIButtonOffsetY );
    
}

void GamepadUIMainMenu::PaintLogo()
{
    vgui::surface()->DrawSetTextColor( m_colLogoColor );
    vgui::surface()->DrawSetTextFont( m_hLogoFont );

    int nMaxLogosW = 0, nTotalLogosH = 0;
    int nLogoW[ 2 ], nLogoH[ 2 ];
    for ( int i = 0; i < 2; i++ )
    {
        nLogoW[ i ] = 0;
        nLogoH[ i ] = 0;
        if ( !m_LogoText[ i ].IsEmpty() )
            vgui::surface()->GetTextSize( m_hLogoFont, m_LogoText[ i ].String(), nLogoW[ i ], nLogoH[ i ] );
        nMaxLogosW = Max( nLogoW[ i ], nMaxLogosW );
        nTotalLogosH += nLogoH[ i ];
    }

    int nLogoY = GetTall() - ( GetCurrentLogoOffset() + nTotalLogosH );

    if ( m_LogoImage.IsValid() )
    {
        int nY1 = nLogoY;
        int nY2 = nY1 + nLogoH[ 0 ];
        int nX1 = m_flLogoOffsetX;
        int nX2 = nX1 + ( nLogoH[ 0 ] * 3 );
        vgui::surface()->DrawSetColor( Color( 255, 255, 255, 255 ) );
        vgui::surface()->DrawSetTexture( m_LogoImage );
        vgui::surface()->DrawTexturedRect( nX1, nY1, nX2, nY2 );
        vgui::surface()->DrawSetTexture( 0 );
    }
    else
    {
        for ( int i = 1; i >= 0; i-- )
        {
            vgui::surface()->DrawSetTextPos( m_flLogoOffsetX, nLogoY );
            vgui::surface()->DrawPrintText( m_LogoText[ i ].String(), m_LogoText[ i ].Length() );

            nLogoY -= nLogoH[ i ];
        }
    }
}

void GamepadUIMainMenu::OnThink()
{
    BaseClass::OnThink();

    LayoutMainMenu();
}

void GamepadUIMainMenu::Paint()
{
    BaseClass::Paint();

    PaintLogo();
}

void GamepadUIMainMenu::OnCommand( char const* pCommand )
{
    if ( StringHasPrefixCaseSensitive( pCommand, "cmd " ) )
    {
        const char* pszClientCmd = &pCommand[ 4 ];
        if ( *pszClientCmd )
            GamepadUI::GetInstance().GetEngineClient()->ClientCmd_Unrestricted( pszClientCmd );

        // This is a hack to reset bonus challenges in the event that the player disconnected before the map loaded.
        // We have no known way of detecting that event and differentiating between a bonus level and non-bonus level being loaded,
        // so for now, we just reset this when the player presses any menu button, as that indicates they are in the menu and no longer loading a bonus level
        // (note that this does not cover loading a map through other means, like through the console)
        ConVarRef sv_bonus_challenge( "sv_bonus_challenge" );
        if (sv_bonus_challenge.GetInt() != 0)
        {
            GamepadUI_Log( "Resetting sv_bonus_challenge\n" );
            sv_bonus_challenge.SetValue( 0 );
        }
    }
    else
    {
        BaseClass::OnCommand( pCommand );
    }
}

void GamepadUIMainMenu::OnSetFocus()
{
    BaseClass::OnSetFocus();
    OnMenuStateChanged();
}

void GamepadUIMainMenu::OnMenuStateChanged()
{
    UpdateGradients();
    UpdateButtonVisibility();
}

void GamepadUIMainMenu::UpdateButtonVisibility()
{
    for ( CUtlVector<GamepadUIButton*>& buttons : m_Buttons )
    {
        for ( GamepadUIButton* pButton : buttons )
        {
            pButton->NavigateFrom();
            pButton->SetVisible( false );
        }
    }

    CUtlVector<GamepadUIButton*>& currentButtons = GetCurrentButtons();
    currentButtons.Sort( []( GamepadUIButton* const* a, GamepadUIButton* const* b ) -> int
    {
        return ( ( *a )->GetPriority() > ( *b )->GetPriority() );
    });

    for ( int i = 1; i < currentButtons.Count(); i++ )
    {
        currentButtons[i]->SetNavDown( currentButtons[i - 1] );
        currentButtons[i - 1]->SetNavUp( currentButtons[i] );
    }

    for ( GamepadUIButton* pButton : currentButtons )
        pButton->SetVisible( true );

    if ( !currentButtons.IsEmpty() )
        currentButtons[ currentButtons.Count() - 1 ]->NavigateTo();
}

void GamepadUIMainMenu::OnKeyCodeReleased( vgui::KeyCode code )
{
    ButtonCode_t buttonCode = GetBaseButtonCode( code );
    switch (buttonCode)
    {
#ifdef HL2_RETAIL // Steam input and Steam Controller are not supported in SDK2013 (Madi)
    case STEAMCONTROLLER_B:
#endif

    case KEY_XBUTTON_B:
        if ( GamepadUI::GetInstance().IsInLevel() )
        {
            GamepadUI::GetInstance().GetEngineClient()->ClientCmd_Unrestricted( "gamemenucommand resumegame" );
            // I tried it and didn't like it.
            // Oh well.
            //vgui::surface()->PlaySound( "UI/buttonclickrelease.wav" );
        }
        break;
    default:
        BaseClass::OnKeyCodeReleased( code );
        break;
    }
}

GamepadUIMenuState GamepadUIMainMenu::GetCurrentMenuState() const
{
    if ( GamepadUI::GetInstance().IsInLevel() )
        return GamepadUIMenuStates::InGame;
    return GamepadUIMenuStates::MainMenu;
}

CUtlVector<GamepadUIButton*>& GamepadUIMainMenu::GetCurrentButtons()
{
    return m_Buttons[ GetCurrentMenuState() ];
}

float GamepadUIMainMenu::GetCurrentButtonOffset()
{
    return GetCurrentMenuState() == GamepadUIMenuStates::InGame ? m_flButtonsOffsetYInGame : m_flButtonsOffsetYMenu;
}

float GamepadUIMainMenu::GetCurrentLogoOffset()
{
    return GetCurrentMenuState() == GamepadUIMenuStates::InGame ? m_flLogoOffsetYInGame : m_flLogoOffsetYMenu;
}
