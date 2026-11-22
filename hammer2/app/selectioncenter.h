#pragma once

#include "document.h"
#include "corebounds.h"

// Use the center of the complete selection's bounds, as Hammer does.
inline bool SelectionCenter( const CHammerDocument &document, double center[3] )
{
	double mins[3] = {}, maxs[3] = {};
	bool found = false;
	const auto add = [&]( const double lo[3], const double hi[3] ) {
		for ( int axis = 0; axis < 3; ++axis )
		{
			if ( !found ) { mins[axis] = lo[axis]; maxs[axis] = hi[axis]; }
			else { mins[axis] = qMin( mins[axis], lo[axis] ); maxs[axis] = qMax( maxs[axis], hi[axis] ); }
		}
		found = true;
	};
	for ( const EntityLite &entity : document.Map().entities )
		if ( document.IsSelected( entity.id ) )
		{
			bool ownsBrush = false;
			for ( const SolidLite &solid : document.Map().solids ) if ( solid.ownerEntityId == entity.id ) { ownsBrush = true; break; }
			if ( !ownsBrush ) add( entity.origin, entity.origin );
		}
	for ( const SolidLite &solid : document.Map().solids )
		if ( document.IsSelected( solid.id ) || ( solid.ownerEntityId && document.IsSelected( solid.ownerEntityId ) ) ) add( solid.mins, solid.maxs );
	if ( found && !TryHammerCoreBoundsCenter( mins, maxs, center ) )
		for ( int axis = 0; axis < 3; ++axis ) center[axis] = ( mins[axis] + maxs[axis] ) * 0.5;
	return found;
}
