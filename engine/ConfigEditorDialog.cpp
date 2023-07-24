#include "ConfigEditorDialog.h"
#include "vgui_controls/TextEntry.h"
#include "vgui_controls/Button.h"
#include "vgui_controls/InputDialog.h"
#include "vgui_controls/TreeView.h"
#include "vgui_controls/Menu.h"
#include "vgui/IScheme.h"
#include "vgui/IInput.h"
#include "vgui_controls/Controls.h"
#include "vgui/IVGui.h"
#include <KeyValues.h>
#include "filesystem.h"
#include "common.h"
#include "cmd.h"
#include "convar.h"
#include "convar_serverbounded.h"

using namespace vgui;

//-----------------------------------------------------------------------------
// Used by the autocompletion system
//-----------------------------------------------------------------------------
class CNonFocusableMenu : public Menu
{
	DECLARE_CLASS_SIMPLE(CNonFocusableMenu, Menu);

public:
	CNonFocusableMenu(Panel* parent, const char* panelName)
		: BaseClass(parent, panelName),
		m_pFocus(0)
	{
	}

	void SetFocusPanel(Panel* panel)
	{
		m_pFocus = panel;
	}

	VPANEL GetCurrentKeyFocus()
	{
		if (!m_pFocus)
			return GetVPanel();

		return m_pFocus->GetVPanel();
	}

private:
	Panel* m_pFocus;
};



// Things the user typed in and hit submit/return with
CHistoryItem::CHistoryItem(void)
{
	m_text = NULL;
	m_extraText = NULL;
	m_bHasExtra = false;
}

CHistoryItem::CHistoryItem(const char* text, const char* extra)
{
	Assert(text);
	m_text = NULL;
	m_extraText = NULL;
	m_bHasExtra = false;
	SetText(text, extra);
}

CHistoryItem::CHistoryItem(const CHistoryItem& src)
{
	m_text = NULL;
	m_extraText = NULL;
	m_bHasExtra = false;
	SetText(src.GetText(), src.GetExtra());
}

CHistoryItem::~CHistoryItem(void)
{
	delete[] m_text;
	delete[] m_extraText;
	m_text = NULL;
}

const char* CHistoryItem::GetText() const
{
	if (m_text)
	{
		return m_text;
	}
	else
	{
		return "";
	}
}

const char* CHistoryItem::GetExtra() const
{
	if (m_extraText)
	{
		return m_extraText;
	}
	else
	{
		return NULL;
	}
}

void CHistoryItem::SetText(const char* text, const char* extra)
{
	delete[] m_text;
	int len = strlen(text) + 1;

	m_text = new char[len];
	Q_memset(m_text, 0x0, len);
	Q_strncpy(m_text, text, len);

	if (extra)
	{
		m_bHasExtra = true;
		delete[] m_extraText;
		int elen = strlen(extra) + 1;
		m_extraText = new char[elen];
		Q_memset(m_extraText, 0x0, elen);
		Q_strncpy(m_extraText, extra, elen);
	}
	else
	{
		m_bHasExtra = false;
	}
}


//-----------------------------------------------------------------------------
//
// Console page completion item starts here
//
//-----------------------------------------------------------------------------
CConfigEditorDialog::CompletionItem::CompletionItem(void)
{
	m_bIsCommand = true;
	m_pCommand = NULL;
	m_pText = NULL;
}

CConfigEditorDialog::CompletionItem::CompletionItem(const CompletionItem& src)
{
	m_bIsCommand = src.m_bIsCommand;
	m_pCommand = src.m_pCommand;
	if (src.m_pText)
	{
		m_pText = new CHistoryItem((const CHistoryItem&)src.m_pText);
	}
	else
	{
		m_pText = NULL;
	}
}

