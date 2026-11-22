#pragma once

#include "brushwindings.h"
#include <QRegularExpression>
#include <limits>

namespace HammerOverlay
{
using Point = HammerWinding::Point;
struct Data
{
	Point origin{}, u{}, v{}, normal{};
	std::array<Point, 4> points{};
	QList<int> sides;
	double startU = 0, endU = 1, startV = 0, endV = 1;
	QString material;
	int renderOrder = 0;
};

inline QString Value( const EntityLite &entity, const QString &key, const QString &fallback = {} )
{
	for ( auto it = entity.keyvalues.cbegin(); it != entity.keyvalues.cend(); ++it )
		if ( it.key().compare( key, Qt::CaseInsensitive ) == 0 ) return it.value();
	return fallback;
}

inline void Set( EntityLite &entity, const QString &key, const QString &value )
{
	for ( auto it = entity.keyvalues.begin(); it != entity.keyvalues.end(); ++it )
		if ( it.key().compare( key, Qt::CaseInsensitive ) == 0 ) { it.value() = value; return; }
	entity.keyvalues.insert( key, value );
}

inline bool Parse( const EntityLite &entity, Data &out )
{
	if ( entity.classname.compare( QLatin1String( "info_overlay" ), Qt::CaseInsensitive ) != 0 ) return false;
	Data data;
	const auto vec = [&]( const QString &key, Point &point ) {
		if ( !ParseVec3( Value( entity, key ), point.data() ) ) return false;
		for ( double component : point )
			if ( !std::isfinite( component ) || std::abs( component ) > (std::numeric_limits<float>::max)() ) return false;
		return true;
	};
	if ( !vec( QStringLiteral( "BasisOrigin" ), data.origin ) || !vec( QStringLiteral( "BasisU" ), data.u ) ||
		 !vec( QStringLiteral( "BasisV" ), data.v ) || !vec( QStringLiteral( "BasisNormal" ), data.normal ) ) return false;
	for ( const Point &axis : { data.u, data.v, data.normal } )
		if ( std::abs( HammerWinding::Dot( axis, axis ) - 1 ) > 0.01 ) return false;
	if ( std::abs( HammerWinding::Dot( data.u, data.v ) ) > 0.01 ||
		 std::abs( HammerWinding::Dot( data.u, data.normal ) ) > 0.01 ||
		 std::abs( HammerWinding::Dot( data.v, data.normal ) ) > 0.01 ) return false;
	for ( int i = 0; i < 4; ++i )
		if ( !vec( QStringLiteral( "uv%1" ).arg( i ), data.points[i] ) ) return false;
	bool ok = false;
	data.renderOrder = Value( entity, QStringLiteral( "RenderOrder" ), QStringLiteral( "0" ) ).toInt( &ok );
	if ( !ok || data.renderOrder < 0 || data.renderOrder > 3 ) return false;
	data.material = Value( entity, QStringLiteral( "material" ) );
	if ( data.material.trimmed().isEmpty() ) return false;
	const auto read = [&]( const QString &key, const QString &fallback, double &number ) {
		number = Value( entity, key, fallback ).toDouble( &ok );
		return ok && std::isfinite( number ) && std::abs( number ) <= (std::numeric_limits<float>::max)();
	};
	if ( !read( QStringLiteral( "StartU" ), QStringLiteral( "0" ), data.startU ) ||
		 !read( QStringLiteral( "EndU" ), QStringLiteral( "1" ), data.endU ) ||
		 !read( QStringLiteral( "StartV" ), QStringLiteral( "0" ), data.startV ) ||
		 !read( QStringLiteral( "EndV" ), QStringLiteral( "1" ), data.endV ) ) return false;
	const auto sides = Value( entity, QStringLiteral( "sides" ) ).split(
		QRegularExpression( QStringLiteral( "\\s+" ) ), Qt::SkipEmptyParts );
	for ( const QString &side : sides )
	{
		const int id = side.toInt( &ok );
		if ( !ok || id <= 0 || data.sides.contains( id ) ) return false;
		data.sides.append( id );
	}
	if ( data.sides.isEmpty() ) return false;
	out = data;
	return true;
}

inline Point WorldPoint( const Data &data, int index )
{
	Point result = data.origin;
	for ( int axis = 0; axis < 3; ++axis )
		result[axis] += data.u[axis] * data.points[index][0] + data.v[axis] * data.points[index][1];
	return result;
}

inline bool Convex( const Data &data )
{
	double sign = 0.0;
	for ( int i = 0; i < 4; ++i )
	{
		const Point &a = data.points[i], &b = data.points[(i+1)%4], &c = data.points[(i+2)%4];
		const double cross = (b[0]-a[0])*(c[1]-b[1]) - (b[1]-a[1])*(c[0]-b[0]);
		if ( !std::isfinite( cross ) || std::abs( cross ) < 1e-6 || sign * cross < 0 ) return false;
		sign = cross;
	}
	return true;
}
}
