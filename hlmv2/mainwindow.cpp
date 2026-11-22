//========= Half-Life Model Viewer 2 (Qt6 port of utils/hlmv) ================
//
// Purpose: Main window implementation. The panel population mirrors the
//          original viewer's ControlPanel content (sequences, bodygroups,
//          skins, bones, attachments, materials, physics), but reads the data
//          through CStudioModelData instead of the materialsystem stack.
//
//=============================================================================

#include "mainwindow.h"

#include "localization.h"
#include "materialloader.h"
#include "studiodata.h"
#include "viewport.h"

#include <QAction>
#include <QActionGroup>
#include <QCloseEvent>
#include <QComboBox>
#include <QCoreApplication>
#include <QDesktopServices>
#include <QDockWidget>
#include <QGuiApplication>
#include <QScreen>
#include <QSignalBlocker>
#include <QLayout>
#include <QFileDialog>
#include <QFileInfo>
#include <QFrame>
#include <QHBoxLayout>
#include <QIcon>
#include <QImage>
#include <QLabel>
#include <QListWidget>
#include <QListWidgetItem>
#include <QMenu>
#include <QMenuBar>
#include <QDir>
#include <QMessageBox>
#include <QPushButton>
#include <QRadioButton>
#include <QSettings>
#include <QScrollArea>
#include <QSize>
#include <QSlider>
#include <QStatusBar>
#include <QTabWidget>
#include <QTimer>
#include <QToolBar>
#include <QTreeWidget>
#include <QTreeWidgetItem>
#include <QUrl>
#include <QVBoxLayout>
#include <QCheckBox>
#include <QGridLayout>
#include <QGroupBox>
#include <QLineEdit>

#ifdef _WIN32
static const char kModelDialogFilter[] =
	"Studio models (*.mdl);;All files (*.*)";
#else
static const char kModelDialogFilter[] =
	"Studio models (*.mdl);;All files (*)";
#endif

// Icons for the Qt front end are all drawn by res/gen_icon.py and embedded
// through res/hlmv2.qrc. Registering all three sizes keeps menus and toolbar
// crisp on high-DPI displays without touching a system icon theme.
static QIcon HlmvIcon( const char *pszName )
{
	QIcon icon;
	for ( int nSize : { 16, 24, 32 } )
	{
		icon.addFile( QStringLiteral( ":/icons/%1-%2.png" )
			.arg( QLatin1String( pszName ) ).arg( nSize ), QSize( nSize, nSize ) );
	}
	return icon;
}

//-----------------------------------------------------------------------------
// Purpose: Construction/destruction
//-----------------------------------------------------------------------------
CMainWindow::CMainWindow( QWidget *parent )
	: QMainWindow( parent )
{
	CreateMenus();
	CreateCentralWidget();
	CreateDockWidgets();
	CreateStatusBar();

	// 30 Hz animation clock; the viewport repaints on every tick.
	m_pAnimTimer = new QTimer( this );
	connect( m_pAnimTimer, &QTimer::timeout, this, &CMainWindow::OnAnimationTick );
	m_AnimClock.start();

	UpdateTitle();
	{
		QSize size( 1024, 768 );
		if ( QScreen *pScreen = QGuiApplication::primaryScreen() )
			size = size.boundedTo( pScreen->availableGeometry().size() * 9 / 10 );
		resize( size );
	}

	// The floating Control Panel gets its own window geometry, like the stock
	// viewer's separate ControlPanel window.
	// The dock split has to be applied once the window is on screen: doing it
	// in the constructor lets Qt park the Control Panel outside the visible
	// area, and the panel then never appears.
	QTimer::singleShot( 0, this, [this]()
	{
		resizeDocks( { m_pControlPanelDock }, { 330 }, Qt::Vertical );
		m_pControlPanelDock->show();

		// Restore the Control Panel mode the user last chose (docked by
		// default, so nothing pops up over the viewport at every start).
		QSettings settings;
		const bool bFloating = settings.value(
			QStringLiteral( "controlPanel/floating" ), false ).toBool();
		if ( m_pActionControlPanel )
			m_pActionControlPanel->setChecked( bFloating );
		else
			SetControlPanelFloating( bFloating );
	} );
}

CMainWindow::~CMainWindow()
{
	// The viewport is a child widget and outlives this destructor body, so it
	// must stop referencing the models before they are deleted.
	if ( m_pAnimTimer )
		m_pAnimTimer->stop();
	if ( m_pViewport )
	{
		m_pViewport->SetHandsModel( nullptr );
		m_pViewport->SetModel( nullptr );
	}
	delete m_pHandsModel;
	m_pHandsModel = nullptr;
	delete m_pModel;
	m_pModel = nullptr;
}

