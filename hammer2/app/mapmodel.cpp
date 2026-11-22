//========= Hammer Qt6 (hammer2/app) ==========================================
//
// Purpose: MapDocLite implementation. Contains the axis-aligned box generator
//          (six faces with vbsp-compatible plane windings and texture axes),
//          solid translation and small shared parsing/formatting helpers.
//
//          Note on the plane math: SSolid (hammer2/core) could not be reused
//          because CSSolid::Convert()/FromMapSolid() require CMapSolid, whose
//          translation unit is not part of hammer_core yet. The box winding
//          table below is therefore self-contained and uses VMF's winding.
//
//=============================================================================

#include "mapmodel.h"
#include "brushwindings.h"

#include <QtGlobal>
#include <QtMath>
#include <QRegularExpression>
#include <QStringList>

//-----------------------------------------------------------------------------
// SideLite
//-----------------------------------------------------------------------------
SideLite::SideLite()
	: id( 0 )
	, texture( QStringLiteral( "tools/toolsnodraw" ) )
	, uscale( 1.0f )
	, vscale( 1.0f )
	, rotation( 0.0f )
	, lightmapscale( 16 )
	, smoothing( 0 )
{
	for ( int i = 0; i < 3; ++i )
	{
		planePts[i][0] = 0.0;
		planePts[i][1] = 0.0;
		planePts[i][2] = 0.0;
	}
	uaxis[0] = 0.0f; uaxis[1] = 0.0f; uaxis[2] = 1.0f; uaxis[3] = 0.0f;
	vaxis[0] = 0.0f; vaxis[1] = 1.0f; vaxis[2] = 0.0f; vaxis[3] = 0.0f;
}

//-----------------------------------------------------------------------------
// SolidLite
//-----------------------------------------------------------------------------
SolidLite::SolidLite()
	: id( 0 )
{
	mins[0] = mins[1] = mins[2] = 0.0;
	maxs[0] = maxs[1] = maxs[2] = 0.0;
}

//-----------------------------------------------------------------------------
// EntityLite
//-----------------------------------------------------------------------------
EntityLite::EntityLite()
	: id( 0 )
	, angle( 0.0 )
{
	origin[0] = origin[1] = origin[2] = 0.0;
}

//-----------------------------------------------------------------------------
// MapDocLite
//-----------------------------------------------------------------------------
MapDocLite::MapDocLite()
	: m_nNextId( 2 )
{
}

int MapDocLite::AllocId()
{
	return m_nNextId++;
}

EntityLite *MapDocLite::EntityById( int id )
{
	for ( EntityLite &entity : entities )
	{
		if ( entity.id == id )
			return &entity;
	}
	return nullptr;
}

SolidLite *MapDocLite::SolidById( int id )
{
	for ( SolidLite &solid : solids )
	{
		if ( solid.id == id )
			return &solid;
	}
	return nullptr;
}

bool MapDocLite::RemoveById( int id )
{
	for ( int i = 0; i < entities.count(); ++i )
	{
		if ( entities[i].id == id )
		{
			entities.removeAt( i );
			return true;
		}
	}
	for ( int i = 0; i < solids.count(); ++i )
	{
		if ( solids[i].id == id )
		{
			solids.removeAt( i );
			return true;
		}
	}
	return false;
}

void MapDocLite::RecalcSolidBounds( SolidLite &solid )
{
	// VMF plane points define infinite planes and need not lie on the brush.
	// Derive bounds from the clipped convex faces, as Hammer does after
	// CreateFromPlanes. Keep the previous fallback for unsupported geometry.
	QList<HammerWinding::Polygon> faces;
	if ( HammerWinding::BuildFaces( solid, faces ) )
	{
		bool first = true;
		for ( const auto &face : faces )
			for ( const auto &point : face )
			{
				for ( int axis = 0; axis < 3; ++axis )
				{
					if ( first || point[axis] < solid.mins[axis] ) solid.mins[axis] = point[axis];
					if ( first || point[axis] > solid.maxs[axis] ) solid.maxs[axis] = point[axis];
				}
				first = false;
			}
		if ( !first ) return;
	}
	bool bFirst = true;
	for ( const SideLite &side : solid.sides )
	{
		for ( int i = 0; i < 3; ++i )
		{
			for ( int axis = 0; axis < 3; ++axis )
			{
				const double v = side.planePts[i][axis];
				if ( bFirst || v < solid.mins[axis] )
					solid.mins[axis] = v;
				if ( bFirst || v > solid.maxs[axis] )
					solid.maxs[axis] = v;
			}
			bFirst = false;
		}
	}
}

