#include "ModelBrowser.h"
#include "vgui_controls/Button.h"
#include "vgui_controls/TextEntry.h"
#include "vgui/IVGui.h"
#include "filesystem.h"
#include "server.h"
#include "client.h"
#include "ModelInfo.h"
using namespace vgui;
ConVar sb_modelbrowser_button_width("sb_modelbrowser_button_width", "150",0,"",true,30.0,true,400.0);
ConVar sb_modelbrowser_button_height("sb_modelbrowser_button_height", "24", 0, "", true, 12.0, true, 100.0);

class CModelButton : public Button
{
	DECLARE_CLASS_SIMPLE(CModelButton, Button);
	CModelButton(Panel* parent, const char* panelName, const char* text);

public:
	virtual void DoClick();
};

CModelButton::CModelButton(Panel* parent, const char* panelName, const char* text) : Button(parent, panelName, text)
{

}

void CModelButton::DoClick() {
	char modelname[512];
	GetText(modelname, 512);
	char cmd[512] = { 0 };
	strcat(cmd, "sb_modelspawn ");
	strcat(cmd, modelname);
	Cbuf_AddText(cmd);
	/*if (sv.PrecacheModel(modelname, 0) != -1) {
		int modelindex = modelinfo->GetModelIndex(modelname);
		vcollide_t* collide = modelinfo->GetVCollide(modelindex);
		if (collide) {
			Msg("%u\n", collide->solidCount);
		}
	}*/
	BaseClass::DoClick();
	
}


CModelBrowser::CModelBrowser(Panel* parent) : BaseClass(parent, "CModelBrowser")
{
	SetVisible(false);
	SetSizeable(true);
	m_pSearchBox = new TextEntry(this, "ModelSearchBox");
	m_pSearchBox->SendNewLine(true);
	m_pSearchBox->AddActionSignalTarget(this);
	m_pRightButton = new Button(this, "ModelRightButton", ">", this, "right");
	m_pRightButton->SetSize(24, 24);
	m_pLeftButton = new Button(this, "ModelLeftButton", "<", this, "left");
	m_pLeftButton->SetSize(24, 24);
	m_pageNumber = 0;
	SetBounds(0, 0, 560, 340);
}

void CModelBrowser::PerformLayout()
{
	BaseClass::PerformLayout();
	int wide, tall;
	GetSize(wide, tall);
	tall -= 64+16;
	wide -= 32;
	int buttonwidth = sb_modelbrowser_button_width.GetInt();
	int buttonheight = sb_modelbrowser_button_height.GetInt();
	int i = 0;
	for (int y = 0; (y + buttonheight) <= tall; y += buttonheight) {
		for (int x = 0; (x + buttonwidth) <= wide; x += buttonwidth) {
			if (i >= ARRAYSIZE(m_pButtons) || !m_pButtons[i]) {
				goto too_many;
			}
			m_pButtons[i]->SetPos(x+16, y+64);
			i++;
		}
	}
too_many:
	m_pSearchBox->SetPos(16+24+8+24+8,32);
	m_pSearchBox->SetSize(wide-64, 24);
	m_pLeftButton->SetPos(16, 32);
	m_pRightButton->SetPos(16+24+8, 32);
	return;
}

void CModelBrowser::OnSizeChanged(int nwide, int ntall) {
	int wide, tall;
	GetSize(wide, tall);
	tall -= 64 + 16;
	wide -= 32;
	int buttonwidth = sb_modelbrowser_button_width.GetInt();
	int buttonheight = sb_modelbrowser_button_height.GetInt();
	int i = 0;
	bool havetoupdate = false;
	for (int y = 0;( y + buttonheight ) <= tall; y += buttonheight) {
		for (int x = 0;( x + buttonwidth ) <= wide; x += buttonwidth) {
			if (i >= ARRAYSIZE(m_pButtons)) {
				goto too_many;
			}
			if (!m_pButtons[i]) {
				char cmd[16];
				m_pButtons[i] = new CModelButton(this, "ModelBrowserButton", "");
				m_pButtons[i]->SetKeyBoardInputEnabled(false);
				havetoupdate = true;
			}
			m_pButtons[i]->SetSize(buttonwidth, buttonheight);
			m_pButtons[i]->SetVisible(true);
			i++;
		}
	}
	for (; i < ARRAYSIZE(m_pButtons); i++) {
		if (m_pButtons[i]) {
			m_pButtons[i]->SetVisible(false);
		}
	}
too_many:
	if (havetoupdate) {
		UpdateButtons();
	}
	BaseClass::OnSizeChanged(wide, tall);
}
void CModelBrowser::OnSetText(const wchar_t *text)
{
	m_pSearchBox->RequestFocus();
}
void CModelBrowser::UpdatePaths() 
{
	char modelname[512];
	m_pSearchBox->GetText(modelname, 512);
	int i = 0;
	modelpaths.RemoveAll();
	CUtlStringList paths;
	g_pFullFileSystem->SearchEntireFilesystem("models/", modelname, paths);
	int c = paths.Count();
	int button = 0;
	int maxbuttons = ARRAYSIZE(m_pButtons);
	while (i < c) {
		char* path = paths[i];
		int len = strlen(path);
		if (path[len - 4] == '.' && path[len - 3] == 'm' && path[len - 2] == 'd' && path[len - 1] == 'l')
		{
			//m_pButtons[button]->SetText(path);
			modelpaths.CopyAndAddToTail(path);
			//button++;
		}
		i++;
		//else
		//{
			//m_pButtons[button]->SetText("");
			//button++;
		//}
	}
	m_pageNumber = 0;
	UpdateButtons();
}
void CModelBrowser::OnTextNewLine(KeyValues* data)
{
	UpdatePaths();
}

