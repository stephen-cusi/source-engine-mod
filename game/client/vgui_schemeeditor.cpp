//========= Copyright Valve Corporation, All rights reserved. ============//
//
//=======================================================================================//

#include "cbase.h"
#include "vgui_schemeeditor.h"
#include "vgui/IBorder.h"
#include "vgui/ISurface.h"
#include "vgui/IVGui.h"
#include "vgui/IInput.h"
#include "ienginevgui.h"
#include "KeyValues.h"
#include "fmtstr.h"
#include "filesystem.h"
//----------------------------------------------------------------------------------------

using namespace vgui;


class Knob : public Panel
{
	DECLARE_CLASS_SIMPLE(Knob, Panel);
public:
	Knob(Panel* pParent, const char* pName, int min = 0, int max = 255, int value = 0, float speed = 1.0);
	virtual bool IsDragged(void) const;
	virtual void OnCursorMoved(int x, int y);
	virtual void OnMousePressed(MouseCode code);
	virtual void OnMouseReleased(MouseCode code);
	virtual void Paint();
	virtual void ApplySchemeSettings(IScheme* pScheme);
	DECLARE_GET_SET(int, _min, Min)
	DECLARE_GET_SET(int, _max, Max)
	int GetValue() { return _value; } 
	void SetValue(int val) { _value = Clamp(val,_min,_max); }

private:
	bool _dragging;
	int _min;
	int _max;
	int _value;
	int _origx;
	int _origvalue;
	float _speed;
	Color _lightcolor;
	Color _darkcolor;
};

Knob::Knob(Panel* pParent, const char* pName, int min, int max, int value, float speed) : BaseClass(pParent, pName)
{
	SetSize(12, 12);
	_dragging = false;
	_min = min;
	_max = max;
	_value = value;
	_origx = 0;
	_origvalue = value;
	_speed = speed;
}

void Knob::Paint()
{
	int wide, tall;
	GetSize(wide, tall);
	int radius = min(wide, tall)/2;
	Color col = GetBgColor();
	Color notchCol = GetFgColor();
	surface()->DrawSetColor(col);
	surface()->DrawOutlinedCircle(radius, radius, radius, 8);
	float percent = 0.0;
	surface()->DrawSetColor(_lightcolor);
	percent = -0.25 * M_PI_F;
	int borderx;
	int bordery;
	int oldborderx = radius - (int)(cosf(percent) * radius);
	int oldbordery = radius - (int)(sinf(percent) * radius);
	for (int i = 1; i < 16; i++) 
	{
		percent = ((float)i / 16.0 - 0.25) * M_PI_F;
		borderx = radius - (int)(cosf(percent) * radius);
		bordery = radius - (int)(sinf(percent) * radius);
		surface()->DrawLine(oldborderx, oldbordery, borderx, bordery);
		oldborderx = borderx;
		oldbordery = bordery;

	}
	surface()->DrawSetColor(_darkcolor);
	for (int i = 0; i < 16; i++)
	{
		percent = ((float)i / 16.0 + 0.75) * M_PI_F;
		borderx = radius - (int)(cosf(percent) * radius);
		bordery = radius - (int)(sinf(percent) * radius);
		surface()->DrawLine(oldborderx, oldbordery, borderx, bordery);
		oldborderx = borderx;
		oldbordery = bordery;
	}
	surface()->DrawSetColor(notchCol);
	percent = (((float)_value - (float)_min) / ((float)_max - (float)_min) - 0.5)*M_PI_F*1.5;
	surface()->DrawLine(radius, radius, radius+(int)(sinf(percent) * radius), radius-(int)(cosf(percent) * radius));
}

void Knob::OnMousePressed(MouseCode code)
{
	int x, y;

	if (!IsEnabled())
		return;

	//	input()->GetCursorPos(x,y);
	input()->GetCursorPosition(x, y);

	ScreenToLocal(x, y);
	RequestFocus();
	_dragging = true;
	input()->SetMouseCapture(GetVPanel());
	_origx = x;
	_origvalue = _value;
}

