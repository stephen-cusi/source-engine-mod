//========= Copyright Valve Corporation, All rights reserved. ============//
//
//=======================================================================================//

#ifndef VGUI_CLIENTSCHEMEEDITOR_H
#define VGUI_CLIENTSCHEMEEDITOR_H

//----------------------------------------------------------------------------------------

#include "vgui_controls/Frame.h"
#include "vgui_controls/PanelListPanel.h"
#include "vgui_controls/PropertySheet.h"
#include "vgui_controls/Button.h"
#include "vgui_controls/CheckButton.h"
#include "vgui_controls/RadioButton.h"
#include "vgui_controls/Label.h"

//----------------------------------------------------------------------------------------

class CSchemeEditor : public vgui::Frame
{
	DECLARE_CLASS_SIMPLE(CSchemeEditor, vgui::Frame );

public:
	CSchemeEditor( vgui::Panel *pParent );
	virtual void	OnCommand(const char* cmd);
	virtual void	ApplySchemeSettings(vgui::IScheme* sch);
private:
	virtual void	PerformLayout();
	vgui::PropertySheet* m_pTabs;
	vgui::Button* m_pPreviewButton;
	vgui::CheckButton* m_pPreviewCheckButton;
	vgui::RadioButton* m_pPreviewRadioButton;
	vgui::Label* m_pPreviewLabel;
	vgui::Button* m_pApplyButton;
};

//----------------------------------------------------------------------------------------

#endif //  VGUI_CLIENTSCHEMEEDITOR_H