#include "cbase.h"
#include "toolgun_menu.h"
//#include "sandbox.h"
using namespace vgui;
#include <vgui/IVGui.h>
#include <vgui_controls/Frame.h>
#include <vgui_controls/Button.h>
#include <vgui_controls/TextEntry.h>

char szColorCommand[64];
char szDurationCommand[64];

class CToolMenu : public vgui::Frame
{
	DECLARE_CLASS_SIMPLE(CToolMenu, vgui::Frame);

	CToolMenu(vgui::VPANEL parent);
	~CToolMenu(){};
protected:
	virtual void OnTick();
	virtual void OnCommand(const char* pcCommand);
private:
	vgui::TextEntry* m_Red;
	vgui::TextEntry* m_Green;
	vgui::TextEntry* m_Blue;
	vgui::TextEntry* m_Modelscale;
};

CToolMenu::CToolMenu(vgui::VPANEL parent)
	: BaseClass(NULL, "ToolMenu")
{
	SetParent(parent);

	SetKeyBoardInputEnabled(true);
	SetMouseInputEnabled(true);
	SetSize(800, 600);
	SetPos(50, 34);
	SetTitle("SMenu", this);

	//SetProportional(true);

	SetTitleBarVisible(true);
	SetMinimizeButtonVisible(false);
	SetMaximizeButtonVisible(false);
	SetCloseButtonVisible(false);
	SetSizeable(false);
	SetMoveable(false);
	SetVisible(true);

	SetScheme(vgui::scheme()->LoadSchemeFromFile("resource/SourceScheme.res", "SourceScheme"));

	LoadControlSettings("resource/ui/toolmenu.res");

	vgui::ivgui()->AddTickSignal(GetVPanel(), 100);

	m_Modelscale = new vgui::TextEntry(this, "Duration");
	m_Modelscale->SetPos(680, 18);
	m_Modelscale->SetSize( 100, 50 );
	
	m_Red = new vgui::TextEntry(this, "Red");
	m_Red->SetPos(680, 98 );
	m_Red->SetSize( 100, 50 );
		
	m_Green = new vgui::TextEntry(this, "Green");
	m_Green->SetPos(680, 178 );
	m_Green->SetSize(100, 50 );
		
	m_Blue = new vgui::TextEntry(this, "Blue");
	m_Blue->SetPos(680, 268 );
	m_Blue->SetSize(100, 50 );
}

class CToolMenuInterface : public ToolMenu
{
private:
	CToolMenu *ToolMenu;
public:
	CToolMenuInterface()
	{
		ToolMenu = NULL;
	}
	void Create(vgui::VPANEL parent)
	{
		ToolMenu = new CToolMenu(parent);
	}
	void Destroy()
	{
		if (ToolMenu)
		{
			ToolMenu->SetParent((vgui::Panel *)NULL);
			delete ToolMenu;
		}
	}
	void Activate(void)
	{
		if (ToolMenu)
		{
			ToolMenu->Activate();
		}
	}
};
static CToolMenuInterface g_ToolMenu;
ToolMenu* toolmenu = (ToolMenu*)&g_ToolMenu;

ConVar cl_toolmenu("toolmenu", "0", FCVAR_CLIENTDLL, "Open SMenu");

void CToolMenu::OnTick()
{
	BaseClass::OnTick();
	SetVisible(cl_toolmenu.GetBool());
}

void CToolMenu::OnCommand(const char* pcCommand)
{
	BaseClass::OnCommand(pcCommand);

	if (!Q_stricmp(pcCommand, "turnoff"))	
	{
		cl_toolmenu.SetValue(0);
	}
	else if(!Q_stricmp(pcCommand, "remover"))
	{
		engine->ClientCmd("toolmode 0");
	}
	else if(!Q_stricmp(pcCommand, "setcolor"))
	{
		engine->ClientCmd("toolmode 1");
	}
	else if(!Q_stricmp(pcCommand, "modelscale"))
	{
		engine->ClientCmd("toolmode 2");
	}
	else if(!Q_stricmp(pcCommand, "igniter"))
	{
		engine->ClientCmd("toolmode 3");
	}
	else if(!Q_stricmp(pcCommand, "apply"))
	{
		char red[256];
		m_Red->GetText( red, sizeof(red));

		char green[256];
		m_Green->GetText( green, sizeof(green) );

		char blue[256];
		m_Blue->GetText( blue, sizeof(blue) );
		
		char duration[256];
		m_Modelscale->GetText(duration, sizeof(duration));
		
		Q_snprintf(szColorCommand, sizeof( szColorCommand ), "red %s\n;green %s\n;blue %s\n", red, green, blue );
		engine->ClientCmd(szColorCommand);

		Q_snprintf(szDurationCommand, sizeof( szDurationCommand ), "duration %s\n", duration );
		engine->ClientCmd( szDurationCommand );
	}
}