//========= Hammer Qt6 (hammer2/app) ==========================================
//
// Purpose: 2D orthogonal viewport implementation. Pure Qt QPainter drawing
//          (grid, brush wireframes, entity markers) plus the Select/Entity/
//          Block tool interactions described in the header.
//
//=============================================================================

#include "viewport2d.h"

#include "document.h"
#include "mapmodel.h"
#include "corebounds.h"
#include "viewpick.h"
#include "brushentityview.h"
#include "brushwindings.h"
#include "displacementmesh.h"
#include "theme.h"
#include "savedcameras.h"

#include <cmath>

#include <QEvent>
#include <QApplication>
#include <QContextMenuEvent>
#include <QMenu>
#include <QKeyEvent>
#include <QMouseEvent>
#include <QPainter>
#include <QPaintEvent>
#include <QWheelEvent>

namespace
{
	const int kMinGridPixelStep = 8;    // grid lines are decimated below this spacing
	const int kEntityPickRadius = 6;    // screen pixels around an entity origin
	const double kClipHandleRadius = 8.0; // screen pixels, independent of zoom
	const double kDefaultEntityHalfSize = 8.0;  // fallback marker half-size

	// Purpose: Returns the world axis (0=X, 1=Y, 2=Z) for the view's
	//          horizontal / vertical / depth axis.
	int AxisUFor( CQtViewport2D::EViewAxis axis )
	{
		switch ( axis )
		{
			case CQtViewport2D::VIEWAXIS_SIDE: return 1; // Y
			default: return 0;                           // X
		}
	}

	int AxisVFor( CQtViewport2D::EViewAxis axis )
	{
		switch ( axis )
		{
			case CQtViewport2D::VIEWAXIS_TOP: return 1; // Y
			default: return 2;                            // Z
		}
	}
} // anonymous namespace

CQtViewport2D::CQtViewport2D( EViewAxis axis, QWidget *parent )
	: QWidget( parent )
	, m_eAxis( axis )
	, m_pDocument( nullptr )
	, m_nGridSize( 16 )
	, m_bGridVisible( true )
	, m_bNudgeEnabled( false )
	, m_flZoom( 0.5 )
	, m_flCenterU( 0.0 )
	, m_flCenterV( 0.0 )
	, m_eDragMode( DRAG_NONE )
	, m_bMoveEditActive( false )
	, m_flPressU( 0.0 )
	, m_flPressV( 0.0 )
	, m_flLastMoveU( 0.0 )
	, m_flLastMoveV( 0.0 )
	, m_flBlockU0( 0.0 )
	, m_flBlockV0( 0.0 )
	, m_flBlockU1( 0.0 )
	, m_flBlockV1( 0.0 )
{
	setMinimumSize( 160, 120 );
	setMouseTracking( true );
	setFocusPolicy( Qt::ClickFocus );
}

//-----------------------------------------------------------------------------
// Purpose: Stores the document and wires the repaint signals.
//-----------------------------------------------------------------------------
void CQtViewport2D::setDocument( CHammerDocument *pDocument )
{
	FinishMoveEdit();
	ClearClipPlane();
	if ( m_pDocument )
		disconnect( m_pDocument, nullptr, this, nullptr );
	m_pDocument = pDocument;

	if ( pDocument )
	{
		void (QWidget::*updateFn)() = static_cast<void (QWidget::*)()>( &QWidget::update );
		connect( pDocument, &CHammerDocument::documentChanged, this, updateFn );
		connect( pDocument, &CHammerDocument::documentChanged, this, [this] { if ( m_eDragMode == DRAG_VERTEX || m_eDragMode == DRAG_CAMERA ) { m_eDragMode = DRAG_NONE; update(); } } );
		connect( pDocument, &CHammerDocument::selectionChanged, this, updateFn );
		connect( pDocument, &CHammerDocument::stateChanged, this, updateFn );
		connect( pDocument, &CHammerDocument::selectionChanged, this, [this] { ClearClipPlane(); if ( m_eDragMode == DRAG_VERTEX ) { m_eDragMode = DRAG_NONE; update(); } } );
		connect( pDocument, &CHammerDocument::stateChanged, this, [this] {
			if ( m_pDocument && m_pDocument->GetTool() != TOOL_CLIP ) ClearClipPlane();
			if ( m_pDocument && m_pDocument->GetTool() != TOOL_VERTEX && m_eDragMode == DRAG_VERTEX ) { m_eDragMode = DRAG_NONE; update(); }
			if ( m_pDocument && m_pDocument->GetTool() != TOOL_CAMERA && m_eDragMode == DRAG_CAMERA ) { m_eDragMode = DRAG_NONE; update(); }
		} );
	}
}

void CQtViewport2D::ClearClipPlane()
{
	if ( m_eDragMode == DRAG_CLIP ) m_eDragMode = DRAG_NONE;
	m_clipDragHandle = -1;
	if ( !m_clipReady ) return;
	m_clipReady = false;
	update();
}

void CQtViewport2D::UpdateClipDrag( double u, double v, bool moveAll )
{
	if ( m_clipDragHandle < 0 )
	{
		m_clipU1 = Snap( u ); m_clipV1 = Snap( v );
		return;
	}
	const double du = Snap( u ) - m_clipPressU, dv = Snap( v ) - m_clipPressV;
	m_clipU0 = m_clipStartU0 + ( moveAll || m_clipDragHandle == 0 ? du : 0 );
	m_clipV0 = m_clipStartV0 + ( moveAll || m_clipDragHandle == 0 ? dv : 0 );
	m_clipU1 = m_clipStartU1 + ( moveAll || m_clipDragHandle == 1 ? du : 0 );
	m_clipV1 = m_clipStartV1 + ( moveAll || m_clipDragHandle == 1 ? dv : 0 );
}

void CQtViewport2D::cycleClipMode()
{
	m_clipKeep = m_clipKeep == -1 ? 1 : m_clipKeep == 1 ? 0 : -1;
	update();
}

void CQtViewport2D::FinishMoveEdit()
{
	if ( m_bMoveEditActive )
	{
		m_bMoveEditActive = false;
		if ( m_pDocument )
			m_pDocument->EndEdit();
	}
}

