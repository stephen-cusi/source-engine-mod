//========= Hammer Qt6 (hammer2/app) ==========================================
//
// Purpose: 3D perspective viewport implementation. QPainter wireframe
//          rendering with a lightweight orbit camera (yaw/pitch/distance
//          around a fixed target) and selection movement.
//
//=============================================================================

#include "viewport3d.h"
#include "overlaydata.h"

#include "document.h"
#include "mapmodel.h"
#include "brushwindings.h"
#include "facehit.h"
#include "displacementedit.h"
#include "displacementpaint.h"
#include "theme.h"

#include <cmath>
#include <algorithm>
#include <array>
#include <limits>
#include "brushentityview.h"
#include <QApplication>
#include <QContextMenuEvent>
#include <QCursor>
#include <QLinearGradient>
#include <QKeyEvent>
#include <QMenu>
#include <QMouseEvent>
#include <QPainter>
#include <QPaintEvent>
#include <QWheelEvent>
#include <QTimer>
#include <QResizeEvent>
#include <QSettings>
#include <QtOpenGLWidgets/QOpenGLWidget>

namespace
{
	const double kNearPlane = 1.0;
	const double kHalfFovTan = 1.0; // 90 degrees vertical FOV

	// Purpose: Vector helpers for the camera math.
	inline void VecSub( const double a[3], const double b[3], double out[3] )
	{
		out[0] = a[0] - b[0];
		out[1] = a[1] - b[1];
		out[2] = a[2] - b[2];
	}

	inline double VecDot( const double a[3], const double b[3] )
	{
		return a[0] * b[0] + a[1] * b[1] + a[2] * b[2];
	}

	inline void VecCross( const double a[3], const double b[3], double out[3] )
	{
		out[0] = a[1] * b[2] - a[2] * b[1];
		out[1] = a[2] * b[0] - a[0] * b[2];
		out[2] = a[0] * b[1] - a[1] * b[0];
	}

	inline void VecNormalize( double v[3] )
	{
		const double len = std::sqrt( VecDot( v, v ) );
		if ( len > 1e-8 )
		{
			v[0] /= len;
			v[1] /= len;
			v[2] /= len;
		}
	}

	// Purpose: The 12 edge index pairs of a box built from its 8 corners.
	const int kBoxEdges[12][2] =
	{
		{ 0, 1 }, { 0, 2 }, { 0, 4 }, { 1, 3 }, { 1, 5 }, { 2, 3 },
		{ 2, 6 }, { 3, 7 }, { 4, 5 }, { 4, 6 }, { 5, 7 }, { 6, 7 },
	};

	// Match the renderer's supported flat-shaded box geometry. Bounds alone
	// are not sufficient for imported sloped or otherwise complex brushes.
	bool IsAxisBox( const SolidLite &solid )
	{
		if ( solid.sides.size() != 6 ) return false;
		bool found[6] = {};
		for ( int i = 0; i < 3; ++i )
			if ( !std::isfinite( solid.mins[i] ) || !std::isfinite( solid.maxs[i] ) || solid.mins[i] >= solid.maxs[i] ) return false;
		for ( const SideLite &side : solid.sides )
		{
			double a[3], b[3], n[3];
			VecSub( side.planePts[1], side.planePts[0], a );
			VecSub( side.planePts[2], side.planePts[0], b );
			VecCross( b, a, n ); VecNormalize( n );
			int axis = -1;
			for ( int i = 0; i < 3; ++i ) if ( std::abs( n[i] ) > 0.999999999 ) axis = i;
			if ( axis < 0 ) return false;
			for ( int i = 0; i < 3; ++i ) if ( i != axis && std::abs( n[i] ) > 1e-8 ) return false;
			const int face = axis * 2 + ( n[axis] > 0 ? 1 : 0 );
			const double expected = ( face & 1 ) ? solid.maxs[axis] : solid.mins[axis];
			if ( found[face] || !std::isfinite( side.planePts[0][axis] ) || std::abs( side.planePts[0][axis] - expected ) > 0.01 ) return false;
			found[face] = true;
		}
		return true;
	}

	// Parameter t measures forward depth because the ray has forward component 1.
	bool RayBox( const double eye[3], const double ray[3], const double mins[3], const double maxs[3], double &distance )
	{
		double enter = kNearPlane, exit = std::numeric_limits<double>::infinity();
		for ( int axis = 0; axis < 3; ++axis )
		{
			if ( !std::isfinite( mins[axis] ) || !std::isfinite( maxs[axis] ) || mins[axis] > maxs[axis] ) return false;
			if ( std::abs( ray[axis] ) < 1e-12 )
			{
				if ( eye[axis] < mins[axis] || eye[axis] > maxs[axis] ) return false;
				continue;
			}
			double a = ( mins[axis] - eye[axis] ) / ray[axis];
			double b = ( maxs[axis] - eye[axis] ) / ray[axis];
			if ( a > b ) std::swap( a, b );
			enter = std::max( enter, a ); exit = std::min( exit, b );
			if ( enter > exit ) return false;
		}
		distance = enter;
		return true;
	}
} // anonymous namespace

class CTexturedViewportSurface final : public QOpenGLWidget
{
public:
	explicit CTexturedViewportSurface( CQtViewport3D *owner ) : QOpenGLWidget( owner ), m_owner( owner )
	{
		setAttribute( Qt::WA_TransparentForMouseEvents );
		setFocusPolicy( Qt::NoFocus );
	}
	~CTexturedViewportSurface() override
	{
		if ( context() ) { makeCurrent(); m_owner->m_materialRenderer.Release(); doneCurrent(); }
	}
protected:
	void initializeGL() override
	{
		if ( !m_owner->m_materialRenderer.Initialize() )
			QTimer::singleShot( 0, m_owner, [owner = m_owner] { owner->DisableTexturedSurface(); } );
	}
	void paintGL() override { m_owner->PaintScene( this, true ); }
private:
	CQtViewport3D *m_owner;
};

CQtViewport3D::CQtViewport3D( QWidget *parent )
	: QWidget( parent )
	, m_pDocument( nullptr )
	, m_flYaw( 45.0 )
	, m_flPitch( 30.0 )
	, m_flRoll( 0.0 )
	, m_flDist( 1024.0 )
	, m_bDragging( false )
	, m_displayMode( Flat )
{
	m_vTarget[0] = 0.0;
	m_vTarget[1] = 0.0;
	m_vTarget[2] = 0.0;

	setMinimumSize( 320, 240 );
	setMouseTracking( true );
	setFocusPolicy( Qt::ClickFocus );
	m_groundGridVisible = QSettings().value( QStringLiteral( "view3d/groundGridVisible" ), true ).toBool();
}

CQtViewport3D::~CQtViewport3D()
{
	delete m_texturedSurface;
}

void CQtViewport3D::setGroundGridVisible( bool visible )
{
	if ( m_groundGridVisible == visible ) return;
	m_groundGridVisible = visible;
	QSettings().setValue( QStringLiteral( "view3d/groundGridVisible" ), visible );
	UpdateViews();
}

void CQtViewport3D::setDisplayMode( DisplayMode mode )
{
	if ( m_displayMode == mode ) return;
	m_displayMode = mode;
	if ( mode == Textured )
	{
		if ( !m_texturedSurface ) m_texturedSurface = new CTexturedViewportSurface( this );
		m_texturedSurface->setGeometry( rect() );
		m_texturedSurface->show();
		QTimer::singleShot( 0, this, [this] {
			if ( m_displayMode == Textured && m_texturedSurface && !m_texturedSurface->isValid() )
				DisableTexturedSurface();
		} );
	}
	else if ( m_texturedSurface ) m_texturedSurface->hide();
	UpdateViews();
}

void CQtViewport3D::DisableTexturedSurface()
{
	if ( m_displayMode != Textured ) return;
	m_displayMode = Flat;
	if ( m_texturedSurface ) m_texturedSurface->hide();
	UpdateViews();
}

void CQtViewport3D::UpdateViews()
{
	Camera cam;
	BuildCamera( cam );
	bool changed = !m_hasNotifiedEye;
	for ( int i = 0; i < 3; ++i )
		if ( m_lastNotifiedEye[i] != cam.eye[i] ) changed = true;
	if ( changed )
	{
		for ( int i = 0; i < 3; ++i ) m_lastNotifiedEye[i] = cam.eye[i];
		m_hasNotifiedEye = true;
		emit cameraPositionChanged();
	}
	update();
	if ( m_texturedSurface && m_displayMode == Textured ) m_texturedSurface->update();
}

QImage CQtViewport3D::texturedFramebuffer()
{
	return m_texturedSurface ? m_texturedSurface->grabFramebuffer() : QImage();
}

void CQtViewport3D::resizeEvent( QResizeEvent *event )
{
	QWidget::resizeEvent( event );
	if ( m_texturedSurface ) m_texturedSurface->setGeometry( rect() );
}

