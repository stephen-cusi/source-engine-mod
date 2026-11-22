//========= Hammer Qt6 (hammer2/app) ==========================================
//
// Purpose: 2D orthogonal viewport widget (Top XZ / Front XY / Side YZ).
//          Draws a world-stepped grid, brush wireframes, entity markers and
//          the selection highlight with QPainter, and implements the editing
//          tools: Select (click pick + grid-snapped move), Entity (place the
//          class chosen in the toolbar combo) and Block (drag a rectangle to
//          create a box solid). Wheel zooms, mouse move reports world coords.
//
//          TODO(hammer-port): switch to QOpenGLWidget and wire the
//          materialsystem render path for real scene drawing.
//
//=============================================================================

#pragma once

#include <QWidget>

class QPainter;
class QPaintEvent;
class QMouseEvent;
class QWheelEvent;
class QKeyEvent;
class CHammerDocument;

class CQtViewport2D : public QWidget
{
	Q_OBJECT

public:
	enum EViewAxis
	{
		VIEWAXIS_TOP = 0,   // XY plane (top view, looking down -Z)
		VIEWAXIS_FRONT,     // XZ plane (front view, looking down -Y)
		VIEWAXIS_SIDE,      // YZ plane (side view, looking down -X)
	};

	explicit CQtViewport2D( EViewAxis axis, QWidget *parent = nullptr );

	// Feeds the document in and connects the repaint signals
	void setDocument( CHammerDocument *pDocument );
	void setGridVisible( bool visible );
	void setNudgeEnabled( bool enabled ) { m_bNudgeEnabled = enabled; }
	bool nudgeEnabled() const { return m_bNudgeEnabled; }
	void centerOn( const double point[3] );
	void setViewAxis( EViewAxis axis );
	EViewAxis viewAxis() const { return m_eAxis; }
	void centerCoordinates( double point[3] ) const;
	bool gridVisible() const { return m_bGridVisible; }
	double zoom() const { return m_flZoom; }
	QString interactionHint() const;
	QString interactionHelp() const;
	void cycleClipMode();
	QPointF centerWorld() const { return QPointF( m_flCenterU, m_flCenterV ); }

	virtual void paintEvent( QPaintEvent *event ) override;

	// Render hook. Empty placeholder for now.
	// TODO(hammer-port): override once the materialsystem render path is wired.
	virtual void paintGL();

signals:
	// Carries "U: <x> V: <y>" for the status bar while the mouse moves
	void cursorWorldChanged( const QString &text );

protected:
	bool event( QEvent *event ) override;
	void mousePressEvent( QMouseEvent *event ) override;
	void mouseMoveEvent( QMouseEvent *event ) override;
	void mouseReleaseEvent( QMouseEvent *event ) override;
	void wheelEvent( QWheelEvent *event ) override;
	void keyPressEvent( QKeyEvent *event ) override;
	void contextMenuEvent( QContextMenuEvent *event ) override;

private:
	void FinishMoveEdit();
	void SelectRegion( double u0, double v0, double u1, double v1, bool insideOnly );
	// World axis indices (0=X, 1=Y, 2=Z) for this view's horizontal,
	// vertical and depth axes
	int AxisU() const;
	int AxisV() const;
	int AxisD() const;

	QPointF WorldToScreen( double u, double v ) const;
	void ScreenToWorld( const QPoint &pt, double &u, double &v ) const;
	double Snap( double value ) const;

	void DrawGrid( QPainter &painter );
	void DrawSolids( QPainter &painter );
	void DrawEntities( QPainter &painter );
	void DrawRubberBand( QPainter &painter );
	void DrawClipPlane( QPainter &painter );
	void DrawVertexHandles( QPainter &painter );
	void ClearClipPlane();
	int HitTest( const QPoint &pt ) const;
	QString AxisNameU() const;
	QString AxisNameV() const;
	QString GetAxisLabel() const;

	EViewAxis m_eAxis;
	CHammerDocument *m_pDocument;
	int m_nGridSize;        // fallback grid size when no document is set
	bool m_bGridVisible;
	bool m_bNudgeEnabled;

	// View transform: pixels per world unit + world position at the center
	double m_flZoom;
	double m_flCenterU;
	double m_flCenterV;
	double m_flCenterDepth = 0.0;

	// Interaction state
	enum EDragMode
	{
		DRAG_NONE = 0,
		DRAG_MOVE,      // dragging the selected object
		DRAG_BLOCK,     // drawing the block tool rubber band
		DRAG_SELECT,    // projected region selection
		DRAG_PAN,       // panning the viewport with the middle mouse button
		DRAG_CLIP,      // defining a clipping line in this projection
		DRAG_VERTEX,    // preview a projected vertex group
	};
	EDragMode m_eDragMode;
	bool m_bMoveEditActive;
	double m_flPressU;
	double m_flPressV;
	double m_flLastMoveU;   // last snapped position used for incremental moves
	double m_flLastMoveV;
	double m_flBlockU0;     // rubber band start (snapped)
	double m_flBlockV0;
	double m_flBlockU1;     // rubber band current corner (snapped)
	double m_flBlockV1;
	QPoint m_ptPanLast;
	QPoint m_ptSelectStart;
	bool m_bSelectInsideOnly = false;
	bool m_clipReady = false;
	int m_clipKeep = -1; // negative, positive, both
	double m_clipU0 = 0, m_clipV0 = 0, m_clipU1 = 0, m_clipV1 = 0;
	// -1 creates a new line; 0/1 drag an existing clipping handle.
	int m_clipDragHandle = -1;
	double m_clipPressU = 0, m_clipPressV = 0;
	double m_clipStartU0 = 0, m_clipStartV0 = 0, m_clipStartU1 = 0, m_clipStartV1 = 0;
	void UpdateClipDrag( double u, double v, bool moveAll );
	double m_vertexFromU = 0, m_vertexFromV = 0, m_vertexToU = 0, m_vertexToV = 0;
};
