//========= Hammer Qt6 (hammer2/app) ==========================================
//
// Purpose: Hammer (Qt6) main window implementation. Pure Qt widgets so it
//          builds on Windows/Linux/macOS. The window wires menus/toolbars/
//          docks to CHammerDocument, which owns the MapDocLite model, the
//          FGD data (via fgdlib) and all editing state.
//
//=============================================================================

#include "mainwindow.h"

#include "document.h"
#include "gameconfig.h"
#include "vmfio.h"
#include "cordon.h"
#include <QTemporaryDir>
#include "localization.h"
#include "mapmodel.h"
#include "mappropertiesdialog.h"
#include "mapcheck.h"
#include "mapcheckgoto.h"
#include "gridsettings.h"
#include "materialcatalog.h"
#include "materialbrowserpreview.h"
#include "facepropertiesdialog.h"
#include "displacementedit.h"
#include "navigation.h"
#include "selectioncenter.h"
#include "savedcameras.h"
#include "pointfilestate.h"
#include "pastespecialsettings.h"
#include "portableconfig.h"
#include "theme.h"
#include "propertyvaluedelegate.h"
#include "entityreportdialog.h"
#include "entitypropertyrows.h"
#include "entityoutputsdialog.h"
#include "visgroupdialog.h"
#include "viewport2d.h"
#include "viewport3d.h"

#include <QAction>
#include <QActionGroup>
#include <QCloseEvent>
#include <QCheckBox>
#include <QDialog>
#include <QDialogButtonBox>
#include <QFormLayout>
#include <QInputDialog>
#include <QShortcut>
#include <QComboBox>
#include <QCoreApplication>
#include <QDir>
#include <QDockWidget>
#include <QDoubleSpinBox>
#include <QFile>
#include <QFileDialog>
#include <QFileInfo>
#include <QIcon>
#include <QLabel>
#include <QLineEdit>
#include <QListWidget>
#include <QMenu>
#include <QMenuBar>
#include <QMessageBox>
#include <QHBoxLayout>
#include <QHeaderView>
#include <QPlainTextEdit>
#include <QProcess>
#include <QProgressDialog>
#include <QPushButton>
#include <QSaveFile>
#include <QSignalBlocker>
#include <QSettings>
#include <QSplitter>
#include <QSpinBox>
#include <QStatusBar>
#include <QStyle>
#include <QStyledItemDelegate>
#include <QTableWidget>
#include <QTime>
#include <QTimer>
#include <QThread>
#include <cmath>
#include <memory>
#include <QToolBar>
#include <QTreeWidget>
#include <QVBoxLayout>

namespace
{
    class CReadOnlyPropertyNameDelegate : public QStyledItemDelegate
    {
    public:
        using QStyledItemDelegate::QStyledItemDelegate;
        QWidget *createEditor( QWidget *, const QStyleOptionViewItem &, const QModelIndex & ) const override { return nullptr; }
    };
    const int kMaxRecentFiles = 8;
    const int kDefaultGridSize = 16;
    const int kDefaultWindowWidth = 1600;
    const int kDefaultWindowHeight = 1000;

    // Grid sizes offered in the View -> Grid Size submenu (Hammer powers of 2)
    const int kGridSizes[] = { 1, 2, 4, 8, 16, 32, 64, 128, 256, 512 };
    const int kGridSizeCount = static_cast<int>( sizeof( kGridSizes ) / sizeof( kGridSizes[0] ) );
} // anonymous namespace

CMainWindow::CMainWindow( QWidget *parent, bool initializeUserState )
    : QMainWindow( parent )
    , m_pDocument( new CHammerDocument( this ) )
    , m_pRecoveryTimer( nullptr )
    , m_pMenuFile( nullptr )
    , m_pMenuRecent( nullptr )
    , m_pMenuEdit( nullptr )
    , m_pMenuSelection( nullptr )
    , m_pMenuView( nullptr )
    , m_pMenuMap( nullptr )
    , m_pMenuTools( nullptr )
    , m_pMenuHelp( nullptr )
    , m_pActionNew( nullptr )
    , m_pActionOpen( nullptr )
    , m_pActionSave( nullptr )
    , m_pActionRunMap( nullptr )
    , m_pActionUndo( nullptr )
    , m_pActionRedo( nullptr )
    , m_pMapToolBar( nullptr )
    , m_pBoundaryToolBar( nullptr )
    , m_pToolActionGroup( nullptr )
    , m_pEntityToolCombo( nullptr )
    , m_pViewportSplitter( nullptr )
    , m_pViewLeftSplitter( nullptr )
    , m_pView2DSplitter( nullptr )
    , m_pViewport3D( nullptr )
    , m_pViewportTop( nullptr )
    , m_pViewportFront( nullptr )
    , m_pViewportSide( nullptr )
    , m_pLastViewport( nullptr )
    , m_pMaximizedViewport( nullptr )
    , m_pTextureBrowserDock( nullptr )
    , m_pObjectPropertiesDock( nullptr )
    , m_pOutputDock( nullptr )
    , m_pTextureList( nullptr )
    , m_pEntityClassCombo( nullptr )
    , m_pPropertyTree( nullptr )
    , m_pOutputEdit( nullptr )
    , m_bUpdatingProps( false )
    , m_bLanguageRestartConfirmed( false )
    , m_pStatusCoords( nullptr )
    , m_pStatusGrid( nullptr )
    , m_pStatusSelection( nullptr )
{
    setObjectName( "hammerMainWindow" );
    setWindowIcon( QIcon( ":/hammer-256.png" ) );

    // Start from a clean worldspawn-only document
    m_pDocument->NewDocument();

    CreateCentralWidget();
    CreateDockWidgets();
    CreateMenus();
    CreateToolBars();
    CreateStatusBar();
    CreateDocumentConnections();
    m_pRecoveryTimer = new QTimer( this );
    m_pRecoveryTimer->setSingleShot( true );
    m_pRecoveryTimer->setInterval( 1200 );
    connect( m_pRecoveryTimer, &QTimer::timeout, this, [this] {
        if ( !m_pDocument->IsDirty() ) return;
        QString error;
        if ( !m_recoveryStore.Write( m_pDocument->Map(), m_pDocument->GetFileName(), &error ) )
            LogOutput( tr( "Automatic recovery copy failed: %1" ).arg( error ) );
    } );
    if ( initializeUserState )
    {
        LoadRecentFiles();
        RestoreLayout();
    }
    else
        resize( kDefaultWindowWidth, kDefaultWindowHeight );
    UpdateTitle();

    // Read GameConfig.txt and load the FGD (warns but keeps running when the
    // FGD file is missing, leaving the entity list empty)
    if ( initializeUserState )
        InitializeGameData();

    PopulatePropertyTree();

    LogOutput( tr( "Hammer (Qt6) ready" ) );
}

CMainWindow::~CMainWindow()
{
}

void CMainWindow::closeEvent( QCloseEvent *event )
{
    if ( m_pDocument->IsDirty() && !m_bLanguageRestartConfirmed )
    {
        QMessageBox::StandardButton reply = QMessageBox::question(
            this,
            tr( "Hammer" ),
            tr( "The map has been modified. Save changes before quitting?" ),
            QMessageBox::Save | QMessageBox::Discard | QMessageBox::Cancel );

        if ( reply == QMessageBox::Cancel )
        {
            event->ignore();
            return;
        }

        if ( reply == QMessageBox::Save && !m_pDocument->SaveDocument() )
        {
            event->ignore();
            return;
        }
    }

    SaveLayout();
    m_pRecoveryTimer->stop();
    m_recoveryStore.Clear();
    event->accept();
}

