//========= Copyright Valve Corporation, All rights reserved. ============//
//
// Purpose: 
//
//=============================================================================//

#include "cbase.h"
#include "hud.h"
#include "hudelement.h"
#include "hud_macros.h"
#include "hud_numericdisplay.h"
#include "iclientmode.h"
#include "iclientvehicle.h"
#include <vgui_controls/AnimationController.h>
#include <vgui/ILocalize.h>
#include <vgui/ISurface.h>
#include "ihudlcd.h"
#ifdef LUA_SDK
#include "weapon_hl2mpbase_scriptedweapon.h"
#endif

// memdbgon must be the last include file in a .cpp file!!!
#include "tier0/memdbgon.h"

using namespace vgui;

static const char *HL2SB_AmmoLayout( bool secondary, bool primaryClip, bool secondaryClip )
{
	if ( !secondary )
		return primaryClip ? "WeaponUsesClips" : "WeaponDoesNotUseClips";
	if ( primaryClip )
		return secondaryClip ? "WeaponUsesClipsAndSecondaryClips" : "WeaponUsesClipsAndSecondaryAmmo";
	return secondaryClip ? "WeaponDoesNotUseClipsAndUsesSecondaryClips" : "WeaponDoesNotUseClipsButUsesSecondaryAmmo";
}

//-----------------------------------------------------------------------------
// Purpose: Displays current ammunition level
//-----------------------------------------------------------------------------
class CHudAmmo : public CHudNumericDisplay, public CHudElement
{
	DECLARE_CLASS_SIMPLE( CHudAmmo, CHudNumericDisplay );

public:
	CHudAmmo( const char *pElementName );
	void Init( void );
	void VidInit( void );
	void Reset();

	void SetAmmo(int ammo, bool playAnimation);
	void SetAmmo2(int ammo2, bool playAnimation);
	virtual void Paint( void );

protected:
	virtual void OnThink();

	void UpdateAmmoDisplays();
	void UpdatePlayerAmmo( C_BasePlayer *player );
	void UpdateVehicleAmmo( C_BasePlayer *player, IClientVehicle *pVehicle );
	
private:
	CHandle< C_BaseCombatWeapon > m_hCurrentActiveWeapon;
	CHandle< C_BaseEntity > m_hCurrentVehicle;
	int		m_iAmmo;
	int		m_iAmmo2;
	CHudTexture *m_iconPrimaryAmmo;
	const char *m_pszAmmoLayout;
};

DECLARE_HUDELEMENT( CHudAmmo );

//-----------------------------------------------------------------------------
// Purpose: Constructor
//-----------------------------------------------------------------------------
CHudAmmo::CHudAmmo( const char *pElementName ) : BaseClass(NULL, "HudAmmo"), CHudElement( pElementName )
{
	m_pszAmmoLayout = NULL;
	SetHiddenBits( HIDEHUD_HEALTH | HIDEHUD_PLAYERDEAD | HIDEHUD_NEEDSUIT | HIDEHUD_WEAPONSELECTION );

	hudlcd->SetGlobalStat( "(ammo_primary)", "0" );
	hudlcd->SetGlobalStat( "(ammo_secondary)", "0" );
	hudlcd->SetGlobalStat( "(weapon_print_name)", "" );
	hudlcd->SetGlobalStat( "(weapon_name)", "" );
}

//-----------------------------------------------------------------------------
// Purpose: 
//-----------------------------------------------------------------------------
void CHudAmmo::Init( void )
{
	m_iAmmo		= -1;
	m_iAmmo2	= -1;
	
	m_iconPrimaryAmmo = NULL;

	wchar_t *tempString = g_pVGuiLocalize->Find("#Valve_Hud_AMMO");
	if (tempString)
	{
		SetLabelText(tempString);
	}
	else
	{
		SetLabelText(L"AMMO");
	}
}

//-----------------------------------------------------------------------------
// Purpose: 
//-----------------------------------------------------------------------------
void CHudAmmo::VidInit( void )
{
}

//-----------------------------------------------------------------------------
// Purpose: Resets hud after save/restore
//-----------------------------------------------------------------------------
void CHudAmmo::Reset()
{
	BaseClass::Reset();

	m_hCurrentActiveWeapon = NULL;
	m_hCurrentVehicle = NULL;
	m_iAmmo = 0;
	m_iAmmo2 = 0;
	m_pszAmmoLayout = NULL;

	UpdateAmmoDisplays();
}

