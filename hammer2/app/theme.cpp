#include "theme.h"

#include <QApplication>
#include <QFile>
#include <QFontDatabase>
#include <QPainter>
#include <QPalette>
#include <QPixmap>
#include <QStyleFactory>

namespace HammerTheme
{
const Palette &Colors()
{
	static const Palette colors = [] {
		Palette p;
		p.window = QColor( 48, 50, 53 ); p.windowDeep = QColor( 37, 39, 42 );
		p.panel = QColor( 37, 39, 41 ); p.border = QColor( 72, 75, 79 );
		p.borderStrong = QColor( 118, 125, 128 ); p.text = QColor( 228, 230, 232 );
		p.textDim = QColor( 164, 169, 173 ); p.textBright = QColor( 248, 249, 250 );
		p.accent = QColor( 255, 216, 64 ); p.accentSoft = QColor( 255, 216, 64, 24 );
		p.accentDim = QColor( 153, 132, 65 );
		p.view2dPaper = QColor( 54, 54, 58 ); p.view2dGridMinor = QColor( 62, 62, 68 );
		p.view2dGridMajor = QColor( 84, 84, 92 ); p.view2dGridAxis = QColor( 104, 104, 128 );
		p.view2dBrush = QColor( 196, 196, 200 ); p.view2dBrushFill = QColor( 196, 196, 200, 20 );
		p.view2dEntity = QColor( 96, 190, 240 ); p.view2dEntityText = QColor( 150, 200, 240 );
		p.view2dHandle = QColor( 255, 216, 64 ); p.view2dTitle = QColor( 232, 232, 232 );
		p.view2dHint = QColor( 168, 168, 172 );
		p.view3dSky = QColor( 44, 58, 84 ); p.view3dHorizon = QColor( 30, 34, 42 );
		p.view3dGround = QColor( 58, 64, 76 ); p.view3dAxisX = QColor( 190, 80, 80 );
		p.view3dAxisY = QColor( 80, 190, 80 ); p.view3dAxisZ = QColor( 80, 80, 220 );
		p.view3dFaceLit = QColor( 210, 210, 215 ); p.view3dFaceDim = QColor( 70, 70, 75 );
		p.view3dEdge = QColor( 200, 200, 210 ); p.view3dEntity = QColor( 96, 190, 240 );
		p.view3dEntityText = QColor( 150, 200, 240 );
		p.selection = p.accent; p.pointFile = QColor( 255, 0, 0 );
		p.warning = QColor( 245, 190, 70 ); p.error = QColor( 235, 105, 105 );
		return p;
	}();
	return colors;
}

QFont UiFont( int pointDelta, bool bold )
{
	QFont font = QFontDatabase::systemFont( QFontDatabase::GeneralFont );
	if ( font.pointSizeF() > 0 ) font.setPointSizeF( qMax( 6.0, font.pointSizeF() + pointDelta ) );
	font.setBold( bold );
	return font;
}

QFont MonospaceFont( int pointDelta )
{
	QFont font = QFontDatabase::systemFont( QFontDatabase::FixedFont );
	if ( font.pointSizeF() > 0 ) font.setPointSizeF( qMax( 6.0, font.pointSizeF() + pointDelta ) );
	return font;
}

QFont ViewportFont( int pointDelta, bool bold ) { return UiFont( pointDelta, bold ); }

QString StyleSheet()
{
	return QStringLiteral( R"(
QMainWindow, QDialog, QDockWidget { background: #303235; color: #e4e6e8; }
QMenuBar, QStatusBar, QToolBar { background: #25272a; color: #e4e6e8; }
QMenuBar { border-bottom: 1px solid #484b4f; spacing: 1px; }
QMenuBar::item { padding: 5px 9px; background: transparent; }
QMenuBar::item:selected, QMenuBar::item:pressed { background: #495055; }
QMenu { background: #303235; color: #e4e6e8; border: 1px solid #5e6367; padding: 4px 0; }
QMenu::item { padding: 5px 28px 5px 22px; }
QMenu::item:selected { background: #495b5c; color: #ffffff; }
QMenu::item:disabled { color: #85898b; }
QMenu::separator { height: 1px; background: #55595c; margin: 4px 7px; }
QToolBar { border: 0; border-bottom: 1px solid #484b4f; spacing: 2px; padding: 2px; }
QToolBar:vertical { border-bottom: 0; border-right: 1px solid #484b4f; }
QToolButton { color: #e4e6e8; background: transparent; border: 1px solid transparent; border-radius: 2px; padding: 3px; }
QToolButton:hover { background: #474b4e; border-color: #676c70; }
QToolButton:pressed, QToolButton:checked { background: #59523b; border-color: #dfc56d; }
QToolButton:checked:hover { background: #6b6244; border-color: #ffe080; }
QToolButton:disabled { color: #85898b; }
QDockWidget { color: #e4e6e8; }
QDockWidget::title { background: #292c2f; border-bottom: 1px solid #505458; padding: 6px 8px; text-align: left; }
QLineEdit, QPlainTextEdit, QTextEdit, QListView, QTreeView, QTableView, QComboBox, QSpinBox, QDoubleSpinBox {
  background: #252729; color: #e4e6e8; border: 1px solid #565b5e; border-radius: 2px; padding: 3px;
  selection-background-color: #49665f; selection-color: #ffffff;
}
QLineEdit:focus, QListView:focus, QTreeView:focus, QTableView:focus, QComboBox:focus, QSpinBox:focus, QDoubleSpinBox:focus { border-color: #8bb5a3; }
QLineEdit:disabled, QComboBox:disabled, QSpinBox:disabled, QDoubleSpinBox:disabled { color: #85898b; background: #303235; }
QListView::item, QTreeView::item, QTableView::item { min-height: 20px; }
QListView::item:selected, QTreeView::item:selected, QTableView::item:selected { background: #49665f; color: #ffffff; }
QHeaderView::section { background: #3a3d40; color: #e4e6e8; border: 0; border-right: 1px solid #55595c; border-bottom: 1px solid #55595c; padding: 4px; }
QPushButton { color: #e4e6e8; background: #414548; border: 1px solid #666c70; border-radius: 3px; padding: 5px 9px; }
QPushButton:hover { background: #52585a; border-color: #929a9c; }
QPushButton:pressed { background: #344e45; }
QPushButton:disabled { color: #85898b; background: #343638; border-color: #494c4f; }
QSplitter::handle { background: #484b4f; }
QStatusBar::item { border: 0; }
QToolTip { background: #f0f0e9; color: #202221; border: 1px solid #737873; padding: 4px 6px; }
QCheckBox, QRadioButton { spacing: 6px; color: #e4e6e8; }
QCheckBox:disabled, QRadioButton:disabled { color: #85898b; }
)" );
}

void Apply( QApplication &app )
{
	app.setStyle( QStyleFactory::create( QStringLiteral( "Fusion" ) ) );
	const Palette &p = Colors();
	QPalette palette;
	palette.setColor( QPalette::Window, p.window ); palette.setColor( QPalette::WindowText, p.text );
	palette.setColor( QPalette::Base, p.panel ); palette.setColor( QPalette::AlternateBase, p.window );
	palette.setColor( QPalette::Text, p.text ); palette.setColor( QPalette::Button, QColor( 65, 69, 72 ) );
	palette.setColor( QPalette::ButtonText, p.text ); palette.setColor( QPalette::BrightText, p.textBright );
	palette.setColor( QPalette::Highlight, QColor( 73, 102, 95 ) ); palette.setColor( QPalette::HighlightedText, Qt::white );
	palette.setColor( QPalette::ToolTipBase, QColor( 240, 240, 233 ) ); palette.setColor( QPalette::ToolTipText, QColor( 32, 34, 33 ) );
	for ( QPalette::ColorRole role : { QPalette::WindowText, QPalette::Text, QPalette::ButtonText } )
		palette.setColor( QPalette::Disabled, role, QColor( 133, 137, 139 ) );
	app.setPalette( palette );
	app.setStyleSheet( StyleSheet() );
}

void DrawPlaceholder( QPainter &painter, const QRectF &box, const QString &glyph, const QColor &tint )
{
	painter.save();
	painter.setRenderHint( QPainter::Antialiasing );
	painter.setPen( tint.isValid() ? tint : Colors().text );
	painter.setFont( UiFont( 0, true ) );
	painter.drawText( box, Qt::AlignCenter, glyph.left( 2 ).toUpper() );
	painter.restore();
}

QIcon ToolIcon( const QString &name, const QColor &tint )
{
	const QString path = QStringLiteral( ":/tool_%1.svg" ).arg( name );
	if ( QFile::exists( path ) ) return QIcon( path );
	QPixmap pixmap( 24, 24 ); pixmap.fill( Qt::transparent );
	QPainter painter( &pixmap ); painter.setRenderHint( QPainter::Antialiasing );
	const QColor color = tint.isValid() ? tint : Colors().text;
	QPen pen( color, 1.8, Qt::SolidLine, Qt::RoundCap, Qt::RoundJoin ); painter.setPen( pen );
	if ( name == QLatin1String( Icons::New ) )
	{
		painter.drawRect( QRectF( 5, 3, 14, 18 ) ); painter.drawLine( 12, 7, 12, 17 ); painter.drawLine( 8, 12, 16, 12 );
	}
	else if ( name == QLatin1String( Icons::Open ) )
	{
		painter.drawPolyline( QPolygonF() << QPointF( 3, 8 ) << QPointF( 3, 5 ) << QPointF( 9, 5 ) << QPointF( 11, 7 ) << QPointF( 19, 7 ) << QPointF( 19, 9 ) );
		painter.drawPolygon( QPolygonF() << QPointF( 3, 19 ) << QPointF( 6, 10 ) << QPointF( 21, 10 ) << QPointF( 18, 19 ) );
	}
	else if ( name == QLatin1String( Icons::Save ) )
	{
		painter.drawRect( QRectF( 4, 3, 16, 18 ) ); painter.drawRect( QRectF( 7, 3, 9, 6 ) ); painter.drawRect( QRectF( 7, 14, 10, 7 ) );
	}
	else if ( name == QLatin1String( Icons::RunMap ) )
	{
		painter.setBrush( QColor( 133, 193, 156 ) ); painter.drawPolygon( QPolygonF() << QPointF( 7, 4 ) << QPointF( 19, 12 ) << QPointF( 7, 20 ) );
	}
	else if ( name == QLatin1String( Icons::Undo ) || name == QLatin1String( Icons::Redo ) )
	{
		const bool redo = name == QLatin1String( Icons::Redo );
		painter.save(); if ( redo ) { painter.translate( 24, 0 ); painter.scale( -1, 1 ); }
		painter.drawPolyline( QPolygonF() << QPointF( 8, 5 ) << QPointF( 3, 10 ) << QPointF( 8, 15 ) );
		painter.drawArc( QRectF( 5, 9, 15, 11 ), 0, 180 * 16 ); painter.restore();
	}
	else DrawPlaceholder( painter, QRectF( 0, 0, 24, 24 ), name, color );
	return QIcon( pixmap );
}

void DrawViewportLabel( QPainter &painter, const QRect &widgetRect, const QString &text, bool active )
{
	painter.save();
	painter.setFont( ViewportFont( -1, true ) );
	const QFontMetrics metrics( painter.font(), painter.device() );
	const int labelWidth = metrics.horizontalAdvance( text ) + 16;
	const int labelHeight = metrics.height() + 6;
	QRect label( widgetRect.left() + 7, widgetRect.top() + 6, qMin( labelWidth, qMax( 0, widgetRect.width() - 14 ) ), labelHeight );
	painter.fillRect( label, QColor( 24, 26, 28, 210 ) );
	painter.setPen( active ? Colors().accent : Colors().view2dTitle );
	painter.drawText( label.adjusted( 6, 0, -6, 0 ), Qt::AlignVCenter | Qt::AlignLeft, metrics.elidedText( text, Qt::ElideRight, qMax( 0, label.width() - 12 ) ) );
	painter.restore();
}

void DrawViewportHint( QPainter &painter, const QRect &widgetRect, const QString &text )
{
	painter.save(); painter.setFont( ViewportFont( -1 ) ); painter.setPen( Colors().view2dHint );
	const QFontMetrics metrics( painter.font(), painter.device() );
	painter.drawText( widgetRect.adjusted( 8, 0, -8, -7 ), Qt::AlignBottom | Qt::AlignLeft,
		metrics.elidedText( text, Qt::ElideRight, qMax( 0, widgetRect.width() - 16 ) ) );
	painter.restore();
}

void DrawViewportFrame( QPainter &painter, const QRect &widgetRect, bool focused )
{
	painter.save(); painter.setPen( focused ? Colors().accentDim : Colors().border ); painter.setBrush( Qt::NoBrush );
	painter.drawRect( widgetRect.adjusted( 0, 0, -1, -1 ) ); painter.restore();
}
} // namespace HammerTheme