CConfigEditorDialog::CompletionItem& CConfigEditorDialog::CompletionItem::operator =(const CompletionItem& src)
{
	if (this == &src)
		return *this;

	m_bIsCommand = src.m_bIsCommand;
	m_pCommand = src.m_pCommand;
	if (src.m_pText)
	{
		m_pText = new CHistoryItem((const CHistoryItem&)*src.m_pText);
	}
	else
	{
		m_pText = NULL;
	}

	return *this;
}

CConfigEditorDialog::CompletionItem::~CompletionItem(void)
{
	if (m_pText)
	{
		delete m_pText;
		m_pText = NULL;
	}
}

const char* CConfigEditorDialog::CompletionItem::GetName() const
{
	if (m_bIsCommand)
		return m_pCommand->GetName();
	return m_pCommand ? m_pCommand->GetName() : GetCommand();
}

const char* CConfigEditorDialog::CompletionItem::GetItemText(void)
{
	static char text[256];
	text[0] = 0;
	if (m_pText)
	{
		if (m_pText->HasExtra())
		{
			Q_snprintf(text, sizeof(text), "%s %s", m_pText->GetText(), m_pText->GetExtra());
		}
		else
		{
			Q_strncpy(text, m_pText->GetText(), sizeof(text));
		}
	}
	return text;
}

const char* CConfigEditorDialog::CompletionItem::GetCommand(void) const
{
	static char text[256];
	text[0] = 0;
	if (m_pText)
	{
		Q_strncpy(text, m_pText->GetText(), sizeof(text));
	}
	return text;
}



//-----------------------------------------------------------------------------
// Purpose: swallows tab key pressed
//-----------------------------------------------------------------------------
void CConfigEditorTextEntry::OnKeyCodeTyped(KeyCode code)
{
	if (!m_bAutoCompleting)
	{
		BaseClass::OnKeyCodeTyped(code);
		return;
	}
	if (code == KEY_TAB || code == KEY_ENTER)
	{
		reinterpret_cast<CConfigEditorDialog*>(GetParent())->OnComplete();
	}
	else if (code == KEY_DOWN)
	{
		reinterpret_cast<CConfigEditorDialog*>(GetParent())->OnCycle(false);
	}
	else if (code == KEY_UP)
	{
		reinterpret_cast<CConfigEditorDialog*>(GetParent())->OnCycle(true);
	}
	else 
	{
		BaseClass::OnKeyCodeTyped(code);
	}

}


CConfigEditorDialog::CConfigEditorDialog(vgui::Panel* parent) : BaseClass(parent, "CConfigEditorDialog")
{
	SetBounds(0, 0, 512 + 64 + 16, 340);
	SetMinimumSize(512 + 64 + 16, 100);
	SetSizeable(true);
	SetVisible(false);
	m_pTextEntry = new CConfigEditorTextEntry(this,"CodeInput");
	m_pTextEntry->SetMultiline(true);
	m_pTextEntry->SetCatchEnterKey(true);
	m_pTextEntry->SetVerticalScrollbar(true);
	m_pTextEntry->SetHorizontalScrolling(true);
	m_pTextEntry->AddActionSignalTarget(this);
	m_pNewButton = new Button(this, "ConfigNewButton", "New", this, "new");
	m_pSaveButton = new Button(this, "ConfigSaveButton", "Save", this, "save");
	m_pSaveAsButton = new Button(this, "ConfigSaveAsButton", "Save As", this, "saveas");
	m_pExecuteButton = new Button(this, "ConfigExecuteButton", "Execute", this, "execute");
	m_pFileList = new TreeView(this, "ConfigFileList");
	CNonFocusableMenu* pCompletionList = new CNonFocusableMenu(this, "CompletionList");
	pCompletionList->SetFocusPanel(m_pTextEntry);
	m_pCompletionList = pCompletionList;
	m_pCompletionList->SetVisible(false);
	RepopulateFileList();
	SetTitle("Untitled",true);
	m_pDialog = NULL;
	m_sFileBuffer = CUtlVector<char>(0,1);
	m_bAutoCompleteMode = false;
}
void CConfigEditorDialog::ApplySchemeSettings(IScheme* pScheme)
{
	BaseClass::ApplySchemeSettings(pScheme);
	m_pCompletionList->SetFont(pScheme->GetFont("DefaultSmall"));
}
void CConfigEditorDialog::RepopulateFileList()
{
	m_pFileList->RemoveAll();
	KeyValues* kv = new KeyValues("root", "Text", "cfg");
	kv->SetString("PathWithoutName", "cfg/");
	int rootindex = m_pFileList->AddItem(kv, -1);
	m_pFileList->SetFgColor(Color(255, 255, 255, 255));
	m_pFileList->SetItemFgColor(rootindex, Color(127, 127, 127, 255));
	m_pFileList->AddActionSignalTarget(this);
	kv->deleteThis();
	PopulateFileList("cfg/", rootindex);
}

