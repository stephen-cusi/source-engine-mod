#pragma once

#include "displacementmesh.h"

namespace HammerDecalProjection
{
using HammerWinding::Point;
struct Vertex { Point position; std::array<double, 2> uv; };

// Source's CMapDecal accepts nearby front-facing planes and clips the face
// winding to the decal's unit texture square (hammer/clipcode.cpp).
inline QList<Vertex> ClipProjection( const HammerWinding::Polygon &face, const Point &normal,
	const Point &origin, double width, double height )
{
	if ( face.size() < 3 || !std::isfinite( width ) || !std::isfinite( height ) || width <= 0 || height <= 0 ) return {};
	const Point reference = std::abs( normal[2] ) > std::sqrt( 0.5 ) ? Point{ 1, 0, 0 } : Point{ 0, 0, -1 };
	Point s = std::abs( normal[2] ) > std::sqrt( 0.5 )
		? HammerWinding::Cross( normal, HammerWinding::Cross( reference, normal ) )
		: HammerWinding::Cross( normal, reference );
	Point t = HammerWinding::Cross( s, normal );
	if ( !HammerWinding::Normalize( s ) || !HammerWinding::Normalize( t ) ) return {};
	QList<Vertex> polygon;
	for ( const Point &point : face )
	{
		Point relative;
		for ( int i = 0; i < 3; ++i ) relative[i] = point[i] - origin[i];
		const double u = 0.5 + HammerWinding::Dot( s, relative ) / width;
		const double v = 0.5 + HammerWinding::Dot( t, relative ) / height;
		if ( !std::isfinite( u ) || !std::isfinite( v ) ) return {};
		polygon.append( { point, { u, v } } );
	}
	for ( int axis = 0; axis < 2; ++axis )
		for ( double edge : { 0.0, 1.0 } )
		{
			QList<Vertex> clipped;
			for ( int i = 0; i < polygon.size(); ++i )
			{
				const Vertex &a = polygon[i], &b = polygon[( i + 1 ) % polygon.size()];
				const auto signedDistance = [axis, edge]( const Vertex &vertex ) {
					return edge == 0 ? vertex.uv[axis] : 1.0 - vertex.uv[axis];
				};
				const double da = signedDistance( a ), db = signedDistance( b );
				if ( da >= 0 ) clipped.append( a );
				// An endpoint exactly on the clip line is already retained.
				if ( ( da < 0 && db > 0 ) || ( da > 0 && db < 0 ) )
				{
					const double weight = da / ( da - db );
					Vertex point;
					for ( int k = 0; k < 3; ++k ) point.position[k] = a.position[k] + weight * ( b.position[k] - a.position[k] );
					for ( int k = 0; k < 2; ++k ) point.uv[k] = a.uv[k] + weight * ( b.uv[k] - a.uv[k] );
					point.uv[axis] = edge;
					clipped.append( point );
				}
			}
			polygon = clipped;
			if ( polygon.size() < 3 ) return {};
		}
	return polygon;
}

inline QList<Vertex> Build( const HammerWinding::Polygon &face, const HammerWinding::Plane &plane,
	const Point &origin, double width, double height )
{
	const double distance = HammerWinding::Dot( plane.normal, origin ) - plane.dist;
	if ( !std::isfinite( distance ) || distance < -0.0001 || distance > 16.0 ) return {};
	return ClipProjection( face, plane.normal, origin, width, height );
}

// Keep the base face's texture frame across the whole mesh, as the Source
// displacement decal renderer does. Clip on the deformed triangles so the
// result stays on the surface, including at changes in slope.
inline QList<Vertex> BuildDisplacement( const HammerDisplacementMesh::Mesh &mesh, const Point &normal,
	const Point &origin, double width, double height )
{
	QList<Vertex> vertices;
	const double depth = std::max( width, height ) * 0.5;
	for ( const auto &indices : mesh.triangles )
	{
		const Point &a = mesh.vertices[indices[0]], &b = mesh.vertices[indices[1]], &c = mesh.vertices[indices[2]];
		Point ab, ac, relative;
		for ( int axis = 0; axis < 3; ++axis )
		{
			ab[axis] = b[axis] - a[axis];
			ac[axis] = c[axis] - a[axis];
			relative[axis] = origin[axis] - a[axis];
		}
		Point triangleNormal = HammerWinding::Cross( ab, ac );
		if ( !HammerWinding::Normalize( triangleNormal ) ||
			std::abs( HammerWinding::Dot( triangleNormal, relative ) ) > depth ) continue;
		const auto clipped = ClipProjection( { a, b, c }, normal, origin, width, height );
		for ( int fan = 1; fan + 1 < clipped.size(); ++fan )
			for ( int index : { 0, fan, fan + 1 } ) vertices.append( clipped[index] );
	}
	return vertices;
}
}
