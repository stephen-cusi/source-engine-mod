//========= Copyright Valve Corporation, All rights reserved. ============//
//
// Purpose: MFC-free notification bridge between the hammer2/core data model
//			and a document/view layer (originally CMapDoc). The editor (or the
//			future Qt front end) implements this interface and registers it on
//			the model objects it owns.
//
// $NoKeywords: $
//
//=============================================================================//

#ifndef HC_IDOCNOTIFY_H
#define HC_IDOCNOTIFY_H

#include "mapatom.h"	// Notify_Dependent_t

class CMapClass;
class CMapWorld;
class CVisGroup;

//
// View update flags. Bit values mirror the MAPVIEW_UPDATE_* flags declared in
// the editor's mapdoc.h so adapters can pass them straight through.
//
enum hc_UpdateAllViewsFlags_t
{
	HC_MAPVIEW_UPDATE_OBJECTS		= 0x001,	// a world object has changed (pos, size etc)
	HC_MAPVIEW_UPDATE_ANIMATION		= 0x002,	// an object animation has been changed
	HC_MAPVIEW_UPDATE_COLOR			= 0x004,	// an object color has been changed
	HC_MAPVIEW_UPDATE_SELECTION		= 0x008,	// the current selection has changed
	HC_MAPVIEW_UPDATE_TOOL			= 0x010,	// the current tool has been changed
	HC_MAPVIEW_UPDATE_ONLY_2D		= 0x020,	// update only 2D views for some reason
	HC_MAPVIEW_UPDATE_ONLY_3D		= 0x040,	// update only 3D views for some reason
	HC_MAPVIEW_UPDATE_ONLY_LOGICAL	= 0x080,	// update only Logical views for some reason
	HC_MAPVIEW_UPDATE_VISGROUP_STATE= 0x200,	// a visgroup was hidden or shown.
	HC_MAPVIEW_UPDATE_VISGROUP_ALL	= 0x400,	// a visgroup was created or deleted.
};


//-----------------------------------------------------------------------------
// Purpose: The subset of CMapDoc that the data model classes depend on.
//			Method names mirror their CMapDoc counterparts.
//-----------------------------------------------------------------------------
class IDocNotify
{
public:

	virtual ~IDocNotify() {}

	// Mirrors CMapDoc::UpdateAllViews.
	virtual void UpdateAllViews(unsigned int nFlags, CMapClass *pObject1 = NULL, CMapClass *pObject2 = NULL) = 0;

	// Mirrors CMapDoc::NotifyDependents.
	virtual void NotifyDependents(CMapClass *pObject, Notify_Dependent_t eNotifyType) = 0;

	// Mirrors CMapDoc::GetNextMapObjectID.
	virtual int GetNextMapObjectID(void) = 0;

	// Mirrors CMapDoc::IsLoading.
	virtual bool IsLoading(void) = 0;

	// Mirrors CMapDoc::GetMapWorld.
	virtual CMapWorld *GetMapWorld(void) = 0;

	// Mirrors CMapDoc::VisGroups_ObjectCanBelongToVisGroup.
	virtual bool VisGroups_ObjectCanBelongToVisGroup(CMapClass *pObject) = 0;

	// Mirrors CMapDoc::VisGroups_AddGroup.
	virtual void VisGroups_AddGroup(CVisGroup *pVisGroup) = 0;
};


//-----------------------------------------------------------------------------
// Module-level equivalent of CMapDoc::GetActiveMapDoc() for code paths that
// have no object to consult (e.g. CMapClass::UpdateAllDependencies). Returns
// NULL when no document is active, exactly like GetActiveMapDoc().
//-----------------------------------------------------------------------------
IDocNotify *hc_GetActiveDocNotify(void);
void hc_SetActiveDocNotify(IDocNotify *pNotify);

#endif // HC_IDOCNOTIFY_H
