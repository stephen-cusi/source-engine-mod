// Exercise the original Hammer core through its public bounding-box API.
#include "hc_stdafx.h"
#include "boundbox.h"
#include "corebounds.h"
#include <cmath>
#include <limits>

bool TryHammerCoreRegionMatch( const double mins[2], const double maxs[2], const double regionMins[2], const double regionMaxs[2], bool insideOnly, bool &matches )
{
	Vector lo( 0, 0, -1 ), hi( 0, 0, 1 ), rlo( 0, 0, -2 ), rhi( 0, 0, 2 );
	for ( int i = 0; i < 2; ++i )
	{
		const double values[] = { mins[i], maxs[i], regionMins[i], regionMaxs[i] };
		float converted[4];
		for ( int j = 0; j < 4; ++j )
		{
			if ( !std::isfinite( values[j] ) || std::abs( values[j] ) > (std::numeric_limits<float>::max)() ) return false;
			converted[j] = static_cast<float>( values[j] );
			if ( static_cast<double>( converted[j] ) != values[j] ) return false;
		}
		lo[i] = converted[0]; hi[i] = converted[1]; rlo[i] = converted[2]; rhi[i] = converted[3];
	}
	const BoundBox box( lo, hi );
	matches = insideOnly ? box.IsInsideBox( rlo, rhi ) : box.IsIntersectingBox( rlo, rhi );
	return true;
}

bool TryHammerCoreBoundsCenter( const double mins[3], const double maxs[3], double center[3] )
{
	Vector lo, hi;
	for ( int axis = 0; axis < 3; ++axis )
	{
		const double limit = (std::numeric_limits<float>::max)();
		if ( !std::isfinite( mins[axis] ) || !std::isfinite( maxs[axis] ) ||
			 std::abs( mins[axis] ) > limit || std::abs( maxs[axis] ) > limit || mins[axis] > maxs[axis] )
			return false;
		lo[axis] = static_cast<float>( mins[axis] );
		hi[axis] = static_cast<float>( maxs[axis] );
		if ( static_cast<double>( lo[axis] ) != mins[axis] ||
			 static_cast<double>( hi[axis] ) != maxs[axis] )
			return false;
	}
	BoundBox bounds( lo, hi );
	Vector result;
	bounds.GetBoundsCenter( result );
	for ( int axis = 0; axis < 3; ++axis )
		if ( !std::isfinite( result[axis] ) ||
			 static_cast<double>( result[axis] ) != ( mins[axis] + maxs[axis] ) * 0.5 )
			return false;
	for ( int axis = 0; axis < 3; ++axis ) center[axis] = result[axis];
	return true;
}

bool RunHammerCoreBoundsSmokeTest()
{
	BoundBox box;
	if ( box.IsValidBox() ) return false;
	box.UpdateBounds( Vector( -3.0f, 2.0f, 5.0f ) );
	box.UpdateBounds( Vector( 7.0f, 10.0f, 17.0f ) );
	Vector center, size;
	box.GetBoundsCenter( center );
	box.GetBoundsSize( size );
	if ( !box.IsValidBox() || center != Vector( 2.0f, 6.0f, 11.0f ) ||
		 size != Vector( 10.0f, 8.0f, 12.0f ) ||
		 !box.ContainsPoint( Vector( -3.0f, 2.0f, 5.0f ) ) ||
		 box.ContainsPoint( Vector( -4.0f, 2.0f, 5.0f ) ) ) return false;
	box.SnapToGrid( 4 );
	Vector snappedSize;
	box.GetBoundsSize( snappedSize );
	const double lo[3] = { -3.0, 2.0, 5.0 }, hi[3] = { 7.0, 10.0, 17.0 };
	double bridged[3] = {};
	const double preciseLo[3] = { 16777216.0, 0.0, 0.0 }, preciseHi[3] = { 16777218.0, 0.0, 0.0 };
	return snappedSize == size && box.bmins.x == -4.0f && box.bmins.z == 4.0f &&
		TryHammerCoreBoundsCenter( lo, hi, bridged ) && bridged[0] == 2.0 && bridged[1] == 6.0 && bridged[2] == 11.0 &&
		!TryHammerCoreBoundsCenter( preciseLo, preciseHi, bridged );
}
