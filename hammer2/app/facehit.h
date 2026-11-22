#pragma once

#include "brushwindings.h"
#include "displacementmesh.h"
#include "document.h"
#include "brushentityview.h"
#include <algorithm>
#include <limits>

// Returns the nearest visible brush face. Unsupported geometry and entity
// bounds occlude faces conservatively, matching the viewport's object picking.
namespace HammerFaceHit
{
struct Result { int solidId = -1; int faceIndex = -1; double distance = std::numeric_limits<double>::infinity(); };

inline bool BoxDistance( const double eye[3], const double ray[3], const double mins[3], const double maxs[3], double nearPlane, double &distance )
{
	double enter = -std::numeric_limits<double>::infinity();
	double exit = std::numeric_limits<double>::infinity();
	for ( int axis = 0; axis < 3; ++axis )
	{
		if ( std::abs( ray[axis] ) < 1e-12 ) { if ( eye[axis] < mins[axis] || eye[axis] > maxs[axis] ) return false; continue; }
		double a = ( mins[axis] - eye[axis] ) / ray[axis], b = ( maxs[axis] - eye[axis] ) / ray[axis];
		if ( a > b ) std::swap( a, b );
		enter = std::max( enter, a ); exit = std::min( exit, b );
		if ( enter > exit ) return false;
	}
	if ( exit < nearPlane ) return false;
	distance = std::max( enter, nearPlane );
	return true;
}

inline Result Find( CHammerDocument &document, const double eye[3], const double ray[3], double nearPlane )
{
	using namespace HammerWinding;
	Result result;
	const Point origin{ eye[0], eye[1], eye[2] }, direction{ ray[0], ray[1], ray[2] };
	// View > Filter: hidden brushes and entities neither hit nor occlude.
	const ViewFilter &filter = document.GetViewFilter();
	for ( const SolidLite &solid : document.Map().solids )
	{
		if ( !HammerBrushEntityView::Visible( document.Map(), filter, solid ) ) continue;
		QList<Polygon> faces;
		if ( !BuildFaces( solid, faces ) )
		{
			double distance;
			if ( BoxDistance( eye, ray, solid.mins, solid.maxs, nearPlane, distance ) && distance < result.distance )
				result = { -1, -1, distance };
			continue;
		}
		for ( int face = 0; face < faces.size(); ++face )
			HammerDisplacementMesh::ForEachPolygon( solid.sides[face], faces[face], [&]( const Polygon &polygon ) {
			for ( int i = 1; i + 1 < polygon.size(); ++i )
			{
				const auto &a = polygon[0], &b = polygon[i], &c = polygon[i + 1];
				Point e1, e2, offset;
				for ( int k = 0; k < 3; ++k ) { e1[k] = b[k] - a[k]; e2[k] = c[k] - a[k]; offset[k] = origin[k] - a[k]; }
				const Point h = Cross( direction, e2 ); const double determinant = Dot( e1, h );
				if ( std::abs( determinant ) < 1e-10 ) continue;
				const double inverse = 1.0 / determinant, u = inverse * Dot( offset, h );
				if ( u < -1e-8 || u > 1.00000001 ) continue;
				const Point q = Cross( offset, e1 ); const double v = inverse * Dot( direction, q );
				if ( v < -1e-8 || u + v > 1.00000001 ) continue;
				const double distance = inverse * Dot( e2, q );
				if ( distance >= nearPlane && distance < result.distance ) result = { solid.id, face, distance };
			}
			} );
	}
	for ( const EntityLite &entity : document.Map().entities )
	{
		if ( !filter.EntityVisible( entity ) ) continue;
		if ( HammerBrushEntityView::HasBrushes( document.Map(), entity.id ) ) continue;
		double mins[3], maxs[3], distance;
		document.Fgd().ClassBounds( entity.classname, mins, maxs );
		for ( int k = 0; k < 3; ++k ) { mins[k] += entity.origin[k]; maxs[k] += entity.origin[k]; }
		if ( BoxDistance( eye, ray, mins, maxs, nearPlane, distance ) && distance < result.distance ) result = { -1, -1, distance };
	}
	return result;
}
}
