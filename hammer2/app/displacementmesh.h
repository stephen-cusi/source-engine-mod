#pragma once

#include "brushwindings.h"
#include "displacementinspect.h"
#include <algorithm>
#include <limits>
#include <utility>

namespace HammerDisplacementMesh
{
using HammerWinding::Point;
using HammerWinding::Polygon;

struct Mesh
{
	int width = 0;
	QList<Point> base;
	QList<Point> vertices;
	QList<std::array<int, 3>> triangles;
};

inline bool Build( const SideLite &side, const Polygon &quad, Mesh &output )
{
	const auto &chunk = side.displacement;
	if ( !chunk || quad.size() != 4 ) return false;
	static const QRegularExpression whitespace( QStringLiteral( "\\s+" ) );
	const auto words = []( const QString &value ) { return value.trimmed().split( whitespace, Qt::SkipEmptyParts ); };
	int power = 0;
	Point start{};
	bool hasStart = false;
	double elevation = 0;
	for ( const auto &key : chunk->keys )
	{
		const QString name = key.first.toLower();
		if ( name == QLatin1String( "power" ) ) power = key.second.toInt();
		else if ( name == QLatin1String( "elevation" ) ) elevation = key.second.toDouble();
		else if ( name == QLatin1String( "startposition" ) )
		{
			QString value = key.second.trimmed();
			if ( value.startsWith( '[' ) && value.endsWith( ']' ) ) value = value.mid( 1, value.size() - 2 );
			const QStringList tokens = words( value );
			if ( tokens.size() != 3 ) return false;
			for ( int k = 0; k < 3; ++k )
			{
				bool ok = false;
				start[k] = tokens[k].toDouble( &ok );
				if ( !ok || !std::isfinite( start[k] ) ) return false;
			}
			hasStart = true;
		}
	}
	if ( power < 2 || power > 4 || !hasStart || !std::isfinite( elevation ) ) return false;
	const int width = ( 1 << power ) + 1;
	const auto readRows = [&]( QLatin1String name, int components, QList<double> &out ) {
		for ( const auto &section : chunk->children )
		{
			if ( section.name.compare( name, Qt::CaseInsensitive ) != 0 ) continue;
			if ( section.keys.size() != width ) return false;
			out.resize( width * width * components );
			QSet<int> seen;
			for ( const auto &row : section.keys )
			{
				bool ok = false;
				const int index = row.first.mid( 3 ).toInt( &ok );
				if ( !ok || !row.first.startsWith( QLatin1String( "row" ), Qt::CaseInsensitive ) || index < 0 || index >= width || seen.contains( index ) ) return false;
				seen.insert( index );
				const QStringList tokens = words( row.second );
				if ( tokens.size() != width * components ) return false;
				for ( int i = 0; i < tokens.size(); ++i )
				{
					const double value = tokens[i].toDouble( &ok );
					if ( !ok || !std::isfinite( value ) ) return false;
					out[index * width * components + i] = value;
				}
			}
			return true;
		}
		return name == QLatin1String( "offsets" );
	};
	QList<double> normals, distances, offsets;
	if ( !readRows( QLatin1String( "normals" ), 3, normals ) || !readRows( QLatin1String( "distances" ), 1, distances ) ||
	     !readRows( QLatin1String( "offsets" ), 3, offsets ) ) return false;

	int first = 0;
	double nearest = std::numeric_limits<double>::infinity();
	for ( int i = 0; i < 4; ++i )
	{
		Point delta;
		for ( int k = 0; k < 3; ++k ) delta[k] = quad[i][k] - start[k];
		const double distance = HammerWinding::Dot( delta, delta );
		if ( distance < nearest ) { nearest = distance; first = i; }
	}
	if ( !std::isfinite( nearest ) ) return false;
	Point edgeA, edgeB;
	for ( int k = 0; k < 3; ++k )
	{
		edgeA[k] = side.planePts[1][k] - side.planePts[0][k];
		edgeB[k] = side.planePts[2][k] - side.planePts[0][k];
	}
	Point faceNormal = HammerWinding::Cross( edgeB, edgeA );
	if ( !HammerWinding::Normalize( faceNormal ) ) return false;

	// Follow CCoreDispInfo::GenerateDispSurf and GenerateCollisionSurface.
	// VMF rows run from corner 0 to 1, then columns from that edge to 3/2.
	Mesh mesh;
	mesh.width = width;
	const Point &a = quad[first], &b = quad[(first + 1) % 4], &c = quad[(first + 2) % 4], &d = quad[(first + 3) % 4];
	for ( int row = 0; row < width; ++row )
		for ( int col = 0; col < width; ++col )
		{
			const double r = double( row ) / ( width - 1 ), s = double( col ) / ( width - 1 );
			const int index = row * width + col;
			Point base, position;
			for ( int k = 0; k < 3; ++k )
			{
				base[k] = ( a[k] + ( b[k] - a[k] ) * r ) * ( 1 - s ) + ( d[k] + ( c[k] - d[k] ) * r ) * s;
				position[k] = base[k] + elevation * faceNormal[k] + distances[index] * normals[index * 3 + k];
				if ( !offsets.isEmpty() ) position[k] += offsets[index * 3 + k];
				if ( !std::isfinite( position[k] ) || std::abs( position[k] ) > 1048576.0 ) return false;
			}
			mesh.base.append( base );
			mesh.vertices.append( position );
			if ( row == width - 1 || col == width - 1 ) continue;
			if ( index % 2 )
			{
				mesh.triangles.append( std::array<int, 3>{ index, index + width, index + 1 } );
				mesh.triangles.append( std::array<int, 3>{ index + 1, index + width, index + width + 1 } );
			}
			else
			{
				mesh.triangles.append( std::array<int, 3>{ index, index + width, index + width + 1 } );
				mesh.triangles.append( std::array<int, 3>{ index, index + width + 1, index + 1 } );
			}
		}
	output = std::move( mesh );
	return true;
}

template <typename Visitor>
inline void ForEachPolygon( const SideLite &side, const Polygon &face, Visitor visit )
{
	Mesh mesh;
	if ( !Build( side, face, mesh ) ) { visit( face ); return; }
	for ( const auto &t : mesh.triangles ) visit( Polygon{ mesh.vertices[t[0]], mesh.vertices[t[1]], mesh.vertices[t[2]] } );
}

inline bool HasTerrain( const SolidLite &solid )
{
	return std::any_of( solid.sides.cbegin(), solid.sides.cend(), []( const SideLite &side ) { return side.displacement.has_value(); } );
}

inline void ExpandProjectedBounds( const SolidLite &solid, int u, int v, double mins[2], double maxs[2] )
{
	if ( !HasTerrain( solid ) ) return;
	QList<Polygon> faces;
	if ( !HammerWinding::BuildFaces( solid, faces ) ) return;
	for ( int face = 0; face < faces.size(); ++face )
	{
		Mesh terrain;
		if ( !Build( solid.sides[face], faces[face], terrain ) ) continue;
		for ( const Point &point : terrain.vertices )
		{
			mins[0] = std::min( mins[0], point[u] ); mins[1] = std::min( mins[1], point[v] );
			maxs[0] = std::max( maxs[0], point[u] ); maxs[1] = std::max( maxs[1], point[v] );
		}
	}
}
}