//-----------------------------------------------------------------------------
// Purpose: called every frame to get ammo info from the weapon
//-----------------------------------------------------------------------------
void CHudAmmo::UpdatePlayerAmmo( C_BasePlayer *player )
{
	// Clear out the vehicle entity
	m_hCurrentVehicle = NULL;

	C_BaseCombatWeapon *wpn = GetActiveWeapon();

	hudlcd->SetGlobalStat( "(weapon_print_name)", wpn ? wpn->GetPrintName() : " " );
	hudlcd->SetGlobalStat( "(weapon_name)", wpn ? wpn->GetName() : " " );

	m_iconPrimaryAmmo = NULL;
	if ( !wpn || !player )
	{
		hudlcd->SetGlobalStat( "(ammo_primary)", "n/a" );
        hudlcd->SetGlobalStat( "(ammo_secondary)", "n/a" );

		SetPaintEnabled(false);
		SetPaintBackgroundEnabled(false);
		m_hCurrentActiveWeapon = NULL;
		return;
	}

#ifdef LUA_SDK
	// GMod SWEPs can hide the ammo HUD with SWEP.DrawAmmo = false
	// (weapon_nyangun does exactly that).
	{
		CHL2MPScriptedWeapon *pScripted = dynamic_cast<CHL2MPScriptedWeapon *>( wpn );
		if ( pScripted && !pScripted->DrawAmmo() )
		{
			hudlcd->SetGlobalStat( "(ammo_primary)", "n/a" );
			hudlcd->SetGlobalStat( "(ammo_secondary)", "n/a" );
			SetPaintEnabled(false);
			SetPaintBackgroundEnabled(false);
			m_hCurrentActiveWeapon = NULL;
			return;
		}
	}
#endif

	int ammo1 = -1;
	int ammo2 = -1;
	bool bCustomDisplay = false;
	int secondaryClip = -1;
	int secondaryAmmo = -1;

#ifdef LUA_SDK
	// A custom display owns its values. Missing fields stay hidden rather
	// than inheriting an unrelated native ammo type or reserve count.
	{
		CHL2MPScriptedWeapon *pScripted = dynamic_cast<CHL2MPScriptedWeapon *>( wpn );
		if ( pScripted )
		{
			bool bDraw = true;
			int nClipOverride = -1;
			int nReserveOverride = -1;
			if ( pScripted->DispatchCustomAmmoDisplay( &bDraw, &nClipOverride, &nReserveOverride, &secondaryClip, &secondaryAmmo ) )
			{
				bCustomDisplay = true;
				if ( !bDraw || ( nClipOverride < 0 && nReserveOverride < 0 ) )
				{
					hudlcd->SetGlobalStat( "(ammo_primary)", "n/a" );
					hudlcd->SetGlobalStat( "(ammo_secondary)", "n/a" );
					SetPaintEnabled(false);
					SetPaintBackgroundEnabled(false);
					m_hCurrentActiveWeapon = NULL;
					return;
				}
				ammo1 = nClipOverride;
				ammo2 = nReserveOverride;
			}
		}
	}
#endif

	if ( !bCustomDisplay )
	{
		// Native weapons require an actual ammo type, regardless of clip size.
		if ( !wpn->UsesPrimaryAmmo() )
		{
			hudlcd->SetGlobalStat( "(ammo_primary)", "n/a" );
			hudlcd->SetGlobalStat( "(ammo_secondary)", "n/a" );
			SetPaintEnabled(false);
			SetPaintBackgroundEnabled(false);
			m_hCurrentActiveWeapon = NULL;
			return;
		}

		ammo1 = wpn->Clip1();
		const int nReserve = player->GetAmmoCount( wpn->GetPrimaryAmmoType() );
		ammo2 = ammo1 < 0 ? -1 : nReserve;
		if ( ammo1 < 0 )
			ammo1 = nReserve;
		m_iconPrimaryAmmo = gWR.GetAmmoIconFromWeapon( wpn->GetPrimaryAmmoType() );
		if ( wpn->UsesSecondaryAmmo() )
		{
			secondaryAmmo = player->GetAmmoCount( wpn->GetSecondaryAmmoType() );
			if ( wpn->UsesClipsForAmmo2() ) secondaryClip = wpn->Clip2();
		}
	}

	SetPaintEnabled(true);
	SetPaintBackgroundEnabled(true);
	const bool bShowPrimary = ammo1 >= 0;
	const bool bShowReserve = ammo2 >= 0;
	const char *layout = HL2SB_AmmoLayout( secondaryClip >= 0 || secondaryAmmo >= 0,
		bShowReserve, secondaryClip >= 0 && secondaryAmmo >= 0 );
	if ( wpn != m_hCurrentActiveWeapon || layout != m_pszAmmoLayout )
	{
		g_pClientMode->GetViewportAnimationController()->StartAnimationSequence( layout );
		m_pszAmmoLayout = layout;
	}
	SetShouldDisplayValue( bShowPrimary );
	SetShouldDisplaySecondaryValue( bShowReserve );

	hudlcd->SetGlobalStat( "(ammo_primary)", VarArgs( "%d", ammo1 ) );
	hudlcd->SetGlobalStat( "(ammo_secondary)", VarArgs( "%d", ammo2 ) );

	if (wpn == m_hCurrentActiveWeapon)
	{
		// same weapon, just update counts
		SetAmmo(ammo1, true);
		SetAmmo2(ammo2, true);
	}
	else
	{
		// diferent weapon, change without triggering
		SetAmmo(ammo1, false);
		SetAmmo2(ammo2, false);

		g_pClientMode->GetViewportAnimationController()->StartAnimationSequence("WeaponChanged");
		m_hCurrentActiveWeapon = wpn;
	}
}

