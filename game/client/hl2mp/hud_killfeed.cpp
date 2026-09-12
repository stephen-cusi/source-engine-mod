//========= Copyright Valve Corporation, All rights reserved. ============//
//
// Purpose: GMod-style kill feed / death notice HUD for HL2SB.
//
//			Anchored to the TOP-RIGHT of the screen. Each entry is right-
//			justified as:
//
//			   <KillerName>  <iconDeath>  <VictimName>
//
//			Colour rules:
//			  - killer name  : gold when the killer is a player, red otherwise
//			                   (NPC or world / environment)
//			  - victim name  : gold when the victim is a player, red when it is
//			                   an NPC / world entity
//			  - icon         : red for a suicide / world / NPC kill, else the
//			                   killer's team colour
//
//			Rows stack downward from a position derived from the res block
//			(xpos = right margin, ypos = top margin) plus a small screen
//			percentage, so they don't sit flush against the top edge. The icon
//			is vertically centred against the text on the same row, and each row
//			fades out over the last ~1.2s of its lifetime (an explicit alpha
//			fade, not an abrupt pop-out).
//
//			Events:
//			  - player_death    : killer vs victim (players), or suicide
//			  - entity_killed   : an NPC (or entity) was killed. The killer may
//			                     be a player, another NPC, or the world, so
//			                     NPC-vs-NPC scraps also show up in the feed.
//
// $NoKeywords: $
//=============================================================================//
#include "cbase.h"
#include "hudelement.h"
#include "hud_macros.h"
#include "c_playerresource.h"
#include "clientmode_hl2mpnormal.h"
#include <vgui_controls/Controls.h>
#include <vgui_controls/Panel.h>
#include <vgui/ISurface.h>
#include <vgui/ILocalize.h>
#include <KeyValues.h>
#include "c_baseplayer.h"
#include "c_team.h"
#include "filesystem.h"

#ifdef LUA_SDK
#include "luamanager.h"
#endif

// memdbgon must be the last include file in a .cpp file!!!
#include "tier0/memdbgon.h"

static ConVar hud_deathnotice_time( "hud_deathnotice_time", "6", FCVAR_ARCHIVE, "How long each death notice stays on screen (seconds)." );
static ConVar cl_drawdeathnotice( "cl_drawdeathnotice", "1", FCVAR_ARCHIVE, "Toggle the death notice / kill feed HUD on and off." );
static ConVar hud_killfeed_iconscale( "hud_killfeed_iconscale", "0.9", FCVAR_ARCHIVE, "Kill feed icon height as a fraction of the text height." );
static ConVar hud_killfeed_max( "hud_killfeed_max", "4", FCVAR_ARCHIVE,
	"Maximum number of kill feed lines shown at once. 0 = unlimited (GMod behaviour), -1 = use the panel's res MaxDeathNotices." );
static ConVar cl_killfeed_lua( "cl_killfeed_lua", "1", FCVAR_ARCHIVE,
	"Let the Lua script own the kill feed drawing (events are forwarded to the AddDeathNotice hook)." );

// Name colours: players are always gold, NPCs and world/environment are always
// red, regardless of team.
static const Color KILLFEED_PLAYER_NAME_COLOUR( 255, 210, 60, 255 );	// gold
static const Color KILLFEED_NPC_NAME_COLOUR( 220, 40, 40, 255 );		// red

//-----------------------------------------------------------------------------
// Player (or entity) entries in a death notice.
//-----------------------------------------------------------------------------
struct KillFeedPlayer
{
	char		szName[MAX_PLAYER_NAME_LENGTH];
	int			iEntIndex;	// engine player index, or 0 if not a real player
};

//-----------------------------------------------------------------------------
// Contents of each entry in our list of death notices.
//-----------------------------------------------------------------------------
struct KillFeedItem
{
	KillFeedPlayer	Killer;			// may be empty (suicide / world)
	KillFeedPlayer	Victim;
	CHudTexture		*iconDeath;		// death_<weapon> glyph, or the skull
	int				iSuicide;		// 1 = no killer (world / self)
	bool			bKillerIsPlayer;
	bool			bVictimIsNPC;	// victim is a non-player entity
	bool			bUseSkull;		// draw the textured skull (suicide / world /
									// NPC-vs-NPC): no weapon glyph to show
	float			flAddTime;		// server time when this entry was added
	float			flDisplayTime;	// server time when it should be removed
};

