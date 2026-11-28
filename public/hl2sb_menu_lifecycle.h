#ifndef HL2SB_MENU_LIFECYCLE_H
#define HL2SB_MENU_LIFECYCLE_H

#define HL2SB_MENU_LIFECYCLE_VERSION "HL2SBMenuLifecycle001"

class IHL2SBMenuLifecycle
{
public:
	virtual void CloseTransientDialogs() = 0;
};

#endif