void CConfigEditorDialog::CompleteText(const char* completion)
{
	int beginning = m_pTextEntry->GetCursorPos();
	if (beginning == 0)
		return;
	for (; m_pTextEntry->m_TextStream[beginning-1] != '\n' && beginning > 0; beginning--)
	{

	}
	m_pTextEntry->_select[0] = beginning;
	m_pTextEntry->_select[1] = m_pTextEntry->GetCursorPos();
	m_pTextEntry->DeleteSelected();
	m_pTextEntry->InsertString(completion);
	OnTextChanged();
	
}

void CConfigEditorDialog::OnMenuItemSelected(const char* command)
{
	if (strstr(command, "...")) // stop the menu going away if you click on ...
	{
		m_pCompletionList->SetVisible(true);
	}
	else
	{
		CompleteText(command);
	}
}

void CConfigEditorDialog::OnComplete()
{
	// match found, set text
	char completedText[256];
	CompletionItem* item = m_CompletionList[m_iNextCompletion-1];
	Assert(item);

	if (!item->m_bIsCommand && item->m_pCommand)
	{
		Q_strncpy(completedText, item->GetCommand(), sizeof(completedText) - 2);
	}
	else
	{
		Q_strncpy(completedText, item->GetItemText(), sizeof(completedText) - 2);
	}

	if (!Q_strstr(completedText, " "))
	{
		Q_strncat(completedText, " ", sizeof(completedText), COPY_ALL_CHARACTERS);
	}

	CompleteText(completedText);
}

//-----------------------------------------------------------------------------
// Purpose: auto completes current text
//-----------------------------------------------------------------------------
void CConfigEditorDialog::OnCycle(bool reverse)
{
	if (!m_bAutoCompleteMode)
	{
		// we're not in auto-complete mode, Start
		m_iNextCompletion = 0;
		m_bAutoCompleteMode = true;
	}

	// if we're in reverse, move back to before the current
	if (reverse)
	{
		m_iNextCompletion -= 2;
		if (m_iNextCompletion < 0)
		{
			// loop around in reverse
			m_iNextCompletion = m_CompletionList.Size() - 1;
		}
	}

	// get the next completion
	if (!m_CompletionList.IsValidIndex(m_iNextCompletion))
	{
		// loop completion list
		m_iNextCompletion = 0;
	}

	// make sure everything is still valid
	if (!m_CompletionList.IsValidIndex(m_iNextCompletion))
		return;

	m_iNextCompletion++;

	FillCompletionMenu();
}

