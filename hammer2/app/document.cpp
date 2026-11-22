//========= Hammer Qt6 (hammer2/app) ==========================================
//
// Purpose: CHammerDocument implementation. All file I/O goes through Qt
//          (QFile/QFileDialog/QTextStream) so it stays portable; map content
//          lives in MapDocLite and is serialized by VmfIO.
//
//=============================================================================

#include "document.h"

#include "vmfio.h"
#include "cordon.h"
#include "brushwindings.h"
#include "brushentityview.h"
#include "vertexedit.h"
#include "displacementedit.h"
#include "displacementsew.h"
#include "overlaydata.h"

#include <QFile>
#include <QFileDialog>
#include <QMessageBox>
#include <QStringList>
#include <cmath>
#include <limits>
#include <utility>
#include <functional>

namespace
{
	// This material is present in the stock Half-Life 2 content.
	const char *kDefaultTexture = "concrete/concretewall074a";
	void TranslateEntity( EntityLite &entity, const double delta[3] )
	{
		for ( int i = 0; i < 3; ++i ) entity.origin[i] += delta[i];
		if ( entity.classname.compare( QLatin1String( "info_overlay" ), Qt::CaseInsensitive ) != 0 ) return;
		HammerOverlay::Point basis;
		const QString key = QStringLiteral( "BasisOrigin" );
		if ( !ParseVec3( HammerOverlay::Value( entity, key ), basis.data() ) ) return;
		for ( int i = 0; i < 3; ++i ) basis[i] += delta[i];
		HammerOverlay::Set( entity, key, FormatVec3( basis.data() ) );
	}
	bool CanPasteOverlayFaces( const QList<EntityLite> &entities, const QList<SolidLite> &solids, bool sameDocument )
	{
		if ( sameDocument ) return true;
		QSet<int> copiedFaces;
		for ( const SolidLite &solid : solids )
			for ( const SideLite &side : solid.sides ) copiedFaces.insert( side.id );
		for ( const EntityLite &entity : entities )
		{
			if ( entity.classname.compare( QLatin1String( "info_overlay" ), Qt::CaseInsensitive ) != 0 ) continue;
			HammerOverlay::Data overlay;
			if ( !HammerOverlay::Parse( entity, overlay ) ) return false;
			for ( int faceId : overlay.sides ) if ( !copiedFaces.contains( faceId ) ) return false;
		}
		return true;
	}

	void RemapPastedOverlayFaces( MapDocLite &map, int firstEntity, const QMap<int, int> &faceIds )
	{
		if ( faceIds.isEmpty() ) return;
		for ( int i = firstEntity; i < map.entities.size(); ++i )
		{
			EntityLite &entity = map.entities[i];
			HammerOverlay::Data overlay;
			if ( !HammerOverlay::Parse( entity, overlay ) ) continue;
			QStringList ids;
			for ( int id : overlay.sides ) ids.append( QString::number( faceIds.value( id, id ) ) );
			HammerOverlay::Set( entity, QStringLiteral( "sides" ), ids.join( QLatin1Char( ' ' ) ) );
		}
	}
	bool HasProtectedVisgroupState( bool hiddenWrapper, const QList<QPair<QString, QString>> &properties )
	{
		if ( hiddenWrapper ) return true;
		for ( const auto &property : properties )
		{
			const QString &key = property.first;
			if ( key.compare( QLatin1String( "visgroupid" ), Qt::CaseInsensitive ) == 0 ||
				 key.compare( QLatin1String( "colorvisgroupid" ), Qt::CaseInsensitive ) == 0 ) return true;
			if ( ( key.compare( QLatin1String( "visgroupshown" ), Qt::CaseInsensitive ) == 0 ||
				   key.compare( QLatin1String( "visgroupautoshown" ), Qt::CaseInsensitive ) == 0 ) &&
				 property.second != QLatin1String( "1" ) ) return true;
		}
		return false;
	}
	bool EntitySelectable( const MapDocLite &map, const ViewFilter &filter, const EntityLite &entity )
	{
		if ( !filter.EntityVisible( entity ) ) return false;
		bool owns = false;
		for ( const SolidLite &solid : map.solids ) if ( solid.ownerEntityId == entity.id )
		{
			owns = true;
			if ( HammerBrushEntityView::Visible( map, filter, solid ) ) return true;
		}
		return !owns;
	}
	bool SelectionBrushesVisible( const MapDocLite &map, const ViewFilter &filter, const QSet<int> &ids )
	{
		for ( const SolidLite &solid : map.solids )
			if ( ( ( !solid.ownerEntityId && ids.contains( solid.id ) ) ||
				   ( solid.ownerEntityId && ids.contains( solid.ownerEntityId ) ) ) &&
				 !HammerBrushEntityView::Visible( map, filter, solid ) ) return false;
		return true;
	}
}

CHammerDocument::CHammerDocument( QObject *parent )
	: QObject( parent )
	, m_bDirty( false )
	, m_eTool( TOOL_SELECT )
	, m_nGridSize( 16 )
	, m_bSnap( true )
	, m_strActiveMaterial( QLatin1String( kDefaultTexture ) )
	, m_nBlockDepth( 128 )
	, m_nSelectedId( -1 )
	, m_nClipboardPrimaryId( -1 )
	, m_nClipboardPasteCount( 0 )
	, m_nEditDepth( 0 )
	, m_bEditChanged( false )
	, m_nRevision( 0 )
	, m_nNextRevision( 1 )
	, m_nSavedRevision( 0 )
{
}

//-----------------------------------------------------------------------------
// Purpose: Resets the map to an empty worldspawn-only document.
//-----------------------------------------------------------------------------
bool CHammerDocument::NewDocument()
{
	++m_nDocumentIdentity;
	m_PointFilePoints.clear();
	m_nPointFileIndex = -1;
	m_Map = MapDocLite();
	m_ViewFilter = ViewFilter();
	m_nGridSize = 16; m_bSnap = true; m_bGridVisible = true;
	m_bPersistGridSize = m_bPersistSnap = m_bPersistGridVisible = false;
	emit stateChanged();
	m_Map.SetWorldspawnValue( "classname", "worldspawn" );
	m_Map.SetWorldspawnValue( "detailmaterial", "detail/detailsprites" );
	m_Map.SetWorldspawnValue( "detailvbsp", "detail.vbsp" );
	m_Map.SetWorldspawnValue( "maxpropscreenwidth", "-1" );
	m_Map.SetWorldspawnValue( "skyname", "sky_day01_01" );
	m_Map.SetWorldspawnValue( "mapversion", "1" );

	m_nSelectedId = -1;
	m_SelectedIds.clear();
	m_strFileName.clear();
	ResetHistory();
	SetDirty( false );
	emit fileNameChanged( m_strFileName );
	emit documentChanged();
	emit selectionChanged( -1 );
	return true;
}

//-----------------------------------------------------------------------------
// Purpose: Loads the FGD into the document's CFgdData wrapper.
//-----------------------------------------------------------------------------
bool CHammerDocument::LoadFgd( const QString &fgdPath, QString *errorOut )
{
	const bool bOk = m_Fgd.Load( fgdPath, errorOut );
	emit stateChanged();
	return bOk;
}

bool CHammerDocument::LoadFgds( const QStringList &paths, QString *errorOut )
{
	const bool ok = m_Fgd.LoadFiles( paths, errorOut );
	if ( ok ) emit stateChanged();
	return ok;
}

//-----------------------------------------------------------------------------
// Purpose: Places a point entity with FGD default key/values.
//-----------------------------------------------------------------------------
int CHammerDocument::PlaceDecal( const double origin[3] )
{
	for ( int axis = 0; axis < 3; ++axis )
		if ( !std::isfinite( origin[axis] ) ) return -1;
	if ( m_strActiveMaterial.isEmpty() ) return -1;

	BeginEdit();
	EntityLite entity;
	entity.classname = QStringLiteral( "infodecal" );
	entity.id = m_Map.AllocId();
	entity.keyvalues = m_Fgd.DefaultKeyValues( entity.classname );
	entity.keyvalues.insert( QStringLiteral( "texture" ), m_strActiveMaterial );
	for ( int axis = 0; axis < 3; ++axis ) entity.origin[axis] = origin[axis];
	m_Map.entities.append( entity );
	SetSelection( QSet<int>() << entity.id, entity.id );
	TouchContent();
	EndEdit();
	return entity.id;
}

int CHammerDocument::PlaceOverlay( int solidId, int sideId, const double origin[3], double width, double height )
{
	using namespace HammerOverlay;
	if ( !origin || m_strActiveMaterial.trimmed().isEmpty() ||
		 !std::isfinite( width ) || !std::isfinite( height ) || width < 0.001 || height < 0.001 ||
		 width > 32768 || height > 32768 ) return -1;
	for ( int axis = 0; axis < 3; ++axis ) if ( !std::isfinite( origin[axis] ) ) return -1;
	const MapDocLite &map = m_Map;
	const SolidLite *solid = map.SolidById( solidId );
	if ( !solid || !HammerBrushEntityView::Visible( map, m_ViewFilter, *solid ) || sideId <= 0 ) return -1;
	int faceIndex = -1;
	for ( const SolidLite &candidate : map.solids )
		for ( int i = 0; i < candidate.sides.size(); ++i )
			if ( candidate.sides[i].id == sideId )
			{
				if ( &candidate != solid || faceIndex >= 0 ) return -1;
				faceIndex = i;
			}
	QList<HammerWinding::Plane> planes;
	if ( faceIndex < 0 || !HammerWinding::BuildPlanes( *solid, planes ) ) return -1;
	const auto &plane = planes[faceIndex];
	const Point &normal = plane.normal;
	const Point guess = std::abs( normal[0] ) >= std::abs( normal[1] ) && std::abs( normal[0] ) >= std::abs( normal[2] )
		? Point{ 0, 1, 0 } : Point{ 1, 0, 0 };
	Point v = HammerWinding::Cross( normal, guess );
	if ( !HammerWinding::Normalize( v ) ) return -1;
	Point u = HammerWinding::Cross( v, normal );
	if ( !HammerWinding::Normalize( u ) ) return -1;
	Point center{ origin[0], origin[1], origin[2] };
	const double offPlane = HammerWinding::Dot( center, normal ) - plane.dist;
	for ( int axis = 0; axis < 3; ++axis ) center[axis] -= normal[axis] * offPlane;

	EntityLite entity;
	entity.classname = QStringLiteral( "info_overlay" );
	entity.keyvalues = m_Fgd.DefaultKeyValues( entity.classname );
	for ( int axis = 0; axis < 3; ++axis ) entity.origin[axis] = origin[axis];
	Set( entity, QStringLiteral( "material" ), m_strActiveMaterial );
	Set( entity, QStringLiteral( "sides" ), QString::number( sideId ) );
	Set( entity, QStringLiteral( "BasisOrigin" ), FormatVec3( center.data() ) );
	Set( entity, QStringLiteral( "BasisU" ), FormatVec3( u.data() ) );
	Set( entity, QStringLiteral( "BasisV" ), FormatVec3( v.data() ) );
	Set( entity, QStringLiteral( "BasisNormal" ), FormatVec3( normal.data() ) );
	const auto positiveMajor = []( const Point &axis ) {
		int major = 0;
		for ( int i = 1; i < 3; ++i )
			if ( std::abs( axis[i] ) > std::abs( axis[major] ) ) major = i;
		return axis[major] >= 0;
	};
	const bool reverse = positiveMajor( u ) != positiveMajor( v );
	Set( entity, QStringLiteral( "StartU" ), reverse ? QStringLiteral( "1" ) : QStringLiteral( "0" ) );
	Set( entity, QStringLiteral( "EndU" ), reverse ? QStringLiteral( "0" ) : QStringLiteral( "1" ) );
	Set( entity, QStringLiteral( "StartV" ), reverse ? QStringLiteral( "0" ) : QStringLiteral( "1" ) );
	Set( entity, QStringLiteral( "EndV" ), reverse ? QStringLiteral( "1" ) : QStringLiteral( "0" ) );
	Set( entity, QStringLiteral( "RenderOrder" ), QStringLiteral( "0" ) );
	const double x = width / 2, y = height / 2;
	const Point corners[] = { { -x, -y, 0 }, { -x, y, 0 }, { x, y, 0 }, { x, -y, 0 } };
	for ( int i = 0; i < 4; ++i ) Set( entity, QStringLiteral( "uv%1" ).arg( i ), FormatVec3( corners[i].data() ) );
	Data parsed;
	if ( !Parse( entity, parsed ) || !Convex( parsed ) ) return -1;
	BeginEdit();
	entity.id = m_Map.AllocId();
	m_Map.entities.append( entity );
	SetSelection( QSet<int>() << entity.id, entity.id );
	TouchContent();
	EndEdit();
	return entity.id;
}

bool CHammerDocument::MoveOverlayHandle( int entityId, int corner, const double origin[3] )
{
	using namespace HammerOverlay;
	if ( !origin || corner < 0 || corner >= 4 ) return false;
	const EntityLite *entity = m_Map.EntityById( entityId );
	Data data;
	if ( !entity || !m_SelectedIds.contains( entityId ) || !m_ViewFilter.EntityVisible( *entity ) ||
		 !Parse( *entity, data ) ) return false;
	const Point delta{ origin[0]-data.origin[0], origin[1]-data.origin[1], origin[2]-data.origin[2] };
	Point &handle = data.points[corner];
	const double u = HammerWinding::Dot( delta, data.u ), v = HammerWinding::Dot( delta, data.v );
	if ( !std::isfinite( u ) || !std::isfinite( v ) || std::abs( u ) > 65536 || std::abs( v ) > 65536 ) return false;
	if ( std::abs( handle[0] - u ) < 0.000001 && std::abs( handle[1] - v ) < 0.000001 ) return true;
	handle[0] = u;
	handle[1] = v;
	if ( !Convex( data ) ) return false;
	BeginEdit();
	Set( *m_Map.EntityById( entityId ), QStringLiteral( "uv%1" ).arg( corner ), FormatVec3( handle.data() ) );
	TouchContent();
	EndEdit();
	return true;
}

int CHammerDocument::PlaceEntity( const QString &className, const double origin[3] )
{
	BeginEdit();
	EntityLite entity;
	entity.classname = className;
	entity.id = m_Map.AllocId();

	// Prefill from the FGD defaults (empty map when the FGD has no such class)
	entity.keyvalues = m_Fgd.DefaultKeyValues( className );

	entity.origin[0] = origin[0];
	entity.origin[1] = origin[1];
	entity.origin[2] = origin[2];

	m_Map.entities.append( entity );
	SetSelection( QSet<int>() << entity.id, entity.id );
	TouchContent();
	EndEdit();
	return entity.id;
}

//-----------------------------------------------------------------------------
// Purpose: Creates an axis-aligned box solid from (already snapped) bounds.
//-----------------------------------------------------------------------------
int CHammerDocument::CreateBrush( const double mins[3], const double maxs[3] )
{
	BeginEdit();
	const int id = CreateBoxSolid( m_Map, mins, maxs, m_strActiveMaterial );
	if ( id < 0 )
	{
		EndEdit();
		return -1;
	}

	SetSelection( QSet<int>() << id, id );
	TouchContent();
	EndEdit();
	return id;
}

const SolidLite *CHammerDocument::SelectedVertexEditableSolid() const
{
	if ( m_SelectedIds.size() != 1 ) return nullptr;
	const int id = *m_SelectedIds.constBegin();
	const SolidLite *solid = nullptr;
	for ( const SolidLite &candidate : m_Map.solids ) if ( candidate.id == id ) { solid = &candidate; break; }
	if ( solid ) return solid->ownerEntityId == 0 && HammerBrushEntityView::Visible( m_Map, m_ViewFilter, *solid ) ? solid : nullptr;
	const EntityLite *entity = nullptr;
	for ( const EntityLite &candidate : m_Map.entities ) if ( candidate.id == id ) { entity = &candidate; break; }
	if ( !entity || !m_ViewFilter.EntityVisible( *entity ) ) return nullptr;
	const SolidLite *child = nullptr;
	for ( const SolidLite &candidate : m_Map.solids ) if ( candidate.ownerEntityId == id )
	{
		if ( child || !HammerBrushEntityView::Visible( m_Map, m_ViewFilter, candidate ) ) return nullptr;
		child = &candidate;
	}
	return child;
}