//-----------------------------------------------------------------------------
// Purpose: Menu bar, mirroring stock HLMV's File / Options / View / Help.
//          Entries the port does not implement yet are present but disabled,
//          so the layout stays faithful instead of inventing substitutes.
//-----------------------------------------------------------------------------
void CMainWindow::CreateMenus()
{
	// ---------- File (mdlviewer.cpp:236-308) ----------
	QMenu *pFile = menuBar()->addMenu( tr( "&File" ) );
	// Stock HLMV has no accelerator on Load Model; Ctrl+O belongs to "Bones".
	QAction *pOpen = pFile->addAction( tr( "&Load Model..." ) );
	pOpen->setIcon( HlmvIcon( "open" ) );
	connect( pOpen, &QAction::triggered, this, &CMainWindow::OnOpenModel );
	QAction *pRefresh = pFile->addAction( tr( "Refresh (F5)" ),
		QKeySequence( QStringLiteral( "F5" ) ) );
	pRefresh->setIcon( HlmvIcon( "refresh" ) );
	connect( pRefresh, &QAction::triggered, this, [this]()
	{
		if ( !m_sCurrentModelPath.isEmpty() )
			OpenModel( m_sCurrentModelPath );
	} );
	pFile->addSeparator();

	QAction *pLoadWeapon = pFile->addAction( tr( "Load &Weapon..." ) );
	pLoadWeapon->setIcon( HlmvIcon( "weapon" ) );
	// Merging needs a main model whose bones the weapon is matched against.
	pLoadWeapon->setEnabled( false );
	m_pActionLoadWeapon = pLoadWeapon;
	connect( pLoadWeapon, &QAction::triggered, this, [this]()
	{
		if ( !m_pModel )
			return;
		const QString dir = m_sCurrentModelPath.isEmpty()
			? QString() : QFileInfo( m_sCurrentModelPath ).absolutePath();
		const QString path = QFileDialog::getOpenFileName( this, tr( "Load Weapon" ),
			dir, QLatin1String( kModelDialogFilter ) );
		if ( !path.isEmpty() )
			AttachHandsModel( path );
	} );

	// The original File menu has an "Unload Weapon" submenu with twelve
	// numbered slots (one per possible merged model).
	QMenu *pUnload = pFile->addMenu( tr( "Unload &Weapon" ) );
	m_pActionUnloadWeapon = pUnload->addAction(
		tr( "Unload All Merged Models (Ctrl+U)" ),
		QKeySequence( QStringLiteral( "Ctrl+U" ) ) );
	m_pActionUnloadWeapon->setIcon( HlmvIcon( "weapon" ) );
	m_pActionUnloadWeapon->setEnabled( false );
	connect( m_pActionUnloadWeapon, &QAction::triggered, this, [this]()
	{
		DetachHandsModel();
	} );
	for ( int i = 1; i <= 12; ++i )
	{
		QAction *pSlot = pUnload->addAction(
			tr( "(empty)" ) + QStringLiteral( " %1" ).arg( i ) );
		pSlot->setEnabled( false );
	}
	pFile->addSeparator();

	for ( const char *label : { QT_TR_NOOP( "Load Background Texture..." ),
		QT_TR_NOOP( "Load Ground Texture..." ), QT_TR_NOOP( "Unload Ground Texture" ) } )
	{
		QAction *pAction = pFile->addAction( tr( label ) );
		pAction->setEnabled( false );
	}
	pFile->addSeparator();
	m_pMenuRecent = pFile->addMenu( tr( "Recent &Models" ) );
	UpdateRecentMenu();
	pFile->addSeparator();
	QAction *pExit = pFile->addAction( tr( "E&xit" ), QKeySequence::Quit );
	connect( pExit, &QAction::triggered, this, &CMainWindow::OnExit );

	// ---------- Options (mdlviewer.cpp:314-324) ----------
	QMenu *pOptions = menuBar()->addMenu( tr( "&Options" ) );
	for ( const char *label : { QT_TR_NOOP( "Background Color..." ), QT_TR_NOOP( "Ground Color..." ),
		QT_TR_NOOP( "Light Color..." ), QT_TR_NOOP( "Ambient Color..." ) } )
	{
		QAction *pAction = pOptions->addAction( tr( label ) );
		pAction->setEnabled( false );
	}
	pOptions->addSeparator();
	QAction *pCenter = pOptions->addAction( tr( "&Center View" ) );
	pCenter->setIcon( HlmvIcon( "center" ) );
	connect( pCenter, &QAction::triggered, this, [this]()
	{
		if ( m_pViewport )
			m_pViewport->FrameModel();
	} );
	QAction *pViewmodel = pOptions->addAction( tr( "&Viewmodel Mode" ) );
	pViewmodel->setIcon( HlmvIcon( "viewmodel" ) );
	connect( pViewmodel, &QAction::triggered, this, [this]()
	{
		if ( m_pViewport )
			m_pViewport->SetCameraPreset( CModelViewport::CamViewmodel );
	} );
	pOptions->addSeparator();
	QAction *pShot = pOptions->addAction( tr( "Make &Screenshot..." ) );
	pShot->setIcon( HlmvIcon( "screenshot" ) );
	connect( pShot, &QAction::triggered, this, [this]()
	{
		if ( !m_pViewport )
			return;
		const QString path = QFileDialog::getSaveFileName( this,
			tr( "Make Screenshot" ), QStringLiteral( "hlmv2-screenshot.png" ),
			tr( "PNG images (*.png)" ) );
		if ( path.isEmpty() )
			return;
		const QImage frame = m_pViewport->GrabFrame();
		if ( frame.isNull() || !frame.save( path ) )
			QMessageBox::warning( this, tr( "Make Screenshot" ),
				tr( "Could not save the viewport image." ) );
		else
			statusBar()->showMessage( tr( "Saved %1" ).arg( path ), 4000 );
	} );

	// ---------- View (mdlviewer.cpp:327-332) ----------
	// Display toggles are NOT menu entries in stock HLMV; they live on the
	// Control Panel's Render page and only carry accelerators.
	QMenu *pView = menuBar()->addMenu( tr( "&View" ) );
	QAction *pAssoc = pView->addAction( tr( "&File Associations..." ) );
	pAssoc->setEnabled( false );
	QAction *pActivities = pView->addAction( tr( "Show &Activities" ) );
	pActivities->setEnabled( false );
	QAction *pHidden = pView->addAction( tr( "Show &hidden" ) );
	pHidden->setEnabled( false );
	// Stock HLMV keeps the Control Panel in its own top-level window, which
	// the user may close. The port offers the same choice explicitly: docked
	// (default, nothing pops up) or in its own window.
	m_pActionControlPanel = pView->addAction( tr( "Control &Panel in separate window" ) );
	m_pActionControlPanel->setCheckable( true );
	connect( m_pActionControlPanel, &QAction::toggled,
		this, &CMainWindow::SetControlPanelFloating );

	// ---------- Language (catalogs in hlmvlang/, applied on next start) ----------
	QMenu *pLanguage = pView->addMenu( tr( "&Language" ) );
	QActionGroup *pLangGroup = new QActionGroup( this );
	HlmvLocalization *pLoc = QCoreApplication::instance()->findChild<HlmvLocalization *>();
	if ( pLoc )
	{
		for ( const HlmvLanguageInfo &lang : pLoc->AvailableLanguages() )
		{
			QAction *pLang = pLanguage->addAction( lang.displayName );
			pLang->setCheckable( true );
			pLang->setData( lang.locale );
			pLang->setChecked( lang.locale == pLoc->CurrentLanguage() );
			pLangGroup->addAction( pLang );
		}
	}
	connect( pLangGroup, &QActionGroup::triggered, this, [this, pLangGroup]( QAction *pLang )
	{
		HlmvLocalization *pLocalization =
			QCoreApplication::instance()->findChild<HlmvLocalization *>();
		if ( !pLocalization || pLang->data().toString() == pLocalization->CurrentLanguage() )
			return;
		const QString sPrevious = pLocalization->CurrentLanguage();
		if ( pLocalization->LoadLanguage( pLang->data().toString() ) )
		{
			QMessageBox::information( this, tr( "Language" ),
				tr( "The language change takes effect after restarting the viewer." ) );
			return;
		}

		// The group already checked the clicked entry; put the check back on
		// the language that is still in effect.
		for ( QAction *pOther : pLangGroup->actions() )
		{
			if ( pOther->data().toString() == sPrevious )
			{
				pOther->setChecked( true );
				break;
			}
		}
		QMessageBox::warning( this, tr( "Language" ),
			tr( "Could not load the language \"%1\"." ).arg( pLang->text() ) );
	} );

	// ---------- Help ----------
	QMenu *pHelp = menuBar()->addMenu( tr( "&Help" ) );
	QAction *pHomepage = pHelp->addAction( tr( "&Goto Homepage..." ) );
	connect( pHomepage, &QAction::triggered, this, []()
	{
		QDesktopServices::openUrl( QUrl( QStringLiteral(
			"https://developer.valvesoftware.com/wiki/Half-Life_Model_Viewer" ) ) );
	} );
	QAction *pAbout = pHelp->addAction( tr( "&About" ) );
	connect( pAbout, &QAction::triggered, this, &CMainWindow::OnAbout );

	// Hidden accelerator actions from mdlviewer.cpp's accelTable. The Render
	// page check boxes bind to these, so Ctrl+W/A/G/O etc. work even though
	// the toggles are not in any menu.
	auto AddAccel = [this]( const QString &label, const QString &key,
		const char *pszIcon )
	{
		QAction *pAct = new QAction( label, this );
		if ( !key.isEmpty() )
			pAct->setShortcut( QKeySequence( key ) );
		pAct->setCheckable( true );
		// The Control Panel is a floating top-level window, so a window-level
		// shortcut would only fire when the main window has focus.
		pAct->setShortcutContext( Qt::ApplicationShortcut );
		pAct->setIcon( HlmvIcon( pszIcon ) );
		addAction( pAct );
		return pAct;
	};
	m_pActionWireframe = AddAccel( tr( "Wireframe" ), QStringLiteral( "Ctrl+W" ), "wireframe" );
	m_pActionBones = AddAccel( tr( "Bones" ), QStringLiteral( "Ctrl+O" ), "bones" );
	m_pActionAttachments = AddAccel( tr( "Attachments" ), QStringLiteral( "Ctrl+A" ), "attachments" );
	m_pActionGround = AddAccel( tr( "Ground" ), QStringLiteral( "Ctrl+G" ), "ground" );
	m_pActionTextures = AddAccel( tr( "Textured" ), QString(), "textured" );
}

//-----------------------------------------------------------------------------
// Purpose: Central viewport.
//-----------------------------------------------------------------------------
void CMainWindow::CreateCentralWidget()
{
	m_pViewport = new CModelViewport( this );
	setCentralWidget( m_pViewport );
}

