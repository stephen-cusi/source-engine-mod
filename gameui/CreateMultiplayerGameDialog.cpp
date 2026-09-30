//========= Copyright Valve Corporation, All rights reserved. ============//
//
// Purpose: GMod-style fullscreen "create server" dialog
//
// $NoKeywords: $
//=============================================================================//

#include "CreateMultiplayerGameDialog.h"

// include original page classes so we can still read cvars / bot settings
#include "CreateMultiplayerGameServerPage.h"
#include "CreateMultiplayerGameGameplayPage.h"
#include "CreateMultiplayerGameBotPage.h"

#include "EngineInterface.h"
#include "ModInfo.h"
#include "GameUI_Interface.h"
#include "PNGImagePanel.h"
#include "MouseMessageForwardingPanel.h"

#include <stdio.h>

using namespace vgui;

#include "vgui_controls/ComboBox.h"
#include "vgui_controls/TextEntry.h"
#include "vgui_controls/Button.h"
#include "vgui_controls/Label.h"
#include "vgui_controls/PanelListPanel.h"
#include "vgui_controls/ImagePanel.h"
#include "vgui_controls/CheckButton.h"
#include <vgui/ILocalize.h>
#include <vgui/ISurface.h>
#include <vgui/IScheme.h>

#include "filesystem.h"
#include <KeyValues.h>
#include "tier1/convar.h"
#include <tier0/memdbgon.h>

#define RANDOM_MAP "#GameUI_RandomMap"
#define MAX_PLAYERS_DEFAULT 32

//-----------------------------------------------------------------------------
// Purpose: A single selectable map card (thumbnail + name)
//-----------------------------------------------------------------------------
class CMapCardPanel : public vgui::EditablePanel
{
	DECLARE_CLASS_SIMPLE( CMapCardPanel, vgui::EditablePanel );
public:
	CMapCardPanel( PanelListPanel *parent, const char *name, CCreateMultiplayerGameDialog *pOwner, const char *pszMapName )
		: BaseClass( parent, name )
	{
		m_pOwner = pOwner;
		Q_strncpy( m_szMapName, pszMapName, sizeof( m_szMapName ) );

		m_pThumb = new CPNGImagePanel( this, "MapThumb" );
		m_pThumb->SetMouseInputEnabled( false );
		m_pThumb->SetMapImage( pszMapName );

		m_pName = new Label( this, "MapName", pszMapName );
		m_pName->SetMouseInputEnabled( false );

		// transparent panel that fills the card and forwards clicks to us
		m_pClickCatcher = new CMouseMessageForwardingPanel( this, NULL );
		m_pClickCatcher->SetZPos( 2 );

		// Square-ish card to match the 128x128 map thumbnails (image + name bar)
		SetSize( 150, 178 );
		SetPaintBackgroundEnabled( true );
		m_bSelected = false;
	}

	~CMapCardPanel() {}

	const char *GetMapName() const { return m_szMapName; }

	virtual void ApplySchemeSettings( vgui::IScheme *pScheme )
	{
		BaseClass::ApplySchemeSettings( pScheme );

		if ( m_pName )
		{
			m_pName->SetContentAlignment( Label::a_center );
			m_pName->SetTextInset( 0, 0 );
		}
	}

	virtual void PerformLayout()
	{
		BaseClass::PerformLayout();

		int w, h;
		GetSize( w, h );

		// Square thumbnail area (matches the 128x128 source), name bar at bottom
		int nameBarH = 24;
		int pad = 6;

		if ( m_pThumb )
		{
			// Keep the thumb square, centered horizontally, filling the height above the name bar
			int thumbArea = h - nameBarH - pad;
			int size = w - pad * 2;
			if ( size > thumbArea )
				size = thumbArea;
			int x = (w - size) / 2;
			m_pThumb->SetBounds( x, pad, size, size );
		}
		if ( m_pName )
		{
			m_pName->SetBounds( pad, h - nameBarH - 2, w - pad * 2, nameBarH - 2 );
		}
	}

	// Forwarded from the click-catcher panel (CMouseMessageForwardingPanel ->
	// CallParentFunction("MousePressed")).
	virtual void OnMousePressed( vgui::MouseCode code )
	{
		BaseClass::OnMousePressed( code );
		if ( m_pOwner )
		{
			m_pOwner->OnMapSelected( m_szMapName );
		}
	}

	virtual void PaintBackground()
	{
		// selected highlight
		if ( m_bSelected )
		{
			vgui::surface()->DrawSetColor( 255, 200, 0, 90 );
			vgui::surface()->DrawFilledRect( 0, 0, GetWide(), GetTall() );
		}
		else
		{
			vgui::surface()->DrawSetColor( 0, 0, 0, 120 );
			vgui::surface()->DrawFilledRect( 0, 0, GetWide(), GetTall() );
		}
	}

	void SetSelected( bool b ) { m_bSelected = b; }

private:
	CPNGImagePanel *m_pThumb;
	Label *m_pName;
	CMouseMessageForwardingPanel *m_pClickCatcher;
	CCreateMultiplayerGameDialog *m_pOwner;
	char m_szMapName[256];
	bool m_bSelected;
};

