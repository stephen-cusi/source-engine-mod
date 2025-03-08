//////////////////////////////////////////////////////////////////////////////
// Shitcoded by ItzVladik
//////////////////////////////////////////////////////////////////////////////
#include "cbase.h"
#include <vgui_controls/ComboBox.h>
#include <vgui_controls/Button.h>
#include <vgui_controls/ImagePanel.h>
#include <vgui_controls/CheckButton.h>
#include <vgui_controls/PanelListPanel.h>
#include <vgui_controls/PropertyPage.h>
#include <vgui_controls/PropertyDialog.h>
#include <vgui_controls/PropertySheet.h>
#include <vgui_controls/TextEntry.h>
#include <vgui_controls/MessageBox.h>

#include "matsys_controls/mdlpanel.h"
#include "../common/GameUI/cvarslider.h"

class ImageButton : public vgui::ImagePanel
{
	typedef vgui::ImagePanel BaseClass;

public:
	ImageButton( vgui::Panel *parent, const char *panelName, const char *normalImage, const char *mouseOverImage, const char *mouseClickImage, const char *pCmd );

	virtual void OnCursorEntered(); // When the mouse hovers over this panel, change images
	virtual void OnCursorExited(); // When the mouse leaves this panel, change back

	virtual void OnMouseReleased( vgui::MouseCode code );
	virtual void OnMousePressed( vgui::MouseCode code );

	void SetNormalImage( void );
	void SetMouseOverImage( void );
	void SetMouseClickImage( void );

private:
	vgui::IImage *i_normalImage; // The image when the mouse isn't over it, and its not being clicked
	vgui::IImage *i_mouseOverImage; // The image that appears as when the mouse is hovering over it
	vgui::IImage *i_mouseClickImage; // The image that appears while the mouse is clicking
	
	char command[260]; // The command when it is clicked on
	char m_normalImage[260];
	char m_mouseOverImage[260];
	char m_mouseClickImage[260];
	
	Panel* m_pParent;

	bool hasCommand; // If this is to act as a button
	bool hasMouseOverImage; // If this changes images when the mouse is hovering over it
	bool hasMouseClickImage; // If this changes images when the mouse is clicking it

	virtual void SetImage( vgui::IImage *image ); //Private because this really shouldnt be changed
};

class SMFrame : public vgui::Frame
{
	DECLARE_CLASS_SIMPLE(SMFrame, vgui::Frame);
public:
	SMFrame( vgui::Panel * panel, const char *panelName, const char *name, bool bCitizen);
	virtual void OnCommand( const char *cmd );
	virtual void OnTick();
	virtual void InitWeapons();
	virtual void MakeCommand();
	void SetProportionalBounds( vgui::Panel *panel, int x, int y, int w, int h );
private:
	bool bCombine;
	vgui::ComboBox *m_CitizenType;
	vgui::ComboBox *m_WeaponType;
	vgui::Button *m_Spawn;
	vgui::Label *m_WeaponLabel;
	vgui::Label *m_CitizenLabel;
};

class SMVisual : public vgui::PropertyPage
{
	DECLARE_CLASS_SIMPLE( SMVisual, vgui::PropertyPage );
public:
	SMVisual( vgui::Panel *parent, const char *panelName );

	virtual void OnTick( void );
	virtual void OnCommand( const char *cmd );
	virtual void ScreenOverlay( const char * materialname );
	virtual void Check();
private:
	vgui::CheckButton *m_Yuv;
	vgui::CheckButton *m_Hsv;
	vgui::CheckButton *m_CombineBinocle;
	vgui::CheckButton *m_Eyefx;
	vgui::CheckButton *m_Refract;
};

class SMMovement : public vgui::PropertyPage
{
	DECLARE_CLASS_SIMPLE( SMMovement, vgui::PropertyPage );
public:
	SMMovement( vgui::Panel *parent, const char *panelName );