//-----------------------------------------------------------------------------
// Menu bar
//-----------------------------------------------------------------------------
void CMainWindow::CreateMenus()
{
    const QIcon newIcon = HammerTheme::ToolIcon( QLatin1String( HammerTheme::Icons::New ) );
    const QIcon openIcon = HammerTheme::ToolIcon( QLatin1String( HammerTheme::Icons::Open ) );
    const QIcon saveIcon = HammerTheme::ToolIcon( QLatin1String( HammerTheme::Icons::Save ) );
    const QIcon runIcon = HammerTheme::ToolIcon( QLatin1String( HammerTheme::Icons::RunMap ) );
    const QIcon undoIcon = HammerTheme::ToolIcon( QLatin1String( HammerTheme::Icons::Undo ) );
    const QIcon redoIcon = HammerTheme::ToolIcon( QLatin1String( HammerTheme::Icons::Redo ) );

    //--- File -----------------------------------------------------------------
    m_pMenuFile = menuBar()->addMenu( tr( "&File" ) );

    m_pActionNew = new QAction( newIcon, tr( "&New" ), this );
    m_pActionNew->setShortcut( QKeySequence::New );
    connect( m_pActionNew, &QAction::triggered, this, &CMainWindow::OnNewDocument );
    m_pMenuFile->addAction( m_pActionNew );

    m_pActionOpen = new QAction( openIcon, tr( "&Open..." ), this );
    m_pActionOpen->setShortcut( QKeySequence::Open );
    connect( m_pActionOpen, &QAction::triggered, this, &CMainWindow::OnOpenDocument );
    m_pMenuFile->addAction( m_pActionOpen );

    m_pActionSave = new QAction( saveIcon, tr( "&Save" ), this );
    m_pActionSave->setShortcut( QKeySequence::Save );
    connect( m_pActionSave, &QAction::triggered, this, &CMainWindow::OnSaveDocument );
    m_pMenuFile->addAction( m_pActionSave );

    QAction *actionSaveAs = new QAction( tr( "Save &As..." ), this );
    actionSaveAs->setShortcut( QKeySequence::SaveAs );
    connect( actionSaveAs, &QAction::triggered, this, &CMainWindow::OnSaveDocumentAs );
    m_pMenuFile->addAction( actionSaveAs );

    m_pMenuFile->addSeparator();

    m_pActionRunMap = new QAction( runIcon, tr( "Run &Map..." ), this );
    m_pActionRunMap->setShortcut( QKeySequence( "F9" ) );
    connect( m_pActionRunMap, &QAction::triggered, this, &CMainWindow::OnRunMap );
    m_pMenuFile->addAction( m_pActionRunMap );

    m_pMenuFile->addSeparator();

    m_pMenuRecent = m_pMenuFile->addMenu( tr( "Open &Recent" ) );
    connect( m_pMenuRecent, &QMenu::triggered, this, &CMainWindow::OnRecentTriggered );

    m_pMenuFile->addSeparator();

    QAction *actionQuit = new QAction( tr( "E&xit" ), this );
    actionQuit->setShortcut( QKeySequence( "Ctrl+Q" ) );
    connect( actionQuit, &QAction::triggered, this, &CMainWindow::close );
    m_pMenuFile->addAction( actionQuit );

    //--- Edit -----------------------------------------------------------------
    m_pMenuEdit = menuBar()->addMenu( tr( "&Edit" ) );

    QAction *selectFgds = new QAction( tr( "Select FGD files..." ), this );
    m_pMenuEdit->addAction( selectFgds );
    connect( selectFgds, &QAction::triggered, this, [this] {
        QSettings settings;
        const QStringList previous = settings.value( "game/fgdFiles" ).toStringList();
        const QString initial = previous.isEmpty() ? QCoreApplication::applicationDirPath() : QFileInfo( previous.first() ).absolutePath();
        const QStringList selected = QFileDialog::getOpenFileNames( this, tr( "Select FGD files" ), initial, tr( "Game data files (*.fgd);;All files (*)" ) );
        if ( selected.isEmpty() ) return;
        QString error;
        if ( !m_pDocument->LoadFgds( selected, &error ) )
        {
            QMessageBox::warning( this, tr( "Hammer" ), tr( "Failed to load game data; previous files remain active.\n%1" ).arg( error ) );
            return;
        }
        settings.setValue( "game/fgdFiles", selected );
        PopulateEntityClasses();
        PopulatePropertyTree();
        LogOutput( tr( "Loaded %1 FGD files (%2 point classes)." ).arg( selected.size() ).arg( m_pDocument->Fgd().PointClassNames().count() ) );
    } );
    m_pMenuEdit->addSeparator();

    m_pActionUndo = new QAction( undoIcon, tr( "&Undo" ), this );
    m_pActionUndo->setObjectName( "actionUndo" );
    m_pActionUndo->setShortcut( QKeySequence::Undo );
    m_pActionUndo->setToolTip( tr( "Undo (Ctrl+Z)" ) );
    connect( m_pActionUndo, &QAction::triggered, this, &CMainWindow::OnUndo );
    m_pMenuEdit->addAction( m_pActionUndo );

    m_pActionRedo = new QAction( redoIcon, tr( "&Redo" ), this );
    m_pActionRedo->setObjectName( "actionRedo" );
    m_pActionRedo->setShortcut( QKeySequence::Redo );
    m_pActionRedo->setToolTip( tr( "Redo (Ctrl+Y)" ) );
    connect( m_pActionRedo, &QAction::triggered, this, &CMainWindow::OnRedo );
    m_pMenuEdit->addAction( m_pActionRedo );

    m_pMenuEdit->addSeparator();

    QAction *actionCut = new QAction( tr( "Cu&t" ), this );
    actionCut->setShortcut( QKeySequence::Cut );
    connect( actionCut, &QAction::triggered, this, &CMainWindow::OnCut );
    m_pMenuEdit->addAction( actionCut );

    QAction *actionCopy = new QAction( tr( "&Copy" ), this );
    actionCopy->setShortcut( QKeySequence::Copy );
    connect( actionCopy, &QAction::triggered, this, &CMainWindow::OnCopy );
    m_pMenuEdit->addAction( actionCopy );

    QAction *actionPaste = new QAction( tr( "&Paste" ), this );
    actionPaste->setShortcut( QKeySequence::Paste );
    connect( actionPaste, &QAction::triggered, this, &CMainWindow::OnPaste );
    m_pMenuEdit->addAction( actionPaste );

    QAction *pasteSpecial = new QAction( QCoreApplication::translate( "PasteSpecial", "Paste Special..." ), this );
    pasteSpecial->setShortcut( QKeySequence( "Ctrl+Shift+V" ) );
    connect( pasteSpecial, &QAction::triggered, this, [this] {
        if ( !m_pDocument->HasClipboardObjects() ) return;
        if ( m_pDocument->ClipboardHasForeignVisgroupState() )
        {
            QMessageBox::warning( this, tr( "Paste" ), tr( "Cannot paste objects with visgroup or hidden metadata into a different map." ) );
            return;
        }
        const auto text = []( const char *s ) { return QCoreApplication::translate( "PasteSpecial", s ); };
        QDialog dialog( this );
        dialog.setWindowTitle( text( "Paste Special" ) );
        QVBoxLayout *layout = new QVBoxLayout( &dialog );
        QSettings settings;
        const PasteSpecialSettings saved = LoadPasteSpecialSettings( settings );
        QFormLayout *form = new QFormLayout;
        QSpinBox *copies = new QSpinBox( &dialog );
        copies->setRange( 1, 256 );
        copies->setValue( saved.copies );
        form->addRow( text( "Copies:" ), copies );
        QSpinBox *offsets[3];
        const char *labels[] = { "X offset per copy:", "Y offset per copy:", "Z offset per copy:" };
        double clipboardSize[3] = {};
        const bool hasClipboardSize = m_pDocument->ClipboardSize( clipboardSize );
        for ( int axis = 0; axis < 3; ++axis )
        {
            offsets[axis] = new QSpinBox( &dialog );
            offsets[axis]->setRange( -32768, 32768 );
            offsets[axis]->setValue( saved.offsets[axis] );
            QWidget *row = new QWidget( &dialog );
            QHBoxLayout *rowLayout = new QHBoxLayout( row );
            rowLayout->setContentsMargins( 0, 0, 0, 0 );
            rowLayout->addWidget( offsets[axis] );
            QPushButton *getOffset = new QPushButton( text( "Get Offset" ), row );
            getOffset->setToolTip( text( "Use copied object bounds; click again to reverse the offset. Point entities use their origins." ) );
            getOffset->setEnabled( hasClipboardSize && clipboardSize[axis] <= 32768 );
            rowLayout->addWidget( getOffset );
            connect( getOffset, &QPushButton::clicked, &dialog, [spin = offsets[axis], extent = clipboardSize[axis]] {
                const int size = static_cast<int>( extent );
                spin->setValue( spin->value() == size ? -size : size );
            } );
            form->addRow( text( labels[axis] ), row );
        }
        layout->addLayout( form );
        QCheckBox *centerViews = new QCheckBox( text( "Center first copy on 2D views" ), &dialog );
        centerViews->setChecked( saved.centerViews );
        centerViews->setToolTip( text( "Use the top view for X/Y and the front view for Z; the remaining copies use the offsets above." ) );
        layout->addWidget( centerViews );
        layout->addWidget( new QLabel( text( "Entity names and connections are retained." ), &dialog ) );
        QDialogButtonBox *buttons = new QDialogButtonBox( QDialogButtonBox::Ok | QDialogButtonBox::Cancel, &dialog );
        connect( buttons, &QDialogButtonBox::accepted, &dialog, &QDialog::accept );
        connect( buttons, &QDialogButtonBox::rejected, &dialog, &QDialog::reject );
        layout->addWidget( buttons );
        if ( dialog.exec() != QDialog::Accepted ) return;
        PasteSpecialSettings chosen;
        chosen.copies = copies->value();
        for ( int axis = 0; axis < 3; ++axis ) chosen.offsets[axis] = offsets[axis]->value();
        chosen.centerViews = centerViews->isChecked();
        SavePasteSpecialSettings( settings, chosen );
        const double step[3] = { double( offsets[0]->value() ), double( offsets[1]->value() ), double( offsets[2]->value() ) };
        double firstOffset[3] = {};
        const double *first = nullptr;
        if ( centerViews->isChecked() )
        {
            double sourceCenter[3] = {};
            if ( !m_pDocument->ClipboardCenter( sourceCenter ) )
            {
                QMessageBox::warning( this, text( "Paste Special" ), text( "Cannot paste: the offsets or copied coordinates are invalid." ) );
                return;
            }
            double top[3], front[3];
            m_pViewportTop->centerCoordinates( top );
            m_pViewportFront->centerCoordinates( front );
            const double target[3] = { top[0], top[1], front[2] };
            for ( int axis = 0; axis < 3; ++axis )
                firstOffset[axis] = m_pDocument->SnapEnabled()
                    ? SnapCoord( target[axis], m_pDocument->GridSize() ) - SnapCoord( sourceCenter[axis], m_pDocument->GridSize() )
                    : target[axis] - sourceCenter[axis];
            first = firstOffset;
        }
        if ( m_pDocument->PasteSpecial( copies->value(), step, first ) < 0 )
            QMessageBox::warning( this, text( "Paste Special" ), text( "Cannot paste: the offsets or copied coordinates are invalid." ) );
    } );
    m_pMenuEdit->addAction( pasteSpecial );

    QAction *actionDelete = new QAction( tr( "&Delete" ), this );
    actionDelete->setShortcut( QKeySequence( "Del" ) );
    connect( actionDelete, &QAction::triggered, this, &CMainWindow::OnDelete );
    m_pMenuEdit->addAction( actionDelete );

    m_pMenuEdit->addSeparator();

    m_pMenuSelection = m_pMenuEdit->addMenu( tr( "&Selection" ) );
    m_pMenuSelection->setObjectName( "SelectionMenu" );

    QAction *actionSelectAll = new QAction( tr( "Select &All" ), this );
    actionSelectAll->setShortcut( QKeySequence( "Ctrl+A" ) );
    connect( actionSelectAll, &QAction::triggered, this, &CMainWindow::OnSelectAll );
    m_pMenuSelection->addAction( actionSelectAll );

    QAction *actionSelectNone = new QAction( tr( "Select &None" ), this );
    actionSelectNone->setShortcut( QKeySequence( "Ctrl+Shift+A" ) );
    connect( actionSelectNone, &QAction::triggered, this, &CMainWindow::OnSelectNone );
    m_pMenuSelection->addAction( actionSelectNone );

    QAction *actionSelectInvert = new QAction( tr( "&Invert Selection" ), this );
    actionSelectInvert->setShortcut( QKeySequence( "Ctrl+I" ) );
    connect( actionSelectInvert, &QAction::triggered, this, &CMainWindow::OnSelectInvert );
    m_pMenuSelection->addAction( actionSelectInvert );

    QAction *actionSelectByProperty = new QAction( tr( "Select by Property..." ), this );
    connect( actionSelectByProperty, &QAction::triggered, this, &CMainWindow::OnSelectByProperty );
    m_pMenuSelection->addAction( actionSelectByProperty );

    //--- View -----------------------------------------------------------------
    m_pMenuView = menuBar()->addMenu( tr( "&View" ) );

    QAction *centerSelection = new QAction( tr( "Center on Selection" ), this );
    centerSelection->setShortcut( QKeySequence( "Ctrl+E" ) );
    connect( centerSelection, &QAction::triggered, this, [this] {
        double center[3];
        if ( !SelectionCenter( *m_pDocument, center ) ) return;
        m_pViewportTop->centerOn( center ); m_pViewportFront->centerOn( center );
        m_pViewportSide->centerOn( center ); m_pViewport3D->centerOn( center );
    } );
    m_pMenuView->addAction( centerSelection );

    QAction *center2dSelection = m_pMenuView->addAction( tr( "Center 2D Views on Selection" ) );
    center2dSelection->setObjectName( "center2dSelection" );
    connect( center2dSelection, &QAction::triggered, this, [this] {
        double center[3];
        if ( !SelectionCenter( *m_pDocument, center ) ) return;
        m_pViewportTop->centerOn( center ); m_pViewportFront->centerOn( center );
        m_pViewportSide->centerOn( center );
    } );
    QAction *center2dCamera = m_pMenuView->addAction( tr( "Center 2D Views on Camera" ) );
    center2dCamera->setObjectName( "center2dCamera" );
    connect( center2dCamera, &QAction::triggered, this, [this] {
        double eye[3], forward[3], up[3];
        m_pViewport3D->cameraBasis( eye, forward, up );
        m_pViewportTop->centerOn( eye ); m_pViewportFront->centerOn( eye );
        m_pViewportSide->centerOn( eye );
    } );

    QAction *followCamera = m_pMenuView->addAction( tr( "Keep 2D Views Centered on Camera" ) );
    followCamera->setObjectName( "followCamera2d" );
    followCamera->setCheckable( true );
    followCamera->setChecked( QSettings().value( QStringLiteral( "view2d/centerOnCamera" ), false ).toBool() );
    connect( m_pViewport3D, &CQtViewport3D::cameraPositionChanged, this, [followCamera, center2dCamera] {
        if ( followCamera->isChecked() ) center2dCamera->trigger();
    } );
    connect( followCamera, &QAction::toggled, this, [center2dCamera]( bool enabled ) {
        QSettings().setValue( QStringLiteral( "view2d/centerOnCamera" ), enabled );
        if ( enabled ) center2dCamera->trigger();
    } );

    QAction *center3dSelection = new QAction( tr( "Center 3D View on Selection" ), this );
    center3dSelection->setObjectName( "center3dSelection" );
    center3dSelection->setShortcut( QKeySequence( "Ctrl+Shift+E" ) );
    connect( center3dSelection, &QAction::triggered, this, [this] {
        double center[3];
        if ( !SelectionCenter( *m_pDocument, center ) ) return;
        // CMapDoc::Center3DViewsOn places the eye 100 units along -Y and
        // points it at the selection, without moving the 2D views.
        const double eye[3] = { center[0], center[1] - 100.0, center[2] };
        const double angles[3] = { 0.0, 90.0, 0.0 };
        m_pViewport3D->setCameraPositionAngles( eye, angles );
    } );
    m_pMenuView->addAction( center3dSelection );

    QAction *goCoordinates = m_pMenuView->addAction( tr( "Go to Coordinates..." ) );
    connect( goCoordinates, &QAction::triggered, this, [this] {
        QString input;
        while ( true )
        {
            bool accepted = false;
            input = QInputDialog::getText( this, tr( "Go to Coordinates" ),
                tr( "Enter X Y Z or setpos X Y Z; setang P Y R:" ), QLineEdit::Normal, input, &accepted );
            if ( !accepted ) return;
            double point[3];
            if ( ParseNavigationCoordinates( input, point ) )
            {
                m_pViewportTop->centerOn( point ); m_pViewportFront->centerOn( point );
                m_pViewportSide->centerOn( point ); m_pViewport3D->centerOn( point );
                return;
            }
            double angles[3];
            if ( ParseNavigationCommand( input, point, angles ) )
            {
                // Match original Hammer: console setpos refers to player origin,
                // while the 3D view is positioned at HL2 standing eye height.
                point[2] += 64.0;
                m_pViewportTop->centerOn( point ); m_pViewportFront->centerOn( point );
                m_pViewportSide->centerOn( point ); m_pViewport3D->setCameraPositionAngles( point, angles );
                return;
            }
            QMessageBox::warning( this, tr( "Go to Coordinates" ), tr( "Enter three finite coordinates, or setpos X Y Z; setang P Y R." ) );
        }
    } );
    QAction *goBrush = m_pMenuView->addAction( tr( "Go to Brush ID..." ) );
    goBrush->setShortcut( QKeySequence( "Ctrl+Shift+G" ) );
    connect( goBrush, &QAction::triggered, this, [this] {
        bool accepted = false;
        const int id = QInputDialog::getInt( this, tr( "Go to Brush ID" ), tr( "Brush ID:" ),
            1, 1, 2147483647, 1, &accepted );
        if ( !accepted ) return;
        if ( !SelectBrushById( *m_pDocument, id ) )
        {
            QMessageBox::information( this, tr( "Go to Brush ID" ), tr( "That brush ID does not exist." ) );
            return;
        }
        double center[3];
        if ( SelectionCenter( *m_pDocument, center ) )
        {
            m_pViewportTop->centerOn( center ); m_pViewportFront->centerOn( center );
            m_pViewportSide->centerOn( center ); m_pViewport3D->centerOn( center );
        }
    } );

    QAction *savedCameras = m_pMenuView->addAction( tr( "Saved Cameras..." ) );
    savedCameras->setObjectName( "savedCamerasAction" );
    connect( savedCameras, &QAction::triggered, this, [this] {
        QDialog dialog( this );
        dialog.setWindowTitle( tr( "Saved Cameras" ) );
        dialog.resize( 460, 300 );
        QVBoxLayout *layout = new QVBoxLayout( &dialog );
        QListWidget *list = new QListWidget( &dialog );
        list->setObjectName( "savedCameraList" );
        layout->addWidget( list );
        QHBoxLayout *actions = new QHBoxLayout;
        QPushButton *go = new QPushButton( tr( "Go to Camera" ), &dialog );
        QPushButton *save = new QPushButton( tr( "Save Current View" ), &dialog );
        QPushButton *remove = new QPushButton( tr( "Delete Camera" ), &dialog );
        go->setObjectName( "savedCameraGo" );
        save->setObjectName( "savedCameraAdd" );
        remove->setObjectName( "savedCameraDelete" );
        actions->addWidget( go ); actions->addWidget( save ); actions->addWidget( remove );
        layout->addLayout( actions );
        QDialogButtonBox *close = new QDialogButtonBox( QDialogButtonBox::Close, &dialog );
        connect( close, &QDialogButtonBox::rejected, &dialog, &QDialog::reject );
        layout->addWidget( close );
        auto refresh = [this, list, go, remove]( int selected ) {
            list->clear();
            const auto &cameras = m_pDocument->Map().cameras;
            for ( int i = 0; i < cameras.size(); ++i )
            {
                double eye[3], angles[3];
                const QString label = ReadSavedCamera( cameras[i], eye, angles )
                    ? tr( "Camera %1: %2" ).arg( i + 1 ).arg( FormatVec3( eye ) )
                    : tr( "Camera %1: invalid position or target" ).arg( i + 1 );
                list->addItem( label );
            }
            if ( selected >= 0 && selected < cameras.size() ) list->setCurrentRow( selected );
            go->setEnabled( list->currentRow() >= 0 );
            remove->setEnabled( list->currentRow() >= 0 );
        };
        connect( list, &QListWidget::currentRowChanged, &dialog, [go, remove]( int row ) {
            go->setEnabled( row >= 0 ); remove->setEnabled( row >= 0 );
        } );
        connect( go, &QPushButton::clicked, &dialog, [this, list, &dialog] {
            const int index = list->currentRow();
            if ( index < 0 || index >= m_pDocument->Map().cameras.size() ) return;
            double eye[3], angles[3];
            if ( !ReadSavedCamera( m_pDocument->Map().cameras[index], eye, angles ) )
            {
                QMessageBox::warning( &dialog, tr( "Saved Cameras" ), tr( "This camera has an invalid position or target." ) );
                return;
            }
            m_pDocument->SetSavedCameras( m_pDocument->Map().cameras, index );
            m_pViewport3D->setCameraPositionAngles( eye, angles );
        } );
        connect( save, &QPushButton::clicked, &dialog, [this, refresh] {
            double eye[3], forward[3], up[3];
            m_pViewport3D->cameraBasis( eye, forward, up );
            QList<CameraLite> cameras = m_pDocument->Map().cameras;
            cameras.append( MakeSavedCamera( eye, forward ) );
            m_pDocument->SetSavedCameras( cameras, cameras.size() - 1 );
            refresh( cameras.size() - 1 );
        } );
        connect( remove, &QPushButton::clicked, &dialog, [this, list, refresh] {
            int index = list->currentRow();
            QList<CameraLite> cameras = m_pDocument->Map().cameras;
            if ( index < 0 || index >= cameras.size() ) return;
            int active = SavedCameraActiveIndex( m_pDocument->Map() );
            cameras.removeAt( index );
            if ( active > index ) --active;
            else if ( active == index ) active = cameras.isEmpty() ? -1 : qMin( index, int( cameras.size() ) - 1 );
            index = cameras.isEmpty() ? -1 : qMin( index, int( cameras.size() ) - 1 );
            m_pDocument->SetSavedCameras( cameras, active );
            refresh( index );
        } );
        const int active = SavedCameraActiveIndex( m_pDocument->Map() );
        refresh( active >= 0 ? active : m_pDocument->Map().cameras.isEmpty() ? -1 : 0 );
        dialog.exec();
    } );

    QAction *actionMaximize = new QAction( tr( "&Maximize Viewport" ), this );
    actionMaximize->setShortcut( QKeySequence( "Shift+Z" ) );
    connect( actionMaximize, &QAction::triggered, this, &CMainWindow::OnMaximizeViewport );
    m_pMenuView->addAction( actionMaximize );

    QAction *nudgeSelection = m_pMenuView->addAction( tr( "Nudge Selection with Arrow Keys" ) );
    nudgeSelection->setCheckable( true );
    const bool nudgeEnabled = QSettings().value( QStringLiteral( "view2d/nudge" ), false ).toBool();
    nudgeSelection->setChecked( nudgeEnabled );
    for ( CQtViewport2D *view : { m_pViewportTop, m_pViewportFront, m_pViewportSide } )
        view->setNudgeEnabled( nudgeEnabled );
    connect( nudgeSelection, &QAction::toggled, this, [this]( bool enabled ) {
        for ( CQtViewport2D *view : { m_pViewportTop, m_pViewportFront, m_pViewportSide } )
            view->setNudgeEnabled( enabled );
        QSettings().setValue( QStringLiteral( "view2d/nudge" ), enabled );
    } );

    m_pMenuView->addSeparator();

    // Grid size: exclusive radio actions (1/2/4/.../512 world units)
    QMenu *pGridMenu = m_pMenuView->addMenu( tr( "Grid &Size" ) );
    QActionGroup *pGridGroup = new QActionGroup( this );
    pGridGroup->setExclusive( true );
    for ( int i = 0; i < kGridSizeCount; ++i )
    {
        QAction *pGridAction = new QAction( QStringLiteral( "%1" ).arg( kGridSizes[i] ), pGridGroup );
        pGridAction->setCheckable( true );
        pGridAction->setData( kGridSizes[i] );
        if ( kGridSizes[i] == kDefaultGridSize )
            pGridAction->setChecked( true );
        pGridGroup->addAction( pGridAction );
        pGridMenu->addAction( pGridAction );
    }
    connect( pGridGroup, &QActionGroup::triggered, this, &CMainWindow::OnGridSizeChanged );

    QAction *actionFilter = new QAction( tr( "&Filter..." ), this );
    connect( actionFilter, &QAction::triggered, this, &CMainWindow::OnFilter );
    m_pMenuView->addAction( actionFilter );
    QAction *hideSelected = m_pMenuView->addAction( tr( "Quick Hide Selected" ) );
    hideSelected->setObjectName( "quickHideSelected" );
    connect( hideSelected, &QAction::triggered, this, [this] {
        ViewFilter filter = m_pDocument->GetViewFilter();
        filter.quickHiddenIds.unite( m_pDocument->SelectedIds() );
        m_pDocument->SetViewFilter( filter );
    } );
    QAction *hideUnselected = m_pMenuView->addAction( tr( "Quick Hide Unselected" ) );
    hideUnselected->setObjectName( "quickHideUnselected" );
    connect( hideUnselected, &QAction::triggered, this, [this] {
        if ( m_pDocument->SelectedIds().isEmpty() ) return;
        ViewFilter filter = m_pDocument->GetViewFilter();
        for ( const auto &solid : m_pDocument->Map().solids )
	        if ( filter.SolidVisible( solid ) && !m_pDocument->IsSelected( solid.ownerEntityId ? solid.ownerEntityId : solid.id ) ) filter.quickHiddenIds.insert( solid.id );
        for ( const auto &entity : m_pDocument->Map().entities )
            if ( filter.EntityVisible( entity ) && !m_pDocument->IsSelected( entity.id ) ) filter.quickHiddenIds.insert( entity.id );
        m_pDocument->SetViewFilter( filter );
    } );
    QAction *unhide = m_pMenuView->addAction( tr( "Quick Unhide All" ) );
    unhide->setObjectName( "quickUnhideAll" );
    connect( unhide, &QAction::triggered, this, [this] {
        ViewFilter filter = m_pDocument->GetViewFilter();
        filter.quickHiddenIds.clear();
        m_pDocument->SetViewFilter( filter );
    } );
    QAction *showVisgroupHidden = m_pMenuView->addAction( tr( "Show Visgroup-Hidden Objects" ) );
    showVisgroupHidden->setObjectName( "showVisgroupHidden" );
    showVisgroupHidden->setCheckable( true );
    showVisgroupHidden->setToolTip( tr( "Temporarily show objects hidden by saved visgroup visibility; the map is unchanged." ) );
    connect( showVisgroupHidden, &QAction::toggled, this, [this]( bool show ) {
        ViewFilter filter = m_pDocument->GetViewFilter(); filter.showVisgroupHidden = show;
        m_pDocument->SetViewFilter( filter );
    } );
    connect( m_pDocument, &CHammerDocument::stateChanged, showVisgroupHidden, [this, showVisgroupHidden] {
        const QSignalBlocker blocker( showVisgroupHidden );
        showVisgroupHidden->setChecked( m_pDocument->GetViewFilter().showVisgroupHidden );
    } );
    QAction *visgroups = m_pMenuView->addAction( tr( "Manage Visgroups..." ) );
    visgroups->setObjectName( "manageVisgroups" );
    connect( visgroups, &QAction::triggered, this, [this] { CVisgroupDialog dialog( m_pDocument, this ); dialog.exec(); } );

    m_pMenuView->addSeparator();

    // Dock visibility toggles stay switchable from the View menu
    m_pMenuView->addAction( m_pTextureBrowserDock->toggleViewAction() );
    m_pMenuView->addAction( m_pObjectPropertiesDock->toggleViewAction() );
    m_pMenuView->addAction( m_pOutputDock->toggleViewAction() );

    m_pMenuView->addSeparator();

    QAction *actionRestoreLayout = new QAction( tr( "Restore Default Layout" ), this );
    connect( actionRestoreLayout, &QAction::triggered, this, &CMainWindow::OnRestoreDefaultLayout );
    m_pMenuView->addAction( actionRestoreLayout );

    //--- Map ------------------------------------------------------------------
    m_pMenuMap = menuBar()->addMenu( tr( "&Map" ) );

    QAction *actionSnapToGrid = new QAction( tr( "&Snap to Grid" ), this );
    actionSnapToGrid->setCheckable( true );
    actionSnapToGrid->setChecked( true );
    actionSnapToGrid->setShortcut( QKeySequence( "Shift+W" ) );
    connect( actionSnapToGrid, &QAction::toggled, this, &CMainWindow::OnSnapToGrid );
    m_pMenuMap->addAction( actionSnapToGrid );

    QAction *showGrid = new QAction( tr( "Show &Grid" ), this );
    showGrid->setCheckable( true );
    showGrid->setChecked( true );
    showGrid->setShortcut( QKeySequence( "Shift+R" ) );
    connect( showGrid, &QAction::toggled, m_pDocument, &CHammerDocument::SetGridVisible );
    connect( m_pDocument, &CHammerDocument::stateChanged, this, [this, showGrid, actionSnapToGrid, pGridGroup] {
        const QSignalBlocker snapBlocker( actionSnapToGrid );
        const QSignalBlocker showBlocker( showGrid );
        actionSnapToGrid->setChecked( m_pDocument->SnapEnabled() );
        showGrid->setChecked( m_pDocument->GridVisible() );
        for ( QAction *action : pGridGroup->actions() )
            if ( action->data().toInt() == m_pDocument->GridSize() ) { action->setChecked( true ); break; }
        m_pViewportTop->setGridVisible( m_pDocument->GridVisible() );
        m_pViewportFront->setGridVisible( m_pDocument->GridVisible() );
        m_pViewportSide->setGridVisible( m_pDocument->GridVisible() );
        if ( m_pStatusGrid ) m_pStatusGrid->setText( tr( "Grid: %1%2" ).arg( m_pDocument->GridSize() )
            .arg( m_pDocument->SnapEnabled() ? tr( " (snap)" ) : QString() ) );
    } );
    m_pMenuMap->addAction( showGrid );
    QMenu *gridSettings = m_pMenuMap->addMenu( tr( "Grid &Settings" ) );
    QAction *gridDialogAction = gridSettings->addAction( tr( "Grid Settings Dialog..." ) );
    connect( gridDialogAction, &QAction::triggered, this, &CMainWindow::OnGridSettings );
    gridSettings->addSeparator();
    QAction *smallerGrid = gridSettings->addAction( tr( "Smaller Grid" ) );
    smallerGrid->setShortcuts( QList<QKeySequence>() << QKeySequence( "[" ) << QKeySequence( "Alt+A" ) );
    QAction *largerGrid = gridSettings->addAction( tr( "Larger Grid" ) );
    largerGrid->setShortcuts( QList<QKeySequence>() << QKeySequence( "]" ) << QKeySequence( "Alt+S" ) );
    auto changeGrid = [this, pGridGroup]( bool larger ) {
        const int oldSize = m_pDocument->GridSize();
        const int newSize = larger ? qMin( 512, oldSize * 2 ) : qMax( 1, oldSize / 2 );
        for ( QAction *action : pGridGroup->actions() )
            if ( action->data().toInt() == newSize ) { action->trigger(); break; }
    };
    connect( smallerGrid, &QAction::triggered, this, [changeGrid] { changeGrid( false ); } );
    connect( largerGrid, &QAction::triggered, this, [changeGrid] { changeGrid( true ); } );

    m_pMenuMap->addSeparator();

    // Entity Report appears both here and under Tools
    QAction *actionEntityReport = new QAction( tr( "&Entity Report..." ), this );
    connect( actionEntityReport, &QAction::triggered, this, &CMainWindow::OnEntityReport );
    m_pMenuMap->addAction( actionEntityReport );

    QAction *actionMapProperties = new QAction( tr( "Map &Properties..." ), this );
    connect( actionMapProperties, &QAction::triggered, this, &CMainWindow::OnMapProperties );
    m_pMenuMap->addAction( actionMapProperties );

    QAction *cordonEdit = m_pMenuMap->addAction( tr( "Edit Cordon Bounds..." ) );
    QAction *cordonToggle = m_pMenuMap->addAction( tr( "Toggle Cordon" ) );
    cordonToggle->setCheckable( true );
    connect( m_pMenuMap, &QMenu::aboutToShow, this, [this, cordonEdit, cordonToggle] {
        const MapDocLite &map = m_pDocument->Map();
        const bool supported = !HammerCordon::HasOtherCordonFormat( map );
        cordonEdit->setEnabled( supported );
        cordonToggle->setEnabled( supported );
        cordonToggle->setChecked( map.IsCordonActive() );
    } );
    connect( cordonToggle, &QAction::triggered, this, [this, cordonToggle]( bool active ) {
        HammerCordon::Bounds bounds;
        if ( HammerCordon::ReadBounds( m_pDocument->Map(), bounds ) &&
             m_pDocument->SetCordon( bounds.mins, bounds.maxs, active ) ) return;
        cordonToggle->setChecked( m_pDocument->Map().IsCordonActive() );
        QMessageBox::warning( this, tr( "Cordon" ), tr( "Edit the invalid cordon bounds first." ) );
    } );
    connect( cordonEdit, &QAction::triggered, this, [this] {
        HammerCordon::Bounds bounds;
        if ( !HammerCordon::ReadBounds( m_pDocument->Map(), bounds ) ) bounds = HammerCordon::Bounds();
        QDialog dialog( this );
        dialog.setWindowTitle( tr( "Edit Cordon Bounds" ) );
        QFormLayout *form = new QFormLayout( &dialog );
        QLineEdit *mins = new QLineEdit( FormatVec3( bounds.mins ), &dialog );
        QLineEdit *maxs = new QLineEdit( FormatVec3( bounds.maxs ), &dialog );
        QCheckBox *active = new QCheckBox( tr( "Active" ), &dialog );
        active->setChecked( m_pDocument->Map().IsCordonActive() );
        form->addRow( tr( "Minimum (X Y Z):" ), mins );
        form->addRow( tr( "Maximum (X Y Z):" ), maxs );
        form->addRow( active );
        auto *buttons = new QDialogButtonBox( QDialogButtonBox::Ok | QDialogButtonBox::Cancel, &dialog );
        connect( buttons, &QDialogButtonBox::rejected, &dialog, &QDialog::reject );
        connect( buttons, &QDialogButtonBox::accepted, &dialog, [this, &dialog, mins, maxs, active] {
            HammerCordon::Bounds edited;
            if ( ParseVec3( mins->text(), edited.mins ) && ParseVec3( maxs->text(), edited.maxs ) &&
                 m_pDocument->SetCordon( edited.mins, edited.maxs, active->isChecked() ) ) dialog.accept();
            else QMessageBox::warning( &dialog, tr( "Cordon" ), tr( "Enter valid minimum and maximum coordinates within the map limits." ) );
        } );
        form->addRow( buttons );
        dialog.exec();
    } );

    QAction *inspectDisplacement = m_pMenuMap->addAction( tr( "Inspect Displacements in VMF..." ) );
    inspectDisplacement->setObjectName( "inspectDisplacementsAction" );
    connect( inspectDisplacement, &QAction::triggered, this, [this] {
        const QString path = QFileDialog::getOpenFileName( this, tr( "Inspect Displacements in VMF" ), QString(), tr( "Valve Map Files (*.vmf);;All Files (*)" ) );
        if ( path.isEmpty() ) return;
        VmfIO::DisplacementReport report;
        QString error;
        if ( !VmfIO::InspectDisplacements( path, report, &error ) )
        {
            QMessageBox::warning( this, tr( "Displacement Inspection" ), tr( "Could not inspect the file: %1" ).arg( error ) );
            return;
        }
        QString message = report.count == 0 ? tr( "No displacement blocks found." ) :
            tr( "%1 displacement block(s); %2 non-canonical or unsupported." ).arg( report.count ).arg( report.nonCanonical );
        message += QStringLiteral( "\n\n" ) + tr( "This is a read-only structural check, not map loading, rendering, editing, or a guarantee of compatibility." );
        if ( !report.details.isEmpty() ) message += QStringLiteral( "\n\n" ) + report.details.join( QLatin1Char( '\n' ) );
        if ( report.count > report.details.size() ) message += QStringLiteral( "\n" ) + tr( "Further entries omitted." );
        QMessageBox::information( this, tr( "Displacement Inspection" ), message );
    } );

    QAction *checkProblems = m_pMenuMap->addAction( tr( "Check for Problems..." ) );
    checkProblems->setShortcut( QKeySequence( "Alt+P" ) );
    connect( checkProblems, &QAction::triggered, this, [this] {
        QList<MapCheckIssue> issues;
        QDialog dialog( this );
        dialog.setWindowTitle( tr( "Check for Problems" ) );
        dialog.resize( 600, 350 );
        QVBoxLayout *layout = new QVBoxLayout( &dialog );
        layout->addWidget( new QLabel( tr( "Basic checks only; this does not check for leaks or validate all geometry." ), &dialog ) );
        QCheckBox *visibleOnly = new QCheckBox( tr( "Check visible objects only" ), &dialog );
        visibleOnly->setObjectName( "mapCheckVisibleOnly" );
        visibleOnly->setChecked( QSettings().value( QStringLiteral( "mapCheck/visibleOnly" ), false ).toBool() );
        layout->addWidget( visibleOnly );
        QListWidget *list = new QListWidget( &dialog );
        const auto refresh = [&] {
        issues = CheckMapProblems( m_pDocument->Map(), visibleOnly->isChecked() ? &m_pDocument->GetViewFilter() : nullptr );
        list->clear();
        for ( const MapCheckIssue &issue : issues )
        {
            QString description;
            if ( issue.kind == MapCheckKind::NoPlayerStart )
                description = tr( "No info_player_start found (other games may use a different spawn entity)." );
            else if ( issue.kind == MapCheckKind::InvalidTextureAxes )
                description = tr( "Brush %1, face %2: invalid texture axes or scale." ).arg( issue.solidId ).arg( issue.faceId );
            else if ( issue.kind == MapCheckKind::DuplicateFaceId )
                description = tr( "Brush %1, face %2: duplicate face ID." ).arg( issue.solidId ).arg( issue.faceId );
            else if ( issue.kind == MapCheckKind::DuplicateNodeId )
                description = tr( "Entity %1: duplicate node ID %2." ).arg( issue.solidId ).arg( issue.faceId );
            else if ( issue.kind == MapCheckKind::DuplicatePlaneNormals )
                description = tr( "Brush %1: faces have duplicate plane normals (check manually)." ).arg( issue.solidId );
            else if ( issue.kind == MapCheckKind::UnreconstructableSolid )
                description = tr( "Brush %1: could not reconstruct a closed convex shape (check manually)." ).arg( issue.solidId );
            else if ( issue.kind == MapCheckKind::OutputEmptyTarget )
                description = tr( "Entity %1, output %2: target is empty (check manually)." ).arg( issue.solidId ).arg( issue.faceId );
            else if ( issue.kind == MapCheckKind::OutputMissingTarget )
                description = tr( "Entity %1, output %2: target not found (check manually)." ).arg( issue.solidId ).arg( issue.faceId );
            else if ( issue.kind == MapCheckKind::OutputInvalidDelay )
                description = tr( "Entity %1, output %2: delay is not a finite number (check manually)." ).arg( issue.solidId ).arg( issue.faceId );
            else
                description = tr( "Entity %1, output %2: fire count is not an integer (check manually)." ).arg( issue.solidId ).arg( issue.faceId );
            QListWidgetItem *item = new QListWidgetItem( description, list );
            item->setData( Qt::UserRole, MapCheckIssueObjectId( issue ) );
        }
        if ( issues.isEmpty() )
        {
            QListWidgetItem *item = new QListWidgetItem( tr( "No problems found by these checks." ), list );
            item->setData( Qt::UserRole, -1 );
        }
        };
        refresh();
        connect( visibleOnly, &QCheckBox::toggled, &dialog, [&, refresh]( bool checked ) {
            QSettings().setValue( QStringLiteral( "mapCheck/visibleOnly" ), checked );
            refresh();
        } );
        layout->addWidget( list );
        QDialogButtonBox *buttons = new QDialogButtonBox( QDialogButtonBox::Close, &dialog );
        QPushButton *goTo = buttons->addButton( tr( "Go to Problem" ), QDialogButtonBox::ActionRole );
        goTo->setEnabled( false );
        connect( list, &QListWidget::currentItemChanged, &dialog, [goTo]( QListWidgetItem *item ) {
            goTo->setEnabled( item && item->data( Qt::UserRole ).toInt() >= 0 );
        } );
        // Select and center without closing, so several problems can be visited.
        const auto goToCurrent = [this, list, &issues] {
            QListWidgetItem *current = list->currentItem();
            if ( !current ) return;
            const int row = list->row( current );
            if ( row < 0 || row >= issues.size() ) return;
            double center[3];
            if ( !MapCheckGoToIssue( *m_pDocument, issues[row], center ) ) return;
            m_pViewportTop->centerOn( center ); m_pViewportFront->centerOn( center );
            m_pViewportSide->centerOn( center ); m_pViewport3D->centerOn( center );
        };
        connect( goTo, &QPushButton::clicked, &dialog, goToCurrent );
        connect( list, &QListWidget::itemDoubleClicked, &dialog, [goTo, goToCurrent] {
            if ( goTo->isEnabled() ) goToCurrent();
        } );
        connect( buttons, &QDialogButtonBox::rejected, &dialog, &QDialog::reject );
        layout->addWidget( buttons );
        dialog.exec();
    } );

    QAction *loadPointFile = m_pMenuMap->addAction( tr( "Load Pointfile..." ) );
    connect( loadPointFile, &QAction::triggered, this, [this] {
        QString suggested = m_pDocument->GetFileName();
        if ( !suggested.isEmpty() )
            suggested = QFileInfo( suggested ).absolutePath() + QLatin1Char( '/' ) + QFileInfo( suggested ).completeBaseName() + QStringLiteral( ".lin" );
        const QString path = QFileDialog::getOpenFileName( this, tr( "Load Pointfile" ), suggested,
            tr( "Pointfiles (*.lin *.pts);;All files (*)" ) );
        if ( path.isEmpty() ) return;
        int line = 0;
        if ( !m_pDocument->LoadPointFile( path, &line ) )
            QMessageBox::warning( this, tr( "Load Pointfile" ), line ?
                tr( "Invalid pointfile data at line %1." ).arg( line ) : tr( "Could not read the pointfile." ) );
    } );
    QAction *unloadPointFile = m_pMenuMap->addAction( tr( "Unload Pointfile" ) );
    connect( unloadPointFile, &QAction::triggered, this, [this] { m_pDocument->UnloadPointFile(); } );
    auto stepPoint = [this]( int direction ) {
        PointFilePoint point;
        if ( !m_pDocument->StepPointFile( direction, point ) ) return;
        const double center[3] = { point.x, point.y, point.z };
        m_pViewportTop->centerOn( center ); m_pViewportFront->centerOn( center );
        m_pViewportSide->centerOn( center ); m_pViewport3D->centerOn( center );
    };
    QAction *previousPoint = m_pMenuMap->addAction( tr( "Previous Leak Point" ) );
    QAction *nextPoint = m_pMenuMap->addAction( tr( "Next Leak Point" ) );
    connect( previousPoint, &QAction::triggered, this, [stepPoint] { stepPoint( -1 ); } );
    connect( nextPoint, &QAction::triggered, this, [stepPoint] { stepPoint( 1 ); } );
    // Limit Home/End to viewports so text fields retain normal cursor movement.
    for ( QWidget *view : { static_cast<QWidget *>( m_pViewportTop ), static_cast<QWidget *>( m_pViewportFront ),
                           static_cast<QWidget *>( m_pViewportSide ), static_cast<QWidget *>( m_pViewport3D ) } )
    {
        QShortcut *home = new QShortcut( QKeySequence( Qt::Key_Home ), view );
        QShortcut *end = new QShortcut( QKeySequence( Qt::Key_End ), view );
        connect( home, &QShortcut::activated, this, [stepPoint] { stepPoint( -1 ); } );
        connect( end, &QShortcut::activated, this, [stepPoint] { stepPoint( 1 ); } );
    }

    //--- Tools ----------------------------------------------------------------
    m_pMenuTools = menuBar()->addMenu( tr( "T&ools" ) );
    m_pMenuTools->setObjectName( "ToolsMenu" );

    const auto prefabDirectory = [] {
        return QSettings().value( QStringLiteral( "prefabs/directory" ),
            QDir( QCoreApplication::applicationDirPath() ).filePath( QStringLiteral( "prefabs" ) ) ).toString();
    };
    QAction *createPrefab = m_pMenuTools->addAction( tr( "Create Prefab..." ) );
    createPrefab->setShortcut( QKeySequence( "Ctrl+R" ) );
    connect( createPrefab, &QAction::triggered, this, [this, prefabDirectory] {
        if ( m_pDocument->SelectedIds().isEmpty() )
        {
            QMessageBox::information( this, tr( "Create Prefab" ), tr( "Select objects to create a prefab." ) );
            return;
        }
        const QString directory = prefabDirectory();
        QString path = QFileDialog::getSaveFileName( this, tr( "Create Prefab" ),
            QDir( QFileInfo( directory ).isDir() ? directory : QDir::homePath() ).filePath( QStringLiteral( "prefab.vmf" ) ),
            tr( "Valve Map Files (*.vmf)" ) );
        if ( path.isEmpty() ) return;
        if ( !path.endsWith( QLatin1String( ".vmf" ), Qt::CaseInsensitive ) )
        {
            path += QLatin1String( ".vmf" );
            if ( QFileInfo::exists( path ) &&
                 QMessageBox::question( this, tr( "Create Prefab" ), tr( "Replace the existing prefab?" ) ) != QMessageBox::Yes ) return;
        }
        QString error;
        if ( !m_pDocument->SaveSelectionAsPrefab( path, &error ) )
            QMessageBox::warning( this, tr( "Create Prefab" ), error );
        else QSettings().setValue( QStringLiteral( "prefabs/directory" ), QFileInfo( path ).absolutePath() );
    } );
    QAction *insertPrefab = m_pMenuTools->addAction( tr( "Insert Original Prefab..." ) );
    insertPrefab->setShortcut( QKeySequence( "Alt+Shift+C" ) );
    connect( insertPrefab, &QAction::triggered, this, [this, prefabDirectory] {
        const QString directory = prefabDirectory();
        const QString path = QFileDialog::getOpenFileName( this, tr( "Insert Original Prefab" ),
            QFileInfo( directory ).isDir() ? directory : QDir::homePath(), tr( "Valve Map Files (*.vmf)" ) );
        if ( path.isEmpty() ) return;
        QString error;
        if ( !m_pDocument->InsertOriginalPrefab( path, &error ) )
            QMessageBox::warning( this, tr( "Insert Original Prefab" ), error );
        else QSettings().setValue( QStringLiteral( "prefabs/directory" ), QFileInfo( path ).absolutePath() );
    } );

    QMenu *displacementMenu = m_pMenuTools->addMenu( tr( "Displacements" ) );
    displacementMenu->setStatusTip( tr( "Select faces in the 3D view with the Material tool first." ) );
    const auto displacementFaces = [this] {
        const auto faces = m_pDocument->GetTool() == TOOL_TEXTURE
            ? m_pViewport3D->selectedFaces() : QMap<int, QSet<int>>{};
        if ( faces.isEmpty() )
            QMessageBox::information( this, tr( "Displacements" ), tr( "Select faces in the 3D view with the Material tool first." ) );
        return faces;
    };
    QAction *createDisplacement = displacementMenu->addAction( tr( "Create Displacements..." ) );
    connect( createDisplacement, &QAction::triggered, this, [this, displacementFaces] {
        const auto faces = displacementFaces();
        if ( faces.isEmpty() ) return;
        bool accepted = false;
        const int power = QInputDialog::getInt( this, tr( "Create Displacements" ),
            tr( "Power (2: 5×5, 3: 9×9, 4: 17×17):" ), 3, 2, 4, 1, &accepted );
        if ( accepted && !m_pDocument->CreateDisplacements( faces, power ) )
            QMessageBox::warning( this, tr( "Displacements" ), tr( "Create requires visible, unmodified quadrilateral world faces without a displacement." ) );
    } );
    QAction *removeDisplacement = displacementMenu->addAction( tr( "Remove Displacements" ) );
    connect( removeDisplacement, &QAction::triggered, this, [this, displacementFaces] {
        const auto faces = displacementFaces();
        if ( !faces.isEmpty() && !m_pDocument->RemoveDisplacements( faces ) )
            QMessageBox::warning( this, tr( "Displacements" ), tr( "Every selected face must contain a displacement." ) );
    } );
    QAction *paintGeometry = displacementMenu->addAction( tr( "Paint Geometry..." ) );
    paintGeometry->setObjectName( "paintDisplacementGeometry" );
    connect( paintGeometry, &QAction::triggered, this, [this, displacementFaces] {
        const auto faces = displacementFaces();
        if ( faces.isEmpty() ) return;
        bool hasDisplacement = false;
        for ( auto it = faces.cbegin(); it != faces.cend(); ++it )
            if ( const SolidLite *solid = m_pDocument->Map().SolidById( it.key() ) )
                for ( const SideLite &side : solid->sides )
                    if ( side.displacement && it.value().contains( side.id ) ) hasDisplacement = true;
        if ( !hasDisplacement )
        {
            QMessageBox::information( this, tr( "Displacements" ), tr( "Select a displacement face before painting." ) );
            return;
        }
        if ( QDialog *existing = findChild<QDialog *>( QStringLiteral( "displacementGeometryDialog" ) ) )
        {
            existing->raise();
            existing->activateWindow();
            return;
        }

        auto *dialog = new QDialog( this, Qt::Tool );
        dialog->setAttribute( Qt::WA_DeleteOnClose );
        dialog->setObjectName( "displacementGeometryDialog" );
        dialog->setWindowTitle( tr( "Displacement Paint Geometry" ) );
        auto *layout = new QFormLayout( dialog );
        auto *effect = new QComboBox( dialog );
        effect->setObjectName( "displacementPaintEffect" );
        effect->addItems( { tr( "Raise/Lower" ), tr( "Raise To" ), tr( "Smooth" ) } );
        layout->addRow( tr( "Effect:" ), effect );
        auto *radius = new QDoubleSpinBox( dialog );
        radius->setRange( 1, 4096 );
        radius->setValue( 64 );
        layout->addRow( tr( "Radius:" ), radius );
        auto *distance = new QDoubleSpinBox( dialog );
        distance->setRange( 0, 60 );
        distance->setValue( 8 );
        layout->addRow( tr( "Distance:" ), distance );
        auto *edge = new QComboBox( dialog );
        edge->addItems( { tr( "Soft-Edge" ), tr( "Hard-Edge" ) } );
        layout->addRow( tr( "Brush:" ), edge );
        auto *close = new QDialogButtonBox( QDialogButtonBox::Close, dialog );
        layout->addRow( close );
        connect( close, &QDialogButtonBox::rejected, dialog, &QDialog::reject );
        const auto updatePaint = [this, effect, radius, distance, edge] {
            const int selectedEffect = effect->currentIndex();
            distance->setEnabled( selectedEffect != 2 );
            const double amount = selectedEffect == 2 ? 2.0 : distance->value();
            m_pViewport3D->setDisplacementPaint( true, selectedEffect, radius->value(),
                amount, edge->currentIndex() == 0 );
        };
        connect( effect, &QComboBox::currentIndexChanged, dialog, updatePaint );
        connect( radius, &QDoubleSpinBox::valueChanged, dialog, updatePaint );
        connect( distance, &QDoubleSpinBox::valueChanged, dialog, updatePaint );
        connect( edge, &QComboBox::currentIndexChanged, dialog, updatePaint );
        connect( dialog, &QDialog::finished, m_pViewport3D, [view = m_pViewport3D] { view->setDisplacementPaint( false ); } );
        connect( m_pDocument, &CHammerDocument::stateChanged, dialog, [this, dialog] {
            if ( m_pDocument->GetTool() != TOOL_TEXTURE ) dialog->reject();
        } );
        connect( m_pDocument, &CHammerDocument::fileNameChanged, dialog, &QDialog::reject );
        updatePaint();
        dialog->show();
    } );
    QAction *sewDisplacements = displacementMenu->addAction( tr( "Sew" ) );
    sewDisplacements->setObjectName( "sewDisplacements" );
    connect( sewDisplacements, &QAction::triggered, this, [this, displacementFaces] {
        const auto faces = displacementFaces();
        if ( !faces.isEmpty() && !m_pDocument->SewDisplacements( faces ) )
            QMessageBox::information( this, tr( "Displacements" ), tr( "Select adjacent faces with a compatible displacement seam." ) );
    } );
    QAction *editDisplacement = displacementMenu->addAction( tr( "Edit Displacement Distances..." ) );
    connect( editDisplacement, &QAction::triggered, this, [this, displacementFaces] {
        const auto faces = displacementFaces();
        if ( faces.isEmpty() ) return;
        if ( faces.size() != 1 || faces.first().size() != 1 )
        {
            QMessageBox::information( this, tr( "Displacements" ), tr( "Select exactly one displacement face to edit its distances." ) );
            return;
        }
        const int solidId = faces.firstKey();
        const int sideId = *faces.first().constBegin();
        const SolidLite *solid = m_pDocument->Map().SolidById( solidId );
        int size = 0;
        QList<double> distances;
        if ( solid )
            for ( const SideLite &side : solid->sides )
                if ( side.id == sideId && side.displacement && HammerDisplacementEdit::ReadDistances( *side.displacement, distances ) )
                    size = HammerDisplacementEdit::GridSize( *side.displacement );
        if ( size == 0 )
        {
            QMessageBox::information( this, tr( "Displacements" ), tr( "Select exactly one displacement face to edit its distances." ) );
            return;
        }
        QDialog dialog( this );
        dialog.setWindowTitle( tr( "Edit Displacement Distances" ) );
        QVBoxLayout layout( &dialog );
        QLabel hint( tr( "Distances are in map units along each vertex's displacement normal. Select cells to smooth them." ), &dialog );
        hint.setWordWrap( true );
        layout.addWidget( &hint );
        QTableWidget grid( size, size, &dialog );
        grid.horizontalHeader()->setDefaultSectionSize( 72 );
        for ( int row = 0; row < size; ++row )
            for ( int column = 0; column < size; ++column )
                grid.setItem( row, column, new QTableWidgetItem( QString::number( distances[row * size + column], 'g', 17 ) ) );
        layout.addWidget( &grid );
        const auto readGrid = [&grid, size]( QList<double> &values ) {
            values.clear();
            for ( int row = 0; row < size; ++row )
                for ( int column = 0; column < size; ++column )
                {
                    bool ok = false;
                    const double value = grid.item( row, column )->text().toDouble( &ok );
                    if ( !ok || !std::isfinite( value ) ) return false;
                    values.append( value );
                }
            return true;
        };
        QDialogButtonBox buttons( QDialogButtonBox::Ok | QDialogButtonBox::Cancel, &dialog );
        QPushButton *smooth = buttons.addButton( tr( "Smooth Selected" ), QDialogButtonBox::ActionRole );
        connect( smooth, &QPushButton::clicked, &dialog, [&] {
            QList<double> source;
            if ( !readGrid( source ) ) return;
            for ( const QModelIndex &cell : grid.selectionModel()->selectedIndexes() )
            {
                // Leave the perimeter alone so smoothing cannot open a seam
                // along an edge the author already aligned to a neighbor.
                const int row = cell.row(), column = cell.column();
                if ( row == 0 || row == size - 1 || column == 0 || column == size - 1 ) continue;
                const int center = row * size + column;
                const double average = source[center] * 0.5 + source[center - 1] * 0.125 +
                    source[center + 1] * 0.125 + source[center - size] * 0.125 + source[center + size] * 0.125;
                grid.item( row, column )->setText( QString::number( average, 'g', 17 ) );
            }
        } );
        connect( &buttons, &QDialogButtonBox::rejected, &dialog, &QDialog::reject );
        connect( &buttons, &QDialogButtonBox::accepted, &dialog, [&] {
            if ( readGrid( distances ) && m_pDocument->SetDisplacementDistances( solidId, sideId, distances ) )
                dialog.accept();
            else
                QMessageBox::warning( &dialog, tr( "Displacements" ), tr( "Distances must be valid numbers between -32768 and 32768 for edited vertices." ) );
        } );
        layout.addWidget( &buttons );
        dialog.resize( 720, 440 );
        dialog.exec();
    } );

    QAction *snapSelection = m_pMenuTools->addAction( tr( "Snap Selected to Grid" ) );
    snapSelection->setShortcut( QKeySequence( "Ctrl+B" ) );
    connect( snapSelection, &QAction::triggered, this, [this] { m_pDocument->SnapSelectionToGrid( false ); } );
    QAction *tieEntity = m_pMenuTools->addAction( tr( "Tie to Entity..." ) );
    tieEntity->setShortcut( QKeySequence( "Ctrl+T" ) );
    tieEntity->setObjectName( "tieToEntityAction" );
    tieEntity->setToolTip( tr( "Select one or more world brushes first, then choose an FGD brush entity class. (Ctrl+T)" ) );
    m_pMenuSelection->addSeparator();
    m_pMenuSelection->addAction( tieEntity );
    connect( tieEntity, &QAction::triggered, this, [this] {
        const QStringList classes = m_pDocument->Fgd().SolidClassNames();
        if ( classes.isEmpty() ) { QMessageBox::warning( this, tr( "Tie to Entity" ), tr( "No brush entity classes are available in the current FGD." ) ); return; }
        bool ok = false;
        const QString name = QInputDialog::getItem( this, tr( "Tie to Entity" ), tr( "Class:" ), classes, qMax( 0, classes.indexOf( QStringLiteral( "func_detail" ) ) ), false, &ok );
        if ( ok && !m_pDocument->TieSelectionToEntity( name ) ) QMessageBox::warning( this, tr( "Tie to Entity" ), tr( "Select only world brushes." ) );
    } );
    QAction *tieWorld = m_pMenuTools->addAction( tr( "Move to World" ) );
    connect( tieWorld, &QAction::triggered, this, [this] {
        if ( QMessageBox::question( this, tr( "Move to World" ), tr( "Remove the selected brush entities and their properties, leaving their brushes in the world?" ) ) != QMessageBox::Yes ) return;
        if ( !m_pDocument->MoveSelectionToWorld() ) QMessageBox::warning( this, tr( "Move to World" ), tr( "Select only brush entities." ) );
    } );
    QAction *centerOrigins = m_pMenuTools->addAction( tr( "Center Brush Entity Origins" ) );
    centerOrigins->setToolTip( tr( "Center explicit origins on selected brush entities without moving their brushes." ) );
    connect( centerOrigins, &QAction::triggered, this, [this] {
        if ( !m_pDocument->CenterSelectedBrushEntityOrigins() )
            QMessageBox::information( this, tr( "Center Brush Entity Origins" ),
                tr( "Select a brush entity with an explicit origin that is not already centered." ) );
    } );
    QAction *snapIndividually = m_pMenuTools->addAction( tr( "Snap Selected to Grid Individually" ) );
    snapIndividually->setShortcut( QKeySequence( "Ctrl+Shift+B" ) );
    connect( snapIndividually, &QAction::triggered, this, [this] { m_pDocument->SnapSelectionToGrid( true ); } );
    QAction *clipBoxes = m_pMenuTools->addAction( tr( "Clip Convex Brushes..." ) );
    connect( clipBoxes, &QAction::triggered, this, [this] {
        QDialog dialog( this );
        dialog.setWindowTitle( tr( "Clip Convex Brushes" ) );
        QVBoxLayout *layout = new QVBoxLayout( &dialog );
        QFormLayout *form = new QFormLayout;
        QComboBox *axis = new QComboBox( &dialog );
        axis->addItems( QStringList() << QStringLiteral( "X" ) << QStringLiteral( "Y" ) << QStringLiteral( "Z" ) );
        QDoubleSpinBox *coordinate = new QDoubleSpinBox( &dialog );
        coordinate->setRange( -1048576, 1048576 ); coordinate->setDecimals( 4 );
        QComboBox *keep = new QComboBox( &dialog );
        keep->addItem( tr( "Keep lower side" ), -1 );
        keep->addItem( tr( "Keep upper side" ), 1 );
        keep->addItem( tr( "Keep both sides" ), 0 );
        form->addRow( tr( "Axis:" ), axis );
        form->addRow( tr( "Cut coordinate:" ), coordinate );
        form->addRow( tr( "Result:" ), keep );
        layout->addLayout( form );
        QDialogButtonBox *buttons = new QDialogButtonBox( QDialogButtonBox::Ok | QDialogButtonBox::Cancel, &dialog );
        connect( buttons, &QDialogButtonBox::accepted, &dialog, &QDialog::accept );
        connect( buttons, &QDialogButtonBox::rejected, &dialog, &QDialog::reject );
        layout->addWidget( buttons );
        if ( dialog.exec() == QDialog::Accepted && !m_pDocument->ClipSelectedBoxes( axis->currentIndex(), coordinate->value(), keep->currentData().toInt() ) )
            QMessageBox::warning( this, tr( "Clip Convex Brushes" ), tr( "Select valid convex brushes without Hammer++ vertex caches. The cut must pass through each selected brush." ) );
    } );
    QAction *clipPlane = m_pMenuTools->addAction( tr( "Clip Convex Brushes by Three Points..." ) );
    connect( clipPlane, &QAction::triggered, this, [this] {
        QDialog dialog( this );
        dialog.setWindowTitle( tr( "Clip by Three World Points" ) );
        QVBoxLayout *layout = new QVBoxLayout( &dialog );
        QLabel *hint = new QLabel( tr( "Enter three non-collinear world coordinates (X Y Z). Positive side follows the VMF plane normal: (point 3 - point 1) cross (point 2 - point 1)." ), &dialog );
        hint->setWordWrap( true ); layout->addWidget( hint );
        QFormLayout *form = new QFormLayout;
        QLineEdit *point[3];
        const QString defaults[] = { QStringLiteral( "0 0 0" ), QStringLiteral( "0 0 128" ), QStringLiteral( "128 128 0" ) };
        for ( int i = 0; i < 3; ++i ) { point[i] = new QLineEdit( defaults[i], &dialog ); form->addRow( tr( "Point %1:" ).arg( i + 1 ), point[i] ); }
        QComboBox *keep = new QComboBox( &dialog );
        keep->addItem( tr( "Keep negative side" ), -1 ); keep->addItem( tr( "Keep positive side" ), 1 ); keep->addItem( tr( "Keep both sides" ), 0 );
        form->addRow( tr( "Result:" ), keep ); layout->addLayout( form );
        QDialogButtonBox *buttons = new QDialogButtonBox( QDialogButtonBox::Ok | QDialogButtonBox::Cancel, &dialog );
        connect( buttons, &QDialogButtonBox::accepted, &dialog, &QDialog::accept );
        connect( buttons, &QDialogButtonBox::rejected, &dialog, &QDialog::reject ); layout->addWidget( buttons );
        if ( dialog.exec() != QDialog::Accepted ) return;
        double points[3][3];
        if ( !ParseVec3( point[0]->text(), points[0] ) || !ParseVec3( point[1]->text(), points[1] ) ||
             !ParseVec3( point[2]->text(), points[2] ) || !m_pDocument->ClipSelectedByPlane( points, keep->currentData().toInt() ) )
            QMessageBox::warning( this, tr( "Clip by Three World Points" ), tr( "Select valid convex brushes and enter three non-collinear points. The plane must pass through every selected brush; unsupported vertex caches cannot be clipped." ) );
    } );
    QAction *hollow = m_pMenuTools->addAction( tr( "Make Hollow..." ) );
    connect( hollow, &QAction::triggered, this, [this] {
        bool accepted = false;
        const int width = QInputDialog::getInt( this, tr( "Make Hollow" ),
            tr( "Wall thickness (negative builds outward):" ), 32, -1024, 1024, 1, &accepted );
        if ( accepted && !m_pDocument->HollowSelection( width ) )
            QMessageBox::warning( this, tr( "Make Hollow" ),
                tr( "Select axis-aligned box brushes without Hammer++ vertex caches. Thickness must be at least 2 units and fit inside each brush." ) );
    } );
    QAction *scaleBoxes = m_pMenuTools->addAction( tr( "Scale Brushes..." ) );
    connect( scaleBoxes, &QAction::triggered, this, [this] {
        QDialog dialog( this );
        dialog.setWindowTitle( tr( "Scale Brushes" ) );
        QVBoxLayout *layout = new QVBoxLayout( &dialog );
        QFormLayout *form = new QFormLayout;
        QDoubleSpinBox *axes[3];
        const char *labels[3] = { "X", "Y", "Z" };
        for ( int axis = 0; axis < 3; ++axis )
        {
            axes[axis] = new QDoubleSpinBox( &dialog );
            axes[axis]->setRange( 0.01, 100.0 );
            axes[axis]->setDecimals( 3 );
            axes[axis]->setSingleStep( 0.1 );
            axes[axis]->setValue( 1.0 );
            form->addRow( QString::fromLatin1( labels[axis] ), axes[axis] );
        }
        layout->addLayout( form );
        QDialogButtonBox *buttons = new QDialogButtonBox( QDialogButtonBox::Ok | QDialogButtonBox::Cancel, &dialog );
        connect( buttons, &QDialogButtonBox::accepted, &dialog, &QDialog::accept );
        connect( buttons, &QDialogButtonBox::rejected, &dialog, &QDialog::reject );
        layout->addWidget( buttons );
        if ( dialog.exec() != QDialog::Accepted ) return;
        const double factors[3] = { axes[0]->value(), axes[1]->value(), axes[2]->value() };
        if ( factors[0] == 1.0 && factors[1] == 1.0 && factors[2] == 1.0 ) return;
        if ( !m_pDocument->ScaleSelectedBoxes( factors ) )
            QMessageBox::warning( this, tr( "Scale Brushes" ),
                tr( "Select valid convex brushes without Hammer++ vertex caches. Scale must produce valid map coordinates." ) );
    } );
    QAction *rotateLeft = m_pMenuTools->addAction( tr( "Rotate Brushes 90° Left" ) );
    QAction *rotateRight = m_pMenuTools->addAction( tr( "Rotate Brushes 90° Right" ) );
    const auto rotateBoxes = [this]( bool clockwise, int axis ) {
        if ( !m_pDocument->RotateSelectedBoxes90( clockwise, axis ) )
            QMessageBox::warning( this, tr( "Rotate Brushes" ),
                tr( "Select valid convex brushes without Hammer++ vertex caches. Texture scales must be nonzero." ) );
    };
    connect( rotateLeft, &QAction::triggered, this, [rotateBoxes] { rotateBoxes( false, 2 ); } );
    connect( rotateRight, &QAction::triggered, this, [rotateBoxes] { rotateBoxes( true, 2 ); } );
    QMenu *rotateAxes = m_pMenuTools->addMenu( tr( "Rotate Brushes Around Axis" ) );
    const char *positiveNames[] = { QT_TR_NOOP( "X axis +90°" ), QT_TR_NOOP( "Y axis +90°" ), QT_TR_NOOP( "Z axis +90°" ) };
    const char *negativeNames[] = { QT_TR_NOOP( "X axis -90°" ), QT_TR_NOOP( "Y axis -90°" ), QT_TR_NOOP( "Z axis -90°" ) };
    for ( int axis = 0; axis < 3; ++axis )
    {
        connect( rotateAxes->addAction( tr( positiveNames[axis] ) ), &QAction::triggered, this, [rotateBoxes, axis] { rotateBoxes( false, axis ); } );
        connect( rotateAxes->addAction( tr( negativeNames[axis] ) ), &QAction::triggered, this, [rotateBoxes, axis] { rotateBoxes( true, axis ); } );
    }
    QMenu *flipAxes = m_pMenuTools->addMenu( tr( "Flip Brushes" ) );
    const char *flipNames[] = { QT_TR_NOOP( "Flip X" ), QT_TR_NOOP( "Flip Y" ), QT_TR_NOOP( "Flip Z" ) };
    for ( int axis = 0; axis < 3; ++axis )
        connect( flipAxes->addAction( tr( flipNames[axis] ) ), &QAction::triggered, this, [this, axis] {
            if ( !m_pDocument->FlipSelectedBoxes( axis ) )
                QMessageBox::warning( this, tr( "Flip Brushes" ),
                    tr( "Select valid convex brushes without Hammer++ vertex caches. Texture scales must be nonzero." ) );
        } );
    m_pMenuTools->addSeparator();

    // Tool switching: exclusive group of working tool modes; the ids map to
    // the document's EHammerTool values and retain classic Hammer hotkeys.
    m_pToolActionGroup = new QActionGroup( this );
    m_pToolActionGroup->setExclusive( true );

    struct ToolDef
    {
        const char *name;
        const char *id;
        QList<QKeySequence> shortcuts;
        const char *icon;
    };

    const ToolDef toolDefs[] = {
        { QT_TR_NOOP( "Selection Tool" ), "select",
            QList<QKeySequence>() << QKeySequence( "1" ) << QKeySequence( "Shift+S" ),
            ":/tool_select.svg" },
        { QT_TR_NOOP( "Camera Tool" ), "camera",
            QList<QKeySequence>() << QKeySequence( "Shift+C" ),
            ":/tool_camera.svg" },
        { QT_TR_NOOP( "Entity Tool" ), "entity",
            QList<QKeySequence>() << QKeySequence( "2" ) << QKeySequence( "Shift+E" ),
            ":/tool_entity.svg" },
        { QT_TR_NOOP( "Block Tool" ), "block",
            QList<QKeySequence>() << QKeySequence( "3" ) << QKeySequence( "Shift+B" ),
            ":/tool_block.svg" },
        { QT_TR_NOOP( "Apply Texture Tool" ), "texture",
            QList<QKeySequence>() << QKeySequence( "Shift+T" ),
            ":/tool_apply_material.svg" },
        { QT_TR_NOOP( "Apply Decals" ), "decal",
            QList<QKeySequence>() << QKeySequence( "Shift+D" ),
            ":/tool_decal.svg" },
        { QT_TR_NOOP( "Clipping Tool" ), "clip",
            QList<QKeySequence>() << QKeySequence( "Shift+X" ),
            ":/tool_clip.svg" },
        { QT_TR_NOOP( "Vertex Tool" ), "vertex",
            QList<QKeySequence>() << QKeySequence( "Shift+V" ),
            ":/tool_vertex.svg" },
    };

    for ( const ToolDef &def : toolDefs )
    {
        QAction *toolAction = new QAction( tr( def.name ), this );
        toolAction->setCheckable( true );
        toolAction->setData( QString::fromLatin1( def.id ) );
        toolAction->setObjectName( QStringLiteral( "toolAction_%1" ).arg( QString::fromLatin1( def.id ) ) );
        toolAction->setShortcuts( def.shortcuts );
        QStringList shortcutLabels;
        for ( const QKeySequence &shortcut : def.shortcuts )
            shortcutLabels.append( shortcut.toString( QKeySequence::NativeText ) );
        toolAction->setToolTip( QStringLiteral( "%1 (%2)" ).arg( tr( def.name ), shortcutLabels.join( QStringLiteral( ", " ) ) ) );
        toolAction->setIcon( QIcon( QString::fromLatin1( def.icon ) ) );
        m_pToolActionGroup->addAction( toolAction );
        m_pMenuTools->addAction( toolAction );
    }
    m_pToolActionGroup->actions().first()->setChecked( true );

    connect( m_pToolActionGroup, &QActionGroup::triggered, this, &CMainWindow::OnToolModeChanged );

    m_pMenuTools->addSeparator();

    // Dock show/hide actions under Tools as well
    m_pMenuTools->addAction( m_pTextureBrowserDock->toggleViewAction() );
    m_pMenuTools->addAction( m_pObjectPropertiesDock->toggleViewAction() );
    // TODO(hammer-port): build the Entity Report dock and add its
    // toggleViewAction here instead of the placeholder dialog action
    m_pMenuTools->addAction( actionEntityReport );

    QMenu *languageMenu = m_pMenuTools->addMenu( tr( "&Language" ) );
    QActionGroup *languageGroup = new QActionGroup( this );
    languageGroup->setExclusive( true );
    HammerLocalization *localization = QCoreApplication::instance()->findChild<HammerLocalization *>();
    if ( localization )
    {
        const QString currentLocale = localization->CurrentLanguage();
        for ( const HammerLanguageInfo &language : localization->AvailableLanguages() )
        {
            QAction *languageAction = new QAction( language.displayName, languageGroup );
            languageAction->setCheckable( true );
            languageAction->setData( language.locale );
            languageAction->setChecked( language.locale == currentLocale );
            languageGroup->addAction( languageAction );
            languageMenu->addAction( languageAction );
        }
        connect( languageGroup, &QActionGroup::triggered, this, &CMainWindow::OnLanguageTriggered );
    }

    //--- Help -----------------------------------------------------------------
    m_pMenuHelp = menuBar()->addMenu( tr( "&Help" ) );

    QAction *actionAbout = new QAction( tr( "About &Hammer" ), this );
    connect( actionAbout, &QAction::triggered, this, &CMainWindow::OnAbout );
    m_pMenuHelp->addAction( actionAbout );
}