bool CHammerDocument::MoveSelectedProjectedVertices( int u, int v, double fromU, double fromV, double toU, double toV )
{
	const SolidLite *solid = SelectedVertexEditableSolid();
	if ( !solid ) return false;
	for ( const SideLite &side : solid->sides ) if ( side.displacement ) return false;
	const int id = solid->id;
	SolidLite edited;
	if ( !HammerVertexEdit::MoveProjected( *solid, u, v, fromU, fromV, toU, toV, edited ) ) return false;
	m_Map.RecalcSolidBounds( edited );
	BeginEdit();
	*m_Map.SolidById( id ) = edited;
	TouchContent(); EndEdit();
	return true;
}

//-----------------------------------------------------------------------------
// Purpose: Moves the selected entity or solid by a world-space delta.
//-----------------------------------------------------------------------------
bool CHammerDocument::MoveSelection( const double delta[3] )
{
	if ( delta[0] == 0.0 && delta[1] == 0.0 && delta[2] == 0.0 )
		return false;
	QSet<int> ids = m_SelectedIds;
	if ( ids.isEmpty() && m_nSelectedId >= 0 )
		ids.insert( m_nSelectedId );
	if ( ids.isEmpty() || !SelectionBrushesVisible( m_Map, m_ViewFilter, ids ) )
		return false;
	BeginEdit();
	bool moved = false;
	for ( int id : ids )
	{
		EntityLite *pEntity = m_Map.EntityById( id );
		if ( pEntity )
		{
			TranslateEntity( *pEntity, delta );
			for ( SolidLite &solid : m_Map.solids )
				if ( solid.ownerEntityId == id ) TranslateSolid( solid, delta );
			moved = true;
			continue;
		}
		SolidLite *pSolid = m_Map.SolidById( id );
		if ( pSolid )
		{
			TranslateSolid( *pSolid, delta );
			moved = true;
		}
	}
	if ( moved )
	{
		TouchContent();
		EndEdit();
		return true;
	}

	EndEdit();
	return false;
}

bool CHammerDocument::SetSelectedEntityOrigin( const double origin[3] )
{
	if ( !origin || m_SelectedIds.size() != 1 || !SelectionBrushesVisible( m_Map, m_ViewFilter, m_SelectedIds ) ) return false;
	EntityLite *entity = m_Map.EntityById( m_nSelectedId );
	if ( !entity ) return false;
	double delta[3];
	for ( int i = 0; i < 3; ++i ) { if ( !std::isfinite( origin[i] ) ) return false; delta[i] = origin[i] - entity->origin[i]; }
	if ( delta[0] == 0 && delta[1] == 0 && delta[2] == 0 && entity->hasExplicitOrigin ) return true;
	BeginEdit();
	MoveSelection( delta );
	m_Map.EntityById( m_nSelectedId )->hasExplicitOrigin = true;
	TouchContent(); EndEdit();
	return true;
}

// Recenter explicitly defined brush-entity origins without moving their solids.
// Point helpers and implicit brush origins need the original object model.
bool CHammerDocument::CenterSelectedBrushEntityOrigins()
{
	if ( !SelectionBrushesVisible( m_Map, m_ViewFilter, m_SelectedIds ) ) return false;
	struct Change { int id; double center[3]; };
	QList<Change> changes;
	for ( int id : m_SelectedIds )
	{
		const EntityLite *entity = m_Map.EntityById( id );
		if ( !entity || !entity->hasExplicitOrigin ) continue;
		double lo[3] = {}, hi[3] = {};
		bool found = false;
		for ( const SolidLite &solid : m_Map.solids )
		{
			if ( solid.ownerEntityId != id ) continue;
			for ( int axis = 0; axis < 3; ++axis )
			{
				if ( !std::isfinite( solid.mins[axis] ) || !std::isfinite( solid.maxs[axis] ) || solid.mins[axis] > solid.maxs[axis] ) return false;
				lo[axis] = found ? qMin( lo[axis], solid.mins[axis] ) : solid.mins[axis];
				hi[axis] = found ? qMax( hi[axis], solid.maxs[axis] ) : solid.maxs[axis];
			}
			found = true;
		}
		if ( !found ) continue;
		Change change; change.id = id;
		bool different = false;
		for ( int axis = 0; axis < 3; ++axis )
		{
			change.center[axis] = lo[axis] / 2.0 + hi[axis] / 2.0;
			if ( !std::isfinite( change.center[axis] ) ) return false;
			different = different || change.center[axis] != entity->origin[axis];
		}
		if ( different ) changes.append( change );
	}
	if ( changes.isEmpty() ) return false;
	BeginEdit();
	for ( const Change &change : changes )
	{
		EntityLite *entity = m_Map.EntityById( change.id );
		for ( int axis = 0; axis < 3; ++axis ) entity->origin[axis] = change.center[axis];
	}
	TouchContent(); EndEdit();
	return true;
}

bool CHammerDocument::TieSelectionToEntity( const QString &className )
{
	if ( m_SelectedIds.isEmpty() || !m_Fgd.SolidClassNames().contains( className, Qt::CaseInsensitive ) ) return false;
	for ( int id : m_SelectedIds )
	{
		const SolidLite *solid = m_Map.SolidById( id );
		if ( !solid || solid->ownerEntityId != 0 || HasProtectedVisgroupState( solid->hiddenWrapper, solid->editorProperties ) ) return false;
		for ( const SideLite &side : solid->sides ) if ( side.displacement ) return false;
	}
	BeginEdit();
	EntityLite entity;
	entity.id = m_Map.AllocId();
	entity.classname = className;
	entity.keyvalues = m_Fgd.DefaultKeyValues( className );
	entity.hasExplicitOrigin = false;
	for ( SolidLite &solid : m_Map.solids )
		if ( m_SelectedIds.contains( solid.id ) ) solid.ownerEntityId = entity.id;
	m_Map.entities.append( entity );
	SetSelection( QSet<int>() << entity.id, entity.id );
	TouchContent(); EndEdit();
	return true;
}

bool CHammerDocument::MoveSelectionToWorld()
{
	if ( m_SelectedIds.isEmpty() ) return false;
	for ( int id : m_SelectedIds )
	{
		const EntityLite *entity = m_Map.EntityById( id );
		if ( !entity || HasProtectedVisgroupState( entity->hiddenWrapper, entity->editorProperties ) ) return false;
		bool found = false;
		for ( const SolidLite &solid : m_Map.solids ) if ( solid.ownerEntityId == id )
		{
			if ( HasProtectedVisgroupState( solid.hiddenWrapper, solid.editorProperties ) ) return false;
			found = true;
		}
		if ( !found ) return false;
	}
	BeginEdit();
	QSet<int> solids;
	for ( SolidLite &solid : m_Map.solids )
		if ( m_SelectedIds.contains( solid.ownerEntityId ) ) { solid.ownerEntityId = 0; solids.insert( solid.id ); }
	for ( int id : m_SelectedIds ) m_Map.RemoveById( id );
	SetSelection( solids ); TouchContent(); EndEdit();
	return true;
}

namespace
{
	// Find the six outward-facing planes of a real axis-aligned box. Cached
	// bounds alone are not enough to identify an arbitrary imported brush.
	bool BoxFaceOrder( const SolidLite &solid, int faceIndex[6] )
	{
		if ( solid.sides.size() != 6 ) return false;
		for ( int axis = 0; axis < 3; ++axis )
			if ( !std::isfinite( solid.mins[axis] ) || !std::isfinite( solid.maxs[axis] ) || solid.maxs[axis] <= solid.mins[axis] ) return false;
		for ( int i = 0; i < 6; ++i ) faceIndex[i] = -1;
		for ( int i = 0; i < 6; ++i )
		{
			const SideLite &side = solid.sides[i];
			double a[3], b[3], n[3];
			for ( int k = 0; k < 3; ++k )
			{
				if ( !std::isfinite( side.planePts[0][k] ) || !std::isfinite( side.planePts[1][k] ) || !std::isfinite( side.planePts[2][k] ) ) return false;
				a[k] = side.planePts[1][k] - side.planePts[0][k];
				b[k] = side.planePts[2][k] - side.planePts[0][k];
			}
			n[0] = b[1] * a[2] - b[2] * a[1]; n[1] = b[2] * a[0] - b[0] * a[2]; n[2] = b[0] * a[1] - b[1] * a[0];
			int axis = -1;
			for ( int k = 0; k < 3; ++k ) if ( n[k] != 0 ) { if ( axis != -1 ) return false; axis = k; }
			if ( axis < 0 || !std::isfinite( n[axis] ) ) return false;
			const int face = axis * 2 + ( n[axis] > 0 ? 0 : 1 );
			const double boundary = ( face % 2 == 0 ) ? solid.maxs[axis] : solid.mins[axis];
			if ( faceIndex[face] >= 0 || side.planePts[0][axis] != boundary ) return false;
			faceIndex[face] = i;
		}
		return true;
	}
	bool CanTransformBrush( const SolidLite &solid )
	{
		for ( int axis = 0; axis < 3; ++axis )
			if ( !std::isfinite( solid.mins[axis] ) || !std::isfinite( solid.maxs[axis] ) ||
				 solid.maxs[axis] - solid.mins[axis] < 0.00001 ) return false;
		for ( const SideLite &side : solid.sides )
			if ( side.hasVerticesPlus || side.displacement ) return false;
		QList<HammerWinding::Polygon> faces;
		return HammerWinding::BuildFaces( solid, faces );
	}
	bool ExpandBoxSelection( const MapDocLite &map, const ViewFilter &filter, const QSet<int> &selected, QSet<int> &brushIds, QList<EntityLite> &owners )
	{
		for ( int id : selected )
		{
			bool found = false;
			for ( const SolidLite &solid : map.solids )
				if ( solid.id == id || solid.ownerEntityId == id )
				{
					// A selected owner may contain a hidden child. Transforming only
					// visible children would split the entity; transforming every
					// child would silently edit hidden geometry. Reject atomically.
					if ( !HammerBrushEntityView::Visible( map, filter, solid ) ) return false;
					brushIds.insert( solid.id ); found = true;
				}
			for ( const EntityLite &entity : map.entities ) if ( entity.id == id )
			{
				if ( !found ) return false;
				owners.append( entity );
				break;
			}
			if ( !found ) return false;
		}
		return !brushIds.isEmpty();
	}
}

bool CHammerDocument::ScaleSelectedBoxes( const double factors[3] )
{
	if ( m_SelectedIds.isEmpty() ) return false;
	for ( int axis = 0; axis < 3; ++axis )
		if ( !std::isfinite( factors[axis] ) || factors[axis] < 0.01 || factors[axis] > 100.0 ) return false;
	if ( factors[0] == 1 && factors[1] == 1 && factors[2] == 1 ) return false;
	double mins[3] = {}, maxs[3] = {};
	QList<SolidLite> boxes;
	QSet<int> brushIds; QList<EntityLite> owners;
	if ( !ExpandBoxSelection( m_Map, m_ViewFilter, m_SelectedIds, brushIds, owners ) ) return false;
	for ( int id : brushIds )
	{
		const SolidLite *solid = nullptr;
		for ( const SolidLite &candidate : static_cast<const QList<SolidLite> &>( m_Map.solids ) )
			if ( candidate.id == id ) { solid = &candidate; break; }
		if ( !solid || !CanTransformBrush( *solid ) ) return false;

		for ( int axis = 0; axis < 3; ++axis )
		{
			mins[axis] = boxes.isEmpty() ? solid->mins[axis] : qMin( mins[axis], solid->mins[axis] );
			maxs[axis] = boxes.isEmpty() ? solid->maxs[axis] : qMax( maxs[axis], solid->maxs[axis] );
		}
		boxes.append( *solid );
	}
	double center[3];
	for ( int axis = 0; axis < 3; ++axis ) center[axis] = mins[axis] / 2.0 + maxs[axis] / 2.0;
	for ( SolidLite &solid : boxes )
	{
		for ( int axis = 0; axis < 3; ++axis )
		{
			const auto scale = [&]( double value ) { return center[axis] + ( value - center[axis] ) * factors[axis]; };
			solid.mins[axis] = scale( solid.mins[axis] );
			solid.maxs[axis] = scale( solid.maxs[axis] );
			if ( !std::isfinite( solid.mins[axis] ) || !std::isfinite( solid.maxs[axis] ) ||
				 std::abs( solid.mins[axis] ) > 1048576 || std::abs( solid.maxs[axis] ) > 1048576 ||
				 solid.maxs[axis] - solid.mins[axis] < 0.00001 ) return false;
			for ( SideLite &side : solid.sides )
				for ( int point = 0; point < 3; ++point )
				{
					const double v = scale( side.planePts[point][axis] );
					if ( !std::isfinite( v ) || std::abs( v ) > 1048576 ) return false;
					side.planePts[point][axis] = v;
				}
		}
	}
	for ( const SolidLite &brush : boxes ) if ( !CanTransformBrush( brush ) ) return false;
	for ( EntityLite &entity : owners ) if ( entity.hasExplicitOrigin )
		for ( int axis = 0; axis < 3; ++axis )
		{
			entity.origin[axis] = center[axis] + ( entity.origin[axis] - center[axis] ) * factors[axis];
			if ( !std::isfinite( entity.origin[axis] ) || std::abs( entity.origin[axis] ) > 1048576 ) return false;
		}
	BeginEdit();
	for ( const SolidLite &box : boxes ) *m_Map.SolidById( box.id ) = box;
	for ( const EntityLite &owner : owners ) *m_Map.EntityById( owner.id ) = owner;
	TouchContent();
	EndEdit();
	return true;
}

bool CHammerDocument::RotateSelectedBoxes90( bool clockwise, int axis )
{
	if ( axis < 0 || axis > 2 || m_SelectedIds.isEmpty() ) return false;
	const int a = ( axis + 1 ) % 3, b = ( axis + 2 ) % 3;
	QList<SolidLite> boxes;
	double lo[2] = {}, hi[2] = {};
	QSet<int> brushIds; QList<EntityLite> owners;
	if ( !ExpandBoxSelection( m_Map, m_ViewFilter, m_SelectedIds, brushIds, owners ) ) return false;
	for ( int id : brushIds )
	{
		const SolidLite *solid = nullptr;
		for ( const SolidLite &candidate : static_cast<const QList<SolidLite> &>( m_Map.solids ) )
			if ( candidate.id == id ) { solid = &candidate; break; }
		if ( !solid || !CanTransformBrush( *solid ) ) return false;

		for ( int i = 0; i < 2; ++i )
		{
			const int n = i == 0 ? a : b;
			lo[i] = boxes.isEmpty() ? solid->mins[n] : qMin( lo[i], solid->mins[n] );
			hi[i] = boxes.isEmpty() ? solid->maxs[n] : qMax( hi[i], solid->maxs[n] );
		}
		boxes.append( *solid );
	}
	const double cx = lo[0] / 2.0 + hi[0] / 2.0, cy = lo[1] / 2.0 + hi[1] / 2.0;
	const double sign = clockwise ? -1.0 : 1.0;
	const auto rotate = [&]( double x, double y, double &outX, double &outY ) {
		outX = cx - sign * ( y - cy ); outY = cy + sign * ( x - cx );
	};
	for ( SolidLite &box : boxes )
	{
		double x1, y1, x2, y2;
		rotate( box.mins[a], box.mins[b], x1, y1 );
		rotate( box.maxs[a], box.maxs[b], x2, y2 );
		box.mins[a] = qMin( x1, x2 ); box.maxs[a] = qMax( x1, x2 );
		box.mins[b] = qMin( y1, y2 ); box.maxs[b] = qMax( y1, y2 );
		for ( int i = 0; i < 3; ++i )
			if ( !std::isfinite( box.mins[i] ) || !std::isfinite( box.maxs[i] ) ||
				 std::abs( box.mins[i] ) > 1048576 || std::abs( box.maxs[i] ) > 1048576 ||
				 box.maxs[i] - box.mins[i] < 0.00001 ) return false;
		for ( SideLite &side : box.sides )
		{
			for ( int point = 0; point < 3; ++point )
			{
				double x, y; rotate( side.planePts[point][a], side.planePts[point][b], x, y );
				if ( !std::isfinite( x ) || !std::isfinite( y ) || std::abs( x ) > 1048576 || std::abs( y ) > 1048576 ) return false;
				side.planePts[point][a] = x; side.planePts[point][b] = y;
			}
			// Texture lock: rotate UV vectors and compensate the translation so
			// the same texture coordinates remain attached to the rotated face.
			float *axes[] = { side.uaxis, side.vaxis };
			const double scales[] = { side.uscale, side.vscale };
			for ( int i = 0; i < 2; ++i )
			{
				float *axis = axes[i];
				if ( !std::isfinite( scales[i] ) || scales[i] == 0 ) return false;
				const double x = -sign * axis[b], y = sign * axis[a];
				const double shift = axis[3] - ( x * ( cx + sign * cy ) + y * ( cy - sign * cx ) ) / scales[i];
				if ( !std::isfinite( x ) || !std::isfinite( y ) || !std::isfinite( axis[( b + 1 ) % 3] ) ||
					 !std::isfinite( shift ) || std::abs( shift ) > std::numeric_limits<float>::max() ) return false;
				axis[a] = float( x ); axis[b] = float( y ); axis[3] = float( shift );
			}
		}
	}
	for ( const SolidLite &brush : boxes ) if ( !CanTransformBrush( brush ) ) return false;
	for ( EntityLite &entity : owners ) if ( entity.hasExplicitOrigin )
	{
		double x, y; rotate( entity.origin[a], entity.origin[b], x, y );
		if ( !std::isfinite( x ) || !std::isfinite( y ) || std::abs( x ) > 1048576 || std::abs( y ) > 1048576 ) return false;
		entity.origin[a] = x; entity.origin[b] = y;
	}
	BeginEdit();
	for ( const SolidLite &box : boxes ) *m_Map.SolidById( box.id ) = box;
	for ( const EntityLite &owner : owners ) *m_Map.EntityById( owner.id ) = owner;
	TouchContent(); EndEdit();
	return true;
}

