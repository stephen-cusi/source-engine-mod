#pragma once

#include "displacementinspect.h"

namespace HammerDisplacementEdit
{
using HammerDisplacementInspect::Chunk;

inline int GridSize( const Chunk &chunk )
{
	for ( const auto &key : chunk.keys )
		if ( key.first.compare( QLatin1String( "power" ), Qt::CaseInsensitive ) == 0 )
		{
			bool ok = false;
			const int power = key.second.toInt( &ok );
			if ( ok && power >= 2 && power <= 4 ) return ( 1 << power ) + 1;
		}
	return 0;
}

inline bool ReadDistances( const Chunk &chunk, QList<double> &distances )
{
	distances.clear();
	if ( !HammerDisplacementInspect::InspectCanonical( chunk ) ) return false;
	const int size = GridSize( chunk );
	for ( const Chunk &section : chunk.children )
		if ( section.name.compare( QLatin1String( "distances" ), Qt::CaseInsensitive ) == 0 )
		{
			distances.resize( size * size );
			const QRegularExpression whitespace( QStringLiteral( "\\s+" ) );
			for ( const auto &row : section.keys )
			{
				const int rowIndex = row.first.mid( 3 ).toInt();
				const QStringList values = row.second.split( whitespace, Qt::SkipEmptyParts );
				for ( int column = 0; column < size; ++column )
					distances[rowIndex * size + column] = values[column].toDouble();
			}
			return true;
		}
	return false;
}

inline Chunk RepeatedRows( const QString &name, int rows, int columns, const QString &value )
{
	Chunk chunk;
	chunk.name = name;
	QStringList values;
	for ( int i = 0; i < columns; ++i ) values.append( value );
	const QString line = values.join( QLatin1Char( ' ' ) );
	for ( int row = 0; row < rows; ++row )
		chunk.keys.append( { QStringLiteral( "row%1" ).arg( row ), line } );
	return chunk;
}

inline Chunk CreateFlat( int power, const double *corner, const double *normal )
{
	const int size = ( 1 << power ) + 1;
	QStringList cornerText, normalText;
	for ( int axis = 0; axis < 3; ++axis )
	{
		cornerText.append( QString::number( corner[axis], 'g', 16 ) );
		normalText.append( QString::number( normal[axis], 'g', 9 ) );
	}
	const QString normalRow = normalText.join( QLatin1Char( ' ' ) );
	Chunk chunk;
	chunk.name = QStringLiteral( "dispinfo" );
	chunk.keys = {
		{ QStringLiteral( "power" ), QString::number( power ) },
		{ QStringLiteral( "startposition" ), QStringLiteral( "[%1]" ).arg( cornerText.join( QLatin1Char( ' ' ) ) ) },
		{ QStringLiteral( "flags" ), QStringLiteral( "0" ) },
		{ QStringLiteral( "elevation" ), QStringLiteral( "0" ) },
		{ QStringLiteral( "subdiv" ), QStringLiteral( "0" ) }
	};
	chunk.children = {
		RepeatedRows( QStringLiteral( "normals" ), size, size, normalRow ),
		RepeatedRows( QStringLiteral( "distances" ), size, size, QStringLiteral( "0" ) ),
		RepeatedRows( QStringLiteral( "offsets" ), size, size, QStringLiteral( "0 0 0" ) ),
		RepeatedRows( QStringLiteral( "offset_normals" ), size, size, normalRow ),
		RepeatedRows( QStringLiteral( "alphas" ), size, size, QStringLiteral( "0" ) ),
		RepeatedRows( QStringLiteral( "triangle_tags" ), size - 1, 2 * ( size - 1 ), QStringLiteral( "0" ) ),
		{ QStringLiteral( "allowed_verts" ), { { QStringLiteral( "10" ), QStringLiteral( "-1 -1 -1 -1 -1 -1 -1 -1 -1 -1" ) } }, {} }
	};
	return chunk;
}
} // namespace HammerDisplacementEdit