//-----------------------------------------------------------------------------
// Purpose: Left info dock + right panel docks.
//-----------------------------------------------------------------------------
void CMainWindow::CreateDockWidgets()
{
	setDockNestingEnabled( true );

	// Left: model info tree (studiohdr summary).
	m_pInfoTree = new QTreeWidget( this );
	m_pInfoTree->setColumnCount( 2 );
	m_pInfoTree->setHeaderLabels( QStringList() << tr( "Property" ) << tr( "Value" ) );
	m_pInfoTree->setAlternatingRowColors( true );
	m_pInfoDock = new QDockWidget( tr( "Model info" ), this );
	m_pInfoDock->setWidget( m_pInfoTree );
	addDockWidget( Qt::LeftDockWidgetArea, m_pInfoDock );
	m_pInfoDock->hide(); // stock HLMV has no side panel; View > Model Info shows it
	for ( QAction *pTop : menuBar()->actions() )
	{
		if ( pTop->text() == tr( "&View" ) && pTop->menu() )
		{
			m_pInfoDock->toggleViewAction()->setText( tr( "Model &Info" ) );
			pTop->menu()->addAction( m_pInfoDock->toggleViewAction() );
		}
	}

	// Right: the stock Control Panel. controlpanel.cpp:1232 creates a
	// "Frame" (Force To Frame) slider above a nine-page tab control; the page
	// order is 1247-1255 Render, Sequence, Model, Flex, Physics, Bones,
	// Attachments, IKRule, Events.
	QWidget *pPanel = new QWidget( this );
	QVBoxLayout *pPanelLayout = new QVBoxLayout( pPanel );
	pPanelLayout->setContentsMargins( 0, 0, 0, 0 );
	pPanelLayout->setSpacing( 0 );

	m_pPanelTabs = new QTabWidget( pPanel );
	pPanelLayout->addWidget( m_pPanelTabs, 1 );

	QHBoxLayout *pFrameRow = new QHBoxLayout();
	pFrameRow->addWidget( new QLabel( tr( "Frame" ), pPanel ) );
	m_pFrameSlider = new QSlider( Qt::Horizontal, pPanel );
	m_pFrameSlider->setRange( 0, 1000 );
	m_pFrameSlider->setValue( 0 );
	m_pFrameSlider->setToolTip( tr( "Force To Frame" ) );
	m_pFrameLabel = new QLabel( QStringLiteral( "0" ), pPanel );
	m_pFrameLabel->setMinimumWidth( 32 );
	pFrameRow->addWidget( m_pFrameSlider, 1 );
	pFrameRow->addWidget( m_pFrameLabel );
	pPanelLayout->addLayout( pFrameRow );
	connect( m_pFrameSlider, &QSlider::valueChanged,
		this, &CMainWindow::OnFrameSliderChanged );


	// Every page scrolls. The stock pages are absolutely positioned and much
	// wider and taller than a docked panel; wrapping them keeps the dock's
	// minimum size small, so the Control Panel stays inside the main window
	// instead of being pushed out of it.
	auto WrapScroll = [this]( QWidget *pPage )
	{
		QScrollArea *pScroll = new QScrollArea( m_pPanelTabs );
		pScroll->setWidgetResizable( true );
		pScroll->setFrameShape( QFrame::NoFrame );
		pScroll->setWidget( pPage );
		return pScroll;
	};

	// ---------------- Render (controlpanel.cpp:1268-1349) ----------------
	QWidget *pRenderPage = new QWidget( m_pPanelTabs );
	QVBoxLayout *pRenderLayout = new QVBoxLayout( pRenderPage );

	QHBoxLayout *pModeRow = new QHBoxLayout();
	pModeRow->addWidget( new QLabel( tr( "Render Mode:" ), pRenderPage ) );
	m_pRenderMode = new QComboBox( pRenderPage );
	m_pRenderMode->addItems( QStringList()
		<< tr( "Wireframe" ) << tr( "Smoothshaded" ) << tr( "Textured" )
		<< tr( "BoneWeights" ) << tr( "BadVertexData" ) << tr( "UV Chart" ) );
	// Modes the Qt/OpenGL renderer does not implement stay visible but
	// unselectable, matching the port's "faithful layout, no substitutes" rule.
	for ( int i = 3; i < m_pRenderMode->count(); ++i )
		m_pRenderMode->setItemData( i, 0, Qt::UserRole - 1 );
	m_pRenderMode->setCurrentIndex( 2 ); // stock default: Textured
	m_pRenderMode->setToolTip( tr( "Select Render Mode" ) );
	m_pRenderMode->setMaximumWidth( 220 );
	pModeRow->addWidget( m_pRenderMode );
	pModeRow->addStretch( 1 );
	pRenderLayout->addLayout( pModeRow );
	connect( m_pRenderMode, QOverload<int>::of( &QComboBox::currentIndexChanged ),
		this, &CMainWindow::OnRenderModeChanged );

	m_pActionTextures->setChecked( true );

	// Ctrl-W (or the Wireframe check box) toggles the action; mirror it into
	// the combo. OnRenderModeChanged only writes the action when it differs,
	// so the two directions converge without a signal loop.
	connect( m_pActionWireframe, &QAction::toggled, this, [this]( bool bOn )
	{
		const int nIndex = m_pRenderMode->currentIndex();
		if ( bOn && nIndex != 0 )
			m_pRenderMode->setCurrentIndex( 0 );
		else if ( !bOn && nIndex == 0 )
			m_pRenderMode->setCurrentIndex( 2 ); // back to Textured
	} );

	QGridLayout *pChecks = new QGridLayout();
	pRenderLayout->addLayout( pChecks );

	// Binds a stock accelerator to a visible check box. The QAction is a
	// window-level shortcut (never a menu entry), exactly like mdlviewer.cpp's
	// accelTable driving the Control Panel check boxes.
	auto AddCheck = [this, pRenderPage, pChecks]( int row, int col,
		const QString &label, const QString &key, QAction *pBound,
		bool bEnabled, bool bChecked ) -> QCheckBox *
	{
		QAction *pAct = pBound;
		if ( !pAct )
		{
			pAct = new QAction( label, this );
			if ( !key.isEmpty() )
				pAct->setShortcut( QKeySequence( key ) );
			pAct->setCheckable( true );
			pAct->setEnabled( bEnabled );
			pAct->setShortcutContext( Qt::ApplicationShortcut );
			addAction( pAct );
		}
		QCheckBox *pBox = new QCheckBox( label, pRenderPage );
		pBox->setEnabled( bEnabled );
		pBox->setChecked( bChecked );
		pAct->setChecked( bChecked );
		connect( pAct, &QAction::toggled, pBox, &QCheckBox::setChecked );
		connect( pBox, &QCheckBox::toggled, pAct, [pAct]( bool bOn )
		{
			if ( pAct->isChecked() != bOn )
				pAct->setChecked( bOn );
		} );
		pChecks->addWidget( pBox, row, col );
		return pBox;
	};

	// Column 0 (stock x=5)
	AddCheck( 0, 0, tr( "Normal Mapping" ), QString(), nullptr, false, true );
	QCheckBox *pAtt = AddCheck( 1, 0, tr( "Attachments (Ctrl-A)" ),
		QStringLiteral( "Ctrl+A" ), m_pActionAttachments, true, false );
	QCheckBox *pBonesChk = AddCheck( 2, 0, tr( "Bones (Ctrl-O)" ),
		QStringLiteral( "Ctrl+O" ), m_pActionBones, true, false );
	AddCheck( 3, 0, tr( "Normals (Ctrl-N)" ), QStringLiteral( "Ctrl+N" ), nullptr, false, false );
	AddCheck( 4, 0, tr( "Tangents (Ctrl-T)" ), QStringLiteral( "Ctrl+T" ), nullptr, false, false );
	QCheckBox *pWire = AddCheck( 5, 0, tr( "Wireframe (Ctrl-W)" ),
		QStringLiteral( "Ctrl+W" ), m_pActionWireframe, true, false );
	AddCheck( 6, 0, tr( "Specular" ), QString(), nullptr, false, true );

	// Column 1 (stock x=125)
	QCheckBox *pGroundChk = AddCheck( 0, 1, tr( "Ground (Ctrl-G)" ),
		QStringLiteral( "Ctrl+G" ), m_pActionGround, true, true );
	AddCheck( 1, 1, tr( "Movement (Ctrl-M)" ), QStringLiteral( "Ctrl+M" ), nullptr, false, false );
	AddCheck( 2, 1, tr( "Background (Ctrl-B)" ), QStringLiteral( "Ctrl+B" ), nullptr, false, false );
	AddCheck( 3, 1, tr( "Hit Boxes (Ctrl-H)" ), QStringLiteral( "Ctrl+H" ), nullptr, false, false );
	AddCheck( 4, 1, tr( "Seq. Boxes" ), QString(), nullptr, false, false );
	AddCheck( 5, 1, tr( "Shadow (Ctrl-S)" ), QStringLiteral( "Ctrl+S" ), nullptr, false, false );
	AddCheck( 6, 1, tr( "Software Skin" ), QString(), nullptr, false, false );
	AddCheck( 7, 1, tr( "Enable Overbrightening" ), QString(), nullptr, false, true );

	// Column 2 (stock x=275)
	QComboBox *pPhysBone = new QComboBox( pRenderPage );
	pPhysBone->addItem( tr( "None" ) );
	pPhysBone->setEnabled( false );
	pPhysBone->setToolTip( tr( "Select Physics Bone to highlight" ) );
	pChecks->addWidget( pPhysBone, 0, 2 );
	AddCheck( 1, 2, tr( "Physics Model" ), QString(), nullptr, false, false );
	AddCheck( 2, 2, tr( "Enable IK" ), QString(), nullptr, false, false );
	AddCheck( 3, 2, tr( "Head Turn" ), QString(), nullptr, false, false );
	AddCheck( 4, 2, tr( "Illum. Position" ), QString(), nullptr, false, false );
	AddCheck( 5, 2, tr( "Play Sounds" ), QString(), nullptr, false, false );
	AddCheck( 6, 2, tr( "Show Origin Axis" ), QString(), nullptr, false, false );
	QSlider *pAxisLen = new QSlider( Qt::Horizontal, pRenderPage );
	pAxisLen->setRange( 1, 100 );
	pAxisLen->setValue( 10 );
	pAxisLen->setEnabled( false );
	pChecks->addWidget( pAxisLen, 7, 2 );

	QHBoxLayout *pFovRow = new QHBoxLayout();
	pFovRow->addWidget( new QLabel( tr( "FOV:" ), pRenderPage ) );
	QLineEdit *pFov = new QLineEdit( QStringLiteral( "65" ), pRenderPage );
	pFov->setEnabled( false );
	pFov->setMaximumWidth( 60 );
	pFovRow->addWidget( pFov );
	pFovRow->addStretch( 1 );
	pRenderLayout->addLayout( pFovRow );
	pRenderLayout->addStretch( 1 );

	connect( pAtt, &QCheckBox::toggled, this, &CMainWindow::OnToggleAttachments );
	connect( pBonesChk, &QCheckBox::toggled, this, &CMainWindow::OnToggleBones );
	connect( pWire, &QCheckBox::toggled, this, &CMainWindow::OnToggleWireframe );
	connect( pGroundChk, &QCheckBox::toggled, this, &CMainWindow::OnToggleGround );

	m_pPanelTabs->addTab( WrapScroll( pRenderPage ), tr( "Render" ) );

	// ---------------- Sequence (controlpanel.cpp:1355-1411) ----------------
	QWidget *pSeqPage = new QWidget( m_pPanelTabs );
	QVBoxLayout *pSeqLayout = new QVBoxLayout( pSeqPage );

	m_pSequenceList = new QListWidget( pSeqPage );
	m_pSequenceList->setMaximumHeight( 150 );
	pSeqLayout->addWidget( m_pSequenceList );
	connect( m_pSequenceList, &QListWidget::itemDoubleClicked, this,
		[this]( QListWidgetItem * ) { OnSequenceSelected( m_pSequenceList->currentRow() ); } );
	connect( m_pSequenceList, &QListWidget::currentRowChanged,
		this, &CMainWindow::OnSequenceSelected );

	QGridLayout *pLayers = new QGridLayout();
	for ( int i = 0; i < 5; ++i ) // MAX_SEQUENCES animation layers
	{
		QComboBox *pChoice = new QComboBox( pSeqPage );
		pChoice->addItem( tr( "(None)" ) );
		pChoice->setEnabled( false );
		QSlider *pScale = new QSlider( Qt::Horizontal, pSeqPage );
		pScale->setRange( 0, 100 );
		pScale->setEnabled( false );
		QRadioButton *pRadio = new QRadioButton( QString(), pSeqPage );
		pRadio->setChecked( i == 0 );
		pRadio->setEnabled( false );
		pLayers->addWidget( pChoice, i, 0 );
		pLayers->addWidget( pScale, i, 1 );
		pLayers->addWidget( pRadio, i, 2 );
	}
	pSeqLayout->addLayout( pLayers );

	QHBoxLayout *pPlayLayout = new QHBoxLayout();
	m_pPlayButton = new QPushButton( tr( "Play" ), pSeqPage );
	m_pPlayButton->setObjectName( "playButton" );
	pPlayLayout->addWidget( m_pPlayButton );
	pPlayLayout->addWidget( new QLabel( tr( "Speed:" ), pSeqPage ) );
	m_pSpeedSlider = new QSlider( Qt::Horizontal, pSeqPage );
	m_pSpeedSlider->setRange( 10, 400 );
	m_pSpeedSlider->setValue( 100 );
	pPlayLayout->addWidget( m_pSpeedSlider, 1 );
	m_pFpsLabel = new QLabel( QString(), pSeqPage );
	pPlayLayout->addWidget( m_pFpsLabel );
	pSeqLayout->addLayout( pPlayLayout );
	connect( m_pPlayButton, &QPushButton::clicked, this, &CMainWindow::OnPlayPauseClicked );
	connect( m_pSpeedSlider, &QSlider::valueChanged, this, &CMainWindow::OnSpeedSliderChanged );

	QHBoxLayout *pBlendRow = new QHBoxLayout();
	QCheckBox *pBlend = new QCheckBox( tr( "Blend Sequence Changes" ), pSeqPage );
	pBlend->setEnabled( false );
	QPushButton *pBlendNow = new QPushButton( tr( "Blend Now" ), pSeqPage );
	pBlendNow->setEnabled( false );
	QSlider *pBlendTime = new QSlider( Qt::Horizontal, pSeqPage );
	pBlendTime->setRange( 0, 100 );
	pBlendTime->setEnabled( false );
	pBlendRow->addWidget( pBlend );
	pBlendRow->addWidget( pBlendNow );
	pBlendRow->addWidget( pBlendTime, 1 );
	pSeqLayout->addLayout( pBlendRow );

	QHBoxLayout *pActivityRow = new QHBoxLayout();
	pActivityRow->addWidget( new QLabel( tr( "Activity modifiers:" ), pSeqPage ) );
	QComboBox *pModifiers = new QComboBox( pSeqPage );
	pModifiers->setEnabled( false );
	pActivityRow->addWidget( pModifiers, 1 );
	QCheckBox *pAnimateWeapons = new QCheckBox( tr( "Animate weapons" ), pSeqPage );
	pAnimateWeapons->setEnabled( false );
	pActivityRow->addWidget( pAnimateWeapons );
	pSeqLayout->addLayout( pActivityRow );
	pSeqLayout->addStretch( 1 );
	m_pPanelTabs->addTab( WrapScroll( pSeqPage ), tr( "Sequence" ) );

	// ---------------- Model (controlpanel.cpp:1417-1464) ----------------
	QWidget *pModelPage = new QWidget( m_pPanelTabs );
	QVBoxLayout *pModelLayout = new QVBoxLayout( pModelPage );

	QHBoxLayout *pBodyTop = new QHBoxLayout();
	pBodyTop->addWidget( new QLabel( tr( "Bodypart:" ), pModelPage ) );
	QComboBox *pBodypart = new QComboBox( pModelPage );
	pBodypart->setEnabled( false );
	pBodyTop->addWidget( pBodypart, 1 );
	pBodyTop->addWidget( new QLabel( tr( "Submodel:" ), pModelPage ) );
	QComboBox *pSubmodel = new QComboBox( pModelPage );
	pSubmodel->setEnabled( false );
	pBodyTop->addWidget( pSubmodel, 1 );
	pModelLayout->addLayout( pBodyTop );

	QGroupBox *pBodyBox = new QGroupBox( tr( "Bodygroups" ), pModelPage );
	QVBoxLayout *pBodyBoxLayout = new QVBoxLayout( pBodyBox );
	m_pBodygroupPage = new QWidget( pBodyBox );
	pBodyBoxLayout->addWidget( m_pBodygroupPage );
	QScrollArea *pBodyScroll = new QScrollArea( pModelPage );
	pBodyScroll->setWidgetResizable( true );
	pBodyScroll->setWidget( pBodyBox );
	pBodyScroll->setFrameShape( QFrame::NoFrame );
	pBodyScroll->setMaximumHeight( 180 );
	pModelLayout->addWidget( pBodyScroll );

	QHBoxLayout *pCtrlRow = new QHBoxLayout();
	pCtrlRow->addWidget( new QLabel( tr( "Controller:" ), pModelPage ) );
	QComboBox *pController = new QComboBox( pModelPage );
	pController->setEnabled( false );
	pCtrlRow->addWidget( pController, 1 );
	QSlider *pCtrlValue = new QSlider( Qt::Horizontal, pModelPage );
	pCtrlValue->setRange( 0, 255 );
	pCtrlValue->setEnabled( false );
	pCtrlRow->addWidget( pCtrlValue, 1 );
	pModelLayout->addLayout( pCtrlRow );

	QHBoxLayout *pSkinRow = new QHBoxLayout();
	pSkinRow->addWidget( new QLabel( tr( "Skin family:" ), pModelPage ) );
	m_pSkinCombo = new QComboBox( pModelPage );
	pSkinRow->addWidget( m_pSkinCombo, 1 );
	pModelLayout->addLayout( pSkinRow );
	connect( m_pSkinCombo, QOverload<int>::of( &QComboBox::currentIndexChanged ),
		this, &CMainWindow::OnSkinChanged );

	QHBoxLayout *pLodRow = new QHBoxLayout();
	QCheckBox *pAutoLod = new QCheckBox( tr( "Auto LOD" ), pModelPage );
	pAutoLod->setEnabled( false );
	QComboBox *pLodChoice = new QComboBox( pModelPage );
	pLodChoice->setEnabled( false );
	QLineEdit *pLodSwitch = new QLineEdit( pModelPage );
	pLodSwitch->setEnabled( false );
	pLodSwitch->setMaximumWidth( 50 );
	pLodRow->addWidget( pAutoLod );
	pLodRow->addWidget( pLodChoice );
	pLodRow->addWidget( new QLabel( tr( "LOD Switch:" ), pModelPage ) );
	pLodRow->addWidget( pLodSwitch );
	pModelLayout->addLayout( pLodRow );

	pModelLayout->addWidget( new QLabel( tr( "Materials used:" ), pModelPage ) );
	m_pMaterialList = new QListWidget( pModelPage );
	m_pMaterialList->setMaximumHeight( 120 );
	pModelLayout->addWidget( m_pMaterialList );

	QHBoxLayout *pVmtRow = new QHBoxLayout();
	pVmtRow->addWidget( new QLabel( tr( "VMTs Loaded:" ), pModelPage ) );
	QListWidget *pVmtList = new QListWidget( pModelPage );
	pVmtList->addItem( tr( "None" ) );
	pVmtList->setEnabled( false );
	pVmtList->setMaximumHeight( 60 );
	pVmtRow->addWidget( pVmtList, 1 );
	pVmtRow->addWidget( new QLabel( tr( "Shader:" ), pModelPage ) );
	QListWidget *pShaderList = new QListWidget( pModelPage );
	pShaderList->addItem( tr( "Select material to show shader" ) );
	pShaderList->setEnabled( false );
	pShaderList->setMaximumHeight( 60 );
	pVmtRow->addWidget( pShaderList, 1 );
	pModelLayout->addLayout( pVmtRow );
	pModelLayout->addStretch( 1 );
	m_pPanelTabs->addTab( WrapScroll( pModelPage ), tr( "Model" ) );

	// ---------------- Flex / Physics / Bones / Attachments / IKRule / Events
	auto AddPlaceholder = [this, &WrapScroll]( const QString &title, const QString &text )
	{
		QWidget *pPage = new QWidget( m_pPanelTabs );
		QVBoxLayout *pLayout = new QVBoxLayout( pPage );
		QLabel *pLabel = new QLabel( text, pPage );
		pLabel->setWordWrap( true );
		pLabel->setEnabled( false );
		pLayout->addWidget( pLabel );
		pLayout->addStretch( 1 );
		m_pPanelTabs->addTab( WrapScroll( pPage ), title );
	};

	AddPlaceholder( tr( "Flex" ), tr( "Flex controllers require the studio flex "
		"driver data from the engine material system; not available in this port." ) );

	m_pPhysicsTree = new QTreeWidget( m_pPanelTabs );
	m_pPhysicsTree->setColumnCount( 2 );
	m_pPhysicsTree->setHeaderLabels( QStringList() << tr( "Key" ) << tr( "Value" ) );
	m_pPhysicsTree->setRootIsDecorated( false );
	m_pPanelTabs->addTab( WrapScroll( m_pPhysicsTree ), tr( "Physics" ) );

	m_pBonesTree = new QTreeWidget( m_pPanelTabs );
	m_pBonesTree->setColumnCount( 2 );
	m_pBonesTree->setHeaderLabels( QStringList() << tr( "Bone" ) << tr( "Parent" ) );
	m_pPanelTabs->addTab( WrapScroll( m_pBonesTree ), tr( "Bones" ) );

	m_pAttachmentList = new QListWidget( m_pPanelTabs );
	m_pPanelTabs->addTab( WrapScroll( m_pAttachmentList ), tr( "Attachments" ) );

	AddPlaceholder( tr( "IKRule" ), tr( "IK rule editing requires the engine IK "
		"context; not available in this port." ) );
	AddPlaceholder( tr( "Events" ), tr( "Animation event editing requires the "
		"engine event list; not available in this port." ) );

	QDockWidget *pPanelsDock = new QDockWidget( tr( "Control Panel" ), this );
	pPanelsDock->setWidget( pPanel );
	pPanelsDock->setFeatures( QDockWidget::DockWidgetFloatable );
	// Embedded look while docked; a floating dock needs its real title bar
	// so it can be dragged and re-docked.
	pPanelsDock->setTitleBarWidget( new QWidget( pPanelsDock ) );
	connect( pPanelsDock, &QDockWidget::topLevelChanged, this, [this]( bool bFloating )
	{
		// QDockWidget deletes the previous custom title bar widget when it is
		// replaced, so a fresh empty one is created every time it re-docks.
		m_pControlPanelDock->setTitleBarWidget(
			bFloating ? nullptr : new QWidget( m_pControlPanelDock ) );
		if ( m_pActionControlPanel && m_pActionControlPanel->isChecked() != bFloating )
		{
			// Keep the View menu in sync when the user drags/re-docks it.
			// SetControlPanelFloating() is a no-op because the state already matches.
			m_pActionControlPanel->setChecked( bFloating );
		}
	} );
	// Stock HLMV's ControlPanel is a separate top-level mxWindow. The port
	// docks it along the bottom by default so nothing pops up over the
	// viewport on every start; View > Control Panel switches it to a
	// separate window (and back), and the choice is remembered.
	addDockWidget( Qt::BottomDockWidgetArea, pPanelsDock );
	m_pControlPanelDock = pPanelsDock;

	// Keep the individual dock pointers for the API surface and status text.
	m_pSequencesDock = pPanelsDock;
	m_pBodygroupsDock = pPanelsDock;
	m_pSkinsDock = pPanelsDock;
	m_pBonesDock = pPanelsDock;
	m_pAttachmentsDock = pPanelsDock;
	m_pMaterialsDock = pPanelsDock;
	m_pPhysicsDock = pPanelsDock;
}