void CQtViewport2D::setGridVisible( bool visible )
{
	if ( m_bGridVisible == visible )
		return;
	m_bGridVisible = visible;
	update();
}

bool CQtViewport2D::event( QEvent *event )
{
	if ( event->type() == QEvent::FocusIn || event->type() == QEvent::FocusOut )
		update();
	if ( event->type() == QEvent::UngrabMouse || event->type() == QEvent::WindowDeactivate ||
		 event->type() == QEvent::Hide || event->type() == QEvent::FocusOut )
	{
		// A completed line must survive the ordinary end of an implicit mouse
		// grab; only an interrupted drag or lost focus cancels the preview.
		if ( event->type() != QEvent::UngrabMouse || m_eDragMode == DRAG_CLIP ) ClearClipPlane();
		FinishMoveEdit();
		if ( m_eDragMode != DRAG_NONE )
		{
			m_eDragMode = DRAG_NONE;
			update();
		}
	}
	return QWidget::event( event );
}

int CQtViewport2D::AxisU() const { return AxisUFor( m_eAxis ); }
int CQtViewport2D::AxisV() const { return AxisVFor( m_eAxis ); }
int CQtViewport2D::AxisD() const
{
	const int u = AxisU();
	const int v = AxisV();
	return 3 - u - v; // the remaining axis of {0,1,2}
}

void CQtViewport2D::centerOn( const double point[3] )
{
	m_flCenterU = point[AxisU()];
	m_flCenterV = point[AxisV()];
	m_flCenterDepth = point[AxisD()];
	update();
}

void CQtViewport2D::centerCoordinates( double point[3] ) const
{
	point[AxisU()] = m_flCenterU;
	point[AxisV()] = m_flCenterV;
	point[AxisD()] = m_flCenterDepth;
}

void CQtViewport2D::setViewAxis( EViewAxis axis )
{
	if ( axis < VIEWAXIS_TOP || axis > VIEWAXIS_SIDE || axis == m_eAxis ) return;
	ClearClipPlane();
	FinishMoveEdit();
	m_eDragMode = DRAG_NONE;
	double center[3];
	centerCoordinates( center );
	m_eAxis = axis;
	centerOn( center );
}

void CQtViewport2D::contextMenuEvent( QContextMenuEvent *event )
{
	QMenu menu( this );
	QAction *top = menu.addAction( tr( "Top (XY)" ) );
	QAction *front = menu.addAction( tr( "Front (XZ)" ) );
	QAction *side = menu.addAction( tr( "Side (YZ)" ) );
	top->setCheckable( true ); front->setCheckable( true ); side->setCheckable( true );
	top->setChecked( m_eAxis == VIEWAXIS_TOP ); front->setChecked( m_eAxis == VIEWAXIS_FRONT ); side->setChecked( m_eAxis == VIEWAXIS_SIDE );
	QAction *chosen = menu.exec( event->globalPos() );
	if ( chosen == top ) setViewAxis( VIEWAXIS_TOP );
	else if ( chosen == front ) setViewAxis( VIEWAXIS_FRONT );
	else if ( chosen == side ) setViewAxis( VIEWAXIS_SIDE );
}

QPointF CQtViewport2D::WorldToScreen( double u, double v ) const
{
	const double sx = width() / 2.0 + ( u - m_flCenterU ) * m_flZoom;
	const double sy = height() / 2.0 - ( v - m_flCenterV ) * m_flZoom; // world up = screen up
	return QPointF( sx, sy );
}

void CQtViewport2D::ScreenToWorld( const QPoint &pt, double &u, double &v ) const
{
	u = m_flCenterU + ( pt.x() - width() / 2.0 ) / m_flZoom;
	v = m_flCenterV - ( pt.y() - height() / 2.0 ) / m_flZoom;
}

double CQtViewport2D::Snap( double value ) const
{
	if ( !m_pDocument || !m_pDocument->SnapEnabled() )
		return value;
	return SnapCoord( value, m_pDocument->GridSize() );
}

//-----------------------------------------------------------------------------
// Purpose: Paints background, grid, map objects, selection and rubber band.
//-----------------------------------------------------------------------------
void CQtViewport2D::paintEvent( QPaintEvent *event )
{
	Q_UNUSED( event );

	QPainter painter( this );
	painter.setRenderHint( QPainter::Antialiasing, false );

	painter.fillRect( rect(), HammerTheme::Colors().view2dPaper );

	if ( m_bGridVisible )
		DrawGrid( painter );
	DrawSolids( painter );
	DrawEntities( painter );
	if ( m_pDocument )
	{
		const auto &points = m_pDocument->PointFilePoints();
		painter.setPen( QPen( HammerTheme::Colors().pointFile, 2 ) );
		for ( int i = 1; i < points.size(); ++i )
			painter.drawLine( WorldToScreen( points[i-1].Axis( AxisU() ), points[i-1].Axis( AxisV() ) ),
				WorldToScreen( points[i].Axis( AxisU() ), points[i].Axis( AxisV() ) ) );
	}
	DrawRubberBand( painter );
	DrawClipPlane( painter );
	DrawCameraHandles( painter );
	DrawVertexHandles( painter );

	HammerTheme::DrawViewportLabel( painter, rect(), GetAxisLabel(), hasFocus() );

	HammerTheme::DrawViewportHint( painter, rect(), interactionHint() );
	const QString help = interactionHelp();
	if ( toolTip() != help ) setToolTip( help );
	HammerTheme::DrawViewportFrame( painter, rect(), hasFocus() );

	// Render hook (empty for now; to be overridden by the real renderer)
	paintGL();
}

QString CQtViewport2D::interactionHint() const
{
	switch ( m_pDocument ? m_pDocument->GetTool() : TOOL_SELECT )
	{
		case TOOL_ENTITY: return tr( "2 Entity: click to place" );
		case TOOL_BLOCK: return tr( "3 Block: drag to draw" );
		case TOOL_TEXTURE: return tr( "Shift+T: use 3D view" );
		case TOOL_CLIP: return tr( "Shift+X: draw clip line" );
		case TOOL_VERTEX: return tr( "Shift+V: drag vertex" );
		case TOOL_CAMERA: return tr( "Camera: Shift-drag to add" );
		default: return tr( "1 Select: click / drag" );
	}
}