//-----------------------------------------------------------------------------
// Turn an entity classname into a readable display name.
//-----------------------------------------------------------------------------
static const char *KillFeed_DisplayName( const char *szClass, char *szOut, int nOutSize )
{
	Q_strncpy( szOut, szClass, nOutSize );

	if ( !Q_strnicmp( szOut, "class ", 6 ) )
		Q_strncpy( szOut, szClass + 6, nOutSize );

	const char *strip[] = { "npc_", "monster_", "weapon_", "item_", "ammo_", "entity_", "func_", "prop_" };
	for ( int i = 0; i < ARRAYSIZE(strip); i++ )
	{
		int n = Q_strlen( strip[i] );
		if ( !Q_strnicmp( szOut, strip[i], n ) )
		{
			memmove( szOut, szOut + n, nOutSize - n );
			break;
		}
	}

	if ( szOut[0] >= 'a' && szOut[0] <= 'z' )
		szOut[0] -= ( 'a' - 'A' );

	return szOut;
}

//-----------------------------------------------------------------------------
// Kill feed HUD element.
//-----------------------------------------------------------------------------
class CHudKillFeed : public CHudElement, public vgui::Panel
{
	DECLARE_CLASS_SIMPLE( CHudKillFeed, vgui::Panel );
public:
	CHudKillFeed( const char *pElementName );

	void Init( void );
	void VidInit( void );
	virtual bool ShouldDraw( void );
	virtual void Paint( void );
	virtual void ApplySchemeSettings( vgui::IScheme *scheme );

	virtual void FireGameEvent( IGameEvent *event );

private:
	void RetireExpiredDeathNotices( void );
	Color GetKillerColour( const KillFeedItem &e );
	Color GetVictimColour( const KillFeedItem &e );
	Color GetIconColour( const KillFeedItem &e );

	// Res-driven layout/size fields (all read from the "HudKillFeed" block in
	// HudLayout.res, so everything can be tuned without recompiling).
	CPanelAnimationVarAliasType( float, m_flLineHeight, "LineHeight", "16", "proportional_float" );
	CPanelAnimationVar( float, m_flMaxDeathNotices, "MaxDeathNotices", "4" );
	CPanelAnimationVar( bool, m_bRightJustify, "RightJustify", "1" );
	CPanelAnimationVar( vgui::HFont, m_hTextFont, "TextFont", "HudNumbersTimer" );

	// Position/size.
	CPanelAnimationVar( float, m_flXPos, "xpos", "0" );
	CPanelAnimationVar( float, m_flYPos, "ypos", "0" );
	CPanelAnimationVar( float, m_flIconGap, "IconGap", "12" );
	CPanelAnimationVar( float, m_flKillerGap, "KillerGap", "12" );
	CPanelAnimationVar( float, m_flVictimGap, "VictimGap", "12" );
	CPanelAnimationVar( float, m_flLineGap, "LineGap", "8" );
	CPanelAnimationVar( float, m_flTopFrac, "TopFrac", "0.03" );
	CPanelAnimationVar( float, m_flFadeInTime, "FadeInTime", "0.3" );
	CPanelAnimationVar( float, m_flFadeOutTime, "FadeOutTime", "1.2" );

	// Texture for skull symbol (suicide / world / unknown kill).
	CHudTexture		*m_iconD_skull;
	// Loaded texture id for the GMod killicon material (hud/killicons/default)
	// so the kill icon can be drawn as a texture and scaled/centred exactly,
	// avoiding the vertical-ink offset of font glyphs.
	int				m_iKillIconTex;

	CUtlVector<KillFeedItem> m_DeathNotices;
};

using namespace vgui;

DECLARE_HUDELEMENT( CHudKillFeed );

//-----------------------------------------------------------------------------
// Purpose: Constructor
//-----------------------------------------------------------------------------
CHudKillFeed::CHudKillFeed( const char *pElementName ) :
	CHudElement( pElementName ), BaseClass( NULL, "HudKillFeed" )
{
	vgui::Panel *pParent = g_pClientMode->GetViewport();
	SetParent( pParent );

	m_iconD_skull = NULL;
	m_iKillIconTex = -1;

	// Never hide the death notice when the local player dies (the player must
	// see who killed them), so don't register any HIDEHUD_* bits.
	SetHiddenBits( 0 );
}