bool CMainWindow::OpenDocumentPath( const QString &fileName )
{
    if ( fileName.isEmpty() || !m_pDocument->LoadFromFile( fileName ) )
        return false;
    AddRecentFile( fileName );
    LogOutput( tr( "Opened map: %1" ).arg( fileName ) );
    return true;
}

void CMainWindow::OfferRecovery()
{
    HammerRecoveryInfo recovery;
    if ( !m_recoveryStore.Read( recovery ) )
        return;

    const QString source = recovery.sourcePath.isEmpty() ? tr( "Untitled map" ) : recovery.sourcePath;
    const QString saved = recovery.savedAt.isValid() ? recovery.savedAt.toLocalTime().toString( Qt::ISODate ) : tr( "unknown time" );
    const QMessageBox::StandardButton answer = QMessageBox::question(
        this, tr( "Recover Unsaved Map" ),
        tr( "A recovery copy for %1 was saved at %2. Restore it? This will not overwrite the original map." ).arg( source, saved ),
        QMessageBox::Yes | QMessageBox::No, QMessageBox::Yes );
    if ( answer == QMessageBox::No )
    {
        m_recoveryStore.Clear();
        return;
    }

    if ( !m_pDocument->RestoreFromRecovery( recovery.vmfPath, recovery.sourcePath ) )
    {
        QMessageBox::warning( this, tr( "Recover Unsaved Map" ), tr( "Could not restore the recovery copy: %1" ).arg( m_pDocument->LastError() ) );
        return;
    }
    LogOutput( tr( "Restored recovery copy for %1." ).arg( source ) );
}