void CConfigEditorDialog::PopulateFileList(const char* startpath, int rootindex)
{
	FileFindHandle_t findHandle;
	char searchpath[MAX_PATH];
	strcpy(searchpath, startpath);
	strcat(searchpath, "*");
	const char* pszFileName = g_pFullFileSystem->FindFirst(searchpath, &findHandle);
	while (pszFileName)
	{
		if (pszFileName[0] == '.')
		{
			pszFileName = g_pFullFileSystem->FindNext(findHandle);
			continue;
		}
		int len = strlen(pszFileName);
		if (g_pFullFileSystem->FindIsDirectory(findHandle))
		{
			char pNextPath[MAX_PATH];
			strcpy(pNextPath, startpath);
			strcat(pNextPath, pszFileName);
			strcat(pNextPath, "/");
			KeyValues* kv = new KeyValues("folder","Text", pszFileName);
			kv->SetBool("Folder", true);
			kv->SetString("PathWithoutName", startpath);
			int folderindex = m_pFileList->AddItem(kv, rootindex);
			m_pFileList->SetItemFgColor(folderindex, Color(224, 255, 255,255));
			kv->deleteThis();
			PopulateFileList(pNextPath, folderindex);
		}
		else if(len >= 4 && pszFileName[len-4] == '.' && pszFileName[len - 3] == 'c' && pszFileName[len - 2] == 'f' && pszFileName[len - 1] == 'g')
		{
			KeyValues* kv = new KeyValues("file", "Text", pszFileName);
			char pFilePath[MAX_PATH];
			strcpy(pFilePath, startpath);
			strcat(pFilePath, pszFileName);
			kv->SetString("Path", pFilePath);
			kv->SetBool("Folder", false);
			kv->SetString("PathWithoutName", startpath);
			m_pFileList->SetItemFgColor(m_pFileList->AddItem(kv, rootindex),Color(255,255,255,255));
			kv->deleteThis();
		}
		pszFileName = g_pFullFileSystem->FindNext(findHandle);
	}
}
void CConfigEditorDialog::OnClosePrompt()
{
	m_pDialog->MarkForDeletion();
	m_pDialog = NULL;
}

static ConCommand* FindAutoCompleteCommmandFromPartial(const char* partial)
{
	char command[256];
	Q_strncpy(command, partial, sizeof(command));

	char* space = Q_strstr(command, " ");
	if (space)
	{
		*space = 0;
	}

	ConCommand* cmd = g_pCVar->FindCommand(command);
	if (!cmd)
		return NULL;

	if (!cmd->CanAutoComplete())
		return NULL;

	return cmd;
}


void CConfigEditorDialog::FillCompletionMenu()
{
	m_pCompletionList->SetVisible(true);
	m_pCompletionList->DeleteAllItems();
	m_pTextEntry->m_bAutoCompleting = true;
	const int MAX_MENU_ITEMS = 10;
	int scrollOffset = min(max(1, m_CompletionList.Count() - MAX_MENU_ITEMS + 1), m_iNextCompletion);
	// add the first ten items to the list
	for (int i = 0; i < m_CompletionList.Count() && i < MAX_MENU_ITEMS; i++)
	{
		char text[256];
		text[0] = 0;
		int index = i + scrollOffset - 1;
		if (i == MAX_MENU_ITEMS - 1 && index != m_CompletionList.Count() - 1)
		{
			Q_strncpy(text, "...", sizeof(text));
		}
		else
		{
			Q_strncpy(text, m_CompletionList[index]->GetItemText(), sizeof(text));
		}
		KeyValues* kv = new KeyValues("CompletionCommand");
		kv->SetString("command", text);
		int itemindex = m_pCompletionList->AddMenuItem(text, kv, this);
		if (index == m_iNextCompletion - 1)
		{
			m_pCompletionList->SetCurrentlyHighlightedItem(itemindex);
		}
	}
	int cx, cy;
	m_pTextEntry->CursorToPixelSpace(m_pTextEntry->GetCursorPos(), cx, cy);
	m_pTextEntry->LocalToScreen(cx, cy);
	m_pCompletionList->SetPos(cx, cy);
}