//-----------------------------------------------------------------------------
// Purpose:
//-----------------------------------------------------------------------------
void CHudKillFeed::ApplySchemeSettings( IScheme *scheme )
{
	BaseClass::ApplySchemeSettings( scheme );
	SetPaintBackgroundEnabled( false );

	// Fix the font here so the width/height math never runs against an
	// invalid font (the res-driven "HudNumbersTimer" may not exist in HL2MP).
	m_hTextFont = scheme->GetFont( "Default", true );

	// Fill the screen so the Panel doesn't clip the right-justified block.
	SetBounds( 0, 0, ScreenWidth(), ScreenHeight() );
}

//-----------------------------------------------------------------------------
// Purpose:
//-----------------------------------------------------------------------------
void CHudKillFeed::Init( void )
{
	ListenForGameEvent( "player_death" );
	ListenForGameEvent( "entity_killed" );
	ListenForGameEvent( "item_pickup" );
}

//-----------------------------------------------------------------------------
// Purpose:
//-----------------------------------------------------------------------------
void CHudKillFeed::VidInit( void )
{
	m_iconD_skull = gHUD.GetIcon( "d_skull" );

	// Load the GMod killicon material as a texture so we can draw it as a
	// texture and scale/centre it exactly (font glyphs carry a vertical ink
	// offset we can't compensate without exact bounds).
	//
	// That material ships with GMod content, not with the engine.  When it is
	// absent DrawSetTextureFile() - which returns void - silently binds the
	// pink ERROR material, so every world/suicide kill (e.g. trigger_hurt)
	// showed a checkerboard instead of the skull.  Probe for the file first and
	// leave m_iKillIconTex at -1 when it is missing; Paint() then falls back to
	// the d_skull font glyph.
	if ( m_iKillIconTex == -1 )
	{
		if ( filesystem->FileExists( "materials/hud/killicons/default.vmt", "GAME" ) )
		{
			m_iKillIconTex = surface()->CreateNewTextureID();
			// Material path is relative to "materials/" (the material system prepends it).
			surface()->DrawSetTextureFile( m_iKillIconTex, "hud/killicons/default", true, false );
		}
		else
		{
			Msg( "[HL2SB] materials/hud/killicons/default.vmt not found - "
				 "kill feed falls back to the d_skull glyph\n" );
		}
	}

	m_DeathNotices.Purge();
	SetPaintBackgroundEnabled( false );

	SetBounds( 0, 0, ScreenWidth(), ScreenHeight() );
}

//-----------------------------------------------------------------------------
// Purpose: Draw if we've got at least one death notice in the queue.
//-----------------------------------------------------------------------------
bool CHudKillFeed::ShouldDraw( void )
{
	if ( !cl_drawdeathnotice.GetBool() )
		return false;

	return ( CHudElement::ShouldDraw() && ( m_DeathNotices.Count() ) );
}

//-----------------------------------------------------------------------------
// Purpose: Colour for the killer name: gold for players, red for NPCs/world.
//-----------------------------------------------------------------------------
Color CHudKillFeed::GetKillerColour( const KillFeedItem &e )
{
	if ( e.bKillerIsPlayer )
		return KILLFEED_PLAYER_NAME_COLOUR;

	// NPC killer or world/environmental kill.
	return KILLFEED_NPC_NAME_COLOUR;
}

//-----------------------------------------------------------------------------
// Purpose: Colour for the victim name: gold for players, red for NPCs/world.
//-----------------------------------------------------------------------------
Color CHudKillFeed::GetVictimColour( const KillFeedItem &e )
{
	if ( e.bVictimIsNPC )
		return KILLFEED_NPC_NAME_COLOUR;

	return KILLFEED_PLAYER_NAME_COLOUR;
}

//-----------------------------------------------------------------------------
// Purpose: Colour for the weapon / skull icon.
//-----------------------------------------------------------------------------
Color CHudKillFeed::GetIconColour( const KillFeedItem &e )
{
	if ( e.iSuicide || e.bVictimIsNPC || !e.bKillerIsPlayer )
		return Color( 255, 60, 20, 255 );

	if ( e.Killer.iEntIndex > 0 )
	{
		int iTeam = g_PR ? g_PR->GetTeam( e.Killer.iEntIndex ) : 0;
		if ( iTeam > 0 )
			return GameResources()->GetTeamColor( iTeam );
	}

	return Color( 255, 60, 20, 255 );
}

