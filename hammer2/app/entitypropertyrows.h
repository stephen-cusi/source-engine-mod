#pragma once

#include <QList>
#include <QMap>
#include <QString>

struct EntityPropertyRow
{
	QString key;
	QString caption;
	QString value;
};

// Keep existing spelling and unknown keys while showing unset FGD properties.
inline QList<EntityPropertyRow> EntityPropertyRows( const QMap<QString, QString> &values,
	const QMap<QString, QString> &schema )
{
	QList<EntityPropertyRow> rows;
	for ( auto it = values.constBegin(); it != values.constEnd(); ++it )
	{
		if ( it.key().compare( QLatin1String( "classname" ), Qt::CaseInsensitive ) == 0 ||
			it.key().compare( QLatin1String( "origin" ), Qt::CaseInsensitive ) == 0 )
			continue;
		QString caption = it.key();
		for ( auto field = schema.constBegin(); field != schema.constEnd(); ++field )
			if ( field.key().compare( it.key(), Qt::CaseInsensitive ) == 0 ) { caption = field.value(); break; }
		rows.append( { it.key(), caption, it.value() } );
	}
	for ( auto field = schema.constBegin(); field != schema.constEnd(); ++field )
	{
		if ( field.key().compare( QLatin1String( "classname" ), Qt::CaseInsensitive ) == 0 ||
			field.key().compare( QLatin1String( "origin" ), Qt::CaseInsensitive ) == 0 )
			continue;
		bool found = false;
		for ( const EntityPropertyRow &row : rows )
			if ( row.key.compare( field.key(), Qt::CaseInsensitive ) == 0 ) { found = true; break; }
		if ( !found ) rows.append( { field.key(), field.value(), QString() } );
	}
	return rows;
}
