#pragma once

#include <QFile>
#include <QList>
#include <QRegularExpression>
#include <QString>
#include <QTextStream>
#include <cmath>

// VBSP leak paths (.lin, and the older .pts) contain one XYZ point per line.
struct PointFilePoint
{
	double x, y, z;
	double Axis( int axis ) const { return axis == 0 ? x : axis == 1 ? y : z; }
};

// Parse into a temporary list so failure never destroys an existing path.
// errorLine is zero on I/O failure and otherwise the invalid one-based line.
inline bool ReadPointFile( const QString &path, QList<PointFilePoint> &points, int *errorLine = nullptr )
{
	if ( errorLine ) *errorLine = 0;
	QFile file( path );
	if ( !file.open( QIODevice::ReadOnly | QIODevice::Text ) ) return false;
	QTextStream stream( &file );
	QList<PointFilePoint> parsed;
	int lineNumber = 0;
	while ( !stream.atEnd() )
	{
		const QString line = stream.readLine().trimmed();
		++lineNumber;
		if ( line.isEmpty() ) continue;
		const QStringList parts = line.split( QRegularExpression( QStringLiteral( "\\s+" ) ), Qt::SkipEmptyParts );
		bool xOk = false, yOk = false, zOk = false;
		PointFilePoint point{};
		if ( parts.size() == 3 )
		{
			point.x = parts[0].toDouble( &xOk );
			point.y = parts[1].toDouble( &yOk );
			point.z = parts[2].toDouble( &zOk );
		}
		if ( !xOk || !yOk || !zOk || !std::isfinite( point.x ) || !std::isfinite( point.y ) || !std::isfinite( point.z ) )
		{
			if ( errorLine ) *errorLine = lineNumber;
			return false;
		}
		parsed.append( point );
	}
	// Empty compiler output is not a leak path. Treat it as a failed load so a
	// previously displayed path is not silently replaced by an empty list.
	if ( stream.status() != QTextStream::Ok || parsed.isEmpty() ) return false;
	points = parsed;
	return true;
}
