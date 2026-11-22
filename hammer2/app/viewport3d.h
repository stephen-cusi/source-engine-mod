//========= Hammer Qt6 (hammer2/app) ==========================================
//
// Purpose: 3D perspective viewport widget with a simple orbit camera.
//          Draws a ground grid, brush wireframes (12 edges per solid) and
//          entity markers with QPainter. Mouse drag orbits, wheel zooms.
//          Selected objects can be moved by dragging in the camera plane.
//
//          TODO(hammer-port): switch to QOpenGLWidget and wire the
//          materialsystem render path for real scene drawing.
//
//=============================================================================

#pragma once

#include <QWidget>
#include <array>
#include <optional>
#include "materialpreviewrenderer.h"

class QPainter;
class QPaintEvent;
class QMouseEvent;
class QWheelEvent;
class QContextMenuEvent;
class QKeyEvent;
class CHammerDocument;
class CTexturedViewportSurface;

class CQtViewport3D : public QWidget
{
	Q_OBJECT

public:
	explicit CQtViewport3D( QWidget *parent = nullptr );
	~CQtViewport3D() override;

	// Feeds the document in and connects the repaint signals
	void setDocument( CHammerDocument *pDocument );
	void centerOn( const double point[3] );
	void setCameraPositionAngles( const double eye[3], const double angles[3] );
	void cameraBasis( double eye[3], double forward[3], double up[3] ) const;
	enum DisplayMode { Flat, Wireframe, Textured };
	void setDisplayMode( DisplayMode mode );
	DisplayMode displayMode() const { return m_displayMode; }
	void setGroundGridVisible( bool visible );
	bool groundGridVisible() const { return m_groundGridVisible; }
	void setMaterialGameDirectory( const QString &gameDir );
	int cachedMaterialTextureCount() const { return m_materialRenderer.CachedTextureCount(); }
	bool hasLoadedMaterialTexture( const QString &name ) const { return m_materialRenderer.HasLoadedTexture( name ); }
	QImage texturedFramebuffer();
	QString interactionHint() const;
	QString interactionHelp() const;
	const QMap<int, QSet<int>> &selectedFaces() const { return m_selectedFaces; }
	void setDisplacementPaint( bool enabled, int effect = 0, double radius = 64, double amount = 8, bool soft = true );

signals:
	void cameraPositionChanged();

protected:
	bool event( QEvent *event ) override;
	void paintEvent( QPaintEvent *event ) override;
	void resizeEvent( QResizeEvent *event ) override;
	void mousePressEvent( QMouseEvent *event ) override;
	void mouseMoveEvent( QMouseEvent *event ) override;
	void mouseReleaseEvent( QMouseEvent *event ) override;
	void wheelEvent( QWheelEvent *event ) override;
	void contextMenuEvent( QContextMenuEvent *event ) override;
	void keyPressEvent( QKeyEvent *event ) override;

private:
	friend class CTexturedViewportSurface;
	void PaintScene( QWidget *target, bool textured );
	void UpdateViews();
	void DisableTexturedSurface();
	// Simple double-precision vector helpers for the orbit camera math
	struct Camera
	{
		double eye[3];
		double forward[3];
		double right[3];
		double up[3];
		double focal; // pixels per unit at distance 1
	};

	void BuildCamera( Camera &cam ) const;
	bool Project( const Camera &cam, const double p[3], QPointF &out ) const;
	void DrawBoxWireframe( QPainter &painter, const Camera &cam,
		const double mins[3], const double maxs[3] );
	void DrawGridFloor( QPainter &painter, const Camera &cam );
	void DrawSolids( QPainter &painter, const Camera &cam );
	void DrawEntities( QPainter &painter, const Camera &cam );
	int HitTest( const QPoint &position, double *depth = nullptr ) const;
	void CancelMoveEdit();
	void SetMouseLook( bool enabled );
	void ClearFaceSelection();
	void PruneFaceSelection();
	bool DisplacementPaintActive() const;
	void UpdateDisplacementPaint( const QPoint &position, bool dab );
	void FinishDisplacementPaint( bool cancel );
	void DrawDisplacementBrush( QPainter &painter, const Camera &cam );

	CHammerDocument *m_pDocument;
	CMaterialPreviewRenderer m_materialRenderer;
	CTexturedViewportSurface *m_texturedSurface = nullptr;
	QMap<int, QSet<int>> m_selectedFaces; // session-only solid ID -> face IDs
	bool m_displacementPaintEnabled = false;
	bool m_displacementPaintApplying = false;
	int m_displacementPaintEffect = 0;
	double m_displacementPaintRadius = 64;
	double m_displacementPaintAmount = 8;
	bool m_displacementPaintSoft = true;
	Qt::MouseButton m_displacementPaintButton = Qt::NoButton;
	std::optional<std::array<double, 3>> m_displacementBrushCenter;

	// Orbit camera state
	double m_flYaw;         // degrees around the Z axis
	double m_flPitch;       // degrees above the horizon
	double m_flRoll;
	double m_flDist;        // distance from the orbit target
	double m_vTarget[3];
	double m_lastNotifiedEye[3] = {};
	bool m_hasNotifiedEye = false;
	bool m_bDragging;
	bool m_mouseLook = false;
	bool m_groundGridVisible = true;
	DisplayMode m_displayMode;
	QPoint m_ptLast;
	QPoint m_ptPress;
	Qt::MouseButton m_pressedButton = Qt::NoButton;
	Qt::KeyboardModifiers m_pressModifiers = Qt::NoModifier;
	bool m_mouseMoved = false;
	bool m_bRightDrag = false;
	bool m_moveCandidate = false;
	bool m_moveEditActive = false;
	double m_moveDepth = 0.0;
	double m_moveFocal = 1.0;
	double m_moveRight[3] = {};
	double m_moveUp[3] = {};
	double m_moveApplied[3] = {};
};
