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
	virtual void OnSizeChanged( int wide, int tall );
	virtual void OnCommand( const char* command );
	static	void	InstallModelBrowser( vgui::Panel* parent );
	

private:
	//void IterateFolders(const char* startpath, const char* searchstr, wchar_t* ufilename, int &i);
	MESSAGE_FUNC_PARAMS(OnTextNewLine, "TextNewLine", data);
	MESSAGE_FUNC_WCHARPTR(OnSetText, "SetText", text);

	virtual void UpdateButtons();
	virtual void UpdatePaths();

	vgui::Button* m_pButtons[512] = {};
	vgui::Button* m_pRightButton;
	vgui::Button* m_pLeftButton;
	vgui::TextEntry* m_pSearchBox;

	CUtlStringList modelpaths;
	int m_pageNumber;
};
extern CModelBrowser* g_ModelBrowser;
#endif // CONFIGEDITORDIALOG_H