void CConfigEditorDialog::OnTextChanged()
{
	m_bAutoCompleteMode = false;
	m_pTextEntry->m_bAutoCompleting = false;
	int length = m_pTextEntry->GetTextLength();
	if (length == 0)
	{
		m_pCompletionList->SetVisible(false);
		return;
	}
	//const wchar_t* text = m_pTextEntry->m_TextStream.Base();
	int beginning = m_pTextEntry->GetCursorPos();
	for (; (beginning > 0) && m_pTextEntry->m_TextStream[beginning-1] != '\n'; beginning--)
	{
		
	}
	int end = m_pTextEntry->GetCursorPos();
	for (; (end < length - 1) && m_pTextEntry->m_TextStream[end+1] != '\n'; end++)
	{
		
	}
	int len = end - beginning;
	if (len == 0)
	{
		m_pCompletionList->SetVisible(false);
		return;
	}
	char text[COMMAND_MAX_LENGTH];
	V_wcstostr(&m_pTextEntry->m_TextStream.Base()[beginning], len, text, COMMAND_MAX_LENGTH);
	
	
	int c = m_CompletionList.Count();
	int i;
	for (i = c - 1; i >= 0; i--)
	{
		delete m_CompletionList[i];
	}
	m_CompletionList.Purge();

	bool bNormalBuild = true;

	// if there is a space in the text, and the command isn't of the type to know how to autocomplet, then command completion is over
	const char* space = strstr(text, " ");
	if (space)
	{
		ConCommand* pCommand = FindAutoCompleteCommmandFromPartial(text);
		if (!pCommand)
		{
			m_pCompletionList->SetVisible(false);
			return;
		}
		bNormalBuild = false;

		CUtlVector< CUtlString > commands;
		int count = pCommand->AutoCompleteSuggest(text, commands);
		Assert(count <= COMMAND_COMPLETION_MAXITEMS);
		int i;

		for (i = 0; i < count; i++)
		{
			// match found, add to list
			CompletionItem* item = new CompletionItem();
			m_CompletionList.AddToTail(item);
			item->m_bIsCommand = false;
			item->m_pCommand = NULL;
			item->m_pText = new CHistoryItem(commands[i].String());
		}
	}

	if (bNormalBuild)
	{
		// look through the command list for all matches
		ConCommandBase const* cmd = (ConCommandBase const*)cvar->GetCommands();
		while (cmd)
		{
			if (cmd->IsFlagSet(FCVAR_DEVELOPMENTONLY) || cmd->IsFlagSet(FCVAR_HIDDEN))
			{
				cmd = cmd->GetNext();
				continue;
			}

			if (!strnicmp(text, cmd->GetName(), len))
			{
				// match found, add to list
				CompletionItem* item = new CompletionItem();
				m_CompletionList.AddToTail(item);
				item->m_pCommand = (ConCommandBase*)cmd;
				const char* tst = cmd->GetName();
				if (!cmd->IsCommand())
				{
					item->m_bIsCommand = false;
					ConVar* var = (ConVar*)cmd;
					ConVar_ServerBounded* pBounded = dynamic_cast<ConVar_ServerBounded*>(var);
					if (pBounded || var->IsFlagSet(FCVAR_NEVER_AS_STRING))
					{
						char strValue[512];

						int intVal = pBounded ? pBounded->GetInt() : var->GetInt();
						float floatVal = pBounded ? pBounded->GetFloat() : var->GetFloat();

						if (floatVal == intVal)
							Q_snprintf(strValue, sizeof(strValue), "%d", intVal);
						else
							Q_snprintf(strValue, sizeof(strValue), "%f", floatVal);

						item->m_pText = new CHistoryItem(var->GetName(), strValue);
					}
					else
					{
						item->m_pText = new CHistoryItem(var->GetName(), var->GetString());
					}
				}
				else
				{
					item->m_bIsCommand = true;
					item->m_pText = new CHistoryItem(tst);
				}
			}

			cmd = cmd->GetNext();
		}

		// Now sort the list by command name
		if (m_CompletionList.Count() >= 2)
		{
			for (int i = 0; i < m_CompletionList.Count(); i++)
			{
				for (int j = i + 1; j < m_CompletionList.Count(); j++)
				{
					const CompletionItem* i1, * i2;
					i1 = m_CompletionList[i];
					i2 = m_CompletionList[j];

					if (Q_stricmp(i1->GetName(), i2->GetName()) > 0)
					{
						CompletionItem* temp = m_CompletionList[i];
						m_CompletionList[i] = m_CompletionList[j];
						m_CompletionList[j] = temp;
					}
				}
			}
		}
	}

	if (m_CompletionList.Count() < 1)
	{
		m_pCompletionList->SetVisible(false);
		m_pTextEntry->m_bAutoCompleting = false;
	}
	else
	{
		m_iNextCompletion = 1;
		FillCompletionMenu();
	}
	RequestFocus();
	m_pTextEntry->RequestFocus();
}