//-----------------------------------------------------------------------------
// Purpose: Constructor
//-----------------------------------------------------------------------------
CCreateMultiplayerGameDialog::CCreateMultiplayerGameDialog(vgui::Panel *parent) : BaseClass(parent, "CreateMultiplayerGameDialog")
{
	m_bBotsEnabled = false;
	m_bBuilt = false;
	m_pSavedData = NULL;
	m_szSelectedMap[0] = 0;

	// Zero every child pointer up front. The children are only created inside
	// ApplySchemeSettings() (guarded by m_bBuilt), but OnMapSelected() ->
	// RefreshSelection() can run from that same path, and any reentrant or
	// early call would otherwise read an uninitialised garbage pointer
	// (observed: 0xffeeffee -> access violation in m_pSelectedMapLabel->SetText).
	m_pGameModeList = NULL;
	m_pMapList = NULL;
	m_pHostName = NULL;
	m_pPassword = NULL;
	m_pMaxPlayers = NULL;
	m_pSelectedMapLabel = NULL;
	m_pStartButton = NULL;
	m_pBackButton = NULL;
	m_pTitleLabel = NULL;
	m_pHostNameLabel = NULL;
	m_pPasswordLabel = NULL;
	m_pMaxPlayersLabel = NULL;
	m_pFallDamageCheck = NULL;
	m_pMaxAmmoLabel = NULL;
	m_pMaxAmmo = NULL;
	m_pLoadoutCheck = NULL;
	m_pGodModeCheck = NULL;
	m_pSuitCheck = NULL;

	SetDeleteSelfOnClose(true);

	// Content & chrome are built in ApplySchemeSettings() (first call), which
	// runs after the Frame is fully constructed. Building children or invoking
	// Frame chome mutators here crashes the Frame's half-built internals.
}

//-----------------------------------------------------------------------------
// Purpose: Destructor
//-----------------------------------------------------------------------------
CCreateMultiplayerGameDialog::~CCreateMultiplayerGameDialog()
{
	if ( m_pSavedData )
	{
		m_pSavedData->deleteThis();
		m_pSavedData = NULL;
	}

	for ( int i = 0; i < m_MapNames.Count(); ++i )
	{
		delete[] m_MapNames[i];
	}
	m_MapNames.RemoveAll();
}

//-----------------------------------------------------------------------------
// Purpose: Layout everything
//-----------------------------------------------------------------------------
void CCreateMultiplayerGameDialog::PerformLayout()
{
	BaseClass::PerformLayout();

	int nScreenW, nScreenH;
	vgui::surface()->GetScreenSize( nScreenW, nScreenH );
	int sw = nScreenW;
	int sh = nScreenH;

	// left column (map categories)
	int leftX = (int)(sw * 0.02);
	int leftW = (int)(sw * 0.14);
	int topY = (int)(sh * 0.16);
	int bottomY = (int)(sh * 0.92);

	if ( m_pGameModeList )
		m_pGameModeList->SetBounds( leftX, topY, leftW, bottomY - topY );

	// layout the category buttons vertically in the left column
	{
		int y = topY;
		for ( int i = 0; i < m_CategoryButtons.Count(); ++i )
		{
			if ( m_CategoryButtons[i] )
			{
				m_CategoryButtons[i]->SetBounds( leftX, y, leftW, 28 );
				y += 34;
			}
		}
	}

	// center map grid
	int mapX = (int)(sw * 0.18);
	int mapW = (int)(sw * 0.52);
	if ( m_pMapList )
		m_pMapList->SetBounds( mapX, topY, mapW, bottomY - topY );

	// right settings panel
	int rightX = (int)(sw * 0.72);
	int rightW = sw - rightX - (int)(sw * 0.02);

	if ( m_pTitleLabel )
		m_pTitleLabel->SetBounds( (int)(sw * 0.18), (int)(sh * 0.05), (int)(sw * 0.5), 40 );

	if ( m_pSelectedMapLabel )
		m_pSelectedMapLabel->SetBounds( rightX, topY, rightW, 24 );

	if ( m_pHostNameLabel )
		m_pHostNameLabel->SetBounds( rightX, topY + 34, rightW, 20 );
	if ( m_pHostName )
		m_pHostName->SetBounds( rightX, topY + 54, rightW, 26 );

	if ( m_pPasswordLabel )
		m_pPasswordLabel->SetBounds( rightX, topY + 90, rightW, 20 );
	if ( m_pPassword )
		m_pPassword->SetBounds( rightX, topY + 110, rightW, 26 );

	if ( m_pMaxPlayersLabel )
		m_pMaxPlayersLabel->SetBounds( rightX, topY + 146, rightW, 20 );
	if ( m_pMaxPlayers )
		m_pMaxPlayers->SetBounds( rightX, topY + 166, rightW, 26 );

	if ( m_pFallDamageCheck )
		m_pFallDamageCheck->SetBounds( rightX, topY + 200, rightW, 24 );

	// HL2SB (2026-09-28): sidebar options stack below the fall-damage row
	// (label+input for the ammo cap, then three checkboxes), well above the
	// start button at ~0.93 of the screen height.
	{
		int optY = topY + 234;
		if ( m_pMaxAmmoLabel )
			m_pMaxAmmoLabel->SetBounds( rightX, optY, rightW, 20 );
		if ( m_pMaxAmmo )
			m_pMaxAmmo->SetBounds( rightX, optY + 20, rightW, 26 );
		optY += 54;
		if ( m_pLoadoutCheck )
			m_pLoadoutCheck->SetBounds( rightX, optY, rightW, 24 );
		optY += 30;
		if ( m_pGodModeCheck )
			m_pGodModeCheck->SetBounds( rightX, optY, rightW, 24 );
		optY += 30;
		if ( m_pSuitCheck )
			m_pSuitCheck->SetBounds( rightX, optY, rightW, 24 );
	}

	// start / back buttons at bottom
	int btnH = (int)(sh * 0.05);
	int btnY = sh - btnH - (int)(sh * 0.02);

	if ( m_pStartButton )
	{
		m_pStartButton->SetBounds( rightX, btnY, (int)(rightW * 0.55), btnH );
		const wchar_t *pwsz = g_pVGuiLocalize->Find( "#GameUI_StartGame" );
		m_pStartButton->SetText( pwsz ? pwsz : L"Start Game" );
	}

	if ( m_pBackButton )
	{
		m_pBackButton->SetBounds( (int)(sw * 0.02), btnY, (int)(rightW * 0.3), btnH );
		const wchar_t *pwsz = g_pVGuiLocalize->Find( "#GameUI_Back" );
		m_pBackButton->SetText( pwsz ? pwsz : L"Back" );
	}
}

