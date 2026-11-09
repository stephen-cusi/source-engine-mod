#ifndef ISTUDIOMATERIALOVERRIDE_H
#define ISTUDIOMATERIALOVERRIDE_H

#include "interface.h"

class IMaterial;

#define HL2SB_STUDIO_MATERIAL_OVERRIDE_VERSION "HL2SBStudioMaterialOverride001"

class IHL2SBStudioMaterialOverride : public IBaseInterface
{
public:
    virtual void SetMaterialOverrideByIndex( int index, IMaterial *material ) = 0;
    virtual void ClearMaterialOverridesByIndex() = 0;
};

#endif