void CConfigEditorDialog::OnInputPrompt(KeyValues* kv)
{
	const char* filename = kv->GetString("text");
	int selecteditem = m_pFileList->GetFirstSelectedItem();
	const char* foldername = m_pFileList->GetItemData(selecteditem != -1 ? selecteditem : m_pFileList->GetRootItemIndex())->GetString("PathWithoutName");
	char fullname[MAX_PATH];
	strcpy(fullname, foldername);
	strcat(fullname, filename);
	strcat(fullname, ".cfg");
	if (kv->GetFirstTrueSubKey()->GetBool("IsSaveAs"))
	{//Pressed save as
		FileHandle_t f = g_pFullFileSystem->Open(fullname, "w");
		if (f)
		{
			int c = m_pTextEntry->GetTextLength();
			m_sFileBuffer.RemoveAll();
			for (int i = 0; i < c; i++)
			{
				m_sFileBuffer.AddToTail((char)m_pTextEntry->m_TextStream[i]);
			}
			m_sFileBuffer.AddToTail(0);
			g_pFullFileSystem->Write(m_sFileBuffer.Base(), c, f);
			g_pFullFileSystem->Flush(f);
			g_pFullFileSystem->Close(f);
			RepopulateFileList();
		}
	}
	else
	{//Pressed new
		FileHandle_t f = g_pFullFileSystem->Open(fullname, "w");
		if (f)
		{
			g_pFullFileSystem->Flush(f);
			g_pFullFileSystem->Close(f);
			strcpy(m_sCurrentFile, fullname);
			m_pTextEntry->SetText("");
			RepopulateFileList();
		}
	}
	strcpy(m_sCurrentFile, fullname);
	SetTitle(filename, true);
	m_pDialog->MarkForDeletion();
	m_pDialog = NULL;
}

void CConfigEditorDialog::PerformLayout()
{
	BaseClass::PerformLayout();
	int wide, tall;
	GetSize(wide, tall);
	wide -= 256;
	m_pTextEntry->SetBounds(16, 64, wide - 32, tall - 48 - 32);
	m_pNewButton->SetBounds(16, 32, 64, 24);
	m_pSaveButton->SetBounds(16+64+16, 32, 64, 24);
	m_pSaveAsButton->SetBounds(16+64+16+64+16, 32, 64, 24);
	m_pExecuteButton->SetBounds(16+64+16+64+16+64+16, 32, 64, 24);
	m_pFileList->SetBounds(wide, 32, 256 - 16, tall - 48);
}