//-----------------------------------------------------------------------------
// Purpose:
//-----------------------------------------------------------------------------
void CCreateMultiplayerGameDialog::ApplySchemeSettings( vgui::IScheme *pScheme )
{
	BaseClass::ApplySchemeSettings( pScheme );

	if ( !m_bBuilt )
	{
		m_bBuilt = true;

		SetTitle("#GameUI_CreateServer", false);

		if ( ModInfo().UseBots() )
		{
			m_bBotsEnabled = true;
		}

		m_pGameModeList = new vgui::PanelListPanel( this, "GameModeList" );
		m_pGameModeList->SetFirstColumnWidth( 0 );
		// replaced by directly-owned category buttons; keep the empty list
		// panel from eating clicks behind them.
		m_pGameModeList->SetVisible( false );

		m_pMapList = new vgui::PanelListPanel( this, "MapList" );
		m_pMapList->SetFirstColumnWidth( 0 );

		m_pSelectedMapLabel = new Label( this, "SelectedMapLabel", "" );
		m_pHostName = new TextEntry( this, "HostName" );
		m_pPassword = new TextEntry( this, "Password" );
		m_pMaxPlayers = new ComboBox( this, "MaxPlayers", 8, false );

		m_pTitleLabel = new Label( this, "TitleLabel", "#GameUI_CreateServer" );
		m_pHostNameLabel = new Label( this, "HostNameLabel", "#GameUI_ServerName" );
		m_pPasswordLabel = new Label( this, "PasswordLabel", "#GameUI_Password" );
		m_pMaxPlayersLabel = new Label( this, "MaxPlayersLabel", "#GameUI_MaxPlayers" );

		// Realistic fall damage.  Mirrors mp_falldamage (1 = on, 0 = off); the
		// sandbox default is off.  The state is applied once m_pSavedData is
		// loaded, a few lines below.
		m_pFallDamageCheck = new CheckButton( this, "FallDamageCheck", "#HL2SB_FallDamage" );

		// HL2SB (2026-09-28): singleplayer sidebar options -- GMod's new-game
		// column (persistence file excluded).  gmod_maxammo / gmod_suit are
		// engine convars and reach the server through cfg/listenserver.cfg;
		// sbox_weapons / sbox_godmode are Lua convars created by
		// gamemodes/sandbox/gamemode/init.lua, which also applies their values
		// from ServerConfig.vdf (written by SaveConfig below, before "map").
		m_pMaxAmmoLabel = new Label( this, "MaxAmmoLabel", "#HL2SB_MaxAmmo" );
		m_pMaxAmmo = new TextEntry( this, "MaxAmmo" );
		m_pMaxAmmo->SetAllowNumericInputOnly( true );
		m_pMaxAmmo->SetMaximumCharCount( 7 );
		m_pLoadoutCheck = new CheckButton( this, "LoadoutCheck", "#HL2SB_GiveWeapons" );
		m_pGodModeCheck = new CheckButton( this, "GodModeCheck", "#HL2SB_GodMode" );
		m_pSuitCheck = new CheckButton( this, "SuitCheck", "#HL2SB_EnableHEV" );

		m_pStartButton = new Button( this, "StartButton", "#GameUI_Start" );
		m_pStartButton->SetCommand( "CreateGame" );
		m_pStartButton->SetVisible( true );

		m_pBackButton = new Button( this, "BackButton", "#GameUI_Back" );
		m_pBackButton->SetCommand( "Close" );
		m_pBackButton->SetVisible( true );

		// HL2SB: start at 1 (real single player - game.SinglePlayer() is
		// maxclients<=1) and stop at 64, the largest value GetPlayerLimits
		// actually serves; offering 128 here used to silently start a
		// clamped server.  The 1 entry reads "single player" and is the
		// default selection.
		for ( int i = 1; i <= 64; i *= 2 )
		{
			if ( i == 1 )
			{
				const wchar_t *pwsz = g_pVGuiLocalize->Find( "#HL2SB_SinglePlayer" );
				m_pMaxPlayers->AddItem( pwsz ? pwsz : L"Single Player",
					new KeyValues( "maxplayers", "val", i ) );
				continue;
			}
			char sz[16];
			Q_snprintf( sz, sizeof( sz ), "%d", i );
			m_pMaxPlayers->AddItem( sz, new KeyValues( "maxplayers", "val", i ) );
		}
		m_pMaxPlayers->ActivateItemByRow( 0 );

		m_pSavedData = new KeyValues( "ServerConfig" );
		if ( m_pSavedData )
		{
			m_pSavedData->LoadFromFile( g_pFullFileSystem, "ServerConfig.vdf", "GAME" );
		}

		if ( m_pFallDamageCheck )
			m_pFallDamageCheck->SetSelected( GetFallDamage() );

		// HL2SB sidebar: restore the four new options the same way.
		if ( m_pMaxAmmo )
		{
			char szAmmo[16];
			Q_snprintf( szAmmo, sizeof( szAmmo ), "%d", GetMaxAmmo() );
			m_pMaxAmmo->SetText( szAmmo );
		}
		if ( m_pLoadoutCheck )
			m_pLoadoutCheck->SetSelected( GetLoadout() );
		if ( m_pGodModeCheck )
			m_pGodModeCheck->SetSelected( GetGodMode() );
		if ( m_pSuitCheck )
			m_pSuitCheck->SetSelected( GetSuit() );

		BuildGameModeList();
		BuildMapGrid();

		if ( m_pHostName )
			m_pHostName->SetText( ModInfo().GetGameName() );
		if ( m_pPassword )
			m_pPassword->SetText( "" );
		if ( m_pHostName )
			m_pHostName->SetMultiline( false );
		if ( m_pPassword )
			m_pPassword->SetMultiline( false );
	}

	int nScreenW, nScreenH;
	vgui::surface()->GetScreenSize( nScreenW, nScreenH );
	SetSize( nScreenW, nScreenH );
	SetPos( 0, 0 );
}