QString CQtViewport2D::interactionHelp() const
{
	QString toolHint;
	if ( m_pDocument )
	{
		switch ( m_pDocument->GetTool() )
		{
			case TOOL_CAMERA:
				toolHint = tr( "Camera (Shift+C): Shift-drag to add, or drag an existing camera handle" );
				break;
			case TOOL_ENTITY:
				toolHint = tr( "Entity (2): click to place %1 | MMB pan | wheel zoom" ).arg( m_pDocument->EntityClass() );
				break;
			case TOOL_BLOCK:
				toolHint = tr( "Block (3): drag to draw | release creates | Esc cancels | middle drag pans" );
				break;
			case TOOL_TEXTURE:
				toolHint = tr( "Material (Shift+T): apply on a 3D face | Shift-right-click applies to whole brush" );
				break;
			case TOOL_CLIP:
				toolHint = tr( "Clip (Shift+X): drag a line; drag a handle to adjust; Ctrl-drag moves both; Shift-drag starts over; Shift+X changes side; Enter applies; Esc cancels" );
				break;
			case TOOL_VERTEX:
				toolHint = tr( "Vertex (Shift+V): select one world brush or a single-brush entity, then drag a handle; invalid shapes are rejected." );
				break;
			case TOOL_SELECT:
			default:
				toolHint = tr( "Select (1): click select | drag move or box | Shift-click toggle | middle drag pan | wheel zoom" );
				break;
		}
	}
	else
		toolHint = tr( "Select (1): click select | drag move or box | Shift-click toggle | middle drag pan | wheel zoom" );
	return toolHint;
}

void CQtViewport2D::paintGL()
{
	// TODO(hammer-port): wire the materialsystem render pipeline
	// (QOpenGLWidget + shaders) to replace the QPainter placeholder.
}

//-----------------------------------------------------------------------------
// Purpose: World-stepped grid with origin axes; decimates lines that would
//          crowd the screen.
//-----------------------------------------------------------------------------
void CQtViewport2D::DrawGrid( QPainter &painter )
{
	const int w = width();
	const int h = height();

	const int gridSize = m_pDocument ? m_pDocument->GridSize() : m_nGridSize;
	double worldStep = qMax( 1, gridSize );
	while ( worldStep * m_flZoom < kMinGridPixelStep )
		worldStep *= 2;

	// Visible world range
	const double uMin = m_flCenterU - w / 2.0 / m_flZoom;
	const double uMax = m_flCenterU + w / 2.0 / m_flZoom;
	const double vMin = m_flCenterV - h / 2.0 / m_flZoom;
	const double vMax = m_flCenterV + h / 2.0 / m_flZoom;

	// Major lines every eight minor steps
	const double majorStep = worldStep * 8.0;

	// Minor vertical/horizontal lines
	painter.setPen( HammerTheme::Colors().view2dGridMinor );
	for ( double u = qFloor( uMin / worldStep ) * worldStep; u <= uMax; u += worldStep )
	{
		if ( majorStep > worldStep && std::fmod( u, majorStep ) == 0.0 )
			continue;
		const QPointF pt = WorldToScreen( u, vMin );
		const QPointF pt2 = WorldToScreen( u, vMax );
		painter.drawLine( pt, pt2 );
	}
	for ( double v = qFloor( vMin / worldStep ) * worldStep; v <= vMax; v += worldStep )
	{
		if ( majorStep > worldStep && std::fmod( v, majorStep ) == 0.0 )
			continue;
		const QPointF pt = WorldToScreen( uMin, v );
		const QPointF pt2 = WorldToScreen( uMax, v );
		painter.drawLine( pt, pt2 );
	}

	// Major lines
	painter.setPen( HammerTheme::Colors().view2dGridMajor );
	for ( double u = qFloor( uMin / majorStep ) * majorStep; u <= uMax; u += majorStep )
	{
		painter.drawLine( WorldToScreen( u, vMin ), WorldToScreen( u, vMax ) );
	}
	for ( double v = qFloor( vMin / majorStep ) * majorStep; v <= vMax; v += majorStep )
	{
		painter.drawLine( WorldToScreen( uMin, v ), WorldToScreen( uMax, v ) );
	}

	// Origin axes
	painter.setPen( HammerTheme::Colors().view2dGridAxis );
	painter.drawLine( WorldToScreen( 0, vMin ), WorldToScreen( 0, vMax ) );
	painter.drawLine( WorldToScreen( uMin, 0 ), WorldToScreen( uMax, 0 ) );
}

//-----------------------------------------------------------------------------
// Purpose: Brush wireframes (projected bounds rect per view).
//-----------------------------------------------------------------------------
void CQtViewport2D::DrawSolids( QPainter &painter )
{
	if ( !m_pDocument )
		return;

	const MapDocLite &map = m_pDocument->Map();
	const int u = AxisU();
	const int v = AxisV();

	const ViewFilter &filter = m_pDocument->GetViewFilter();
	for ( const SolidLite &solid : map.solids )
	{
		if ( !HammerBrushEntityView::Visible( map, filter, solid ) )
			continue;
		const bool bSelected = m_pDocument->IsSelected( HammerBrushEntityView::SelectionId( solid ) );
		painter.setPen( bSelected ? QPen( HammerTheme::Colors().selection, 2 ) : QPen( HammerTheme::Colors().view2dBrush, 1 ) );
		painter.setBrush( Qt::NoBrush );

		const QPointF tl = WorldToScreen( solid.mins[u], solid.maxs[v] );
		const QPointF br = WorldToScreen( solid.maxs[u], solid.mins[v] );
		painter.drawRect( QRectF( tl, br ).normalized() );
		if ( !HammerDisplacementMesh::HasTerrain( solid ) ) continue;
		QList<HammerWinding::Polygon> faces;
		if ( !HammerWinding::BuildFaces( solid, faces ) ) continue;
		for ( int face = 0; face < faces.size(); ++face )
		{
			HammerDisplacementMesh::Mesh terrain;
			if ( !HammerDisplacementMesh::Build( solid.sides[face], faces[face], terrain ) ) continue;
			const auto edge = [&]( int a, int b ) {
				const auto &start = terrain.vertices[a], &end = terrain.vertices[b];
				painter.drawLine( WorldToScreen( start[u], start[v] ), WorldToScreen( end[u], end[v] ) );
			};
			for ( int row = 0; row < terrain.width; ++row )
				for ( int col = 0; col < terrain.width; ++col )
				{
					const int index = row * terrain.width + col;
					if ( col + 1 < terrain.width && ( bSelected || row == 0 || row == terrain.width - 1 ) ) edge( index, index + 1 );
					if ( row + 1 < terrain.width && ( bSelected || col == 0 || col == terrain.width - 1 ) ) edge( index, index + terrain.width );
				}
		}
	}
}

