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
#include "vgui_controls/TreeView.h"
#include "vgui_controls/InputDialog.h"

//----------------------------------------------------------------------------------------

class CSchemeEditor : public vgui::Frame
{
	DECLARE_CLASS_SIMPLE(CSchemeEditor, vgui::Frame );

public:
	CSchemeEditor( vgui::Panel *pParent );
	virtual void	OnCommand(const char* cmd);
	virtual void	ApplySchemeSettings(vgui::IScheme* sch);
	
private:
	virtual void	PopulateFileList(const char* startpath, int rootindex);
	virtual void	RepopulateFileList();
	virtual void	PerformLayout();

	MESSAGE_FUNC_INT(OnFileSelected, "TreeViewItemSelected", itemIndex);
	//MESSAGE_FUNC_PARAMS(OnInputPrompt, "InputCompleted", kv);
	//MESSAGE_FUNC(OnClosePrompt, "InputCanceled");


	vgui::Button* m_pNewButton;
	vgui::Button* m_pSaveButton;
	vgui::Button* m_pSaveAsButton;
	vgui::Button* m_pApplyButton;
	vgui::TreeView* m_pFileList;
	vgui::InputDialog* m_pDialog;
	CUtlVector<char> m_sFileBuffer;
	char m_sCurrentFile[MAX_PATH] = { 0 };

	vgui::PropertySheet* m_pTabs;
	vgui::Button* m_pPreviewButton;
	vgui::CheckButton* m_pPreviewCheckButton;
	vgui::RadioButton* m_pPreviewRadioButton;
	vgui::Label* m_pPreviewLabel;
};

//----------------------------------------------------------------------------------------

#endif //  VGUI_CLIENTSCHEMEEDITOR_H