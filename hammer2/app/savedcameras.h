#pragma once

#include "mapmodel.h"
#include <cmath>

inline int SavedCameraActiveIndex( const MapDocLite &map )
{
	int active = -1;
	for ( const auto &key : map.cameraProperties )
		if ( key.first.compare( QLatin1String( "activecamera" ), Qt::CaseInsensitive ) == 0 )
		{
			bool ok = false;
			const int parsed = key.second.toInt( &ok );
			active = ok && parsed >= 0 && parsed < map.cameras.size() ? parsed : -1;
		}
	return active;
}

// Hammer's VMF cameras contain position and look-at points, not angles.
inline bool ReadSavedCameraPoints( const CameraLite &camera, double eye[3], double target[3] )
{
	QString position, look;
	for ( const auto &key : camera.keyvalues )
	{
		if ( key.first.compare( QLatin1String( "position" ), Qt::CaseInsensitive ) == 0 ) position = key.second;
		if ( key.first.compare( QLatin1String( "look" ), Qt::CaseInsensitive ) == 0 ) look = key.second;
	}
	const auto parse = []( QString value, double result[3] ) {
		value = value.trimmed();
		if ( value.startsWith( QLatin1Char( '[' ) ) && value.endsWith( QLatin1Char( ']' ) ) ) value = value.mid( 1, value.size() - 2 );
		return ParseVec3( value, result );
	};
	return parse( position, eye ) && parse( look, target );
}

inline bool ReadSavedCamera( const CameraLite &camera, double eye[3], double angles[3] )
{
	double target[3];
	if ( !ReadSavedCameraPoints( camera, eye, target ) ) return false;
	const double dx = target[0] - eye[0], dy = target[1] - eye[1], dz = target[2] - eye[2];
	const double xy = std::hypot( dx, dy ), length = std::hypot( xy, dz );
	if ( !std::isfinite( length ) || length < 1e-9 ) return false;
	constexpr double radToDegrees = 57.2957795130823208768;
	angles[0] = -std::atan2( dz, xy ) * radToDegrees;
	angles[1] = std::atan2( dy, dx ) * radToDegrees;
	angles[2] = 0;
	return true;
}

inline CameraLite MakeSavedCamera( const double eye[3], const double forward[3] )
{
	CameraLite camera;
	double target[3];
	for ( int i = 0; i < 3; ++i ) target[i] = eye[i] + forward[i] * 128.0;
	camera.keyvalues.append( qMakePair( QStringLiteral( "position" ), QStringLiteral( "[%1]" ).arg( FormatVec3( eye ) ) ) );
	camera.keyvalues.append( qMakePair( QStringLiteral( "look" ), QStringLiteral( "[%1]" ).arg( FormatVec3( target ) ) ) );
	return camera;
}
