#pragma once

#include "displacementedit.h"
#include "displacementmesh.h"

namespace HammerDisplacementSew
{
using HammerWinding::Point;
using HammerDisplacementMesh::Mesh;

struct Face
{
	int solidId;
	int sideIndex;
	SideLite side;
	Mesh mesh;
	QList<QList<int>> edges;
	int firstNode = 0;

	bool IsTerrain() const { return side.displacement.has_value(); }
};

inline bool Prepare( Face &face, const HammerWinding::Polygon &polygon )
{
	if ( !face.IsTerrain() )
	{
		for ( int i = 0; i < polygon.size(); ++i )
		{
			const Point &a = polygon[i], &b = polygon[( i + 1 ) % polygon.size()];
			Point midpoint{};
			for ( int k = 0; k < 3; ++k ) midpoint[k] = ( a[k] + b[k] ) / 2;
			face.mesh.base.append( a );
			face.mesh.base.append( midpoint );
			face.edges.append( QList<int>{ 2 * i, 2 * i + 1, 2 * ( ( i + 1 ) % polygon.size() ) } );
		}
		face.mesh.vertices = face.mesh.base;
		return polygon.size() >= 3;
	}
	if ( !HammerDisplacementInspect::InspectCanonical( *face.side.displacement ) ||
	     !HammerDisplacementMesh::Build( face.side, polygon, face.mesh ) ) return false;
	const int width = face.mesh.width, end = width - 1;
	face.edges.resize( 4 );
	for ( int i = 0; i < width; ++i )
	{
		face.edges[0].append( i * width );
		face.edges[1].append( end * width + i );
		face.edges[2].append( ( end - i ) * width + end );
		face.edges[3].append( end - i );
	}
	return true;
}

inline bool Near( const Point &a, const Point &b, double tolerance = 0.01 )
{
	return std::abs( a[0] - b[0] ) <= tolerance && std::abs( a[1] - b[1] ) <= tolerance &&
	       std::abs( a[2] - b[2] ) <= tolerance;
}

struct Span
{
	const Face &face;
	const QList<int> &edge;
	int start;
	int end;