QString MapDocLite::WorldspawnValue( const QString &key ) const
{
	return worldspawn.value( key );
}

void MapDocLite::SetWorldspawnValue( const QString &key, const QString &value )
{
	worldspawn.insert( key, value );
}

//-----------------------------------------------------------------------------
// Purpose: Builds the six sides of an axis-aligned box.
//
//          Winding table: for each face the three plane points are ordered so
//          that cross(p3-p1, p2-p1) is the outward normal used by VBSP.
//          Bounds below are (x0,y0,z0)..(x1,y1,z1).
//-----------------------------------------------------------------------------
QList<SideLite> MakeBoxSides( const double mins[3], const double maxs[3],
	const QString &texture )
{
	const double x0 = mins[0], y0 = mins[1], z0 = mins[2];
	const double x1 = maxs[0], y1 = maxs[1], z1 = maxs[2];

	struct FaceDef
	{
		double pts[3][3];
		float u[4];
		float v[4];
	};

	// Texture axes are chosen tangential to each face (never parallel to the
	// face normal) so vbsp accepts the texinfo without axis errors.
	const FaceDef faces[6] =
	{
		// +X face
		{ { { x1, y1, z1 }, { x1, y0, z1 }, { x1, y1, z0 } },
		  { 0.0f, 0.0f, 1.0f, 0.0f }, { 0.0f, 1.0f, 0.0f, 0.0f } },
		// -X face
		{ { { x0, y0, z0 }, { x0, y0, z1 }, { x0, y1, z1 } },
		  { 0.0f, 0.0f, 1.0f, 0.0f }, { 0.0f, 1.0f, 0.0f, 0.0f } },
		// +Y face
		{ { { x0, y1, z1 }, { x1, y1, z1 }, { x0, y1, z0 } },
		  { 1.0f, 0.0f, 0.0f, 0.0f }, { 0.0f, 0.0f, 1.0f, 0.0f } },
		// -Y face
		{ { { x0, y0, z0 }, { x1, y0, z0 }, { x1, y0, z1 } },
		  { 1.0f, 0.0f, 0.0f, 0.0f }, { 0.0f, 0.0f, 1.0f, 0.0f } },
		// +Z face (floor view from above)
		{ { { x0, y1, z1 }, { x0, y0, z1 }, { x1, y0, z1 } },
		  { 1.0f, 0.0f, 0.0f, 0.0f }, { 0.0f, 1.0f, 0.0f, 0.0f } },
		// -Z face
		{ { { x0, y0, z0 }, { x0, y1, z0 }, { x1, y1, z0 } },
		  { 1.0f, 0.0f, 0.0f, 0.0f }, { 0.0f, 1.0f, 0.0f, 0.0f } },
	};

	QList<SideLite> sides;
	for ( int i = 0; i < 6; ++i )
	{
		SideLite side;
		side.id = 0; // assigned by the caller / document
		// FaceDef uses the conventional outward cross product; VMF's plane
		// convention uses the opposite order.
		const int pointOrder[3] = { 0, 2, 1 };
		for ( int p = 0; p < 3; ++p )
		{
			side.planePts[p][0] = faces[i].pts[pointOrder[p]][0];
			side.planePts[p][1] = faces[i].pts[pointOrder[p]][1];
			side.planePts[p][2] = faces[i].pts[pointOrder[p]][2];
		}
		for ( int a = 0; a < 4; ++a )
		{
			side.uaxis[a] = faces[i].u[a];
			side.vaxis[a] = faces[i].v[a];
		}
		side.texture = texture;
		sides.append( side );
	}
	return sides;
}

