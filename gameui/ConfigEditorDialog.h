#ifndef CONFIGEDITORDIALOG_H
#define CONFIGEDITORDIALOG_H
#ifdef _WIN32
#pragma once
#endif

#include "vgui_controls/Frame.h"


//-----------------------------------------------------------------------------
// Purpose: Browses config files and edits them
//-----------------------------------------------------------------------------
class CConfigEditorDialog : public vgui::Frame
{
	typedef vgui::Frame BaseClass;

public:

	CConfigEditorDialog(vgui::Panel* parent);
	virtual void PerformLayout();
	static	void	InstallConfigEditor(vgui::Panel* parent);

private:
	vgui::TextEntry *m_pTextEntry;

};
extern CConfigEditorDialog* g_ConfigEditorDialog;
#endif // CONFIGEDITORDIALOG_H