	int Count() const { return std::abs( end - start ); }
	int Local( int i ) const { return edge[start + ( end > start ? i : -i )]; }
	int Node( int i ) const { return face.firstNode + Local( i ); }
	const Point &Base( int i ) const { return face.mesh.base[Local( i )]; }
};

struct Node
{
	Point position;
	bool fixed;
	int parent;
};

inline int Root( QList<Node> &nodes, int index )
{
	while ( nodes[index].parent != index )
	{
		nodes[index].parent = nodes[nodes[index].parent].parent;
		index = nodes[index].parent;
	}
	return index;
}

inline void Join( QList<Node> &nodes, int a, int b )
{
	nodes[Root( nodes, a )].parent = Root( nodes, b );
}

struct Interpolation
{
	int node;
	int start;
	int end;
	double fraction;
};

// Sew matching samples before interpolating surplus samples. This follows
// the common-sample and dependent-vertex stages in hammer/dispsew.cpp.
inline void JoinSpans( Span first, Span second, QList<Node> &nodes, QList<Interpolation> &dependent )
{
	const Span &coarse = first.Count() <= second.Count() ? first : second;
	const Span &fine = first.Count() <= second.Count() ? second : first;
	const int ratio = fine.Count() / coarse.Count();
	for ( int i = 0; i <= coarse.Count(); ++i )
	{
		Join( nodes, coarse.Node( i ), fine.Node( i * ratio ) );
		if ( i == coarse.Count() ) break;
		for ( int j = 1; j < ratio; ++j )
			dependent.append( { fine.Node( i * ratio + j ), coarse.Node( i ), coarse.Node( i + 1 ), double( j ) / ratio } );
	}
}

inline bool MatchSpans( Span a, Span b, QList<Node> &nodes, QList<Interpolation> &dependent )
{
	if ( Near( a.Base( 0 ), b.Base( b.Count() ) ) && Near( a.Base( a.Count() ), b.Base( 0 ) ) )
		std::swap( b.start, b.end );
	if ( !Near( a.Base( 0 ), b.Base( 0 ) ) || !Near( a.Base( a.Count() ), b.Base( b.Count() ) ) ) return false;
	JoinSpans( a, b, nodes, dependent );
	return true;
}

inline bool MatchEdges( Span a, Span b, QList<Node> &nodes, QList<Interpolation> &dependent )
{
	if ( MatchSpans( a, b, nodes, dependent ) ) return true;
	const int aMiddle = ( a.start + a.end ) / 2, bMiddle = ( b.start + b.end ) / 2;
	// Original Hammer recognizes T-junctions at an edge midpoint.
	return MatchSpans( { a.face, a.edge, a.start, aMiddle }, b, nodes, dependent ) ||
	       MatchSpans( { a.face, a.edge, aMiddle, a.end }, b, nodes, dependent ) ||
	       MatchSpans( a, { b.face, b.edge, b.start, bMiddle }, nodes, dependent ) ||
	       MatchSpans( a, { b.face, b.edge, bMiddle, b.end }, nodes, dependent );
}

inline bool WriteVertices( Face &face, const QList<Point> &positions, bool &changed )
{
	if ( !face.IsTerrain() ) return true;
	const int width = face.mesh.width;
	QList<QStringList> normals( width ), distances( width );
	for ( const auto &section : face.side.displacement->children )
	{
		const QString name = section.name.toLower();
		if ( name != QLatin1String( "normals" ) && name != QLatin1String( "distances" ) ) continue;
		auto &rows = name == QLatin1String( "normals" ) ? normals : distances;
		for ( const auto &entry : section.keys )
			rows[entry.first.mid( 3 ).toInt()] = entry.second.split( QLatin1Char( ' ' ), Qt::SkipEmptyParts );
	}
	QSet<int> dirtyRows;
	for ( int i = 0; i < face.mesh.vertices.size(); ++i )
	{
		const Point &target = positions[face.firstNode + i], &old = face.mesh.vertices[i];
		if ( Near( old, target, 1e-5 ) ) continue;
		const int row = i / width, column = i % width;
		const double distance = distances[row][column].toDouble();
		Point field{};
		for ( int k = 0; k < 3; ++k )
			field[k] = normals[row][3 * column + k].toDouble() * distance + target[k] - old[k];
		const double length = std::sqrt( HammerWinding::Dot( field, field ) );
		if ( !std::isfinite( length ) ) return false;
		// Preserve the original direction for zero field vectors so a
		// later distance edit can still raise that vertex.
		if ( length > 0 )
			for ( int k = 0; k < 3; ++k )
				normals[row][3 * column + k] = QString::number( field[k] / length, 'g', 10 );
		distances[row][column] = QString::number( length, 'g', 10 );
		dirtyRows.insert( row );
	}
	for ( auto &section : face.side.displacement->children )
	{
		const QString name = section.name.toLower();
		if ( name != QLatin1String( "normals" ) && name != QLatin1String( "distances" ) ) continue;
		const auto &rows = name == QLatin1String( "normals" ) ? normals : distances;
		for ( auto &entry : section.keys )
		{
			const int row = entry.first.mid( 3 ).toInt();
			if ( dirtyRows.contains( row ) ) entry.second = rows[row].join( QLatin1Char( ' ' ) );
		}
	}
	changed = !dirtyRows.isEmpty();
	return !changed || HammerDisplacementInspect::InspectCanonical( *face.side.displacement );
}

inline bool Sew( QList<Face> &faces, QSet<int> &changedFaces )
{
	QList<Node> nodes;
	for ( Face &face : faces )
	{
		face.firstNode = nodes.size();
		for ( const Point &point : face.mesh.vertices )
			nodes.append( { point, !face.IsTerrain(), int( nodes.size() ) } );
	}
	bool matched = false;
	QList<Interpolation> dependent;
	for ( int i = 0; i < faces.size(); ++i )
		for ( int j = 0; j < i; ++j )
		{
			const Face &a = faces[i], &b = faces[j];
			if ( !a.IsTerrain() && !b.IsTerrain() ) continue;
			for ( const auto &edgeA : a.edges )
				for ( const auto &edgeB : b.edges )
				{
					const Span spanA{ a, edgeA, 0, int( edgeA.size() ) - 1 };
					const Span spanB{ b, edgeB, 0, int( edgeB.size() ) - 1 };
					if ( MatchEdges( spanA, spanB, nodes, dependent ) ) matched = true;
					if ( Near( spanA.Base( 0 ), spanB.Base( 0 ) ) )
					{
						Join( nodes, spanA.Node( 0 ), spanB.Node( 0 ) );
						matched = true;
					}
				}
		}
	if ( !matched ) return false;
	struct Accumulator
	{
		Point sum{};
		int count = 0;
		bool fixed = false;
	};
	QList<Accumulator> groups( nodes.size() );
	for ( int i = 0; i < nodes.size(); ++i )
	{
		auto &group = groups[Root( nodes, i )];
		const Node &node = nodes[i];
		if ( group.fixed && !node.fixed ) continue;
		if ( node.fixed && !group.fixed ) group = { {}, 0, true };
		for ( int k = 0; k < 3; ++k ) group.sum[k] += node.position[k];
		++group.count;
	}
	QList<Point> positions( nodes.size() );
	for ( int i = 0; i < nodes.size(); ++i )
	{
		const auto &group = groups[Root( nodes, i )];
		for ( int k = 0; k < 3; ++k ) positions[i][k] = group.sum[k] / group.count;
	}
	QMap<int, Point> constraints;
	for ( const Interpolation &sample : dependent )
	{
		const int root = Root( nodes, sample.node );
		Point value{};
		for ( int k = 0; k < 3; ++k )
			value[k] = positions[sample.start][k] + ( positions[sample.end][k] - positions[sample.start][k] ) * sample.fraction;
		if ( groups[root].fixed && !Near( value, positions[sample.node] ) ) return false;
		if ( constraints.contains( root ) && !Near( constraints.value( root ), value ) ) return false;
		if ( !groups[root].fixed ) constraints.insert( root, value );
	}
	for ( int i = 0; i < nodes.size(); ++i )
	{
		const auto it = constraints.constFind( Root( nodes, i ) );
		if ( it != constraints.cend() ) positions[i] = it.value();
	}
	for ( int i = 0; i < faces.size(); ++i )
	{
		bool changed = false;
		if ( !WriteVertices( faces[i], positions, changed ) ) return false;
		if ( changed ) changedFaces.insert( i );
	}
	return true;
}
}