void Knob::OnCursorMoved(int x, int y)
{
	if (!_dragging)
	{
		return;
	}
	SetValue(_origvalue + (int)((x - _origx)*_speed));
	Repaint();
}

bool Knob::IsDragged(void) const
{
	return _dragging;
}

void Knob::OnMouseReleased(MouseCode code)
{
	if (_dragging)
	{
		_dragging = false;
		input()->SetMouseCapture(null);
	}
}

//-----------------------------------------------------------------------------
// Purpose: Apply resource file scheme.
//-----------------------------------------------------------------------------
void Knob::ApplySchemeSettings(IScheme* pScheme)
{
	SetBgColor(GetSchemeColor("Knob.BgColor", Color(150, 150, 150, 255), pScheme));
	SetFgColor(GetSchemeColor("Knob.NotchColor", Color(255, 255, 255, 255), pScheme));
	_lightcolor = GetSchemeColor("Knob.LightColor", Color(90, 90, 90, 255), pScheme);
	_darkcolor = GetSchemeColor("Knob.DarkColor", Color(20, 20, 20, 255), pScheme);
}


//----------------------------------------------------------------------------------------

class CColorChangePanel : public Panel
{
	DECLARE_CLASS_SIMPLE(CColorChangePanel, Panel);
public:
	CColorChangePanel(Panel* pParent, const char* pName, const Color& color);
	Color GetColor();
	void GetColorString(char *color);
	virtual void PerformLayout();
private:
	virtual void PaintBackground();

	Knob* m_pRedKnob;
	Knob* m_pGreenKnob;
	Knob* m_pBlueKnob;
	Knob* m_pAlphaKnob;
};

//----------------------------------------------------------------------------------------

CColorChangePanel::CColorChangePanel(Panel* pParent, const char* pName, const Color& color): Panel(pParent, pName)
{
	m_pRedKnob = new Knob(this, "RedKnob",0,255,color.r());
	m_pGreenKnob = new Knob(this, "GreenKnob", 0, 255, color.g());
	m_pBlueKnob = new Knob(this, "BlueKnob", 0, 255, color.b());
	m_pAlphaKnob = new Knob(this, "AlphaKnob", 0, 255, color.a());
}


void CColorChangePanel::PaintBackground()
{
	SetBgColor(GetColor());
	BaseClass::PaintBackground();
}

void CColorChangePanel::PerformLayout()
{
	int wide, tall;
	GetSize(wide, tall);
	m_pRedKnob->SetPos(wide / 5 - 6, 6);
	m_pGreenKnob->SetPos(2*wide / 5 - 6, 6);
	m_pBlueKnob->SetPos(3*wide / 5 - 6, 6);
	m_pAlphaKnob->SetPos(4*wide / 5 - 6, 6);
}

Color CColorChangePanel::GetColor()
{
	return Color(m_pRedKnob->GetValue(), m_pGreenKnob->GetValue(), m_pBlueKnob->GetValue(), m_pAlphaKnob->GetValue());
}

void CColorChangePanel::GetColorString(char* color)
{
	sprintf(color, "%i %i %i %i", m_pRedKnob->GetValue(), m_pGreenKnob->GetValue(), m_pBlueKnob->GetValue(), m_pAlphaKnob->GetValue());
}

void GetColorString(char* out, Color color)
{
	sprintf(out, "%i %i %i %i", color.r(), color.g(), color.b(), color.a());
}