bool CHammerDocument::FlipSelectedBoxes( int axis )
{
	if ( axis < 0 || axis > 2 || m_SelectedIds.isEmpty() ) return false;
	QList<SolidLite> boxes;
	double lo = 0, hi = 0;
	QSet<int> brushIds; QList<EntityLite> owners;
	if ( !ExpandBoxSelection( m_Map, m_ViewFilter, m_SelectedIds, brushIds, owners ) ) return false;
	for ( int id : brushIds )
	{
		const SolidLite *solid = nullptr;
		for ( const SolidLite &candidate : static_cast<const QList<SolidLite> &>( m_Map.solids ) )
			if ( candidate.id == id ) { solid = &candidate; break; }
		if ( !solid || !CanTransformBrush( *solid ) ) return false;

		lo = boxes.isEmpty() ? solid->mins[axis] : qMin( lo, solid->mins[axis] );
		hi = boxes.isEmpty() ? solid->maxs[axis] : qMax( hi, solid->maxs[axis] );
		boxes.append( *solid );
	}
	const double center = lo / 2.0 + hi / 2.0;
	const auto mirror = [center]( double value ) { return center + ( center - value ); };
	for ( SolidLite &box : boxes )
	{
		const double oldMin = box.mins[axis], oldMax = box.maxs[axis];
		box.mins[axis] = mirror( oldMax ); box.maxs[axis] = mirror( oldMin );
		if ( !std::isfinite( box.mins[axis] ) || !std::isfinite( box.maxs[axis] ) ||
			 std::abs( box.mins[axis] ) > 1048576 || std::abs( box.maxs[axis] ) > 1048576 ||
			 box.maxs[axis] - box.mins[axis] < 0.00001 ) return false;
		for ( SideLite &side : box.sides )
		{
			for ( int point = 0; point < 3; ++point )
			{
				const double value = mirror( side.planePts[point][axis] );
				if ( !std::isfinite( value ) || std::abs( value ) > 1048576 ) return false;
				side.planePts[point][axis] = value;
			}
			// Reflection reverses winding; swap two points to retain outward planes.
			for ( int k = 0; k < 3; ++k ) std::swap( side.planePts[1][k], side.planePts[2][k] );
			float *axes[] = { side.uaxis, side.vaxis };
			const double scales[] = { side.uscale, side.vscale };
			for ( int i = 0; i < 2; ++i )
			{
				float *uv = axes[i];
				if ( !std::isfinite( scales[i] ) || scales[i] == 0 || !std::isfinite( uv[0] ) ||
					 !std::isfinite( uv[1] ) || !std::isfinite( uv[2] ) || !std::isfinite( uv[3] ) ) return false;
				const double newAxis = -double( uv[axis] );
				const double shift = uv[3] - newAxis * ( 2.0 * center ) / scales[i];
				if ( !std::isfinite( shift ) || std::abs( shift ) > std::numeric_limits<float>::max() ) return false;
				uv[axis] = float( newAxis ); uv[3] = float( shift );
			}
		}
	}
	for ( const SolidLite &brush : boxes ) if ( !CanTransformBrush( brush ) ) return false;
	for ( EntityLite &entity : owners ) if ( entity.hasExplicitOrigin )
	{
		entity.origin[axis] = mirror( entity.origin[axis] );
		if ( !std::isfinite( entity.origin[axis] ) || std::abs( entity.origin[axis] ) > 1048576 ) return false;
	}
	BeginEdit();
	for ( const SolidLite &box : boxes ) *m_Map.SolidById( box.id ) = box;
	for ( const EntityLite &owner : owners ) *m_Map.EntityById( owner.id ) = owner;
	TouchContent(); EndEdit();
	return true;
}

bool CHammerDocument::ClipSelectedBoxes( int axis, double coordinate, int keep )
{
	if ( axis < 0 || axis > 2 || !std::isfinite( coordinate ) || keep < -1 || keep > 1 || m_SelectedIds.isEmpty() ) return false;
	struct ClipResult { int sourceId; QList<SolidLite> halves; };
	QList<ClipResult> results;
	QSet<int> brushIds; QList<EntityLite> owners;
	if ( !ExpandBoxSelection( m_Map, m_ViewFilter, m_SelectedIds, brushIds, owners ) ) return false;
	for ( int id : brushIds )
	{
		const SolidLite *solid = nullptr;
		for ( const SolidLite &candidate : static_cast<const QList<SolidLite> &>( m_Map.solids ) )
			if ( candidate.id == id ) { solid = &candidate; break; }
		if ( !solid ) return false;
		for ( const SideLite &side : solid->sides ) if ( side.hasVerticesPlus || side.displacement ) return false;
		QList<HammerWinding::Polygon> faces;
		if ( !HammerWinding::BuildFaces( *solid, faces ) ) return false;
		QList<HammerWinding::Plane> planes;
		if ( !HammerWinding::BuildPlanes( *solid, planes ) ) return false;
		double lo[3] = { 1e100, 1e100, 1e100 }, hi[3] = { -1e100, -1e100, -1e100 };
		for ( const auto &face : faces ) for ( const auto &point : face )
			for ( int k = 0; k < 3; ++k ) { lo[k] = qMin( lo[k], point[k] ); hi[k] = qMax( hi[k], point[k] ); }
		if ( coordinate - lo[axis] < 0.00001 || hi[axis] - coordinate < 0.00001 ) return false;
		ClipResult result { id, {} };
		const int halves = keep == 0 ? 2 : 1;
		for ( int half = 0; half < halves; ++half )
		{
			const bool upper = keep > 0 || ( keep == 0 && half == 1 );
			SolidLite clipped = *solid;
			clipped.sides.clear();
			for ( int face = 0; face < faces.size(); ++face )
			{
				bool retained = false;
				for ( const auto &point : faces[face] )
					if ( upper ? point[axis] > coordinate + 1e-7 : point[axis] < coordinate - 1e-7 ) { retained = true; break; }
				if ( retained ) clipped.sides.append( solid->sides[face] );
			}
			double cutLo[3], cutHi[3];
			for ( int k = 0; k < 3; ++k ) { cutLo[k] = lo[k]; cutHi[k] = hi[k]; }
			if ( upper ) cutLo[axis] = coordinate; else cutHi[axis] = coordinate;
			const QList<SideLite> box = MakeBoxSides( cutLo, cutHi );
			int templateFace = 0;
			double bestAlignment = -2;
			for ( int face = 0; face < planes.size(); ++face )
			{
				const double alignment = planes[face].normal[axis] * ( upper ? -1 : 1 );
				if ( alignment > bestAlignment ) { bestAlignment = alignment; templateFace = face; }
			}
			SideLite cut = solid->sides[templateFace];
			cut.id = 0;
			const SideLite &plane = box[axis * 2 + ( upper ? 1 : 0 )];
			for ( int p = 0; p < 3; ++p ) for ( int k = 0; k < 3; ++k ) cut.planePts[p][k] = plane.planePts[p][k];
			// The original face can be slanted: its texture axes need not span
			// the new cut plane. Fall back to Hammer's axial box alignment.
			const int a = ( axis + 1 ) % 3, b = ( axis + 2 ) % 3;
			const double textureNormal = double( cut.vaxis[a] ) * cut.uaxis[b] - double( cut.vaxis[b] ) * cut.uaxis[a];
			if ( !std::isfinite( textureNormal ) || std::abs( textureNormal ) < 1e-8 )
				for ( int k = 0; k < 3; ++k ) { cut.uaxis[k] = plane.uaxis[k]; cut.vaxis[k] = plane.vaxis[k]; }
			int insertAt = 0;
			for ( int face = 0; face < templateFace; ++face )
				for ( const SideLite &retained : clipped.sides )
					if ( retained.id == solid->sides[face].id ) { ++insertAt; break; }
			clipped.sides.insert( insertAt, cut );
			QList<HammerWinding::Polygon> checked;
			if ( !HammerWinding::BuildFaces( clipped, checked ) ) return false;
			m_Map.RecalcSolidBounds( clipped );
			result.halves.append( clipped );
		}
		results.append( result );
	}
	BeginEdit();
	QSet<int> selected;
	for ( const EntityLite &owner : owners ) selected.insert( owner.id );
	for ( const ClipResult &result : results ) for ( int half = 0; half < result.halves.size(); ++half )
	{
		SolidLite clipped = result.halves[half];
		if ( half ) clipped.id = m_Map.AllocId();
		for ( SideLite &side : clipped.sides ) if ( half || side.id <= 0 ) side.id = m_Map.AllocId();
		if ( half ) m_Map.solids.append( clipped ); else *m_Map.SolidById( result.sourceId ) = clipped;
		if ( !clipped.ownerEntityId ) selected.insert( clipped.id );
	}
	SetSelection( selected ); TouchContent(); EndEdit();
	return true;
}

bool CHammerDocument::ClipSelectedByPlane( const double points[3][3], int keep )
{
	if ( !points || keep < -1 || keep > 1 || m_SelectedIds.isEmpty() ) return false;
	HammerWinding::Point p, a, b;
	for ( int k = 0; k < 3; ++k )
	{
		for ( int i = 0; i < 3; ++i ) if ( !std::isfinite( points[i][k] ) || std::abs( points[i][k] ) > 1048576 ) return false;
		p[k] = points[0][k]; a[k] = points[1][k] - p[k]; b[k] = points[2][k] - p[k];
	}
	HammerWinding::Point normal = HammerWinding::Cross( b, a );
	if ( !HammerWinding::Normalize( normal ) ) return false;
	const double distance = HammerWinding::Dot( normal, p );
	if ( !std::isfinite( distance ) || std::abs( distance ) > 1048576 ) return false;
	struct ClipResult { int sourceId; QList<SolidLite> halves; };
	QList<ClipResult> results;
	QSet<int> brushIds; QList<EntityLite> owners;
	if ( !ExpandBoxSelection( m_Map, m_ViewFilter, m_SelectedIds, brushIds, owners ) ) return false;
	for ( int id : brushIds )
	{
		const SolidLite *solid = m_Map.SolidById( id );
		if ( !solid ) return false;
		for ( const SideLite &side : solid->sides ) if ( side.hasVerticesPlus || side.displacement ) return false;
		QList<HammerWinding::Polygon> faces;
		QList<HammerWinding::Plane> planes;
		if ( !HammerWinding::BuildFaces( *solid, faces ) || !HammerWinding::BuildPlanes( *solid, planes ) ) return false;
		double low = 1e100, high = -1e100;
		for ( const auto &face : faces ) for ( const auto &vertex : face )
		{
			const double d = HammerWinding::Dot( normal, vertex ) - distance;
			low = qMin( low, d ); high = qMax( high, d );
		}
		if ( low >= -0.00001 || high <= 0.00001 ) return false;
		ClipResult result { id, {} };
		for ( int half = 0; half < ( keep == 0 ? 2 : 1 ); ++half )
		{
			const bool positive = keep > 0 || ( keep == 0 && half == 1 );
			SolidLite clipped = *solid; clipped.sides.clear();
			for ( int face = 0; face < faces.size(); ++face )
			{
				bool retained = false;
				for ( const auto &vertex : faces[face] )
				{
					const double d = HammerWinding::Dot( normal, vertex ) - distance;
					if ( positive ? d > 1e-7 : d < -1e-7 ) { retained = true; break; }
				}
				if ( retained ) clipped.sides.append( solid->sides[face] );
			}
			int templateFace = 0; double best = -2;
			for ( int face = 0; face < planes.size(); ++face )
			{
				const double alignment = HammerWinding::Dot( planes[face].normal, normal ) * ( positive ? -1 : 1 );
				if ( alignment > best ) { best = alignment; templateFace = face; }
			}
			SideLite cut = solid->sides[templateFace]; cut.id = 0;
			for ( int i = 0; i < 3; ++i ) for ( int k = 0; k < 3; ++k )
				cut.planePts[i][k] = points[positive && i > 0 ? 3 - i : i][k];
			// Choose a stable tangent basis; the original face UV axes can be
			// parallel to this new plane and produce an invalid cut face.
			HammerWinding::Point tangent = std::abs( normal[2] ) < 0.9 ? HammerWinding::Point{0,0,1} : HammerWinding::Point{1,0,0};
			const double component = HammerWinding::Dot( tangent, normal );
			for ( int k = 0; k < 3; ++k ) tangent[k] -= component * normal[k];
			if ( !HammerWinding::Normalize( tangent ) ) return false;
			const auto other = HammerWinding::Cross( normal, tangent );
			for ( int k = 0; k < 3; ++k ) { cut.uaxis[k] = float( tangent[k] ); cut.vaxis[k] = float( other[k] ); }
			if ( !std::isfinite( cut.uscale ) || !std::isfinite( cut.vscale ) || cut.uscale == 0 || cut.vscale == 0 ) return false;
			clipped.sides.append( cut );
			QList<HammerWinding::Polygon> checked;
			if ( !HammerWinding::BuildFaces( clipped, checked ) ) return false;
			m_Map.RecalcSolidBounds( clipped ); result.halves.append( clipped );
		}
		results.append( result );
	}
	BeginEdit(); QSet<int> selected;
	for ( const EntityLite &owner : owners ) selected.insert( owner.id );
	for ( const ClipResult &result : results ) for ( int half = 0; half < result.halves.size(); ++half )
	{
		SolidLite clipped = result.halves[half];
		if ( half ) clipped.id = m_Map.AllocId();
		for ( SideLite &side : clipped.sides ) if ( half || side.id <= 0 ) side.id = m_Map.AllocId();
		if ( half ) m_Map.solids.append( clipped ); else *m_Map.SolidById( result.sourceId ) = clipped;
		if ( !clipped.ownerEntityId ) selected.insert( clipped.id );
	}
	SetSelection( selected ); TouchContent(); EndEdit();
	return true;
}

