#ifndef MODELBROWSER_H
#define MODELBROWSER_H
#ifdef _WIN32
#pragma once
#endif

#include "vgui_controls/Frame.h"


//-----------------------------------------------------------------------------
// Purpose: Browses config files and edits them
//-----------------------------------------------------------------------------
class CModelBrowser : public vgui::Frame
{
	DECLARE_CLASS_SIMPLE(CModelBrowser, vgui::Frame);

public:

	CModelBrowser(vgui::Panel* parent);
	virtual void PerformLayout();
	virtual void OnSizeChanged(int wide, int tall);
	static	void	InstallModelBrowser(vgui::Panel* parent);

private:
	//void IterateFolders(const char* startpath, const char* searchstr, wchar_t* ufilename, int &i);
	MESSAGE_FUNC_PARAMS(OnTextNewLine, "TextNewLine", data);

	vgui::Button* m_pButtons[512] = {};
	vgui::TextEntry* m_pSearchBox;
};
extern CModelBrowser* g_ModelBrowser;
#endif // CONFIGEDITORDIALOG_H