//-----------------------------------------------------------------------------
// Purpose: Status bar.
//-----------------------------------------------------------------------------
void CMainWindow::CreateStatusBar()
{
	m_pStatusModel = new QLabel( tr( "No model loaded" ), this );
	m_pStatusCamera = new QLabel( QString(), this );
	statusBar()->addWidget( m_pStatusModel );
	statusBar()->addPermanentWidget( m_pStatusCamera );
}

//-----------------------------------------------------------------------------
// Purpose: Opens a model via the file dialog.
//-----------------------------------------------------------------------------
void CMainWindow::OnOpenModel()
{
	QString path = QFileDialog::getOpenFileName( this, tr( "Open model" ), QString(), QLatin1String( kModelDialogFilter ) );
	if ( path.isEmpty() )
		return;
	OpenModel( path );
}

//-----------------------------------------------------------------------------
// Purpose: Loads a model and populates every panel.
//-----------------------------------------------------------------------------
bool CMainWindow::OpenModel( const QString &mdlPath )
{
	QString sError;
	CStudioModelData *pModel = new CStudioModelData();
	if ( !pModel->LoadFromFile( mdlPath, sError ) )
	{
		delete pModel;
		QMessageBox::critical( this, tr( "Open model" ), tr( "Failed to load model:\n%1" ).arg( sError ) );
		return false;
	}

	// A hands model is matched against the previous main model's bones; drop
	// it when a different model is loaded (Refresh of the same file keeps it).
	if ( m_pHandsModel && mdlPath != m_sCurrentModelPath )
		DetachHandsModel();

	delete m_pModel;
	m_pModel = pModel;
	m_bPlaying = false;
	if ( m_pAnimTimer )
		m_pAnimTimer->stop();

	m_pViewport->SetModel( m_pModel );

	// A new model starts at the default playback speed; apply the slider value
	// so the slider and the real speed agree.
	if ( m_pSpeedSlider )
		m_pModel->SetPlaybackSpeed( m_pSpeedSlider->value() / 100.0f );

	// Resolve every studio texture slot through VMT -> VTF. This uses plain
	// Qt file I/O + our VPK reader, so it works on Linux/macOS too; the old
	// viewer needed materialsystem (D3D9 + engine filesystem) for this.
	CMaterialLoader loader;
	loader.SetModelPath( mdlPath );
	const QVector<TextureInfo_t> &textures = m_pModel->GetTextures();
	QVector<QImage> images( textures.count() );
	QVector<bool> tintable( textures.count(), false );
	QVector3D defaultTint( 1.0f, 1.0f, 1.0f );
	m_lstMaterialStatus.clear();
	for ( int i = 0; i < textures.count(); ++i )
	{
		QString sDetail;
		bool bTintable = false;
		QVector3D tintDefault( 1.0f, 1.0f, 1.0f );
		const bool bOk = loader.LoadTextureImage( textures[ i ].name,
			m_pModel->GetCdTextures(), images[ i ], sDetail, &bTintable, &tintDefault );
		tintable[ i ] = bTintable;
		if ( bTintable )
			defaultTint = tintDefault;
		m_lstMaterialStatus << QStringLiteral( "%1: %2" )
			.arg( bOk ? QStringLiteral( "resolved" ) : QStringLiteral( "unresolved" ), sDetail );
	}
	m_pViewport->SetMaterialImages( images );
	m_pViewport->SetTintableSlots( tintable );
	// Use the colour the engine would pick (GMod's PlayerColor proxy default)
	// until the user chooses another one.
	if ( defaultTint != QVector3D( 1.0f, 1.0f, 1.0f ) )
		m_pViewport->SetTintColor( defaultTint );

	m_sCurrentModelPath = mdlPath;
	AddRecentModel( mdlPath );
	UpdateMergedState();

	PopulateAllPanels();
	UpdateFrameControls(); // m_bPopulating blocked OnSequenceSelected
	UpdateTitle();

	int nResolved = 0;
	for ( const QString &status : m_lstMaterialStatus )
		if ( status.startsWith( QLatin1String( "resolved" ) ) )
			++nResolved;

	// Scopes: textures = all studio texture slots; meshes = meshes currently
	// drawn for the selected bodygroups; tris = model total over all bodygroups.
	m_pStatusModel->setText( tr( "%1 | v%2 | %3 bones, %4 seqs, %5 textures (%6 resolved), %7 meshes drawn (current bodygroups), %8 tris total (all bodygroups)" )
		.arg( m_pModel->GetFileName() )
		.arg( m_pModel->GetVersion() )
		.arg( m_pModel->GetBoneCount() )
		.arg( m_pModel->GetSequenceCount() )
		.arg( m_pModel->GetTextureCount() )
		.arg( nResolved )
		.arg( m_pModel->GetMeshCount() )
		.arg( m_pModel->GetTriangleCount() ) );

	// Reset the play button label (OpenModel always restarts paused).
	if ( m_pPlayButton )
		m_pPlayButton->setText( tr( "Play" ) );

	// c_arms.mdl and friends are animation/bone hosts with no mesh. Say so
	// instead of showing an empty viewport (the original viewer relies on
	// merged models for these).
	if ( m_pModel->GetDrawList().isEmpty() || m_pModel->GetVertexCount() == 0 )
	{
		statusBar()->showMessage( tr(
			"This model has no renderable mesh (animation host). "
			"Use File > Load Weapon to attach geometry." ), 10000 );
	}

	// Stock HLMV starts playing the first sequence.
	if ( m_pModel->GetSequenceCount() > 0 && m_pModel->HasAnimationData() )
		OnPlayPauseClicked();

	return true;
}