bool CHammerDocument::HollowSelection( int thickness )
{
	if ( thickness > 1024 || thickness < -1024 || std::abs( thickness ) < 2 ) return false;
	const double width = std::abs( thickness );
	QList<SolidLite> originals;
	QSet<int> brushIds; QList<EntityLite> owners;
	if ( !ExpandBoxSelection( m_Map, m_ViewFilter, m_SelectedIds, brushIds, owners ) ) return false;
	for ( const SolidLite &solid : static_cast<const QList<SolidLite> &>( m_Map.solids ) )
	{
		if ( !brushIds.contains( solid.id ) ) continue;
		int faces[6];
		if ( !BoxFaceOrder( solid, faces ) ) return false;
		// Do not discard imported Hammer++ geometry caches while replacing faces.
		for ( const SideLite &side : solid.sides ) if ( side.hasVerticesPlus || side.displacement ) return false;
		for ( int axis = 0; axis < 3; ++axis )
		{
			const double lo = solid.mins[axis], hi = solid.maxs[axis];
			const double innerLo = lo + width, innerHi = hi - width;
			const double outerLo = lo - width, outerHi = hi + width;
			if ( thickness > 0 && ( !std::isfinite( innerLo ) || !std::isfinite( innerHi ) || innerLo <= lo || innerHi >= hi || innerLo >= innerHi ) ) return false;
			if ( thickness < 0 && ( !std::isfinite( outerLo ) || !std::isfinite( outerHi ) || outerLo >= lo || outerHi <= hi ) ) return false;
		}
		originals.append( solid );
	}
	if ( originals.isEmpty() ) return false;
	BeginEdit();
	QSet<int> selection = m_SelectedIds;
	for ( const SolidLite &source : originals )
	{
		int faces[6]; BoxFaceOrder( source, faces );
		double outerMin[3], outerMax[3], innerMin[3], innerMax[3];
		for ( int axis = 0; axis < 3; ++axis )
		{
			outerMin[axis] = source.mins[axis] - ( thickness < 0 ? width : 0 );
			outerMax[axis] = source.maxs[axis] + ( thickness < 0 ? width : 0 );
			innerMin[axis] = source.mins[axis] + ( thickness > 0 ? width : 0 );
			innerMax[axis] = source.maxs[axis] - ( thickness > 0 ? width : 0 );
		}
		m_Map.RemoveById( source.id ); selection.remove( source.id );
		for ( int wall = 0; wall < 6; ++wall )
		{
			const int axis = wall / 2;
			double lo[3], hi[3];
			for ( int k = 0; k < 3; ++k ) { lo[k] = k < axis ? innerMin[k] : outerMin[k]; hi[k] = k < axis ? innerMax[k] : outerMax[k]; }
			if ( wall % 2 == 0 ) lo[axis] = innerMax[axis]; else hi[axis] = innerMin[axis];
			SolidLite result = source;
			result.id = m_Map.AllocId();
			const QList<SideLite> geometry = MakeBoxSides( lo, hi );
			result.sides.clear();
			for ( int face = 0; face < 6; ++face )
			{
				SideLite side = source.sides[faces[face]];
				side.id = m_Map.AllocId();
				for ( int p = 0; p < 3; ++p ) for ( int k = 0; k < 3; ++k ) side.planePts[p][k] = geometry[face].planePts[p][k];
				result.sides.append( side );
			}
			for ( int k = 0; k < 3; ++k ) { result.mins[k] = lo[k]; result.maxs[k] = hi[k]; }
			m_Map.solids.append( result ); if ( !result.ownerEntityId ) selection.insert( result.id );
		}
	}
	SetSelection( selection ); TouchContent(); EndEdit();
	return true;
}

// Point entities use their origin; brushes use their minimum corner and only move.
bool CHammerDocument::SnapSelectionToGrid( bool individually )
{
	const QSet<int> ids = m_SelectedIds;
	if ( ids.isEmpty() || m_nGridSize <= 0 || !SelectionBrushesVisible( m_Map, m_ViewFilter, ids ) )
		return false;
	const auto anchor = [this]( int id, double point[3] ) {
		const EntityLite *entity = m_Map.EntityById( id );
		const SolidLite *solid = m_Map.SolidById( id );
		bool brush = false;
		for ( const SolidLite &child : m_Map.solids ) if ( child.ownerEntityId == id )
		{
			for ( int axis = 0; axis < 3; ++axis ) point[axis] = brush ? qMin( point[axis], child.mins[axis] ) : child.mins[axis];
			brush = true;
		}
		if ( brush ) return true;
		const double *source = entity ? entity->origin : solid ? solid->mins : nullptr;
		if ( !source ) return false;
		for ( int axis = 0; axis < 3; ++axis ) point[axis] = source[axis];
		return true;
	};
	double mins[3] = {};
	bool found = false;
	for ( int id : ids )
	{
		double point[3];
		if ( !anchor( id, point ) ) continue;
		for ( int axis = 0; axis < 3; ++axis )
			mins[axis] = found ? qMin( mins[axis], point[axis] ) : point[axis];
		found = true;
	}
	if ( !found ) return false;
	bool singleEntity = ids.size() == 1 && m_Map.EntityById( *ids.constBegin() );
	if ( singleEntity ) for ( const SolidLite &solid : m_Map.solids ) if ( solid.ownerEntityId == *ids.constBegin() ) { singleEntity = false; break; }
	const int individualGrid = m_bSnap ? m_nGridSize : 1;
	const int groupGrid = singleEntity ? individualGrid : m_nGridSize;
	double groupDelta[3];
	for ( int axis = 0; axis < 3; ++axis )
		groupDelta[axis] = SnapCoord( mins[axis], groupGrid ) - mins[axis];
	BeginEdit();
	bool changed = false;
	for ( int id : ids )
	{
		EntityLite *entity = m_Map.EntityById( id );
		SolidLite *solid = m_Map.SolidById( id );
		double point[3];
		if ( !anchor( id, point ) ) continue;
		double delta[3];
		for ( int axis = 0; axis < 3; ++axis )
			delta[axis] = individually ? SnapCoord( point[axis], individualGrid ) - point[axis] : groupDelta[axis];
		if ( delta[0] == 0 && delta[1] == 0 && delta[2] == 0 ) continue;
		if ( entity )
		{
			TranslateEntity( *entity, delta );
			for ( SolidLite &child : m_Map.solids ) if ( child.ownerEntityId == id ) TranslateSolid( child, delta );
		}
		else TranslateSolid( *solid, delta );
		changed = true;
	}
	if ( changed ) TouchContent();
	EndEdit();
	return changed;
}

//-----------------------------------------------------------------------------
// Purpose: Deletes the selected object.
//-----------------------------------------------------------------------------
bool CHammerDocument::DeleteSelection()
{
	QSet<int> ids = m_SelectedIds;
	if ( ids.isEmpty() && m_nSelectedId >= 0 )
		ids.insert( m_nSelectedId );
	if ( ids.isEmpty() )
		return false;

	BeginEdit();
	bool bRemoved = false;
	for ( auto it = m_Map.solids.begin(); it != m_Map.solids.end(); )
		if ( it->ownerEntityId && ids.contains( it->ownerEntityId ) ) { it = m_Map.solids.erase( it ); bRemoved = true; }
		else ++it;
	for ( int id : ids )
		bRemoved = m_Map.RemoveById( id ) || bRemoved;
	if ( bRemoved )
	{
		SetSelection( QSet<int>(), -1 );
		TouchContent();
	}
	EndEdit();
	return bRemoved;
}

bool CHammerDocument::CopySelection()
{
	QList<EntityLite> entities;
	QList<SolidLite> solids;
	for ( const EntityLite &entity : m_Map.entities )
		if ( m_SelectedIds.contains( entity.id ) ) entities.append( entity );
	for ( const SolidLite &solid : m_Map.solids )
		if ( m_SelectedIds.contains( solid.id ) || ( solid.ownerEntityId && m_SelectedIds.contains( solid.ownerEntityId ) ) ) solids.append( solid );
	if ( entities.isEmpty() && solids.isEmpty() )
		return false;
	m_ClipboardEntities = entities;
	m_ClipboardSolids = solids;
	m_nClipboardDocumentIdentity = m_nDocumentIdentity;
	m_nClipboardPrimaryId = m_nSelectedId;
	m_nClipboardPasteCount = 0;
	return true;
}

bool CHammerDocument::SaveSelectionAsPrefab( const QString &path, QString *error ) const
{
	MapDocLite prefab;
	const auto canExport = [&]( const auto &object ) {
		if ( !HasProtectedVisgroupState( object.hiddenWrapper, object.editorProperties ) ) return true;
		if ( error ) *error = tr( "Prefabs cannot include hidden objects or visgroup memberships." );
		return false;
	};
	for ( const EntityLite &entity : m_Map.entities )
		if ( m_SelectedIds.contains( entity.id ) )
		{
			if ( !canExport( entity ) ) return false;
			prefab.entities.append( entity );
		}
	for ( const SolidLite &solid : m_Map.solids )
		if ( m_SelectedIds.contains( solid.ownerEntityId ) || m_SelectedIds.contains( solid.id ) )
		{
			if ( !canExport( solid ) ) return false;
			prefab.solids.append( solid );
		}
	if ( prefab.entities.isEmpty() && prefab.solids.isEmpty() )
	{
		if ( error ) *error = tr( "Select objects to create a prefab." );
		return false;
	}
	prefab.SetNextId( m_Map.PeekNextId() );
	prefab.hasVersionInfo = true;
	prefab.versionProperties = {
		{ QStringLiteral( "editorversion" ), QStringLiteral( "400" ) },
		{ QStringLiteral( "editorbuild" ), QStringLiteral( "8864" ) },
		{ QStringLiteral( "mapversion" ), QStringLiteral( "0" ) },
		{ QStringLiteral( "formatversion" ), QStringLiteral( "100" ) },
		{ QStringLiteral( "prefab" ), QStringLiteral( "1" ) } };
	return VmfIO::SaveMap( path, prefab, error );
}

bool CHammerDocument::InsertOriginalPrefab( const QString &path, QString *error )
{
	MapDocLite prefab;
	if ( !VmfIO::LoadMap( path, prefab, error ) ) return false;
	const quint64 clipboardDocument = m_nClipboardDocumentIdentity;
	const int clipboardPrimary = m_nClipboardPrimaryId;
	m_ClipboardEntities.swap( prefab.entities );
	m_ClipboardSolids.swap( prefab.solids );
	m_nClipboardPrimaryId = -1;
	m_nClipboardDocumentIdentity = m_nDocumentIdentity == 0 ? 1 : 0;
	const double noOffset[3] = {};
	const bool inserted = PasteSpecial( 1, noOffset ) >= 0;
	m_ClipboardEntities.swap( prefab.entities );
	m_ClipboardSolids.swap( prefab.solids );
	m_nClipboardPrimaryId = clipboardPrimary;
	m_nClipboardDocumentIdentity = clipboardDocument;
	if ( !inserted && error ) *error = tr( "The prefab contains no insertable objects or references unsupported editor data." );
	return inserted;
}

bool CHammerDocument::CutSelection()
{
	if ( !CopySelection() )
		return false;

	return DeleteSelection();
}

bool CHammerDocument::ClipboardSize( double size[3] ) const
{
	if ( !size || !HasClipboardObjects() ) return false;
	double lo[3] = { 0, 0, 0 }, hi[3] = { 0, 0, 0 };
	bool havePoint = false;
	auto include = [&]( const double p[3] ) {
		for ( int axis = 0; axis < 3; ++axis ) if ( !std::isfinite( p[axis] ) ) return false;
		for ( int axis = 0; axis < 3; ++axis )
		{
			if ( !havePoint ) lo[axis] = hi[axis] = p[axis];
			else { lo[axis] = qMin( lo[axis], p[axis] ); hi[axis] = qMax( hi[axis], p[axis] ); }
		}
		havePoint = true;
		return true;
	};
	for ( const EntityLite &entity : m_ClipboardEntities )
	{
		bool ownsBrush = false;
		for ( const SolidLite &solid : m_ClipboardSolids ) if ( solid.ownerEntityId == entity.id ) { ownsBrush = true; break; }
		if ( !ownsBrush && !include( entity.origin ) ) return false;
	}
	for ( const SolidLite &solid : m_ClipboardSolids ) if ( !include( solid.mins ) || !include( solid.maxs ) ) return false;
	for ( int axis = 0; axis < 3; ++axis )
	{
		const double extent = hi[axis] - lo[axis];
		if ( !std::isfinite( extent ) || extent < 0 ) return false;
		size[axis] = extent;
	}
	return havePoint;
}

bool CHammerDocument::ClipboardCenter( double center[3] ) const
{
	if ( !center || !HasClipboardObjects() ) return false;
	double lo[3] = {}, hi[3] = {};
	bool havePoint = false;
	auto include = [&]( const double point[3] ) {
		for ( int axis = 0; axis < 3; ++axis ) if ( !std::isfinite( point[axis] ) ) return false;
		for ( int axis = 0; axis < 3; ++axis )
		{
			if ( !havePoint ) lo[axis] = hi[axis] = point[axis];
			else { lo[axis] = qMin( lo[axis], point[axis] ); hi[axis] = qMax( hi[axis], point[axis] ); }
		}
		havePoint = true;
		return true;
	};
	for ( const EntityLite &entity : m_ClipboardEntities )
	{
		bool ownsBrush = false;
		for ( const SolidLite &solid : m_ClipboardSolids ) if ( solid.ownerEntityId == entity.id ) { ownsBrush = true; break; }
		if ( !ownsBrush && !include( entity.origin ) ) return false;
	}
	for ( const SolidLite &solid : m_ClipboardSolids ) if ( !include( solid.mins ) || !include( solid.maxs ) ) return false;
	if ( !havePoint ) return false;
	for ( int axis = 0; axis < 3; ++axis )
	{
		center[axis] = lo[axis] * 0.5 + hi[axis] * 0.5;
		if ( !std::isfinite( center[axis] ) ) return false;
	}
	return true;
}

bool CHammerDocument::ClipboardHasForeignVisgroupState() const
{
	if ( m_nClipboardDocumentIdentity == m_nDocumentIdentity ) return false;
	for ( const EntityLite &entity : m_ClipboardEntities )
		if ( HasProtectedVisgroupState( entity.hiddenWrapper, entity.editorProperties ) ) return true;
	for ( const SolidLite &solid : m_ClipboardSolids )
		if ( HasProtectedVisgroupState( solid.hiddenWrapper, solid.editorProperties ) ) return true;
	return false;
}

int CHammerDocument::PasteSpecial( int copies, const double step[3], const double *firstOffset )
{
	if ( !HasClipboardObjects() || ClipboardHasForeignVisgroupState() || !step || copies < 1 || copies > 256 )
		return -1;
	if ( !CanPasteOverlayFaces( m_ClipboardEntities, m_ClipboardSolids, m_nClipboardDocumentIdentity == m_nDocumentIdentity ) )
		return -1;
	for ( const SolidLite &solid : m_ClipboardSolids ) if ( solid.ownerEntityId )
	{
		bool found = false;
		for ( const EntityLite &entity : m_ClipboardEntities ) if ( entity.id == solid.ownerEntityId ) { found = true; break; }
		if ( !found ) return -1;
	}
	// Validate all translations before starting an undo transaction or mutating
	// the map. Intermediate copies lie between zero and this final offset.
	for ( int axis = 0; axis < 3; ++axis )
	{
		const double initial = firstOffset ? firstOffset[axis] : step[axis];
		const double offset = initial + step[axis] * ( copies - 1 );
		if ( !std::isfinite( step[axis] ) || !std::isfinite( initial ) || !std::isfinite( offset ) ) return -1;
		for ( const EntityLite &entity : m_ClipboardEntities )
			if ( !std::isfinite( entity.origin[axis] ) || !std::isfinite( entity.origin[axis] + initial ) || !std::isfinite( entity.origin[axis] + offset ) ) return -1;
		for ( const SolidLite &solid : m_ClipboardSolids )
		{
			if ( !std::isfinite( solid.mins[axis] ) || !std::isfinite( solid.maxs[axis] ) ||
				!std::isfinite( solid.mins[axis] + initial ) || !std::isfinite( solid.maxs[axis] + initial ) ||
				!std::isfinite( solid.mins[axis] + offset ) || !std::isfinite( solid.maxs[axis] + offset ) ) return -1;
			for ( const SideLite &side : solid.sides )
			{
				for ( const auto &point : side.planePts )
					if ( !std::isfinite( point[axis] ) || !std::isfinite( point[axis] + initial ) || !std::isfinite( point[axis] + offset ) ) return -1;
				for ( const QString &vertex : side.verticesPlus )
				{
					double point[3];
					if ( !ParseVec3( vertex, point ) || !std::isfinite( point[axis] ) ||
						!std::isfinite( point[axis] + initial ) || !std::isfinite( point[axis] + offset ) ) return -1;
				}
			}
		}
	}
	BeginEdit();
	QSet<int> pastedIds;
	int primaryId = -1;
	for ( int copy = 0; copy < copies; ++copy )
	{
		QMap<int, int> owners;
		QMap<int, int> faceIds;
		const int firstEntity = m_Map.entities.size();
		const double offset[3] = {
			( firstOffset ? firstOffset[0] : step[0] ) + step[0] * copy,
			( firstOffset ? firstOffset[1] : step[1] ) + step[1] * copy,
			( firstOffset ? firstOffset[2] : step[2] ) + step[2] * copy };
		for ( const EntityLite &original : m_ClipboardEntities )
		{
			EntityLite entity = original;
			entity.id = m_Map.AllocId();
			owners.insert( original.id, entity.id );
			TranslateEntity( entity, offset );
			if ( primaryId < 0 || original.id == m_nClipboardPrimaryId ) primaryId = entity.id;
			pastedIds.insert( entity.id );
			m_Map.entities.append( entity );
		}
		for ( const SolidLite &original : m_ClipboardSolids )
		{
			SolidLite solid = original;
			solid.id = m_Map.AllocId();
			if ( solid.ownerEntityId ) solid.ownerEntityId = owners.value( solid.ownerEntityId, 0 );
			for ( SideLite &side : solid.sides )
			{
				const int oldId = side.id;
				side.id = m_Map.AllocId();
				faceIds.insert( oldId, side.id );
			}
			TranslateSolid( solid, offset );
			if ( primaryId < 0 || original.id == m_nClipboardPrimaryId ) primaryId = solid.id;
			if ( !solid.ownerEntityId ) pastedIds.insert( solid.id );
			m_Map.solids.append( solid );
		}
		RemapPastedOverlayFaces( m_Map, firstEntity, faceIds );
	}
	SetSelection( pastedIds, primaryId );
	TouchContent();
	EndEdit();
	return primaryId;
}