void CQtViewport3D::contextMenuEvent( QContextMenuEvent *event )
{
	if ( event->reason() == QContextMenuEvent::Mouse && m_pDocument && m_pDocument->GetTool() == TOOL_TEXTURE )
	{
		m_bRightDrag = false;
		event->accept();
		return;
	}
	if ( event->reason() == QContextMenuEvent::Mouse && m_bRightDrag )
	{
		m_bRightDrag = false;
		event->accept();
		return;
	}
	QMenu menu( this );
	QAction *flat = menu.addAction( tr( "3D Flat" ) );
	QAction *wire = menu.addAction( tr( "3D Wireframe" ) );
	QAction *textured = menu.addAction( tr( "3D Textured" ) );
	flat->setCheckable( true ); wire->setCheckable( true ); textured->setCheckable( true );
	flat->setChecked( m_displayMode == Flat ); wire->setChecked( m_displayMode == Wireframe );
	textured->setChecked( m_displayMode == Textured );
	menu.addSeparator();
	QAction *groundGrid = menu.addAction( tr( "Show Ground Grid (Flat/Wireframe)" ) );
	groundGrid->setCheckable( true );
	groundGrid->setChecked( m_groundGridVisible );
	QAction *chosen = menu.exec( event->globalPos() );
	if ( chosen == flat ) setDisplayMode( Flat );
	else if ( chosen == wire ) setDisplayMode( Wireframe );
	else if ( chosen == textured ) setDisplayMode( Textured );
	else if ( chosen == groundGrid ) setGroundGridVisible( groundGrid->isChecked() );
}

// Original Hammer's default (non-mouselook) camera bindings. Each key event
// advances one step; native key repeat provides continued movement.
void CQtViewport3D::keyPressEvent( QKeyEvent *event )
{
	if ( m_overlayPickEntity >= 0 &&
		 ( event->key() == Qt::Key_Escape || event->key() == Qt::Key_Return || event->key() == Qt::Key_Enter ) )
	{
		FinishOverlayFacePick( event->key() != Qt::Key_Escape );
		event->accept();
		return;
	}
	if ( m_overlayDragEntity >= 0 )
	{
		if ( event->key() == Qt::Key_Escape ) FinishOverlayDrag( true );
		event->accept();
		return;
	}
	if ( m_displacementPaintButton != Qt::NoButton )
	{
		if ( event->key() == Qt::Key_Escape ) FinishDisplacementPaint( true );
		event->accept();
		return;
	}
	if ( m_pDocument && m_pDocument->GetTool() == TOOL_TEXTURE && !m_mouseLook && !m_bDragging )
	{
		if ( event->key() == Qt::Key_Escape && !m_selectedFaces.isEmpty() )
		{
			ClearFaceSelection(); event->accept(); return;
		}
		if ( ( event->key() == Qt::Key_Return || event->key() == Qt::Key_Enter ) && !m_selectedFaces.isEmpty() )
		{
			// The signal emitted by a successful edit clears the selection.
			const auto faces = m_selectedFaces;
			m_pDocument->ApplyMaterialToFaces( faces ); event->accept(); return;
		}
	}
	if ( event->key() == Qt::Key_Z && event->modifiers() == Qt::NoModifier && !event->isAutoRepeat() && !m_bDragging )
	{
		SetMouseLook( !m_mouseLook );
		event->accept();
		return;
	}
	if ( event->key() == Qt::Key_Escape && m_mouseLook )
	{
		SetMouseLook( false );
		event->accept();
		return;
	}
	if ( event->key() == Qt::Key_Escape && ( m_moveCandidate || m_moveEditActive ) )
	{
		CancelMoveEdit();
		m_bDragging = false;
		m_pressedButton = Qt::NoButton;
		event->accept();
		return;
	}
	if ( m_moveEditActive ) { event->accept(); return; }
	const Qt::KeyboardModifiers modifiers = event->modifiers();
	const bool shift = modifiers == Qt::ShiftModifier;
	if ( modifiers != Qt::NoModifier && !shift )
	{
		QWidget::keyPressEvent( event );
		return;
	}
	Camera cam;
	BuildCamera( cam );
	const double moveStep = 32.0;
	double translation[3] = {};
	bool rotate = false;
	const int key = event->key();
	if ( !shift && ( key == Qt::Key_Left || key == Qt::Key_Right || key == Qt::Key_Up || key == Qt::Key_Down ) )
	{
		if ( key == Qt::Key_Left ) m_flYaw += 5.0;
		if ( key == Qt::Key_Right ) m_flYaw -= 5.0;
		if ( key == Qt::Key_Up ) m_flPitch = qBound( -89.0, m_flPitch - 5.0, 89.0 );
		if ( key == Qt::Key_Down ) m_flPitch = qBound( -89.0, m_flPitch + 5.0, 89.0 );
		const double yaw = qDegreesToRadians( m_flYaw ), pitch = qDegreesToRadians( m_flPitch );
		const double offset[3] = { std::cos( pitch ) * std::cos( yaw ), std::cos( pitch ) * std::sin( yaw ), std::sin( pitch ) };
		for ( int i = 0; i < 3; ++i ) m_vTarget[i] = cam.eye[i] - offset[i] * m_flDist;
		rotate = true;
	}
	else if ( shift && ( key == Qt::Key_Left || key == Qt::Key_Right ) )
	{
		const double sign = key == Qt::Key_Left ? -1.0 : 1.0;
		for ( int i = 0; i < 3; ++i ) translation[i] = cam.right[i] * moveStep * sign;
	}
	else if ( shift && ( key == Qt::Key_Up || key == Qt::Key_Down ) )
	{
		const double sign = key == Qt::Key_Up ? 1.0 : -1.0;
		for ( int i = 0; i < 3; ++i ) translation[i] = cam.up[i] * moveStep * sign;
	}
	else if ( m_mouseLook && !shift && ( key == Qt::Key_W || key == Qt::Key_S ) )
	{
		const double sign = key == Qt::Key_W ? 1.0 : -1.0;
		for ( int i = 0; i < 3; ++i ) translation[i] = cam.forward[i] * moveStep * sign;
	}
	else if ( m_mouseLook && !shift && ( key == Qt::Key_A || key == Qt::Key_D ) )
	{
		const double sign = key == Qt::Key_A ? -1.0 : 1.0;
		for ( int i = 0; i < 3; ++i ) translation[i] = cam.right[i] * moveStep * sign;
	}
	else if ( !m_mouseLook && !shift && ( key == Qt::Key_D || key == Qt::Key_C ) )
	{
		const double sign = key == Qt::Key_D ? 1.0 : -1.0;
		for ( int i = 0; i < 3; ++i ) translation[i] = cam.forward[i] * moveStep * sign;
	}
	else
	{
		QWidget::keyPressEvent( event );
		return;
	}
	if ( !rotate ) for ( int i = 0; i < 3; ++i ) m_vTarget[i] += translation[i];
	UpdateViews();
	event->accept();
}

//-----------------------------------------------------------------------------
// Purpose: Stores the document and wires the repaint signals.
//-----------------------------------------------------------------------------
void CQtViewport3D::setDocument( CHammerDocument *pDocument )
{
	FinishOverlayFacePick( false );
	FinishOverlayDrag( true );
	FinishDisplacementPaint( true );
	CancelMoveEdit();
	ClearFaceSelection();
	if ( m_pDocument ) disconnect( m_pDocument, nullptr, this, nullptr );
	m_pDocument = pDocument;

	if ( pDocument )
	{
		connect( pDocument, &CHammerDocument::documentChanged, this, &CQtViewport3D::UpdateViews );
		// Geometry, IDs and document identity may all change on edit or undo.
		connect( pDocument, &CHammerDocument::documentChanged, this, &CQtViewport3D::ClearFaceSelection );
		connect( pDocument, &CHammerDocument::documentChanged, this, [this] { FinishOverlayFacePick( false ); } );
		connect( pDocument, &CHammerDocument::selectionChanged, this, &CQtViewport3D::UpdateViews );
		connect( pDocument, &CHammerDocument::selectionChanged, this, [this] {
			if ( m_overlayPickEntity >= 0 && ( m_pDocument->SelectedId() != m_overlayPickEntity ||
				m_pDocument->SelectedIds().size() != 1 ) ) FinishOverlayFacePick( false );
		} );
		connect( pDocument, &CHammerDocument::stateChanged, this, &CQtViewport3D::UpdateViews );
		connect( pDocument, &CHammerDocument::stateChanged, this, &CQtViewport3D::PruneFaceSelection );
		connect( pDocument, &CHammerDocument::stateChanged, this, [this] {
			if ( m_pDocument->GetTool() != TOOL_OVERLAY ) FinishOverlayDrag( true );
			if ( m_overlayPickEntity >= 0 && m_pDocument->GetTool() != m_overlayPickPreviousTool )
				FinishOverlayFacePick( false );
		} );
	}
}

void CQtViewport3D::ClearFaceSelection()
{
	if ( m_displacementPaintApplying ) return;
	if ( m_selectedFaces.isEmpty() ) return;
	m_selectedFaces.clear(); UpdateViews();
}

void CQtViewport3D::PruneFaceSelection()
{
	if ( !m_pDocument || ( m_pDocument->GetTool() != TOOL_TEXTURE && m_overlayPickEntity < 0 ) )
	{
		FinishDisplacementPaint( true );
		ClearFaceSelection();
		return;
	}
	bool changed = false;
	for ( auto it = m_selectedFaces.begin(); it != m_selectedFaces.end(); )
	{
		const SolidLite *solid = m_pDocument->Map().SolidById( it.key() );
		if ( !solid || !HammerBrushEntityView::Visible( m_pDocument->Map(), m_pDocument->GetViewFilter(), *solid ) )
		{ it = m_selectedFaces.erase( it ); changed = true; continue; }
		for ( auto face = it.value().begin(); face != it.value().end(); )
		{
			bool found = false; for ( const SideLite &side : solid->sides ) if ( side.id == *face ) { found = true; break; }
			if ( !found ) { face = it.value().erase( face ); changed = true; } else ++face;
		}
		if ( it.value().isEmpty() ) it = m_selectedFaces.erase( it ); else ++it;
	}
	if ( changed ) UpdateViews();
}

