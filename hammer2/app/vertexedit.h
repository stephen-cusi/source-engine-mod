#pragma once

// Conservative plane-based vertex editing. A 2D handle represents every
// vertex sharing its projected coordinates. Faces and vertices must keep the
// same topology; unsupported or non-convex results are rejected.
#include "brushwindings.h"
#include <algorithm>

namespace HammerVertexEdit
{
using namespace HammerWinding;
inline bool Near( double a, double b ) { return std::abs( a - b ) < 1e-4; }
inline bool Same( const Point &a, const Point &b )
{ return Near( a[0], b[0] ) && Near( a[1], b[1] ) && Near( a[2], b[2] ); }

inline bool MoveProjected( const SolidLite &source, int u, int v, double fromU, double fromV,
                           double toU, double toV, SolidLite &result )
{
 if ( u < 0 || u > 2 || v < 0 || v > 2 || u == v ||
      !std::isfinite( fromU ) || !std::isfinite( fromV ) ||
      !std::isfinite( toU ) || !std::isfinite( toV ) ||
      std::abs( toU ) > 1048576 || std::abs( toV ) > 1048576 ) return false;
 if ( Near( fromU, toU ) && Near( fromV, toV ) ) return false;
 for ( const SideLite &s : source.sides ) if ( s.hasVerticesPlus ) return false;
 QList<Polygon> faces; QList<Plane> planes;
 if ( !BuildFaces( source, faces ) || !BuildPlanes( source, planes ) ) return false;
 QList<Point> oldVertices, expected;
 for ( const Polygon &face : faces ) for ( const Point &p : face )
 {
  bool found = false; for ( const Point &q : oldVertices ) if ( Same( p, q ) ) { found = true; break; }
  if ( !found ) oldVertices.append( p );
 }
 bool moved = false;
 for ( Point p : oldVertices )
 {
  if ( Near( p[u], fromU ) && Near( p[v], fromV ) ) { p[u] = toU; p[v] = toV; moved = true; }
  for ( const Point &q : expected ) if ( Same( p, q ) ) return false;
  expected.append( p );
 }
 if ( !moved ) return false;
 result = source;
 for ( int i = 0; i < faces.size(); ++i )
 {
  Polygon face = faces[i];
  for ( Point &p : face ) if ( Near( p[u], fromU ) && Near( p[v], fromV ) ) { p[u] = toU; p[v] = toV; }
  Point normal{}; int second = -1, third = -1;
  for ( int a = 1; a < face.size() && second < 0; ++a ) for ( int b = a+1; b < face.size(); ++b )
  {
   Point x, y; for ( int k=0;k<3;++k ) { x[k]=face[a][k]-face[0][k]; y[k]=face[b][k]-face[0][k]; }
   normal = Cross( y, x ); if ( Normalize( normal ) ) { second=a; third=b; break; }
  }
  if ( second < 0 ) return false;
  // Keep winding outward; reversal or collapse of a face is not supported.
  if ( Dot( normal, planes[i].normal ) < 0 ) { std::swap( second, third ); for ( double &x : normal ) x = -x; }
  if ( Dot( normal, planes[i].normal ) < 1e-6 ) return false;
  const double dist = Dot( normal, face[0] );
  for ( const Point &p : face ) if ( std::abs( Dot( normal, p ) - dist ) > 1e-4 ) return false;
  // A valid geometric plane may make formerly usable texture axes parallel
  // to that plane's normal, or collapse their area in the plane. VBSP cannot
  // safely map such a face. Preserve UV data, but reject a degenerate result.
  const SideLite &side = source.sides[i];
  if ( !std::isfinite( side.uscale ) || !std::isfinite( side.vscale ) ||
       side.uscale == 0 || side.vscale == 0 ) return false;
  Point uAxis = { side.uaxis[0], side.uaxis[1], side.uaxis[2] };
  Point vAxis = { side.vaxis[0], side.vaxis[1], side.vaxis[2] };
  if ( !Normalize( uAxis ) || !Normalize( vAxis ) ||
       !std::isfinite( side.uaxis[3] ) || !std::isfinite( side.vaxis[3] ) ||
       std::abs( Dot( Cross( uAxis, vAxis ), normal ) ) < 1e-6 ) return false;
  const int indices[3] = { 0, second, third };
  for ( int j=0;j<3;++j ) for ( int k=0;k<3;++k ) result.sides[i].planePts[j][k] = face[indices[j]][k];
 }
 QList<Polygon> rebuilt;
 if ( !BuildFaces( result, rebuilt ) || rebuilt.size() != faces.size() ) return false;
 QList<Point> actual;
 for ( int i=0;i<rebuilt.size();++i )
 {
  if ( rebuilt[i].size() != faces[i].size() ) return false;
  for ( const Point &p : rebuilt[i] )
  {
   bool found = false; for ( const Point &q : expected ) if ( Same( p, q ) ) { found = true; break; }
   if ( !found ) return false;
   bool seen = false; for ( const Point &q : actual ) if ( Same( p, q ) ) { seen = true; break; }
   if ( !seen ) actual.append( p );
  }
 }
 return actual.size() == expected.size();
}
}
