#pragma once

#include "decalprojection.h"
#include "overlaydata.h"

namespace HammerOverlay
{
inline bool MapTriangle( Point &point, const HammerWinding::Polygon &base,
	const Point &a, const Point &b, const Point &c )
{
	Point alongB, alongC, relative;
	for ( int i = 0; i < 3; ++i )
	{
		alongB[i] = base[1][i] - base[0][i];
		alongC[i] = base[2][i] - base[0][i];
		relative[i] = point[i] - base[0][i];
	}
	using HammerWinding::Dot;
	const double bb = Dot( alongB, alongB ), bc = Dot( alongB, alongC ), cc = Dot( alongC, alongC );
	const double determinant = bb * cc - bc * bc;
	if ( determinant <= 1e-14 ) return false;
	const double rb = Dot( relative, alongB ), rc = Dot( relative, alongC );
	const double wb = ( cc * rb - bc * rc ) / determinant;
	const double wc = ( bb * rc - bc * rb ) / determinant;
	for ( int i = 0; i < 3; ++i ) point[i] = a[i] + wb * ( b[i] - a[i] ) + wc * ( c[i] - a[i] );
	return true;
}

// Project the four overlay handles along the stored basis normal, then clip
// each textured triangle to the receiving face. Triangle UVs stay continuous
// across the split and across neighboring receiving faces.
inline QList<HammerDecalProjection::Vertex> ProjectTriangles(
	const Data &data, const HammerWinding::Polygon &face, const HammerWinding::Plane &plane )
{
	using HammerWinding::Cross;
	using HammerWinding::Dot;
	using HammerDecalProjection::Vertex;
	if ( face.size() < 3 || !Convex( data ) ) return {};
	const double denominator = Dot( plane.normal, data.normal );
	if ( std::abs( denominator ) < 1e-6 ) return {};
	const auto subtract = []( const Point &a, const Point &b ) {
		return Point{ a[0] - b[0], a[1] - b[1], a[2] - b[2] };
	};
	const std::array<std::array<double, 2>, 4> uv{{
		{ data.startU, data.startV }, { data.startU, data.endV },
		{ data.endU, data.endV }, { data.endU, data.startV }
	}};
	std::array<Vertex, 4> projected;
	for ( int i = 0; i < 4; ++i )
	{
		Point position = WorldPoint( data, i );
		const double displacement = ( plane.dist - Dot( plane.normal, position ) ) / denominator;
		for ( int axis = 0; axis < 3; ++axis )
		{
			position[axis] += displacement * data.normal[axis];
			if ( !std::isfinite( position[axis] ) ) return {};
		}
		projected[i] = { position, uv[i] };
	}
	Point center{};
	for ( const auto &point : face )
		for ( int axis = 0; axis < 3; ++axis ) center[axis] += point[axis] / face.size();
	QList<Vertex> triangles;
	for ( const auto &indices : { std::array<int, 3>{ 0, 1, 2 }, std::array<int, 3>{ 0, 2, 3 } } )
	{
		QList<Vertex> polygon{ projected[indices[0]], projected[indices[1]], projected[indices[2]] };
		for ( int edge = 0; edge < face.size() && polygon.size() >= 3; ++edge )
		{
			const Point &edgeStart = face[edge];
			Point inward = Cross( plane.normal, subtract( face[( edge + 1 ) % face.size()], edgeStart ) );
			if ( !HammerWinding::Normalize( inward ) ) continue;
			if ( Dot( inward, subtract( center, edgeStart ) ) < 0 )
				for ( auto &axis : inward ) axis = -axis;
			QList<Vertex> clipped;
			for ( int i = 0; i < polygon.size(); ++i )
			{
				const Vertex &a = polygon[i], &b = polygon[( i + 1 ) % polygon.size()];
				const double da = Dot( inward, subtract( a.position, edgeStart ) );
				const double db = Dot( inward, subtract( b.position, edgeStart ) );
				if ( da >= 0 ) clipped.append( a );
				if ( ( da < 0 && db > 0 ) || ( da > 0 && db < 0 ) )
				{
					const double weight = da / ( da - db );
					Vertex point;
					for ( int k = 0; k < 3; ++k ) point.position[k] = a.position[k] + weight * ( b.position[k] - a.position[k] );
					for ( int k = 0; k < 2; ++k ) point.uv[k] = a.uv[k] + weight * ( b.uv[k] - a.uv[k] );
					clipped.append( point );
				}
			}
			polygon = clipped;
		}
		for ( int i = 1; i + 1 < polygon.size(); ++i )
			for ( int index : { 0, i, i + 1 } ) triangles.append( polygon[index] );
	}
	return triangles;
}
}
