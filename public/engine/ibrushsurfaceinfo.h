#ifndef IBRUSHSURFACEINFO_H
#define IBRUSHSURFACEINFO_H

#include "interface.h"

class IMaterial;
class Vector;
struct model_t;

#define HL2SB_BRUSH_SURFACE_INFO_VERSION "HL2SBBrushSurfaceInfo001"

class IHL2SBBrushSurfaceInfo : public IBaseInterface
{
public:
    virtual int GetSurfaceCount( const model_t *model ) const = 0;
    virtual int GetSurfaceIndex( const model_t *model, int index ) const = 0;
    virtual IMaterial *GetMaterial( int surface ) const = 0;
    virtual int GetVertexCount( int surface ) const = 0;
    virtual bool GetVertex( int surface, int index, Vector &position ) const = 0;
    virtual unsigned int GetFlags( int surface ) const = 0;
};

#endif