//-----------------------------------------------------------------------------
// Purpose: Builds the orbit camera basis from the yaw/pitch/distance state.
//-----------------------------------------------------------------------------
void CQtViewport3D::centerOn( const double point[3] )
{
	for ( int i = 0; i < 3; ++i ) m_vTarget[i] = point[i];
	UpdateViews();
}

void CQtViewport3D::setCameraPositionAngles( const double eye[3], const double angles[3] )
{
	// Orbit offset points from target to eye, opposite Source's view direction.
	m_flPitch = std::remainder( angles[0], 360.0 );
	m_flYaw = std::remainder( angles[1] + 180.0, 360.0 );
	m_flRoll = std::remainder( angles[2], 360.0 );
	const double yaw = qDegreesToRadians( m_flYaw ), pitch = qDegreesToRadians( m_flPitch );
	const double offset[3] = { std::cos( pitch ) * std::cos( yaw ),
		std::cos( pitch ) * std::sin( yaw ), std::sin( pitch ) };
	for ( int i = 0; i < 3; ++i ) m_vTarget[i] = eye[i] - offset[i] * m_flDist;
	UpdateViews();
}

void CQtViewport3D::cameraBasis( double eye[3], double forward[3], double up[3] ) const
{
	Camera cam;
	BuildCamera( cam );
	for ( int i = 0; i < 3; ++i ) { eye[i] = cam.eye[i]; forward[i] = cam.forward[i]; up[i] = cam.up[i]; }
}

void CQtViewport3D::BuildCamera( Camera &cam ) const
{
	const double yawRad = qDegreesToRadians( m_flYaw );
	const double pitchRad = qDegreesToRadians( m_flPitch );

	// Offset direction from the target to the eye
	const double offset[3] =
	{
		std::cos( pitchRad ) * std::cos( yawRad ),
		std::cos( pitchRad ) * std::sin( yawRad ),
		std::sin( pitchRad ),
	};

	for ( int i = 0; i < 3; ++i )
	{
		cam.eye[i] = m_vTarget[i] + offset[i] * m_flDist;
		cam.forward[i] = -offset[i]; // camera looks at the target
	}
	VecNormalize( cam.forward );

	// Build a stable basis even when looking straight up/down, then apply roll.
	double right[3] = { -std::sin( yawRad ), std::cos( yawRad ), 0.0 };
	double up[3];
	VecCross( right, cam.forward, up );
	const double roll = qDegreesToRadians( m_flRoll );
	for ( int i = 0; i < 3; ++i )
	{
		cam.right[i] = right[i] * std::cos( roll ) - up[i] * std::sin( roll );
		cam.up[i] = up[i] * std::cos( roll ) + right[i] * std::sin( roll );
	}

	cam.focal = ( height() / 2.0 ) / kHalfFovTan;
}

//-----------------------------------------------------------------------------
// Purpose: Projects a world point to screen space; returns false when the
//          point is behind the near plane.
//-----------------------------------------------------------------------------
bool CQtViewport3D::Project( const Camera &cam, const double p[3], QPointF &out ) const
{
	double rel[3];
	VecSub( p, cam.eye, rel );

	const double z = VecDot( rel, cam.forward );
	if ( z < kNearPlane )
		return false;

	const double x = VecDot( rel, cam.right );
	const double y = VecDot( rel, cam.up );

	out.setX( width() / 2.0 + x * cam.focal / z );
	out.setY( height() / 2.0 - y * cam.focal / z );
	return true;
}

//-----------------------------------------------------------------------------
// Purpose: Paints the sky gradient, ground grid and map wireframes.
//-----------------------------------------------------------------------------
void CQtViewport3D::setMaterialGameDirectory( const QString &gameDir )
{
	m_materialRenderer.SetGameDirectory( gameDir );
	UpdateViews();
}

void CQtViewport3D::paintEvent( QPaintEvent *event )
{
	Q_UNUSED( event );
	PaintScene( this, false );
}

void CQtViewport3D::PaintScene( QWidget *target, bool textured )
{
	Camera cam;
	BuildCamera( cam );
	if ( textured && m_pDocument )
	{
		// The textured renderer draws every solid it is given; hand it an
		// empty map when View > Filter hides brushes (view-only, no doc change).
		const ViewFilter &filter = m_pDocument->GetViewFilter();
		MapDocLite visibleMap;
		const MapDocLite *renderMap = &m_pDocument->Map();
		bool hasVisgroupHidden = false;
		if ( !filter.showVisgroupHidden )
		{
			for ( const SolidLite &solid : m_pDocument->Map().solids )
				if ( solid.hiddenWrapper || !ViewFilter::EditorVisible( solid.editorProperties ) ) { hasVisgroupHidden = true; break; }
			if ( !hasVisgroupHidden ) for ( const EntityLite &entity : m_pDocument->Map().entities )
				if ( entity.hiddenWrapper || !ViewFilter::EditorVisible( entity.editorProperties ) ) { hasVisgroupHidden = true; break; }
		}
		if ( !filter.IsDefault() || hasVisgroupHidden )
		{
			visibleMap = m_pDocument->Map();
			visibleMap.solids.erase( std::remove_if( visibleMap.solids.begin(), visibleMap.solids.end(),
				[&filter, this]( const SolidLite &solid ) { return !HammerBrushEntityView::Visible( m_pDocument->Map(), filter, solid ); } ), visibleMap.solids.end() );
			visibleMap.entities.erase( std::remove_if( visibleMap.entities.begin(), visibleMap.entities.end(),
				[&filter]( const EntityLite &entity ) { return !filter.EntityVisible( entity ); } ), visibleMap.entities.end() );
			renderMap = &visibleMap;
		}
		QSet<int> selected = m_pDocument->SelectedIds();
		for ( const SolidLite &solid : renderMap->solids )
			if ( selected.contains( solid.ownerEntityId ) && solid.ownerEntityId > 0 ) selected.insert( solid.id );
		m_materialRenderer.Render( renderMap, selected, cam.eye,
			cam.forward, cam.up, width() * devicePixelRatioF(), height() * devicePixelRatioF(), true, m_selectedFaces );
	}

	// Render the overlay with Qt's raster paint engine. Painting glyphs directly
	// into the OpenGL FBO after our shader can inherit driver state and produce
	// distorted text, especially on high-DPI displays.
	QImage overlay;
	if ( textured )
	{
		const qreal dpr = target->devicePixelRatioF();
		overlay = QImage( target->size() * dpr, QImage::Format_ARGB32_Premultiplied );
		overlay.setDevicePixelRatio( dpr );
		overlay.fill( Qt::transparent );
	}
	QPainter painter( textured ? static_cast<QPaintDevice *>( &overlay ) : static_cast<QPaintDevice *>( target ) );

	if ( !textured )
	{
		QLinearGradient gradient( 0, 0, 0, height() );
		gradient.setColorAt( 0.0, HammerTheme::Colors().view3dSky );
		gradient.setColorAt( 0.7, HammerTheme::Colors().view3dHorizon );
		gradient.setColorAt( 1.0, QColor( 24, 26, 32 ) );
		painter.fillRect( rect(), gradient );
	}

	// The software overlay has no access to the GL depth buffer; keep grid and
	// axes out of textured mode so they cannot show through opaque brushes.
	if ( !textured && m_groundGridVisible ) DrawGridFloor( painter, cam );
	if ( !textured ) DrawSolids( painter, cam );
	DrawEntities( painter, cam );
	DrawOverlayHandles( painter, cam );
	DrawDisplacementBrush( painter, cam );
	if ( m_pDocument )
	{
		const auto &points = m_pDocument->PointFilePoints();
		painter.setPen( QPen( HammerTheme::Colors().pointFile, 2 ) );
		for ( int i = 1; i < points.size(); ++i )
		{
			double a[3] = { points[i-1].x, points[i-1].y, points[i-1].z };
			double b[3] = { points[i].x, points[i].y, points[i].z };
			double ar[3], br[3];
			VecSub( a, cam.eye, ar ); VecSub( b, cam.eye, br );
			const double za = VecDot( ar, cam.forward ), zb = VecDot( br, cam.forward );
			if ( za < kNearPlane && zb < kNearPlane ) continue;
			if ( za < kNearPlane || zb < kNearPlane )
			{
				const double t = ( kNearPlane - za ) / ( zb - za );
				double *clipped = za < kNearPlane ? a : b;
				for ( int axis = 0; axis < 3; ++axis )
					clipped[axis] = a[axis] + ( b[axis] - a[axis] ) * t;
			}
			QPointF pa, pb;
			if ( Project( cam, a, pa ) && Project( cam, b, pb ) ) painter.drawLine( pa, pb );
		}
	}

	painter.setRenderHint( QPainter::Antialiasing, true );
	HammerTheme::DrawViewportLabel( painter, rect(),
		m_displayMode == Flat ? tr( "3D Flat" ) : m_displayMode == Wireframe ? tr( "3D Wireframe" ) : tr( "3D Textured" ), hasFocus() );

	HammerTheme::DrawViewportHint( painter, rect(), interactionHint() );
	const QString help = interactionHelp();
	if ( toolTip() != help ) setToolTip( help );
	if ( m_mouseLook )
	{
		painter.setPen( QPen( Qt::white, 1 ) );
		const QPoint center = rect().center();
		painter.drawLine( center + QPoint( -6, 0 ), center + QPoint( 6, 0 ) );
		painter.drawLine( center + QPoint( 0, -6 ), center + QPoint( 0, 6 ) );
	}
	HammerTheme::DrawViewportFrame( painter, rect(), hasFocus() );
	painter.end();
	if ( textured )
	{
		QPainter compositor( target );
		compositor.drawImage( QPointF( 0, 0 ), overlay );
	}

}