void CModelBrowser::OnCommand(const char* command) 
{
	if (strcmp(command, "right") == 0)
	{
		int wide, tall;
		GetSize(wide, tall);
		tall -= 64 + 16;
		wide -= 32;
		int buttonwidth = sb_modelbrowser_button_width.GetInt();
		int buttonheight = sb_modelbrowser_button_height.GetInt();
		m_pageNumber = min(m_pageNumber+1,modelpaths.Count()/((wide/buttonwidth)*(tall/buttonheight)));
		UpdateButtons();
	}
	else if (strcmp(command, "left") == 0)
	{
		if (m_pageNumber >= 0)
		{
			m_pageNumber--;
		}
	}
	else
	{
		BaseClass::OnCommand(command);
	}
}

void CModelBrowser::UpdateButtons()
{
	int wide, tall;
	GetSize(wide, tall);
	tall -= 64 + 16;
	wide -= 32;
	int buttonwidth = sb_modelbrowser_button_width.GetInt();
	int buttonheight = sb_modelbrowser_button_height.GetInt();

	int c = modelpaths.Count();
	int pagen = ((wide / buttonwidth) * (tall / buttonheight));
	m_pageNumber = min(m_pageNumber, c / pagen);
	int i = m_pageNumber * pagen;
	int button = 0;
	int maxbuttons = ARRAYSIZE(m_pButtons);
	while (button < maxbuttons && m_pButtons[button])
	{
		if (i < c)
		{
			m_pButtons[button]->SetText(modelpaths[i]);
			i++;
		}
		else
		{
			m_pButtons[button]->SetText("");
		}
		button++;
	}
}
/*
void CModelBrowser::IterateFolders(const char* startpath, const char *searchstr, wchar_t *ufilename, int &i) 
{
	FileFindHandle_t filefind;
	char starpath[MAX_FILEPATH];
	Q_snprintf(starpath, MAX_FILEPATH, "%s*", startpath);
	const char* filename = g_pFullFileSystem->FindFirst(starpath, &filefind);
	char innerpath[MAX_FILEPATH];
	while (i < ARRAYSIZE(m_pButtons) && m_pButtons[i] && filename)
	{
		Q_snprintf(innerpath, MAX_FILEPATH, "%s%s/", startpath, filename);
		if (g_pFullFileSystem->IsDirectory(innerpath) && filename[0] != '/' && filename[0] != '.')
		{
			IterateFolders(innerpath, searchstr, ufilename, i);
		}
		else if (Q_strstr(innerpath, searchstr))
		{
			mbstowcs(ufilename, filename, 512);
			m_pButtons[i]->SetText(ufilename);
			i++;
		}
		filename = g_pFullFileSystem->FindNext(filefind);
	}
	g_pFullFileSystem->FindClose(filefind);
}
*/
CModelBrowser* g_ModelBrowser = NULL;



void CModelBrowser::InstallModelBrowser(Panel* parent)
{
	if (g_ModelBrowser)
		return;	// UI already created

	g_ModelBrowser = new CModelBrowser(parent);
	Assert(g_ModelBrowser);
}



void ShowModelBrowser(const CCommand& args)
{
	if (!g_ModelBrowser) {
		return;
	}
	g_ModelBrowser->SetVisible(!g_ModelBrowser->IsVisible());
}

ConCommand toggle_modelbrowser("toggle_modelbrowser", ShowModelBrowser, "Toggle the model browser");