int CHammerDocument::PasteSelection()
{
	if ( ( m_ClipboardEntities.isEmpty() && m_ClipboardSolids.isEmpty() ) || ClipboardHasForeignVisgroupState() )
		return -1;
	if ( !CanPasteOverlayFaces( m_ClipboardEntities, m_ClipboardSolids, m_nClipboardDocumentIdentity == m_nDocumentIdentity ) )
		return -1;
	for ( const SolidLite &solid : m_ClipboardSolids ) if ( solid.ownerEntityId )
	{
		bool found = false;
		for ( const EntityLite &entity : m_ClipboardEntities ) if ( entity.id == solid.ownerEntityId ) { found = true; break; }
		if ( !found ) return -1;
	}

	BeginEdit();
	const double offset[3] = { 32.0 * ( m_nClipboardPasteCount + 1 ), 0.0, 0.0 };
	int pastedId = -1;
	QSet<int> pastedIds;
	QMap<int, int> owners;
	QMap<int, int> faceIds;
	const int firstEntity = m_Map.entities.size();
	for ( const EntityLite &original : m_ClipboardEntities )
	{
		EntityLite entity = original;
		entity.id = m_Map.AllocId();
		owners.insert( original.id, entity.id );
		TranslateEntity( entity, offset );
		if ( pastedId < 0 || original.id == m_nClipboardPrimaryId ) pastedId = entity.id;
		pastedIds.insert( entity.id );
		m_Map.entities.append( entity );
	}
	for ( const SolidLite &original : m_ClipboardSolids )
	{
		SolidLite solid = original;
		solid.id = m_Map.AllocId();
		if ( solid.ownerEntityId ) solid.ownerEntityId = owners.value( solid.ownerEntityId, 0 );
		for ( SideLite &side : solid.sides )
		{
			const int oldId = side.id;
			side.id = m_Map.AllocId();
			faceIds.insert( oldId, side.id );
		}
		TranslateSolid( solid, offset );
		if ( pastedId < 0 || original.id == m_nClipboardPrimaryId ) pastedId = solid.id;
		if ( !solid.ownerEntityId ) pastedIds.insert( solid.id );
		m_Map.solids.append( solid );
	}
	RemapPastedOverlayFaces( m_Map, firstEntity, faceIds );
	++m_nClipboardPasteCount;
	SetSelection( pastedIds, pastedId );
	TouchContent();
	EndEdit();
	return pastedId;
}

//-----------------------------------------------------------------------------
// Purpose: Marks the map content as modified and notifies the views.
//-----------------------------------------------------------------------------
void CHammerDocument::TouchContent()
{
	if ( m_nEditDepth > 0 )
		m_bEditChanged = true;
	SetDirty( true );
	emit documentChanged();
}

CHammerDocument::HistoryState CHammerDocument::CaptureHistoryState() const
{
	HistoryState state;
	state.map = m_Map;
	state.selectedId = m_nSelectedId;
	state.selectedIds = m_SelectedIds;
	state.revision = m_nRevision;
	return state;
}

void CHammerDocument::ResetHistory()
{
	m_UndoHistory.clear();
	m_RedoHistory.clear();
	m_nEditDepth = 0;
	m_bEditChanged = false;
	m_nRevision = 0;
	m_nNextRevision = 1;
	m_nSavedRevision = 0;
}

void CHammerDocument::UpdateHistoryDirty()
{
	SetDirty( m_nRevision != m_nSavedRevision );
}

void CHammerDocument::BeginEdit()
{
	if ( m_nEditDepth++ == 0 )
	{
		m_EditBefore = CaptureHistoryState();
		m_bEditChanged = false;
	}
}

void CHammerDocument::EndEdit()
{
	if ( m_nEditDepth <= 0 || --m_nEditDepth != 0 || !m_bEditChanged )
		return;
	HistoryEntry entry;
	entry.before = m_EditBefore;
	m_nRevision = m_nNextRevision++;
	entry.after = CaptureHistoryState();
	m_UndoHistory.append( entry );
	if ( m_UndoHistory.size() > 100 )
		m_UndoHistory.removeFirst();
	m_RedoHistory.clear();
	UpdateHistoryDirty();
}

void CHammerDocument::CancelEdit()
{
	if ( m_nEditDepth <= 0 )
		return;
	m_nEditDepth = 0;
	const bool changed = m_bEditChanged;
	m_bEditChanged = false;
	if ( changed )
		RestoreHistoryState( m_EditBefore );
}

void CHammerDocument::RestoreHistoryState( const HistoryState &state )
{
	m_Map = state.map;
	for ( auto it = m_ViewFilter.quickHiddenIds.begin(); it != m_ViewFilter.quickHiddenIds.end(); )
		if ( !m_Map.EntityById( *it ) && !m_Map.SolidById( *it ) ) it = m_ViewFilter.quickHiddenIds.erase( it );
		else ++it;
	for ( auto it = m_ViewFilter.visgroupForceShownIds.begin(); it != m_ViewFilter.visgroupForceShownIds.end(); )
		if ( !m_Map.EntityById( *it ) && !m_Map.SolidById( *it ) ) it = m_ViewFilter.visgroupForceShownIds.erase( it );
		else ++it;
	for ( auto it = m_ViewFilter.visgroupForceHiddenIds.begin(); it != m_ViewFilter.visgroupForceHiddenIds.end(); )
		if ( !m_Map.EntityById( *it ) && !m_Map.SolidById( *it ) ) it = m_ViewFilter.visgroupForceHiddenIds.erase( it );
		else ++it;
	SetSelection( state.selectedIds, state.selectedId );
	m_nRevision = state.revision;
	UpdateHistoryDirty();
	emit documentChanged();
}

bool CHammerDocument::Undo()
{
	if ( m_nEditDepth || m_UndoHistory.isEmpty() )
		return false;
	const HistoryEntry entry = m_UndoHistory.takeLast();
	m_RedoHistory.append( entry );
	RestoreHistoryState( entry.before );
	return true;
}

bool CHammerDocument::Redo()
{
	if ( m_nEditDepth || m_RedoHistory.isEmpty() )
		return false;
	const HistoryEntry entry = m_RedoHistory.takeLast();
	m_UndoHistory.append( entry );
	RestoreHistoryState( entry.after );
	return true;
}

bool CHammerDocument::SetSelectedEntityClass( const QString &className )
{
	if ( className.isEmpty() )
		return false;
	bool brushEntity = false;
	for ( const SolidLite &solid : m_Map.solids ) if ( solid.ownerEntityId == m_nSelectedId ) { brushEntity = true; break; }
	if ( brushEntity && !m_Fgd.SolidClassNames().contains( className, Qt::CaseInsensitive ) ) return false;
	if ( !brushEntity && m_Fgd.SolidClassNames().contains( className, Qt::CaseInsensitive ) ) return false;
	BeginEdit();
	EntityLite *entity = m_Map.EntityById( m_nSelectedId );
	if ( !entity || entity->classname == className )
	{
		EndEdit();
		return false;
	}
	entity->classname = className;
	entity->keyvalues = m_Fgd.DefaultKeyValues( className );
	SetSelection( m_SelectedIds, m_nSelectedId );
	TouchContent();
	EndEdit();
	return true;
}

bool CHammerDocument::SetSelectedEntityKey( const QString &key, const QString &value )
{
	BeginEdit();
	EntityLite *entity = m_Map.EntityById( m_nSelectedId );
	if ( !entity || ( entity->keyvalues.contains( key ) && entity->keyvalues.value( key ) == value ) )
	{
		EndEdit();
		return false;
	}
	if ( entity->classname.compare( QLatin1String( "info_overlay" ), Qt::CaseInsensitive ) == 0 &&
		 key.compare( QLatin1String( "sides" ), Qt::CaseInsensitive ) == 0 )
	{
		EntityLite test = *entity;
		HammerOverlay::Set( test, key, value );
		HammerOverlay::Data overlay;
		if ( !HammerOverlay::Parse( test, overlay ) || overlay.sides.size() > 64 )
		{
			EndEdit();
			return false;
		}
		QSet<int> match;
		for ( const SolidLite &solid : static_cast<const MapDocLite &>( m_Map ).solids )
			for ( const SideLite &side : solid.sides )
				if ( overlay.sides.contains( side.id ) )
				{
					if ( match.contains( side.id ) ) { EndEdit(); return false; }
					match.insert( side.id );
				}
		if ( match.size() != overlay.sides.size() ) { EndEdit(); return false; }
		HammerOverlay::Set( *entity, key, value );
	}
	else entity->keyvalues.insert( key, value );
	TouchContent();
	EndEdit();
	return true;
}

// Batch edits only apply to a homogeneous selection. Preserve each entity's
// existing key spelling and reject ambiguous case-insensitive duplicates.
bool CHammerDocument::SetSelectedEntitiesKey( const QString &key, const QString &value )
{
	if ( m_SelectedIds.size() < 2 || key.isEmpty() ||
		 key.compare( QLatin1String( "classname" ), Qt::CaseInsensitive ) == 0 ||
		 key.compare( QLatin1String( "origin" ), Qt::CaseInsensitive ) == 0 ) return false;
	QMap<int, QString> keys;
	QString className;
	bool changed = false;
	for ( int id : m_SelectedIds )
	{
		const EntityLite *entity = m_Map.EntityById( id );
		if ( !entity || entity->classname.isEmpty() ) return false;
		if ( className.isEmpty() ) className = entity->classname;
		else if ( className.compare( entity->classname, Qt::CaseInsensitive ) != 0 ) return false;
		QString actualKey;
		for ( auto it = entity->keyvalues.cbegin(); it != entity->keyvalues.cend(); ++it )
			if ( it.key().compare( key, Qt::CaseInsensitive ) == 0 )
			{
				if ( !actualKey.isEmpty() ) return false;
				actualKey = it.key();
			}
		if ( actualKey.isEmpty() ) { actualKey = key; changed = true; }
		else if ( entity->keyvalues.value( actualKey ) != value ) changed = true;
		keys.insert( id, actualKey );
	}
	if ( !changed ) return false;
	BeginEdit();
	for ( auto it = keys.cbegin(); it != keys.cend(); ++it )
		m_Map.EntityById( it.key() )->keyvalues.insert( it.value(), value );
	TouchContent(); EndEdit();
	return true;
}

bool CHammerDocument::ResetSelectedEntityKey( const QString &key )
{
	if ( m_SelectedIds.size() != 1 || key.compare( QLatin1String( "classname" ), Qt::CaseInsensitive ) == 0 ||
		 key.compare( QLatin1String( "origin" ), Qt::CaseInsensitive ) == 0 ) return false;
	const EntityLite *entity = m_Map.EntityById( m_nSelectedId );
	QString value;
	if ( !entity || !m_Fgd.PropertyDefault( entity->classname, key, value ) ) return false;
	QString actualKey = key;
	for ( auto it = entity->keyvalues.constBegin(); it != entity->keyvalues.constEnd(); ++it )
		if ( it.key().compare( key, Qt::CaseInsensitive ) == 0 ) { actualKey = it.key(); break; }
	return SetSelectedEntityKey( actualKey, value );
}

bool CHammerDocument::SetSelectedEntityOutputs( const QList<QPair<QString, QString> > &outputs )
{
	if ( m_SelectedIds.size() != 1 ) return false;
	EntityLite *entity = m_Map.EntityById( m_nSelectedId );
	if ( !entity || entity->connections == outputs ) return false;
	for ( const auto &output : outputs )
		if ( output.first.trimmed().isEmpty() || output.first.contains( QLatin1Char( '\n' ) ) ||
			 output.first.contains( QLatin1Char( '\r' ) ) || output.second.contains( QLatin1Char( '\n' ) ) ||
			 output.second.contains( QLatin1Char( '\r' ) ) ) return false;
	BeginEdit();
	// BeginEdit snapshots the implicitly shared map. Reacquire through the
	// mutable container so it detaches before the entity is modified.
	m_Map.EntityById( m_nSelectedId )->connections = outputs;
	TouchContent();
	EndEdit();
	return true;
}

bool CHammerDocument::SetCordon( const double mins[3], const double maxs[3], bool active )
{
	const HammerCordon::Bounds bounds{ { mins[0], mins[1], mins[2] }, { maxs[0], maxs[1], maxs[2] } };
	if ( !HammerCordon::Valid( bounds ) || HammerCordon::HasOtherCordonFormat( m_Map ) ) return false;
	auto properties = m_Map.cordonProperties;
	for ( auto it = properties.begin(); it != properties.end(); )
	{
		const QString key = it->first.toLower();
		if ( key == QLatin1String( "mins" ) || key == QLatin1String( "maxs" ) || key == QLatin1String( "active" ) )
			it = properties.erase( it );
		else ++it;
	}
	properties.append( { QStringLiteral( "mins" ), QStringLiteral( "(%1)" ).arg( FormatVec3( mins ) ) } );
	properties.append( { QStringLiteral( "maxs" ), QStringLiteral( "(%1)" ).arg( FormatVec3( maxs ) ) } );
	properties.append( { QStringLiteral( "active" ), active ? QStringLiteral( "1" ) : QStringLiteral( "0" ) } );
	if ( properties == m_Map.cordonProperties ) return true;
	BeginEdit();
	m_Map.cordonProperties = properties;
	TouchContent();
	EndEdit();
	return true;
}

bool CHammerDocument::SetSavedCameras( const QList<CameraLite> &cameras, int activeIndex )
{
	if ( activeIndex < -1 || activeIndex >= cameras.size() ) return false;
	QList<QPair<QString, QString>> properties = m_Map.cameraProperties;
	bool found = false;
	for ( auto &key : properties )
		if ( key.first.compare( QLatin1String( "activecamera" ), Qt::CaseInsensitive ) == 0 )
		{
			key.second = QString::number( activeIndex );
			found = true;
		}
	if ( !found ) properties.append( qMakePair( QStringLiteral( "activecamera" ), QString::number( activeIndex ) ) );
	bool same = properties == m_Map.cameraProperties && cameras.size() == m_Map.cameras.size();
	for ( int i = 0; same && i < cameras.size(); ++i ) same = cameras[i].keyvalues == m_Map.cameras[i].keyvalues;
	if ( same ) return false;
	BeginEdit();
	m_Map.cameras = cameras;
	m_Map.cameraProperties = properties;
	TouchContent();
	EndEdit();
	return true;
}