QString CQtViewport3D::interactionHint() const
{
	if ( m_overlayPickEntity >= 0 ) return tr( "Pick Overlay Faces: click / Ctrl-click | Enter apply | Esc cancel" );
	if ( m_mouseLook ) return tr( "Z look | WASD | Esc exit" );
	if ( DisplacementPaintActive() ) return tr( "Paint Geometry: drag | Esc cancels stroke" );
	switch ( m_pDocument ? m_pDocument->GetTool() : TOOL_SELECT )
	{
		case TOOL_ENTITY: return tr( "2 Entity: place in 2D" );
		case TOOL_BLOCK: return tr( "3 Block: draw in 2D" );
		case TOOL_TEXTURE: return tr( "Face: click / Enter apply" );
		case TOOL_DECAL: return tr( "Decal: click a brush face" );
		case TOOL_OVERLAY: return tr( "Overlay: click a face / drag a corner" );
		case TOOL_CLIP: return tr( "Shift+X: clip in 2D" );
		default: return tr( "1 Select: click / drag" );
	}
}

QString CQtViewport3D::interactionHelp() const
{
	if ( m_overlayPickEntity >= 0 ) return tr( "Pick Overlay Faces: click / Ctrl-click | Enter apply | Esc cancel" );
	QString toolHint;
	if ( m_mouseLook )
		toolHint = tr( "Mouse look: mouse aims | WASD moves | Z or Esc exits" );
	else if ( m_pDocument )
	{
		switch ( m_pDocument->GetTool() )
		{
			case TOOL_ENTITY:
				toolHint = tr( "Entity (2): place in a 2D view | drag or right-drag orbits | wheel zooms | Z look" );
				break;
			case TOOL_BLOCK:
				toolHint = tr( "Block (3): draw in 2D | drag or right-drag orbit | wheel zoom | Z look" );
				break;
			case TOOL_TEXTURE:
				toolHint = tr( "Material: left-click selects face; Ctrl-click toggles | Enter applies selected | Esc clears | right-click paints face | Shift-right-click paints brush | drag orbits" );
				break;
			case TOOL_CLIP:
				toolHint = tr( "Clip (Shift+X): define the clipping line in a 2D view | drag orbits | wheel zooms" );
				break;
			case TOOL_DECAL:
				toolHint = tr( "Decal (Shift+D): click a brush face to place the current material" );
				break;
			case TOOL_OVERLAY:
				toolHint = tr( "Overlay (Shift+O): click a brush face to place | select an overlay and drag its corners" );
				break;
			case TOOL_SELECT:
			default:
				toolHint = tr( "Select (1): click select | drag selected move | empty/right-drag orbit | wheel zoom | Z look" );
				break;
		}
	}
	else
		toolHint = tr( "Select (1): click select | drag selected move | empty/right-drag orbit | wheel zoom | Z look" );
	return toolHint;
}

//-----------------------------------------------------------------------------
// Purpose: Ground grid on the z=0 plane plus colored world axes.
//-----------------------------------------------------------------------------
void CQtViewport3D::DrawGridFloor( QPainter &painter, const Camera &cam )
{
	const int kExtent = 2048;
	const int kStep = 128;

	painter.setPen( HammerTheme::Colors().view3dGround );
	for ( int i = -kExtent; i <= kExtent; i += kStep )
	{
		QPointF a;
		QPointF b;
		const double lineA[3] = { double( i ), double( -kExtent ), 0.0 };
		const double lineB[3] = { double( i ), double( kExtent ), 0.0 };
		const double lineC[3] = { double( -kExtent ), double( i ), 0.0 };
		const double lineD[3] = { double( kExtent ), double( i ), 0.0 };
		if ( Project( cam, lineA, a ) && Project( cam, lineB, b ) )
			painter.drawLine( a, b );
		if ( Project( cam, lineC, a ) && Project( cam, lineD, b ) )
			painter.drawLine( a, b );
	}

	// World axes through the origin: X red, Y green, Z blue
	QPointF o;
	QPointF p;
	const double origin[3] = { 0.0, 0.0, 0.0 };

	const double xAxis[3] = { 256.0, 0.0, 0.0 };
	const double yAxis[3] = { 0.0, 256.0, 0.0 };
	const double zAxis[3] = { 0.0, 0.0, 256.0 };
	if ( Project( cam, origin, o ) )
	{
		painter.setPen( HammerTheme::Colors().view3dAxisX );
		if ( Project( cam, xAxis, p ) )
			painter.drawLine( o, p );
		painter.setPen( HammerTheme::Colors().view3dAxisY );
		if ( Project( cam, yAxis, p ) )
			painter.drawLine( o, p );
		painter.setPen( HammerTheme::Colors().view3dAxisZ );
		if ( Project( cam, zAxis, p ) )
			painter.drawLine( o, p );
	}
}

//-----------------------------------------------------------------------------
// Purpose: Draws one axis-aligned box wireframe (12 edges).
//-----------------------------------------------------------------------------
void CQtViewport3D::DrawBoxWireframe( QPainter &painter, const Camera &cam,
	const double mins[3], const double maxs[3] )
{
	double corners[8][3];
	int idx = 0;
	for ( int z = 0; z < 2; ++z )
	{
		for ( int y = 0; y < 2; ++y )
		{
			for ( int x = 0; x < 2; ++x, ++idx )
			{
				corners[idx][0] = x ? maxs[0] : mins[0];
				corners[idx][1] = y ? maxs[1] : mins[1];
				corners[idx][2] = z ? maxs[2] : mins[2];
			}
		}
	}

	QPointF projected[8];
	bool visible[8];
	for ( int i = 0; i < 8; ++i )
		visible[i] = Project( cam, corners[i], projected[i] );

	for ( int e = 0; e < 12; ++e )
	{
		if ( !visible[kBoxEdges[e][0]] || !visible[kBoxEdges[e][1]] )
			continue;
		painter.drawLine( projected[kBoxEdges[e][0]], projected[kBoxEdges[e][1]] );
	}
}

void CQtViewport3D::DrawSolids( QPainter &painter, const Camera &cam )
{
	if ( !m_pDocument )
		return;

	const MapDocLite &map = m_pDocument->Map();
	struct Face { QPolygonF polygon; double depth; QColor color; bool selected; bool faceSelected; };
	QList<Face> faces;
	QList<const SolidLite *> fallback;
	const ViewFilter &filter = m_pDocument->GetViewFilter();
	for ( const SolidLite &solid : map.solids )
	{
		if ( !HammerBrushEntityView::Visible( map, filter, solid ) ) continue;
		QList<HammerWinding::Polygon> windings;
		if ( !HammerWinding::BuildFaces( solid, windings ) ) { fallback.append( &solid ); continue; }
		for ( int face = 0; face < windings.size(); ++face )
		HammerDisplacementMesh::ForEachPolygon( solid.sides[face], windings[face], [&]( const HammerWinding::Polygon &poly ) {
			using Vec = HammerWinding::Point;
			QList<Vec> clipped;
			for ( int i = 0; i < poly.size(); ++i )
			{
				const Vec &a = poly[i], &b = poly[( i + 1 ) % poly.size()];
				double ar[3], br[3]; VecSub( a.data(), cam.eye, ar ); VecSub( b.data(), cam.eye, br );
				const double za = VecDot( ar, cam.forward ), zb = VecDot( br, cam.forward );
				if ( za >= kNearPlane ) clipped.append( a );
				if ( ( za >= kNearPlane ) != ( zb >= kNearPlane ) )
				{
					const double t = ( kNearPlane + 0.00001 - za ) / ( zb - za );
					Vec point; for ( int axis = 0; axis < 3; ++axis ) point[axis] = a[axis] + ( b[axis] - a[axis] ) * t;
					clipped.append( point );
				}
			}
			if ( clipped.size() < 3 ) return;
			Face result; result.depth = 0; result.selected = m_pDocument->IsSelected( HammerBrushEntityView::SelectionId( solid ) );
			result.faceSelected = face < solid.sides.size() && m_selectedFaces.value( solid.id ).contains( solid.sides[face].id );
			double a[3], b[3], normal[3];
			VecSub( poly[1].data(), poly[0].data(), a ); VecSub( poly[2].data(), poly[0].data(), b );
			VecCross( b, a, normal ); VecNormalize( normal );
			const int shade = qBound( 70, int( 110 + 60*std::abs(normal[2]) + 25*normal[0] ), 210 );
			result.color = QColor( shade, shade, shade + 5 );
			for ( const Vec &point : clipped )
			{
				QPointF screen; if ( !Project( cam, point.data(), screen ) ) continue;
				result.polygon << screen;
				double rel[3]; VecSub( point.data(), cam.eye, rel ); result.depth += VecDot( rel, cam.forward );
			}
			if ( result.polygon.size() >= 3 ) { result.depth /= result.polygon.size(); faces.append( result ); }
		} );
	}
	std::sort( faces.begin(), faces.end(), []( const Face &a, const Face &b ) { return a.depth > b.depth; } );
	for ( const Face &face : faces )
	{
		painter.setPen( face.faceSelected ? QPen( QColor( 255, 95, 65 ), 2 ) : face.selected ? QPen( HammerTheme::Colors().selection, 2 ) : QPen( QColor( 55, 55, 60 ), 1 ) );
		painter.setBrush( m_displayMode == Flat ? QBrush( face.faceSelected ? QColor( 180, 65, 55 ) : face.color ) : QBrush( Qt::NoBrush ) ); painter.drawPolygon( face.polygon );
	}
	for ( const SolidLite *ptr : fallback )
	{
		const SolidLite &solid = *ptr;
		const bool bSelected = m_pDocument->IsSelected( HammerBrushEntityView::SelectionId( solid ) );
		painter.setPen( bSelected ? QPen( HammerTheme::Colors().selection, 2 ) : QPen( HammerTheme::Colors().view3dEdge, 1 ) );
		painter.setBrush( Qt::NoBrush );
		DrawBoxWireframe( painter, cam, solid.mins, solid.maxs );
	}
}