//-----------------------------------------------------------------------------
// Tool bars: map commands and entity picker on top, editing tools on the left.
//-----------------------------------------------------------------------------
void CMainWindow::CreateToolBars()
{
    // Row 1: Map tools (file operations + run map)
    m_pMapToolBar = addToolBar( tr( "Map Tools" ) );
    m_pMapToolBar->setObjectName( "MapToolBar" );
    m_pMapToolBar->setIconSize( QSize( 20, 20 ) );
    m_pMapToolBar->setToolButtonStyle( Qt::ToolButtonTextUnderIcon );
    m_pMapToolBar->addAction( m_pActionNew );
    m_pMapToolBar->addAction( m_pActionOpen );
    m_pMapToolBar->addAction( m_pActionSave );
    m_pMapToolBar->addSeparator();
    m_pMapToolBar->addAction( m_pActionUndo );
    m_pMapToolBar->addAction( m_pActionRedo );
    m_pMapToolBar->addSeparator();
    m_pMapToolBar->addAction( m_pActionRunMap );

    // Keep the existing object name so saved window state can restore this bar.
    m_pBoundaryToolBar = new QToolBar( tr( "Editing Tools" ), this );
    addToolBar( Qt::LeftToolBarArea, m_pBoundaryToolBar );
    m_pBoundaryToolBar->setObjectName( "EditingToolBar" );
    m_pBoundaryToolBar->setIconSize( QSize( 24, 24 ) );
    m_pBoundaryToolBar->setToolButtonStyle( Qt::ToolButtonIconOnly );
    m_pBoundaryToolBar->addActions( m_pToolActionGroup->actions() );

    QToolBar *entityToolBar = new QToolBar( tr( "Entity Tools" ), this );
    entityToolBar->setObjectName( "EntityToolBar" );
    addToolBar( Qt::TopToolBarArea, entityToolBar );
    QLabel *placeEntityLabel = new QLabel( tr( "Place Entity:" ), entityToolBar );
    placeEntityLabel->setObjectName( "PlaceEntityLabel" );
    m_pEntityToolCombo = new QComboBox( entityToolBar );
    m_pEntityToolCombo->setObjectName( "EntityToolCombo" );
    m_pEntityToolCombo->setAccessibleName( tr( "Place Entity Class" ) );
    m_pEntityToolCombo->setMinimumContentsLength( 28 );
    m_pEntityToolCombo->setSizeAdjustPolicy( QComboBox::AdjustToMinimumContentsLengthWithIcon );
    placeEntityLabel->setBuddy( m_pEntityToolCombo );
    m_pEntityToolCombo->setToolTip( tr( "Choose the class created by the Entity Tool (2)." ) );
    entityToolBar->addWidget( placeEntityLabel );
    entityToolBar->addWidget( m_pEntityToolCombo );
    connect( m_pEntityToolCombo, &QComboBox::currentTextChanged,
        this, &CMainWindow::OnEntityToolChanged );

    QAction *classicTools = m_pMenuView->addAction( tr( "Classic Tool Bar Layout" ) );
    classicTools->setObjectName( "classicToolBarLayout" );
    connect( classicTools, &QAction::triggered, this, [this, entityToolBar] {
        addToolBar( Qt::TopToolBarArea, m_pMapToolBar );
        addToolBar( Qt::TopToolBarArea, entityToolBar );
        addToolBar( Qt::LeftToolBarArea, m_pBoundaryToolBar );
        m_pBoundaryToolBar->setToolButtonStyle( Qt::ToolButtonIconOnly );
        m_pMapToolBar->show(); entityToolBar->show(); m_pBoundaryToolBar->show();
    } );
}

