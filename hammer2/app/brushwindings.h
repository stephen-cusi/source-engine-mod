//========= Copyright Valve Corporation, All rights reserved. ============//
#pragma once

// Adapted from Hammer's CreateWindingFromPlane/ClipWinding and
// CMapSolid::CreateFromPlanes (hammer/brushops.cpp, hammer/mapsolid.cpp).
// Builds display-only convex face polygons from VMF plane points. A failure
// leaves the map untouched and callers can fall back to bounds display.
#include "mapmodel.h"
#include <QList>
#include <array>
#include <cmath>

namespace HammerWinding
{
using Point = std::array<double, 3>;
struct Plane { Point normal; double dist; };
using Polygon = QList<Point>;
inline double Dot( const Point &a, const Point &b ) { return a[0]*b[0]+a[1]*b[1]+a[2]*b[2]; }
inline Point Cross( const Point &a, const Point &b ) { return { a[1]*b[2]-a[2]*b[1], a[2]*b[0]-a[0]*b[2], a[0]*b[1]-a[1]*b[0] }; }
inline bool Normalize( Point &p )
{
 const double n = std::sqrt( Dot(p,p) );
 if ( !std::isfinite(n) || n < 1e-10 ) return false;
 for ( double &x : p ) x /= n;
 return true;
}
inline bool BuildPlanes( const SolidLite &solid, QList<Plane> &planes )
{
 planes.clear();
 if ( solid.sides.size() < 4 || solid.sides.size() > 128 ) return false;
 for ( const SideLite &side : solid.sides )
 {
  Point a, b, p;
  for ( int k=0;k<3;++k ) { p[k]=side.planePts[0][k]; a[k]=side.planePts[1][k]-p[k]; b[k]=side.planePts[2][k]-p[k]; if (!std::isfinite(p[k]) || !std::isfinite(a[k]) || !std::isfinite(b[k])) return false; }
  Plane plane { Cross(b,a), 0 };
  if (!Normalize(plane.normal)) return false;
  plane.dist=Dot(plane.normal,p);
  if (!std::isfinite(plane.dist) || std::abs(plane.dist)>1048576.0) return false;
  for (const Plane &other : planes) if (Dot(plane.normal,other.normal)>0.999999999 && std::abs(plane.dist-other.dist)<1e-7) return false;
  planes.append(plane);
 }
 return true;
}
inline bool BuildFaces( const SolidLite &solid, QList<Polygon> &faces )
{
 QList<Plane> planes;
 faces.clear();
 if (!BuildPlanes(solid, planes)) return false;
 constexpr double radius=8388608.0;
 constexpr double epsilon=1e-7;
 for (int i=0;i<planes.size();++i)
 {
  const Plane &plane=planes[i];
  int major=0; for(int k=1;k<3;++k) if(std::abs(plane.normal[k])>std::abs(plane.normal[major])) major=k;
  Point up=major==2 ? Point{1,0,0} : Point{0,0,1};
  double along=Dot(up,plane.normal); for(int k=0;k<3;++k) up[k]-=along*plane.normal[k];
  if (!Normalize(up)) { faces.clear(); return false; }
  Point right=Cross(up,plane.normal), center;
  for(int k=0;k<3;++k) center[k]=plane.normal[k]*plane.dist;
  Polygon poly;
  for (const auto &sign : { std::array<int,2>{-1,1}, {1,1}, {1,-1}, {-1,-1} })
  { Point p; for(int k=0;k<3;++k) p[k]=center[k]+radius*(sign[0]*right[k]+sign[1]*up[k]); poly.append(p); }
  for(int j=0;j<planes.size() && !poly.isEmpty();++j)
  {
   if(i==j) continue;
   Polygon clipped;
   for(int k=0;k<poly.size();++k)
   {
    const Point &a=poly[k], &b=poly[(k+1)%poly.size()];
    double da=Dot(planes[j].normal,a)-planes[j].dist, db=Dot(planes[j].normal,b)-planes[j].dist;
    bool insideA=da<=epsilon, insideB=db<=epsilon;
    if(insideA) clipped.append(a);
    if(insideA != insideB)
    {
     const double t=da/(da-db); Point p;
     for(int axis=0;axis<3;++axis) p[axis]=a[axis]+t*(b[axis]-a[axis]);
     clipped.append(p);
    }
   }
   if(clipped.size()>256) { faces.clear(); return false; }
   poly=clipped;
  }
  // Clipping through existing vertices can emit adjacent duplicates. Remove
  // them before triangulation, then reject degenerate faces.
  for (int k=poly.size()-1;k>=0 && poly.size()>1;--k)
  {
   const Point &a=poly[k], &b=poly[(k+1)%poly.size()];
   const double dx=a[0]-b[0],dy=a[1]-b[1],dz=a[2]-b[2];
   if(dx*dx+dy*dy+dz*dz<1e-14) poly.removeAt(k);
  }
  if(poly.size()<3) { faces.clear(); return false; }
  bool hasArea=false;
  for(int n=1;n+1<poly.size();++n)
  {
   Point edge1, edge2;
   for(int k=0;k<3;++k) { edge1[k]=poly[n][k]-poly[0][k]; edge2[k]=poly[n+1][k]-poly[0][k]; }
   const Point cross=Cross(edge1,edge2); const double area2=Dot(cross,cross);
   if(!std::isfinite(area2)) { faces.clear(); return false; }
   if(area2>=1e-14) hasArea=true;
  }
  if(!hasArea) { faces.clear(); return false; }
  for(const Point &p : poly) for(double x : p) if(!std::isfinite(x) || std::abs(x)>1048576.0) { faces.clear(); return false; }
  faces.append(poly);
 }
 return true;
}
}