int CQtViewport3D::HitTest( const QPoint &position, double *depth ) const
{
	if ( !m_pDocument || height() <= 0 ) return -1;
	Camera cam;
	BuildCamera( cam );
	const double x = ( position.x() - width() / 2.0 ) / cam.focal;
	const double y = ( height() / 2.0 - position.y() ) / cam.focal;
	double ray[3];
	for ( int i = 0; i < 3; ++i ) ray[i] = cam.forward[i] + x * cam.right[i] + y * cam.up[i];
	double closest = std::numeric_limits<double>::infinity();
	int hit = -1;
	const ViewFilter &filter = m_pDocument->GetViewFilter();
	for ( const SolidLite &solid : m_pDocument->Map().solids )
	{
		if ( !HammerBrushEntityView::Visible( m_pDocument->Map(), filter, solid ) ) continue;
		QList<HammerWinding::Polygon> windings;
		if ( !HammerWinding::BuildFaces( solid, windings ) )
		{
			double distance;
			if ( RayBox( cam.eye, ray, solid.mins, solid.maxs, distance ) && distance < closest )
				{ closest = distance; hit = -1; }
			continue;
		}
		for ( int face = 0; face < windings.size(); ++face )
		HammerDisplacementMesh::ForEachPolygon( solid.sides[face], windings[face], [&]( const HammerWinding::Polygon &poly ) {
			for ( int i = 1; i + 1 < poly.size(); ++i )
			{
				double edge1[3], edge2[3], h[3], s[3], q[3];
				VecSub( poly[i].data(), poly[0].data(), edge1 );
				VecSub( poly[i+1].data(), poly[0].data(), edge2 );
				VecCross( ray, edge2, h ); const double det = VecDot( edge1, h );
				if ( std::abs( det ) < 1e-10 ) continue;
				const double inv = 1.0/det; VecSub( cam.eye, poly[0].data(), s );
				const double u = inv*VecDot(s,h); if (u < -1e-8 || u > 1.00000001) continue;
				VecCross(s,edge1,q); const double v=inv*VecDot(ray,q);
				if (v < -1e-8 || u+v > 1.00000001) continue;
				const double distance=inv*VecDot(edge2,q);
				if (distance >= kNearPlane && distance < closest) { closest=distance; hit=HammerBrushEntityView::SelectionId( solid ); }
			}
		} );
	}
	for ( const EntityLite &entity : m_pDocument->Map().entities )
	{
		if ( !filter.EntityVisible( entity ) ) continue;
		if ( HammerBrushEntityView::HasBrushes( m_pDocument->Map(), entity.id ) ) continue;
		double mins[3], maxs[3], distance;
		m_pDocument->Fgd().ClassBounds( entity.classname, mins, maxs );
		for ( int i = 0; i < 3; ++i ) { mins[i] += entity.origin[i]; maxs[i] += entity.origin[i]; }
		if ( RayBox( cam.eye, ray, mins, maxs, distance ) && distance < closest )
			{ closest = distance; hit = entity.id; }
	}
	if ( hit >= 0 && depth ) *depth = closest;
	return hit;
}

void CQtViewport3D::DrawEntities( QPainter &painter, const Camera &cam )
{
	if ( !m_pDocument )
		return;

	const MapDocLite &map = m_pDocument->Map();

	QFont font = painter.font();
	font.setPointSizeF( 9.0 );
	painter.setFont( font );

	const ViewFilter &filter = m_pDocument->GetViewFilter();
	for ( const EntityLite &entity : map.entities )
	{
		if ( !filter.EntityVisible( entity ) ) continue;
		if ( HammerBrushEntityView::HasBrushes( map, entity.id ) ) continue;
		double mins[3];
		double maxs[3];
		m_pDocument->Fgd().ClassBounds( entity.classname, mins, maxs );

		double boxMins[3];
		double boxMaxs[3];
		for ( int i = 0; i < 3; ++i )
		{
			boxMins[i] = entity.origin[i] + mins[i];
			boxMaxs[i] = entity.origin[i] + maxs[i];
		}

		const bool bSelected = m_pDocument->IsSelected( entity.id );
		painter.setPen( bSelected ? QPen( HammerTheme::Colors().selection, 2 ) : QPen( HammerTheme::Colors().view3dEntity, 1 ) );
		painter.setBrush( Qt::NoBrush );
		if ( entity.classname.compare( QLatin1String( "infodecal" ), Qt::CaseInsensitive ) == 0 )
		{
			QPointF position;
			if ( Project( cam, entity.origin, position ) )
			{
				QPolygonF pin;
				pin << position + QPointF( 0, -5 ) << position + QPointF( 5, 0 )
					<< position + QPointF( 0, 5 ) << position + QPointF( -5, 0 );
				painter.drawPolygon( pin );
				if ( filter.showEntityNames )
					painter.drawText( position + QPointF( 8, -5 ), entity.keyvalues.value( QStringLiteral( "texture" ), entity.classname ) );
			}
			continue;
		}
		DrawBoxWireframe( painter, cam, boxMins, boxMaxs );

		QPointF org;
		if ( filter.showEntityNames && Project( cam, entity.origin, org ) )
		{
			painter.setPen( bSelected ? HammerTheme::Colors().selection : HammerTheme::Colors().view3dEntityText );
			painter.drawText( org + QPointF( 6, -6 ), entity.classname );
		}
	}
}

//-----------------------------------------------------------------------------
// Purpose: Mouse drag orbits the camera; wheel zooms toward/away the target.
//-----------------------------------------------------------------------------
void CQtViewport3D::CancelMoveEdit()
{
	if ( m_moveEditActive && m_pDocument ) m_pDocument->CancelEdit();
	m_moveEditActive = false;
	m_moveCandidate = false;
}

void CQtViewport3D::setDisplacementPaint( bool enabled, int effect, double radius, double amount, bool soft )
{
	FinishDisplacementPaint( true );
	m_displacementPaintEnabled = enabled;
	m_displacementPaintEffect = std::clamp( effect, 0, 2 );
	m_displacementPaintRadius = std::isfinite( radius ) ? std::clamp( radius, 1.0, 4096.0 ) : 64.0;
	m_displacementPaintAmount = std::isfinite( amount ) ? std::clamp( amount, -32768.0, 32768.0 ) : 8.0;
	m_displacementPaintSoft = soft;
	if ( enabled ) SetMouseLook( false );
	UpdateViews();
}

bool CQtViewport3D::DisplacementPaintActive() const
{
	return m_displacementPaintEnabled && m_pDocument && m_pDocument->GetTool() == TOOL_TEXTURE;
}

void CQtViewport3D::FinishDisplacementPaint( bool cancel )
{
	m_displacementBrushCenter.reset();
	if ( m_displacementPaintButton == Qt::NoButton ) return;
	m_displacementPaintButton = Qt::NoButton;
	if ( m_pDocument )
	{
		m_displacementPaintApplying = true;
		if ( cancel ) m_pDocument->CancelEdit();
		else m_pDocument->EndEdit();
		m_displacementPaintApplying = false;
	}
	UpdateViews();
}