//-----------------------------------------------------------------------------
// Central widget: four-viewport splitter layout
//-----------------------------------------------------------------------------
void CMainWindow::CreateCentralWidget()
{
    m_pViewportSplitter = new QSplitter( this );
    m_pViewportSplitter->setObjectName( "ViewportSplitter" );
    m_pViewportSplitter->setOrientation( Qt::Horizontal );
    m_pViewportSplitter->setChildrenCollapsible( false );

    // Original Hammer's four-view layout has a 3D view in the upper left.
    m_pViewLeftSplitter = new QSplitter( Qt::Vertical, m_pViewportSplitter );
    m_pViewLeftSplitter->setObjectName( "ViewLeftSplitter" );
    m_pViewLeftSplitter->setChildrenCollapsible( false );
    m_pViewport3D = new CQtViewport3D();
    m_pViewport3D->setDocument( m_pDocument );
    m_pViewport3D->setMaterialGameDirectory( QSettings().value(
        QStringLiteral( "compile/gameDir" ), PortableDefault( QCoreApplication::applicationDirPath(),
            QStringLiteral( "compile/gameDir" ) ) ).toString() );
    const QString materialGameDir = QSettings().value( QStringLiteral( "compile/gameDir" ),
        PortableDefault( QCoreApplication::applicationDirPath(), QStringLiteral( "compile/gameDir" ) ) ).toString();
    if ( !materialGameDir.isEmpty() && QFileInfo::exists( QDir( materialGameDir ).filePath( QStringLiteral( "gameinfo.txt" ) ) ) )
        m_pViewport3D->setDisplayMode( CQtViewport3D::Textured );
    m_pViewport3D->setObjectName( "Viewport3D" );
    m_pViewLeftSplitter->addWidget( m_pViewport3D );

    // The remaining panes show top (XY), front (XZ), and side (YZ).
    QSplitter *rightSplitter = new QSplitter( Qt::Vertical, m_pViewportSplitter );
    m_pView2DSplitter = rightSplitter;
    rightSplitter->setObjectName( "View2DSplitter" );
    rightSplitter->setChildrenCollapsible( false );

    m_pViewportTop = new CQtViewport2D( CQtViewport2D::VIEWAXIS_TOP );
    m_pViewportFront = new CQtViewport2D( CQtViewport2D::VIEWAXIS_FRONT );
    m_pViewportSide = new CQtViewport2D( CQtViewport2D::VIEWAXIS_SIDE );
    m_pViewportTop->setObjectName( "ViewportTop" );
    m_pViewportFront->setObjectName( "ViewportFront" );
    m_pViewportSide->setObjectName( "ViewportSide" );
    m_pViewportTop->setDocument( m_pDocument );
    m_pViewportFront->setDocument( m_pDocument );
    m_pViewportSide->setDocument( m_pDocument );
    for ( CQtViewport2D *viewport : { m_pViewportTop, m_pViewportFront, m_pViewportSide } )
        connect( viewport, &CQtViewport2D::savedCameraActivated, this, [this]( int index ) {
            const auto &cameras = m_pDocument->Map().cameras;
            double eye[3], angles[3];
            if ( index >= 0 && index < cameras.size() && ReadSavedCamera( cameras[index], eye, angles ) )
                m_pViewport3D->setCameraPositionAngles( eye, angles );
        } );

    rightSplitter->addWidget( m_pViewportTop );
    m_pViewLeftSplitter->addWidget( m_pViewportFront );
    rightSplitter->addWidget( m_pViewportSide );

    m_pLastViewport = m_pViewport3D;
    for ( QWidget *viewport : { static_cast<QWidget *>( m_pViewport3D ), static_cast<QWidget *>( m_pViewportTop ),
                                static_cast<QWidget *>( m_pViewportFront ), static_cast<QWidget *>( m_pViewportSide ) } )
        viewport->installEventFilter( this );

    m_pViewportSplitter->addWidget( rightSplitter );

    m_pViewportSplitter->setStretchFactor( 0, 1 );
    m_pViewportSplitter->setStretchFactor( 1, 1 );
    m_pViewLeftSplitter->setStretchFactor( 0, 1 );
    m_pViewLeftSplitter->setStretchFactor( 1, 1 );
    rightSplitter->setStretchFactor( 0, 1 );
    rightSplitter->setStretchFactor( 1, 1 );

    setCentralWidget( m_pViewportSplitter );
}

