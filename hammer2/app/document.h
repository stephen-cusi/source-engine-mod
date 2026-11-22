//========= Hammer Qt6 (hammer2/app) ==========================================
//
// Purpose: CHammerDocument - the Qt6 Hammer editor document. Owns the
//          lightweight map model (MapDocLite), the loaded FGD data
//          (CFgdData) and all shared editing state (active tool, grid size,
//          snap, entity class, selection). Provides the editing operations
//          used by the viewports (place entity, create brush, move, delete)
//          and real VMF read/write through VmfIO.
//
//=============================================================================

#pragma once

#include <QObject>
#include <QString>
#include <QList>
#include <QMap>
#include <QSet>

#include "fgddata.h"
#include "mapmodel.h"
#include "pointfile.h"
#include "viewfilter.h"

//-----------------------------------------------------------------------------
// Purpose: Active editing tool (matches the toolbar tool group).
//-----------------------------------------------------------------------------
enum EHammerTool
{
	TOOL_SELECT = 0,	// selection / move tool (hotkey 1)
	TOOL_ENTITY,		// entity placement tool (hotkey 2)
	TOOL_BLOCK,			// block (brush) drawing tool (hotkey 3)
	TOOL_TEXTURE,		// apply the current material to a picked face
	TOOL_CLIP,		// interactive 2D clipping plane
	TOOL_VERTEX,	// conservative projected vertex editing
	TOOL_CAMERA,
	TOOL_DECAL,
	TOOL_OVERLAY,
};

class CHammerDocument : public QObject
{
	Q_OBJECT

public:
	explicit CHammerDocument( QObject *parent = nullptr );

	// File -> New: reset to an empty document
	bool NewDocument();

	// File -> Open: pick a VMF file via QFileDialog and load it
	bool OpenDocument();

	// File -> Save: write to the current path, fall back to Save As
	bool SaveDocument();

	// File -> Save As: pick a target path via QFileDialog
	bool SaveDocumentAs();

	//--- Model access ---------------------------------------------------------
	MapDocLite &Map() { return m_Map; }
	const MapDocLite &Map() const { return m_Map; }
	quint64 DocumentIdentity() const { return m_nDocumentIdentity; }
	CFgdData &Fgd() { return m_Fgd; }

	// Purpose: Loads the FGD referenced by the game config. Returns false and
	//          fills errorOut on failure (the caller decides whether to warn).
	bool LoadFgd( const QString &fgdPath, QString *errorOut = nullptr );
	bool LoadFgds( const QStringList &paths, QString *errorOut = nullptr );

	//--- Editing state --------------------------------------------------------
	EHammerTool GetTool() const { return m_eTool; }
	void SetTool( EHammerTool tool );

	int GridSize() const { return m_nGridSize; }
	void SetGridSize( int size );

	bool SnapEnabled() const { return m_bSnap; }
	void SetSnapEnabled( bool enabled );
	bool GridVisible() const { return m_bGridVisible; }
	void SetGridVisible( bool visible );

	// View > Filter: view-only visibility state. Never dirties the document,
	// touches undo history or is saved; reset by New/Open.
	const ViewFilter &GetViewFilter() const { return m_ViewFilter; }
	void SetViewFilter( const ViewFilter &filter );

	// Class used by the entity placement tool
	QString EntityClass() const { return m_strEntityClass; }
	void SetEntityClass( const QString &className );

	// Depth (third axis size) of brushes created with the block tool
	int BlockDepth() const { return m_nBlockDepth; }
	void SetBlockDepth( int depth );

	// Material used by newly created brushes. Names are relative to materials/.
	QString ActiveMaterial() const { return m_strActiveMaterial; }
	bool SetActiveMaterial( const QString &name );
	bool ApplyMaterialToSelection();
	bool ApplyMaterialToFace( int solidId, int faceIndex );
	bool ApplyMaterialToBrush( int solidId );
	// Stable solid/face IDs; validates every face before one undoable edit.
	bool ApplyMaterialToFaces( const QMap<int, QSet<int>> &facesBySolid );
	bool CreateDisplacements( const QMap<int, QSet<int>> &facesBySolid, int power );
	bool RemoveDisplacements( const QMap<int, QSet<int>> &facesBySolid );
	bool SetDisplacementDistances( int solidId, int sideId, const QList<double> &distances );
	bool SewDisplacements( const QMap<int, QSet<int>> &facesBySolid );
	bool SetVisibleFaceProperties( const QMap<int, QList<SideLite>> &facesBySolid );
	// One selected world brush or a brush entity owning exactly one solid.
	const SolidLite *SingleSelectedSolid() const;
	QList<int> SelectedVisibleSolidIds() const;
	bool SetSelectedSolidFaceProperties( int solidId, const QList<SideLite> &faces );
	bool SetSelectedSolidsFaceProperties( const QMap<int, QList<SideLite>> &facesBySolid );
	// Updates only the material/UV properties of an unchanged, single brush.
	bool SetSelectedFaceProperties( const QList<SideLite> &faces );
	// Exact case-insensitive material replacement; -1 denotes an invalid name.
	enum class MaterialReplaceMode { Exact, PartialWhole, PartialSubstring };
	int ReplaceMaterial( const QString &find, const QString &replacement, bool selectionOnly,
		MaterialReplaceMode mode = MaterialReplaceMode::Exact );

