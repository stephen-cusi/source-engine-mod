#pragma once

#include "document.h"
#include "brushentityview.h"
#include <QRegularExpression>
#include <cmath>

// Parse the space-delimited XYZ form accepted by original Hammer.
inline bool ParseNavigationCoordinates( const QString &text, double point[3] )
{
	const QStringList fields = text.trimmed().split( QRegularExpression( QStringLiteral( "\\s+" ) ), Qt::SkipEmptyParts );
	if ( fields.size() != 3 ) return false;
	double parsed[3];
	for ( int i = 0; i < 3; ++i )
	{
		bool valid = false;
		parsed[i] = fields[i].toDouble( &valid );
		if ( !valid || !std::isfinite( parsed[i] ) ) return false;
	}
	for ( int i = 0; i < 3; ++i ) point[i] = parsed[i];
	return true;
}

// Source console output uses player origin, followed by pitch/yaw/roll.
// Commit parsed output only after all fields are validated.
inline bool ParseNavigationCommand( const QString &text, double position[3], double angles[3] )
{
	const QStringList sections = text.split( QLatin1Char( ';' ) );
	if ( sections.size() != 2 ) return false;
	const auto parseSection = []( const QString &section, const char *command, double output[3] ) {
		const QStringList fields = section.trimmed().split( QRegularExpression( QStringLiteral( "\\s+" ) ), Qt::SkipEmptyParts );
		if ( fields.size() != 4 || fields[0].compare( QLatin1String( command ), Qt::CaseInsensitive ) != 0 ) return false;
		for ( int i = 0; i < 3; ++i )
		{
			bool valid = false;
			output[i] = fields[i + 1].toDouble( &valid );
			if ( !valid || !std::isfinite( output[i] ) ) return false;
		}
		return true;
	};
	double parsedPosition[3], parsedAngles[3];
	if ( !parseSection( sections[0], "setpos", parsedPosition ) ||
		 !parseSection( sections[1], "setang", parsedAngles ) ) return false;
	for ( int i = 0; i < 3; ++i ) { position[i] = parsedPosition[i]; angles[i] = parsedAngles[i]; }
	return true;
}

// IDs are VMF solid IDs, not the ordinal position in the document.
inline bool SelectBrushById( CHammerDocument &document, int id )
{
	const SolidLite *solid = document.Map().SolidById( id );
	if ( !solid || !HammerBrushEntityView::Visible( document.Map(), document.GetViewFilter(), *solid ) ) return false;
	const int selectionId = HammerBrushEntityView::SelectionId( *solid );
	document.SelectObjects( QSet<int>() << selectionId );
	return document.IsSelected( selectionId );
}