//-----------------------------------------------------------------------------
// Dock widgets: Texture Browser / Object Properties / Output
//-----------------------------------------------------------------------------
void CMainWindow::CreateDockWidgets()
{
    //--- Texture Browser ------------------------------------------------------
    m_pTextureBrowserDock = new QDockWidget( tr( "Texture Browser" ), this );
    m_pTextureBrowserDock->setObjectName( "TextureBrowserDock" );
    m_pTextureBrowserDock->setAllowedAreas( Qt::LeftDockWidgetArea | Qt::RightDockWidgetArea );

    QWidget *materialPanel = new QWidget( m_pTextureBrowserDock );
    QVBoxLayout *materialLayout = new QVBoxLayout( materialPanel );
    materialLayout->setContentsMargins( 4, 4, 4, 4 );
    QLabel *materialHelp = new QLabel( tr( "Material name (relative to materials/):" ), materialPanel );
    materialHelp->setWordWrap( true );
    QLineEdit *materialEdit = new QLineEdit( m_pDocument->ActiveMaterial(), materialPanel );
    materialEdit->setObjectName( "MaterialName" );
    materialEdit->setToolTip( tr( "Press Enter to set the material for new brushes." ) );
    QPushButton *setMaterial = new QPushButton( tr( "Set for new brushes" ), materialPanel );
    QPushButton *applyMaterial = new QPushButton( tr( "Apply to selected brush" ), materialPanel );
    QPushButton *editFaces = new QPushButton( tr( "Edit brush faces..." ), materialPanel );
    editFaces->setToolTip( tr( "Edit selected 3D faces in the Material tool, or all faces of selected brushes." ) );
    QPushButton *replaceMaterial = new QPushButton( tr( "Replace materials..." ), materialPanel );
    m_pTextureList = new QListWidget( materialPanel );
    m_pTextureList->setObjectName( "SuggestedMaterials" );
    QLineEdit *materialFilter = new QLineEdit( materialPanel );
    materialFilter->setPlaceholderText( tr( "Filter materials" ) );
    QPushButton *scanMaterials = new QPushButton( tr( "Scan game materials..." ), materialPanel );
    QLabel *materialPreviewImage = new QLabel( materialPanel );
    materialPreviewImage->setObjectName( "MaterialPreviewImage" );
    materialPreviewImage->setFixedSize( 128, 128 );
    materialPreviewImage->setAlignment( Qt::AlignCenter );
    QLabel *materialPreviewStatus = new QLabel( tr( "Select a scanned material to preview it." ), materialPanel );
    materialPreviewStatus->setObjectName( "MaterialPreviewStatus" );
    materialPreviewStatus->setWordWrap( true );
    CMaterialBrowserPreview *materialPreview = new CMaterialBrowserPreview( materialPreviewImage, materialPreviewStatus, materialPanel );
    m_pTextureList->setToolTip( tr( "Select to preview; double-click to apply to selected brushes." ) );
    QLabel *materialStatus = new QLabel( tr( "No materials loaded. Choose Scan game materials..., then select a result to set the active material." ), materialPanel );
    materialStatus->setObjectName( "MaterialStatus" );
    materialStatus->setWordWrap( true );
    materialLayout->addWidget( materialHelp );
    materialLayout->addWidget( materialEdit );
    materialLayout->addWidget( setMaterial );
    materialLayout->addWidget( applyMaterial );
    materialLayout->addWidget( editFaces );
    materialLayout->addWidget( replaceMaterial );
    materialLayout->addWidget( materialFilter );
    materialLayout->addWidget( scanMaterials );
    materialLayout->addWidget( materialStatus );
    materialLayout->addWidget( materialPreviewImage, 0, Qt::AlignHCenter );
    materialLayout->addWidget( materialPreviewStatus );
    materialLayout->addWidget( m_pTextureList, 1 );

    const auto catalog = std::make_shared<QStringList>();
    const auto catalogGameDir = std::make_shared<QString>();
    const auto updateMaterials = [this, materialFilter, catalog] {
        m_pTextureList->clear();
        const QString needle = materialFilter->text();
        for ( const QString &name : *catalog )
            if ( name.contains( needle, Qt::CaseInsensitive ) ) m_pTextureList->addItem( name );
    };
    connect( materialFilter, &QLineEdit::textChanged, this, [updateMaterials] { updateMaterials(); } );
    connect( scanMaterials, &QPushButton::clicked, this, [this, scanMaterials, materialStatus, catalog, catalogGameDir, materialPreview, updateMaterials] {
        QSettings settings;
        const QString initial = settings.value( QStringLiteral( "compile/gameDir" ),
            PortableDefault( QCoreApplication::applicationDirPath(), QStringLiteral( "compile/gameDir" ) ) ).toString();
        const QString dir = QFileDialog::getExistingDirectory( this, tr( "Select game directory" ), initial );
        if ( dir.isEmpty() ) return;
        catalogGameDir->clear();
        materialPreview->Request( {}, {} );
        scanMaterials->setEnabled( false );
        materialStatus->setText( tr( "Scanning materials..." ) );
        const auto result = std::make_shared<MaterialCatalogResult>();
        const QStringList searchPaths = MaterialSearchPaths( dir );
        QThread *worker = QThread::create( [result, dir, searchPaths] { *result = ScanGameMaterials( dir, searchPaths ); } );
        connect( worker, &QThread::finished, this, [scanMaterials, materialStatus, catalog, catalogGameDir, dir, result, updateMaterials] {
            *catalog = result->names;
            *catalogGameDir = dir;
            updateMaterials();
            scanMaterials->setEnabled( true );
            materialStatus->setText( result->failedArchives.isEmpty() ?
                tr( "%1 materials found; mounted games are not included." ).arg( catalog->size() ) :
                tr( "%1 materials found; some VPK indexes could not be read: %2" ).arg( catalog->size() ).arg( result->failedArchives.join( QStringLiteral( ", " ) ) ) );
        } );
        connect( worker, &QThread::finished, worker, &QObject::deleteLater );
        worker->start();
    } );

    const auto setActiveMaterial = [this, materialEdit]() -> bool {
        if ( !m_pDocument->SetActiveMaterial( materialEdit->text() ) )
        {
            QMessageBox::warning( this, tr( "Material" ), tr( "Enter a valid material name relative to materials/." ) );
            return false;
        }
        materialEdit->setText( m_pDocument->ActiveMaterial() );
        LogOutput( tr( "Active material: %1" ).arg( m_pDocument->ActiveMaterial() ) );
        return true;
    };
    connect( setMaterial, &QPushButton::clicked, this, [setActiveMaterial]() { setActiveMaterial(); } );
    connect( materialEdit, &QLineEdit::returnPressed, this, [setActiveMaterial]() { setActiveMaterial(); } );
    connect( editFaces, &QPushButton::clicked, this, [this, materialEdit] {
        // Material-tool face picks are independent of object selection. When
        // present, edit just those faces; otherwise retain the object workflow.
        const QMap<int, QSet<int>> pickedFaces = m_pDocument->GetTool() == TOOL_TEXTURE
            ? m_pViewport3D->selectedFaces() : QMap<int, QSet<int>>();
        const bool picked = !pickedFaces.isEmpty();
        const QList<int> ids = picked ? pickedFaces.keys() : m_pDocument->SelectedVisibleSolidIds();
        if ( ids.isEmpty() ) { QMessageBox::information( this, tr( "Face Properties" ), tr( "Select one visible world brush or brush entity first." ) ); return; }
        QList<SideLite> allFaces;
        QStringList labels;
        QMap<int, int> faceCounts;
        for ( int id : ids )
        {
            const SolidLite *solid = m_pDocument->Map().SolidById( id );
            if ( !solid ) return;
            int count = 0;
            for ( int i = 0; i < solid->sides.size(); ++i )
            {
                if ( picked && !pickedFaces.value( id ).contains( solid->sides[i].id ) ) continue;
                allFaces.append( solid->sides[i] );
                labels.append( ids.size() == 1 ? QString::number( i + 1 ) : tr( "Brush %1, face %2" ).arg( id ).arg( i + 1 ) );
                ++count;
            }
            if ( !count || ( picked && count != pickedFaces.value( id ).size() ) ) return;
            faceCounts.insert( id, count );
        }
        CFacePropertiesDialog dialog( allFaces, this, materialEdit->text(), labels );
        if ( dialog.exec() != QDialog::Accepted ) return;
        QMap<int, QList<SideLite>> facesBySolid;
        const QList<SideLite> faces = dialog.Faces();
        int offset = 0;
        for ( int id : ids )
        {
            facesBySolid.insert( id, faces.mid( offset, faceCounts[id] ) );
            offset += faceCounts[id];
        }
        const bool validSelection = !picked || ( m_pDocument->GetTool() == TOOL_TEXTURE && m_pViewport3D->selectedFaces() == pickedFaces );
        const bool applied = validSelection && ( picked ? m_pDocument->SetVisibleFaceProperties( facesBySolid ) :
            m_pDocument->SetSelectedSolidsFaceProperties( facesBySolid ) );
        if ( !applied )
            QMessageBox::warning( this, tr( "Face Properties" ), tr( "Invalid face properties. No changes were made." ) );
    } );
    connect( applyMaterial, &QPushButton::clicked, this, [this, setActiveMaterial]() {
        if ( setActiveMaterial() && !m_pDocument->ApplyMaterialToSelection() )
            QMessageBox::information( this, tr( "Material" ), tr( "Select a brush first." ) );
    } );
    connect( replaceMaterial, &QPushButton::clicked, this, [this, materialEdit] {
        QDialog dialog( this );
        dialog.setWindowTitle( tr( "Replace Materials" ) );
        QVBoxLayout *layout = new QVBoxLayout( &dialog );
        QFormLayout *form = new QFormLayout;
        QLineEdit *find = new QLineEdit( materialEdit->text(), &dialog );
        QLineEdit *replacement = new QLineEdit( &dialog );
        form->addRow( tr( "Find material:" ), find );
        form->addRow( tr( "Replace with:" ), replacement );
        QComboBox *matchMode = new QComboBox( &dialog );
        matchMode->addItem( tr( "Exact match" ) );
        matchMode->addItem( tr( "Partial match: replace whole name" ) );
        matchMode->addItem( tr( "Partial match: replace matched text" ) );
        form->addRow( tr( "Match mode:" ), matchMode );
        layout->addLayout( form );
        QCheckBox *selected = new QCheckBox( tr( "Selected brushes only" ), &dialog );
        layout->addWidget( selected );
        QDialogButtonBox *buttons = new QDialogButtonBox( QDialogButtonBox::Ok | QDialogButtonBox::Cancel, &dialog );
        connect( buttons, &QDialogButtonBox::accepted, &dialog, &QDialog::accept );
        connect( buttons, &QDialogButtonBox::rejected, &dialog, &QDialog::reject );
        layout->addWidget( buttons );
        if ( dialog.exec() != QDialog::Accepted ) return;
        const int count = m_pDocument->ReplaceMaterial( find->text(), replacement->text(), selected->isChecked(),
            static_cast<CHammerDocument::MaterialReplaceMode>( matchMode->currentIndex() ) );
        if ( count < 0 )
            QMessageBox::warning( this, tr( "Material" ), tr( "Enter a valid material name relative to materials/." ) );
        else
            QMessageBox::information( this, tr( "Replace Materials" ), tr( "%1 faces changed." ).arg( count ) );
    } );
    connect( m_pTextureList, &QListWidget::currentItemChanged, this,
        [materialEdit, setActiveMaterial, materialPreview, catalogGameDir]( QListWidgetItem *item ) {
            if ( !item ) { materialPreview->Request( {}, {} ); return; }
            materialEdit->setText( item->text() );
            if ( setActiveMaterial() ) materialPreview->Request( *catalogGameDir, item->text() );
        } );
    connect( m_pTextureList, &QListWidget::itemDoubleClicked, this, [this, materialEdit, setActiveMaterial]( QListWidgetItem *item ) {
        if ( !item ) return;
        materialEdit->setText( item->text() );
        if ( setActiveMaterial() && !m_pDocument->ApplyMaterialToSelection() )
            QMessageBox::information( this, tr( "Material" ), tr( "Select a brush first." ) );
    } );

    m_pTextureBrowserDock->setWidget( materialPanel );
    addDockWidget( Qt::LeftDockWidgetArea, m_pTextureBrowserDock );

    //--- Object Properties ----------------------------------------------------
    m_pObjectPropertiesDock = new QDockWidget( tr( "Object Properties" ), this );
    m_pObjectPropertiesDock->setObjectName( "ObjectPropertiesDock" );
    m_pObjectPropertiesDock->setAllowedAreas( Qt::LeftDockWidgetArea | Qt::RightDockWidgetArea );

    QWidget *propertiesPanel = new QWidget( m_pObjectPropertiesDock );
    QVBoxLayout *propertiesLayout = new QVBoxLayout( propertiesPanel );
    propertiesLayout->setContentsMargins( 4, 4, 4, 4 );
    propertiesLayout->setSpacing( 4 );

    m_pEntityClassCombo = new QComboBox( propertiesPanel );
    m_pEntityClassCombo->setObjectName( "SelectedEntityClass" );
    m_pEntityClassCombo->setAccessibleName( tr( "Selected Entity Class" ) );
    m_pEntityClassCombo->setToolTip( tr( "Class of the selected entity; changing it re-applies FGD defaults." ) );
    connect( m_pEntityClassCombo, &QComboBox::currentTextChanged,
        this, &CMainWindow::OnSelectedEntityClassChanged );

    m_pPropertyTree = new QTreeWidget( propertiesPanel );
    m_pPropertyTree->setObjectName( "EntityPropertyTree" );
    m_pPropertyTree->setColumnCount( 2 );
    m_pPropertyTree->setHeaderLabels( QStringList() << tr( "Property" ) << tr( "Value" ) );
    m_pPropertyTree->setAlternatingRowColors( true );
    m_pPropertyTree->setItemDelegateForColumn( 0, new CReadOnlyPropertyNameDelegate( m_pPropertyTree ) );
    m_pPropertyTree->setItemDelegateForColumn( 1, new CPropertyValueDelegate( m_pPropertyTree ) );
    connect( m_pPropertyTree, &QTreeWidget::itemChanged,
        this, &CMainWindow::OnPropertyItemChanged );
    m_pPropertyTree->setContextMenuPolicy( Qt::CustomContextMenu );
    connect( m_pPropertyTree, &QWidget::customContextMenuRequested, this, [this]( const QPoint &pos ) {
        QTreeWidgetItem *item = m_pPropertyTree->itemAt( pos );
        if ( !item || m_pDocument->SelectedIds().size() != 1 ) return;
        const EntityLite *entity = m_pDocument->Map().EntityById( m_pDocument->SelectedId() );
        const QString key = item->data( 0, Qt::UserRole ).toString();
        QString value;
        if ( !entity || key.compare( QLatin1String( "classname" ), Qt::CaseInsensitive ) == 0 ||
             key.compare( QLatin1String( "origin" ), Qt::CaseInsensitive ) == 0 ||
             !m_pDocument->Fgd().PropertyDefault( entity->classname, key, value ) ) return;
        QMenu menu( m_pPropertyTree );
        QAction *reset = menu.addAction( tr( "Reset to FGD Default" ) );
        if ( menu.exec( m_pPropertyTree->viewport()->mapToGlobal( pos ) ) == reset )
        {
            if ( m_pDocument->ResetSelectedEntityKey( key ) )
                PopulatePropertyTree();
        }
    } );

    QLabel *selectedClassLabel = new QLabel( tr( "Selected Entity Class:" ), propertiesPanel );
    selectedClassLabel->setObjectName( "SelectedEntityClassLabel" );
    selectedClassLabel->setBuddy( m_pEntityClassCombo );
    propertiesLayout->addWidget( selectedClassLabel );
    propertiesLayout->addWidget( m_pEntityClassCombo );
    propertiesLayout->addWidget( m_pPropertyTree, 1 );
    QPushButton *browseResource = new QPushButton( tr( "Browse..." ), propertiesPanel );
    browseResource->setObjectName( "BrowseEntityResource" );
    browseResource->setEnabled( false );
    propertiesLayout->addWidget( browseResource );
    const auto resourceType = [this]( const QTreeWidgetItem *item ) {
        if ( !item || m_pDocument->SelectedIds().size() != 1 ) return CFgdData::ResourceType::None;
        const EntityLite *entity = m_pDocument->Map().EntityById( m_pDocument->SelectedId() );
        if ( !entity ) return CFgdData::ResourceType::None;
        return m_pDocument->Fgd().PropertyResourceType( entity->classname, item->data( 0, Qt::UserRole ).toString() );
    };
    connect( m_pPropertyTree, &QTreeWidget::currentItemChanged, browseResource, [browseResource, resourceType]( QTreeWidgetItem *item ) {
        browseResource->setEnabled( resourceType( item ) != CFgdData::ResourceType::None );
    } );
    connect( browseResource, &QPushButton::clicked, this, [this, resourceType] {
        const QTreeWidgetItem *item = m_pPropertyTree->currentItem();
        const auto type = resourceType( item );
        if ( type == CFgdData::ResourceType::None ) return;
        const bool model = type == CFgdData::ResourceType::StudioModel;
        const QString key = item->data( 0, Qt::UserRole ).toString();
        const QString current = item->text( 1 );
        const int entityId = m_pDocument->SelectedId();
        const quint64 identity = m_pDocument->DocumentIdentity();
        const QString gameDir = QSettings().value( QStringLiteral( "compile/gameDir" ),
            PortableDefault( QCoreApplication::applicationDirPath(), QStringLiteral( "compile/gameDir" ) ) ).toString();
        const QString title = model ? tr( "Browse Models" ) : tr( "Browse Sounds" );
        if ( gameDir.isEmpty() || !QFileInfo( gameDir ).isDir() )
        {
            QMessageBox::information( this, title, tr( "Select a game directory first." ) );
            return;
        }
        const auto catalog = ScanGameResources( gameDir, MaterialSearchPaths( gameDir ),
            model ? QStringLiteral( "models" ) : QStringLiteral( "sound" ),
            model ? QStringList{ QStringLiteral( "mdl" ) } : QStringList{ QStringLiteral( "wav" ), QStringLiteral( "mp3" ) },
            model, true );
        QDialog dialog( this );
        dialog.setWindowTitle( title );
        dialog.resize( 620, 500 );
        QVBoxLayout *layout = new QVBoxLayout( &dialog );
        QLineEdit *filter = new QLineEdit( &dialog );
        filter->setPlaceholderText( tr( "Filter by name" ) );
        layout->addWidget( filter );
        QListWidget *items = new QListWidget( &dialog );
        items->setUniformItemSizes( true );
        items->addItems( catalog.names );
        const auto selected = items->findItems( current, Qt::MatchFixedString );
        if ( !selected.isEmpty() ) items->setCurrentItem( selected.first() );
        layout->addWidget( items );
        if ( !catalog.failedArchives.isEmpty() )
            layout->addWidget( new QLabel( tr( "%1 archive(s) could not be indexed." ).arg( catalog.failedArchives.size() ), &dialog ) );
        QDialogButtonBox *buttons = new QDialogButtonBox( QDialogButtonBox::Ok | QDialogButtonBox::Cancel, &dialog );
        layout->addWidget( buttons );
        QPushButton *accept = buttons->button( QDialogButtonBox::Ok );
        const auto updateAccept = [items, accept] {
            accept->setEnabled( items->currentItem() && !items->currentItem()->isHidden() );
        };
        updateAccept();
        connect( items, &QListWidget::currentItemChanged, &dialog, updateAccept );
        connect( filter, &QLineEdit::textChanged, &dialog, [items, updateAccept]( const QString &query ) {
            for ( int i = 0; i < items->count(); ++i )
                items->item( i )->setHidden( !items->item( i )->text().contains( query, Qt::CaseInsensitive ) );
            updateAccept();
        } );
        connect( buttons, &QDialogButtonBox::accepted, &dialog, &QDialog::accept );
        connect( buttons, &QDialogButtonBox::rejected, &dialog, &QDialog::reject );
        connect( items, &QListWidget::itemDoubleClicked, &dialog, &QDialog::accept );
        if ( dialog.exec() == QDialog::Accepted && items->currentItem() && !items->currentItem()->isHidden() &&
             m_pDocument->DocumentIdentity() == identity && m_pDocument->SelectedIds().size() == 1 &&
             m_pDocument->SelectedId() == entityId && m_pDocument->SetSelectedEntityKey( key, items->currentItem()->text() ) )
            PopulatePropertyTree();
    } );
    QPushButton *outputsButton = new QPushButton( CEntityOutputsDialog::T( "Edit Outputs..." ), propertiesPanel );
    outputsButton->setEnabled( false );
    connect( m_pDocument, &CHammerDocument::selectionChanged, outputsButton, [this, outputsButton]( int id ) {
        outputsButton->setEnabled( m_pDocument->SelectedIds().size() == 1 && m_pDocument->Map().EntityById( id ) );
    } );
    connect( outputsButton, &QPushButton::clicked, this, [this] {
        const EntityLite *entity = m_pDocument->Map().EntityById( m_pDocument->SelectedId() );
        if ( !entity || m_pDocument->SelectedIds().size() != 1 ) return;
        CEntityOutputsDialog dialog( entity->connections, this, &m_pDocument->Fgd(), &m_pDocument->Map(), entity->classname );
        if ( dialog.exec() == QDialog::Accepted ) m_pDocument->SetSelectedEntityOutputs( dialog.Outputs() );
    } );
    propertiesLayout->addWidget( outputsButton );

    m_pObjectPropertiesDock->setWidget( propertiesPanel );
    addDockWidget( Qt::RightDockWidgetArea, m_pObjectPropertiesDock );

    //--- Output ---------------------------------------------------------------
    m_pOutputDock = new QDockWidget( tr( "Output" ), this );
    m_pOutputDock->setObjectName( "OutputDock" );

    m_pOutputEdit = new QPlainTextEdit( m_pOutputDock );
    m_pOutputEdit->setReadOnly( true );
    m_pOutputEdit->setMaximumBlockCount( 1000 );

    m_pOutputDock->setWidget( m_pOutputEdit );
    addDockWidget( Qt::BottomDockWidgetArea, m_pOutputDock );
}

//-----------------------------------------------------------------------------
// Status bar: coordinates + grid size + selection count
//-----------------------------------------------------------------------------
void CMainWindow::CreateStatusBar()
{
    m_pStatusCoords = new QLabel( tr( "0, 0, 0" ), this );
    m_pStatusCoords->setToolTip( tr( "Cursor world position" ) );

    m_pStatusGrid = new QLabel( tr( "Grid: %1 (snap)" ).arg( kDefaultGridSize ), this );
    m_pStatusGrid->setToolTip( tr( "Grid size and snap state" ) );

    m_pStatusSelection = new QLabel( tr( "0 selected" ), this );
    m_pStatusSelection->setToolTip( tr( "Number of selected objects" ) );

    statusBar()->addWidget( m_pStatusCoords );
    statusBar()->addPermanentWidget( m_pStatusGrid );
    statusBar()->addPermanentWidget( m_pStatusSelection );
}

//-----------------------------------------------------------------------------
// Document signal wiring
//-----------------------------------------------------------------------------
void CMainWindow::CreateDocumentConnections()
{
    connect( m_pDocument, &CHammerDocument::fileNameChanged,
        this, &CMainWindow::OnDocumentFileNameChanged );
    connect( m_pDocument, &CHammerDocument::dirtyChanged,
        this, &CMainWindow::OnDocumentDirtyChanged );
    connect( m_pDocument, &CHammerDocument::documentChanged,
        this, &CMainWindow::OnDocumentChanged );
    connect( m_pDocument, &CHammerDocument::selectionChanged,
        this, &CMainWindow::OnSelectionChanged );

    // 2D viewports report the cursor world position for the status bar
    QList<CQtViewport2D *> viewports;
    viewports << m_pViewportTop << m_pViewportFront << m_pViewportSide;
    for ( CQtViewport2D *viewport : viewports )
    {
        connect( viewport, &CQtViewport2D::cursorWorldChanged,
            this, &CMainWindow::OnViewportCursorMoved );
    }
}

//-----------------------------------------------------------------------------
// Purpose: Reads GameConfig.txt next to the executable and loads the FGD it
//          points at. Missing files produce a warning, not a crash.
//-----------------------------------------------------------------------------
void CMainWindow::InitializeGameData()
{
    GameConfigInfo info;
    QString error;
    const QString exeDir = QCoreApplication::applicationDirPath();

    const QStringList selected = QSettings().value( "game/fgdFiles", PortableDefault( exeDir, QStringLiteral( "game/fgdFiles" ) ) ).toStringList();
    if ( !selected.isEmpty() )
    {
        if ( m_pDocument->LoadFgds( selected, &error ) )
        {
            PopulateEntityClasses();
            LogOutput( tr( "Loaded %1 FGD files (%2 point classes)." ).arg( selected.size() ).arg( m_pDocument->Fgd().PointClassNames().count() ) );
            return;
        }
        QMessageBox::warning( this, tr( "Hammer" ), tr( "Failed to load saved FGD files; trying GameConfig.txt.\n%1" ).arg( error ) );
    }

    if ( !LoadGameConfig( exeDir, info, &error ) )
    {
        LogOutput( tr( "GameConfig: %1" ).arg( error ) );
        return;
    }

    LogOutput( info.bCreatedDefault
        ? tr( "GameConfig.txt created with defaults (game '%1')" ).arg( info.gameName )
        : tr( "GameConfig loaded: game '%1'" ).arg( info.gameName ) );

    if ( !QFile::exists( info.fgdPath ) )
    {
        QMessageBox::warning( this, tr( "Hammer" ),
            tr( "The game data file was not found:\n%1\n\n"
                "The editor will start with an empty entity list.\n"
                "Copy your game's FGD files (with their includes) into %2/fgd/ or edit GameConfig.txt next to the executable." )
                .arg( QDir::toNativeSeparators( info.fgdPath ),
                    QDir::toNativeSeparators( exeDir ) ) );
        LogOutput( tr( "FGD not found: %1" ).arg( info.fgdPath ) );
        return;
    }

    if ( m_pDocument->LoadFgds( info.fgdPaths, &error ) )
    {
        PopulateEntityClasses();
        LogOutput( tr( "FGD loaded: %1 (%2 point classes)" )
            .arg( QDir::toNativeSeparators( info.fgdPath ) )
            .arg( m_pDocument->Fgd().PointClassNames().count() ) );
    }
    else
    {
        QMessageBox::warning( this, tr( "Hammer" ),
            tr( "Failed to load the game data file:\n%1\n\n%2" )
                .arg( QDir::toNativeSeparators( info.fgdPath ), error ) );
        LogOutput( tr( "FGD failed: %1" ).arg( error ) );
    }
}

//-----------------------------------------------------------------------------
// Purpose: Fills both entity class combos from the loaded FGD point classes.
//-----------------------------------------------------------------------------
void CMainWindow::PopulateEntityClasses()
{
    const QStringList classes = m_pDocument->Fgd().PointClassNames();
    const QSignalBlocker selectedClassBlocker( m_pEntityClassCombo );

    m_pEntityToolCombo->clear();
    m_pEntityClassCombo->clear();
    m_pEntityToolCombo->addItems( classes );
    m_pEntityClassCombo->addItems( classes );
    m_pEntityClassCombo->addItems( m_pDocument->Fgd().SolidClassNames() );

    // Prefer a sensible default placement class when available
    int defaultIndex = classes.indexOf( "light" );
    if ( defaultIndex < 0 )
        defaultIndex = classes.indexOf( "info_player_start" );
    if ( defaultIndex < 0 && !classes.isEmpty() )
        defaultIndex = 0;
    if ( defaultIndex >= 0 )
        m_pEntityToolCombo->setCurrentIndex( defaultIndex );
    PopulatePropertyTree();
}

