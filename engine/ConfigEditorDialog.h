#ifndef CONFIGEDITORDIALOG_H
#define CONFIGEDITORDIALOG_H
#ifdef _WIN32
#pragma once
#endif

#include "vgui_controls/Frame.h"
#include "vgui_controls/InputDialog.h"

//-----------------------------------------------------------------------------
// Purpose: Browses config files and edits them
//-----------------------------------------------------------------------------
class CConfigEditorDialog : public vgui::Frame
{
	DECLARE_CLASS_SIMPLE(CConfigEditorDialog, vgui::Frame);

public:

	CConfigEditorDialog(vgui::Panel* parent);
	virtual void PerformLayout();
	virtual void OnCommand(const char *command);
	static	void	InstallConfigEditor(vgui::Panel* parent);

private:

	MESSAGE_FUNC_INT(OnFileSelected, "TreeViewItemSelected", itemIndex);
	MESSAGE_FUNC_PARAMS(OnInputPrompt, "InputCompleted", kv);
	MESSAGE_FUNC(OnClosePrompt, "InputCanceled");

	virtual void PopulateFileList(const char* startpath, int rootindex);
	virtual void RepopulateFileList();

	vgui::TextEntry *m_pTextEntry;
	vgui::Button* m_pNewButton;
	vgui::Button* m_pSaveButton;
	vgui::Button* m_pSaveAsButton;
	vgui::Button* m_pExecuteButton;
	vgui::TreeView* m_pFileList;
	vgui::InputDialog* m_pDialog;
	CUtlVector<char> m_sFileBuffer;
	char m_sCurrentFile[MAX_PATH] = { 0 };

};
extern CConfigEditorDialog* g_ConfigEditorDialog;
#endif // CONFIGEDITORDIALOG_H