	virtual void OnTick( void );
	virtual void OnCommand( const char *cmd );
private:
	vgui::CheckButton *m_AFH;
	vgui::CheckButton *m_OldEngine;
	vgui::TextEntry *m_MainScale;
	vgui::TextEntry *m_ForwardScale;
	vgui::TextEntry *m_SprintForwardScale;
	vgui::TextEntry *m_NoclipSpeed;
	vgui::TextEntry *m_NormSpeed;
	vgui::TextEntry *m_SprintSpeed;
	vgui::TextEntry *m_WalkSpeed;
	vgui::CheckButton *m_AutoJump;
};

class SMWeapons : public vgui::PropertyPage
{
	DECLARE_CLASS_SIMPLE( SMWeapons, vgui::PropertyPage );
public:
	SMWeapons( vgui::Panel *parent, const char *panelName );

	virtual void OnTick( void );
	virtual void OnCommand( const char *cmd );
	virtual void CheckWeapons();
private:
	vgui::CheckButton *m_AllowBetaWeapons;
	vgui::CheckButton *m_AllowHLSWeapons;
	vgui::ComboBox *m_ComboBox;
	vgui::TextEntry *m_CheckVar;
	vgui::CheckButton *m_AllowStunstick;
};

class SMToolMenu : public vgui::PropertyPage
{
	DECLARE_CLASS_SIMPLE( SMToolMenu, vgui::PropertyPage );
public:
	SMToolMenu( vgui::Panel *parent, const char *panelName );

	virtual void OnTick( void );
	virtual void OnCommand( const char *pcCommand );
	void ApplyValues();
	void SliderCheck();
private:
	vgui::Panel *pParent;
	vgui::TextEntry *m_Red; 
	vgui::TextEntry *m_Green;
	vgui::TextEntry *m_Blue; 
	vgui::TextEntry *m_Modelscale; 
	vgui::TextEntry *m_EntCreate;
	CCvarSlider *m_Timescale;
	CCvarSlider *m_PhysTimescale;
	CCvarSlider *m_Gravity;
	vgui::CheckButton *m_Noclip;
	vgui::CheckButton *m_Notarget;
	vgui::CheckButton *m_DarkTheme;
	vgui::TextEntry *m_TimescaleValue;
	vgui::TextEntry *m_GravityValue;
	vgui::TextEntry *m_PhysTimescaleValue;
	bool bCam;
protected:
	MESSAGE_FUNC_PTR( OnTextChanged, "TextChanged", panel );
	MESSAGE_FUNC_PTR( OnControlModified, "ControlModified", panel );
};

class SMModels : public vgui::PropertyPage
{
	typedef vgui::PropertyPage BaseClass;

public:
	SMModels( vgui::Panel *parent, const char *panelName );
	const char *GetText();
	virtual void InitModels( Panel *panel, const char *modeltype, const char *modelfolder, const char *mdlPath  );
	virtual void OnCommand( const char *command );

private:
	vgui::ComboBox *box;
	CMDLPanel *mdl;
	char sz_mdlname[260];
};

class SMList : public vgui::PanelListPanel
{
public:
	typedef vgui::PanelListPanel BaseClass;

	SMList( vgui::Panel *parent, const char *pName );

	virtual void OnTick( void );
	virtual void OnCommand( const char *command );
	virtual void PerformLayout();
	virtual void AddImageButton( vgui::PanelListPanel *panel, const char *image, const char *hover, const char *command );
	//virtual void AddModelPanel( vgui::PanelListPanel *panel, const char *mdlname, const char *cmd );
	virtual void InitEntities( KeyValues *kv, vgui::PanelListPanel *panel, const char *enttype );
	//virtual void InitModels( vgui::PanelListPanel *panel, const char *modeltype, const char *modelfolder, const char *mdlPath );
	void ExtraSpawn( const char *name, bool bCitizen );
private:
	CUtlVector<vgui::Panel *> m_LayoutItems;
	CUtlVector<CMDLPanel *> m_ModelsItems;
};

class CSMenu : public vgui::PropertyDialog
{
	typedef vgui::PropertyDialog BaseClass;
public:
	CSMenu( vgui::VPANEL *parent, const char *panelName );
	void OnTick();
	void OnCommand( const char *command ); 
};

class SMPanel
{
public:
	virtual void		Create(vgui::VPANEL parent) = 0;
	virtual void		Destroy(void) = 0;
	virtual void		Activate(void) = 0;
};
extern SMPanel* smenu;