//-----------------------------------------------------------------------------
// Purpose: Rebuilds the Object Properties tree from the current selection.
//-----------------------------------------------------------------------------
void CMainWindow::PopulatePropertyTree()
{
    m_bUpdatingProps = true;
    m_pPropertyTree->clear();

    const int id = m_pDocument->SelectedId();
    EntityLite *pEntity = m_pDocument->Map().EntityById( id );
    const bool multipleSelected = m_pDocument->SelectedIds().size() > 1;
    {
        // Refreshing selection must never invoke the class-changing command.
        const QSignalBlocker classBlocker( m_pEntityClassCombo );
        const bool singleEntity = pEntity && !multipleSelected;
        m_pEntityClassCombo->setEnabled( singleEntity );
        if ( singleEntity )
        {
            int index = m_pEntityClassCombo->findText( pEntity->classname, Qt::MatchExactly );
            // Imported custom classes must show their own name, not an unrelated
            // first FGD class, even if the currently loaded FGD lacks the class.
            if ( index < 0 )
            {
                m_pEntityClassCombo->addItem( pEntity->classname );
                index = m_pEntityClassCombo->count() - 1;
            }
            m_pEntityClassCombo->setCurrentIndex( index );
        }
        else
        {
            m_pEntityClassCombo->setPlaceholderText( multipleSelected ? tr( "Multiple objects selected" ) :
                m_pDocument->SelectedIds().isEmpty() ? tr( "Select an entity" ) : tr( "No entity selected" ) );
            m_pEntityClassCombo->setCurrentIndex( -1 );
        }
    }

    auto addRow = [ this ]( const QString &prop, const QString &value, bool bEditable, const QString &key = QString() )
    {
        QTreeWidgetItem *item = new QTreeWidgetItem( m_pPropertyTree );
        item->setText( 0, prop );
        item->setText( 1, value );
        item->setData( 0, Qt::UserRole, key.isEmpty() ? prop : key );
        if ( bEditable )
            item->setFlags( item->flags() | Qt::ItemIsEditable );
        else
            item->setFlags( item->flags() & ~Qt::ItemIsEditable );
        return item;
    };

    if ( multipleSelected )
    {
        QList<const EntityLite *> entities;
        QString sharedClass;
        bool homogeneous = true;
        for ( int selectedId : m_pDocument->SelectedIds() )
        {
            const EntityLite *entity = m_pDocument->Map().EntityById( selectedId );
            if ( !entity || entity->classname.isEmpty() ) { homogeneous = false; break; }
            if ( entities.isEmpty() ) sharedClass = entity->classname;
            else if ( sharedClass.compare( entity->classname, Qt::CaseInsensitive ) != 0 ) { homogeneous = false; break; }
            entities.append( entity );
        }
        if ( !homogeneous || entities.isEmpty() )
            addRow( tr( "Selection" ), tr( "%1 objects selected; properties are unavailable." )
                .arg( m_pDocument->SelectedIds().size() ), false );
        else
        {
            addRow( tr( "Class" ), sharedClass, false );
            // FGD's ClassForName uses strcmp, while class identity in the
            // selection is case-insensitive. Resolve the canonical FGD name
            // independently of QSet iteration order.
            QString schemaClass = sharedClass;
            QStringList knownClasses = m_pDocument->Fgd().PointClassNames();
            knownClasses.append( m_pDocument->Fgd().SolidClassNames() );
            for ( const QString &candidate : knownClasses )
                if ( candidate.compare( sharedClass, Qt::CaseInsensitive ) == 0 ) { schemaClass = candidate; break; }
            const auto schema = m_pDocument->Fgd().ClassPropertyNames( schemaClass );
            const auto rows = EntityPropertyRows( entities.first()->keyvalues, schema );
            for ( const EntityPropertyRow &row : rows )
            {
                bool inSchema = false;
                for ( auto it = schema.cbegin(); it != schema.cend(); ++it )
                    if ( it.key().compare( row.key, Qt::CaseInsensitive ) == 0 ) { inSchema = true; break; }
                bool valid = true, first = true, firstPresent = false, same = true;
                QString firstValue;
                for ( const EntityLite *entity : entities )
                {
                    bool present = false; QString value;
                    for ( auto it = entity->keyvalues.cbegin(); it != entity->keyvalues.cend(); ++it )
                        if ( it.key().compare( row.key, Qt::CaseInsensitive ) == 0 )
                        {
                            if ( present ) { valid = false; break; }
                            present = true; value = it.value();
                        }
                    if ( !valid || ( !present && !inSchema ) ) { valid = false; break; }
                    if ( first ) { firstPresent = present; firstValue = value; first = false; }
                    else if ( firstPresent != present || ( present && firstValue != value ) ) same = false;
                }
                if ( !valid ) continue;
                const bool placeholder = !same || !firstPresent;
                QTreeWidgetItem *item = addRow( row.caption.isEmpty() ? row.key : row.caption,
                    !same ? tr( "<different values>" ) : !firstPresent ? tr( "<not set>" ) : firstValue, true, row.key );
                item->setToolTip( 0, row.key );
                if ( placeholder ) item->setData( 1, Qt::UserRole + 3, true );
                else
                {
                    QVariantList choices;
                    for ( const auto &choice : m_pDocument->Fgd().PropertyChoices( schemaClass, row.key ) )
                        choices.append( QStringList() << choice.first << choice.second );
                    if ( !choices.isEmpty() ) item->setData( 1, Qt::UserRole + 1, choices );
                    QVariantList flags;
                    for ( const auto &flag : m_pDocument->Fgd().PropertyFlags( schemaClass, row.key ) )
                        flags.append( QStringList() << flag.first << flag.second );
                    if ( !flags.isEmpty() ) item->setData( 1, Qt::UserRole + 2, flags );
                }
            }
        }
    }
    else if ( pEntity )
    {
        // Class row is read-only here; class switching goes through the combo
        addRow( tr( "Class" ), pEntity->classname, false );
        addRow( tr( "Origin" ), FormatVec3( pEntity->origin ), true, QStringLiteral( "origin" ) );

        const auto rows = EntityPropertyRows( pEntity->keyvalues,
            m_pDocument->Fgd().ClassPropertyNames( pEntity->classname ) );
        for ( const EntityPropertyRow &row : rows )
        {
            const QString label = row.caption.isEmpty() ? row.key : row.caption;
            QTreeWidgetItem *item = addRow( label, row.value, true, row.key );
            item->setToolTip( 0, row.key );
            QString defaultValue;
            const bool hasDefault = m_pDocument->Fgd().PropertyDefault( pEntity->classname, row.key, defaultValue );
            if ( row.value.isEmpty() && hasDefault )
            {
                item->setToolTip( 1, tr( "Unset; FGD default: %1" ).arg( defaultValue ) );
                item->setForeground( 1, palette().color( QPalette::Disabled, QPalette::Text ) );
            }
            else if ( hasDefault )
            {
                item->setToolTip( 1, tr( "FGD default: %1" ).arg( defaultValue ) );
            }
            QVariantList choices;
            for ( const auto &choice : m_pDocument->Fgd().PropertyChoices( pEntity->classname, row.key ) )
                choices.append( QStringList() << choice.first << choice.second );
            if ( !choices.isEmpty() ) item->setData( 1, Qt::UserRole + 1, choices );
            QVariantList flags;
            for ( const auto &flag : m_pDocument->Fgd().PropertyFlags( pEntity->classname, row.key ) )
                flags.append( QStringList() << flag.first << flag.second );
            if ( !flags.isEmpty() ) item->setData( 1, Qt::UserRole + 2, flags );
        }
    }
    else
    {
        const SolidLite *pSolid = m_pDocument->Map().SolidById( id );
        if ( pSolid )
        {
            addRow( tr( "Type" ), tr( "Brush (solid)" ), false );
            addRow( tr( "Sides" ), QString::number( pSolid->sides.count() ), false );
            addRow( tr( "Bounds" ), QStringLiteral( "%1 .. %2" )
                .arg( FormatVec3( pSolid->mins ), FormatVec3( pSolid->maxs ) ), false );
            addRow( tr( "Texture" ), pSolid->sides.isEmpty()
                ? QString() : pSolid->sides.first().texture, false );
        }
        else
        {
            addRow( tr( "Selection" ), tr( "(nothing selected)" ), false );
        }
    }

    m_pPropertyTree->resizeColumnToContents( 0 );
    m_bUpdatingProps = false;
}

void CMainWindow::UpdateSelectionStatus()
{
    m_pStatusSelection->setText( tr( "%1 selected" ).arg( m_pDocument->SelectedIds().size() ) );
}

//-----------------------------------------------------------------------------
// Recent files
//-----------------------------------------------------------------------------
void CMainWindow::LoadRecentFiles()
{
    QSettings settings;
    m_lstRecentFiles = settings.value( "recent/files" ).toStringList();
    UpdateRecentFilesMenu();
}

void CMainWindow::UpdateRecentFilesMenu()
{
    m_pMenuRecent->clear();

    if ( m_lstRecentFiles.isEmpty() )
    {
        QAction *emptyAction = m_pMenuRecent->addAction( tr( "(no recent files)" ) );
        emptyAction->setEnabled( false );
        return;
    }

    for ( const QString &fileName : m_lstRecentFiles )
    {
        QAction *fileAction = m_pMenuRecent->addAction( QDir::toNativeSeparators( fileName ) );
        fileAction->setData( fileName );
    }
}

void CMainWindow::AddRecentFile( const QString &fileName )
{
    m_lstRecentFiles.removeAll( fileName );
    m_lstRecentFiles.prepend( fileName );

    while ( m_lstRecentFiles.count() > kMaxRecentFiles )
        m_lstRecentFiles.removeLast();

    QSettings settings;
    settings.setValue( "recent/files", m_lstRecentFiles );
    UpdateRecentFilesMenu();
}

//-----------------------------------------------------------------------------
// Layout persistence
//-----------------------------------------------------------------------------
void CMainWindow::SaveLayout()
{
    QSettings settings;
    settings.setValue( "layout/geometry", saveGeometry() );
    settings.setValue( "layout/windowstate", saveState() );
    settings.setValue( "layout/viewGridColumns", m_pMaximizedViewport ? m_viewportSplitterState : m_pViewportSplitter->saveState() );
    settings.setValue( "layout/viewGridLeft", m_pMaximizedViewport ? m_viewLeftSplitterState : m_pViewLeftSplitter->saveState() );
    settings.setValue( "layout/viewGridRight", m_pMaximizedViewport ? m_view2DSplitterState : m_pView2DSplitter->saveState() );
}

void CMainWindow::RestoreLayout()
{
    QSettings settings;

    const QByteArray geometry = settings.value( "layout/geometry" ).toByteArray();
    if ( geometry.isEmpty() || !restoreGeometry( geometry ) )
        resize( kDefaultWindowWidth, kDefaultWindowHeight );

    restoreState( settings.value( "layout/windowstate" ).toByteArray() );

    // The earlier layout used a single 3D pane and three stacked 2D panes.
    // Its splitter state is structurally incompatible with the four-view grid.
    const QByteArray splitterState = settings.value( "layout/viewGridColumns" ).toByteArray();
    if ( !splitterState.isEmpty() )
        m_pViewportSplitter->restoreState( splitterState );
    const QByteArray leftState = settings.value( "layout/viewGridLeft" ).toByteArray();
    if ( !leftState.isEmpty() )
        m_pViewLeftSplitter->restoreState( leftState );
    const QByteArray view2DState = settings.value( "layout/viewGridRight" ).toByteArray();
    if ( !view2DState.isEmpty() )
        m_pView2DSplitter->restoreState( view2DState );
}

bool CMainWindow::eventFilter( QObject *watched, QEvent *event )
{
    if ( event->type() == QEvent::FocusIn || event->type() == QEvent::MouseButtonPress )
    {
        if ( watched == m_pViewport3D || watched == m_pViewportTop ||
             watched == m_pViewportFront || watched == m_pViewportSide )
            m_pLastViewport = static_cast<QWidget *>( watched );
    }
    return QMainWindow::eventFilter( watched, event );
}

//-----------------------------------------------------------------------------
// Small helpers
//-----------------------------------------------------------------------------
void CMainWindow::LogOutput( const QString &message )
{
    if ( !m_pOutputEdit )
        return;

    m_pOutputEdit->appendPlainText( QString( "[%1] %2" )
        .arg( QTime::currentTime().toString( "hh:mm:ss" ), message ) );
}

void CMainWindow::UpdateTitle()
{
    QString title = tr( "Hammer" );
    if ( !m_pDocument->GetFileName().isEmpty() )
        title += " - " + QDir::toNativeSeparators( m_pDocument->GetFileName() );
    else
        title += " - " + tr( "untitled" );

    if ( m_pDocument->IsDirty() )
        title += "*";

    setWindowTitle( title );
}

bool CMainWindow::ConfirmDiscardChanges()
{
    QMessageBox::StandardButton reply = QMessageBox::question(
        this,
        tr( "Hammer" ),
        tr( "The map has been modified. Do you want to save your changes?" ),
        QMessageBox::Save | QMessageBox::Discard | QMessageBox::Cancel );

    if ( reply == QMessageBox::Cancel )
        return false;

    if ( reply == QMessageBox::Save && !m_pDocument->SaveDocument() )
        return false;

    return true;
}

//-----------------------------------------------------------------------------
// File slots
//-----------------------------------------------------------------------------
void CMainWindow::OnNewDocument()
{
    if ( m_pDocument->IsDirty() && !ConfirmDiscardChanges() )
        return;

    if ( m_pDocument->NewDocument() )
        LogOutput( tr( "New map created" ) );
}

void CMainWindow::OnOpenDocument()
{
    if ( m_pDocument->IsDirty() && !ConfirmDiscardChanges() )
        return;

    if ( m_pDocument->OpenDocument() )
    {
        AddRecentFile( m_pDocument->GetFileName() );
        LogOutput( tr( "Opened map: %1" ).arg( m_pDocument->GetFileName() ) );
    }
}

void CMainWindow::OnSaveDocument()
{
    if ( m_pDocument->SaveDocument() )
    {
        AddRecentFile( m_pDocument->GetFileName() );
        LogOutput( tr( "Saved map: %1" ).arg( m_pDocument->GetFileName() ) );
    }
}

void CMainWindow::OnSaveDocumentAs()
{
    if ( m_pDocument->SaveDocumentAs() )
    {
        AddRecentFile( m_pDocument->GetFileName() );
        LogOutput( tr( "Saved map: %1" ).arg( m_pDocument->GetFileName() ) );
    }
}

void CMainWindow::OnRunMap()
{
    const bool cordoned = m_pDocument->Map().IsCordonActive();
    MapDocLite compileMap;
    if ( cordoned && !HammerCordon::BuildCompileMap( m_pDocument->Map(), compileMap ) )
    {
        QMessageBox::warning( this, tr( "Compile Map" ),
            tr( "This map has invalid or unsupported cordon bounds." ) );
        return;
    }
    if ( !m_pDocument->SaveDocument() )
        return;

    QSettings settings;
    const QString gameDir = QFileDialog::getExistingDirectory( this, tr( "Select game directory (containing gameinfo.txt)" ),
        settings.value( "compile/gameDir", PortableDefault( QCoreApplication::applicationDirPath(), QStringLiteral( "compile/gameDir" ) ) ).toString() );
    if ( gameDir.isEmpty() )
        return;
    if ( !QFileInfo::exists( QDir( gameDir ).filePath( "gameinfo.txt" ) ) )
    {
        QMessageBox::warning( this, tr( "Compile Map" ), tr( "The selected directory does not contain gameinfo.txt." ) );
        return;
    }
    const QString binDir = QFileDialog::getExistingDirectory( this, tr( "Select directory containing VBSP, VVIS and VRAD" ),
        settings.value( "compile/binDir", PortableDefault( QCoreApplication::applicationDirPath(), QStringLiteral( "compile/binDir" ), QDir( gameDir ).filePath( "../bin" ) ) ).toString() );
    if ( binDir.isEmpty() )
        return;
    settings.setValue( "compile/gameDir", gameDir );
    settings.setValue( "compile/binDir", binDir );
    m_pViewport3D->setMaterialGameDirectory( gameDir );

    const QFileInfo mapInfo( m_pDocument->GetFileName() );
    std::unique_ptr<QTemporaryDir> cordonDirectory;
    QString inputPath = mapInfo.absoluteFilePath();
    if ( cordoned )
    {
        cordonDirectory = std::make_unique<QTemporaryDir>( mapInfo.dir().filePath( QStringLiteral( ".hammer-cordon-XXXXXX" ) ) );
        QString error;
        if ( cordonDirectory->isValid() ) inputPath = cordonDirectory->filePath( mapInfo.fileName() );
        // VBSP first resolves an instance relative to the input VMF. The
        // cordon copy lives one level deeper, so preserve that first lookup.
        for ( EntityLite &entity : compileMap.entities )
        {
            if ( entity.classname.compare( QLatin1String( "func_instance" ), Qt::CaseInsensitive ) != 0 ) continue;
            auto file = entity.keyvalues.find( QStringLiteral( "file" ) );
            if ( file == entity.keyvalues.end() || file.value().isEmpty() || QDir::isAbsolutePath( file.value() ) ) continue;
            const QFileInfo relative( QDir::fromNativeSeparators( file.value() ) );
            const QString instanceVmf = relative.path() + QLatin1Char( '/' ) + relative.completeBaseName() + QStringLiteral( ".vmf" );
            if ( QFileInfo( mapInfo.dir().filePath( instanceVmf ) ).isFile() )
                file.value() = QStringLiteral( "../" ) + QDir::fromNativeSeparators( file.value() );
        }
        if ( !cordonDirectory->isValid() || !VmfIO::SaveMap( inputPath, compileMap, &error ) )
        {
            QMessageBox::warning( this, tr( "Compile Map" ), tr( "Could not write the temporary cordon map. %1" ).arg( error ) );
            return;
        }
    }
    const QFileInfo compileInfo( inputPath );
    const QString bspPath = compileInfo.dir().filePath( mapInfo.completeBaseName() + ".bsp" );
    const QString leakPath = compileInfo.dir().filePath( mapInfo.completeBaseName() + ".lin" );
    const QString destination = QDir( gameDir ).filePath( "maps/" + mapInfo.completeBaseName() + ".bsp" );
    if ( QFileInfo::exists( destination ) && QFileInfo( bspPath ).absoluteFilePath() != QFileInfo( destination ).absoluteFilePath() &&
         QMessageBox::question( this, tr( "Compile Map" ), tr( "Replace the existing map at %1?" ).arg( destination ) ) != QMessageBox::Yes )
        return;

    const QStringList tools = { "vbsp", "vvis", "vrad" };
    const PointFileState pointFileBefore = CapturePointFileState( leakPath );
    QProgressDialog progress( tr( "Compiling map..." ), tr( "Cancel" ), 0, tools.size(), this );
    progress.setWindowModality( Qt::WindowModal );
    progress.setMinimumDuration( 0 );
    for ( int i = 0; i < tools.size(); ++i )
    {
        QString executable = QDir( binDir ).filePath( tools[i] );
#ifdef Q_OS_WIN
        executable += ".exe";
#endif
        if ( !QFileInfo::exists( executable ) )
        {
            QMessageBox::warning( this, tr( "Compile Map" ), tr( "Compiler not found: %1" ).arg( executable ) );
            return;
        }
        progress.setLabelText( tr( "Running %1..." ).arg( tools[i] ) );
        progress.setValue( i );
        QProcess process;
        process.setWorkingDirectory( binDir );
        process.setProcessChannelMode( QProcess::MergedChannels );
        process.start( executable, QStringList() << "-game" << gameDir << ( i == 0 ? inputPath : bspPath ) );
        if ( !process.waitForStarted() )
        {
            QMessageBox::warning( this, tr( "Compile Map" ), tr( "Could not start %1: %2" ).arg( executable, process.errorString() ) );
            return;
        }
        while ( !process.waitForFinished( 100 ) )
        {
            QCoreApplication::processEvents();
            if ( progress.wasCanceled() )
            {
                process.kill();
                process.waitForFinished();
                LogOutput( tr( "Map compilation cancelled." ) );
                return;
            }
        }
        const QString compilerOutput = QString::fromLocal8Bit( process.readAll() );
        LogOutput( compilerOutput );
        // VBSP can return success even when the map leaks; VVIS and VRAD
        // must not run on that incomplete result.
        const bool leaked = i == 0 && compilerOutput.contains( "**** leaked ****", Qt::CaseInsensitive );
        bool loadedLeak = false;
        if ( leaked )
        {
            int errorLine = 0;
            QList<PointFilePoint> leakPoints;
            if ( IsFreshPointFile( pointFileBefore, CapturePointFileState( leakPath ) ) &&
                 ReadPointFile( leakPath, leakPoints, &errorLine ) && !leakPoints.isEmpty() &&
                 m_pDocument->LoadPointFile( leakPath, &errorLine ) )
            {
                loadedLeak = true;
                LogOutput( tr( "Leak pointfile loaded: %1" ).arg( leakPath ) );
            }
            else
                LogOutput( tr( "Could not load a newly generated leak pointfile. Use Map > Load Pointfile to select one." ) );
        }
        if ( process.exitStatus() != QProcess::NormalExit || process.exitCode() != 0 || leaked )
        {
            QMessageBox::warning( this, tr( "Compile Map" ), leaked ?
                ( loadedLeak ? tr( "The map leaked. The leak path is displayed in the viewports." ) :
                  tr( "The map leaked, but its new pointfile could not be loaded. See Output for details." ) ) :
                tr( "%1 failed. See Output for details." ).arg( tools[i] ) );
            return;
        }
    }
    progress.setValue( tools.size() );
    if ( QFileInfo( bspPath ).absoluteFilePath() != QFileInfo( destination ).absoluteFilePath() )
    {
        QDir().mkpath( QFileInfo( destination ).absolutePath() );
        QFile source( bspPath );
        QSaveFile output( destination );
        if ( !source.open( QIODevice::ReadOnly ) || !output.open( QIODevice::WriteOnly ) ||
             output.write( source.readAll() ) != source.size() || !output.commit() )
        {
            QMessageBox::warning( this, tr( "Compile Map" ), tr( "Could not copy the compiled map to %1." ).arg( destination ) );
            return;
        }
    }
    LogOutput( tr( "Compiled map: %1" ).arg( destination ) );
    QMessageBox::information( this, tr( "Compile Map" ), tr( "Map compiled to %1. Start the game and load %2 from the console." )
        .arg( destination, mapInfo.completeBaseName() ) );
}

void CMainWindow::OnRecentTriggered( QAction *action )
{
    const QString fileName = action->data().toString();
    if ( fileName.isEmpty() )
        return;

    if ( m_pDocument->IsDirty() && !ConfirmDiscardChanges() )
        return;

    if ( m_pDocument->LoadFromFile( fileName ) )
    {
        AddRecentFile( fileName );
        LogOutput( tr( "Opened map: %1" ).arg( fileName ) );
    }
    else
    {
        QMessageBox::warning( this, tr( "Hammer" ), tr( "Failed to open map: %1\n%2" ).arg( fileName, m_pDocument->LastError() ) );
    }
}

//-----------------------------------------------------------------------------
// Edit slots
//-----------------------------------------------------------------------------
void CMainWindow::OnUndo()
{
    if ( m_pDocument->Undo() )
        LogOutput( tr( "Undo" ) );
}

void CMainWindow::OnRedo()
{
    if ( m_pDocument->Redo() )
        LogOutput( tr( "Redo" ) );
}

void CMainWindow::OnCut()
{
    if ( m_pDocument->CutSelection() )
        LogOutput( tr( "Cut selection" ) );
}

void CMainWindow::OnCopy()
{
    if ( m_pDocument->CopySelection() )
        LogOutput( tr( "Copied selection" ) );
}