class CBorderLineChangePanel : public Panel
{
	DECLARE_CLASS_SIMPLE(CBorderLineChangePanel, Panel);
public:
	CBorderLineChangePanel(Panel* pParent, const char* pName, const Color& color, int offsetx, int offsety);
	virtual void PerformLayout();
	CColorChangePanel* m_pColor;
	Knob* m_pOffsetX;
	Knob* m_pOffsetY;

};
CBorderLineChangePanel::CBorderLineChangePanel(Panel* pParent, const char* pName, const Color& color, int offsetx, int offsety) : Panel(pParent, pName)
{
	m_pColor = new CColorChangePanel(this, "BorderLineChangePanelColor", color);
	m_pOffsetX = new Knob(this, "BorderLineChangePanelOffsetX", 0, 5, offsetx,0.05);
	m_pOffsetY = new Knob(this, "BorderLineChangePanelOffsetY", 0, 5, offsety,0.05);
}
void CBorderLineChangePanel::PerformLayout()
{
	int wide, tall;
	GetSize(wide, tall);
	m_pColor->SetBounds(wide / 4 - 48, tall / 2 - 8, 96, 16);
	m_pOffsetX->SetBounds(3 * wide / 4 - 8, tall / 2 - 6, 12, 12);
	m_pOffsetY->SetBounds(3 * wide / 4 + 8, tall / 2 - 6, 12, 12);
}


class CBorderSideChangePanel : public Panel
{
	DECLARE_CLASS_SIMPLE(CBorderSideChangePanel, Panel);
public:
	CBorderSideChangePanel(Panel* pParent, const char* pName);
	virtual void PerformLayout();
	virtual void OnCommand(const char* cmd);
	virtual void AddLine(Color color, int offsetx, int offsety);
	CUtlVector<CBorderLineChangePanel*> m_vpBorders;

private:
	Button* m_pAddButton;
	Button* m_pRemoveButton;
};

CBorderSideChangePanel::CBorderSideChangePanel(Panel* pParent, const char* pName) : Panel(pParent, pName)
{
	m_pAddButton = new Button(this, "BorderSideChangePanelAddButton", "Add", this, "add");
	m_pRemoveButton = new Button(this, "BorderSideChangePanelRemoveButton", "Remove", this, "remove");
}

void CBorderSideChangePanel::PerformLayout()
{
	int count = m_vpBorders.Count();
	SetTall(count * 24 + 24);
	int wide = GetWide();
	for (int i = 0; i < count; i++)
	{
		m_vpBorders[i]->SetBounds(0, i * 24, wide, 24);
	}
	m_pAddButton->SetBounds(0, count * 24, wide / 2, 24);
	m_pRemoveButton->SetBounds(wide/2, count * 24, wide / 2, 24);
}

void CBorderSideChangePanel::AddLine(Color color, int offsetx, int offsety)
{
	m_vpBorders.AddToTail(new CBorderLineChangePanel(this, "BorderLineChangePanel", color, offsetx, offsety));
}

void CBorderSideChangePanel::OnCommand(const char *cmd)
{
	if (strcmp(cmd, "add") == 0)
	{
		AddLine(Color(0, 0, 0, 255), 0, 0);
		InvalidateLayout();
		GetParent()->InvalidateLayout();
	}
	else if(strcmp(cmd,"remove") == 0)
	{
		if (!m_vpBorders.IsEmpty())
		{
			m_vpBorders.Tail()->DeletePanel();
			m_vpBorders.RemoveMultipleFromTail(1);
			InvalidateLayout();
			GetParent()->InvalidateLayout();
		}
	}
	else
	{
		BaseClass::OnCommand(cmd);
	}
}

class CBorderChangePanel : public Panel
{
	DECLARE_CLASS_SIMPLE(CBorderChangePanel, Panel);
public:
	CBorderChangePanel(Panel* pParent, const char* pName, const char* label);
	virtual void PerformLayout();
	CBorderSideChangePanel* m_pLeftBorder;
	CBorderSideChangePanel* m_pTopBorder;
	CBorderSideChangePanel* m_pRightBorder;
	CBorderSideChangePanel* m_pBottomBorder;
	Label* m_pLabel;
};

CBorderChangePanel::CBorderChangePanel(Panel* pParent, const char* pName, const char* label) : Panel(pParent, pName)
{
	m_pLeftBorder = new CBorderSideChangePanel(this, "LeftBorderChange");
	m_pTopBorder = new CBorderSideChangePanel(this, "TopBorderChange");
	m_pRightBorder = new CBorderSideChangePanel(this, "RightBorderChange");
	m_pBottomBorder = new CBorderSideChangePanel(this, "BottomBorderChange");
	m_pLabel = new Label(this, "BorderChangePanelName", label);
}