void CHudAmmo::UpdateVehicleAmmo( C_BasePlayer *player, IClientVehicle *pVehicle )
{
	m_hCurrentActiveWeapon = NULL;
	CBaseEntity *pVehicleEnt = pVehicle->GetVehicleEnt();

	if ( !pVehicleEnt || pVehicle->GetPrimaryAmmoType() < 0 )
	{
		SetPaintEnabled(false);
		SetPaintBackgroundEnabled(false);
		return;
	}

	SetPaintEnabled(true);
	SetPaintBackgroundEnabled(true);
	SetShouldDisplayValue(true);

	// get the ammo in our clip
	int ammo1 = pVehicle->GetPrimaryAmmoClip();
	int ammo2;
	if (ammo1 < 0)
	{
		// we don't use clip ammo, just use the total ammo count
		ammo1 = pVehicle->GetPrimaryAmmoCount();
		ammo2 = 0;
	}
	else
	{
		// we use clip ammo, so the second ammo is the total ammo
		ammo2 = pVehicle->GetPrimaryAmmoCount();
	}

	if (pVehicleEnt == m_hCurrentVehicle)
	{
		// same weapon, just update counts
		SetAmmo(ammo1, true);
		SetAmmo2(ammo2, true);
	}
	else
	{
		// diferent weapon, change without triggering
		SetAmmo(ammo1, false);
		SetAmmo2(ammo2, false);

		// update whether or not we show the total ammo display
		if (pVehicle->PrimaryAmmoUsesClips())
		{
			SetShouldDisplaySecondaryValue(true);
			g_pClientMode->GetViewportAnimationController()->StartAnimationSequence("WeaponUsesClips");
		}
		else
		{
			g_pClientMode->GetViewportAnimationController()->StartAnimationSequence("WeaponDoesNotUseClips");
			SetShouldDisplaySecondaryValue(false);
		}

		g_pClientMode->GetViewportAnimationController()->StartAnimationSequence("WeaponChanged");
		m_hCurrentVehicle = pVehicleEnt;
	}
}

//-----------------------------------------------------------------------------
// Purpose: called every frame to get ammo info from the weapon
//-----------------------------------------------------------------------------
void CHudAmmo::OnThink()
{
	UpdateAmmoDisplays();
}

//-----------------------------------------------------------------------------
// Purpose: updates the ammo display counts
//-----------------------------------------------------------------------------
void CHudAmmo::UpdateAmmoDisplays()
{
	C_BasePlayer *player = C_BasePlayer::GetLocalPlayer();
	IClientVehicle *pVehicle = player ? player->GetVehicle() : NULL;

	if ( !pVehicle )
	{
		UpdatePlayerAmmo( player );
	}
	else
	{
		UpdateVehicleAmmo( player, pVehicle );
	}
}