void CMainWindow::OnPaste()
{
    if ( m_pDocument->ClipboardHasForeignVisgroupState() )
    {
        QMessageBox::warning( this, tr( "Paste" ), tr( "Cannot paste objects with visgroup or hidden metadata into a different map." ) );
        return;
    }
    if ( m_pDocument->PasteSelection() >= 0 )
        LogOutput( tr( "Pasted selection" ) );
}

void CMainWindow::OnDelete()
{
    if ( m_pDocument->DeleteSelection() )
        LogOutput( tr( "Deleted selection" ) );
}

//-----------------------------------------------------------------------------
// Selection slots
//-----------------------------------------------------------------------------
void CMainWindow::OnSelectAll()
{
    m_pDocument->SelectAll();
    LogOutput( tr( "Selected all objects" ) );
}

void CMainWindow::OnSelectNone()
{
    m_pDocument->SelectObject( -1 );
    LogOutput( tr( "Selection cleared" ) );
}

void CMainWindow::OnSelectInvert()
{
    m_pDocument->InvertSelection();
    LogOutput( tr( "Inverted selection" ) );
}

void CMainWindow::OnSelectByProperty()
{
    QDialog dialog( this );
    dialog.setWindowTitle( tr( "Select by Property" ) );
    QVBoxLayout *layout = new QVBoxLayout( &dialog );
    QFormLayout *form = new QFormLayout;
    QLineEdit *key = new QLineEdit( &dialog );
    QLineEdit *value = new QLineEdit( &dialog );
    key->setPlaceholderText( tr( "Any property" ) );
    form->addRow( tr( "Property key:" ), key );
    form->addRow( tr( "Value:" ), value );
    layout->addLayout( form );
    QCheckBox *exact = new QCheckBox( tr( "Exact value" ), &dialog );
    layout->addWidget( exact );
    QDialogButtonBox *buttons = new QDialogButtonBox(
        QDialogButtonBox::Ok | QDialogButtonBox::Cancel, &dialog );
    buttons->button( QDialogButtonBox::Ok )->setEnabled( false );
    connect( value, &QLineEdit::textChanged, &dialog, [buttons]( const QString &text ) {
        buttons->button( QDialogButtonBox::Ok )->setEnabled( !text.isEmpty() );
    } );
    connect( buttons, &QDialogButtonBox::accepted, &dialog, &QDialog::accept );
    connect( buttons, &QDialogButtonBox::rejected, &dialog, &QDialog::reject );
    layout->addWidget( buttons );
    if ( dialog.exec() == QDialog::Accepted )
    {
        const int count = m_pDocument->SelectEntitiesByProperty( key->text(), value->text(), exact->isChecked() );
        LogOutput( tr( "Selected %1 entities by property" ).arg( count ) );
    }
}

//-----------------------------------------------------------------------------
// View slots
//-----------------------------------------------------------------------------
void CMainWindow::OnMaximizeViewport()
{
    if ( m_pMaximizedViewport )
    {
        RestoreMaximizedViewport();
        return;
    }
    if ( !m_pLastViewport )
        return;
    m_viewportSplitterState = m_pViewportSplitter->saveState();
    m_viewLeftSplitterState = m_pViewLeftSplitter->saveState();
    m_view2DSplitterState = m_pView2DSplitter->saveState();
    m_pMaximizedViewport = m_pLastViewport;
    m_pViewport3D->setVisible( m_pMaximizedViewport == m_pViewport3D );
    m_pViewportTop->setVisible( m_pMaximizedViewport == m_pViewportTop );
    m_pViewportFront->setVisible( m_pMaximizedViewport == m_pViewportFront );
    m_pViewportSide->setVisible( m_pMaximizedViewport == m_pViewportSide );
    m_pViewLeftSplitter->setVisible( m_pMaximizedViewport == m_pViewport3D || m_pMaximizedViewport == m_pViewportFront );
    m_pView2DSplitter->setVisible( m_pMaximizedViewport == m_pViewportTop || m_pMaximizedViewport == m_pViewportSide );
    m_pMaximizedViewport->setFocus();
}

void CMainWindow::RestoreMaximizedViewport()
{
    if ( !m_pMaximizedViewport )
        return;
    QWidget *previous = m_pMaximizedViewport;
    m_pMaximizedViewport = nullptr;
    m_pViewLeftSplitter->show();
    m_pView2DSplitter->show();
    m_pViewport3D->show();
    m_pViewportTop->show();
    m_pViewportFront->show();
    m_pViewportSide->show();
    m_pViewportSplitter->restoreState( m_viewportSplitterState );
    m_pViewLeftSplitter->restoreState( m_viewLeftSplitterState );
    m_pView2DSplitter->restoreState( m_view2DSplitterState );
    previous->setFocus();
}

void CMainWindow::OnFilter()
{
    // View-only object filter; visgroups and entity categories are not modelled yet.
    const ViewFilter current = m_pDocument->GetViewFilter();
    QDialog dialog( this );
    dialog.setObjectName( "viewFilterDialog" );
    dialog.setWindowTitle( tr( "Filter" ) );
    QVBoxLayout *layout = new QVBoxLayout( &dialog );
    QCheckBox *showBrushes = new QCheckBox( tr( "Show world brushes" ), &dialog );
    showBrushes->setObjectName( "viewFilterBrushes" );
    showBrushes->setChecked( current.showBrushes );
    QCheckBox *showEntities = new QCheckBox( tr( "Show point entities" ), &dialog );
    showEntities->setObjectName( "viewFilterEntities" );
    showEntities->setChecked( current.showEntities );
    QCheckBox *showNames = new QCheckBox( tr( "Show entity names" ), &dialog );
    showNames->setObjectName( "viewFilterNames" );
    showNames->setChecked( current.showEntityNames );
    QLineEdit *classFilter = new QLineEdit( current.classFilter, &dialog );
    classFilter->setObjectName( "viewFilterClass" );
    classFilter->setPlaceholderText( tr( "All classes" ) );
    layout->addWidget( showBrushes );
    layout->addWidget( showEntities );
    layout->addWidget( showNames );
    QFormLayout *form = new QFormLayout;
    form->addRow( tr( "Only entities whose class contains:" ), classFilter );
    layout->addLayout( form );
    layout->addWidget( new QLabel( tr( "The filter only affects the views; hidden objects are not selectable but are still saved." ), &dialog ) );
    QDialogButtonBox *buttons = new QDialogButtonBox( QDialogButtonBox::Ok | QDialogButtonBox::Cancel | QDialogButtonBox::Reset, &dialog );
    layout->addWidget( buttons );
    connect( buttons, &QDialogButtonBox::accepted, &dialog, &QDialog::accept );
    connect( buttons, &QDialogButtonBox::rejected, &dialog, &QDialog::reject );
    connect( buttons->button( QDialogButtonBox::Reset ), &QPushButton::clicked, &dialog, [=] {
        showBrushes->setChecked( true );
        showEntities->setChecked( true );
        showNames->setChecked( true );
        classFilter->clear();
    } );
    if ( dialog.exec() != QDialog::Accepted )
        return;
    ViewFilter filter = current;
    filter.showBrushes = showBrushes->isChecked();
    filter.showEntities = showEntities->isChecked();
    filter.showEntityNames = showNames->isChecked();
    filter.classFilter = classFilter->text().trimmed();
    m_pDocument->SetViewFilter( filter );
}

void CMainWindow::OnRestoreDefaultLayout()
{
    RestoreMaximizedViewport();
    QSettings settings;
    settings.remove( "layout/geometry" );
    settings.remove( "layout/windowstate" );
    settings.remove( "layout/splitter" );
    settings.remove( "layout/splitter2d" );
    settings.remove( "layout/viewGridColumns" );
    settings.remove( "layout/viewGridLeft" );
    settings.remove( "layout/viewGridRight" );

    addDockWidget( Qt::LeftDockWidgetArea, m_pTextureBrowserDock );
    addDockWidget( Qt::RightDockWidgetArea, m_pObjectPropertiesDock );
    addDockWidget( Qt::BottomDockWidgetArea, m_pOutputDock );

    m_pTextureBrowserDock->setVisible( true );
    m_pObjectPropertiesDock->setVisible( true );
    m_pOutputDock->setVisible( true );

    m_pViewportSplitter->setSizes( QList<int>() << 1 << 1 );
    m_pViewLeftSplitter->setSizes( QList<int>() << 1 << 1 );
    m_pView2DSplitter->setSizes( QList<int>() << 1 << 1 );
    resize( kDefaultWindowWidth, kDefaultWindowHeight );

    LogOutput( tr( "Layout restored to defaults" ) );
}

//-----------------------------------------------------------------------------
// Map slots
//-----------------------------------------------------------------------------
void CMainWindow::OnSnapToGrid( bool checked )
{
    m_pDocument->SetSnapEnabled( checked );
    m_pStatusGrid->setText( tr( "Grid: %1%2" )
        .arg( m_pDocument->GridSize() )
        .arg( checked ? tr( " (snap)" ) : QString() ) );
    LogOutput( tr( "Snap to Grid: %1" ).arg( checked ? tr( "on" ) : tr( "off" ) ) );
}

void CMainWindow::OnGridSettings()
{
    // Only the power-of-two sizes the document and View > Grid Size support.
    QDialog dialog( this );
    dialog.setWindowTitle( tr( "Grid Settings" ) );
    QVBoxLayout *layout = new QVBoxLayout( &dialog );
    QFormLayout *form = new QFormLayout;
    QComboBox *sizeBox = new QComboBox( &dialog );
    const GridSettingsState current = CurrentGridSettings( *m_pDocument );
    for ( int i = 0; i < kGridSizeCount; ++i )
        sizeBox->addItem( QString::number( kGridSizes[i] ), kGridSizes[i] );
    const int currentIndex = sizeBox->findData( current.size );
    if ( currentIndex >= 0 ) sizeBox->setCurrentIndex( currentIndex );
    form->addRow( tr( "Grid spacing:" ), sizeBox );
    QCheckBox *showBox = new QCheckBox( tr( "Show grid" ), &dialog );
    showBox->setChecked( current.visible );
    QCheckBox *snapBox = new QCheckBox( tr( "Snap to grid" ), &dialog );
    snapBox->setChecked( current.snap );
    form->addRow( showBox );
    form->addRow( snapBox );
    layout->addLayout( form );
    QDialogButtonBox *buttons = new QDialogButtonBox( QDialogButtonBox::Ok | QDialogButtonBox::Cancel, &dialog );
    connect( buttons, &QDialogButtonBox::accepted, &dialog, &QDialog::accept );
    connect( buttons, &QDialogButtonBox::rejected, &dialog, &QDialog::reject );
    layout->addWidget( buttons );
    if ( dialog.exec() != QDialog::Accepted ) return;

    GridSettingsState state;
    state.size = sizeBox->currentData().toInt();
    state.visible = showBox->isChecked();
    state.snap = snapBox->isChecked();
    if ( ApplyGridSettings( *m_pDocument, state ) )
        LogOutput( tr( "Grid settings: spacing %1, grid %2, snap %3" ).arg( state.size )
            .arg( state.visible ? tr( "shown" ) : tr( "hidden" ) )
            .arg( state.snap ? tr( "on" ) : tr( "off" ) ) );
}

void CMainWindow::OnGridSizeChanged( QAction *action )
{
    const int size = action->data().toInt();
    m_pDocument->SetGridSize( size );
    m_pStatusGrid->setText( tr( "Grid: %1%2" )
        .arg( size )
        .arg( m_pDocument->SnapEnabled() ? tr( " (snap)" ) : QString() ) );
    LogOutput( tr( "Grid size: %1" ).arg( size ) );
}

void CMainWindow::OnEntityReport()
{
    if ( !m_pEntityReport )
    {
        m_pEntityReport = new CEntityReportDialog( m_pDocument, this, [this]( int result, const QSet<int> &ids ) {
            m_pDocument->SelectObjects( ids );
            if ( result == CEntityReportDialog::PropertiesResult && !m_pDocument->SelectedIds().isEmpty() )
            {
                m_pObjectPropertiesDock->show();
                m_pObjectPropertiesDock->raise();
            }
            if ( result == CEntityReportDialog::GoToResult )
            {
                double center[3];
                if ( !SelectionCenter( *m_pDocument, center ) ) return;
                m_pViewportTop->centerOn( center ); m_pViewportFront->centerOn( center );
                m_pViewportSide->centerOn( center ); m_pViewport3D->centerOn( center );
            }
        } );
        m_pEntityReport->setObjectName( "entityReportWindow" );
    }
    m_pEntityReport->show();
    m_pEntityReport->raise();
    m_pEntityReport->activateWindow();
}

void CMainWindow::OnMapProperties()
{
    CMapPropertiesDialog dialog( m_pDocument->Map().worldspawn,
        m_pDocument->Fgd().ClassPropertyNames( QStringLiteral( "worldspawn" ) ), this, &m_pDocument->Fgd() );
    if ( dialog.exec() == QDialog::Accepted )
    {
        const QMap<QString, QString> properties = dialog.Properties();
        if ( properties == m_pDocument->Map().worldspawn )
            return;
        if ( m_pDocument->SetWorldProperties( properties ) )
            LogOutput( tr( "Map properties updated" ) );
        else
            QMessageBox::warning( this, tr( "Map Properties" ),
                tr( "Could not apply map properties. Check for duplicate keys or line breaks." ) );
    }
}

//-----------------------------------------------------------------------------
// Tools slots
//-----------------------------------------------------------------------------
void CMainWindow::OnToolModeChanged( QAction *action )
{
    const QString id = action->data().toString();

    if ( id == QLatin1String( "select" ) )
        m_pDocument->SetTool( TOOL_SELECT );
    else if ( id == QLatin1String( "camera" ) )
        m_pDocument->SetTool( TOOL_CAMERA );
    else if ( id == QLatin1String( "entity" ) )
        m_pDocument->SetTool( TOOL_ENTITY );
    else if ( id == QLatin1String( "block" ) )
        m_pDocument->SetTool( TOOL_BLOCK );
    else if ( id == QLatin1String( "texture" ) )
        m_pDocument->SetTool( TOOL_TEXTURE );
    else if ( id == QLatin1String( "decal" ) )
        m_pDocument->SetTool( TOOL_DECAL );
    else if ( id == QLatin1String( "vertex" ) )
        m_pDocument->SetTool( TOOL_VERTEX );
    else if ( id == QLatin1String( "clip" ) )
    {
        if ( m_pDocument->GetTool() == TOOL_CLIP )
        {
            CQtViewport2D *view = qobject_cast<CQtViewport2D *>( m_pLastViewport );
            if ( !view ) view = m_pViewportTop;
            view->cycleClipMode();
        }
        else m_pDocument->SetTool( TOOL_CLIP );
    }
    LogOutput( tr( "Tool mode: %1" ).arg( action->text() ) );
}

void CMainWindow::OnLanguageTriggered( QAction *action )
{
    if ( !action )
        return;
    HammerLocalization *localization = QCoreApplication::instance()->findChild<HammerLocalization *>();
    if ( !localization )
        return;

    const QString locale = action->data().toString();
    if ( locale == localization->CurrentLanguage() )
        return;
    const auto restoreCheckedLanguage = [action, localization]() {
        if ( QActionGroup *group = action->actionGroup() )
        {
            for ( QAction *entry : group->actions() )
                if ( entry->data().toString() == localization->CurrentLanguage() ) entry->setChecked( true );
        }
    };
    if ( m_pDocument->IsDirty() && !ConfirmDiscardChanges() )
    {
        restoreCheckedLanguage();
        return;
    }

    const QString oldLocale = localization->CurrentLanguage();
    if ( !localization->LoadLanguage( locale ) )
    {
        restoreCheckedLanguage();
        QMessageBox::warning( this, tr( "Hammer" ), tr( "Could not load the selected language catalog." ) );
        return;
    }

    QStringList arguments;
    if ( !m_pDocument->GetFileName().isEmpty() )
        arguments << "--open" << m_pDocument->GetFileName();
    if ( !QProcess::startDetached( QCoreApplication::applicationFilePath(), arguments,
            QCoreApplication::applicationDirPath() ) )
    {
        localization->LoadLanguage( oldLocale );
        restoreCheckedLanguage();
        QMessageBox::warning( this, tr( "Hammer" ), tr( "Could not restart Hammer to apply the language." ) );
        return;
    }
    // The same unsaved edits were already handled above. Avoid prompting a
    // second time after the replacement process has successfully started.
    m_bLanguageRestartConfirmed = true;
    close();
}

//-----------------------------------------------------------------------------
// Purpose: Toolbar entity class combo drives the Entity tool placement class.
//-----------------------------------------------------------------------------
void CMainWindow::OnEntityToolChanged( const QString &className )
{
    m_pDocument->SetEntityClass( className );
    LogOutput( tr( "Entity tool class: %1" ).arg( className ) );
}

//-----------------------------------------------------------------------------
// Purpose: Dock combo changes the class of the selected entity and refills
//          its key/values with the FGD defaults of the new class.
//-----------------------------------------------------------------------------
void CMainWindow::OnSelectedEntityClassChanged( const QString &className )
{
    if ( m_bUpdatingProps || !m_pEntityClassCombo->isEnabled() || className.isEmpty() || m_pDocument->SelectedIds().size() != 1 )
        return;

    if ( m_pDocument->SetSelectedEntityClass( className ) )
        LogOutput( tr( "Entity class changed to %1" ).arg( className ) );
}

//-----------------------------------------------------------------------------
// Purpose: Object Properties cell edit write-back into the model.
//-----------------------------------------------------------------------------
void CMainWindow::OnPropertyItemChanged( QTreeWidgetItem *item, int column )
{
    if ( m_bUpdatingProps || column != 1 || !item )
        return;

    if ( m_pDocument->SelectedIds().size() > 1 )
    {
        const QString prop = item->data( 0, Qt::UserRole ).toString();
        if ( m_pDocument->SetSelectedEntitiesKey( prop, item->text( 1 ) ) )
            QTimer::singleShot( 0, this, &CMainWindow::PopulatePropertyTree );
        return;
    }

    EntityLite *pEntity = m_pDocument->Map().EntityById( m_pDocument->SelectedId() );
    if ( !pEntity )
        return;

    const QString prop = item->data( 0, Qt::UserRole ).toString();
    const QString value = item->text( 1 );

    if ( prop.compare( QLatin1String( "origin" ), Qt::CaseInsensitive ) == 0 )
    {
        double newOrigin[3];
        if ( !ParseVec3( value, newOrigin ) )
        {
            QMessageBox::warning( this, tr( "Hammer" ),
                tr( "Origin must be three numbers separated by spaces (\"x y z\")." ) );
            return;
        }

        m_pDocument->SetSelectedEntityOrigin( newOrigin );
    }
    else
    {
        // Plain key/value edit: write into the model without rebuilding the
        // tree (keeps the editor cell focused)
        m_pDocument->SetSelectedEntityKey( prop, value );
    }
}

//-----------------------------------------------------------------------------
// Document callbacks
//-----------------------------------------------------------------------------
void CMainWindow::OnDocumentFileNameChanged( const QString &fileName )
{
    Q_UNUSED( fileName );
    UpdateTitle();
}

void CMainWindow::OnDocumentDirtyChanged( bool dirty )
{
    Q_UNUSED( dirty );
    if ( m_pRecoveryTimer )
    {
        if ( m_pDocument->IsDirty() ) m_pRecoveryTimer->start();
        else { m_pRecoveryTimer->stop(); m_recoveryStore.Clear(); }
    }
    UpdateTitle();
}

void CMainWindow::OnDocumentChanged()
{
    if ( m_pDocument->IsDirty() && m_pRecoveryTimer ) m_pRecoveryTimer->start();
    UpdateSelectionStatus();
    // Queued so a rebuild never runs inside the itemChanged handler stack
    QTimer::singleShot( 0, this, &CMainWindow::PopulatePropertyTree );
}

void CMainWindow::OnSelectionChanged( int id )
{
    Q_UNUSED( id );
    UpdateSelectionStatus();
    PopulatePropertyTree();
}

void CMainWindow::OnViewportCursorMoved( const QString &text )
{
    m_pStatusCoords->setText( text );
}

//-----------------------------------------------------------------------------
// Help slots
//-----------------------------------------------------------------------------
void CMainWindow::OnAbout()
{
    QMessageBox::about( this, tr( "About Hammer" ),
        tr( "Hammer for Source - Qt6 rewrite\n"
            "Qt %1\n\n"
            "Lightweight map editing: FGD-based entity placement, block tool,\n"
            "VMF save/load. Run with --selftest for automated verification." )
            .arg( QT_VERSION_STR ) );
}
