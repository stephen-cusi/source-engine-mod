//========= Hammer Qt6 (hammer2/app) ==========================================
//
// Purpose: MapDocLite - lightweight, MFC-free map data model used by the Qt6
//          Hammer editor. Holds a worldspawn key/value table, a list of point
//          entities and a list of world brushes (axis-aligned solids made of
//          six planes each). This model is deliberately independent of
//          hammer2/core so the editor skeleton can run, render and save/load
//          VMF without the full CMapDoc/CMapSolid machinery.
//
//          Plane convention matches the VMF format consumed by vbsp:
//          each side stores three plane points whose cross product
//          ((p1-p2) x (p3-p2) in the file order) points OUTWARD from the
//          solid, plus texture axes in the "[ux uy uz offset] scale" form.
//
//=============================================================================

#pragma once

#include <QString>
#include <QStringList>
#include <QMap>
#include <QList>
#include <QPair>
#include <cstdlib>
#include <optional>
#include "displacementinspect.h"

//-----------------------------------------------------------------------------
// Purpose: One face of a lightweight solid. The plane is stored as three
//          points (exactly what the VMF "plane" key needs); normals and
//          distances are derived on demand.
//-----------------------------------------------------------------------------
struct SideLite
{
	SideLite();

	int id;						// unique face id (allocated from the document counter)
	double planePts[3][3];		// three points defining the plane (VMF order)
	QString texture;			// material name, e.g. "tools/toolsnodraw"
	float uaxis[4];				// texture u axis: x y z offset
	float vaxis[4];				// texture v axis: x y z offset
	float uscale;				// texture u scale (world units per texel)
	float vscale;				// texture v scale
	float rotation;				// texture rotation (degrees), unused for axis-aligned boxes
	int lightmapscale;			// lightmap resolution (world units per luxel)
	int smoothing;				// smoothing group bitmask
	bool hasVerticesPlus = false;
	QStringList verticesPlus;	// Hammer++ vertex cache in its original order
	std::optional<HammerDisplacementInspect::Chunk> displacement;
};

//-----------------------------------------------------------------------------
// Purpose: A brush solid: six (or more) sides plus a cached bounding box.
//-----------------------------------------------------------------------------
struct SolidLite
{
	SolidLite();

	int id;
	int ownerEntityId = 0; // 0: worldspawn; otherwise an EntityLite id
	bool hiddenWrapper = false; // Preserves original VMF hidden { solid { ... } } semantics.
	QList<SideLite> sides;
	bool hasEditor = false;
	QList<QPair<QString, QString> > editorProperties;
	double mins[3];
	double maxs[3];
};

//-----------------------------------------------------------------------------
// Purpose: A point entity. "classname" and "origin" live in dedicated fields;
//          all remaining key/values are in keyvalues (they never contain the
//          classname or origin keys).
//-----------------------------------------------------------------------------
struct EntityLite
{
	EntityLite();

	int id;
	bool hiddenWrapper = false; // Preserves original VMF hidden { entity { ... } } semantics.
	QString classname;
	QMap<QString, QString> keyvalues;
	QList<QPair<QString, QString> > connections;	// VMF output name/value pairs; names may repeat
	bool hasEditor = false;
	QList<QPair<QString, QString> > editorProperties;
	double origin[3];
	bool hasExplicitOrigin = true; // Preserve absent origin on loaded brush entities.
	double angle;				// yaw in degrees (mirrored to/from the "angles" key)
};

// A saved Hammer camera, preserving its VMF key/value pairs in file order.
struct CameraLite
{
	QList<QPair<QString, QString> > keyvalues;
};

// Original Hammer visgroup definitions retain ordered keys and children.
// Group IDs are independent of VMF object IDs.
struct VisGroupLite
{
	QList<QPair<QString, QString> > properties;
	QList<VisGroupLite> children;
};

struct VmfMetadataBlock
{
	QString name;
	QList<QPair<QString, QString> > properties;
};

//-----------------------------------------------------------------------------
// Purpose: Lightweight map document: worldspawn key/values + entities + solids.
//-----------------------------------------------------------------------------
class MapDocLite
{
public:
	MapDocLite();

	// Global id allocation (entities, solids, faces share one counter)
	int AllocId();
	int PeekNextId() const { return m_nNextId; }
	void SetNextId( int id ) { m_nNextId = id; }

	// Lookup helpers; return NULL when the id is unknown
	EntityLite *EntityById( int id );
	SolidLite *SolidById( int id );
	const SolidLite *SolidById( int id ) const;

	// Removes an entity or a solid by id; returns true if something was removed
	bool RemoveById( int id );

	// Recomputes the cached bounding box from the side plane points
	void RecalcSolidBounds( SolidLite &solid );

	// Worldspawn access (includes "classname" = "worldspawn")
	QString WorldspawnValue( const QString &key ) const;
	void SetWorldspawnValue( const QString &key, const QString &value );
	// Match the original Hammer's integer interpretation of the active key.
	bool IsCordonActive() const
	{
		bool active = false;
		for ( const auto &key : cordonProperties )
			if ( key.first.compare( QLatin1String( "active" ), Qt::CaseInsensitive ) == 0 )
				active = std::strtol( key.second.toUtf8().constData(), nullptr, 10 ) > 0;
		for ( const auto &block : extraMetadata )
			if ( block.name.compare( QLatin1String( "cordons" ), Qt::CaseInsensitive ) == 0 )
				for ( const auto &key : block.properties )
					if ( key.first.compare( QLatin1String( "active" ), Qt::CaseInsensitive ) == 0 )
						active = active || std::strtol( key.second.toUtf8().constData(), nullptr, 10 ) > 0;
		return active;
	}

	QMap<QString, QString> worldspawn;
	QList<EntityLite> entities;
	QList<SolidLite> solids;
	QList<QPair<QString, QString> > cameraProperties;
	QList<CameraLite> cameras;
	QList<VisGroupLite> visgroups;
	QList<QPair<QString, QString> > cordonProperties;
	// Empty list is a valid versioninfo block, so track its presence separately.
	bool hasVersionInfo = false;
	QList<QPair<QString, QString> > versionProperties;
	QList<VmfMetadataBlock> extraMetadata;

private:
	int m_nNextId;
};

//-----------------------------------------------------------------------------
// Purpose: Builds the six sides of an axis-aligned box. The plane point order
//          guarantees outward normals, matching vbsp's expectations.
// Input  : mins/maxs - box bounds; texture - material for all six faces.
//-----------------------------------------------------------------------------
QList<SideLite> MakeBoxSides( const double mins[3], const double maxs[3],
	const QString &texture = QStringLiteral( "tools/toolsnodraw" ) );

// Creates a box solid in the document (bounds snapped by the caller) and
// returns its id, or -1 on a degenerate (zero-thickness) box.
int CreateBoxSolid( MapDocLite &doc, const double mins[3], const double maxs[3],
	const QString &texture = QStringLiteral( "tools/toolsnodraw" ) );

// Translates a solid by a delta (moves every plane point and the bounds cache)
void TranslateSolid( SolidLite &solid, const double delta[3] );

// Snaps a single coordinate to the given grid
double SnapCoord( double value, int gridSize );

// Formats one floating coordinate for VMF output, trimming trailing zeros
QString FormatVmfNumber( double value );

// Parses "x y z" into a double triple; returns false on malformed input
bool ParseVec3( const QString &text, double out[3] );

// Formats a coordinate triple as "x y z"
QString FormatVec3( const double v[3] );