//-----------------------------------------------------------------------------
// Purpose: Build the game mode / category list (left). Each entry is a Button
//          owned by the dialog so clicking it filters the map grid.
//-----------------------------------------------------------------------------
void CCreateMultiplayerGameDialog::BuildGameModeList()
{
	// Remove any existing category buttons
	for ( int i = 0; i < m_CategoryButtons.Count(); ++i )
	{
		if ( m_CategoryButtons[i] )
			m_CategoryButtons[i]->MarkForDeletion();
	}
	m_CategoryButtons.RemoveAll();

	for ( int i = 0; i < MAPCAT_COUNT; ++i )
	{
		Button *pBtn = new Button( this, "GameModeLabel", GetCategoryName( i ) );
		pBtn->SetContentAlignment( Label::a_west );
		pBtn->SetTextInset( 8, 0 );
		pBtn->SetTall( 28 );

		char szCmd[32];
		Q_snprintf( szCmd, sizeof( szCmd ), "MapCat %d", i );
		pBtn->SetCommand( szCmd );

		// Highlight the active category WITHOUT using SetSelected(): the
		// game scheme's ButtonSelected text colour matches the hot list
		// background, so a selected button's label vanishes. Instead paint
		// an explicit background + keep a bright label visible.
		if ( i == m_iSelectedCategory )
		{
			pBtn->SetBgColor( Color( 255, 176, 32, 255 ) ); // GMod-tan
			pBtn->SetFgColor( Color( 20, 20, 20, 255 ) );
		}
		else
		{
			pBtn->SetBgColor( Color( 40, 42, 46, 255 ) );
			pBtn->SetFgColor( Color( 200, 200, 200, 255 ) );
		}
		pBtn->SetPaintBackgroundEnabled( true );
		pBtn->SetVisible( true );

		m_CategoryButtons.AddToTail( pBtn );
	}

	// relayout the buttons
	PerformLayout();
}

//-----------------------------------------------------------------------------
// Purpose: Human-readable name for a map category
//-----------------------------------------------------------------------------
const char *CCreateMultiplayerGameDialog::GetCategoryName( int iCategory )
{
	switch ( iCategory )
	{
		case MAPCAT_ALL:        return "All Maps";
		case MAPCAT_HL2:        return "Half-Life 2";
		case MAPCAT_HL2DM:      return "Half-Life 2: DM";
		case MAPCAT_SANDBOX:    return "GMod Sandbox";
		case MAPCAT_OTHER:      return "Other";
		default:                return "All Maps";
	}
}

//-----------------------------------------------------------------------------
// Purpose: Detect a map's category from its filesystem MOUNT path, not from a
//          hard-coded name. g_pFullFileSystem->GetLocalPath resolves the map
//          to its absolute disk path; which game folder it sits under tells us
//          which search path it was mounted from. A name-prefix heuristic only
//          kicks in when the path is indeterminate (e.g. a map supplied by a
//          custom vpk we can't resolve to a folder here).
//-----------------------------------------------------------------------------
int CCreateMultiplayerGameDialog::MapNameToCategory( const char *pszMapName )
{
	if ( !pszMapName || !pszMapName[0] )
		return MAPCAT_OTHER;

	// Resolve to a full disk path so we can see which mount this came from.
	char szRel[ 300 ];
	Q_snprintf( szRel, sizeof( szRel ), "maps/%s.bsp", pszMapName );

	char szLocal[ MAX_PATH ];
	if ( g_pFullFileSystem->GetLocalPath( szRel, szLocal, sizeof( szLocal ) ) )
	{
		// Normalise to lowercase for case-insensitive matching.
		char szLower[ MAX_PATH ];
		Q_strncpy( szLower, szLocal, sizeof( szLower ) );
		Q_strlower( szLower );

		// hl2 single-player campaign mount
		if ( Q_stristr( szLower, "\\hl2\\" ) || Q_stristr( szLower, "/hl2/" ) )
		{
			return MAPCAT_HL2;
		}
		// hl2mp mount
		if ( Q_stristr( szLower, "\\hl2mp\\" ) || Q_stristr( szLower, "/hl2mp/" ) )
		{
			return MAPCAT_HL2DM;
		}
		// our custom GMod maps addon (gm_/sb_/mm_ content we shipped as the
		// gmod_maps addon) resolves under custom/gmod_maps
		if ( Q_stristr( szLower, "gmod_maps" ) )
		{
			return MAPCAT_SANDBOX;
		}
	}

	// Fallback heuristics when the path can't be resolved (vpk-supplied maps).
	if ( !Q_strnicmp( pszMapName, "gm_", 3 ) ||
		 !Q_strnicmp( pszMapName, "sb_", 3 ) ||
		 !Q_strnicmp( pszMapName, "mm_", 3 ) ||
		 !Q_strnicmp( pszMapName, "rp_", 3 ) )
	{
		return MAPCAT_SANDBOX;
	}
	if ( !Q_strnicmp( pszMapName, "dm_", 3 ) )
	{
		return MAPCAT_HL2DM;
	}
	if ( !Q_strnicmp( pszMapName, "d1_", 3 ) ||
		 !Q_strnicmp( pszMapName, "d2_", 3 ) ||
		 !Q_strnicmp( pszMapName, "d3_", 3 ) ||
		 !Q_strnicmp( pszMapName, "ep1_", 3 ) ||
		 !Q_strnicmp( pszMapName, "ep2_", 3 ) )
	{
		return MAPCAT_HL2;
	}

	return MAPCAT_OTHER;
}