//-----------------------------------------------------------------------------
// Purpose:
//-----------------------------------------------------------------------------
void CHudKillFeed::Paint()
{
	if ( m_DeathNotices.Count() == 0 )
		return;

	if ( !m_hTextFont )
		m_hTextFont = vgui::scheme()->GetIScheme( GetScheme() )->GetFont( "Default", true );

	surface()->DrawSetTextFont( m_hTextFont );
	int iTextTall = surface()->GetFontTall( m_hTextFont );

	// Anchor near the top-right, but not flush against the top edge.
	int yStart = (int)( (float)ScreenHeight() * m_flTopFrac ) + (int)m_flYPos;
	int nScreenW = ScreenWidth();
	int xRight = ( m_bRightJustify ) ? ( nScreenW - (int)m_flXPos ) : (int)m_flXPos;

	int iCount = m_DeathNotices.Count();

	// Icon size driven by the cvar, scaled against the current text height.
	float flIconScale = hud_killfeed_iconscale.GetFloat();
	if ( flIconScale <= 0.0f )
		flIconScale = 0.9f;

	for ( int i = 0; i < iCount; i++ )
	{
		KillFeedItem &e = m_DeathNotices[i];
		CHudTexture *icon = e.iconDeath;
		if ( !icon )
			continue;

		wchar_t victim[ 256 ];
		wchar_t killer[ 256 ];
		g_pVGuiLocalize->ConvertANSIToUnicode( e.Victim.szName, victim, sizeof( victim ) );
		g_pVGuiLocalize->ConvertANSIToUnicode( e.Killer.szName, killer, sizeof( killer ) );

		// Fade in on add, then fade out over the last FadeOutTime seconds.
		float flFadeIn = MAX( m_flFadeInTime, 0.0f );
		float flFadeOut = MAX( m_flFadeOutTime, 0.01f );
		float flAge = gpGlobals->curtime - e.flAddTime;
		float flRemain = e.flDisplayTime - gpGlobals->curtime;
		float flAlpha = 1.0f;
		if ( flFadeIn > 0.0f && flAge < flFadeIn )
			flAlpha = flAge / flFadeIn;
		else if ( flRemain < flFadeOut )
			flAlpha = clamp( flRemain / flFadeOut, 0.0f, 1.0f );
		flAlpha = clamp( flAlpha, 0.0f, 1.0f );
		int nAlpha = (int)( 255.0f * flAlpha );

		int iVictimW = UTIL_ComputeStringWidth( m_hTextFont, victim );
		int iKillerW = UTIL_ComputeStringWidth( m_hTextFont, killer );
		bool bShowKiller = ( !e.iSuicide && e.Killer.szName[0] );

		// Size the icon from the object that is actually drawn. A weapon glyph
		// (or the font skull) is a font character: it CANNOT be scaled, so we
		// must use its real character width/height and draw it on the same
		// baseline as the names — otherwise it overflows its layout box and
		// overlaps the victim name. A skull texture we draw ourselves can be
		// sized freely and centred in the text height.
		bool bDrawTexSkull = e.bUseSkull && ( m_iKillIconTex != -1 );
		int iconWide = 0;
		int iconTall = 0;
		if ( bDrawTexSkull )
		{
			iconTall = (int)( (float)iTextTall * flIconScale );
			iconWide = (int)( (float)iconTall * 1.0f );
		}
		else if ( icon->bRenderUsingFont )
		{
			iconWide = surface()->GetCharacterWidth( icon->hFont, icon->cCharacterInFont );
			iconTall = surface()->GetFontTall( icon->hFont );
		}
		else
		{
			iconWide = icon->Width();
			iconTall = icon->Height();
		}
		if ( iconWide <= 0 ) iconWide = iTextTall;
		if ( iconTall <= 0 ) iconTall = iTextTall;

		// Row vertical metrics. Use a CONSISTENT per-row stride (text height +
		// line gap) so consecutive rows never overlap even when a row's icon is
		// fatter/taller than another's — the original HL2MP deathnotice does the
		// same (yStart + m_flLineHeight * i). Within the row, a font glyph sits on
		// the name baseline (iTextY); only the freely-scaled skull texture gets
		// vertically centred against the text height.
		int iRowStride = iTextTall + (int)m_flLineGap;
		int iRowY = yStart + ( i * iRowStride );
		int iTextY = iRowY;
		int iIconY = bDrawTexSkull ? ( iTextY + ( iTextTall - iconTall ) / 2 ) : iTextY;

		// Layout, right-justified: victim name rightmost, icon to its left,
		// killer name further left, each separated by its own res-defined gap.
		int iIconGap = (int)m_flIconGap;
		int iVictimX = xRight - iVictimW;
		int iIconX = iVictimX - iIconGap - iconWide;
		int iKillerX = iIconX - (int)m_flKillerGap - ( ( bShowKiller ) ? iKillerW : 0 );

		// Killer name (leftmost).
		if ( bShowKiller )
		{
			Color c = GetKillerColour( e );
			c[3] = nAlpha;
			surface()->DrawSetTextColor( c );
			surface()->DrawSetTextPos( iKillerX, iTextY );
			surface()->DrawSetTextFont( m_hTextFont );
			surface()->DrawUnicodeString( killer );
		}

		// Icon (middle). A weapon glyph is a font character and renders at its
		// own size via DrawSelf (which ignores the w/h we pass here). Only the
		// skull texture fills the box we set above.
		Color iconColor = GetIconColour( e );
		iconColor[3] = nAlpha;
		if ( bDrawTexSkull )
		{
			surface()->DrawSetTexture( m_iKillIconTex );
			surface()->DrawSetColor( iconColor );
			surface()->DrawTexturedRect( iIconX, iIconY, iIconX + iconWide, iIconY + iconTall );
		}
		else if ( icon )
		{
			icon->DrawSelf( iIconX, iIconY, iconWide, iconTall, iconColor );
		}
		// Reset text font so the victim name below isn't affected.
		surface()->DrawSetTextFont( m_hTextFont );

		// Victim name (rightmost).
		Color c = GetVictimColour( e );
		c[3] = nAlpha;
		surface()->DrawSetTextColor( c );
		surface()->DrawSetTextPos( iVictimX, iTextY );
		surface()->DrawSetTextFont( m_hTextFont );
		surface()->DrawUnicodeString( victim );
	}

	// Now retire any death notices that have expired.
	RetireExpiredDeathNotices();
}

