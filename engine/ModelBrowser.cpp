#include "ModelBrowser.h"
#include "vgui_controls/Button.h"
#include "vgui_controls/TextEntry.h"
#include "vgui/IVGui.h"
#include "filesystem.h"

using namespace vgui;
ConVar sb_modelbrowser_button_width("sb_modelbrowser_button_width", "150",0,"",true,30.0,true,400.0);
ConVar sb_modelbrowser_button_height("sb_modelbrowser_button_height", "24", 0, "", true, 12.0, true, 100.0);

CModelBrowser::CModelBrowser(Panel* parent) : BaseClass(parent, "CModelBrowser")
{
	SetBounds(0, 0, 560, 340);
	SetSizeable(true);
	SetVisible(false);
	m_pSearchBox = new TextEntry(this, "ModelSearchBox");
	m_pSearchBox->SendNewLine(true);
	m_pSearchBox->AddActionSignalTarget(this);
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
	m_pSearchBox->SetPos(16,32);
	m_pSearchBox->SetSize(wide, 24);
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
	for (int y = 0;( y + buttonheight ) <= tall; y += buttonheight) {
		for (int x = 0;( x + buttonwidth ) <= wide; x += buttonwidth) {
			if (i >= ARRAYSIZE(m_pButtons)) {
				goto too_many;
			}
			if (!m_pButtons[i]) {
				m_pButtons[i] = new Button(this, "ModelBrowserButton", "");
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
	BaseClass::OnSizeChanged(wide, tall);
}

void CModelBrowser::OnTextNewLine(KeyValues* data)
{
	char modelname[512];
	m_pSearchBox->GetText(modelname, 512);
	int i = 0;
	CUtlStringList paths;
	g_pFullFileSystem->SearchEntireFilesystem("models/", modelname, paths);
	int c = paths.Count();
	int button = 0;
	int maxbuttons = ARRAYSIZE(m_pButtons);
	while (button < maxbuttons && m_pButtons[button]) {
		if (i < c)
		{
			char* path = paths[i];
			int len = strlen(path);
			if (path[len - 4] == '.' && path[len - 3] == 'm' && path[len - 2] == 'd' && path[len - 1] == 'l')
			{
				m_pButtons[button]->SetText(path);
				button++;
			}
			i++;
		}
		else
		{
			m_pButtons[button]->SetText("");
			button++;
		}
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