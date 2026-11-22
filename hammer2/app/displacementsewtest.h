#pragma once

#include "displacementmesh.h"
#include "document.h"

inline bool TestHammerDisplacementSew()
{
	using namespace HammerDisplacementMesh;
	CHammerDocument map;
	const double aMin[] = { 0, 0, -16 }, aMax[] = { 64, 64, 0 };
	const double bMin[] = { 64, 0, -16 }, bMax[] = { 128, 64, 0 };
	const double cMin[] = { 128, 0, -16 }, cMax[] = { 192, 32, 0 };
	const int a = map.CreateBrush( aMin, aMax );
	const int b = map.CreateBrush( bMin, bMax );
	const int c = map.CreateBrush( cMin, cMax );
	const auto solid = [&map]( int id ) { return static_cast<const CHammerDocument &>( map ).Map().SolidById( id ); };
	const auto topIndex = [&solid]( int id ) {
		QList<Polygon> faces;
		if ( !solid( id ) || !HammerWinding::BuildFaces( *solid( id ), faces ) ) return -1;
		for ( int i = 0; i < faces.size(); ++i )
		{
			bool top = !faces[i].isEmpty();
			for ( const Point &point : faces[i] ) top = top && std::abs( point[2] ) < 1e-6;
			if ( top ) return i;
		}
		return -1;
	};
	const int ai = topIndex( a ), bi = topIndex( b ), ci = topIndex( c );
	if ( ai < 0 || bi < 0 || ci < 0 ) return false;
	const int af = solid( a )->sides[ai].id, bf = solid( b )->sides[bi].id, cf = solid( c )->sides[ci].id;
	const QMap<int, QSet<int>> pair{ { a, { af } }, { b, { bf } } };
	if ( !map.CreateDisplacements( { { a, { af } } }, 2 ) ||
	     !map.CreateDisplacements( { { b, { bf } } }, 3 ) ||
	     !map.SetDisplacementDistances( a, af, QList<double>( 25, 8 ) ) ||
	     !map.SetDisplacementDistances( b, bf, QList<double>( 81, 24 ) ) ) return false;
	const auto mesh = [&solid]( int id, int index, Mesh &out ) {
		QList<Polygon> faces;
		const SolidLite *brush = solid( id );
		return brush && HammerWinding::BuildFaces( *brush, faces ) && Build( brush->sides[index], faces[index], out );
	};
	const auto checkEdge = []( const Mesh &surface, double x, double yMax, double height ) {
		int matches = 0;
		for ( int i = 0; i < surface.base.size(); ++i )
			if ( std::abs( surface.base[i][0] - x ) < 1e-6 && surface.base[i][1] <= yMax + 1e-6 )
			{
				++matches;
				if ( std::abs( surface.vertices[i][2] - height ) > 1e-5 ) return false;
			}
		return matches > 0;
	};
	Mesh left, right;
	if ( map.SewDisplacements( { { a, { af } } } ) ||
	     map.SewDisplacements( { { a, { af } }, { b, { -1 } } } ) ||
	     !map.SewDisplacements( pair ) || !mesh( a, ai, left ) || !mesh( b, bi, right ) ||
	     !checkEdge( left, 64, 64, 16 ) || !checkEdge( right, 64, 64, 16 ) ||
	     !map.Undo() || !mesh( a, ai, left ) || !checkEdge( left, 64, 64, 8 ) ||
	     !map.Redo() || !mesh( a, ai, left ) || !checkEdge( left, 64, 64, 16 ) ) return false;
	if ( !map.SewDisplacements( { { b, { bf } }, { c, { cf } } } ) || !mesh( b, bi, right ) ||
	     !checkEdge( right, 128, 32, 0 ) ) return false;
	for ( int i = 0; i < right.base.size(); ++i )
		if ( std::abs( right.base[i][0] - 128 ) < 1e-6 && right.base[i][1] > 32 &&
		     std::abs( right.vertices[i][2] - 24 ) > 1e-5 ) return false;
	return true;
}