//-----------------------------------------------------------------------------
// Purpose: Populates every right-side panel and the info tree.
//-----------------------------------------------------------------------------
void CMainWindow::PopulateAllPanels()
{
	m_bPopulating = true;
	PopulateModelInfo();
	PopulateSequences();
	PopulateBodygroups();
	PopulateSkins();
	PopulateBones();
	PopulateAttachments();
	PopulateMaterials();
	PopulatePhysics();
	m_bPopulating = false;
}

//-----------------------------------------------------------------------------
// Purpose: Clears all panels.
//-----------------------------------------------------------------------------
void CMainWindow::ClearPanels()
{
	m_pInfoTree->clear();
	m_pSequenceList->clear();
	PopulateBodygroups(); // clears + rebuilds the combo rows
	m_pSkinCombo->clear();
	m_pBonesTree->clear();
	m_pAttachmentList->clear();
	m_pMaterialList->clear();
	m_pPhysicsTree->clear();
}

//-----------------------------------------------------------------------------
// Purpose: Left info tree: studiohdr summary.
//-----------------------------------------------------------------------------
void CMainWindow::PopulateModelInfo()
{
	m_pInfoTree->clear();
	if ( !m_pModel )
		return;

	auto addRow = [this]( const QString &key, const QString &value, QTreeWidgetItem *pParent = nullptr ) {
		QTreeWidgetItem *pItem = pParent ? new QTreeWidgetItem( pParent ) : new QTreeWidgetItem( m_pInfoTree );
		pItem->setText( 0, key );
		pItem->setText( 1, value );
		return pItem;
	};

	addRow( tr( "File" ), m_pModel->GetFileName() );
	addRow( tr( "Model name" ), m_pModel->GetModelName() );
	addRow( tr( "Version" ), QString::number( m_pModel->GetVersion() ) );
	addRow( tr( "Checksum" ), QStringLiteral( "0x%1" ).arg( ( uint )m_pModel->GetChecksum(), 8, 16, QChar( '0' ) ) );
	addRow( tr( "Bones" ), QString::number( m_pModel->GetBoneCount() ) );
	addRow( tr( "Sequences" ), QString::number( m_pModel->GetSequenceCount() ) );
	addRow( tr( "Materials" ), QString::number( m_pModel->GetTextureCount() ) );
	addRow( tr( "Skin families" ), QString::number( m_pModel->GetSkinFamilies() ) );
	addRow( tr( "Skin refs" ), QString::number( m_pModel->GetSkinRefs() ) );
	addRow( tr( "Bodygroups" ), QString::number( m_pModel->GetBodypartCount() ) );
	addRow( tr( "Meshes" ), QString::number( m_pModel->GetMeshCount() ) );
	addRow( tr( "Vertices" ), QString::number( m_pModel->GetVertexCount() ) );
	addRow( tr( "Triangles" ), QString::number( m_pModel->GetTriangleCount() ) );
	addRow( tr( "Attachments" ), QString::number( m_pModel->GetAttachmentCount() ) );
	addRow( tr( "Pose parameters" ), QString::number( m_pModel->GetPoseParameterCount() ) );
	addRow( tr( "Hitbox sets" ), QString::number( m_pModel->GetHitboxSetCount() ) );
	addRow( tr( "IK chains" ), QString::number( m_pModel->GetIKChainCount() ) );
	addRow( tr( "Flex descs" ), QString::number( m_pModel->GetFlexDescCount() ) );
	QString sMissing = m_pModel->GetMissingPeers();
	addRow( tr( "Missing peers" ), sMissing.isEmpty() ? tr( "none" ) : sMissing );

	m_pInfoTree->expandAll();
}