//-----------------------------------------------------------------------------
// Purpose: This removes any death notices that have expired.
//-----------------------------------------------------------------------------
void CHudKillFeed::RetireExpiredDeathNotices( void )
{
	int iSize = m_DeathNotices.Size();
	for ( int i = iSize - 1; i >= 0; i-- )
	{
		if ( m_DeathNotices[i].flDisplayTime < gpGlobals->curtime )
		{
			m_DeathNotices.Remove(i);
		}
	}
}

//-----------------------------------------------------------------------------
// Purpose: Server's told us that someone's died.
//-----------------------------------------------------------------------------
void CHudKillFeed::FireGameEvent( IGameEvent * event )
{
	if ( !g_PR )
		return;

	if ( hud_deathnotice_time.GetFloat() == 0 )
		return;

	const char *pszName = event->GetName();

	// HL2SB: weapon / item / ammo pickup -> forward to Lua.  The engine passes
	// the raw (item, amount); the Lua script classifies and colours it.
	if ( !Q_stricmp( pszName, "item_pickup" ) )
	{
#ifdef LUA_SDK
		if ( cl_killfeed_lua.GetBool() )
		{
			int iUserID = event->GetInt( "userid", 0 );
			const char *pszItem = event->GetString( "item", "" );
			int iAmount = event->GetInt( "amount", 0 );

			BEGIN_LUA_CALL_HOOK( "HUDItemPickedUp" );
				lua_pushinteger( L, iUserID );
				lua_pushstring( L, pszItem );
				lua_pushinteger( L, iAmount );
			END_LUA_CALL_HOOK( 3, 0 );
			return;
		}
#endif
		return;
	}

	KillFeedItem deathMsg;
	deathMsg.Killer.iEntIndex = 0;
	deathMsg.Victim.iEntIndex = 0;
	deathMsg.Killer.szName[0] = 0;
	deathMsg.Victim.szName[0] = 0;
	deathMsg.iconDeath = NULL;
	deathMsg.iSuicide = 0;
	deathMsg.bKillerIsPlayer = false;
	deathMsg.bVictimIsNPC = false;
	deathMsg.bUseSkull = false;

	if ( !Q_stricmp( pszName, "entity_killed" ) )
	{
		// A non-player entity (NPC / combat character) was killed. The victim is
		// always an entity here (player-vs-player goes through player_death).
		// The killer may be a player, another NPC, or the world/env.
		const char *pszVictimClass = event->GetString( "victimclass", "" );
		if ( !Q_strnicmp( pszVictimClass, "player", 6 ) )
			return;		// player-vs-player handled via player_death

		KillFeed_DisplayName( pszVictimClass, deathMsg.Victim.szName, sizeof( deathMsg.Victim.szName ) );
		deathMsg.Victim.iEntIndex = 0;
		deathMsg.bVictimIsNPC = true;

		int iAttackerUID = event->GetInt( "attacker_uid" );
		int killer = ( iAttackerUID != 0 ) ? engine->GetPlayerForUserID( iAttackerUID ) : 0;

		// A non-empty "attackername" means the killer is an entity (an NPC),
		// empty means the world / environmental kill.
		const char *pszAttackerName = event->GetString( "attackername", "" );

		if ( killer != 0 && killer != -1 )
		{
			// A real player did the killing.
			const char *killer_name = g_PR->GetPlayerName( killer );
			if ( !killer_name )
				killer_name = "";

			deathMsg.Killer.iEntIndex = killer;
			Q_strncpy( deathMsg.Killer.szName, killer_name, MAX_PLAYER_NAME_LENGTH );
			deathMsg.bKillerIsPlayer = true;
			deathMsg.iSuicide = 0;
		}
		else if ( pszAttackerName && pszAttackerName[0] )
		{
			// An NPC killed the entity (NPC-vs-NPC scrapping).
			char szKillerDisplay[MAX_PLAYER_NAME_LENGTH];
			KillFeed_DisplayName( pszAttackerName, szKillerDisplay, sizeof( szKillerDisplay ) );

			deathMsg.Killer.iEntIndex = 0;
			Q_strncpy( deathMsg.Killer.szName, szKillerDisplay, MAX_PLAYER_NAME_LENGTH );
			deathMsg.bKillerIsPlayer = false;
			deathMsg.iSuicide = 0;
		}
		else
		{
			// World / environmental kill (e.g. an NPC died to a hazard). Show
			// the victim with a skull and no killer name.
			deathMsg.Killer.iEntIndex = 0;
			deathMsg.Killer.szName[0] = 0;
			deathMsg.bKillerIsPlayer = false;
			deathMsg.iSuicide = 1;
		}

		const char *pszWeapon = event->GetString( "weapon", "" );
		deathMsg.iconDeath = gHUD.GetIcon( VarArgs( "death_%s", pszWeapon ) );
		if ( !deathMsg.iconDeath || deathMsg.iSuicide )
		{
			// No weapon death icon found (or it's a world/NPC kill); fall back to
			// the skull. Remember that so Paint draws the textured skull, not a
			// bogus weapon glyph.
			deathMsg.iconDeath = m_iconD_skull;
			deathMsg.bUseSkull = true;
		}
	}
	else if ( !Q_stricmp( pszName, "player_death" ) )
	{
		int killer = engine->GetPlayerForUserID( event->GetInt("attacker") );
		int victim = engine->GetPlayerForUserID( event->GetInt("userid") );
		int iKillerUID = event->GetInt( "attacker" );

		const char *victim_name = ( victim != 0 && victim != -1 ) ? g_PR->GetPlayerName( victim ) : "";
		if ( !victim_name )
			victim_name = "";

		const char *killedwith = event->GetString( "weapon" );
		// When the killer is not a real player (an NPC / world), the server
		// fills "attackername" with the killer's class name (e.g. npc_headcrab).
		const char *pszAttackerName = event->GetString( "attackername", "" );

		deathMsg.Victim.iEntIndex = victim;
		Q_strncpy( deathMsg.Victim.szName, victim_name, MAX_PLAYER_NAME_LENGTH );
		deathMsg.bVictimIsNPC = false;

		bool bKillerIsPlayer = ( killer != 0 && killer != -1 );

		if ( bKillerIsPlayer )
		{
			// Player killed the victim.
			const char *killer_name = g_PR->GetPlayerName( killer );
			if ( !killer_name )
				killer_name = "";

			deathMsg.Killer.iEntIndex = killer;
			Q_strncpy( deathMsg.Killer.szName, killer_name, MAX_PLAYER_NAME_LENGTH );
			deathMsg.bKillerIsPlayer = true;
			deathMsg.iSuicide = ( killer == victim );	// killed themselves
		}
		else if ( pszAttackerName && pszAttackerName[0] )
		{
			// A non-player entity (NPC / world) killed the victim.
			char szKillerDisplay[MAX_PLAYER_NAME_LENGTH];
			KillFeed_DisplayName( pszAttackerName, szKillerDisplay, sizeof( szKillerDisplay ) );

			deathMsg.Killer.iEntIndex = 0;
			Q_strncpy( deathMsg.Killer.szName, szKillerDisplay, MAX_PLAYER_NAME_LENGTH );
			deathMsg.bKillerIsPlayer = false;
			deathMsg.iSuicide = 0;	// an entity killed them, not self/world
			deathMsg.bVictimIsNPC = false;
		}
		else
		{
			// No attacker name: world / suicide.
			deathMsg.Killer.iEntIndex = 0;
			deathMsg.Killer.szName[0] = 0;
			deathMsg.bKillerIsPlayer = false;
			deathMsg.iSuicide = 1;
		}

		char fullkilledwith[128];
		if ( killedwith && *killedwith )
		{
			Q_snprintf( fullkilledwith, sizeof(fullkilledwith), "death_%s", killedwith );
		}
		else
		{
			fullkilledwith[0] = 0;
		}

		deathMsg.iconDeath = gHUD.GetIcon( fullkilledwith );
		if ( !deathMsg.iconDeath || deathMsg.iSuicide )
		{
			// No weapon glyph (or suicide); use the textured skull.
			deathMsg.iconDeath = m_iconD_skull;
			deathMsg.bUseSkull = true;
		}
	}
	else
	{
		return;
	}

	// Hand the parsed notice to Lua when a script owns the drawing.  The C++
	// panel stays alive (it is what receives the game events) but draws nothing.
#ifdef LUA_SDK
	if ( cl_killfeed_lua.GetBool() )
	{
		int iKillerTeam = 0;
		int iVictimTeam = 0;
		if ( g_PR )
		{
			if ( deathMsg.Killer.iEntIndex > 0 )
				iKillerTeam = g_PR->GetTeam( deathMsg.Killer.iEntIndex );
			if ( deathMsg.Victim.iEntIndex > 0 )
				iVictimTeam = g_PR->GetTeam( deathMsg.Victim.iEntIndex );
		}

		BEGIN_LUA_CALL_HOOK( "AddDeathNotice" );
			lua_pushstring( L, deathMsg.Killer.szName );
			lua_pushinteger( L, iKillerTeam );
			lua_pushstring( L, deathMsg.iconDeath && deathMsg.iconDeath->szShortName[0]
								? deathMsg.iconDeath->szShortName : "" );
			lua_pushstring( L, deathMsg.Victim.szName );
			lua_pushinteger( L, iVictimTeam );
			lua_pushboolean( L, deathMsg.iSuicide != 0 );
			lua_pushboolean( L, deathMsg.bVictimIsNPC );
			lua_pushboolean( L, deathMsg.bKillerIsPlayer );
		END_LUA_CALL_HOOK( 8, 0 );

		return;
	}
#endif

	// Trim the queue.  hud_killfeed_max wins when positive; 0 means unlimited,
	// which is what GMod does (its Deaths table is never capped - it just draws
	// every notice still inside hud_deathnotice_time); -1 falls back to the
	// panel's res-defined MaxDeathNotices.
	int iMaxNotices = hud_killfeed_max.GetInt();
	if ( iMaxNotices < 0 )
		iMaxNotices = (int)m_flMaxDeathNotices;

	if ( iMaxNotices > 0 && m_DeathNotices.Count() >= iMaxNotices )
	{
		// Remove the oldest one, which will always be the first.
		m_DeathNotices.Remove(0);
	}

	deathMsg.flAddTime = gpGlobals->curtime;
	float flTime = hud_deathnotice_time.GetFloat();
	if ( flTime <= 0.0f )
		flTime = 6.0f;		// safety net: never let an entry pop out instantly
	deathMsg.flDisplayTime = gpGlobals->curtime + flTime;

	m_DeathNotices.AddToTail( deathMsg );

	Msg( "%s killed %s\n", deathMsg.Killer.szName, deathMsg.Victim.szName );
}
