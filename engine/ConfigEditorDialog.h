#ifndef CONFIGEDITORDIALOG_H
#define CONFIGEDITORDIALOG_H
#ifdef _WIN32
#pragma once
#endif

#include "vgui_controls/Frame.h"
#include "vgui_controls/TextEntry.h"
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


class CConfigEditorTextEntry : public vgui::TextEntry
{
	DECLARE_CLASS_SIMPLE(CConfigEditorTextEntry, vgui::TextEntry);
public:

	CConfigEditorTextEntry(Panel* parent, char const* panelName) :
		BaseClass(parent, panelName)
	{
		m_bAutoCompleting = false;
	}

	virtual void OnKeyCodeTyped(vgui::KeyCode code);

	bool m_bAutoCompleting;
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
	virtual void ApplySchemeSettings(vgui::IScheme* pScheme);
	virtual void OnComplete();
	virtual void OnCycle(bool reverse);

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
	int m_iNextCompletion;
	bool m_bAutoCompleteMode;

	MESSAGE_FUNC_INT(OnFileSelected, "TreeViewItemSelected", itemIndex);
	MESSAGE_FUNC_PARAMS(OnInputPrompt, "InputCompleted", kv);
	MESSAGE_FUNC(OnClosePrompt, "InputCanceled");
	MESSAGE_FUNC(OnTextChanged, "TextChanged");
	MESSAGE_FUNC_CHARPTR(OnMenuItemSelected, "CompletionCommand", command);

	virtual void PopulateFileList(const char* startpath, int rootindex);
	virtual void RepopulateFileList();
	virtual void CompleteText(const char* completion);
	virtual void FillCompletionMenu();
	CConfigEditorTextEntry *m_pTextEntry;
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