//-----------------------------------------------------------------------------
// Purpose: Rebuild the map grid honouring the currently selected category.
//          When a category is active, only maps belonging to it are shown.
//-----------------------------------------------------------------------------
void CCreateMultiplayerGameDialog::ApplyCategoryFilter()
{
	if ( !m_pMapList )
		return;

	m_pMapList->DeleteAllItems();
	m_MapNames.RemoveAll();

	FileFindHandle_t findHandle = NULL;
	const char *pszFilename = g_pFullFileSystem->FindFirstEx( "maps/*.bsp", "GAME", &findHandle );
	while ( pszFilename )
	{
		char mapname[256];
		Q_strncpy( mapname, pszFilename, sizeof( mapname ) - 1 );
		mapname[ sizeof(mapname) - 1 ] = 0;
		char *ext = Q_strstr( mapname, ".bsp" );
		if ( ext )
			*ext = 0;

		// apply category filter (skip when "All" is selected)
		if ( m_iSelectedCategory != MAPCAT_ALL )
		{
			if ( MapNameToCategory( mapname ) != m_iSelectedCategory )
				goto nextFile;
		}

		// dedup
		{
			bool bDup = false;
			for ( int i = 0; i < m_MapNames.Count(); ++i )
			{
				if ( !Q_stricmp( m_MapNames[i], mapname ) )
					{ bDup = true; break; }
			}
			if ( !bDup )
			{
				CMapCardPanel *pCard = new CMapCardPanel( m_pMapList, "MapCard", this, mapname );
				m_pMapList->AddItem( NULL, pCard );

				char *pszCopy = new char[ strlen(mapname) + 1 ];
				Q_strcpy( pszCopy, mapname );
				m_MapNames.AddToTail( pszCopy );
			}
		}

	nextFile:
		pszFilename = g_pFullFileSystem->FindNext( findHandle );
	}
	g_pFullFileSystem->FindClose( findHandle );

	m_pMapList->SetNumColumns( 3 );
	RefreshSelection();
}

//-----------------------------------------------------------------------------
// Purpose: Build the map grid
//-----------------------------------------------------------------------------
void CCreateMultiplayerGameDialog::BuildMapGrid()
{
	if ( !m_pMapList )
		return;

	// Builds the grid honouring the currently selected category.
	ApplyCategoryFilter();
}

//-----------------------------------------------------------------------------
// Purpose: Loads the list of available maps into the map list
//-----------------------------------------------------------------------------
//-----------------------------------------------------------------------------
// Purpose: Scan one search path for maps and add them to the grid (deduped)
//-----------------------------------------------------------------------------
void CCreateMultiplayerGameDialog::LoadMaps( const char *pszPathID )
{
	FileFindHandle_t findHandle = NULL;

	KeyValues *hiddenMaps = ModInfo().GetHiddenMaps();

	const char *pszFilename = g_pFullFileSystem->FindFirstEx( "maps/*.bsp", pszPathID, &findHandle );
	while ( pszFilename )
	{
		char mapname[256];
		char *ext;

		Q_strncpy( mapname, pszFilename, sizeof( mapname ) - 1 );
		mapname[ sizeof(mapname) - 1 ] = 0;

		ext = Q_strstr( mapname, ".bsp" );
		if ( ext )
		{
			*ext = 0;
		}

		// skip hidden maps
		if ( hiddenMaps )
		{
			if ( hiddenMaps->GetInt( mapname, 0 ) )
			{
				goto nextFile;
			}
		}

		// skip duplicates
		{
			bool bDup = false;
			for ( int i = 0; i < m_MapNames.Count(); ++i )
			{
				if ( !Q_stricmp( m_MapNames[i], mapname ) )
				{
					bDup = true;
					break;
				}
			}
			if ( !bDup )
			{
				// add a card to the grid
				CMapCardPanel *pCard = new CMapCardPanel( m_pMapList, "MapCard", this, mapname );
				m_pMapList->AddItem( NULL, pCard );

				// store map name for retrieval
				char *pszCopy = new char[ strlen(mapname) + 1 ];
				Q_strcpy( pszCopy, mapname );
				m_MapNames.AddToTail( pszCopy );
			}
		}

	nextFile:
		pszFilename = g_pFullFileSystem->FindNext( findHandle );
	}
	g_pFullFileSystem->FindClose( findHandle );
}

//-----------------------------------------------------------------------------
// Purpose: Load all available maps into the grid
//-----------------------------------------------------------------------------
void CCreateMultiplayerGameDialog::LoadMapList()
{
	// GAME search path covers the mod dir + mounted custom dirs
	LoadMaps( "GAME" );

	// fall back to MOD in case the mod dir doesn't resolve through GAME
	if ( m_MapNames.Count() == 0 )
	{
		LoadMaps( "MOD" );
	}
}

