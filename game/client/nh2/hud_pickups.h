//========= Copyright Valve Corporation, All rights reserved. ============//
//
// Purpose: Item pickup history displayed onscreen when items are picked up.
//
// $NoKeywords: $
//=============================================================================//

#ifndef PICKUPS_H
#define PICKUPS_H
#pragma once

#include "hudelement.h"
#include "ehandle.h"

#include <vgui_controls/Panel.h>

class C_BaseCombatWeapon;

//-----------------------------------------------------------------------------
// Purpose: Used to draw the history of ammo / weapon / item pickups by the player
//-----------------------------------------------------------------------------
class CHudPickups : public CHudElement, public vgui::Panel
{
	DECLARE_CLASS_SIMPLE( CHudPickups, vgui::Panel );

public:
	CHudPickups( const char *pElementName );

	// CHudElement overrides
	virtual void Init();
	virtual void Reset();
	virtual void Think();
	virtual void Paint();
	virtual void PaintBackground() {};

	void ShowPickup( C_BaseCombatWeapon *weapon );
	void ShowAmmo(int type, int quantity);
	void MsgFunc_ItemPickup( bf_read &msg );

private:
	vgui::IImage* m_pNumbers;
	vgui::IImage* m_pHealthKit;
	vgui::IImage* m_pHealthVial;
	vgui::IImage* m_pWep[5];
	vgui::IImage* m_pAmmo[4];
	int m_iWep;
	int m_iAmmo;
	int m_iAmmoQuantity;

	bool m_bKitVisible;
	bool m_bVialVisible;
	bool m_bWepVisible;
	bool m_bAmmoVisible;
	float m_flKitUntil;
	float m_flVialUntil;
	float m_flWepUntil;
	float m_flAmmoUntil;

	CPanelAnimationVar(float, m_flWepX, "WepX", "0.1");
	CPanelAnimationVar(float, m_flWepY, "WepY", "0.1");
	CPanelAnimationVar(float, m_flWepScale, "WepScale", "0.1");
	CPanelAnimationVar(float, m_flWepAlpha, "WepAlpha", "0");
	CPanelAnimationVar(float, m_flKitX, "KitX", "0.2");
	CPanelAnimationVar(float, m_flKitY, "KitY", "0.1");
	CPanelAnimationVar(float, m_flKitScale, "KitScale", "0.1");
	CPanelAnimationVar(float, m_flKitAlpha, "KitAlpha", "0");
	CPanelAnimationVar(float, m_flVialX, "VialX", "0.3");
	CPanelAnimationVar(float, m_flVialY, "VialY", "0.1");
	CPanelAnimationVar(float, m_flVialScale, "VialScale", "0.1");
	CPanelAnimationVar(float, m_flVialAlpha, "VialAlpha", "0");
	CPanelAnimationVar(float, m_flAmmoX, "AmmoX", "0.4");
	CPanelAnimationVar(float, m_flAmmoY, "AmmoY", "0.1");
	CPanelAnimationVar(float, m_flAmmoScale, "AmmoScale", "0.1");
	CPanelAnimationVar(float, m_flAmmoAlpha, "AmmoAlpha", "0");
	CPanelAnimationVar(float, m_flAmmoQuantityX, "AmmoQuantityX", "0.5");
	CPanelAnimationVar(float, m_flAmmoQuantityY, "AmmoQuantityY", "0.1");
	CPanelAnimationVar(float, m_flAmmoQuantityScale, "AmmoQuantityScale", "0.1");

	CPanelAnimationVar(float, m_flDisplayTime, "DisplayTime", "3.0");
	CPanelAnimationVar(float, m_flFadeTime, "FadeTime", "1.0");
};

#endif // PICKUPS_H