//-----------------------------------------------------------------------------
// Purpose: Sequences panel (double click switches the current sequence).
//-----------------------------------------------------------------------------
void CMainWindow::PopulateSequences()
{
	m_pSequenceList->clear();
	if ( !m_pModel )
		return;

	const int nSeqs = m_pModel->GetSequenceCount();
	for ( int i = 0; i < nSeqs; ++i )
	{
		QString activity, label;
		float fps;
		int numFrames;
		bool looping;
		int numBlends;
		label = m_pModel->GetSequenceLabel( i, &activity, &fps, &numFrames, &looping, &numBlends );
		QString line = QStringLiteral( "%1  (%2 fps, %3 frames%4%5)" )
			.arg( label.isEmpty() ? QStringLiteral( "seq%1" ).arg( i ) : label )
			.arg( fps, 0, 'f', 1 )
			.arg( numFrames )
			.arg( looping ? tr( ", looping" ) : QString() )
			.arg( numBlends > 1 ? tr( ", %1 blends" ).arg( numBlends ) : QString() );
		if ( !activity.isEmpty() )
			line += QStringLiteral( " [%1]" ).arg( activity );
		m_pSequenceList->addItem( line );
	}
	if ( nSeqs > 0 )
		m_pSequenceList->setCurrentRow( 0 );
}

void CMainWindow::OnSequenceSelected( int row )
{
	if ( m_bPopulating || !m_pModel )
		return;
	if ( m_pModel->SetCurrentSequence( row ) )
	{
		UpdateFrameControls();
		m_pViewport->update();
		UpdateTitle();
	}
}

//-----------------------------------------------------------------------------
// Purpose: Refreshes the fps label, frame label and Force To Frame slider from
//          the model's current sequence/cycle without triggering
//          OnFrameSliderChanged (which would pause playback).
//-----------------------------------------------------------------------------
void CMainWindow::UpdateFrameControls()
{
	float flFps = 0.0f;
	int nFrames = 1;
	float flCycle = 0.0f;
	if ( m_pModel && m_pModel->GetSequenceCount() > 0 )
	{
		m_pModel->GetSequenceLabel( m_pModel->GetCurrentSequence(),
			nullptr, &flFps, &nFrames, nullptr, nullptr );
		flCycle = m_pModel->GetCurrentCycle();
	}

	if ( m_pFpsLabel )
		m_pFpsLabel->setText( m_pModel ? tr( "%1 fps" ).arg( flFps, 0, 'f', 1 ) : QString() );
	if ( m_pFrameLabel )
		m_pFrameLabel->setText( QString::number( (int)( flCycle * qMax( 0, nFrames - 1 ) ) ) );
	if ( m_pFrameSlider )
	{
		const QSignalBlocker blocker( m_pFrameSlider );
		m_pFrameSlider->setValue( qBound( 0, (int)( flCycle * 1000.0f ), 1000 ) );
	}
}

//-----------------------------------------------------------------------------
// Purpose: Play/pause toggle for the current sequence.
//-----------------------------------------------------------------------------
void CMainWindow::OnPlayPauseClicked()
{
	if ( !m_pModel || m_pModel->GetSequenceCount() <= 0 )
		return;

	m_bPlaying = !m_bPlaying;
	m_pModel->SetPaused( !m_bPlaying );
	if ( m_pPlayButton )
		m_pPlayButton->setText( m_bPlaying ? tr( "Pause" ) : tr( "Play" ) );
	if ( m_bPlaying )
		m_AnimClock.restart();
	if ( m_pAnimTimer )
		m_pAnimTimer->setInterval( 33 );
	if ( m_bPlaying && !m_pAnimTimer->isActive() )
		m_pAnimTimer->start();
	else if ( !m_bPlaying )
		m_pAnimTimer->stop();
}

void CMainWindow::OnSpeedSliderChanged( int value )
{
	if ( m_pModel )
		m_pModel->SetPlaybackSpeed( value / 100.0f );
}

//-----------------------------------------------------------------------------
// Purpose: Animation clock tick: advances the cycle and repaints.
//-----------------------------------------------------------------------------
void CMainWindow::OnAnimationTick()
{
	if ( m_pModel && m_bPlaying )
	{
		float dt = (float)m_AnimClock.elapsed() / 1000.0f;
		m_AnimClock.restart();
		m_pModel->AdvanceAnimation( dt );
		UpdateFrameControls();
	}
	m_pViewport->update();
}

//-----------------------------------------------------------------------------
// Purpose: Bodygroups panel: one combo per bodypart. Rebuilt on model change;
//          the guard flag keeps combo signals from re-entering during fill.
//-----------------------------------------------------------------------------
void CMainWindow::PopulateBodygroups()
{
	// Tear down the previous rows: every label/combo is parented to the page,
	// so deleting only the layout would leak (and keep showing) the widgets.
	m_lstBodygroupCombos.clear();
	if ( QLayout *pOldLayout = m_pBodygroupPage->layout() )
	{
		auto DestroyItem = []( QLayoutItem *pItem, auto &&self ) -> void
		{
			if ( QWidget *pWidget = pItem->widget() )
			{
				pWidget->hide();
				pWidget->deleteLater();
			}
			else if ( QLayout *pSub = pItem->layout() )
			{
				while ( QLayoutItem *pChild = pSub->takeAt( 0 ) )
					self( pChild, self );
			}
			delete pItem;
		};
		while ( QLayoutItem *pItem = pOldLayout->takeAt( 0 ) )
			DestroyItem( pItem, DestroyItem );
		delete pOldLayout;
	}

	if ( !m_pModel )
		return;

	QVBoxLayout *pLayout = new QVBoxLayout( m_pBodygroupPage );
	pLayout->setContentsMargins( 4, 4, 4, 4 );

	const int nParts = m_pModel->GetBodypartCount();
	for ( int i = 0; i < nParts; ++i )
	{
		int numModels = 0;
		QString name = m_pModel->GetBodypartName( i, &numModels );
		if ( name.isEmpty() )
			name = QStringLiteral( "bodypart%1" ).arg( i );

		QHBoxLayout *pRow = new QHBoxLayout();
		QLabel *pLabel = new QLabel( name, m_pBodygroupPage );
		pLabel->setToolTip( tr( "%1 variants" ).arg( numModels ) );
		pRow->addWidget( pLabel, 1 );

		QComboBox *pCombo = new QComboBox( m_pBodygroupPage );
		for ( int v = 0; v < numModels; ++v )
			pCombo->addItem( m_pModel->GetModelName2( i, v ) );

		const int iSelected = m_pModel->GetBodygroupSelection( i );
		pCombo->setCurrentIndex( qBound( 0, iSelected, qMax( numModels - 1, 0 ) ) );
		const int iBodypart = i;
		connect( pCombo, QOverload<int>::of( &QComboBox::currentIndexChanged ), this,
			[this, iBodypart]( int iVariant ) { OnBodygroupChanged( iBodypart, iVariant ); } );

		pRow->addWidget( pCombo, 2 );
		pLayout->addLayout( pRow );
		m_lstBodygroupCombos.append( pCombo );
	}
	pLayout->addStretch( 1 );
}