bool CHammerDocument::SetWorldProperties( const QMap<QString, QString> &properties )
{
	// Structural VMF keys are managed by the document, not the property editor.
	for ( const QString &key : { QStringLiteral( "classname" ), QStringLiteral( "id" ), QStringLiteral( "mapversion" ) } )
	{
		QMap<QString, QString> before;
		QMap<QString, QString> after;
		for ( auto it = m_Map.worldspawn.constBegin(); it != m_Map.worldspawn.constEnd(); ++it )
			if ( it.key().compare( key, Qt::CaseInsensitive ) == 0 ) before.insert( it.key(), it.value() );
		for ( auto it = properties.constBegin(); it != properties.constEnd(); ++it )
			if ( it.key().compare( key, Qt::CaseInsensitive ) == 0 ) after.insert( it.key(), it.value() );
		if ( before != after ) return false;
	}
	QSet<QString> names;
	for ( auto it = properties.constBegin(); it != properties.constEnd(); ++it )
	{
		const QString key = it.key();
		if ( key.isEmpty() || key.contains( QLatin1Char( '\n' ) ) || key.contains( QLatin1Char( '\r' ) ) ||
			 it.value().contains( QLatin1Char( '\n' ) ) || it.value().contains( QLatin1Char( '\r' ) ) ||
			 names.contains( key.toCaseFolded() ) )
			return false;
		names.insert( key.toCaseFolded() );
	}
	if ( properties == m_Map.worldspawn )
		return false;
	BeginEdit();
	m_Map.worldspawn = properties;
	TouchContent();
	EndEdit();
	return true;
}

//-----------------------------------------------------------------------------
// Purpose: Selects one object by id (-1 clears the selection).
//-----------------------------------------------------------------------------
void CHammerDocument::SelectObject( int id )
{
	if ( const SolidLite *solid = m_Map.SolidById( id ) ) if ( solid->ownerEntityId ) id = solid->ownerEntityId;
	if ( id >= 0 && m_SelectedIds.contains( id ) )
	{
		SetSelection( m_SelectedIds, id );
		return;
	}
	QSet<int> selected;
	if ( id >= 0 && ( m_Map.EntityById( id ) || m_Map.SolidById( id ) ) )
		selected.insert( id );
	SetSelection( selected, id );
}

void CHammerDocument::SetViewFilter( const ViewFilter &filter )
{
	if ( filter == m_ViewFilter ) return;
	m_ViewFilter = filter;
	// Filtering is view-only, but hidden objects must not remain editable.
	SetSelection( m_SelectedIds, m_nSelectedId );
	emit stateChanged();
}

void CHammerDocument::SetSelection( const QSet<int> &ids, int primaryId )
{
	QSet<int> valid;
	for ( int id : ids )
	{
		if ( const SolidLite *owned = m_Map.SolidById( id ) ) if ( owned->ownerEntityId ) id = owned->ownerEntityId;
		const EntityLite *entity = m_Map.EntityById( id );
		const SolidLite *solid = m_Map.SolidById( id );
		if ( ( entity && EntitySelectable( m_Map, m_ViewFilter, *entity ) ) ||
			 ( solid && m_ViewFilter.SolidVisible( *solid ) ) )
			valid.insert( id );
	}
	if ( const SolidLite *owned = m_Map.SolidById( primaryId ) ) if ( owned->ownerEntityId ) primaryId = owned->ownerEntityId;
	if ( !valid.contains( primaryId ) )
	{
		primaryId = -1;
		for ( int id : valid )
			if ( primaryId < 0 || id < primaryId ) primaryId = id;
	}
	if ( valid == m_SelectedIds && primaryId == m_nSelectedId )
		return;
	m_SelectedIds = valid;
	m_nSelectedId = primaryId;
	emit selectionChanged( m_nSelectedId );
}

void CHammerDocument::ToggleSelection( int id )
{
	if ( const SolidLite *solid = m_Map.SolidById( id ) ) if ( solid->ownerEntityId ) id = solid->ownerEntityId;
	if ( id < 0 || ( !m_Map.EntityById( id ) && !m_Map.SolidById( id ) ) )
		return;
	QSet<int> selected = m_SelectedIds;
	if ( selected.contains( id ) )
		selected.remove( id );
	else
		selected.insert( id );
	SetSelection( selected, selected.contains( id ) ? id : m_nSelectedId );
}

void CHammerDocument::SelectAll()
{
	QSet<int> selected;
	for ( const EntityLite &entity : m_Map.entities )
		if ( EntitySelectable( m_Map, m_ViewFilter, entity ) ) selected.insert( entity.id );
	for ( const SolidLite &solid : m_Map.solids )
		if ( !solid.ownerEntityId && m_ViewFilter.SolidVisible( solid ) ) selected.insert( solid.id );
	SetSelection( selected, m_nSelectedId );
}

void CHammerDocument::InvertSelection()
{
	QSet<int> selected;
	for ( const EntityLite &entity : m_Map.entities )
		if ( EntitySelectable( m_Map, m_ViewFilter, entity ) && !m_SelectedIds.contains( entity.id ) ) selected.insert( entity.id );
	for ( const SolidLite &solid : m_Map.solids )
		if ( !solid.ownerEntityId && m_ViewFilter.SolidVisible( solid ) && !m_SelectedIds.contains( solid.id ) ) selected.insert( solid.id );
	SetSelection( selected, -1 );
}

int CHammerDocument::SelectEntitiesByProperty( const QString &key, const QString &value, bool exact )
{
	// The original Entity Report requires a non-empty value; an empty key
	// searches every entity key. Keys and values are case-insensitive.
	if ( value.isEmpty() )
		return 0;

	QSet<int> selected;
	for ( const EntityLite &entity : m_Map.entities )
	{
		auto matches = [&]( const QString &candidateKey, const QString &candidateValue )
		{
			return ( key.isEmpty() || key.compare( candidateKey, Qt::CaseInsensitive ) == 0 ) &&
				( exact ? candidateValue.compare( value, Qt::CaseInsensitive ) == 0 :
					candidateValue.contains( value, Qt::CaseInsensitive ) );
		};
		bool found = matches( QStringLiteral( "classname" ), entity.classname ) ||
			matches( QStringLiteral( "origin" ), FormatVec3( entity.origin ) );
		for ( auto it = entity.keyvalues.constBegin(); !found && it != entity.keyvalues.constEnd(); ++it )
			found = matches( it.key(), it.value() );
		if ( found )
			selected.insert( entity.id );
	}
	SetSelection( selected );
	return m_SelectedIds.size();
}

void CHammerDocument::SetTool( EHammerTool tool )
{
	if ( m_eTool == tool )
		return;
	m_eTool = tool;
	emit stateChanged();
}

void CHammerDocument::SetGridSize( int size )
{
	if ( m_nGridSize == size )
		return;
	m_nGridSize = size;
	m_bPersistGridSize = true;
	emit stateChanged();
	emit documentChanged();
}

void CHammerDocument::SetSnapEnabled( bool enabled )
{
	if ( m_bSnap == enabled )
		return;
	m_bSnap = enabled;
	m_bPersistSnap = true;
	emit stateChanged();
}

void CHammerDocument::SetGridVisible( bool visible )
{
	if ( m_bGridVisible == visible ) return;
	m_bGridVisible = visible;
	m_bPersistGridVisible = true;
	emit stateChanged();
}

void CHammerDocument::SetEntityClass( const QString &className )
{
	m_strEntityClass = className;
	emit stateChanged();
}

void CHammerDocument::SetBlockDepth( int depth )
{
	if ( depth <= 0 )
		depth = 128;
	if ( m_nBlockDepth == depth )
		return;
	m_nBlockDepth = depth;
	emit stateChanged();
}

static bool NormalizeMaterialName( const QString &name, QString &material )
{
	material = name.trimmed();
	material.replace( QLatin1Char( '\\' ), QLatin1Char( '/' ) );
	if ( material.startsWith( QLatin1String( "materials/" ), Qt::CaseInsensitive ) )
		material.remove( 0, 10 );
	if ( material.endsWith( QLatin1String( ".vmt" ), Qt::CaseInsensitive ) )
		material.chop( 4 );
	const QStringList parts = material.split( QLatin1Char( '/' ) );
	if ( material.isEmpty() || material.contains( QLatin1Char( '"' ) ) ||
		material.contains( QLatin1Char( '\n' ) ) || material.contains( QLatin1Char( '\r' ) ) ||
		material.contains( QLatin1Char( ':' ) ) || parts.contains( QString() ) ||
		parts.contains( QLatin1String( "." ) ) || parts.contains( QLatin1String( ".." ) ) )
		return false;
	return true;
}

bool CHammerDocument::SetActiveMaterial( const QString &name )
{
	QString material;
	if ( !NormalizeMaterialName( name, material ) ) return false;
	if ( m_strActiveMaterial != material )
	{
		m_strActiveMaterial = material;
		emit stateChanged();
	}
	return true;
}

const SolidLite *CHammerDocument::SingleSelectedSolid() const
{
	if ( m_SelectedIds.size() != 1 ) return nullptr;
	for ( const SolidLite &solid : m_Map.solids )
		if ( solid.id == m_nSelectedId && solid.ownerEntityId == 0 ) return &solid;
	const SolidLite *only = nullptr;
	for ( const SolidLite &solid : m_Map.solids )
		if ( solid.ownerEntityId == m_nSelectedId )
		{
			if ( only ) return nullptr;
			only = &solid;
		}
	return only;
}

bool CHammerDocument::SetSelectedFaceProperties( const QList<SideLite> &faces )
{
	const SolidLite *solid = SingleSelectedSolid();
	return solid && SetSelectedSolidFaceProperties( solid->id, faces );
}

QList<int> CHammerDocument::SelectedVisibleSolidIds() const
{
	QList<int> result;
	for ( const SolidLite &solid : m_Map.solids )
		if ( ( ( !solid.ownerEntityId && m_SelectedIds.contains( solid.id ) ) ||
			   ( solid.ownerEntityId && m_SelectedIds.contains( solid.ownerEntityId ) ) ) &&
			 HammerBrushEntityView::Visible( m_Map, m_ViewFilter, solid ) ) result.append( solid.id );
	return result;
}

bool CHammerDocument::SetSelectedSolidFaceProperties( int solidId, const QList<SideLite> &faces )
{
	return SetSelectedSolidsFaceProperties( { { solidId, faces } } );
}

bool CHammerDocument::SetSelectedSolidsFaceProperties( const QMap<int, QList<SideLite>> &facesBySolid )
{
	if ( facesBySolid.isEmpty() ) return false;
	const QList<int> selected = SelectedVisibleSolidIds();
	QSet<int> faceIds;
	QMap<int, QStringList> materialsBySolid;
	bool changed = false;
	for ( auto it = facesBySolid.cbegin(); it != facesBySolid.cend(); ++it )
	{
		if ( !selected.contains( it.key() ) ) return false;
		const SolidLite *solid = m_Map.SolidById( it.key() );
		const QList<SideLite> &faces = it.value();
		if ( !solid || solid->sides.size() != faces.size() ) return false;
		QStringList materials;
		for ( int i = 0; i < faces.size(); ++i )
		{
			const SideLite &old = solid->sides[i], &face = faces[i];
			QString material;
			if ( face.id != old.id || faceIds.contains( face.id ) || !NormalizeMaterialName( face.texture, material ) ||
				 !std::isfinite( face.uscale ) || !std::isfinite( face.vscale ) ||
				 face.uscale == 0 || face.vscale == 0 || !std::isfinite( face.rotation ) ||
				 face.lightmapscale < 1 ) return false;
			faceIds.insert( face.id );
			bool uNonzero = false, vNonzero = false;
			for ( int axis = 0; axis < 4; ++axis )
			{
				if ( !std::isfinite( face.uaxis[axis] ) || !std::isfinite( face.vaxis[axis] ) ) return false;
				if ( axis < 3 ) { uNonzero |= face.uaxis[axis] != 0; vNonzero |= face.vaxis[axis] != 0; }
				changed |= face.uaxis[axis] != old.uaxis[axis] || face.vaxis[axis] != old.vaxis[axis];
			}
			if ( !uNonzero || !vNonzero ) return false;
			changed |= material != old.texture || face.uscale != old.uscale || face.vscale != old.vscale ||
				face.rotation != old.rotation || face.lightmapscale != old.lightmapscale;
			materials.append( material );
		}
		materialsBySolid.insert( it.key(), materials );
	}
	if ( !changed ) return true;
	BeginEdit();
	for ( auto it = facesBySolid.cbegin(); it != facesBySolid.cend(); ++it )
	{
		SolidLite *target = m_Map.SolidById( it.key() );
		const QList<SideLite> &faces = it.value();
		const QStringList &materials = materialsBySolid[it.key()];
		for ( int i = 0; i < faces.size(); ++i )
		{
			SideLite &side = target->sides[i];
			side.texture = materials[i];
			for ( int axis = 0; axis < 4; ++axis ) { side.uaxis[axis] = faces[i].uaxis[axis]; side.vaxis[axis] = faces[i].vaxis[axis]; }
			side.uscale = faces[i].uscale; side.vscale = faces[i].vscale;
			side.rotation = faces[i].rotation; side.lightmapscale = faces[i].lightmapscale;
		}
	}
	TouchContent(); EndEdit();
	return true;
}

// Commit a subset of explicitly picked, visible faces without relying on the
// object selection. Only editable texture fields are copied to the model.
bool CHammerDocument::SetVisibleFaceProperties( const QMap<int, QList<SideLite>> &facesBySolid )
{
	if ( facesBySolid.isEmpty() ) return false;
	QSet<int> seen;
	QMap<int, QStringList> normalized;
	bool changed = false;
	for ( auto it = facesBySolid.cbegin(); it != facesBySolid.cend(); ++it )
	{
		const SolidLite *solid = m_Map.SolidById( it.key() );
		if ( !solid || it.value().isEmpty() || !HammerBrushEntityView::Visible( m_Map, m_ViewFilter, *solid ) ) return false;
		QStringList materials;
		for ( const SideLite &face : it.value() )
		{
			const SideLite *old = nullptr;
			int matches = 0;
			for ( const SideLite &candidate : solid->sides ) if ( candidate.id == face.id ) { old = &candidate; ++matches; }
			int globalMatches = 0;
			for ( const SolidLite &other : m_Map.solids ) for ( const SideLite &candidate : other.sides )
				if ( candidate.id == face.id ) ++globalMatches;
			QString material;
			if ( face.id <= 0 || seen.contains( face.id ) || matches != 1 || globalMatches != 1 || !NormalizeMaterialName( face.texture, material ) ||
				 !std::isfinite( face.uscale ) || !std::isfinite( face.vscale ) || face.uscale == 0 || face.vscale == 0 ||
				 !std::isfinite( face.rotation ) || face.lightmapscale < 1 ) return false;
			seen.insert( face.id );
			bool uNonzero = false, vNonzero = false;
			for ( int axis = 0; axis < 4; ++axis )
			{
				if ( !std::isfinite( face.uaxis[axis] ) || !std::isfinite( face.vaxis[axis] ) ) return false;
				if ( axis < 3 ) { uNonzero |= face.uaxis[axis] != 0; vNonzero |= face.vaxis[axis] != 0; }
				changed |= face.uaxis[axis] != old->uaxis[axis] || face.vaxis[axis] != old->vaxis[axis];
			}
			if ( !uNonzero || !vNonzero ) return false;
			changed |= material != old->texture || face.uscale != old->uscale || face.vscale != old->vscale ||
				face.rotation != old->rotation || face.lightmapscale != old->lightmapscale;
			materials.append( material );
		}
		normalized.insert( it.key(), materials );
	}
	if ( !changed ) return true;
	BeginEdit();
	for ( auto it = facesBySolid.cbegin(); it != facesBySolid.cend(); ++it )
	{
		SolidLite *solid = m_Map.SolidById( it.key() );
		for ( int i = 0; i < it.value().size(); ++i )
			for ( SideLite &side : solid->sides ) if ( side.id == it.value()[i].id )
			{
				const SideLite &face = it.value()[i];
				side.texture = normalized[it.key()][i];
				for ( int axis = 0; axis < 4; ++axis ) { side.uaxis[axis] = face.uaxis[axis]; side.vaxis[axis] = face.vaxis[axis]; }
				side.uscale = face.uscale; side.vscale = face.vscale;
				side.rotation = face.rotation; side.lightmapscale = face.lightmapscale;
			}
	}
	TouchContent(); EndEdit();
	return true;
}

bool CHammerDocument::CreateDisplacements( const QMap<int, QSet<int>> &facesBySolid, int power )
{
	return power >= 2 && power <= 4 && ReplaceDisplacements( facesBySolid, power );
}

bool CHammerDocument::RemoveDisplacements( const QMap<int, QSet<int>> &facesBySolid )
{
	return ReplaceDisplacements( facesBySolid, 0 );
}