	//--- Selection ------------------------------------------------------------
	int SelectedId() const { return m_nSelectedId; }
	const QSet<int> &SelectedIds() const { return m_SelectedIds; }
	bool IsSelected( int id ) const { return m_SelectedIds.contains( id ); }

	// Purpose: Selects one object (entity or solid) by id; -1 clears.
	void SelectObject( int id );
	void SelectObjects( const QSet<int> &ids ) { SetSelection( ids ); }
	void ToggleSelection( int id );
	void SelectAll();
	void InvertSelection();
	// Select point entities whose key/value matches the entity report filter.
	int SelectEntitiesByProperty( const QString &key, const QString &value, bool exact );

	//--- Editing operations ---------------------------------------------------
	// Purpose: Places a point entity of the given class at the snapped origin.
	//          Key/values are prefilled from the FGD defaults. Returns the new
	//          entity id or -1 when the class is unknown.
	int PlaceEntity( const QString &className, const double origin[3] );
	int PlaceDecal( const double origin[3] );
	int PlaceOverlay( int solidId, int sideId, const double origin[3], double width = 32.0, double height = 32.0 );
	bool MoveOverlayHandle( int entityId, int corner, const double origin[3] );

	// Purpose: Creates a box solid from snapped bounds. Returns the solid id
	//          or -1 for a degenerate box.
	int CreateBrush( const double mins[3], const double maxs[3] );

	// Purpose: Moves the selected object (entity origin or brush planes).
	bool MoveSelection( const double delta[3] );
	bool SetSelectedEntityOrigin( const double origin[3] );
	bool CenterSelectedBrushEntityOrigins();
	bool SnapSelectionToGrid( bool individually );
	// Hollow selected axis-aligned box brushes, atomically. Negative thickness
	// builds outward. Unsupported brushes leave the document unchanged.
	bool HollowSelection( int thickness );
	// Scale supported world boxes about the combined selection center.
	bool ScaleSelectedBoxes( const double factors[3] );
	// Rotate axis-aligned world boxes around the combined center by a quarter turn.
	bool RotateSelectedBoxes90( bool clockwise, int axis = 2 );
	bool FlipSelectedBoxes( int axis );
	bool TieSelectionToEntity( const QString &className );
	bool MoveSelectionToWorld();
	// Keep the lower half (-1), upper half (+1), or split into both (0).
	bool ClipSelectedBoxes( int axis, double coordinate, int keep );
	// Plane orientation follows VMF's Cross(p2-p0, p1-p0); -1 keeps the
	// side opposite the normal, +1 keeps the normal side, 0 keeps both.
	bool ClipSelectedByPlane( const double points[3][3], int keep );
	bool MoveSelectedProjectedVertices( int u, int v, double fromU, double fromV, double toU, double toV );
	const SolidLite *SelectedVertexEditableSolid() const;

	// Purpose: Deletes the selected object. Returns true if something was deleted.
	bool DeleteSelection();
	bool CopySelection();
	bool SaveSelectionAsPrefab( const QString &path, QString *error = nullptr ) const;
	bool InsertOriginalPrefab( const QString &path, QString *error = nullptr );
	bool CutSelection();
	int PasteSelection();
	bool HasClipboardObjects() const { return !m_ClipboardEntities.isEmpty() || !m_ClipboardSolids.isEmpty(); }
	bool ClipboardHasForeignVisgroupState() const;
	// Bounds of the copied objects, independent of the current selection.
	// Point entities are represented by their origins in the lightweight model.
	bool ClipboardSize( double size[3] ) const;
	bool ClipboardCenter( double center[3] ) const;
	int PasteSpecial( int copies, const double step[3], const double *firstOffset = nullptr );

