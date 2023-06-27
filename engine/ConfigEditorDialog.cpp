#include "ConfigEditorDialog.h"
#include "vgui_controls/TextEntry.h"
#include "vgui_controls/Button.h"
#include "vgui_controls/InputDialog.h"
#include "vgui_controls/TreeView.h"
#include "vgui/IVGui.h"
#include <KeyValues.h>
#include "filesystem.h"
#include "common.h"
#include "cmd.h"
#include "convar.h"

using namespace vgui;



CConfigEditorDialog::CConfigEditorDialog(vgui::Panel* parent) : BaseClass(parent, "CConfigEditorDialog")
{
	SetBounds(0, 0, 512 + 64 + 16, 340);
	SetMinimumSize(512 + 64 + 16, 100);
	SetSizeable(true);
	SetVisible(false);
	m_pTextEntry = new TextEntry(this,"CodeInput");
	m_pTextEntry->SetMultiline(true);
	m_pTextEntry->SetCatchEnterKey(true);
	m_pTextEntry->SetVerticalScrollbar(true);
	m_pTextEntry->SetHorizontalScrolling(true);
	m_pNewButton = new Button(this, "ConfigNewButton", "New", this, "new");
	m_pSaveButton = new Button(this, "ConfigSaveButton", "Save", this, "save");
	m_pSaveAsButton = new Button(this, "ConfigSaveAsButton", "Save As", this, "saveas");
	m_pExecuteButton = new Button(this, "ConfigExecuteButton", "Execute", this, "execute");
	m_pFileList = new TreeView(this, "ConfigFileList");
	RepopulateFileList();
	SetTitle("Untitled",true);
	m_pDialog = NULL;
	m_sFileBuffer = CUtlVector<char>(0,1);
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
		strcpy(command, "exec ");
		strcat(command, m_sCurrentFile + 4);
		command[strlen(command) - 4] = '\x00';
		Cbuf_AddText(command);
		Msg("%s\n",command);
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