void CQtViewport3D::UpdateDisplacementPaint( const QPoint &position, bool dab )
{
	m_displacementBrushCenter.reset();
	UpdateViews();
	if ( !DisplacementPaintActive() || m_selectedFaces.isEmpty() || height() <= 0 ) return;
	Camera cam;
	BuildCamera( cam );
	const double x = ( position.x() - width() / 2.0 ) / cam.focal;
	const double y = ( height() / 2.0 - position.y() ) / cam.focal;
	double ray[3];
	for ( int i = 0; i < 3; ++i ) ray[i] = cam.forward[i] + x * cam.right[i] + y * cam.up[i];
	const auto hit = HammerFaceHit::Find( *m_pDocument, cam.eye, ray, kNearPlane );
	const MapDocLite &map = m_pDocument->Map();
	const SolidLite *hitSolid = map.SolidById( hit.solidId );
	HammerDisplacementMesh::Mesh hitMesh;
	QList<HammerWinding::Polygon> hitFaces;
	if ( !hitSolid || hit.faceIndex < 0 || hit.faceIndex >= hitSolid->sides.size() ||
		!m_selectedFaces.value( hit.solidId ).contains( hitSolid->sides[hit.faceIndex].id ) ||
		!HammerWinding::BuildFaces( *hitSolid, hitFaces ) ||
		!HammerDisplacementMesh::Build( hitSolid->sides[hit.faceIndex], hitFaces[hit.faceIndex], hitMesh ) ) return;

	HammerWinding::Point intersection;
	for ( int i = 0; i < 3; ++i ) intersection[i] = cam.eye[i] + ray[i] * hit.distance;
	const auto nearest = std::min_element( hitMesh.vertices.cbegin(), hitMesh.vertices.cend(),
		[&intersection]( const auto &a, const auto &b ) {
			return HammerDisplacementPaint::DistanceSquared( a, intersection ) <
				HammerDisplacementPaint::DistanceSquared( b, intersection );
		} );
	const auto center = *nearest;
	m_displacementBrushCenter = center;
	if ( !dab ) return;

	struct Edit { int solidId; int faceId; QList<double> distances; };
	QList<Edit> edits;
	const auto effect = static_cast<HammerDisplacementPaint::Effect>( m_displacementPaintEffect );
	const double amount = m_displacementPaintButton == Qt::RightButton ? -m_displacementPaintAmount : m_displacementPaintAmount;
	for ( auto selection = m_selectedFaces.cbegin(); selection != m_selectedFaces.cend(); ++selection )
	{
		const SolidLite *solid = map.SolidById( selection.key() );
		QList<HammerWinding::Polygon> faces;
		if ( !solid || !HammerBrushEntityView::Visible( map, m_pDocument->GetViewFilter(), *solid ) ||
			!HammerWinding::BuildFaces( *solid, faces ) ) continue;
		for ( int i = 0; i < solid->sides.size(); ++i )
		{
			const SideLite &side = solid->sides[i];
			if ( !side.displacement || !selection.value().contains( side.id ) ) continue;
			HammerDisplacementMesh::Mesh mesh;
			QList<double> distances;
			if ( !HammerDisplacementMesh::Build( side, faces[i], mesh ) ||
				!HammerDisplacementEdit::ReadDistances( *side.displacement, distances ) ) continue;
			const auto updated = HammerDisplacementPaint::Dab( mesh, distances, center, effect,
				m_displacementPaintRadius, amount, m_displacementPaintSoft );
			if ( updated != distances ) edits.append( { solid->id, side.id, updated } );
		}
	}
	m_displacementPaintApplying = true;
	bool applied = true;
	for ( const Edit &edit : edits )
		if ( !m_pDocument->SetDisplacementDistances( edit.solidId, edit.faceId, edit.distances ) )
		{
			applied = false;
			break;
		}
	m_displacementPaintApplying = false;
	if ( !applied ) FinishDisplacementPaint( true );
}

void CQtViewport3D::DrawDisplacementBrush( QPainter &painter, const Camera &cam )
{
	if ( !DisplacementPaintActive() || !m_displacementBrushCenter ) return;
	QPointF center;
	if ( !Project( cam, m_displacementBrushCenter->data(), center ) ) return;
	QPointF edge;
	double end[3];
	for ( int i = 0; i < 3; ++i ) end[i] = ( *m_displacementBrushCenter )[i] + cam.right[i] * m_displacementPaintRadius;
	if ( !Project( cam, end, edge ) ) return;
	const double radius = std::abs( edge.x() - center.x() );
	painter.save();
	painter.setPen( QPen( QColor( 255, 210, 100 ), 1.5 ) );
	painter.setBrush( Qt::NoBrush );
	painter.drawEllipse( center, radius, radius );
	painter.drawEllipse( center, 2, 2 );
	painter.restore();
}

void CQtViewport3D::SetMouseLook( bool enabled )
{
	if ( m_mouseLook == enabled ) return;
	m_mouseLook = enabled;
	if ( enabled )
	{
		setCursor( Qt::BlankCursor );
		QCursor::setPos( mapToGlobal( rect().center() ) );
	}
	else unsetCursor();
	UpdateViews();
}

bool CQtViewport3D::BeginOverlayDrag( const Camera &cam, const QPointF &position )
{
	const auto &map = static_cast<const CHammerDocument &>( *m_pDocument ).Map();
	int entityId = -1, corner = -1;
	double bestDistance = 64.0;
	for ( const EntityLite &entity : map.entities )
	{
		HammerOverlay::Data overlay;
		if ( !m_pDocument->IsSelected( entity.id ) || !m_pDocument->GetViewFilter().EntityVisible( entity ) ||
			!HammerOverlay::Parse( entity, overlay ) ) continue;
		for ( int i = 0; i < 4; ++i )
		{
			QPointF projected;
			const auto point = HammerOverlay::WorldPoint( overlay, i );
			if ( !Project( cam, point.data(), projected ) ) continue;
			const QPointF delta = position - projected;
			const double distance = QPointF::dotProduct( delta, delta );
			if ( distance >= bestDistance ) continue;
			bestDistance = distance;
			entityId = entity.id;
			corner = i;
		}
	}
	if ( entityId < 0 ) return false;
	m_overlayDragEntity = entityId;
	m_overlayDragCorner = corner;
	m_pDocument->BeginEdit();
	return true;
}

bool CQtViewport3D::SelectOverlayAt( const Camera &cam, const QPointF &position, int hitSideId )
{
	if ( hitSideId <= 0 ) return false;
	const auto &map = static_cast<const CHammerDocument &>( *m_pDocument ).Map();
	int hit = -1, renderOrder = -1;
	for ( const EntityLite &entity : map.entities )
	{
		HammerOverlay::Data overlay;
		if ( !m_pDocument->GetViewFilter().EntityVisible( entity ) || !HammerOverlay::Parse( entity, overlay ) ||
			!overlay.sides.contains( hitSideId ) || !HammerOverlay::Convex( overlay ) ||
			overlay.renderOrder < renderOrder ) continue;
		QPolygonF quad;
		for ( int i = 0; i < 4; ++i )
		{
			const auto point = HammerOverlay::WorldPoint( overlay, i );
			QPointF screen;
			if ( !Project( cam, point.data(), screen ) ) break;
			quad.append( screen );
		}
		if ( quad.size() != 4 || !quad.containsPoint( position, Qt::OddEvenFill ) ) continue;
		renderOrder = overlay.renderOrder;
		hit = entity.id;
	}
	if ( hit < 0 ) return false;
	m_pDocument->SelectObject( hit );
	return true;
}

void CQtViewport3D::DrawOverlayHandles( QPainter &painter, const Camera &cam )
{
	if ( !m_pDocument || m_pDocument->GetTool() != TOOL_OVERLAY ) return;
	const auto &map = static_cast<const CHammerDocument &>( *m_pDocument ).Map();
	for ( const EntityLite &entity : map.entities )
	{
		HammerOverlay::Data overlay;
		if ( !m_pDocument->IsSelected( entity.id ) || !m_pDocument->GetViewFilter().EntityVisible( entity ) ||
			!HammerOverlay::Parse( entity, overlay ) ) continue;
		QPolygonF quad;
		for ( int i = 0; i < 4; ++i )
		{
			const auto point = HammerOverlay::WorldPoint( overlay, i );
			QPointF screen;
			if ( !Project( cam, point.data(), screen ) ) break;
			quad.append( screen );
		}
		if ( quad.size() != 4 ) continue;
		painter.save();
		painter.setPen( QPen( HammerTheme::Colors().selection, 1 ) );
		painter.setBrush( Qt::NoBrush );
		painter.drawPolygon( quad );
		for ( int i = 0; i < quad.size(); ++i )
		{
			const auto &p = quad[i];
			const bool active = m_overlayDragEntity == entity.id && m_overlayDragCorner == i;
			painter.fillRect( QRectF( p.x() - 3, p.y() - 3, 6, 6 ), active ? HammerTheme::Colors().selection : QColor( Qt::white ) );
		}
		painter.restore();
	}
}

void CQtViewport3D::UpdateOverlayDrag( const QPointF &position, bool snap )
{
	if ( m_pDocument->GetTool() != TOOL_OVERLAY ) { FinishOverlayDrag( true ); return; }
	const auto &map = static_cast<const CHammerDocument &>( *m_pDocument ).Map();
	const EntityLite *entity = nullptr;
	for ( const EntityLite &candidate : map.entities )
		if ( candidate.id == m_overlayDragEntity ) { entity = &candidate; break; }
	HammerOverlay::Data overlay;
	if ( !entity || !HammerOverlay::Parse( *entity, overlay ) ) { FinishOverlayDrag( true ); return; }
	Camera cam;
	BuildCamera( cam );
	const double x = ( position.x() - width() / 2.0 ) / cam.focal;
	const double y = ( height() / 2.0 - position.y() ) / cam.focal;
	double ray[3];
	for ( int axis = 0; axis < 3; ++axis ) ray[axis] = cam.forward[axis] + x * cam.right[axis] + y * cam.up[axis];
	const auto face = HammerFaceHit::Find( *m_pDocument, cam.eye, ray, kNearPlane );
	const SolidLite *solid = map.SolidById( face.solidId );
	if ( !solid || face.faceIndex < 0 || face.faceIndex >= solid->sides.size() ||
		!overlay.sides.contains( solid->sides[face.faceIndex].id ) ) return;
	double origin[3];
	for ( int axis = 0; axis < 3; ++axis ) origin[axis] = cam.eye[axis] + ray[axis] * face.distance;
	if ( snap )
	{
		double closest = 64.0;
		for ( const EntityLite &other : map.entities )
		{
			HammerOverlay::Data candidate;
			if ( other.id == entity->id || !m_pDocument->GetViewFilter().EntityVisible( other ) ||
				!HammerOverlay::Parse( other, candidate ) || !candidate.sides.contains( solid->sides[face.faceIndex].id ) ) continue;
			for ( int i = 0; i < 4; ++i )
			{
				const auto point = HammerOverlay::WorldPoint( candidate, i );
				QPointF projected;
				if ( !Project( cam, point.data(), projected ) ) continue;
				const QPointF delta = position - projected;
				const double distance = QPointF::dotProduct( delta, delta );
				if ( distance >= closest ) continue;
				closest = distance;
				std::copy( point.begin(), point.end(), origin );
			}
		}
	}
	m_pDocument->MoveOverlayHandle( m_overlayDragEntity, m_overlayDragCorner, origin );
	UpdateViews();
}

