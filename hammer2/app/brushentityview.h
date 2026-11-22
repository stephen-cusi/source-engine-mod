#pragma once

#include "mapmodel.h"
#include "viewfilter.h"

// Brush entities are selected through their owning entity rather than their
// individual solids. These helpers keep picking and rendering consistent.
namespace HammerBrushEntityView
{
inline bool HasBrushes( const MapDocLite &map, int entityId )
{
	for ( const SolidLite &solid : map.solids )
		if ( solid.ownerEntityId == entityId ) return true;
	return false;
}
inline int SelectionId( const SolidLite &solid )
{
	return solid.ownerEntityId > 0 ? solid.ownerEntityId : solid.id;
}
inline bool Visible( const MapDocLite &map, const ViewFilter &filter, const SolidLite &solid )
{
	if ( !filter.SolidVisible( solid ) ) return false;
	if ( solid.ownerEntityId <= 0 ) return true;
	for ( const EntityLite &entity : map.entities )
		if ( entity.id == solid.ownerEntityId ) return filter.EntityVisible( entity );
	return false;
}
}