//-----------------------------------------------------------------------------
// Purpose: Entity markers (projected bounding box + origin square + class
//          name abbreviation).
//-----------------------------------------------------------------------------
void CQtViewport2D::DrawEntities( QPainter &painter )
{
	if ( !m_pDocument )
		return;

	const MapDocLite &map = m_pDocument->Map();
	const int u = AxisU();
	const int v = AxisV();

	QFont font = painter.font();
	font.setPointSizeF( 8.0 );
	painter.setFont( font );

	const ViewFilter &filter = m_pDocument->GetViewFilter();
	for ( const EntityLite &entity : map.entities )
	{
		if ( !filter.EntityVisible( entity ) )
			continue;
		if ( HammerBrushEntityView::HasBrushes( map, entity.id ) )
			continue;
		double mins[3];
		double maxs[3];
		m_pDocument->Fgd().ClassBounds( entity.classname, mins, maxs );

		const bool bSelected = m_pDocument->IsSelected( entity.id );
		painter.setPen( bSelected ? QPen( HammerTheme::Colors().selection, 2 ) : QPen( HammerTheme::Colors().view2dEntity, 1 ) );
		painter.setBrush( Qt::NoBrush );

		const QPointF tl = WorldToScreen( entity.origin[u] + mins[u], entity.origin[v] + maxs[v] );
		const QPointF br = WorldToScreen( entity.origin[u] + maxs[u], entity.origin[v] + mins[v] );
		const QRectF box = QRectF( tl, br ).normalized();
		painter.drawRect( box );

		// Origin square
		const QPointF org = WorldToScreen( entity.origin[u], entity.origin[v] );
		painter.fillRect( QRectF( org.x() - 2, org.y() - 2, 4, 4 ),
			bSelected ? HammerTheme::Colors().selection : HammerTheme::Colors().view2dEntity );

		// Class name abbreviation above the box
		if ( filter.showEntityNames )
		{
			painter.setPen( bSelected ? HammerTheme::Colors().selection : HammerTheme::Colors().view2dEntityText );
			const QString label = entity.classname.left( 6 );
			painter.drawText( QPointF( box.left(), box.top() - 2 ), label );
		}
	}
}

void CQtViewport2D::DrawVertexHandles( QPainter &painter )
{
	if ( !m_pDocument || m_pDocument->GetTool() != TOOL_VERTEX || m_pDocument->SelectedIds().size() != 1 ) return;
	const SolidLite *solid = m_pDocument->SelectedVertexEditableSolid();
	if ( !solid ) return;
	for ( const SideLite &side : solid->sides ) if ( side.hasVerticesPlus ) return;
	QList<HammerWinding::Polygon> faces;
	if ( !HammerWinding::BuildFaces( *solid, faces ) ) return;
	painter.save(); painter.setPen( Qt::black ); painter.setBrush( QColor( 255, 245, 80 ) );
	for ( const auto &face : faces ) for ( const auto &p : face )
		painter.drawRect( QRectF( WorldToScreen( p[AxisU()], p[AxisV()] ) - QPointF( 4, 4 ), QSizeF( 8, 8 ) ) );
	if ( m_eDragMode == DRAG_VERTEX )
	{
		painter.setPen( QPen( QColor( 255, 245, 80 ), 1, Qt::DashLine ) );
		painter.drawLine( WorldToScreen( m_vertexFromU, m_vertexFromV ), WorldToScreen( m_vertexToU, m_vertexToV ) );
		painter.drawRect( QRectF( WorldToScreen( m_vertexToU, m_vertexToV ) - QPointF( 4, 4 ), QSizeF( 8, 8 ) ) );
	}
	painter.restore();
}

void CQtViewport2D::DrawCameraHandles( QPainter &painter )
{
	if ( !m_pDocument || m_pDocument->GetTool() != TOOL_CAMERA ) return;
	const auto &map = m_pDocument->Map();
	const int active = SavedCameraActiveIndex( map );
	auto draw = [&]( const double eye[3], const double look[3], bool highlighted ) {
		const QPointF a = WorldToScreen( eye[AxisU()], eye[AxisV()] );
		const QPointF b = WorldToScreen( look[AxisU()], look[AxisV()] );
		painter.setPen( QPen( highlighted ? QColor( 255, 212, 96 ) : QColor( 107, 188, 209 ), 1 ) );
		painter.drawLine( a, b );
		painter.drawEllipse( a, 4, 4 );
		painter.drawRect( QRectF( b - QPointF( 3, 3 ), QSizeF( 6, 6 ) ) );
	};
	for ( int i = 0; i < map.cameras.size(); ++i )
	{
		if ( m_eDragMode == DRAG_CAMERA && i == m_cameraIndex ) continue;
		double eye[3], look[3];
		if ( ReadSavedCameraPoints( map.cameras[i], eye, look ) ) draw( eye, look, i == active );
	}
	if ( m_eDragMode == DRAG_CAMERA ) draw( m_cameraPoints[0], m_cameraPoints[1], true );
}

