#pragma once
#include "brushentityview.h"

#include "mapmodel.h"
#include "brushwindings.h"
#include "viewfilter.h"
#include <QSet>
#include <cmath>

// These are deliberately limited to checks supported by the current model.
// A missing HL2 player start is advisory: other games may use another class.
enum class MapCheckKind { NoPlayerStart, InvalidTextureAxes, DuplicateFaceId, DuplicateNodeId, DuplicatePlaneNormals, UnreconstructableSolid,
	OutputEmptyTarget, OutputMissingTarget, OutputInvalidDelay, OutputInvalidCount };
struct MapCheckIssue { MapCheckKind kind; int solidId; int faceId; };

// A null filter checks the whole map; otherwise apply the current transient
// visibility filter, mirroring Hammer's Check Visible Only option.
inline QList<MapCheckIssue> CheckMapProblems( const MapDocLite &map, const ViewFilter *visibleOnly = nullptr )
{
	QList<MapCheckIssue> issues;
	bool hasStart = false;
	for ( const EntityLite &entity : map.entities )
		if ( ( !visibleOnly || visibleOnly->EntityVisible( entity ) ) &&
			entity.classname.compare( QLatin1String( "info_player_start" ), Qt::CaseInsensitive ) == 0 )
			hasStart = true;
	if ( !hasStart ) issues.append( { MapCheckKind::NoPlayerStart, -1, -1 } );
	// Match GDclass::IsNodeClass and Hammer's nonzero duplicate-node check.
	// Node links reference node IDs but are not node entities themselves.
	QMap<int, int> nodeCounts;
	const auto nodeId = []( const EntityLite &entity ) {
		if ( !entity.classname.startsWith( QLatin1String( "info_node" ), Qt::CaseInsensitive ) ||
			entity.classname.compare( QLatin1String( "info_node_link" ), Qt::CaseInsensitive ) == 0 ) return 0;
		for ( auto it = entity.keyvalues.cbegin(); it != entity.keyvalues.cend(); ++it )
			if ( it.key().compare( QLatin1String( "nodeid" ), Qt::CaseInsensitive ) == 0 )
			{
				bool ok = false;
				const int value = it.value().trimmed().toInt( &ok );
				return ok ? value : 0;
			}
		return 0;
	};
	for ( const EntityLite &entity : map.entities )
	{
		if ( visibleOnly && !visibleOnly->EntityVisible( entity ) ) continue;
		const int id = nodeId( entity );
		if ( id != 0 ) ++nodeCounts[id];
	}
	for ( const EntityLite &entity : map.entities )
	{
		if ( visibleOnly && !visibleOnly->EntityVisible( entity ) ) continue;
		const int id = nodeId( entity );
		if ( id != 0 && nodeCounts.value( id ) > 1 )
			issues.append( { MapCheckKind::DuplicateNodeId, entity.id, id } );
	}
	// Outputs are advisory: custom inputs and runtime targets are
	// legitimate. Only inspect unambiguous five-field connections and leave
	// unknown formats untouched. An empty input defaults to "Use" in Source.
	const auto namesMatch = []( const QString &name, const QString &target ) {
		const int a = name.indexOf( QLatin1Char( '*' ) );
		const int b = target.indexOf( QLatin1Char( '*' ) );
		if ( a < 0 && b < 0 ) return name.compare( target, Qt::CaseInsensitive ) == 0;
		const int length = a < 0 ? b : b < 0 ? a : qMin( a, b );
		return name.left( length ).compare( target.left( length ), Qt::CaseInsensitive ) == 0;
	};
	const auto hasTarget = [&]( const QString &target ) {
		for ( const EntityLite &candidate : map.entities )
		{
			if ( visibleOnly && !visibleOnly->EntityVisible( candidate ) ) continue;
			for ( auto it = candidate.keyvalues.cbegin(); it != candidate.keyvalues.cend(); ++it )
				if ( it.key().compare( QLatin1String( "targetname" ), Qt::CaseInsensitive ) == 0 &&
					!it.value().isEmpty() && namesMatch( it.value(), target ) ) return true;
		}
		return false;
	};
	for ( const EntityLite &entity : map.entities )
	{
		if ( visibleOnly && !visibleOnly->EntityVisible( entity ) ) continue;
		for ( int index = 0; index < entity.connections.size(); ++index )
		{
			const QString raw = entity.connections[index].second;
			const QChar delimiter = raw.contains( QChar( 27 ) ) ? QChar( 27 ) : QLatin1Char( ',' );
			const QStringList fields = raw.split( delimiter );
			if ( fields.size() != 5 ) continue;
			if ( fields[0].trimmed().isEmpty() )
				issues.append( { MapCheckKind::OutputEmptyTarget, entity.id, index + 1 } );
			else if ( !fields[0].startsWith( QLatin1Char( '!' ) ) && !hasTarget( fields[0] ) )
				issues.append( { MapCheckKind::OutputMissingTarget, entity.id, index + 1 } );
			bool ok = false;
			if ( !fields[3].trimmed().isEmpty() )
			{
				const double delay = fields[3].toDouble( &ok );
				if ( !ok || !std::isfinite( delay ) )
					issues.append( { MapCheckKind::OutputInvalidDelay, entity.id, index + 1 } );
			}
			if ( !fields[4].trimmed().isEmpty() )
			{
				fields[4].toInt( &ok );
				if ( !ok ) issues.append( { MapCheckKind::OutputInvalidCount, entity.id, index + 1 } );
			}
		}
	}
	QSet<int> faceIds;
	for ( const SolidLite &solid : map.solids )
	{
		if ( visibleOnly && !HammerBrushEntityView::Visible( map, *visibleOnly, solid ) ) continue;
		// Original Hammer's duplicate-plane check compares face normals, not
		// plane distances: same-facing parallel planes are redundant as well.
		// Invalid/degenerate planes are left to the geometry check below.
		QList<HammerWinding::Point> normals;
		bool duplicateNormal = false;
		for ( const SideLite &side : solid.sides )
		{
			HammerWinding::Point a, b;
			for ( int i = 0; i < 3; ++i )
			{
				a[i] = side.planePts[1][i] - side.planePts[0][i];
				b[i] = side.planePts[2][i] - side.planePts[0][i];
			}
			HammerWinding::Point normal = HammerWinding::Cross( b, a );
			if ( !HammerWinding::Normalize( normal ) ) continue;
			for ( const auto &other : normals )
				if ( normal == other ) { duplicateNormal = true; break; }
			normals.append( normal );
		}
		if ( duplicateNormal ) issues.append( { MapCheckKind::DuplicatePlaneNormals, solid.id, -1 } );
		// Reuse the same plane clipping as the 3D view. This is advisory:
		// its limits and tolerances are not identical to VBSP's validation.
		QList<HammerWinding::Polygon> faces;
		if ( !HammerWinding::BuildFaces( solid, faces ) )
			issues.append( { MapCheckKind::UnreconstructableSolid, solid.id, -1 } );
		for ( const SideLite &side : solid.sides )
		{
			// Nonpositive IDs are assigned when saving, so multiple unallocated
			// faces do not represent a duplicate persisted face ID.
			if ( side.id > 0 )
			{
				if ( faceIds.contains( side.id ) )
					issues.append( { MapCheckKind::DuplicateFaceId, solid.id, side.id } );
				faceIds.insert( side.id );
			}
			bool invalid = !std::isfinite( side.uscale ) || !std::isfinite( side.vscale ) || side.uscale == 0 || side.vscale == 0;
			for ( int i = 0; i < 4; ++i )
				invalid = invalid || !std::isfinite( side.uaxis[i] ) || !std::isfinite( side.vaxis[i] );
			const auto zero = []( const float axis[4] ) { return axis[0] == 0 && axis[1] == 0 && axis[2] == 0; };
			// CMapFace::IsTextureAxisValid checks whether the texture axes span
			// the face: DotProduct(plane.normal, CrossProduct(V, U)) != 0.
			// A degenerate plane has no reliable normal; leave it to geometry checks.
			double a[3], b[3], normal[3], textureNormal[3];
			bool validPlane = true;
			for ( int i = 0; i < 3; ++i )
			{
				for ( int p = 0; p < 3; ++p ) validPlane = validPlane && std::isfinite( side.planePts[p][i] );
				a[i] = side.planePts[1][i] - side.planePts[0][i];
				b[i] = side.planePts[2][i] - side.planePts[0][i];
			}
			for ( int i = 0; i < 3; ++i )
			{
				const int j = ( i + 1 ) % 3, k = ( i + 2 ) % 3;
				normal[i] = a[j] * b[k] - a[k] * b[j];
				textureNormal[i] = double( side.vaxis[j] ) * side.uaxis[k] - double( side.vaxis[k] ) * side.uaxis[j];
				validPlane = validPlane && std::isfinite( normal[i] );
			}
			if ( validPlane && ( normal[0] != 0 || normal[1] != 0 || normal[2] != 0 ) )
			{
				const double dot = normal[0] * textureNormal[0] + normal[1] * textureNormal[1] + normal[2] * textureNormal[2];
				invalid = invalid || !std::isfinite( dot ) || dot == 0;
			}
			if ( invalid || zero( side.uaxis ) || zero( side.vaxis ) )
				issues.append( { MapCheckKind::InvalidTextureAxes, solid.id, side.id } );
		}
	}
	return issues;
}