void CQtViewport3D::FinishOverlayDrag( bool cancel )
{
	if ( m_overlayDragEntity < 0 ) return;
	m_overlayDragEntity = -1;
	m_overlayDragCorner = -1;
	if ( m_pDocument )
	{
		if ( cancel ) m_pDocument->CancelEdit();
		else m_pDocument->EndEdit();
	}
	UpdateViews();
}

bool CQtViewport3D::beginOverlayFacePick( int entityId )
{
	if ( !m_pDocument || m_pDocument->SelectedId() != entityId || m_pDocument->SelectedIds().size() != 1 ) return false;
	const EntityLite *entity = m_pDocument->Map().EntityById( entityId );
	HammerOverlay::Data overlay;
	if ( !entity || !HammerOverlay::Parse( *entity, overlay ) ) return false;
	FinishOverlayFacePick( false );
	FinishOverlayDrag( true );
	FinishDisplacementPaint( true );
	SetMouseLook( false );
	m_overlayPickEntity = entityId;
	m_overlayPickPreviousTool = m_pDocument->GetTool();
	m_selectedFaces.clear();
	for ( const SolidLite &solid : m_pDocument->Map().solids )
		if ( HammerBrushEntityView::Visible( m_pDocument->Map(), m_pDocument->GetViewFilter(), solid ) )
			for ( const SideLite &side : solid.sides )
				if ( overlay.sides.contains( side.id ) ) m_selectedFaces[solid.id].insert( side.id );
	setFocus();
	UpdateViews();
	return true;
}

void CQtViewport3D::FinishOverlayFacePick( bool apply )
{
	if ( m_overlayPickEntity < 0 ) return;
	const int entityId = m_overlayPickEntity;
	m_overlayPickEntity = -1;
	m_overlayPickPreviousTool = -1;
	QStringList faces;
	for ( const QSet<int> &set : m_selectedFaces )
	{
		QList<int> ids = set.values();
		std::sort( ids.begin(), ids.end() );
		for ( int id : ids ) faces.append( QString::number( id ) );
	}
	ClearFaceSelection();
	if ( apply && m_pDocument && m_pDocument->SelectedId() == entityId &&
		 m_pDocument->SelectedIds().size() == 1 && !faces.isEmpty() )
		m_pDocument->SetSelectedEntityKey( QStringLiteral( "sides" ), faces.join( QLatin1Char( ' ' ) ) );
	UpdateViews();
}

bool CQtViewport3D::event( QEvent *event )
{
	if ( event->type() == QEvent::ShortcutOverride &&
		( m_displacementPaintButton != Qt::NoButton || m_overlayDragEntity >= 0 ||
		  ( m_overlayPickEntity >= 0 && ( static_cast<QKeyEvent *>( event )->key() == Qt::Key_Return ||
			static_cast<QKeyEvent *>( event )->key() == Qt::Key_Enter || static_cast<QKeyEvent *>( event )->key() == Qt::Key_Escape ) ) ) )
	{
		event->accept();
		return true;
	}
	if ( event->type() == QEvent::Leave )
	{
		m_displacementBrushCenter.reset();
		UpdateViews();
	}
	if ( event->type() == QEvent::FocusIn || event->type() == QEvent::FocusOut )
		UpdateViews();
	if ( event->type() == QEvent::UngrabMouse || event->type() == QEvent::FocusOut ||
		 event->type() == QEvent::WindowDeactivate || event->type() == QEvent::Hide )
	{
		FinishOverlayFacePick( false );
		FinishOverlayDrag( true );
		FinishDisplacementPaint( true );
		SetMouseLook( false );
		CancelMoveEdit();
		m_bDragging = false;
		m_pressedButton = Qt::NoButton;
		m_mouseMoved = false;
		m_bRightDrag = false;
	}
	return QWidget::event( event );
}

void CQtViewport3D::mousePressEvent( QMouseEvent *event )
{
	if ( m_mouseLook ) { event->accept(); return; }
	if ( m_overlayDragEntity >= 0 ) { event->accept(); return; }
	if ( m_overlayPickEntity >= 0 && event->button() == Qt::LeftButton )
	{
		Camera cam;
		BuildCamera( cam );
		const double x = ( event->position().x() - width() / 2.0 ) / cam.focal;
		const double y = ( height() / 2.0 - event->position().y() ) / cam.focal;
		double ray[3];
		for ( int axis = 0; axis < 3; ++axis ) ray[axis] = cam.forward[axis] + x * cam.right[axis] + y * cam.up[axis];
		const auto hit = HammerFaceHit::Find( *m_pDocument, cam.eye, ray, kNearPlane );
		const SolidLite *solid = m_pDocument->Map().SolidById( hit.solidId );
		const bool toggle = event->modifiers().testFlag( Qt::ControlModifier );
		if ( !toggle ) m_selectedFaces.clear();
		if ( solid && hit.faceIndex >= 0 && hit.faceIndex < solid->sides.size() )
		{
			const int faceId = solid->sides[hit.faceIndex].id;
			QSet<int> &faces = m_selectedFaces[solid->id];
			if ( !toggle || !faces.remove( faceId ) ) faces.insert( faceId );
			if ( faces.isEmpty() ) m_selectedFaces.remove( solid->id );
		}
		UpdateViews();
		event->accept();
		return;
	}
	if ( event->button() == Qt::LeftButton && m_pDocument && m_pDocument->GetTool() == TOOL_OVERLAY )
	{
		Camera cam;
		BuildCamera( cam );
		setFocus();
		if ( !BeginOverlayDrag( cam, event->position() ) )
		{
			const double x = ( event->position().x() - width() / 2.0 ) / cam.focal;
			const double y = ( height() / 2.0 - event->position().y() ) / cam.focal;
			double ray[3];
			for ( int axis = 0; axis < 3; ++axis ) ray[axis] = cam.forward[axis] + x * cam.right[axis] + y * cam.up[axis];
			const auto face = HammerFaceHit::Find( *m_pDocument, cam.eye, ray, kNearPlane );
			if ( face.solidId >= 0 )
			{
				const auto &map = static_cast<const CHammerDocument &>( *m_pDocument ).Map();
				const SolidLite *solid = map.SolidById( face.solidId );
				if ( !solid || face.faceIndex < 0 || face.faceIndex >= solid->sides.size() )
				{
					event->accept();
					return;
				}
				const int sideId = solid->sides[face.faceIndex].id;
				if ( SelectOverlayAt( cam, event->position(), sideId ) )
				{
					event->accept();
					return;
				}
				double origin[3];
				for ( int axis = 0; axis < 3; ++axis ) origin[axis] = cam.eye[axis] + ray[axis] * face.distance;
				QImage image;
				const bool loaded = m_materialRenderer.LoadMaterialImage( m_pDocument->ActiveMaterial(), image );
				const double overlayWidth = loaded ? (std::max)( 1, image.width() / 8 ) * 2.0 : 32.0;
				const double overlayHeight = loaded ? (std::max)( 1, image.height() / 8 ) * 2.0 : 32.0;
				m_pDocument->PlaceOverlay( face.solidId, sideId, origin, overlayWidth, overlayHeight );
			}
		}
		event->accept();
		return;
	}
	if ( event->button() == Qt::LeftButton && m_pDocument && m_pDocument->GetTool() == TOOL_DECAL )
	{
		Camera cam; BuildCamera( cam );
		const double x = ( event->position().x() - width() / 2.0 ) / cam.focal;
		const double y = ( height() / 2.0 - event->position().y() ) / cam.focal;
		double ray[3];
		for ( int axis = 0; axis < 3; ++axis ) ray[axis] = cam.forward[axis] + x * cam.right[axis] + y * cam.up[axis];
		const auto face = HammerFaceHit::Find( *m_pDocument, cam.eye, ray, kNearPlane );
		if ( face.solidId >= 0 )
		{
			double origin[3];
			for ( int axis = 0; axis < 3; ++axis ) origin[axis] = cam.eye[axis] + ray[axis] * face.distance;
			m_pDocument->PlaceDecal( origin );
		}
		event->accept();
		return;
	}
	if ( DisplacementPaintActive() && ( event->button() == Qt::LeftButton || event->button() == Qt::RightButton ) &&
		( m_displacementPaintButton != Qt::NoButton || !event->modifiers().testFlag( Qt::ControlModifier ) ) )
	{
		if ( m_displacementPaintButton == Qt::NoButton &&
			( event->button() == Qt::LeftButton || m_displacementPaintEffect == HammerDisplacementPaint::RaiseLower ) )
		{
			setFocus();
			m_displacementPaintButton = event->button();
			m_pDocument->BeginEdit();
			UpdateDisplacementPaint( event->pos(), true );
		}
		event->accept();
		return;
	}
	if ( event->button() == Qt::LeftButton || event->button() == Qt::RightButton )
	{
		m_bDragging = true;
		m_ptLast = event->pos();
		m_ptPress = event->pos();
		m_pressedButton = event->button();
		m_pressModifiers = event->modifiers();
		m_mouseMoved = false;
		m_moveCandidate = false;
		if ( event->button() == Qt::LeftButton && m_pDocument && m_pDocument->GetTool() == TOOL_SELECT &&
			 event->modifiers() == Qt::NoModifier )
		{
			double depth = 0.0;
			const int hit = HitTest( event->pos(), &depth );
			if ( hit >= 0 && m_pDocument->IsSelected( hit ) )
			{
				Camera cam; BuildCamera( cam );
				m_moveCandidate = true; m_moveDepth = depth; m_moveFocal = cam.focal;
				for ( int i = 0; i < 3; ++i ) { m_moveRight[i] = cam.right[i]; m_moveUp[i] = cam.up[i]; m_moveApplied[i] = 0.0; }
			}
		}
		if ( event->button() == Qt::RightButton ) m_bRightDrag = false;
		setFocus();
		event->accept();
		return;
	}
	QWidget::mousePressEvent( event );
}