void CQtViewport2D::DrawClipPlane( QPainter &painter )
{
	if ( !m_pDocument || m_pDocument->GetTool() != TOOL_CLIP ||
		 ( !m_clipReady && m_eDragMode != DRAG_CLIP ) ) return;
	const QPointF a = WorldToScreen( m_clipU0, m_clipV0 );
	const QPointF b = WorldToScreen( m_clipU1, m_clipV1 );
	painter.save();
	painter.setPen( QPen( QColor( 255, 180, 48 ), 2 ) );
	painter.drawLine( a, b );
	painter.setBrush( QColor( 255, 220, 125 ) );
	painter.drawEllipse( a, 4, 4 ); painter.drawEllipse( b, 4, 4 );
	const double du = m_clipU1 - m_clipU0, dv = m_clipV1 - m_clipV0;
	const double len = std::hypot( du, dv );
	if ( len > 1e-9 && m_clipKeep != 0 )
	{
		// The VMF plane normal is cross(depth, line); the odd XZ axis
		// ordering reverses its 2D orientation relative to XY and YZ.
		const int parity = ( AxisU() + 1 ) % 3 == AxisV() ? 1 : -1;
		const double sign = m_clipKeep * parity;
		const QPointF mid = ( a + b ) / 2.0;
		const QPointF tip = mid + QPointF( -dv * sign / len * 18, -du * sign / len * 18 );
		painter.drawLine( mid, tip );
		painter.drawEllipse( tip, 2, 2 );
	}
	const QString side = m_clipKeep == 0 ? tr( "Keep both" ) : tr( "Keep arrow side" );
	painter.drawText( ( a + b ) / 2.0 + QPointF( 6, -7 ), side );
	painter.restore();
}

void CQtViewport2D::DrawRubberBand( QPainter &painter )
{
	if ( m_eDragMode != DRAG_BLOCK && m_eDragMode != DRAG_SELECT )
		return;

	QPen pen( HammerTheme::Colors().selection, 1, Qt::DashLine );
	painter.setPen( pen );
	painter.setBrush( HammerTheme::Colors().accentSoft );

	const QPointF tl = WorldToScreen( qMin( m_flBlockU0, m_flBlockU1 ), qMax( m_flBlockV0, m_flBlockV1 ) );
	const QPointF br = WorldToScreen( qMax( m_flBlockU0, m_flBlockU1 ), qMin( m_flBlockV0, m_flBlockV1 ) );
	painter.drawRect( QRectF( tl, br ).normalized() );
}

void CQtViewport2D::SelectRegion( double u0, double v0, double u1, double v1, bool insideOnly )
{
	if ( !m_pDocument ) return;
	const double rmin[2] = { qMin( u0, u1 ), qMin( v0, v1 ) };
	const double rmax[2] = { qMax( u0, u1 ), qMax( v0, v1 ) };
	if ( rmin[0] == rmax[0] || rmin[1] == rmax[1] ) return;
	// Hidden objects (View > Filter) are excluded by the shared helper.
	m_pDocument->SelectObjects( HammerViewPick::Region2D( *m_pDocument, AxisU(), AxisV(), rmin, rmax, insideOnly ) );
}

//-----------------------------------------------------------------------------
// Purpose: Screen-space picking: entities first (smaller targets), then
//          brushes by projected bounds. Returns an object id or -1.
//-----------------------------------------------------------------------------
int CQtViewport2D::HitTest( const QPoint &pt ) const
{
	if ( !m_pDocument )
		return -1;

	const double margin = kEntityPickRadius / m_flZoom; // world units

	double wu = 0.0;
	double wv = 0.0;
	ScreenToWorld( pt, wu, wv );

	// Hidden objects (View > Filter) are skipped by the shared pick helper.
	return HammerViewPick::Pick2D( *m_pDocument, AxisU(), AxisV(), wu, wv, margin, kDefaultEntityHalfSize );
}

