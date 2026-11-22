#pragma once

#include "mapmodel.h"

#include <QSet>
#include <cmath>
#include <limits>

namespace HammerCordon
{
// Leave room for the sealing walls within Source's map coordinate limits.
constexpr double WorldLimit = 16376;
constexpr double EditLimit = WorldLimit - 1;

struct Bounds
{
	double mins[3] = { -1024, -1024, -1024 };
	double maxs[3] = { 1024, 1024, 1024 };
};

inline bool Valid( const Bounds &bounds )
{
	for ( int axis = 0; axis < 3; ++axis )
		if ( !std::isfinite( bounds.mins[axis] ) || !std::isfinite( bounds.maxs[axis] ) ||
			bounds.mins[axis] < -EditLimit || bounds.maxs[axis] > EditLimit ||
			bounds.maxs[axis] - bounds.mins[axis] < 1 ) return false;
	return true;
}

inline bool HasOtherCordonFormat( const MapDocLite &map )
{
	for ( const auto &block : map.extraMetadata )
		if ( block.name.compare( QLatin1String( "cordons" ), Qt::CaseInsensitive ) == 0 ) return true;
	return false;
}

inline bool ReadBounds( const MapDocLite &map, Bounds &bounds )
{
	for ( const auto &entry : map.cordonProperties )
	{
		double *result = nullptr;
		if ( entry.first.compare( QLatin1String( "mins" ), Qt::CaseInsensitive ) == 0 ) result = bounds.mins;
		if ( entry.first.compare( QLatin1String( "maxs" ), Qt::CaseInsensitive ) == 0 ) result = bounds.maxs;
		if ( !result ) continue;
		QString value = entry.second.trimmed();
		if ( value.startsWith( QLatin1Char( '(' ) ) && value.endsWith( QLatin1Char( ')' ) ) ) value = value.mid( 1, value.size() - 2 );
		if ( !ParseVec3( value, result ) ) return false;
	}
	return Valid( bounds );
}

inline bool Intersects( const Bounds &bounds, const double mins[3], const double maxs[3] )
{
	for ( int axis = 0; axis < 3; ++axis )
		if ( maxs[axis] < bounds.mins[axis] || mins[axis] > bounds.maxs[axis] ) return false;
	return true;
}

// Prepare a separate VMF so the complete source map and editor history survive
// both successful and failed compiles. Keep intersecting brushes intact.
inline bool BuildCompileMap( const MapDocLite &source, MapDocLite &result )
{
	Bounds bounds;
	if ( HasOtherCordonFormat( source ) || !ReadBounds( source, bounds ) ||
		source.PeekNextId() > (std::numeric_limits<int>::max)() - 42 ) return false;

	MapDocLite filtered = source;
	QSet<int> brushOwners, retainedOwners;
	filtered.solids.clear();
	for ( const SolidLite &solid : source.solids )
	{
		if ( solid.ownerEntityId ) brushOwners.insert( solid.ownerEntityId );
		if ( !Intersects( bounds, solid.mins, solid.maxs ) ) continue;
		filtered.solids.append( solid );
		if ( solid.ownerEntityId ) retainedOwners.insert( solid.ownerEntityId );
	}
	filtered.entities.clear();
	for ( const EntityLite &entity : source.entities )
		if ( brushOwners.contains( entity.id ) ? retainedOwners.contains( entity.id ) :
			Intersects( bounds, entity.origin, entity.origin ) ) filtered.entities.append( entity );

	// Build nonoverlapping slabs, just as subtracting the inner box from the
	// outer box would. Use the Source black tool material for the seal:
	// an all-NODRAW test map compiles to zero renderable leaves in GMod.
	const QString seal = QStringLiteral( "tools/toolsblack" );
	double mins[3] = { -WorldLimit, -WorldLimit, -WorldLimit };
	double maxs[3] = { WorldLimit, WorldLimit, WorldLimit };
	for ( int axis = 0; axis < 3; ++axis )
	{
		const double outerMax = maxs[axis];
		maxs[axis] = bounds.mins[axis];
		if ( CreateBoxSolid( filtered, mins, maxs, seal ) < 0 ) return false;
		mins[axis] = bounds.maxs[axis];
		maxs[axis] = outerMax;
		if ( CreateBoxSolid( filtered, mins, maxs, seal ) < 0 ) return false;
		mins[axis] = bounds.mins[axis];
		maxs[axis] = bounds.maxs[axis];
	}
	filtered.cordonProperties.clear();
	result = filtered;
	return true;
}
}