void CBorderChangePanel::PerformLayout()
{
	SetTall(m_pLabel->GetTall() + m_pLeftBorder->GetTall() + m_pTopBorder->GetTall() + m_pRightBorder->GetTall() + m_pBottomBorder->GetTall());
	int wide = GetWide();
	m_pLabel->SetPos(0, 0);
	m_pLabel->SizeToContents();
	int y = m_pLabel->GetTall();
	m_pLeftBorder->SetPos(0, y);
	m_pLeftBorder->SetWide(wide);
	y += m_pLeftBorder->GetTall();
	m_pTopBorder->SetPos(0, y);
	m_pTopBorder->SetWide(wide);
	y += m_pTopBorder->GetTall();
	m_pRightBorder->SetPos(0, y);
	m_pRightBorder->SetWide(wide);
	y += m_pRightBorder->GetTall();
	m_pBottomBorder->SetPos(0, y);
	m_pBottomBorder->SetWide(wide);
}

//-----------------------------------------------------------------------------
// Purpose: recursively looks up a setting
//-----------------------------------------------------------------------------
const char* LookupSchemeSetting(const char* pchSetting, IScheme* pScheme)
{
	// try parse out the color
	int r, g, b, a = 0;
	int res = sscanf(pchSetting, "%d %d %d %d", &r, &g, &b, &a);
	if (res >= 3)
	{
		return pchSetting;
	}

	// check the color area first
	const char* colStr = ((KeyValues *)(pScheme->GetColorData()))->GetString(pchSetting, NULL);
	if (colStr)
		return colStr;

	// check base settings
	colStr = pScheme->GetBaseSettings()->GetString(pchSetting, NULL);
	if (colStr)
	{
		return LookupSchemeSetting(colStr,pScheme);
	}

	return pchSetting;
}


bool CheckColor(const char* colorName, IScheme* pScheme)
{
	const char* pchT = LookupSchemeSetting(colorName, pScheme);
	if (!pchT)
		return false;

	int r = 0, g = 0, b = 0, a = 0;
	if (sscanf(pchT, "%d %d %d %d", &r, &g, &b, &a) >= 3)
		return true;

	return false;
}

//----------------------------------------------------------------------------------------

CSchemeEditor::CSchemeEditor( vgui::Panel *pParent )
:	vgui::Frame( pParent, "SchemeEditor" )
{
	SetBounds(200, 200, 500, 400);
	m_pTabs = new PropertySheet(this, "SchemeEditorTabs", true);
	m_pTabs->AddPage(new PanelListPanel(this, "SchemeEditorFonts"), "Fonts");
	m_pTabs->AddPage(new PanelListPanel(this, "SchemeEditorColors"), "Colors");
	m_pTabs->AddPage(new PanelListPanel(this, "SchemeEditorBorders"), "Borders");
	m_pPreviewButton = new Button(this, "SchemeEditorPreviewButton", "Button");
	m_pPreviewCheckButton = new CheckButton(this, "SchemeEditorPreviewCheckButton", "");
	m_pPreviewRadioButton = new RadioButton(this, "SchemeEditorPreviewRadioButton", "");
	m_pPreviewLabel = new Label(this, "SchemeEditorPreviewLabel", "Preview");
	m_pApplyButton = new Button(this, "SchemeEditorApplyButton", "Apply", this, "apply");
	

}

