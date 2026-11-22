#pragma once

#include "displacementmesh.h"

inline bool TestHammerDisplacementMesh()
{
	using namespace HammerDisplacementMesh;
	SideLite side{};
	side.planePts[1][1] = 16;
	side.planePts[2][0] = 32;
	side.displacement.emplace();
	auto &chunk = *side.displacement;
	chunk.name = QStringLiteral( "dispinfo" );
	chunk.keys = { { QStringLiteral( "power" ), QStringLiteral( "2" ) },
		{ QStringLiteral( "startposition" ), QStringLiteral( "[32 16 0]" ) },
		{ QStringLiteral( "elevation" ), QStringLiteral( "3" ) } };
	HammerDisplacementInspect::Chunk normals, distances, offsets;
	normals.name = QStringLiteral( "normals" );
	distances.name = QStringLiteral( "distances" );
	offsets.name = QStringLiteral( "offsets" );
	// Reversed file order must still address rows by their names.
	for ( int row = 4; row >= 0; --row )
	{
		const QString key = QStringLiteral( "row%1" ).arg( row );
		normals.keys.append( qMakePair( key, QStringLiteral( "0 0 1 0 0 1 0 0 1 0 0 1 0 0 1" ) ) );
		distances.keys.append( qMakePair( key, row == 2 ? QStringLiteral( "0 7 0 0 0" ) : QStringLiteral( "0 0 0 0 0" ) ) );
		offsets.keys.append( qMakePair( key, row == 2 ? QStringLiteral( "0 0 0 1 2 5 0 0 0 0 0 0 0 0 0" ) : QStringLiteral( "0 0 0 0 0 0 0 0 0 0 0 0 0 0 0" ) ) );
	}
	chunk.children = { normals, distances, offsets };
	const Polygon quad{ Point{ 0, 0, 0 }, Point{ 0, 16, 0 }, Point{ 32, 16, 0 }, Point{ 32, 0, 0 } };
	Mesh terrain;
	if ( !Build( side, quad, terrain ) || terrain.width != 5 || terrain.base.size() != 25 || terrain.vertices.size() != 25 || terrain.triangles.size() != 32 ) return false;
	if ( terrain.vertices[0] != Point{ 32, 16, 3 } || terrain.base[11] != Point{ 24, 8, 0 } || terrain.vertices[11] != Point{ 25, 10, 15 } ) return false;
	if ( terrain.triangles[0] != std::array<int, 3>{ 0, 5, 6 } || terrain.triangles[2] != std::array<int, 3>{ 1, 6, 2 } ) return false;
	chunk.children[1].keys[0].second = QStringLiteral( "0 0" );
	return !Build( side, quad, terrain ) && terrain.vertices[11] == Point{ 25, 10, 15 };
}