//-----------------------------------------------------------------------------
// Purpose: Tool handling on mouse press.
//-----------------------------------------------------------------------------
void CQtViewport2D::mousePressEvent( QMouseEvent *event )
{
	if ( event->button() == Qt::MiddleButton )
	{
		FinishMoveEdit();
		m_eDragMode = DRAG_PAN;
		m_ptPanLast = event->pos();
		setFocus();
		event->accept();
		return;
	}
	if ( event->button() != Qt::LeftButton || !m_pDocument )
	{
		QWidget::mousePressEvent( event );
		return;
	}

	double u = 0.0;
	double v = 0.0;
	ScreenToWorld( event->pos(), u, v );

	switch ( m_pDocument->GetTool() )
	{
		case TOOL_CAMERA:
		{
			const auto &map = m_pDocument->Map();
			m_cameraIndex = -1;
			m_cameraHandle = 1;
			m_cameraPress = event->position();
			if ( map.cameras.isEmpty() || event->modifiers().testFlag( Qt::ShiftModifier ) )
			{
				double center[3]; centerCoordinates( center );
				center[AxisU()] = Snap( u );
				center[AxisV()] = Snap( v );
				for ( int k = 0; k < 3; ++k ) m_cameraPoints[0][k] = m_cameraPoints[1][k] = center[k];
			}
			else
			{
				double best = 64.0;
				for ( int i = 0; i < map.cameras.size(); ++i )
				{
					double points[2][3];
					if ( !ReadSavedCameraPoints( map.cameras[i], points[0], points[1] ) ) continue;
					for ( int h = 0; h < 2; ++h )
					{
						const QPointF d = event->position() - WorldToScreen( points[h][AxisU()], points[h][AxisV()] );
						const double distance = QPointF::dotProduct( d, d );
						if ( distance >= best ) continue;
						best = distance;
						m_cameraIndex = i;
						m_cameraHandle = h;
						for ( int j = 0; j < 2; ++j )
							for ( int k = 0; k < 3; ++k ) m_cameraPoints[j][k] = points[j][k];
					}
				}
				if ( m_cameraIndex < 0 ) break;
			}
			m_eDragMode = DRAG_CAMERA;
			update();
			break;
		}
		case TOOL_SELECT:
		{
			const int id = HitTest( event->pos() );
			if ( id < 0 )
			{
				m_eDragMode = DRAG_SELECT;
				m_ptSelectStart = event->pos();
				m_bSelectInsideOnly = event->modifiers().testFlag( Qt::ShiftModifier );
				m_flBlockU0 = m_flBlockU1 = u;
				m_flBlockV0 = m_flBlockV1 = v;
				break;
			}
			if ( event->modifiers() & Qt::ShiftModifier )
			{
				if ( id >= 0 )
					m_pDocument->ToggleSelection( id );
				m_eDragMode = DRAG_NONE;
				break;
			}
			m_pDocument->SelectObject( id );
			if ( !m_pDocument->SelectedIds().isEmpty() )
			{
				m_pDocument->BeginEdit();
				m_bMoveEditActive = true;
			}
			m_eDragMode = DRAG_MOVE;
			m_flPressU = u;
			m_flPressV = v;
			m_flLastMoveU = Snap( u );
			m_flLastMoveV = Snap( v );
			break;
		}
		case TOOL_ENTITY:
		{
			const QString className = m_pDocument->EntityClass();
			if ( className.isEmpty() )
				break;

			double origin[3] = { 0.0, 0.0, 0.0 };
			origin[AxisU()] = Snap( u );
			origin[AxisV()] = Snap( v );

			// When the depth axis is Z the click does not determine it: use
			// the FGD placement height (class bounds center) instead of 0
			if ( AxisD() == 2 )
				origin[2] = m_pDocument->Fgd().DefaultPlaceZ( className );

			m_pDocument->PlaceEntity( className, origin );
			break;
		}
		case TOOL_BLOCK:
		{
			m_eDragMode = DRAG_BLOCK;
			m_flBlockU0 = Snap( u );
			m_flBlockV0 = Snap( v );
			m_flBlockU1 = m_flBlockU0;
			m_flBlockV1 = m_flBlockV0;
			break;
		}
		case TOOL_CLIP:
		{
			if ( m_pDocument->SelectedIds().isEmpty() )
			{
				emit cursorWorldChanged( tr( "Select brushes before clipping." ) );
				break;
			}
			m_clipDragHandle = -1;
			if ( m_clipReady )
			{
				const QPointF p = event->position();
				const QPointF delta0 = p - WorldToScreen( m_clipU0, m_clipV0 );
				const QPointF delta1 = p - WorldToScreen( m_clipU1, m_clipV1 );
				const double d0 = QPointF::dotProduct( delta0, delta0 ), d1 = QPointF::dotProduct( delta1, delta1 );
				const double limit = kClipHandleRadius * kClipHandleRadius;
				if ( d0 <= limit || d1 <= limit )
				{
					m_clipDragHandle = d0 <= d1 ? 0 : 1;
					m_clipPressU = Snap( u ); m_clipPressV = Snap( v );
					m_clipStartU0 = m_clipU0; m_clipStartV0 = m_clipV0;
					m_clipStartU1 = m_clipU1; m_clipStartV1 = m_clipV1;
					m_eDragMode = DRAG_CLIP;
					break;
				}
				// Like the original clipper, a blank click leaves the current
				// plane intact; Shift-drag starts a replacement line.
				if ( !( event->modifiers() & Qt::ShiftModifier ) ) break;
			}
			m_clipReady = false;
			m_eDragMode = DRAG_CLIP;
			m_clipU0 = m_clipU1 = Snap( u );
			m_clipV0 = m_clipV1 = Snap( v );
			break;
		}
		case TOOL_VERTEX:
		{
			if ( m_pDocument->SelectedIds().size() != 1 ) break;
			const SolidLite *solid = m_pDocument->SelectedVertexEditableSolid();
			if ( !solid ) break;
			bool cached = false; for ( const SideLite &side : solid->sides ) cached = cached || side.hasVerticesPlus;
			if ( cached ) break;
			QList<HammerWinding::Polygon> faces;
			if ( !HammerWinding::BuildFaces( *solid, faces ) ) break;
			double best = 64.0; bool hit = false;
			for ( const auto &face : faces ) for ( const auto &point : face )
			{
				const QPointF delta = event->position() - WorldToScreen( point[AxisU()], point[AxisV()] );
				const double d = QPointF::dotProduct( delta, delta );
				if ( d < best ) { best = d; m_vertexFromU = point[AxisU()]; m_vertexFromV = point[AxisV()]; hit = true; }
			}
			if ( hit ) { m_vertexToU = m_vertexFromU; m_vertexToV = m_vertexFromV; m_eDragMode = DRAG_VERTEX; }
			break;
		}
	}

	update();
}

void CQtViewport2D::mouseMoveEvent( QMouseEvent *event )
{
	double u = 0.0;
	double v = 0.0;
	ScreenToWorld( event->pos(), u, v );

	// Status bar coordinates
	emit cursorWorldChanged( QStringLiteral( "%1: %2  %3: %4" )
		.arg( AxisNameU(), FormatVmfNumber( Snap( u ) ),
			AxisNameV(), FormatVmfNumber( Snap( v ) ) ) );

	if ( !m_pDocument )
	{
		QWidget::mouseMoveEvent( event );
		return;
	}
	if ( m_eDragMode == DRAG_PAN && ( event->buttons() & Qt::MiddleButton ) )
	{
		const QPoint delta = event->pos() - m_ptPanLast;
		m_ptPanLast = event->pos();
		m_flCenterU -= delta.x() / m_flZoom;
		m_flCenterV += delta.y() / m_flZoom;
		update();
		event->accept();
		return;
	}

	if ( m_eDragMode == DRAG_MOVE && !m_pDocument->SelectedIds().isEmpty() &&
		( event->buttons() & Qt::LeftButton ) )
	{
		const double su = Snap( u );
		const double sv = Snap( v );
		double delta[3] = { 0.0, 0.0, 0.0 };
		delta[AxisU()] = su - m_flLastMoveU;
		delta[AxisV()] = sv - m_flLastMoveV;

		if ( delta[0] != 0.0 || delta[1] != 0.0 || delta[2] != 0.0 )
		{
			if ( m_pDocument->MoveSelection( delta ) )
			{
				m_flLastMoveU = su;
				m_flLastMoveV = sv;
			}
		}
	}
	else if ( m_eDragMode == DRAG_SELECT && ( event->buttons() & Qt::LeftButton ) )
	{
		m_flBlockU1 = u;
		m_flBlockV1 = v;
		update();
	}
	else if ( m_eDragMode == DRAG_BLOCK )
	{
		m_flBlockU1 = Snap( u );
		m_flBlockV1 = Snap( v );
		update();
	}
	else if ( m_eDragMode == DRAG_CLIP && ( event->buttons() & Qt::LeftButton ) )
	{
		UpdateClipDrag( u, v, event->modifiers().testFlag( Qt::ControlModifier ) );
		update();
	}
	else if ( m_eDragMode == DRAG_VERTEX && ( event->buttons() & Qt::LeftButton ) )
	{
		m_vertexToU = Snap( u ); m_vertexToV = Snap( v ); update();
	}
	else if ( m_eDragMode == DRAG_CAMERA && ( event->buttons() & Qt::LeftButton ) &&
	          ( m_cameraIndex < 0 || event->position() != m_cameraPress ) )
	{
		m_cameraPoints[m_cameraHandle][AxisU()] = Snap( u );
		m_cameraPoints[m_cameraHandle][AxisV()] = Snap( v );
		update();
	}

	QWidget::mouseMoveEvent( event );
}

