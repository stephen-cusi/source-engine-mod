//========= Hammer Qt6 (hammer2/app) ==========================================
//
// Purpose: Shared visual language for the Qt6 editor. One place defines the
//          palette, the widget style sheet and the tool icon lookup so the
//          main window, the viewports and the dialogs cannot drift apart.
//
//          The palette follows the original Hammer/VGUI editor's dark grey
//          chrome but takes Hammer++'s cleaner separation between panels:
//          a near-black paper for the viewports, a slightly lifted chrome
//          for docks and toolbars, and a single warm accent (amber) that is
//          reserved for selection so it never competes with entity colours.
//
//          The shared colours cover the common viewport and chrome roles;
//          material-dependent rendering and individual widget states may use
//          additional colours where needed.
//
//=============================================================================

#pragma once

#include <QColor>
#include <QFont>
#include <QIcon>
#include <QRect>
#include <QString>

class QApplication;
class QPainter;

namespace HammerTheme
{

//-----------------------------------------------------------------------------
// Colours. Named by role, not by appearance, so a palette swap does not
// rename every call site.
//-----------------------------------------------------------------------------
struct Palette
{
	// Chrome
	QColor window;          // dock/toolbar background
	QColor windowDeep;      // menu bar, status bar, separators
	QColor panel;           // text entry / list background inside a dock
	QColor border;          // 1px panel outlines
	QColor borderStrong;    // focused outline, splitter handle
	QColor text;            // primary label text
	QColor textDim;         // secondary text, hints, disabled
	QColor textBright;      // headings, active tab label

	// Accent: selection and focus only. Nothing decorative uses it.
	QColor accent;          // amber, selection outline
	QColor accentSoft;      // translucent fill for selection
	QColor accentDim;       // inactive/unfocused variant

	// Viewports: 2D
	QColor view2dPaper;     // viewport background
	QColor view2dGridMinor;
	QColor view2dGridMajor;
	QColor view2dGridAxis;  // origin lines
	QColor view2dBrush;     // unselected world brush outline
	QColor view2dBrushFill; // faint brush body fill
	QColor view2dEntity;    // entity box / marker
	QColor view2dEntityText;
	QColor view2dHandle;    // resize handles, pivots
	QColor view2dTitle;     // viewport corner label (Top/Front/Side)
	QColor view2dHint;      // zoom/center readout

	// Viewports: 3D
	QColor view3dSky;       // gradient top
	QColor view3dHorizon;   // gradient bottom
	QColor view3dGround;    // grid floor
	QColor view3dAxisX;
	QColor view3dAxisY;
	QColor view3dAxisZ;
	QColor view3dFaceLit;   // brightest face shade
	QColor view3dFaceDim;   // darkest face shade
	QColor view3dEdge;
	QColor view3dEntity;
	QColor view3dEntityText;

	// Shared semantic colours used by both viewports
	QColor selection;       // == accent, aliased for clarity at call sites
	QColor pointFile;       // leak path
	QColor warning;
	QColor error;
};

// Single immutable instance. Safe to call from any thread that owns a widget.
const Palette &Colors();

//-----------------------------------------------------------------------------
// Typography. The editor uses the system UI font, scaled once here so the
// viewport overlays and the docks share one metric.
//-----------------------------------------------------------------------------
QFont UiFont( int pointDelta = 0, bool bold = false );
QFont MonospaceFont( int pointDelta = 0 );
// Font used for in-viewport text (axis labels, entity class names)
QFont ViewportFont( int pointDelta = 0, bool bold = false );

//-----------------------------------------------------------------------------
// Style sheet returned as a QString so it can be inspected in a debug build.
// Apply() sets the Fusion base style, a matching QPalette and this sheet.
//-----------------------------------------------------------------------------
QString StyleSheet();
void Apply( QApplication &app );

//-----------------------------------------------------------------------------
// Tool icons. Names map to :/tool_<name>.svg in res/hammer.qrc. A missing
// resource falls back to a drawn placeholder rather than a null icon, so a
// typo never leaves a blank toolbar button.
//-----------------------------------------------------------------------------
QIcon ToolIcon( const QString &name, const QColor &tint = QColor() );

// Icons every toolbar/dialog uses; declared here so the set stays finite.
namespace Icons
{
inline constexpr const char *Select       = "select";
inline constexpr const char *Entity       = "entity";
inline constexpr const char *Block        = "block";
inline constexpr const char *Camera       = "camera";
inline constexpr const char *ApplyMaterial= "apply_material";
inline constexpr const char *Clipper      = "clipper";
inline constexpr const char *Vertex       = "vertex";
inline constexpr const char *Cordon       = "cordon";
inline constexpr const char *Decal        = "decal";
inline constexpr const char *Overlay      = "overlay";
inline constexpr const char *Displacement = "displacement";
inline constexpr const char *Magnify      = "magnify";
inline constexpr const char *Path         = "path";
inline constexpr const char *New          = "new";
inline constexpr const char *Open         = "open";
inline constexpr const char *Save         = "save";
inline constexpr const char *RunMap       = "run_map";
inline constexpr const char *Undo         = "undo";
inline constexpr const char *Redo         = "redo";
inline constexpr const char *Grid         = "grid";
inline constexpr const char *Snap         = "snap";
inline constexpr const char *Group        = "group";
inline constexpr const char *Hollow       = "hollow";
inline constexpr const char *Carve        = "carve";
inline constexpr const char *Visgroup     = "visgroup";
inline constexpr const char *Texture      = "texture";
inline constexpr const char *Model        = "model";
inline constexpr const char *Filter       = "filter";
} // namespace Icons

//-----------------------------------------------------------------------------
// Fallback drawing for a missing icon resource. The viewport overlay helpers
// below use the same primitive style so a themed placeholder never looks
// foreign next to a real SVG.
//-----------------------------------------------------------------------------
void DrawPlaceholder( QPainter &painter, const QRectF &box, const QString &glyph,
                      const QColor &tint );

//-----------------------------------------------------------------------------
// Viewport chrome helpers. Both viewport widgets draw the same corner label,
// the same hint strip and the same focus outline; sharing the code keeps the
// two panes from drifting and keeps them matched to the palette.
//-----------------------------------------------------------------------------

// Rounded label chip in the top-left corner ("Top (x/y)", "3D Perspective").
void DrawViewportLabel( QPainter &painter, const QRect &widgetRect, const QString &text,
                        bool active );

// Bottom-left hint line ("zoom 1.5x  center 128, -64").
void DrawViewportHint( QPainter &painter, const QRect &widgetRect, const QString &text );

// 1px outline that lights up with the accent when the viewport holds focus.
void DrawViewportFrame( QPainter &painter, const QRect &widgetRect, bool focused );

} // namespace HammerTheme