//-----------------------------------------------------------------------------
// Purpose: Called when a map card is clicked
//-----------------------------------------------------------------------------
void CCreateMultiplayerGameDialog::OnMapSelected( const char *pszMapName )
{
	Q_strncpy( m_szSelectedMap, pszMapName, sizeof( m_szSelectedMap ) );

	// highlight the matching card
	RefreshSelection();

	// reflect the selection on the label
	if ( m_pSelectedMapLabel )
	{
		char szLabel[512];
		Q_snprintf( szLabel, sizeof( szLabel ), "Map: %s", m_szSelectedMap );
		m_pSelectedMapLabel->SetText( szLabel );
	}
}

//-----------------------------------------------------------------------------
// Purpose: highlight the selected map card
//-----------------------------------------------------------------------------
void CCreateMultiplayerGameDialog::RefreshSelection()
{
	if ( !m_pMapList )
		return;

	// OnMapSelected() calls back into RefreshSelection(); the default-selection
	// block below calls OnMapSelected(). Guard against re-entering here (e.g. an
	// empty default map name would otherwise loop forever).
	if ( m_bInRefreshSelection )
		return;
	m_bInRefreshSelection = true;

	for ( int nItemID = m_pMapList->FirstItem(); nItemID != m_pMapList->InvalidItemID(); nItemID = m_pMapList->NextItem( nItemID ) )
	{
		CMapCardPanel *pCard = dynamic_cast< CMapCardPanel * >( m_pMapList->GetItemPanel( nItemID ) );
		if ( pCard )
		{
			bool bSelected = ( m_szSelectedMap[0] != 0 && !Q_stricmp( pCard->GetMapName(), m_szSelectedMap ) );
			pCard->SetSelected( bSelected );
		}
	}

	// default selection
	if ( m_szSelectedMap[0] == 0 )
	{
		const char *startMap = m_pSavedData ? m_pSavedData->GetString("map", "") : "";
		if ( startMap[0] )
		{
			OnMapSelected( startMap );
		}
		else if ( m_MapNames.Count() )
		{
			OnMapSelected( m_MapNames[0] );
		}
	}

	m_bInRefreshSelection = false;
}

//-----------------------------------------------------------------------------
// Purpose: Returns true if randomly selected map
//-----------------------------------------------------------------------------
bool CCreateMultiplayerGameDialog::IsRandomMapSelected()
{
	return false;
}

//-----------------------------------------------------------------------------
// Purpose: Returns currently selected map
//-----------------------------------------------------------------------------
const char *CCreateMultiplayerGameDialog::GetMapName()
{
	if ( m_szSelectedMap[0] == 0 && m_MapNames.Count() )
		return m_MapNames[0];
	return m_szSelectedMap;
}

//-----------------------------------------------------------------------------
// Purpose: getters for server settings
//-----------------------------------------------------------------------------
const char *CCreateMultiplayerGameDialog::GetHostName()
{
	static char szValue[256];
	if ( m_pHostName )
		m_pHostName->GetText( szValue, sizeof( szValue ) );
	else
		szValue[0] = 0;
	return szValue;
}

const char *CCreateMultiplayerGameDialog::GetPassword()
{
	static char szValue[256];
	if ( m_pPassword )
		m_pPassword->GetText( szValue, sizeof( szValue ) );
	else
		szValue[0] = 0;
	return szValue;
}

int CCreateMultiplayerGameDialog::GetMaxPlayers()
{
	if ( m_pMaxPlayers )
	{
		KeyValues *kv = m_pMaxPlayers->GetActiveItemUserData();
		if ( kv )
			return kv->GetInt( "val", MAX_PLAYERS_DEFAULT );
	}
	return MAX_PLAYERS_DEFAULT;
}

//-----------------------------------------------------------------------------
// Purpose: current mp_falldamage value.  The cvar is registered by the server
// DLL, which is not loaded while the main menu is up, so this normally falls
// back to the sandbox default (off).
//-----------------------------------------------------------------------------
bool CCreateMultiplayerGameDialog::GetFallDamage()
{
	ConVarRef falldamage( "mp_falldamage", true );
	if ( falldamage.IsValid() )
		return ( falldamage.GetInt() != 0 );

	// Not visible from the main menu (the server DLL is not loaded), so fall
	// back to the player's last choice; 0 = the sandbox default.
	if ( m_pSavedData )
		return ( m_pSavedData->GetInt( "mp_falldamage", 0 ) != 0 );

	return false;
}

//-----------------------------------------------------------------------------
// Purpose: HL2SB sidebar getters (2026-09-28).  ServerConfig.vdf - the dialog's
// own persisted state - wins; the live convar is only a fallback for a key the
// file does not carry yet.
//
// The first version was the other way round (convar first) and that is the
// reported "ammo field is 0 again on the next open": gmod_maxammo/gmod_suit
// live in the game DLLs, and once a game has run this process keeps client.dll
// loaded, so ConVarRef succeeds with the REGISTERED default "0" and buries the
// saved 9999 on every later open (the HEV checkbox had the same trap).  The
// sbox_* pair comes from sandbox's init.lua and usually cannot exist at the
// menu, but they get the same order for uniformity.
//-----------------------------------------------------------------------------
int CCreateMultiplayerGameDialog::GetMaxAmmo()
{
	if ( m_pSavedData && m_pSavedData->FindKey( "gmod_maxammo" ) )
		return m_pSavedData->GetInt( "gmod_maxammo", 9999 );

	ConVarRef maxammo( "gmod_maxammo", true );
	if ( maxammo.IsValid() && maxammo.GetInt() > 0 )
		return maxammo.GetInt();

	// GMod's start-menu field default (its HTML menu's own default table).
	// The registered convar default is "0", which means "keep the HL2 per-type
	// caps" - not what the field should display.
	return 9999;
}