bool CHammerDocument::ReplaceDisplacements( const QMap<int, QSet<int>> &facesBySolid, int power )
{
	if ( facesBySolid.isEmpty() ) return false;
	QMap<int, QList<QPair<int, std::optional<HammerDisplacementEdit::Chunk>>>> changes;
	for ( auto it = facesBySolid.cbegin(); it != facesBySolid.cend(); ++it )
	{
		const SolidLite *solid = static_cast<const MapDocLite &>( m_Map ).SolidById( it.key() );
		if ( !solid || it.value().isEmpty() || !HammerBrushEntityView::Visible( m_Map, m_ViewFilter, *solid ) ) return false;
		QList<HammerWinding::Polygon> polygons;
		QList<HammerWinding::Plane> planes;
		if ( power && ( solid->ownerEntityId ||
			 !HammerWinding::BuildFaces( *solid, polygons ) || !HammerWinding::BuildPlanes( *solid, planes ) ) ) return false;
		QSet<int> found;
		for ( int i = 0; i < solid->sides.size(); ++i )
		{
			const SideLite &side = solid->sides[i];
			if ( !it.value().contains( side.id ) ) continue;
			if ( found.contains( side.id ) ) return false;
			found.insert( side.id );
			if ( power )
			{
				if ( side.displacement || side.hasVerticesPlus || polygons[i].size() != 4 ) return false;
				changes[it.key()].append( qMakePair( i, std::optional<HammerDisplacementEdit::Chunk>{
					HammerDisplacementEdit::CreateFlat( power, polygons[i].first().data(), planes[i].normal.data() ) } ) );
			}
			else
			{
				if ( !side.displacement ) return false;
				changes[it.key()].append( qMakePair( i, std::optional<HammerDisplacementEdit::Chunk>{} ) );
			}
		}
		if ( found != it.value() ) return false;
	}
	BeginEdit();
	for ( auto it = changes.cbegin(); it != changes.cend(); ++it )
	{
		SolidLite *solid = m_Map.SolidById( it.key() );
		for ( const auto &change : it.value() ) solid->sides[change.first].displacement = change.second;
	}
	TouchContent();
	EndEdit();
	return true;
}

bool CHammerDocument::SetDisplacementDistances( int solidId, int sideId, const QList<double> &distances )
{
	const SolidLite *solid = static_cast<const MapDocLite &>( m_Map ).SolidById( solidId );
	if ( !solid || !HammerBrushEntityView::Visible( m_Map, m_ViewFilter, *solid ) ) return false;
	int index = -1;
	for ( int i = 0; i < solid->sides.size(); ++i )
		if ( solid->sides[i].id == sideId )
		{
			if ( index >= 0 ) return false;
			index = i;
		}
	if ( index < 0 || !solid->sides[index].displacement ) return false;
	auto updated = *solid->sides[index].displacement;
	QList<double> original;
	if ( !HammerDisplacementEdit::ReadDistances( updated, original ) || distances.size() != original.size() ) return false;
	for ( int i = 0; i < distances.size(); ++i )
		if ( distances[i] != original[i] && ( !std::isfinite( distances[i] ) || std::abs( distances[i] ) > 32768 ) ) return false;
	if ( original == distances ) return true;
	const int size = HammerDisplacementEdit::GridSize( updated );
	for ( auto &section : updated.children )
		if ( section.name.compare( QLatin1String( "distances" ), Qt::CaseInsensitive ) == 0 )
			for ( auto &row : section.keys )
			{
				const int offset = row.first.mid( 3 ).toInt() * size;
				QStringList tokens = row.second.split( QRegularExpression( QStringLiteral( "\\s+" ) ), Qt::SkipEmptyParts );
				bool changed = false;
				for ( int column = 0; column < size; ++column )
					if ( distances[offset + column] != original[offset + column] )
					{
						tokens[column] = QString::number( distances[offset + column], 'g', 16 );
						changed = true;
					}
				if ( changed ) row.second = tokens.join( QLatin1Char( ' ' ) );
			}
	BeginEdit();
	m_Map.SolidById( solidId )->sides[index].displacement = updated;
	TouchContent();
	EndEdit();
	return true;
}

bool CHammerDocument::SewDisplacements( const QMap<int, QSet<int>> &facesBySolid )
{
	using namespace HammerDisplacementSew;
	QList<Face> faces;
	for ( auto it = facesBySolid.cbegin(); it != facesBySolid.cend(); ++it )
	{
		const SolidLite *solid = static_cast<const MapDocLite &>( m_Map ).SolidById( it.key() );
		if ( !solid || it.value().isEmpty() || !HammerBrushEntityView::Visible( m_Map, m_ViewFilter, *solid ) ) return false;
		QList<HammerWinding::Polygon> polygons;
		if ( !HammerWinding::BuildFaces( *solid, polygons ) ) return false;
		QSet<int> found;
		for ( int i = 0; i < solid->sides.size(); ++i )
		{
			const SideLite &side = solid->sides[i];
			if ( !it.value().contains( side.id ) ) continue;
			if ( found.contains( side.id ) ) return false;
			found.insert( side.id );
			Face face{ solid->id, i, side, {}, {} };
			if ( !Prepare( face, polygons[i] ) ) return false;
			faces.append( std::move( face ) );
		}
		if ( found != it.value() ) return false;
	}
	QSet<int> changes;
	if ( !Sew( faces, changes ) ) return false;
	if ( changes.isEmpty() ) return true;
	BeginEdit();
	for ( int index : changes )
	{
		const Face &face = faces[index];
		m_Map.SolidById( face.solidId )->sides[face.sideIndex].displacement = face.side.displacement;
	}
	TouchContent();
	EndEdit();
	return true;
}

bool CHammerDocument::ApplyMaterialToFaces( const QMap<int, QSet<int>> &facesBySolid )
{
	if ( facesBySolid.isEmpty() ) return false;
	bool changed = false;
	QSet<int> uniqueFaceIds;
	for ( auto it = facesBySolid.cbegin(); it != facesBySolid.cend(); ++it )
	{
		const SolidLite *solid = m_Map.SolidById( it.key() );
		if ( !solid || it.value().isEmpty() || !HammerBrushEntityView::Visible( m_Map, m_ViewFilter, *solid ) ) return false;
		for ( int faceId : it.value() )
		{
			if ( faceId <= 0 || uniqueFaceIds.contains( faceId ) ) return false;
			uniqueFaceIds.insert( faceId );
			int count = 0;
			for ( const SideLite &side : solid->sides ) if ( side.id == faceId ) { ++count; changed |= side.texture != m_strActiveMaterial; }
			if ( count != 1 ) return false;
		}
	}
	if ( !changed ) return true;
	BeginEdit();
	for ( auto it = facesBySolid.cbegin(); it != facesBySolid.cend(); ++it )
		for ( SideLite &side : m_Map.SolidById( it.key() )->sides )
			if ( it.value().contains( side.id ) ) side.texture = m_strActiveMaterial;
	TouchContent(); EndEdit();
	return true;
}

bool CHammerDocument::ApplyMaterialToFace( int solidId, int faceIndex )
{
	const SolidLite *solid = m_Map.SolidById( solidId );
	if ( !solid || faceIndex < 0 || faceIndex >= solid->sides.size() ) return false;
	if ( solid->sides[faceIndex].texture == m_strActiveMaterial ) return true;
	BeginEdit();
	m_Map.SolidById( solidId )->sides[faceIndex].texture = m_strActiveMaterial;
	TouchContent();
	EndEdit();
	return true;
}

bool CHammerDocument::ApplyMaterialToBrush( int solidId )
{
	const SolidLite *solid = m_Map.SolidById( solidId );
	if ( !solid || solid->sides.isEmpty() ) return false;
	bool changed = false;
	for ( const SideLite &side : solid->sides ) changed |= side.texture != m_strActiveMaterial;
	if ( !changed ) return true;
	BeginEdit();
	for ( SideLite &side : m_Map.SolidById( solidId )->sides ) side.texture = m_strActiveMaterial;
	TouchContent();
	EndEdit();
	return true;
}

bool CHammerDocument::ApplyMaterialToSelection()
{
	QSet<int> ids = m_SelectedIds;
	if ( ids.isEmpty() && m_nSelectedId >= 0 )
		ids.insert( m_nSelectedId );
	for ( const SolidLite &solid : m_Map.solids )
		if ( solid.ownerEntityId && m_SelectedIds.contains( solid.ownerEntityId ) ) ids.insert( solid.id );
	bool hasSolid = false;
	bool changed = false;
	for ( int id : ids )
	{
		const SolidLite *solid = m_Map.SolidById( id );
		if ( !solid ) continue;
		hasSolid = true;
		for ( const SideLite &side : solid->sides )
			changed = changed || side.texture != m_strActiveMaterial;
	}
	if ( !hasSolid ) return false;
	if ( !changed )
		return true;
	BeginEdit();
	for ( int id : ids )
	{
		SolidLite *solid = m_Map.SolidById( id );
		if ( !solid ) continue;
		for ( SideLite &side : solid->sides )
			side.texture = m_strActiveMaterial;
	}
	TouchContent();
	EndEdit();
	return true;
}


int CHammerDocument::ReplaceMaterial( const QString &find, const QString &replacement, bool selectionOnly,
	MaterialReplaceMode mode )
{
	QString from, to;
	if ( mode == MaterialReplaceMode::Exact )
	{
		if ( !NormalizeMaterialName( find, from ) || !NormalizeMaterialName( replacement, to ) ) return -1;
	}
	else
	{
		from = find.trimmed(); from.replace( QLatin1Char( '\\' ), QLatin1Char( '/' ) );
		to = replacement.trimmed(); to.replace( QLatin1Char( '\\' ), QLatin1Char( '/' ) );
		if ( from.isEmpty() || ( mode == MaterialReplaceMode::PartialWhole && !NormalizeMaterialName( to, to ) ) ) return -1;
	}
	auto changedTexture = [&]( const QString &current, QString &result ) {
		const int index = current.indexOf( from, 0, Qt::CaseInsensitive );
		if ( index < 0 || ( mode == MaterialReplaceMode::Exact && current.compare( from, Qt::CaseInsensitive ) != 0 ) ) return false;
		result = mode == MaterialReplaceMode::PartialSubstring ? current.left( index ) + to + current.mid( index + from.size() ) : to;
		return result != current;
	};
	int count = 0;
	for ( const SolidLite &solid : static_cast<const QList<SolidLite> &>( m_Map.solids ) )
	{
		if ( selectionOnly && !m_SelectedIds.contains( solid.id ) && !( solid.ownerEntityId && m_SelectedIds.contains( solid.ownerEntityId ) ) ) continue;
		for ( const SideLite &side : solid.sides )
		{
			QString result, normalized;
			if ( changedTexture( side.texture, result ) )
			{
				if ( !NormalizeMaterialName( result, normalized ) || normalized != result ) return -1;
				++count;
			}
		}
	}
	if ( !count ) return 0;
	BeginEdit();
	for ( SolidLite &solid : m_Map.solids )
	{
		if ( selectionOnly && !m_SelectedIds.contains( solid.id ) && !( solid.ownerEntityId && m_SelectedIds.contains( solid.ownerEntityId ) ) ) continue;
		for ( SideLite &side : solid.sides )
		{
			QString result;
			if ( changedTexture( side.texture, result ) ) side.texture = result;
		}
	}
	TouchContent();
	EndEdit();
	return count;
}

void CHammerDocument::SetDirty( bool dirty )
{
	if ( m_bDirty == dirty )
		return;

	m_bDirty = dirty;
	emit dirtyChanged( m_bDirty );
}

bool CHammerDocument::OpenDocument()
{
	const QString fileName = QFileDialog::getOpenFileName(
		nullptr,
		tr( "Open Map" ),
		QString(),
		tr( "Valve Map Files (*.vmf);;All Files (*)" ) );

	if ( fileName.isEmpty() )
		return false;

	if ( !LoadFromFile( fileName ) )
	{
		QMessageBox::warning( nullptr, tr( "Hammer" ), tr( "Failed to open map: %1\n%2" ).arg( fileName, m_strLastError ) );
		return false;
	}

	return true;
}

bool CHammerDocument::SaveDocument()
{
	if ( m_strFileName.isEmpty() )
		return SaveDocumentAs();

	if ( SaveToFile( m_strFileName ) )
		return true;
	QMessageBox::warning( nullptr, tr( "Hammer" ), tr( "Failed to save map: %1\n%2" ).arg( m_strFileName, m_strLastError ) );
	return false;
}

bool CHammerDocument::SaveDocumentAs()
{
	const QString fileName = QFileDialog::getSaveFileName(
		nullptr,
		tr( "Save Map As" ),
		QString(),
		tr( "Valve Map Files (*.vmf);;All Files (*)" ) );

	if ( fileName.isEmpty() )
		return false;

	if ( SaveToFile( fileName ) )
		return true;
	QMessageBox::warning( nullptr, tr( "Hammer" ), tr( "Failed to save map: %1\n%2" ).arg( fileName, m_strLastError ) );
	return false;
}

//-----------------------------------------------------------------------------
// Purpose: Loads a VMF file into the lightweight map model.
//-----------------------------------------------------------------------------
bool CHammerDocument::LoadFromFile( const QString &fileName )
{
	MapDocLite loaded;
	m_strLastError.clear();
	if ( !VmfIO::LoadMap( fileName, loaded, &m_strLastError ) )
		return false;
	++m_nDocumentIdentity;
	m_PointFilePoints.clear();
	m_nPointFileIndex = -1;
	m_Map = loaded;
	m_ViewFilter = ViewFilter();
	m_nGridSize = 16; m_bSnap = true; m_bGridVisible = true;
	m_bPersistGridSize = m_bPersistSnap = m_bPersistGridVisible = false;
	for ( const VmfMetadataBlock &block : m_Map.extraMetadata )
	{
		if ( block.name.compare( QLatin1String( "viewsettings" ), Qt::CaseInsensitive ) != 0 ) continue;
		for ( const auto &key : block.properties )
		{
			bool ok = false;
			const int value = key.second.toInt( &ok );
			if ( key.first.compare( QLatin1String( "nGridSpacing" ), Qt::CaseInsensitive ) == 0 && ok && value >= 1 && value <= 512 && !( value & ( value - 1 ) ) )
				{ m_nGridSize = value; m_bPersistGridSize = true; }
			else if ( key.first.compare( QLatin1String( "bSnapToGrid" ), Qt::CaseInsensitive ) == 0 && ok && ( value == 0 || value == 1 ) )
				{ m_bSnap = value != 0; m_bPersistSnap = true; }
			else if ( key.first.compare( QLatin1String( "bShowGrid" ), Qt::CaseInsensitive ) == 0 && ok && ( value == 0 || value == 1 ) )
				{ m_bGridVisible = value != 0; m_bPersistGridVisible = true; }
		}
	}
	emit stateChanged();

	SetSelection( QSet<int>(), -1 );
	m_strFileName = fileName;
	ResetHistory();
	SetDirty( false );
	emit fileNameChanged( m_strFileName );
	emit documentChanged();
	emit selectionChanged( -1 );
	return true;
}

bool CHammerDocument::RestoreFromRecovery( const QString &recoveryFile, const QString &originalFile )
{
	if ( !LoadFromFile( recoveryFile ) )
		return false;
	m_strFileName = originalFile;
	m_nSavedRevision = std::numeric_limits<quint64>::max();
	SetDirty( true );
	emit fileNameChanged( m_strFileName );
	return true;
}

bool CHammerDocument::LoadPointFile( const QString &fileName, int *errorLine )
{
	if ( !ReadPointFile( fileName, m_PointFilePoints, errorLine ) ) return false;
	m_nPointFileIndex = -1;
	emit stateChanged();
	return true;
}

void CHammerDocument::UnloadPointFile()
{
	if ( m_PointFilePoints.isEmpty() ) return;
	m_PointFilePoints.clear();
	m_nPointFileIndex = -1;
	emit stateChanged();
}

bool CHammerDocument::StepPointFile( int direction, PointFilePoint &point )
{
	if ( m_PointFilePoints.isEmpty() || direction == 0 ) return false;
	const int count = m_PointFilePoints.size();
	m_nPointFileIndex += direction > 0 ? 1 : -1;
	if ( m_nPointFileIndex >= count ) m_nPointFileIndex = 0;
	else if ( m_nPointFileIndex < 0 ) m_nPointFileIndex = count - 1;
	point = m_PointFilePoints[m_nPointFileIndex];
	return true;
}