int CreateBoxSolid( MapDocLite &doc, const double mins[3], const double maxs[3],
	const QString &texture )
{
	// Reject degenerate (zero volume) boxes: vbsp would reject them anyway
	for ( int axis = 0; axis < 3; ++axis )
	{
		if ( maxs[axis] - mins[axis] <= 0.0 )
			return -1;
	}

	SolidLite solid;
	solid.id = doc.AllocId();
	solid.sides = MakeBoxSides( mins, maxs, texture );

	// Faces share the document id counter just like entities and solids
	for ( SideLite &side : solid.sides )
		side.id = doc.AllocId();

	doc.RecalcSolidBounds( solid );
	doc.solids.append( solid );
	return solid.id;
}

void TranslateSolid( SolidLite &solid, const double delta[3] )
{
	for ( SideLite &side : solid.sides )
	{
		for ( QString &vertex : side.verticesPlus )
		{
			double pos[3];
			if ( ParseVec3( vertex, pos ) )
			{
				for ( int axis = 0; axis < 3; ++axis ) pos[axis] += delta[axis];
				vertex = QStringLiteral( "%1 %2 %3" ).arg( QString::number( pos[0], 'g', 17 ),
					QString::number( pos[1], 'g', 17 ), QString::number( pos[2], 'g', 17 ) );
			}
		}
		for ( int p = 0; p < 3; ++p )
		{
			side.planePts[p][0] += delta[0];
			side.planePts[p][1] += delta[1];
			side.planePts[p][2] += delta[2];
		}
	}
	solid.mins[0] += delta[0];
	solid.mins[1] += delta[1];
	solid.mins[2] += delta[2];
	solid.maxs[0] += delta[0];
	solid.maxs[1] += delta[1];
	solid.maxs[2] += delta[2];
}

double SnapCoord( double value, int gridSize )
{
	if ( gridSize <= 0 )
		return value;
	const double snapped = qRound64( value / gridSize ) * gridSize;
	return snapped;
}

QString FormatVmfNumber( double value )
{
	// Whole numbers print without decimals (vbsp's sscanf accepts either form);
	// fractional numbers print with up to six decimals, trailing zeros trimmed.
	// NaN/Inf would make qRound64 undefined and produce text vbsp cannot parse
	if ( !qIsFinite( value ) )
		return QStringLiteral( "0" );
	if ( qAbs( value ) >= 1.0e15 )
		return QString::number( value, 'f', 0 );

	if ( qFuzzyIsNull( value - qRound64( value ) ) )
		return QString::number( qRound64( value ) );

	QString str = QString::number( value, 'f', 6 );
	while ( str.endsWith( QLatin1Char( '0' ) ) )
		str.chop( 1 );
	if ( str.endsWith( QLatin1Char( '.' ) ) )
		str.chop( 1 );
	return str;
}

bool ParseVec3( const QString &text, double out[3] )
{
	const QStringList parts = text.trimmed().split( QRegularExpression( QStringLiteral( "\\s+" ) ),
		Qt::SkipEmptyParts );
	if ( parts.count() != 3 )
		return false;

	bool bOk[3] = { false, false, false };
	out[0] = parts[0].toDouble( &bOk[0] );
	out[1] = parts[1].toDouble( &bOk[1] );
	out[2] = parts[2].toDouble( &bOk[2] );
	return bOk[0] && bOk[1] && bOk[2] && qIsFinite( out[0] ) && qIsFinite( out[1] ) && qIsFinite( out[2] );
}

QString FormatVec3( const double v[3] )
{
	return QStringLiteral( "%1 %2 %3" )
		.arg( FormatVmfNumber( v[0] ), FormatVmfNumber( v[1] ), FormatVmfNumber( v[2] ) );
}
