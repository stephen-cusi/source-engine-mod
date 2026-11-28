#ifndef HL2SB_VGUI_PAINT_CALLBACK_H
#define HL2SB_VGUI_PAINT_CALLBACK_H

#include "tier0/basetypes.h"

#define HL2SB_VGUI_PAINT_CALLBACK_VERSION "HL2SBVguiPaintCallback001"

// Lets the game DLL finish a UI-only frame (main menu, no level).  The engine
// paints the root panel tree and then every popup; a menu or overlay that has
// to sit above those popups can only be painted once both are done, and the
// game DLL owns that list, so the engine asks for it here.
abstract_class IHL2SBVguiPaintCallback
{
public:
	virtual void PostUIPaint() = 0;
};

#endif