//-----------------------------------------------------------------------------
// Purpose: Updates ammo display
//-----------------------------------------------------------------------------
void CHudAmmo::SetAmmo(int ammo, bool playAnimation)
{
	if (ammo != m_iAmmo)
	{
		if (ammo == 0)
		{
			g_pClientMode->GetViewportAnimationController()->StartAnimationSequence("AmmoEmpty");
		}
		else if (ammo < m_iAmmo)
		{
			// ammo has decreased
			g_pClientMode->GetViewportAnimationController()->StartAnimationSequence("AmmoDecreased");
		}
		else
		{
			// ammunition has increased
			g_pClientMode->GetViewportAnimationController()->StartAnimationSequence("AmmoIncreased");
		}

		m_iAmmo = ammo;
	}

	SetDisplayValue(ammo);
}

//-----------------------------------------------------------------------------
// Purpose: Updates 2nd ammo display
//-----------------------------------------------------------------------------
void CHudAmmo::SetAmmo2(int ammo2, bool playAnimation)
{
	if (ammo2 != m_iAmmo2)
	{
		if (ammo2 == 0)
		{
			g_pClientMode->GetViewportAnimationController()->StartAnimationSequence("Ammo2Empty");
		}
		else if (ammo2 < m_iAmmo2)
		{
			// ammo has decreased
			g_pClientMode->GetViewportAnimationController()->StartAnimationSequence("Ammo2Decreased");
		}
		else
		{
			// ammunition has increased
			g_pClientMode->GetViewportAnimationController()->StartAnimationSequence("Ammo2Increased");
		}

		m_iAmmo2 = ammo2;
	}

	SetSecondaryValue(ammo2);
}

//-----------------------------------------------------------------------------
// Purpose: We add an icon into the 
//-----------------------------------------------------------------------------
void CHudAmmo::Paint( void )
{
	BaseClass::Paint();

#ifndef HL2MP
	if ( m_hCurrentVehicle == NULL && m_iconPrimaryAmmo )
	{
		int nLabelHeight;
		int nLabelWidth;
		surface()->GetTextSize( m_hTextFont, m_LabelText, nLabelWidth, nLabelHeight );

		// Figure out where we're going to put this
		int x = text_xpos + ( nLabelWidth - m_iconPrimaryAmmo->Width() ) / 2;
		int y = text_ypos - ( nLabelHeight + ( m_iconPrimaryAmmo->Height() / 2 ) );
		
		m_iconPrimaryAmmo->DrawSelf( x, y, GetFgColor() );
	}
#endif
}

//-----------------------------------------------------------------------------
// Purpose: Displays the secondary ammunition level
//-----------------------------------------------------------------------------
class CHudSecondaryAmmo : public CHudNumericDisplay, public CHudElement
{
	DECLARE_CLASS_SIMPLE( CHudSecondaryAmmo, CHudNumericDisplay );

public:
	CHudSecondaryAmmo( const char *pElementName ) : BaseClass( NULL, "HudAmmoSecondary" ), CHudElement( pElementName )
	{
		m_iAmmo = -1;
		m_bSecondaryActive = false;
		m_bSecondaryClip = false;
		m_iconSecondaryAmmo = NULL;

		SetHiddenBits( HIDEHUD_HEALTH | HIDEHUD_WEAPONSELECTION | HIDEHUD_PLAYERDEAD | HIDEHUD_NEEDSUIT );
	}

	void Init( void )
	{
		wchar_t *tempString = g_pVGuiLocalize->Find("#Valve_Hud_AMMO_ALT");
		if (tempString)
		{
			SetLabelText(tempString);
		}
		else
		{
			SetLabelText(L"ALT");
		}
	}

	void VidInit( void )
	{
	}

	void SetAmmo( int ammo )
	{
		if (ammo != m_iAmmo)
		{
			if (ammo == 0)
			{
				g_pClientMode->GetViewportAnimationController()->StartAnimationSequence("AmmoSecondaryEmpty");
			}
			else if (ammo < m_iAmmo)
			{
				// ammo has decreased
				g_pClientMode->GetViewportAnimationController()->StartAnimationSequence("AmmoSecondaryDecreased");
			}
			else
			{
				// ammunition has increased
				g_pClientMode->GetViewportAnimationController()->StartAnimationSequence("AmmoSecondaryIncreased");
			}

			m_iAmmo = ammo;
		}
		SetDisplayValue( ammo );
	}

	void Reset()
	{
		// hud reset, update ammo state
		BaseClass::Reset();
		m_iAmmo = 0;
		m_hCurrentActiveWeapon = NULL;
		m_bSecondaryActive = false;
		m_bSecondaryClip = false;
		SetAlpha( 0 );
		UpdateAmmoState();
	}

