#include <vgui_controls/PropertyPage.h>
#include <vgui_controls/PropertyDialog.h>
#include <vgui_controls/PanelListPanel.h>
using namespace vgui;

class MapListPanel : public vgui::PanelListPanel
{
	typedef vgui::PanelListPanel BaseClass;
public:
	MapListPanel( vgui::Panel *parent, const char *pName );
	virtual void OnTick( void );
	virtual void PerformLayout();
	virtual void AddButton( MapListPanel *panel, const char *image, const char *command );
	virtual void LoadMaps( MapListPanel *panel );
	virtual void OnCommand( const char *command );
private:
	CUtlVector< vgui::Panel * >		layoutItems;
};

class MapList : public vgui::PropertyDialog
{
	typedef vgui::PropertyDialog BaseClass;
public:

	MapList( vgui::VPANEL *parent, const char *pName );
	void OnTick();
};

class CMapList
{
public:
	virtual void		Create( vgui::VPANEL parent) = 0;
	virtual void		Destroy( void ) = 0;
	virtual void		Activate( void ) = 0;
};

extern CMapList* maplist;