#pragma once

#include "displacementmesh.h"

namespace HammerDisplacementPaint
{
enum Effect { RaiseLower, RaiseTo, Smooth };

inline double DistanceSquared( const HammerWinding::Point &a, const HammerWinding::Point &b )
{
	return ( a[0] - b[0] ) * ( a[0] - b[0] ) +
		( a[1] - b[1] ) * ( a[1] - b[1] ) +
		( a[2] - b[2] ) * ( a[2] - b[2] );
}

// Spatial Paint Geometry follows Hammer's spherical brush and squared soft
// falloff. This port paints along the stored displacement vectors.
inline QList<double> Dab( const HammerDisplacementMesh::Mesh &mesh, const QList<double> &original,
	const HammerWinding::Point &center, Effect effect, double radius, double amount, bool soft )
{
	if ( !std::isfinite( radius ) || radius <= 0 || !std::isfinite( amount ) ||
		original.size() != mesh.vertices.size() ) return original;

	QList<double> result = original;
	const double radiusSquared = radius * radius;
	for ( int i = 0; i < mesh.vertices.size(); ++i )
	{
		const double distance = DistanceSquared( center, mesh.vertices[i] );
		if ( distance >= radiusSquared ) continue;
		const double falloff = 1.0 - distance / radiusSquared;
		double value = original[i];
		if ( effect == RaiseLower )
			value += amount * ( soft ? falloff : 1.0 );
		else if ( effect == RaiseTo )
			value = amount;
		else if ( effect == Smooth )
		{
			// Use the pre-dab mesh for all neighbors. Avoid the original
			// division by zero when the smoothing sphere collapses.
			const double localRadiusSquared = radiusSquared * falloff * falloff;
			if ( localRadiusSquared <= 1e-12 ) continue;
			double weightedDelta = 0.0;
			double weights = 1.0;
			for ( int j = 0; j < mesh.vertices.size(); ++j )
			{
				if ( i == j ) continue;
				const double ratio = DistanceSquared( mesh.vertices[i], mesh.vertices[j] ) / localRadiusSquared;
				if ( ratio >= 1.0 ) continue;
				const double weight = std::exp( -ratio ) / ( 2.0 * std::max( 1.0, amount ) );
				weightedDelta += ( original[j] - original[i] ) * weight;
				weights += weight;
			}
			value += weightedDelta / weights;
		}
		if ( std::isfinite( value ) && std::abs( value ) <= 32768.0 ) result[i] = value;
	}
	return result;
}
}