//-----------------------------------------------------------------------------
// Purpose: Writes the map as VMF.
//-----------------------------------------------------------------------------
bool CHammerDocument::SaveToFile( const QString &fileName )
{
	m_strLastError.clear();
	MapDocLite savedMap = m_Map;
	if ( m_bPersistGridSize || m_bPersistSnap || m_bPersistGridVisible )
	{
		VmfMetadataBlock *view = nullptr;
		for ( VmfMetadataBlock &block : savedMap.extraMetadata )
			if ( block.name.compare( QLatin1String( "viewsettings" ), Qt::CaseInsensitive ) == 0 ) { view = &block; break; }
		if ( !view ) { savedMap.extraMetadata.append( VmfMetadataBlock{ QStringLiteral( "viewsettings" ), {} } ); view = &savedMap.extraMetadata.last(); }
		auto setViewKey = [view]( const char *name, const QString &value ) {
			bool found = false;
			for ( auto &key : view->properties )
				if ( key.first.compare( QLatin1String( name ), Qt::CaseInsensitive ) == 0 ) { key.second = value; found = true; }
			if ( !found ) view->properties.append( qMakePair( QString::fromLatin1( name ), value ) );
		};
		if ( m_bPersistGridSize ) setViewKey( "nGridSpacing", QString::number( m_nGridSize ) );
		if ( m_bPersistSnap ) setViewKey( "bSnapToGrid", QString::number( int( m_bSnap ) ) );
		if ( m_bPersistGridVisible ) setViewKey( "bShowGrid", QString::number( int( m_bGridVisible ) ) );
	}
	if ( !VmfIO::SaveMap( fileName, savedMap, &m_strLastError ) )
		return false;
	m_Map = savedMap;

	m_strFileName = fileName;
	m_nSavedRevision = m_nRevision;
	SetDirty( false );
	emit fileNameChanged( m_strFileName );
	return true;
}

namespace
{
	bool ValidVisgroupName( const QString &name )
	{
		return !name.trimmed().isEmpty() && !name.contains( QLatin1Char( '"' ) ) &&
			!name.contains( QLatin1Char( '\\' ) ) && !name.contains( QLatin1Char( '\n' ) ) &&
			!name.contains( QLatin1Char( '\r' ) ) && !name.contains( QChar( 0 ) );
	}
	int VisgroupId( const VisGroupLite &group )
	{
		int id = 0, count = 0;
		for ( const auto &p : group.properties ) if ( p.first.compare( QLatin1String( "visgroupid" ), Qt::CaseInsensitive ) == 0 )
		{
			bool ok = false; const int n = p.second.toInt( &ok );
			if ( !ok || n <= 0 ) return 0;
			id = n; ++count;
		}
		return count == 1 ? id : 0;
	}
	VisGroupLite *FindVisgroup( QList<VisGroupLite> &groups, int id )
	{
		for ( VisGroupLite &group : groups )
		{
			if ( VisgroupId( group ) == id ) return &group;
			if ( VisGroupLite *found = FindVisgroup( group.children, id ) ) return found;
		}
		return nullptr;
	}
}

int CHammerDocument::CreateVisgroup( const QString &name, int parentId )
{
	if ( !ValidVisgroupName( name ) || parentId < 0 ) return -1;
	VisGroupLite *parent = parentId ? FindVisgroup( m_Map.visgroups, parentId ) : nullptr;
	if ( parentId && !parent ) return -1;
	// Reserve unknown references too: creating an ID must not attach an object
	// that already referred to a missing group in the original map.
	QSet<int> reserved;
	std::function<void( const QList<VisGroupLite> & )> collectGroups = [&]( const QList<VisGroupLite> &groups ) {
		for ( const VisGroupLite &group : groups ) {
			for ( const auto &p : group.properties ) if ( p.first.compare( QLatin1String( "visgroupid" ), Qt::CaseInsensitive ) == 0 )
			{ bool ok = false; const int n = p.second.toInt( &ok ); if ( ok && n > 0 ) reserved.insert( n ); }
			collectGroups( group.children );
		}
	};
	collectGroups( m_Map.visgroups );
	auto collectReferences = [&]( const QList<QPair<QString, QString>> &properties ) {
		for ( const auto &p : properties ) if ( p.first.compare( QLatin1String( "visgroupid" ), Qt::CaseInsensitive ) == 0 ||
			p.first.compare( QLatin1String( "colorvisgroupid" ), Qt::CaseInsensitive ) == 0 )
		{ bool ok = false; int n = p.second.toInt( &ok ); if ( ok && n > 0 ) reserved.insert( n ); }
	};
	for ( const EntityLite &entity : m_Map.entities ) collectReferences( entity.editorProperties );
	for ( const SolidLite &solid : m_Map.solids ) collectReferences( solid.editorProperties );
	int id = 1;
	while ( reserved.contains( id ) ) { if ( id == std::numeric_limits<int>::max() ) return -1; ++id; }
	VisGroupLite group;
	group.properties = { qMakePair( QStringLiteral( "name" ), name.trimmed() ),
		qMakePair( QStringLiteral( "visgroupid" ), QString::number( id ) ),
		qMakePair( QStringLiteral( "color" ), QStringLiteral( "128 192 255" ) ) };
	BeginEdit();
	parent = parentId ? FindVisgroup( m_Map.visgroups, parentId ) : nullptr;
	if ( parent ) parent->children.append( group ); else m_Map.visgroups.append( group );
	TouchContent(); EndEdit();
	return id;
}

bool CHammerDocument::RenameVisgroup( int groupId, const QString &name )
{
	if ( !ValidVisgroupName( name ) ) return false;
	VisGroupLite *group = FindVisgroup( m_Map.visgroups, groupId );
	if ( !group ) return false;
	int index = -1;
	for ( int i = 0; i < group->properties.size(); ++i )
		if ( group->properties[i].first.compare( QLatin1String( "name" ), Qt::CaseInsensitive ) == 0 )
		{ if ( index >= 0 ) return false; index = i; }
	const QString value = name.trimmed();
	if ( index >= 0 && group->properties[index].second == value ) return true;
	BeginEdit();
	group = FindVisgroup( m_Map.visgroups, groupId );
	if ( index < 0 ) group->properties.append( qMakePair( QStringLiteral( "name" ), value ) );
	else group->properties[index].second = value;
	TouchContent(); EndEdit();
	return true;
}

bool CHammerDocument::DeleteLeafVisgroup( int groupId )
{
	const VisGroupLite *group = FindVisgroup( m_Map.visgroups, groupId );
	if ( groupId <= 0 || !group || !group->children.isEmpty() ) return false;
	int definitions = 0;
	std::function<void( const QList<VisGroupLite> & )> countGroups = [&]( const QList<VisGroupLite> &groups ) {
		for ( const VisGroupLite &candidate : groups )
		{
			// Count every occurrence, including earlier IDs in an ambiguous
			// definition. VisgroupId deliberately returns zero for multi-ID
			// groups, but those keys must still protect referenced objects.
			for ( const auto &key : candidate.properties )
			{
				if ( key.first.compare( QLatin1String( "visgroupid" ), Qt::CaseInsensitive ) != 0 ) continue;
				bool ok = false; const int id = key.second.toInt( &ok );
				if ( ok && id == groupId ) ++definitions;
			}
			countGroups( candidate.children );
		}
	};
	countGroups( m_Map.visgroups );
	if ( definitions != 1 ) return false;
	// A colour assignment or a saved hidden member may require recalculating
	// original Hammer visibility/colour semantics, which this model cannot yet do.
	const auto safe = [groupId]( bool hidden, const QList<QPair<QString, QString>> &properties ) {
		bool member = false, colour = false;
		for ( const auto &p : properties )
		{
			bool ok = false; const int id = p.second.toInt( &ok );
			if ( !ok || id != groupId ) continue;
			if ( p.first.compare( QLatin1String( "visgroupid" ), Qt::CaseInsensitive ) == 0 ) member = true;
			if ( p.first.compare( QLatin1String( "colorvisgroupid" ), Qt::CaseInsensitive ) == 0 ) colour = true;
		}
		return !colour && !( member && ( hidden || !ViewFilter::EditorVisible( properties ) ) );
	};
	for ( const EntityLite &entity : m_Map.entities ) if ( !safe( entity.hiddenWrapper, entity.editorProperties ) ) return false;
	for ( const SolidLite &solid : m_Map.solids )
	{
		const EntityLite *owner = solid.ownerEntityId ? m_Map.EntityById( solid.ownerEntityId ) : nullptr;
		if ( !safe( solid.hiddenWrapper || ( owner && ( owner->hiddenWrapper || !ViewFilter::EditorVisible( owner->editorProperties ) ) ), solid.editorProperties ) ) return false;
	}
	BeginEdit();
	const auto removeMember = [groupId]( QList<QPair<QString, QString>> &properties ) {
		for ( auto it = properties.begin(); it != properties.end(); )
		{
			bool ok = false; const int id = it->second.toInt( &ok );
			if ( ok && id == groupId && it->first.compare( QLatin1String( "visgroupid" ), Qt::CaseInsensitive ) == 0 ) it = properties.erase( it );
			else ++it;
		}
	};
	for ( EntityLite &entity : m_Map.entities ) removeMember( entity.editorProperties );
	for ( SolidLite &solid : m_Map.solids ) removeMember( solid.editorProperties );
	std::function<bool( QList<VisGroupLite> & )> removeGroup = [&]( QList<VisGroupLite> &groups ) {
		for ( int i = 0; i < groups.size(); ++i )
		{
			if ( VisgroupId( groups[i] ) == groupId ) { groups.removeAt( i ); return true; }
			if ( removeGroup( groups[i].children ) ) return true;
		}
		return false;
	};
	removeGroup( m_Map.visgroups );
	TouchContent(); EndEdit();
	return true;
}

bool CHammerDocument::SetSelectionVisgroupMembership( int groupId, bool add )
{
	if ( m_SelectedIds.isEmpty() || !FindVisgroup( m_Map.visgroups, groupId ) ) return false;
	struct Target { int id; bool entity; };
	QList<Target> targets;
	for ( int id : m_SelectedIds )
	{
		EntityLite *entity = m_Map.EntityById( id );
		SolidLite *solid = m_Map.SolidById( id );
		if ( entity )
		{
			if ( entity->hiddenWrapper || !ViewFilter::EditorVisible( entity->editorProperties ) ) return false;
			targets.append( { id, true } );
		}
		else if ( solid && !solid->ownerEntityId )
		{
			if ( solid->hiddenWrapper || !ViewFilter::EditorVisible( solid->editorProperties ) ) return false;
			targets.append( { id, false } );
		}
		else return false; // Entity children cannot belong independently in Hammer.
	}
	bool changed = false;
	for ( const Target &target : targets )
	{
		bool present = false;
		const auto &properties = target.entity ? m_Map.EntityById( target.id )->editorProperties : m_Map.SolidById( target.id )->editorProperties;
		for ( const auto &p : properties )
		{
			bool ok = false; const int id = p.second.toInt( &ok );
			if ( ok && id == groupId && p.first.compare( QLatin1String( "visgroupid" ), Qt::CaseInsensitive ) == 0 ) present = true;
			if ( !add && ok && id == groupId && p.first.compare( QLatin1String( "colorvisgroupid" ), Qt::CaseInsensitive ) == 0 ) return false;
		}
		changed = changed || ( add ? !present : present );
	}
	if ( !changed ) return true;
	BeginEdit();
	for ( const Target &target : targets )
	{
		EntityLite *entity = target.entity ? m_Map.EntityById( target.id ) : nullptr;
		SolidLite *solid = target.entity ? nullptr : m_Map.SolidById( target.id );
		auto &properties = entity ? entity->editorProperties : solid->editorProperties;
		bool present = false;
		for ( const auto &p : properties ) if ( p.first.compare( QLatin1String( "visgroupid" ), Qt::CaseInsensitive ) == 0 && p.second.toInt() == groupId ) present = true;
		if ( add && !present ) { properties.append( qMakePair( QStringLiteral( "visgroupid" ), QString::number( groupId ) ) ); if ( entity ) entity->hasEditor = true; else solid->hasEditor = true; }
		if ( !add ) for ( auto it = properties.begin(); it != properties.end(); )
			if ( it->first.compare( QLatin1String( "visgroupid" ), Qt::CaseInsensitive ) == 0 && it->second.toInt() == groupId ) it = properties.erase( it ); else ++it;
	}
	TouchContent(); EndEdit();
	return true;
}

bool CHammerDocument::SetVisgroupVisible( int groupId, bool visible )
{
	const VisGroupLite *group = FindVisgroup( m_Map.visgroups, groupId );
	if ( !group ) return false;
	QSet<int> ids;
	std::function<void( const VisGroupLite & )> collect = [&]( const VisGroupLite &item ) {
		const int id = VisgroupId( item ); if ( id > 0 ) ids.insert( id );
		for ( const VisGroupLite &child : item.children ) collect( child );
	};
	collect( *group );
	const auto belongs = [&]( const QList<QPair<QString, QString>> &properties ) {
		for ( const auto &p : properties )
			if ( p.first.compare( QLatin1String( "visgroupid" ), Qt::CaseInsensitive ) == 0 )
			{ bool ok = false; const int id = p.second.toInt( &ok ); if ( ok && ids.contains( id ) ) return true; }
		return false;
	};
	ViewFilter filter = m_ViewFilter;
	auto setVisible = [&]( int id ) {
		if ( visible ) { filter.visgroupForceHiddenIds.remove( id ); filter.visgroupForceShownIds.insert( id ); }
		else { filter.visgroupForceShownIds.remove( id ); filter.visgroupForceHiddenIds.insert( id ); }
	};
	for ( const EntityLite &entity : m_Map.entities ) if ( belongs( entity.editorProperties ) )
	{
		setVisible( entity.id );
		// Original Hammer applies an entity's visibility to its descendants.
		for ( const SolidLite &solid : m_Map.solids ) if ( solid.ownerEntityId == entity.id ) setVisible( solid.id );
	}
	for ( const SolidLite &solid : m_Map.solids ) if ( belongs( solid.editorProperties ) ) setVisible( solid.id );
	SetViewFilter( filter ); // does not modify document, undo history or saved flags
	return true;
}

bool CHammerDocument::SelectVisgroupMembers( int groupId )
{
	if ( groupId <= 0 || !FindVisgroup( m_Map.visgroups, groupId ) ) return false;
	int definitions = 0;
	std::function<void( const QList<VisGroupLite> & )> count = [&]( const QList<VisGroupLite> &groups ) {
		for ( const VisGroupLite &group : groups )
		{
			for ( const auto &p : group.properties )
				if ( p.first.compare( QLatin1String( "visgroupid" ), Qt::CaseInsensitive ) == 0 )
				{ bool ok = false; const int id = p.second.toInt( &ok ); if ( ok && id == groupId ) ++definitions; }
			count( group.children );
		}
	};
	count( m_Map.visgroups );
	if ( definitions != 1 ) return false;
	const auto belongs = [groupId]( const QList<QPair<QString, QString>> &properties ) {
		for ( const auto &p : properties )
			if ( p.first.compare( QLatin1String( "visgroupid" ), Qt::CaseInsensitive ) == 0 )
			{ bool ok = false; const int id = p.second.toInt( &ok ); if ( ok && id == groupId ) return true; }
		return false;
	};
	QSet<int> ids;
	for ( const EntityLite &entity : m_Map.entities )
		if ( belongs( entity.editorProperties ) && EntitySelectable( m_Map, m_ViewFilter, entity ) ) ids.insert( entity.id );
	for ( const SolidLite &solid : m_Map.solids )
		if ( belongs( solid.editorProperties ) && HammerBrushEntityView::Visible( m_Map, m_ViewFilter, solid ) )
		{
			const int id = HammerBrushEntityView::SelectionId( solid );
			const EntityLite *owner = solid.ownerEntityId ? m_Map.EntityById( solid.ownerEntityId ) : nullptr;
			if ( !owner || EntitySelectable( m_Map, m_ViewFilter, *owner ) ) ids.insert( id );
		}
	// Keep the previous selection if the group has no visible members.
	if ( ids.isEmpty() ) return false;
	SetSelection( ids );
	return true;
}