void CMainWindow::OnBodygroupChanged( int iBodypart, int iVariant )
{
	if ( m_bPopulating || !m_pModel )
		return;
	if ( iBodypart < 0 || iBodypart >= m_lstBodygroupCombos.count() )
		return;
	m_pModel->SetBodygroupSelection( iBodypart, iVariant );
	m_pViewport->RefreshModel(); // rebuild GPU buffers, keep the camera
}

//-----------------------------------------------------------------------------
// Purpose: Skins panel.
//-----------------------------------------------------------------------------
void CMainWindow::PopulateSkins()
{
	m_pSkinCombo->clear();
	if ( !m_pModel )
		return;
	const int nFamilies = m_pModel->GetSkinFamilies();
	for ( int i = 0; i < nFamilies; ++i )
		m_pSkinCombo->addItem( QStringLiteral( "skin %1" ).arg( i ) );
	if ( nFamilies > 0 )
		m_pSkinCombo->setCurrentIndex( 0 );
}

void CMainWindow::OnSkinChanged( int index )
{
	if ( m_bPopulating || !m_pModel )
		return;
	m_pModel->SetSkin( index );
	m_pViewport->RefreshModel(); // texture indices are resolved in RebuildMeshBuffers
}

//-----------------------------------------------------------------------------
// Purpose: Bones panel: tree with parent chains.
//-----------------------------------------------------------------------------
void CMainWindow::PopulateBones()
{
	m_pBonesTree->clear();
	if ( !m_pModel )
		return;

	const int nBones = m_pModel->GetBoneCount();
	QVector<QTreeWidgetItem *> items( nBones, nullptr );
	QVector<int> parents( nBones, -1 );
	for ( int i = 0; i < nBones; ++i )
	{
		int iParent = -1;
		QString name = m_pModel->GetBoneName( i, &iParent );
		QTreeWidgetItem *pItem = new QTreeWidgetItem();
		pItem->setText( 0, name );
		pItem->setText( 1, QString::number( iParent ) );
		items[ i ] = pItem;
		parents[ i ] = iParent;
	}

	// Malformed data can contain self-parents, out-of-range parents or parent
	// cycles; such bones are shown as top-level entries instead of vanishing.
	for ( int i = 0; i < nBones; ++i )
	{
		const int iParent = parents[ i ];
		bool bValid = iParent >= 0 && iParent < nBones && iParent != i;
		if ( bValid )
		{
			// Walk up from the parent: reaching bone i again means a cycle.
			int iWalk = iParent;
			int nSteps = 0;
			while ( iWalk >= 0 && iWalk < nBones )
			{
				if ( iWalk == i || ++nSteps > nBones )
				{
					bValid = false;
					break;
				}
				iWalk = parents[ iWalk ];
			}
		}
		if ( bValid )
			items[ iParent ]->addChild( items[ i ] );
		else
			m_pBonesTree->addTopLevelItem( items[ i ] );
	}
}

//-----------------------------------------------------------------------------
// Purpose: Attachments panel: name, bone and current world position.
//-----------------------------------------------------------------------------
void CMainWindow::PopulateAttachments()
{
	m_pAttachmentList->clear();
	if ( !m_pModel )
		return;

	const QVector<QMatrix4x4> &bones = m_pModel->GetSetupBoneMatrices();
	const int nAttachments = m_pModel->GetAttachmentCount();
	for ( int i = 0; i < nAttachments; ++i )
	{
		int iBone = -1;
		QString name = m_pModel->GetAttachmentName( i, &iBone );
		CStudioModelData::Vec3 origin{ 0.0f, 0.0f, 0.0f };
		if ( !m_pModel->GetAttachmentWorldTransform( i, bones, origin ) )
			continue;
		m_pAttachmentList->addItem( QStringLiteral( "%1  [bone %2]  world (%3, %4, %5)" )
			.arg( name.isEmpty() ? QStringLiteral( "attach%1" ).arg( i ) : name )
			.arg( iBone )
			.arg( origin.x, 0, 'f', 1 )
			.arg( origin.y, 0, 'f', 1 )
			.arg( origin.z, 0, 'f', 1 ) );
	}
}

//-----------------------------------------------------------------------------
// Purpose: Materials panel.
//-----------------------------------------------------------------------------
void CMainWindow::PopulateMaterials()
{
	m_pMaterialList->clear();
	if ( !m_pModel )
		return;

	const QVector<TextureInfo_t> &textures = m_pModel->GetTextures();
	for ( int i = 0; i < textures.count(); ++i )
	{
		const bool bResolved = i < m_lstMaterialStatus.count() &&
			m_lstMaterialStatus[ i ].startsWith( QLatin1String( "resolved" ) );
		QListWidgetItem *pItem = new QListWidgetItem(
			( bResolved ? QStringLiteral( "[tex] " ) : QStringLiteral( "[color] " ) ) + textures[ i ].name,
			m_pMaterialList );
		if ( i < m_lstMaterialStatus.count() )
			pItem->setToolTip( m_lstMaterialStatus[ i ] );
	}
	for ( const QString &path : m_pModel->GetCdTextures() )
	{
		m_pMaterialList->addItem( QStringLiteral( "search path: %1" ).arg( path ) );
	}
}

//-----------------------------------------------------------------------------
// Purpose: Physics panel (.phy summary).
//-----------------------------------------------------------------------------
void CMainWindow::PopulatePhysics()
{
	m_pPhysicsTree->clear();
	if ( !m_pModel )
		return;

	const PhysicsSummary_t &phys = m_pModel->GetPhysicsSummary();
	if ( !phys.bLoaded )
	{
		QTreeWidgetItem *pItem = new QTreeWidgetItem( m_pPhysicsTree );
		pItem->setText( 0, tr( "no .phy file" ) );
		return;
	}

	auto addRow = [this]( const QString &key, const QString &value ) {
		QTreeWidgetItem *pItem = new QTreeWidgetItem( m_pPhysicsTree );
		pItem->setText( 0, key );
		pItem->setText( 1, value );
	};

	addRow( tr( "Header size" ), QString::number( phys.nHeaderSize ) );
	addRow( tr( "Solids" ), QString::number( phys.nSolidCount ) );
	addRow( tr( "Checksum" ), QStringLiteral( "0x%1" ).arg( phys.nChecksum, 8, 16, QChar( '0' ) ) );
	addRow( tr( "Mass" ), QString::number( phys.flMass ) );
	addRow( tr( "Surface prop" ), phys.surfaceprop );

	QTreeWidgetItem *pText = new QTreeWidgetItem( m_pPhysicsTree );
	pText->setText( 0, tr( "solid key-values" ) );
	for ( const QString &line : phys.solidText )
	{
		QTreeWidgetItem *pLine = new QTreeWidgetItem( pText );
		pLine->setText( 0, line );
	}
}

//-----------------------------------------------------------------------------
// Purpose: View menu toggles.
//-----------------------------------------------------------------------------
//-----------------------------------------------------------------------------
// Purpose: Control Panel > Render > Render Mode (controlpanel.cpp:1272-1280).
//          Only the modes the Qt/OpenGL renderer implements are selectable.
//-----------------------------------------------------------------------------
void CMainWindow::OnRenderModeChanged( int index )
{
	if ( !m_pViewport )
		return;
	const bool bWireframe = ( index == 0 );
	const bool bTextured = ( index == 2 );
	m_pViewport->SetWireframe( bWireframe );
	m_pViewport->SetTexturesEnabled( bTextured );
	if ( m_pActionWireframe && m_pActionWireframe->isChecked() != bWireframe )
		m_pActionWireframe->setChecked( bWireframe );
	if ( m_pActionTextures && m_pActionTextures->isChecked() != bTextured )
		m_pActionTextures->setChecked( bTextured );
}

