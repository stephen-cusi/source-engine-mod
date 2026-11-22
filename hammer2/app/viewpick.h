#pragma once

#include "corebounds.h"
#include "document.h"
#include "viewfilter.h"
#include "brushentityview.h"
#include <QSet>

// Picking helpers shared by the 2D viewport and the self-test. Hidden objects
// are never returned.
namespace HammerViewPick
{
// Point pick in a 2D view: entities first (with a world-unit margin), then brushes.
inline int Pick2D( CHammerDocument &document, int u, int v, double wu, double wv, double margin, double defaultHalfSize )
{
	const MapDocLite &map = document.Map();
	const ViewFilter &filter = document.GetViewFilter();
	for ( int i = map.entities.count() - 1; i >= 0; --i )
	{
		const EntityLite &entity = map.entities[i];
		if ( !filter.EntityVisible( entity ) ) continue;
		if ( HammerBrushEntityView::HasBrushes( map, entity.id ) ) continue;
		double mins[3], maxs[3];
		document.Fgd().ClassBounds( entity.classname, mins, maxs );
		mins[u] = qMin( mins[u], -defaultHalfSize );
		maxs[u] = qMax( maxs[u], defaultHalfSize );
		mins[v] = qMin( mins[v], -defaultHalfSize );
		maxs[v] = qMax( maxs[v], defaultHalfSize );
		if ( wu >= entity.origin[u] + mins[u] - margin && wu <= entity.origin[u] + maxs[u] + margin &&
			wv >= entity.origin[v] + mins[v] - margin && wv <= entity.origin[v] + maxs[v] + margin )
			return entity.id;
	}
	for ( int i = map.solids.count() - 1; i >= 0; --i )
	{
		const SolidLite &solid = map.solids[i];
		if ( !HammerBrushEntityView::Visible( map, filter, solid ) ) continue;
		if ( wu >= solid.mins[u] && wu <= solid.maxs[u] && wv >= solid.mins[v] && wv <= solid.maxs[v] )
			return HammerBrushEntityView::SelectionId( solid );
	}
	return -1;
}

// Region select in a 2D view; rmin/rmax are the normalized region corners.
inline QSet<int> Region2D( CHammerDocument &document, int u, int v, const double rmin[2], const double rmax[2], bool insideOnly )
{
	QSet<int> selected;
	const ViewFilter &filter = document.GetViewFilter();
	const auto add = [&]( int id, const double mins[2], const double maxs[2] ) {
		bool matches = false;
		if ( !TryHammerCoreRegionMatch( mins, maxs, rmin, rmax, insideOnly, matches ) )
			matches = insideOnly ? ( mins[0] >= rmin[0] && maxs[0] <= rmax[0] && mins[1] >= rmin[1] && maxs[1] <= rmax[1] ) :
				( mins[0] < rmax[0] && maxs[0] > rmin[0] && mins[1] < rmax[1] && maxs[1] > rmin[1] );
		if ( matches ) selected.insert( id );
	};
	QSet<int> processedOwners;
	for ( const SolidLite &solid : document.Map().solids )
	{
		if ( !HammerBrushEntityView::Visible( document.Map(), filter, solid ) ) continue;
		double mins[2] = { solid.mins[u], solid.mins[v] }, maxs[2] = { solid.maxs[u], solid.maxs[v] };
		if ( solid.ownerEntityId > 0 )
		{
			if ( processedOwners.contains( solid.ownerEntityId ) ) continue;
			processedOwners.insert( solid.ownerEntityId );
			for ( const SolidLite &part : document.Map().solids )
				if ( part.ownerEntityId == solid.ownerEntityId && HammerBrushEntityView::Visible( document.Map(), filter, part ) )
				{
					mins[0] = qMin( mins[0], part.mins[u] ); mins[1] = qMin( mins[1], part.mins[v] );
					maxs[0] = qMax( maxs[0], part.maxs[u] ); maxs[1] = qMax( maxs[1], part.maxs[v] );
				}
		}
		add( HammerBrushEntityView::SelectionId( solid ), mins, maxs );
	}
	for ( const EntityLite &entity : document.Map().entities )
	{
		if ( !filter.EntityVisible( entity ) ) continue;
		if ( HammerBrushEntityView::HasBrushes( document.Map(), entity.id ) ) continue;
		double lo[3], hi[3];
		document.Fgd().ClassBounds( entity.classname, lo, hi );
		const double mins[2] = { entity.origin[u] + lo[u], entity.origin[v] + lo[v] };
		const double maxs[2] = { entity.origin[u] + hi[u], entity.origin[v] + hi[v] };
		add( entity.id, mins, maxs );
	}
	return selected;
}
}