void CQtViewport3D::mouseMoveEvent( QMouseEvent *event )
{
	if ( m_overlayDragEntity >= 0 )
	{
		if ( !( event->buttons() & Qt::LeftButton ) ) FinishOverlayDrag( true );
		else UpdateOverlayDrag( event->position(), event->modifiers().testFlag( Qt::ShiftModifier ) );
		event->accept();
		return;
	}
	if ( DisplacementPaintActive() && !m_mouseLook && !m_bDragging )
	{
		if ( m_displacementPaintButton != Qt::NoButton && !( event->buttons() & m_displacementPaintButton ) )
			FinishDisplacementPaint( true );
		const bool dab = m_displacementPaintButton != Qt::NoButton && !event->modifiers().testFlag( Qt::ControlModifier );
		UpdateDisplacementPaint( event->pos(), dab );
		event->accept();
		return;
	}
	if ( m_mouseLook )
	{
		const QPoint center = rect().center();
		const QPoint delta = event->pos() - center;
		if ( !delta.isNull() )
		{
			Camera cam; BuildCamera( cam );
			m_flYaw = std::fmod( m_flYaw - delta.x() * 0.4, 360.0 );
			m_flPitch = qBound( -89.0, m_flPitch + delta.y() * 0.4, 89.0 );
			const double yaw = qDegreesToRadians( m_flYaw ), pitch = qDegreesToRadians( m_flPitch );
			const double offset[3] = { std::cos( pitch ) * std::cos( yaw ), std::cos( pitch ) * std::sin( yaw ), std::sin( pitch ) };
			for ( int i = 0; i < 3; ++i ) m_vTarget[i] = cam.eye[i] - offset[i] * m_flDist;
			QCursor::setPos( mapToGlobal( center ) );
			UpdateViews();
		}
		event->accept();
		return;
	}
	if ( !m_bDragging )
	{
		QWidget::mouseMoveEvent( event );
		return;
	}
	if ( !( event->buttons() & m_pressedButton ) )
	{
		CancelMoveEdit();
		m_bDragging = false;
		m_pressedButton = Qt::NoButton;
		event->accept();
		return;
	}

	const int dragDistance = m_pressedButton == Qt::RightButton ? 4 : QApplication::startDragDistance();
	if ( !m_mouseMoved && ( event->pos() - m_ptPress ).manhattanLength() < dragDistance )
	{
		event->accept();
		return;
	}
	m_mouseMoved = true;
	if ( m_moveCandidate && m_pDocument )
	{
		if ( !m_moveEditActive ) { m_pDocument->BeginEdit(); m_moveEditActive = true; }
		const QPoint motion = event->pos() - m_ptPress;
		double delta[3];
		for ( int i = 0; i < 3; ++i )
		{
			double offset = m_moveDepth / m_moveFocal * ( motion.x() * m_moveRight[i] - motion.y() * m_moveUp[i] );
			if ( m_pDocument->SnapEnabled() ) offset = SnapCoord( offset, m_pDocument->GridSize() );
			delta[i] = offset - m_moveApplied[i];
		}
		if ( m_pDocument->MoveSelection( delta ) )
			for ( int i = 0; i < 3; ++i ) m_moveApplied[i] += delta[i];
		UpdateViews(); event->accept(); return;
	}
	const QPoint delta = event->pos() - m_ptLast;
	m_ptLast = event->pos();
	if ( ( event->buttons() & Qt::RightButton ) && ( event->pos() - m_ptPress ).manhattanLength() >= 4 )
		m_bRightDrag = true;

	m_flYaw = std::fmod( m_flYaw + delta.x() * 0.25, 360.0 );
	m_flPitch = qBound( -89.0, m_flPitch + delta.y() * 0.25, 89.0 );
	UpdateViews();
	event->accept();
}

void CQtViewport3D::mouseReleaseEvent( QMouseEvent *event )
{
	if ( m_overlayDragEntity >= 0 && event->button() == Qt::LeftButton )
	{
		FinishOverlayDrag( false );
		event->accept();
		return;
	}
	if ( event->button() == m_displacementPaintButton )
	{
		FinishDisplacementPaint( false );
		UpdateDisplacementPaint( event->pos(), false );
		event->accept();
		return;
	}
	if ( m_bDragging && event->button() == m_pressedButton )
	{
		m_bDragging = false;
		const int dragDistance = m_pressedButton == Qt::RightButton ? 4 : QApplication::startDragDistance();
		const bool dragged = m_mouseMoved || ( event->pos() - m_ptPress ).manhattanLength() >= dragDistance;
		if ( m_moveEditActive && m_pDocument ) { m_pDocument->EndEdit(); m_moveEditActive = false; }
		m_moveCandidate = false;
		if ( m_pressedButton == Qt::RightButton && dragged ) m_bRightDrag = true;
		if ( m_pressedButton == Qt::LeftButton && !dragged && m_pDocument && m_pDocument->GetTool() == TOOL_SELECT )
		{
			const int id = HitTest( event->pos() );
			if ( m_pressModifiers & Qt::ShiftModifier ) { if ( id >= 0 ) m_pDocument->ToggleSelection( id ); }
			else m_pDocument->SelectObject( id );
		}
		else if ( m_pressedButton == Qt::LeftButton && !dragged && m_pDocument && m_pDocument->GetTool() == TOOL_TEXTURE && height() > 0 )
		{
			Camera cam; BuildCamera( cam );
			const double x = ( event->pos().x() - width() / 2.0 ) / cam.focal;
			const double y = ( height() / 2.0 - event->pos().y() ) / cam.focal;
			double ray[3];
			for ( int i = 0; i < 3; ++i ) ray[i] = cam.forward[i] + x * cam.right[i] + y * cam.up[i];
			const auto face = HammerFaceHit::Find( *m_pDocument, cam.eye, ray, kNearPlane );
			const bool toggle = m_pressModifiers.testFlag( Qt::ControlModifier );
			const SolidLite *solid = m_pDocument->Map().SolidById( face.solidId );
			if ( !toggle ) m_selectedFaces.clear();
			if ( solid && face.faceIndex >= 0 && face.faceIndex < solid->sides.size() )
			{
				const int faceId = solid->sides[face.faceIndex].id;
				if ( faceId > 0 )
				{
					QSet<int> &selected = m_selectedFaces[solid->id];
					if ( toggle && selected.contains( faceId ) ) selected.remove( faceId ); else selected.insert( faceId );
					if ( selected.isEmpty() ) m_selectedFaces.remove( solid->id );
				}
			}
			UpdateViews();
		}
		else if ( m_pressedButton == Qt::RightButton && !dragged && m_pDocument && m_pDocument->GetTool() == TOOL_TEXTURE && height() > 0 )
		{
			Camera cam; BuildCamera( cam );
			const double x = ( event->pos().x() - width() / 2.0 ) / cam.focal;
			const double y = ( height() / 2.0 - event->pos().y() ) / cam.focal;
			double ray[3];
			for ( int i = 0; i < 3; ++i ) ray[i] = cam.forward[i] + x * cam.right[i] + y * cam.up[i];
			const auto face = HammerFaceHit::Find( *m_pDocument, cam.eye, ray, kNearPlane );
			if ( face.solidId >= 0 )
			{
				// Original Hammer applies to every face when Shift is held.
				if ( m_pressModifiers & Qt::ShiftModifier ) m_pDocument->ApplyMaterialToBrush( face.solidId );
				else m_pDocument->ApplyMaterialToFace( face.solidId, face.faceIndex );
			}
		}
		m_pressedButton = Qt::NoButton;
		event->accept();
		return;
	}
	QWidget::mouseReleaseEvent( event );
}

void CQtViewport3D::wheelEvent( QWheelEvent *event )
{
	if ( m_moveEditActive || m_displacementPaintButton != Qt::NoButton || m_overlayDragEntity >= 0 ) { event->accept(); return; }
	const double factor = ( event->angleDelta().y() > 0 ) ? 1.0 / 1.15 : 1.15;
	m_flDist = qBound( 64.0, m_flDist * factor, 8192.0 );
	UpdateViews();
	event->accept();
}