//-----------------------------------------------------------------------------
// Purpose: Control Panel > "Force To Frame" slider (controlpanel.cpp:1232).
//          Dragging it pauses playback and drives the engine cycle directly.
//-----------------------------------------------------------------------------
void CMainWindow::OnFrameSliderChanged( int value )
{
	if ( !m_pModel )
		return;
	int nFrames = 1;
	m_pModel->GetSequenceLabel( m_pModel->GetCurrentSequence(),
		nullptr, nullptr, &nFrames, nullptr, nullptr );
	if ( m_pFrameLabel )
	{
		m_pFrameLabel->setText( QString::number(
			(int)( value / 1000.0f * qMax( 0, nFrames - 1 ) ) ) );
	}

	m_bPlaying = false;
	m_pModel->SetPaused( true );
	m_pModel->SetCycle( value / 1000.0f );
	if ( m_pAnimTimer )
		m_pAnimTimer->stop();
	if ( m_pPlayButton )
		m_pPlayButton->setText( tr( "Play" ) );
	if ( m_pViewport )
		m_pViewport->update();
}

//-----------------------------------------------------------------------------
// Purpose: Switches the Control Panel between the docked panel and a separate
//          top-level window. Stock HLMV keeps the Control Panel in its own
//          mxWindow; the port defaults to docking it so nothing pops up over
//          the viewport at every start, and remembers the user's choice.
//-----------------------------------------------------------------------------
void CMainWindow::SetControlPanelFloating( bool bFloating )
{
	if ( !m_pControlPanelDock )
		return;
	// The dock can also be floated/re-docked by the user (topLevelChanged
	// then syncs the menu action), in which case there is nothing to switch.
	if ( m_pControlPanelDock->isFloating() != bFloating )
	{
		if ( bFloating )
		{
			m_pControlPanelDock->setFloating( true );
			m_pControlPanelDock->resize( 1240, 480 );
			m_pControlPanelDock->move( geometry().right() - 1240,
				geometry().bottom() - 480 );
			m_pControlPanelDock->show();
			m_pControlPanelDock->raise();
		}
		else
		{
			m_pControlPanelDock->setFloating( false );
			addDockWidget( Qt::BottomDockWidgetArea, m_pControlPanelDock );
			resizeDocks( { m_pControlPanelDock }, { 320 }, Qt::Vertical );
			m_pControlPanelDock->show();
		}
	}

	QSettings settings;
	settings.setValue( QStringLiteral( "controlPanel/floating" ), bFloating );
}

void CMainWindow::OnToggleWireframe( bool checked ) { m_pViewport->SetWireframe( checked ); }
void CMainWindow::OnToggleBones( bool checked ) { m_pViewport->SetShowBones( checked ); }
void CMainWindow::OnToggleAttachments( bool checked ) { m_pViewport->SetShowAttachments( checked ); }
void CMainWindow::OnToggleGround( bool checked ) { m_pViewport->SetShowGround( checked ); }

//-----------------------------------------------------------------------------
// Purpose: Misc slots.
//-----------------------------------------------------------------------------
void CMainWindow::OnExit()
{
	close();
}

void CMainWindow::OnAbout()
{
	QMessageBox::about( this, tr( "About" ),
		tr( "Half-Life Model Viewer 2 (Qt6)\n\nA cross-platform rewrite of the classic\nHalf-Life Model Viewer (utils/hlmv).\n\nDirect .mdl/.vvd/.vtx/.phy parsing, Qt OpenGL renderer." ) );
}

//-----------------------------------------------------------------------------
// Purpose: Title bar shows the current file and sequence.
//-----------------------------------------------------------------------------
void CMainWindow::UpdateTitle()
{
	if ( !m_pModel )
	{
		setWindowTitle( tr( "Half-Life Model Viewer 2" ) );
		return;
	}

	QString sSeq = m_pModel->GetCurrentSequenceLabel();
	QString sTitle = QStringLiteral( "%1 - Half-Life Model Viewer 2" ).arg( m_pModel->GetFileName() );
	if ( !sSeq.isEmpty() )
		sTitle += QStringLiteral( "  [%1]" ).arg( sSeq );
	setWindowTitle( sTitle );
}

//-----------------------------------------------------------------------------
// Purpose: Saves nothing today (there is no layout to persist yet).
//-----------------------------------------------------------------------------
void CMainWindow::closeEvent( QCloseEvent *event )
{
	event->accept();
}

//-----------------------------------------------------------------------------
// Purpose: Recent model list, persisted with QSettings so the viewer no
//          longer has to re-pick the same .mdl on every launch.
//-----------------------------------------------------------------------------
QStringList CMainWindow::RecentModels() const
{
	QSettings settings( QStringLiteral( "hlmv2" ), QStringLiteral( "viewer" ) );
	return settings.value( QStringLiteral( "recentModels" ) ).toStringList();
}

void CMainWindow::AddRecentModel( const QString &path )
{
	QStringList list = RecentModels();
	list.removeAll( path );
	list.prepend( path );
	while ( list.count() > 10 )
		list.removeLast();
	QSettings settings( QStringLiteral( "hlmv2" ), QStringLiteral( "viewer" ) );
	settings.setValue( QStringLiteral( "recentModels" ), list );
	UpdateRecentMenu();
}

void CMainWindow::UpdateRecentMenu()
{
	if ( !m_pMenuRecent )
		return;
	m_pMenuRecent->clear();
	const QStringList list = RecentModels();
	if ( list.isEmpty() )
	{
		QAction *pEmpty = m_pMenuRecent->addAction( tr( "(empty)" ) );
		pEmpty->setEnabled( false );
		return;
	}
	for ( const QString &path : list )
	{
		QAction *pAction = m_pMenuRecent->addAction( QFileInfo( path ).fileName() );
		pAction->setToolTip( path );
		// Copy the path by value and defer: OpenModel() rebuilds this menu
		// (AddRecentModel -> UpdateRecentMenu), which must not happen while the
		// action that triggered it is still running.
		const QString sPath = path;
		connect( pAction, &QAction::triggered, this, [this, sPath]()
		{
			QTimer::singleShot( 0, this, [this, sPath]() { OpenModel( sPath ); } );
		} );
	}
}

//-----------------------------------------------------------------------------
// Purpose: Keeps "Unload All Merged Models" in sync with the merged model.
//-----------------------------------------------------------------------------
void CMainWindow::UpdateMergedState()
{
	if ( m_pActionUnloadWeapon )
		m_pActionUnloadWeapon->setEnabled( m_pHandsModel != nullptr );
	if ( m_pActionLoadWeapon )
		m_pActionLoadWeapon->setEnabled( m_pModel != nullptr );
}

//-----------------------------------------------------------------------------
// Purpose: Loads a hands model and merges its bones with the main model.
//-----------------------------------------------------------------------------
void CMainWindow::AttachHandsModel( const QString &path )
{
	if ( !m_pModel )
	{
		QMessageBox::information( this, tr( "Attach hands" ),
			tr( "Load a main model first; the weapon is merged onto its bones." ) );
		return;
	}

	QString sError;
	CStudioModelData *pHands = new CStudioModelData();
	if ( !pHands->LoadFromFile( path, sError ) )
	{
		delete pHands;
		QMessageBox::warning( this, tr( "Attach hands" ),
			tr( "Failed to load hands model:\n%1" ).arg( sError ) );
		return;
	}

	CMaterialLoader loader;
	loader.SetModelPath( path );
	const QVector<TextureInfo_t> &textures = pHands->GetTextures();
	QVector<QImage> images( textures.count() );
	QVector<bool> tintable( textures.count(), false );
	for ( int i = 0; i < textures.count(); ++i )
	{
		QString sStatus;
		bool bTintable = false;
		loader.LoadTextureImage( textures[ i ].name,
			pHands->GetCdTextures(), images[ i ], sStatus, &bTintable );
		tintable[ i ] = bTintable;
	}

	// The viewport must drop its pointer before the old model is destroyed.
	m_pViewport->SetHandsModel( nullptr );
	delete m_pHandsModel;
	m_pHandsModel = pHands;
	m_pViewport->SetHandsMaterialImages( images );
	m_pViewport->SetHandsTintableSlots( tintable );
	m_pViewport->SetHandsModel( m_pHandsModel );
	UpdateMergedState();

	int nMatched = 0;
	if ( m_pModel )
	{
		for ( int i = 0; i < m_pHandsModel->GetBoneCount(); ++i )
		{
			int iParent = -1;
			const QString name = m_pHandsModel->GetBoneName( i, &iParent );
			if ( m_pModel->FindBoneIndexByName( name ) >= 0 )
				++nMatched;
		}
	}
	statusBar()->showMessage( tr( "Loaded weapon %1 (%2/%3 bones matched)" )
		.arg( QFileInfo( path ).fileName() )
		.arg( nMatched ).arg( m_pHandsModel->GetBoneCount() ), 6000 );
}

void CMainWindow::DetachHandsModel()
{
	if ( m_pViewport )
		m_pViewport->SetHandsModel( nullptr );
	delete m_pHandsModel;
	m_pHandsModel = nullptr;
	UpdateMergedState();
	statusBar()->showMessage( tr( "All merged models unloaded" ), 3000 );
}
