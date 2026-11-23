#pragma once

#include "mapmodel.h"

#include <QSet>
#include <QStringList>

namespace HammerShoreline
{
	inline QSet<int> ReferencedFaces( const MapDocLite &map )
	{
		QSet<int> ids;
		const auto collect = [&ids]( const QString &value ) {
			for ( const QString &token : value.simplified().split( QLatin1Char( ' ' ), Qt::SkipEmptyParts ) )
			{
				bool ok = false;
				const int id = token.toInt( &ok );
				if ( ok && id > 0 ) ids.insert( id );
			}
		};
		for ( const EntityLite &entity : map.entities )
		{
			if ( !entity.shorelineOverlays ) continue;
			for ( auto key = entity.keyvalues.cbegin(); key != entity.keyvalues.cend(); ++key )
				if ( key.key().compare( QLatin1String( "sides" ), Qt::CaseInsensitive ) == 0 ||
					key.key().compare( QLatin1String( "sides2" ), Qt::CaseInsensitive ) == 0 )
					collect( key.value() );
			for ( const auto &overlay : *entity.shorelineOverlays )
				for ( const auto &pair : overlay )
					if ( pair.first.compare( QLatin1String( "sides" ), Qt::CaseInsensitive ) == 0 ) collect( pair.second );
		}
		return ids;
	}

	inline bool SelectionReferencesShoreline( const MapDocLite &map, const QSet<int> &selection )
	{
		const QSet<int> faces = ReferencedFaces( map );
		if ( faces.isEmpty() ) return false;
		for ( const SolidLite &solid : map.solids )
			if ( selection.contains( solid.id ) || ( solid.ownerEntityId && selection.contains( solid.ownerEntityId ) ) )
				for ( const SideLite &side : solid.sides )
					if ( faces.contains( side.id ) ) return true;
		return false;
	}
}