bool CCreateMultiplayerGameDialog::GetLoadout()
{
	if ( m_pSavedData && m_pSavedData->FindKey( "sbox_weapons" ) )
		return ( m_pSavedData->GetInt( "sbox_weapons", 1 ) != 0 );

	ConVarRef loadout( "sbox_weapons", true );
	if ( loadout.IsValid() )
		return ( loadout.GetInt() != 0 );

	return true;
}

bool CCreateMultiplayerGameDialog::GetGodMode()
{
	if ( m_pSavedData && m_pSavedData->FindKey( "sbox_godmode" ) )
		return ( m_pSavedData->GetInt( "sbox_godmode", 0 ) != 0 );

	ConVarRef godmode( "sbox_godmode", true );
	if ( godmode.IsValid() )
		return ( godmode.GetInt() != 0 );

	return false;
}

bool CCreateMultiplayerGameDialog::GetSuit()
{
	if ( m_pSavedData && m_pSavedData->FindKey( "gmod_suit" ) )
		return ( m_pSavedData->GetInt( "gmod_suit", 0 ) != 0 );

	ConVarRef suit( "gmod_suit", true );
	if ( suit.IsValid() )
		return ( suit.GetInt() != 0 );

	return false;
}

//-----------------------------------------------------------------------------
// Purpose: writes cfg/listenserver.cfg, which the engine execs at
// SV_ActivateServer.  That is the first moment a cvar owned by the server DLL
// (mp_falldamage) exists, so it cannot be put on the command line before the
// "map" command.  Anything the player already has in the file is preserved;
// only the lines this function owns are replaced.
//-----------------------------------------------------------------------------
static const char *kListenServerHeader[] =
{
	"// Generated by the HL2SB create-server dialog.",
	"// The engine execs this at SV_ActivateServer.",
};

// HL2SB: every line this dialog owns.  Carried-over copies from previous runs
// are dropped by prefix so each option is written exactly once per start.
static const char *kListenServerOwnedLines[] =
{
	"mp_falldamage",
	"gmod_maxammo",
	"gmod_suit",
};

void CCreateMultiplayerGameDialog::WriteListenServerConfig( bool bFallDamage, int nMaxAmmo, bool bSuit )
{
	CUtlBuffer existing( 0, 0, CUtlBuffer::TEXT_BUFFER );
	g_pFullFileSystem->ReadFile( "cfg/listenserver.cfg", "MOD", existing );

	CUtlBuffer out( 0, 0, CUtlBuffer::TEXT_BUFFER );
	for ( int i = 0; i < ARRAYSIZE( kListenServerHeader ); i++ )
	{
		out.PutString( kListenServerHeader[i] );
		out.PutChar( '\n' );
	}

	char szLine[64];
	Q_snprintf( szLine, sizeof( szLine ), "mp_falldamage %d\n", bFallDamage ? 1 : 0 );
	out.PutString( szLine );
	Q_snprintf( szLine, sizeof( szLine ), "gmod_maxammo %d\n", nMaxAmmo );
	out.PutString( szLine );
	Q_snprintf( szLine, sizeof( szLine ), "gmod_suit %d\n", bSuit ? 1 : 0 );
	out.PutString( szLine );

	if ( existing.TellMaxPut() > 0 )
	{
		const char *pCur = (const char *)existing.Base();
		const char *pEnd = pCur + existing.TellMaxPut();

		while ( pCur < pEnd )
		{
			const char *pLine = pCur;
			while ( pCur < pEnd && *pCur != '\n' )
				++pCur;

			int nLen = (int)( pCur - pLine );
			if ( pCur < pEnd )
				++pCur;	// consume the newline

			// Skip leading whitespace before classifying the line.
			const char *pTrim = pLine;
			const char *pTrimEnd = pLine + nLen;
			while ( pTrim < pTrimEnd && ( *pTrim == ' ' || *pTrim == '\t' ) )
				++pTrim;
			while ( pTrimEnd > pTrim && ( pTrimEnd[-1] == '\r' || pTrimEnd[-1] == ' ' ) )
				--pTrimEnd;

			int nTrimLen = (int)( pTrimEnd - pTrim );

			// Ours are already written above; never carry a previous copy over,
			// otherwise every run would stack another header block.
			bool bOurs = false;
			for ( int i = 0; i < ARRAYSIZE( kListenServerOwnedLines ); i++ )
			{
				size_t nPrefix = Q_strlen( kListenServerOwnedLines[i] );
				if ( (size_t)nTrimLen >= nPrefix &&
					 !Q_strnicmp( pTrim, kListenServerOwnedLines[i], nPrefix ) )
				{
					bOurs = true;
					break;
				}
			}
			if ( bOurs )
				continue;

			for ( int i = 0; i < ARRAYSIZE( kListenServerHeader ); i++ )
			{
				if ( (int)Q_strlen( kListenServerHeader[i] ) == nTrimLen &&
					 !Q_strnicmp( pTrim, kListenServerHeader[i], nTrimLen ) )
				{
					bOurs = true;
					break;
				}
			}
			if ( bOurs )
				continue;

			out.Put( pLine, nLen );
			out.PutChar( '\n' );
		}
	}

	g_pFullFileSystem->WriteFile( "cfg/listenserver.cfg", "MOD", out );
}

