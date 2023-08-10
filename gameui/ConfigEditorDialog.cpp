#include "ConfigEditorDialog.h"
#include "vgui_controls/TextEntry.h"
#include "vgui/IVGui.h"

using namespace vgui;

CConfigEditorDialog::CConfigEditorDialog(vgui::Panel* parent) : BaseClass(parent, "CConfigEditorDialog")
{
	SetDeleteSelfOnClose(true);
	SetBounds(0, 0, 560, 340);
	SetSizeable(true);
	SetVisible(false);
	m_pTextEntry = new TextEntry(this,"CodeInput");
}
CConfigEditorDialog* g_ConfigEditorDialog = NULL;

void CConfigEditorDialog::InstallConfigEditor(vgui::Panel* parent)
{
	if (g_ConfigEditorDialog)
		return;	// UI already created

	g_ConfigEditorDialog = new CConfigEditorDialog(parent);
	Assert(g_ConfigEditorDialog);
}

void ShowCodeEditor(const CCommand& args)
{
	if (!g_ConfigEditorDialog) {
		return;
	}
	g_ConfigEditorDialog->SetVisible(!g_ConfigEditorDialog->IsVisible());
}

ConCommand show_codeeditor("toggle_codeeditor", ShowCodeEditor, "Toggle the code editor");