void CConfigEditorDialog::OnFileSelected(int itemindex)
{
	KeyValues* kv = m_pFileList->GetItemData(itemindex);
	if (kv->GetBool("Folder"))
	{
		return;
	}
	if (m_sCurrentFile[0])
	{
		FileHandle_t f = g_pFullFileSystem->Open(m_sCurrentFile, "wt");
		if (f)
		{
			int c = m_pTextEntry->GetTextLength();
			m_sFileBuffer.RemoveAll();
			for (int i = 0; i < c; i++)
			{
				m_sFileBuffer.AddToTail((char)m_pTextEntry->m_TextStream[i]);
			}
			m_sFileBuffer.AddToTail(0);
			g_pFullFileSystem->Write(m_sFileBuffer.Base(), c, f);
			g_pFullFileSystem->Flush(f);
			g_pFullFileSystem->Close(f);
		}
	}
	
	const char* filename = kv->GetString("Path");
	FileHandle_t f = g_pFullFileSystem->Open(filename,"rt");
	if (!f)
	{
		return;
	}

	m_sFileBuffer.RemoveAll();
	int size = g_pFullFileSystem->Size(f);
	if (size > 0)
	{
		m_sFileBuffer.SetSize(size);
		CUtlBuffer buffer = CUtlBuffer(m_sFileBuffer.Base(), size);
		g_pFullFileSystem->ReadToBuffer(f, buffer);
		m_sFileBuffer.AddToTail(0);
		m_pTextEntry->SetText(m_sFileBuffer.Base());
		g_pFullFileSystem->Close(f);
	}
	else
	{
		m_pTextEntry->SetText("");
	}
	strcpy(m_sCurrentFile, filename);
	SetTitle(kv->GetString("Text"), true);
}

void CConfigEditorDialog::OnCommand( const char *command )
{
	if (strcmp(command, "new") == 0 && !m_pDialog)
	{
		m_pDialog = new InputDialog(this, "Config editor", "Input the name of the new file (without extension)");
		KeyValues* kv = new KeyValues("dialog");
		kv->SetBool("IsSaveAs", false);
		m_pDialog->DoModal(kv);
		m_pDialog->AddActionSignalTarget(this);
	}
	else if (strcmp(command, "saveas") == 0 && !m_pDialog)
	{
		m_pDialog = new InputDialog(this, "Config editor", "Input the new file name (without extension)");
		KeyValues* kv = new KeyValues("dialog");
		kv->SetBool("IsSaveAs", true);
		m_pDialog->DoModal(kv);
		m_pDialog->AddActionSignalTarget(this);
	}
	else if (strcmp(command, "execute") == 0 && !m_pDialog)
	{	
		char command[COMMAND_MAX_LENGTH];
		strcpy(command, "exec \"");
		strcat(command, m_sCurrentFile + 4);
		command[strlen(command) - 4] = '\x00';
		strcat(command, "\"");
		Cbuf_AddText(command);
		//Msg("%s\n",command);
	}
	else if (strcmp(command, "save") == 0 && !m_pDialog)
	{
		if (m_sCurrentFile[0])
		{
			FileHandle_t f = g_pFullFileSystem->Open(m_sCurrentFile, "wt");
			if (f)
			{
				int c = m_pTextEntry->GetTextLength();
				m_sFileBuffer.RemoveAll();
				for (int i = 0; i < c; i++)
				{
					m_sFileBuffer.AddToTail((char)m_pTextEntry->m_TextStream[i]);
				}
				m_sFileBuffer.AddToTail(0);
				g_pFullFileSystem->Write(m_sFileBuffer.Base(), c, f);
				g_pFullFileSystem->Flush(f);
				g_pFullFileSystem->Close(f);
			}
		}
	}
	else
	{
		BaseClass::OnCommand(command);
	}
}

CConfigEditorDialog* g_ConfigEditorDialog = NULL;



void CConfigEditorDialog::InstallConfigEditor(vgui::Panel* parent)
{
	if (g_ConfigEditorDialog)
		return;	// UI already created

	g_ConfigEditorDialog = new CConfigEditorDialog(parent);
	Assert(g_ConfigEditorDialog);
}

void ShowConfigEditor(const CCommand& args)
{
	if (!g_ConfigEditorDialog) {
		return;
	}
	g_ConfigEditorDialog->SetVisible(!g_ConfigEditorDialog->IsVisible());
}

ConCommand show_configeditor("toggle_configeditor", ShowConfigEditor, "Toggle the config editor");