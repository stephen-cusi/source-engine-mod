#ifndef IBRUSHMATERIALOVERRIDE_H
#define IBRUSHMATERIALOVERRIDE_H

#include "interface.h"

class IMaterial;
#define HL2SB_BRUSH_MATERIAL_OVERRIDE_VERSION "HL2SBBrushMaterialOverride001"

class IHL2SBBrushMaterialOverride : public IBaseInterface
{
public:
    virtual void SetBrushMaterialOverride( IMaterial *material ) = 0;
};

#endif
