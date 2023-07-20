#ifndef CONFIGEDITORDIALOG_H
#define CONFIGEDITORDIALOG_H
#ifdef _WIN32
#pragma once
#endif

#include "vgui_controls/Frame.h"
#include "vgui_controls/InputDialog.h"

// Things the user typed in and hit submit/return with
class CHistoryItem
{
public:
	CHistoryItem(void);
	CHistoryItem(const char* text, const char* extra = NULL);
	CHistoryItem(const CHistoryItem& src);
	~CHistoryItem(void);

	const char* GetText() const;
	const char* GetExtra() const;
	void SetText(const char* text, const char* extra);
	bool HasExtra() { return m_bHasExtra; }

private:
	char* m_text;
	char* m_extraText;
	bool		m_bHasExtra;
};

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

	class CompletionItem
	{
	public:
		CompletionItem(void);
		CompletionItem(const CompletionItem& src);
		CompletionItem& operator =(const CompletionItem& src);
		~CompletionItem(void);
		const char* GetItemText(void);
		const char* GetCommand(void) const;
		const char* GetName() const;

		bool			m_bIsCommand;
		ConCommandBase* m_pCommand;
		CHistoryItem* m_pText;
	};

	CUtlVector< CompletionItem* > m_CompletionList;

	MESSAGE_FUNC_INT(OnFileSelected, "TreeViewItemSelected", itemIndex);
	MESSAGE_FUNC_PARAMS(OnInputPrompt, "InputCompleted", kv);
	MESSAGE_FUNC(OnClosePrompt, "InputCanceled");
	MESSAGE_FUNC(OnTextChanged, "TextChanged");

	virtual void PopulateFileList(const char* startpath, int rootindex);
	virtual void RepopulateFileList();

	vgui::TextEntry *m_pTextEntry;
	vgui::Button* m_pNewButton;
	vgui::Button* m_pSaveButton;
	vgui::Button* m_pSaveAsButton;
	vgui::Button* m_pExecuteButton;
	vgui::TreeView* m_pFileList;
	vgui::InputDialog* m_pDialog;
	vgui::Menu* m_pCompletionList;
	CUtlVector<char> m_sFileBuffer;
	char m_sCurrentFile[MAX_PATH] = { 0 };

};
extern CConfigEditorDialog* g_ConfigEditorDialog;
#endif // CONFIGEDITORDIALOG_H