	virtual void Paint( void )
	{
		BaseClass::Paint();

#ifndef HL2MP
		if ( m_iconSecondaryAmmo )
		{
			int nLabelHeight;
			int nLabelWidth;
			surface()->GetTextSize( m_hTextFont, m_LabelText, nLabelWidth, nLabelHeight );

			// Figure out where we're going to put this
			int x = text_xpos + ( nLabelWidth - m_iconSecondaryAmmo->Width() ) / 2;
			int y = text_ypos - ( nLabelHeight + ( m_iconSecondaryAmmo->Height() / 2 ) );

			m_iconSecondaryAmmo->DrawSelf( x, y, GetFgColor() );
		}
#endif // HL2MP
	}

protected:

	virtual void OnThink()
	{
		// set whether or not the panel draws based on if we have a weapon that supports secondary ammo
		C_BaseCombatWeapon *wpn = GetActiveWeapon();
		C_BasePlayer *player = C_BasePlayer::GetLocalPlayer();
		IClientVehicle *pVehicle = player ? player->GetVehicle() : NULL;
		if (!wpn || !player || pVehicle)
		{
			m_hCurrentActiveWeapon = NULL;
			SetPaintEnabled(false);
			SetPaintBackgroundEnabled(false);
			return;
		}

#ifdef LUA_SDK
		// SWEP.DrawAmmo = false hides both ammo panels (GMod semantic).
		{
			CHL2MPScriptedWeapon *pScripted = dynamic_cast<CHL2MPScriptedWeapon *>( wpn );
			if ( pScripted && !pScripted->DrawAmmo() )
			{
				SetPaintEnabled(false);
				SetPaintBackgroundEnabled(false);
				return;
			}
		}
#endif

		SetPaintEnabled(true);
		SetPaintBackgroundEnabled(true);

		UpdateAmmoState();
	}

	void UpdateAmmoState()
	{
		C_BaseCombatWeapon *wpn = GetActiveWeapon();
		C_BasePlayer *player = C_BasePlayer::GetLocalPlayer();

		int clip = -1, reserve = -1;
		bool custom = false, draw = true;
#ifdef LUA_SDK
		CHL2MPScriptedWeapon *scripted = dynamic_cast<CHL2MPScriptedWeapon *>( wpn );
		if ( scripted )
			custom = scripted->DispatchCustomAmmoDisplay( &draw, NULL, NULL, &clip, &reserve );
#endif
		if ( !custom && player && wpn && wpn->UsesSecondaryAmmo() )
		{
			reserve = player->GetAmmoCount( wpn->GetSecondaryAmmoType() );
			if ( wpn->UsesClipsForAmmo2() ) clip = wpn->Clip2();
		}
		const bool active = draw && ( clip >= 0 || reserve >= 0 );
		const bool hasClip = clip >= 0 && reserve >= 0;
		if ( m_hCurrentActiveWeapon != wpn || active != m_bSecondaryActive || hasClip != m_bSecondaryClip )
		{
			g_pClientMode->GetViewportAnimationController()->StartAnimationSequence(
				active ? "WeaponUsesSecondaryAmmo" : "WeaponDoesNotUseSecondaryAmmo" );
			g_pClientMode->GetViewportAnimationController()->StartAnimationSequence(
				hasClip ? "WeaponUsesSecondaryClips" : "WeaponDoesNotUseSecondaryClips" );
			m_hCurrentActiveWeapon = wpn;
			m_bSecondaryActive = active;
			m_bSecondaryClip = hasClip;
			m_iconSecondaryAmmo = wpn ? gWR.GetAmmoIconFromWeapon( wpn->GetSecondaryAmmoType() ) : NULL;
		}
		SetShouldDisplayValue( active );
		SetShouldDisplaySecondaryValue( hasClip );
		if ( active )
		{
			SetAmmo( clip >= 0 ? clip : reserve );
			if ( hasClip ) SetSecondaryValue( reserve );
		}
	}
	
private:
	CHandle< C_BaseCombatWeapon > m_hCurrentActiveWeapon;
	CHudTexture *m_iconSecondaryAmmo;
	int		m_iAmmo;
	bool m_bSecondaryActive;
	bool m_bSecondaryClip;
};

DECLARE_HUDELEMENT( CHudSecondaryAmmo );