void CSchemeEditor::ApplySchemeSettings(IScheme* sch)
{
	BaseClass::ApplySchemeSettings(sch);
	if (reinterpret_cast<PanelListPanel*>(m_pTabs->GetPage(1))->GetItemCount() > 0) 
	{
		return;
	}
	KeyValues* colorData = sch->GetBaseSettings();
	for (KeyValues* pKey = colorData->GetFirstValue(); pKey; pKey = pKey->GetNextValue())
	{
		if (CheckColor(pKey->GetName(), sch))
		{
			reinterpret_cast<PanelListPanel*>(m_pTabs->GetPage(1))->AddItem(
				new Label(this, "SchemeEditorColorLabel", pKey->GetName()),
				new CColorChangePanel(this, "SchemeEditorColorChange", GetSchemeColor(pKey->GetName(), Color(255, 0, 255, 255), sch))
			);
		}
	}
	KeyValues* borderData = sch->GetBorders();
	KeyValues* borderSide;
	//int i = 0;
	for (KeyValues* entry = borderData->GetFirstSubKey(); entry != NULL; entry = entry->GetNextKey())
	{
		KeyValues* kv = entry;
		const char* name = kv->GetName();
		if (kv->GetDataType() == KeyValues::TYPE_STRING)
		{
			kv = borderData->FindKey(kv->GetString(), false);
			if (kv == NULL)
			{
				continue;
			}
		}
		CBorderChangePanel* borderchangepanel = new CBorderChangePanel(this, "SchemeEditorBorderChange", name);
		reinterpret_cast<PanelListPanel*>(m_pTabs->GetPage(2))->AddItem(
			NULL,
			borderchangepanel);
		borderSide = kv->FindKey("Left");
		if (borderSide)
		{
			for (KeyValues* side = borderSide->GetFirstSubKey(); side != NULL; side = side->GetNextKey())
			{
				int offsetx, offsety;
				const char* col = side->GetString("offset", NULL);
				int x = 0, y = 0;
				if (col)
				{
					sscanf(col, "%d %d", &x, &y);
				}
				borderchangepanel->m_pLeftBorder->AddLine(GetSchemeColor(side->GetString("color"), Color(255, 0, 255, 255), sch), x, y);
			}
		}
		borderSide = kv->FindKey("Top");
		if (borderSide)
		{
			for (KeyValues* side = borderSide->GetFirstSubKey(); side != NULL; side = side->GetNextKey())
			{
				int offsetx, offsety;
				const char* col = side->GetString("offset", NULL);
				int x = 0, y = 0;
				if (col)
				{
					sscanf(col, "%d %d", &x, &y);
				}
				borderchangepanel->m_pTopBorder->AddLine(GetSchemeColor(side->GetString("color"), Color(255, 0, 255, 255), sch), x, y);
			}
		}
		borderSide = kv->FindKey("Right");
		if (borderSide)
		{
			for (KeyValues* side = borderSide->GetFirstSubKey(); side != NULL; side = side->GetNextKey())
			{
				int offsetx, offsety;
				const char* col = side->GetString("offset", NULL);
				int x = 0, y = 0;
				if (col)
				{
					sscanf(col, "%d %d", &x, &y);
				}
				borderchangepanel->m_pRightBorder->AddLine(GetSchemeColor(side->GetString("color"), Color(255, 0, 255, 255), sch), x, y);
			}
		}
		borderSide = kv->FindKey("Bottom");
		if (borderSide)
		{
			for (KeyValues* side = borderSide->GetFirstSubKey(); side != NULL; side = side->GetNextKey())
			{
				int offsetx, offsety;
				const char* col = side->GetString("offset", NULL);
				int x = 0, y = 0;
				if (col)
				{
					sscanf(col, "%d %d", &x, &y);
				}
				borderchangepanel->m_pBottomBorder->AddLine(GetSchemeColor(side->GetString("color"), Color(255, 0, 255, 255), sch), x, y);
			}
		}
		borderchangepanel->InvalidateLayout();
	}
}

void CSchemeEditor::PerformLayout()
{
	BaseClass::PerformLayout();
	int wide, tall;
	GetSize(wide, tall);
	m_pTabs->SetBounds(wide / 2, 32 + 24, wide / 2 - 16, tall - 32 - 24 - 16);
	m_pPreviewButton->SetBounds(32, 64, 64, 24);
	m_pPreviewCheckButton->SetPos(32, 64+32);
	m_pPreviewRadioButton->SetPos(32, 64+64);
	m_pPreviewLabel->SetPos(32, 32);
	m_pApplyButton->SetBounds(wide / 2, 32, 64, 24);
}

