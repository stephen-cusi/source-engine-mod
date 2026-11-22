#ifndef VGUI_IPANELFOCUSEXT_H
#define VGUI_IPANELFOCUSEXT_H

#include <vgui/IPanel.h>

namespace vgui
{
class IPanelFocusExt : public IPanel
{
public:
    virtual bool IsPopupFocusRaiseEnabled( VPANEL panel ) = 0;
    virtual void SetPopupFocusRaiseEnabled( VPANEL panel, bool enabled ) = 0;
};
}

#endif