void CQtViewport2D::mouseReleaseEvent( QMouseEvent *event )
{
	if ( event->button() == Qt::MiddleButton && m_eDragMode == DRAG_PAN )
	{
		m_eDragMode = DRAG_NONE;
		event->accept();
		return;
	}
	if ( event->button() != Qt::LeftButton )
	{
		QWidget::mouseReleaseEvent( event );
		return;
	}
	if ( m_eDragMode == DRAG_MOVE )
		FinishMoveEdit();
	if ( m_eDragMode == DRAG_CAMERA && m_pDocument )
	{
		double u, v; ScreenToWorld( event->pos(), u, v );
		const bool moved = event->position() != m_cameraPress;
		if ( m_cameraIndex < 0 || moved )
		{
			m_cameraPoints[m_cameraHandle][AxisU()] = Snap( u );
			m_cameraPoints[m_cameraHandle][AxisV()] = Snap( v );
		}
		QList<CameraLite> cameras = m_pDocument->Map().cameras;
		const int index = m_cameraIndex < 0 ? cameras.size() : m_cameraIndex;
		CameraLite camera = index < cameras.size() ? cameras[index] : CameraLite{};
		const QString keys[] = { QStringLiteral( "position" ), QStringLiteral( "look" ) };
		for ( int h = 0; h < 2; ++h )
		{
			if ( m_cameraIndex >= 0 && ( !moved || h != m_cameraHandle ) ) continue;
			const QString value = QStringLiteral( "[%1]" ).arg( FormatVec3( m_cameraPoints[h] ) );
			bool found = false;
			for ( auto &pair : camera.keyvalues )
				if ( pair.first.compare( keys[h], Qt::CaseInsensitive ) == 0 ) { pair.second = value; found = true; }
			if ( !found ) camera.keyvalues.append( qMakePair( keys[h], value ) );
		}
		double eye[3], angles[3];
		if ( ReadSavedCamera( camera, eye, angles ) )
		{
			if ( index < cameras.size() ) cameras[index] = camera;
			else cameras.append( camera );
			m_eDragMode = DRAG_NONE;
			m_pDocument->SetSavedCameras( cameras, index );
			emit savedCameraActivated( index );
		}
	}
	if ( m_eDragMode == DRAG_VERTEX && m_pDocument )
	{
		double u, v; ScreenToWorld( event->pos(), u, v );
		m_vertexToU = Snap( u ); m_vertexToV = Snap( v );
		if ( ( m_vertexToU != m_vertexFromU || m_vertexToV != m_vertexFromV ) &&
			 !m_pDocument->MoveSelectedProjectedVertices( AxisU(), AxisV(), m_vertexFromU, m_vertexFromV, m_vertexToU, m_vertexToV ) )
			emit cursorWorldChanged( tr( "Vertex move rejected: the brush must remain planar and convex." ) );
	}
	if ( m_eDragMode == DRAG_CLIP )
	{
		double u, v; ScreenToWorld( event->pos(), u, v );
		UpdateClipDrag( u, v, event->modifiers().testFlag( Qt::ControlModifier ) );
		if ( m_clipDragHandle >= 0 && m_clipU0 == m_clipU1 && m_clipV0 == m_clipV1 )
		{
			// A zero-length plane cannot be applied. Keep the previous valid
			// preview so the user can adjust it without recreating it.
			m_clipU0 = m_clipStartU0; m_clipV0 = m_clipStartV0;
			m_clipU1 = m_clipStartU1; m_clipV1 = m_clipStartV1;
		}
		m_clipReady = m_clipU0 != m_clipU1 || m_clipV0 != m_clipV1;
		m_clipDragHandle = -1;
	}
	if ( m_eDragMode == DRAG_SELECT && m_pDocument )
	{
		double u, v;
		ScreenToWorld( event->pos(), u, v );
		if ( ( event->pos() - m_ptSelectStart ).manhattanLength() >= QApplication::startDragDistance() )
			SelectRegion( m_flBlockU0, m_flBlockV0, u, v, m_bSelectInsideOnly );
		else if ( !m_bSelectInsideOnly )
			m_pDocument->SelectObject( -1 );
	}

	if ( m_eDragMode == DRAG_BLOCK && m_pDocument )
	{
		// Build the box solid from the (snapped) rubber band rect; the depth
		// axis is centered on the world origin plane
		double mins[3] = { 0.0, 0.0, 0.0 };
		double maxs[3] = { 0.0, 0.0, 0.0 };
		const int u = AxisU();
		const int v = AxisV();
		const int d = AxisD();

		mins[u] = qMin( m_flBlockU0, m_flBlockU1 );
		maxs[u] = qMax( m_flBlockU0, m_flBlockU1 );
		mins[v] = qMin( m_flBlockV0, m_flBlockV1 );
		maxs[v] = qMax( m_flBlockV0, m_flBlockV1 );

		const int depth = m_pDocument->BlockDepth();
		mins[d] = -depth / 2.0;
		maxs[d] = depth / 2.0;

		const int id = m_pDocument->CreateBrush( mins, maxs );
		if ( id < 0 )
			m_pDocument->SelectObject( -1 );
	}

	m_eDragMode = DRAG_NONE;
	update();
}

