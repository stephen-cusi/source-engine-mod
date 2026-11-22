#pragma once

// Read-only preflight for a future VMF group object model. This does not
// connect to the VMF loader and does not make grouped maps editable.
#include <QList>
#include <QMap>
#include <QPair>
#include <QSet>
#include <QString>

namespace HammerGroupInspect
{
using Properties = QList<QPair<QString, QString>>;

struct Group { int id = 0; Properties editor; };
struct Object { int id = 0; int ownerEntityId = 0; Properties editor; };

// Require at most one unambiguous positive parent reference. Keep this
// separate from the VMF lexer: duplicate keys are intentional input here.
inline bool ParentId( const Properties &editor, int &parent, QString *error )
{
	parent = 0;
	bool seen = false;
	for ( const auto &entry : editor )
	{
		if ( entry.first.compare( QLatin1String( "groupid" ), Qt::CaseInsensitive ) != 0 ) continue;
		bool valid = false;
		const int parsed = entry.second.toInt( &valid );
		if ( seen || !valid || parsed <= 0 )
		{
			if ( error ) *error = QStringLiteral( "invalid or duplicate groupid" );
			return false;
		}
		seen = true;
		parent = parsed;
	}
	return true;
}

// Validate group/object IDs, referential integrity and cycles before a host
// builds ownership. Entity-owned brushes cannot also be independent members.
// The caller is responsible for validating all other VMF object/face IDs.
inline bool Inspect( const QList<Group> &groups, const QList<Object> &objects, QString *error = nullptr )
{
	if ( error ) error->clear();
	QMap<int, int> parents;
	QSet<int> ids;
	for ( const Group &group : groups )
	{
		if ( group.id <= 0 || ids.contains( group.id ) )
		{
			if ( error ) *error = QStringLiteral( "invalid or duplicate group ID" );
			return false;
		}
		int parent = 0;
		if ( !ParentId( group.editor, parent, error ) ) return false;
		ids.insert( group.id );
		parents.insert( group.id, parent );
	}
	for ( auto it = parents.cbegin(); it != parents.cend(); ++it )
	{
		QSet<int> path;
		int current = it.key();
		while ( current )
		{
			if ( !parents.contains( current ) )
			{
				if ( error ) *error = QStringLiteral( "unknown parent group" );
				return false;
			}
			if ( path.contains( current ) )
			{
				if ( error ) *error = QStringLiteral( "group cycle" );
				return false;
			}
			path.insert( current );
			current = parents.value( current );
		}
	}
	for ( const Object &object : objects )
	{
		if ( object.id <= 0 || ids.contains( object.id ) )
		{
			if ( error ) *error = QStringLiteral( "invalid or duplicate object ID" );
			return false;
		}
		ids.insert( object.id );
		int parent = 0;
		if ( !ParentId( object.editor, parent, error ) ) return false;
		if ( parent && !parents.contains( parent ) )
		{
			if ( error ) *error = QStringLiteral( "unknown parent group" );
			return false;
		}
		if ( parent && object.ownerEntityId )
		{
			if ( error ) *error = QStringLiteral( "entity-owned brush cannot be a direct group member" );
			return false;
		}
	}
	return true;
}
}