//-----------------------------------------------------------------------------
// Purpose: Save the config to disk
//-----------------------------------------------------------------------------
void CCreateMultiplayerGameDialog::SaveConfig()
{
	if ( m_pSavedData )
	{
		m_pSavedData->SetString( "map", GetMapName() );
		m_pSavedData->SetInt( "mp_falldamage",
			( m_pFallDamageCheck && m_pFallDamageCheck->IsSelected() ) ? 1 : 0 );

		// HL2SB sidebar (2026-09-28): persisted for two consumers --
		// reads on the next dialog open, and sandbox/init.lua's
		// HL2SB_ApplyStartOptions() which lands the sbox_* pair in-game.
		int nMaxAmmo = 9999;
		if ( m_pMaxAmmo )
		{
			char szAmmo[16];
			szAmmo[0] = 0;
			m_pMaxAmmo->GetText( szAmmo, sizeof( szAmmo ) );
			// An emptied-out box would Q_atoi to 0 and poison the saved state
			// with "HL2 caps"; keep the GMod default instead.
			nMaxAmmo = ( szAmmo[0] != 0 ) ? clamp( Q_atoi( szAmmo ), 0, 999999 ) : 9999;
		}
		m_pSavedData->SetInt( "gmod_maxammo", nMaxAmmo );
		m_pSavedData->SetInt( "sbox_weapons",
			( m_pLoadoutCheck && m_pLoadoutCheck->IsSelected() ) ? 1 : 0 );
		m_pSavedData->SetInt( "sbox_godmode",
			( m_pGodModeCheck && m_pGodModeCheck->IsSelected() ) ? 1 : 0 );
		m_pSavedData->SetInt( "gmod_suit",
			( m_pSuitCheck && m_pSuitCheck->IsSelected() ) ? 1 : 0 );

		m_pSavedData->SaveToFile( g_pFullFileSystem, "ServerConfig.vdf", "GAME" );
	}
}

//-----------------------------------------------------------------------------
// Purpose: runs the server when the Start button is pressed
//-----------------------------------------------------------------------------
void CCreateMultiplayerGameDialog::CreateGame()
{
	// "gamemode" carries FCVAR_REPLICATED, so the RevertFlaggedConVars() calls
	// below would silently reset it to its default (sandbox). The gamemode is
	// whatever the player has set (console / config); the map category must NOT
	// override it, otherwise picking a map from any category silently switches
	// the gamemode. Remember the player's choice first, then hand it back to the
	// server with the map command.
	ConVarRef gamemodeVar( "gamemode", true );
	char szGamemode[32];
	Q_strncpy( szGamemode, gamemodeVar.IsValid() ? gamemodeVar.GetString() : "sandbox", sizeof( szGamemode ) );

	// reset server enforced cvars
	g_pCVar->RevertFlaggedConVars( FCVAR_REPLICATED );
	g_pCVar->RevertFlaggedConVars( FCVAR_CHEAT );

	char szMapName[64], szHostName[64], szPassword[64];
	Q_strncpy( szMapName, GetMapName(), sizeof( szMapName ) );
	Q_strncpy( szHostName, GetHostName(), sizeof( szHostName ) );
	Q_strncpy( szPassword, GetPassword(), sizeof( szPassword ) );

	SaveConfig();

	// mp_falldamage belongs to the server DLL, which only exists once the map
	// is running, so it is applied through cfg/listenserver.cfg rather than on
	// the command line below.  Same story for gmod_maxammo / gmod_suit (game
	// DLLs, not loaded at the menu) -- the sbox_* pair is NOT written here,
	// sandbox/init.lua applies it from ServerConfig.vdf after the convars
	// exist.
	int nMaxAmmo = 9999;
	if ( m_pMaxAmmo )
	{
		char szAmmo[16];
		szAmmo[0] = 0;
		m_pMaxAmmo->GetText( szAmmo, sizeof( szAmmo ) );
		// Same empty-box guard as SaveConfig: never exec "gmod_maxammo 0" for a
		// field the player wiped.
		nMaxAmmo = ( szAmmo[0] != 0 ) ? clamp( Q_atoi( szAmmo ), 0, 999999 ) : 9999;
	}
	WriteListenServerConfig( m_pFallDamageCheck && m_pFallDamageCheck->IsSelected(),
		nMaxAmmo, m_pSuitCheck && m_pSuitCheck->IsSelected() );

	char szMapCommand[1024];
	Q_snprintf(szMapCommand, sizeof( szMapCommand ), "disconnect\nwait\nwait\nsv_lan 1\nsetmaster enable\ngamemode \"%s\"\nmaxplayers %i\nsv_password \"%s\"\nhostname \"%s\"\nprogress_enable\nmap %s\n",
		szGamemode,
		GetMaxPlayers(),
		szPassword,
		szHostName,
		szMapName
	);

	engine->ClientCmd_Unrestricted(szMapCommand);
}

//-----------------------------------------------------------------------------
// Purpose: command handler
//-----------------------------------------------------------------------------
void CCreateMultiplayerGameDialog::OnCommand( const char *command )
{
	if ( !Q_stricmp( command, "CreateGame" ) )
	{
		CreateGame();
		return;
	}
	else if ( !Q_stricmp( command, "Close" ) )
	{
		Close();
		return;
	}
	else if ( !Q_strnicmp( command, "MapCat ", 7 ) )
	{
		int iCat = atoi( command + 7 );
		if ( iCat >= 0 && iCat < MAPCAT_COUNT )
		{
			m_iSelectedCategory = iCat;
			// refresh the category list highlight + rebuild the map grid
			BuildGameModeList();
			BuildMapGrid();
		}
		return;
	}

	BaseClass::OnCommand( command );
}

void CCreateMultiplayerGameDialog::OnKeyCodePressed( vgui::KeyCode code )
{
	ButtonCode_t nButtonCode = GetBaseButtonCode( code );

	if ( nButtonCode == KEY_XBUTTON_B || nButtonCode == STEAMCONTROLLER_B )
	{
		Close();
		return;
	}
	else if ( nButtonCode == KEY_ENTER || nButtonCode == KEY_XBUTTON_A || nButtonCode == STEAMCONTROLLER_A )
	{
		CreateGame();
		return;
	}

	BaseClass::OnKeyCodePressed( code );
}
