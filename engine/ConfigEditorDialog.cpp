#include "ConfigEditorDialog.h"
#include "vgui_controls/TextEntry.h"
#include "vgui/IVGui.h"

using namespace vgui;

CConfigEditorDialog::CConfigEditorDialog(vgui::Panel* parent) : BaseClass(parent, "CConfigEditorDialog")
{
	SetBounds(0, 0, 560, 340);
	SetSizeable(true);
	SetVisible(false);
	m_pTextEntry = new TextEntry(this,"CodeInput");
	m_pTextEntry->SetMultiline(true);
	m_pTextEntry->SetCatchEnterKey(true);
	m_pTextEntry->SetVerticalScrollbar(true);
	m_pTextEntry->SetHorizontalScrolling(true);
}

void CConfigEditorDialog::PerformLayout()
{
	BaseClass::PerformLayout();
	int wide, tall;
	GetSize(wide, tall);
	m_pTextEntry->SetBounds(16, 32, wide - 32, tall - 48);
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