	// Group related changes (such as the mouse moves in one drag) into one undo step.
	void BeginEdit();
	void EndEdit();
	void CancelEdit();
	bool Undo();
	bool Redo();
	bool SetSelectedEntityClass( const QString &className );
	bool SetSelectedEntityKey( const QString &key, const QString &value );
	bool SetSelectedEntitiesKey( const QString &key, const QString &value );
	bool ResetSelectedEntityKey( const QString &key );
	bool SetSelectedEntityOutputs( const QList<QPair<QString, QString> > &outputs );
	bool SetWorldProperties( const QMap<QString, QString> &properties );
	bool SetSavedCameras( const QList<CameraLite> &cameras, int activeIndex );
	bool SetCordon( const double mins[3], const double maxs[3], bool active );
	// Manual visgroup definitions and membership; parentId 0 creates a root.
	int CreateVisgroup( const QString &name, int parentId = 0 );
	bool RenameVisgroup( int groupId, const QString &name );
	bool DeleteLeafVisgroup( int groupId );
	bool SetSelectionVisgroupMembership( int groupId, bool add );
	// Session-only show/hide, recursively includes members of child groups.
	bool SetVisgroupVisible( int groupId, bool visible );
	// Select visible direct members of a manual visgroup, like Hammer's Mark.
	bool SelectVisgroupMembers( int groupId );

	// Purpose: Marks the map content as modified and notifies the views
	//          (used by direct-model editors like the properties panel).
	void TouchContent();

	//--- VMF I/O --------------------------------------------------------------
	bool LoadFromFile( const QString &fileName );
	bool RestoreFromRecovery( const QString &recoveryFile, const QString &originalFile );
	bool SaveToFile( const QString &fileName );
	bool LoadPointFile( const QString &fileName, int *errorLine = nullptr );
	void UnloadPointFile();
	const QList<PointFilePoint> &PointFilePoints() const { return m_PointFilePoints; }
	int PointFileIndex() const { return m_nPointFileIndex; }
	bool StepPointFile( int direction, PointFilePoint &point );

	const QString &GetFileName() const { return m_strFileName; }
	const QString &LastError() const { return m_strLastError; }
	bool IsDirty() const { return m_bDirty; }
	void SetDirty( bool dirty = true );

signals:
	void fileNameChanged( const QString &fileName );
	void dirtyChanged( bool dirty );

	// Emitted whenever the map content changed (repaint all viewports)
	void documentChanged();

	// Emitted when the selection changed (id = selected object or -1)
	void selectionChanged( int id );

	// Emitted when editing state (tool/grid/snap/entity class) changed
	void stateChanged();

private:
	bool ReplaceDisplacements( const QMap<int, QSet<int>> &facesBySolid, int power );
	struct HistoryState
	{
		MapDocLite map;
		int selectedId;
		QSet<int> selectedIds;
		quint64 revision;
	};
	struct HistoryEntry
	{
		HistoryState before;
		HistoryState after;
	};
	HistoryState CaptureHistoryState() const;
	void RestoreHistoryState( const HistoryState &state );
	void SetSelection( const QSet<int> &ids, int primaryId = -1 );
	void ResetHistory();
	void UpdateHistoryDirty();

	QString m_strFileName;
	QString m_strLastError;
	bool m_bDirty;

	MapDocLite m_Map;
	QList<PointFilePoint> m_PointFilePoints;
	int m_nPointFileIndex = -1;
	CFgdData m_Fgd;

	EHammerTool m_eTool;
	int m_nGridSize;
	bool m_bSnap;
	bool m_bGridVisible = true;
	ViewFilter m_ViewFilter;
	bool m_bPersistGridSize = false, m_bPersistSnap = false, m_bPersistGridVisible = false;
	QString m_strEntityClass;
	QString m_strActiveMaterial;
	int m_nBlockDepth;
	int m_nSelectedId;
	QSet<int> m_SelectedIds;
	QList<EntityLite> m_ClipboardEntities;
	QList<SolidLite> m_ClipboardSolids;
	int m_nClipboardPrimaryId;
	int m_nClipboardPasteCount;
	quint64 m_nDocumentIdentity = 0, m_nClipboardDocumentIdentity = 0;
	QList<HistoryEntry> m_UndoHistory;
	QList<HistoryEntry> m_RedoHistory;
	HistoryState m_EditBefore;
	int m_nEditDepth;
	bool m_bEditChanged;
	quint64 m_nRevision;
	quint64 m_nNextRevision;
	quint64 m_nSavedRevision;
};