//-----------------------------------------------------------------------------
// Purpose: Wheel zoom, anchored at the cursor position.
//-----------------------------------------------------------------------------
void CQtViewport2D::wheelEvent( QWheelEvent *event )
{
	const double factor = ( event->angleDelta().y() > 0 ) ? 1.15 : 1.0 / 1.15;
	const double newZoom = qBound( 1.0 / 16.0, m_flZoom * factor, 32.0 );

	// Keep the world position under the cursor fixed while zooming
	// (QWheelEvent::position() is in local widget coordinates)
	const QPointF cursor = event->position();
	const QPoint cursorPt( qRound( cursor.x() ), qRound( cursor.y() ) );
	double u = 0.0;
	double v = 0.0;
	ScreenToWorld( cursorPt, u, v );
	m_flZoom = newZoom;
	double uAfter = 0.0;
	double vAfter = 0.0;
	ScreenToWorld( cursorPt, uAfter, vAfter );
	m_flCenterU += u - uAfter;
	m_flCenterV += v - vAfter;

	update();
	event->accept();
}

void CQtViewport2D::keyPressEvent( QKeyEvent *event )
{
	if ( event->key() == Qt::Key_Escape && m_eDragMode == DRAG_VERTEX )
	{ m_eDragMode = DRAG_NONE; update(); event->accept(); return; }
	if ( event->key() == Qt::Key_Escape && m_eDragMode == DRAG_CAMERA )
	{ m_eDragMode = DRAG_NONE; update(); event->accept(); return; }
	if ( m_pDocument && m_pDocument->GetTool() == TOOL_CLIP )
	{
		if ( event->key() == Qt::Key_Escape )
		{
			ClearClipPlane(); update(); event->accept(); return;
		}
		if ( ( event->key() == Qt::Key_Return || event->key() == Qt::Key_Enter ) && m_clipReady && m_eDragMode != DRAG_CLIP )
		{
			double points[3][3] = {};
			points[0][AxisU()] = points[2][AxisU()] = m_clipU0;
			points[0][AxisV()] = points[2][AxisV()] = m_clipV0;
			points[1][AxisU()] = m_clipU1; points[1][AxisV()] = m_clipV1;
			points[2][AxisD()] = 1.0;
			m_clipReady = false; // selection notifications during the edit may clear preview
			if ( !m_pDocument->ClipSelectedByPlane( points, m_clipKeep ) )
				emit cursorWorldChanged( tr( "Clip failed: the line must cross every selected supported brush." ) );
			update(); event->accept(); return;
		}
	}
	if ( m_bNudgeEnabled && m_pDocument && m_pDocument->GetTool() == TOOL_SELECT &&
		 m_eDragMode == DRAG_NONE && !event->modifiers().testFlag( Qt::ShiftModifier ) &&
		 !event->modifiers().testFlag( Qt::AltModifier ) &&
		 ( event->key() == Qt::Key_Left || event->key() == Qt::Key_Right ||
		   event->key() == Qt::Key_Up || event->key() == Qt::Key_Down ) )
	{
		const double unit = m_pDocument->SnapEnabled() && !event->modifiers().testFlag( Qt::ControlModifier )
			? m_pDocument->GridSize() : 1.0;
		double delta[3] = {};
		if ( event->key() == Qt::Key_Left ) delta[AxisU()] = -unit;
		if ( event->key() == Qt::Key_Right ) delta[AxisU()] = unit;
		if ( event->key() == Qt::Key_Up ) delta[AxisV()] = unit;
		if ( event->key() == Qt::Key_Down ) delta[AxisV()] = -unit;
		if ( m_pDocument->MoveSelection( delta ) ) { event->accept(); return; }
	}
	if ( event->key() == Qt::Key_Escape && m_eDragMode == DRAG_MOVE )
	{
		m_eDragMode = DRAG_NONE;
		if ( m_bMoveEditActive && m_pDocument )
		{
			m_bMoveEditActive = false;
			m_pDocument->CancelEdit();
		}
		update();
		event->accept();
		return;
	}
	if ( event->key() == Qt::Key_Escape && m_eDragMode == DRAG_PAN )
	{
		m_eDragMode = DRAG_NONE;
		event->accept();
		return;
	}
	// Escape cancels the block tool rubber band
	if ( event->key() == Qt::Key_Escape && m_eDragMode == DRAG_BLOCK )
	{
		m_eDragMode = DRAG_NONE;
		update();
		event->accept();
		return;
	}
	if ( event->key() == Qt::Key_Escape && m_eDragMode == DRAG_SELECT )
	{
		m_eDragMode = DRAG_NONE;
		update();
		event->accept();
		return;
	}
	if ( event->key() == Qt::Key_Escape && m_pDocument && m_pDocument->GetTool() == TOOL_SELECT )
	{
		m_pDocument->SelectObject( -1 );
		event->accept();
		return;
	}

	QWidget::keyPressEvent( event );
}

QString CQtViewport2D::AxisNameU() const
{
	static const char *names[3] = { "X", "Y", "Z" };
	return QString::fromLatin1( names[AxisU()] );
}

QString CQtViewport2D::AxisNameV() const
{
	static const char *names[3] = { "X", "Y", "Z" };
	return QString::fromLatin1( names[AxisV()] );
}

QString CQtViewport2D::GetAxisLabel() const
{
	switch ( m_eAxis )
	{
		case VIEWAXIS_TOP:      return tr( "Top (XY)" );
		case VIEWAXIS_FRONT:    return tr( "Front (XZ)" );
		case VIEWAXIS_SIDE:     return tr( "Side (YZ)" );
	}
	return QString();
}