void ApplyBorderSide(KeyValues* borderside, CBorderSideChangePanel* sidepanel)
{
	borderside->Clear();
	char buffer[32];
	for (int i = 0; i < sidepanel->m_vpBorders.Count(); i++)
	{
		itoa(i + 1, buffer, 10);
		KeyValues* line = new KeyValues(buffer);
		line->Clear();
		CBorderLineChangePanel* linepanel = sidepanel->m_vpBorders[i];

		linepanel->m_pColor->GetColorString(buffer);
		line->SetString("color", buffer);

		sprintf(buffer, "%i %i", linepanel->m_pOffsetX->GetValue(), linepanel->m_pOffsetY->GetValue());
		line->SetString("offset", buffer);
		borderside->AddSubKey(line->MakeCopy());
	}
}

void CSchemeEditor::OnCommand(const char* cmd)
{
	if (strcmp(cmd, "apply") == 0)
	{
		PanelListPanel *colorlistpanel = reinterpret_cast<PanelListPanel*>(m_pTabs->GetPage(1));
		IScheme* sch = scheme()->GetIScheme(GetScheme());
		KeyValues* colorData = sch->GetBaseSettings();
		char name[256];
		char color[32];
		for (int i = 0; i < colorlistpanel->GetItemCount(); i++)
		{
			reinterpret_cast<Label*>(colorlistpanel->GetItemLabel(i))->GetText(name, 256);
			reinterpret_cast<CColorChangePanel*>(colorlistpanel->GetItemPanel(i))->GetColorString(color);
			colorData->SetString(name, color);
		}
		colorData->SaveToFile(g_pFullFileSystem, "savedThemeBaseSettings.txt");
		PanelListPanel *borderlistpanel = reinterpret_cast<PanelListPanel*>(m_pTabs->GetPage(2));
		KeyValues* borderData = sch->GetBorders();
		KeyValues* border;
		KeyValues* borderside;
		for (int i = 0; i < borderlistpanel->GetItemCount(); i++)
		{
			CBorderChangePanel* borderpanel = reinterpret_cast<CBorderChangePanel*>(borderlistpanel->GetItemPanel(i));
			char name[64];
			borderpanel->m_pLabel->GetText(name, 64);
			border = borderData->FindKey(name,false);
			if (border == NULL)
				continue;
			borderside = border->FindKey("Left", true);
			ApplyBorderSide(borderside, borderpanel->m_pLeftBorder);
			borderside = border->FindKey("Top", true);
			ApplyBorderSide(borderside, borderpanel->m_pTopBorder);
			borderside = border->FindKey("Right", true);
			ApplyBorderSide(borderside, borderpanel->m_pRightBorder);
			borderside = border->FindKey("Bottom", true);
			ApplyBorderSide(borderside, borderpanel->m_pBottomBorder);
		}
		borderData->SaveToFile(g_pFullFileSystem, "savedThemeBorders.txt");
		sch->ReloadBorders();
		//vgui::ipanel()->PerformApplySchemeSettings(vgui::surface()->GetEmbeddedPanel());
		surface()->SolveTraverse(vgui::surface()->GetEmbeddedPanel(), true);
		//vgui::ipanel()->PerformApplySchemeSettings(vgui::surface()->GetEmbeddedPanel());
	}
	else 
	{
		BaseClass::OnCommand(cmd);
	}
}

//----------------------------------------------------------------------------------------

static CSchemeEditor *g_pSchemeEditor = NULL;

//----------------------------------------------------------------------------------------

CON_COMMAND( toggle_schemeeditor, "Toggle the scheme editor" )
{
	if (g_pSchemeEditor)
	{
		g_pSchemeEditor->SetVisible(!g_pSchemeEditor->IsVisible());
	}
	else 
	{
		g_pSchemeEditor = vgui::SETUP_PANEL(new CSchemeEditor(NULL));
		g_pSchemeEditor->InvalidateLayout(false, true);

		g_pSchemeEditor->Activate();
	}
}
