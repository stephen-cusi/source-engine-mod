#pragma once

// Structural validation for displacement blocks emitted or read by original
// Hammer. Preserve ordered key/value data without treating structural
// validity as a guarantee that the base brush can generate terrain.

#include <QList>
#include <QPair>
#include <QRegularExpression>
#include <QSet>
#include <QString>
#include <QStringList>
#include <cmath>

namespace HammerDisplacementInspect
{
struct Chunk
{
	QString name;
	QList<QPair<QString, QString>> keys;
	QList<Chunk> children;
};

inline bool InspectCanonical( const Chunk &root, QString *errorOut = nullptr )
{
	const auto fail = [errorOut]( const QString &message ) {
		if ( errorOut ) *errorOut = message;
		return false;
	};
	if ( errorOut ) errorOut->clear();
	if ( root.name.compare( QLatin1String( "dispinfo" ), Qt::CaseInsensitive ) != 0 ) return fail( QStringLiteral( "expected dispinfo" ) );
	QSet<QString> scalarNames;
	int power = -1;
	bool hasStart = false;
	const QRegularExpression whitespace( QStringLiteral( "\\s+" ) );
	const QRegularExpression nonRowCharacter( QStringLiteral( "[^0-9eE+ .-]" ) );
	const auto numbers = [&whitespace]( const QString &s, int count ) {
		const QStringList tokens = s.trimmed().split( whitespace, Qt::SkipEmptyParts );
		if ( tokens.size() != count ) return false;
		for ( const QString &token : tokens ) { bool ok = false; const float n = token.toFloat( &ok ); if ( !ok || !std::isfinite( n ) ) return false; }
		return true;
	};
	for ( const auto &key : root.keys )
	{
		const QString name = key.first.toLower();
		if ( scalarNames.contains( name ) ) return fail( QStringLiteral( "duplicate scalar: %1" ).arg( name ) );
		scalarNames.insert( name );
		if ( name == QLatin1String( "power" ) )
		{
			bool ok = false; power = key.second.toInt( &ok );
			if ( !ok || power < 2 || power > 4 ) return fail( QStringLiteral( "invalid power" ) );
		}
		else if ( name == QLatin1String( "startposition" ) )
		{
			QString s = key.second.trimmed();
			if ( s.startsWith( QLatin1Char( '[' ) ) && s.endsWith( QLatin1Char( ']' ) ) ) s = s.mid( 1, s.size() - 2 );
			if ( !numbers( s, 3 ) ) return fail( QStringLiteral( "invalid startposition" ) );
			hasStart = true;
		}
		else if ( name == QLatin1String( "uaxis" ) || name == QLatin1String( "vaxis" ) )
		{
			if ( !numbers( key.second, 3 ) ) return fail( QStringLiteral( "invalid mapping axis" ) );
		}
		else if ( name == QLatin1String( "elevation" ) || name == QLatin1String( "smooth" ) )
		{
			if ( !numbers( key.second, 1 ) ) return fail( QStringLiteral( "invalid scalar: %1" ).arg( name ) );
		}
		else if ( name == QLatin1String( "flags" ) || name == QLatin1String( "subdiv" ) || name == QLatin1String( "mintess" ) )
		{
			bool ok = false; key.second.toInt( &ok ); if ( !ok ) return fail( QStringLiteral( "invalid integer: %1" ).arg( name ) );
		}
		else return fail( QStringLiteral( "unknown scalar: %1" ).arg( name ) );
	}
	if ( power < 0 || !hasStart ) return fail( QStringLiteral( "missing power or startposition" ) );
	const int size = ( 1 << power ) + 1;
	QSet<QString> seen;
	for ( const Chunk &section : root.children )
	{
		const QString name = section.name.toLower();
		if ( seen.contains( name ) || !section.children.isEmpty() ) return fail( QStringLiteral( "duplicate or nested section: %1" ).arg( name ) );
		seen.insert( name );
		if ( name == QLatin1String( "allowed_verts" ) )
		{
			if ( section.keys.size() != 1 || section.keys.first().first != QLatin1String( "10" ) ) return fail( QStringLiteral( "invalid allowed_verts key" ) );
			if ( section.keys.first().second.size() >= 1024 ) return fail( QStringLiteral( "allowed_verts exceeds VMF keyvalue length" ) );
			const QStringList values = section.keys.first().second.trimmed().split( whitespace, Qt::SkipEmptyParts );
			if ( values.size() != 10 ) return fail( QStringLiteral( "invalid allowed_verts length" ) );
			for ( const auto &value : values ) { bool ok = false; const qlonglong n = value.toLongLong( &ok ); if ( !ok || n < -2147483648LL || n > 4294967295LL ) return fail( QStringLiteral( "invalid allowed_verts value" ) ); }
			continue;
		}
		const bool vectors = name == QLatin1String( "normals" ) || name == QLatin1String( "offsets" ) || name == QLatin1String( "offset_normals" );
		const bool scalars = name == QLatin1String( "distances" ) || name == QLatin1String( "alphas" );
		const bool triangles = name == QLatin1String( "triangle_tags" );
		if ( !vectors && !scalars && !triangles ) return fail( QStringLiteral( "unknown section: %1" ).arg( name ) );
		const int rows = triangles ? size - 1 : size;
		const int columns = triangles ? 2 * ( size - 1 ) : vectors ? 3 * size : size;
		if ( section.keys.size() != rows ) return fail( QStringLiteral( "wrong row count: %1" ).arg( name ) );
		QSet<int> rowIds;
		const QRegularExpression rowName( QStringLiteral( "^row(0|[1-9][0-9]*)$" ), QRegularExpression::CaseInsensitiveOption );
		for ( const auto &row : section.keys )
		{
			const auto match = rowName.match( row.first );
			bool ok = false; const int index = match.hasMatch() ? match.captured( 1 ).toInt( &ok ) : -1;
			if ( !ok || index < 0 || index >= rows || rowIds.contains( index ) ) return fail( QStringLiteral( "invalid row: %1" ).arg( name ) );
			rowIds.insert( index );
			// Source's row readers split on ASCII spaces and use the
			// CChunkFile 1024-byte keyvalue buffer.
			if ( row.second.size() >= 1024 || row.second.contains( nonRowCharacter ) )
				return fail( QStringLiteral( "unsupported row text: %1" ).arg( name ) );
			if ( !numbers( row.second, columns ) ) return fail( QStringLiteral( "invalid values: %1" ).arg( name ) );
			if ( triangles ) for ( const auto &value : row.second.trimmed().split( whitespace, Qt::SkipEmptyParts ) )
			{ bool integer = false; const uint tag = value.toUInt( &integer ); if ( !integer || tag > 65535 ) return fail( QStringLiteral( "invalid triangle tag" ) ); }
		}
	}
	if ( !seen.contains( QStringLiteral( "normals" ) ) || !seen.contains( QStringLiteral( "distances" ) ) ) return fail( QStringLiteral( "missing normals or distances" ) );
	return true;
}
} // namespace HammerDisplacementInspect
