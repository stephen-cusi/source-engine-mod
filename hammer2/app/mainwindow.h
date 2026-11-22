//========= Hammer Qt6 (hammer2/app) ==========================================
//
// Purpose: Hammer (Qt6) main window, laid out after the GMod-era Hammer
//          editor:
//            - Menu bar: File / Edit / View / Map / Tools / Help
//            - Two tool bars: Map tools + edit tool group with the entity
//              class combo (populated from the loaded FGD)
//            - Central 2x2 splitter with one 3D and three 2D viewports
//            - Dock widgets: Texture Browser / Object Properties / Output
//            - Status bar: coordinate readout + grid size + selection count
//
//          The document owns the MapDocLite model, the FGD data and the
//          editing state; this window is the command surface for it.
//
//=============================================================================

#pragma once

#include <QMainWindow>
#include <QStringList>
#include "recovery.h"

class QAction;
class QActionGroup;
class QComboBox;
class QDockWidget;
class QLabel;
class QListWidget;
class QMenu;
class QPlainTextEdit;
class QSplitter;
class QToolBar;
class QTreeWidget;
class QTreeWidgetItem;
class QTimer;

class CHammerDocument;
class CEntityReportDialog;
class CQtViewport2D;
class CQtViewport3D;

class CMainWindow : public QMainWindow
{
    Q_OBJECT

public:
    explicit CMainWindow( QWidget *parent = nullptr, bool initializeUserState = true );
    ~CMainWindow() override;
    bool OpenDocumentPath( const QString &fileName );
    void OfferRecovery();

protected:
    void closeEvent( QCloseEvent *event ) override;
    bool eventFilter( QObject *watched, QEvent *event ) override;

private slots:
    // File
    void OnNewDocument();
    void OnOpenDocument();
    void OnSaveDocument();
    void OnSaveDocumentAs();
    void OnRunMap();
    void OnRecentTriggered( QAction *action );

    // Edit (placeholders)
    void OnUndo();
    void OnRedo();
    void OnCut();
    void OnCopy();
    void OnPaste();
    void OnDelete();

    // Edit -> Selection (placeholders)
    void OnSelectAll();
    void OnSelectNone();
    void OnSelectInvert();
    void OnSelectByProperty();

    // View (placeholders)
    void OnMaximizeViewport();
    void OnFilter();
    void OnRestoreDefaultLayout();

    // Map
    void OnSnapToGrid( bool checked );
    void OnGridSettings();
    void OnGridSizeChanged( QAction *action );
    void OnEntityReport();
    void OnMapProperties();

    // Tools: edit tool switching (Select / Entity / Block)
    void OnToolModeChanged( QAction *action );
    void OnLanguageTriggered( QAction *action );

    // Entity class combos: toolbar picker vs selected-entity class
    void OnEntityToolChanged( const QString &className );
    void OnSelectedEntityClassChanged( const QString &className );

    // Object Properties panel: cell edit write-back
    void OnPropertyItemChanged( QTreeWidgetItem *item, int column );

    // Help
    void OnAbout();

    // Document callbacks
    void OnDocumentFileNameChanged( const QString &fileName );
    void OnDocumentDirtyChanged( bool dirty );
    void OnDocumentChanged();
    void OnSelectionChanged( int id );
    void OnViewportCursorMoved( const QString &text );

private:
    void CreateMenus();
    void CreateToolBars();
    void CreateCentralWidget();
    void CreateDockWidgets();
    void CreateStatusBar();
    void CreateDocumentConnections();

    void InitializeGameData();
    void PopulateEntityClasses();
    void PopulatePropertyTree();
    void UpdateSelectionStatus();

    void LoadRecentFiles();
    void UpdateRecentFilesMenu();
    void AddRecentFile( const QString &fileName );

    void SaveLayout();
    void RestoreLayout();
    void RestoreMaximizedViewport();
    void LogOutput( const QString &message );
    void UpdateTitle();
    bool ConfirmDiscardChanges();

    CHammerDocument *m_pDocument;
    CEntityReportDialog *m_pEntityReport = nullptr;
    QTimer *m_pRecoveryTimer;
    CHammerRecoveryStore m_recoveryStore;

    // Menus
    QMenu *m_pMenuFile;
    QMenu *m_pMenuRecent;
    QMenu *m_pMenuEdit;
    QMenu *m_pMenuSelection;
    QMenu *m_pMenuView;
    QMenu *m_pMenuMap;
    QMenu *m_pMenuTools;
    QMenu *m_pMenuHelp;

    // Actions referenced by both the menus and the tool bars
    QAction *m_pActionNew;
    QAction *m_pActionOpen;
    QAction *m_pActionSave;
    QAction *m_pActionRunMap;
    QAction *m_pActionUndo;
    QAction *m_pActionRedo;

    // Tool bars
    QToolBar *m_pMapToolBar;
    QToolBar *m_pBoundaryToolBar;
    QActionGroup *m_pToolActionGroup;
    QComboBox *m_pEntityToolCombo;      // entity class picker on the tool bar

    // Central four-viewport layout
    QSplitter *m_pViewportSplitter;
    QSplitter *m_pViewLeftSplitter;
    QSplitter *m_pView2DSplitter;
    CQtViewport3D *m_pViewport3D;
    CQtViewport2D *m_pViewportTop;      // top (XY)
    CQtViewport2D *m_pViewportFront;    // front (XZ)
    CQtViewport2D *m_pViewportSide;     // side (YZ)
    QWidget *m_pLastViewport;
    QWidget *m_pMaximizedViewport;
    QByteArray m_viewportSplitterState;
    QByteArray m_viewLeftSplitterState;
    QByteArray m_view2DSplitterState;

    // Dock widgets
    QDockWidget *m_pTextureBrowserDock;
    QDockWidget *m_pObjectPropertiesDock;
    QDockWidget *m_pOutputDock;
    QListWidget *m_pTextureList;
    QComboBox *m_pEntityClassCombo;     // class of the selected entity
    QTreeWidget *m_pPropertyTree;
    QPlainTextEdit *m_pOutputEdit;
    bool m_bUpdatingProps;              // guards property tree rebuilds
    bool m_bLanguageRestartConfirmed;   // this restart already confirmed unsaved edits

    // Status bar
    QLabel *m_pStatusCoords;
    QLabel *m_pStatusGrid;
    QLabel *m_pStatusSelection;

    // Recent files
    QStringList m_lstRecentFiles;
};
