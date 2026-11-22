//========= Half-Life Model Viewer 2 (Qt6 port of utils/hlmv) ================
//
// Purpose: Main window, laid out after the classic HLMV:
//
//            - Menu bar: File / View / Help
//            - Central render viewport (CModelViewport)
//            - Left dock: model info tree (studiohdr summary)
//            - Right dock widgets (stacked, like the old ControlPanel):
//                  Sequences   - list + play/pause/speed slider
//                  Bodygroups  - one combo box per bodypart
//                  Skins       - skin family selector
//                  Bones       - bone tree with parent chains
//                  Attachments - name/bone/world position list
//                  Materials   - texture names + search paths
//                  Physics     - .phy summary (solids, mass, key-values)
//
//          The parsed model lives in CStudioModelData; this window is the
//          command surface around it.
//
//=============================================================================

#ifndef HLMV2_MAINWINDOW_H
#define HLMV2_MAINWINDOW_H

#include <QElapsedTimer>
#include <QMainWindow>
#include <QStringList>
#include <QVector>

class QCheckBox;
class QComboBox;
class QDockWidget;
class QLabel;
class QLineEdit;
class QListWidget;
class QMenu;
class QPushButton;
class QSlider;
class QTabWidget;
class QTimer;
class QTreeWidget;
class QTreeWidgetItem;

class CModelViewport;
class CStudioModelData;

class CMainWindow : public QMainWindow
{
	Q_OBJECT

public:
	explicit CMainWindow( QWidget *parent = nullptr );
	~CMainWindow() override;

	// Loads a model into the viewer. Returns false and reports on failure.
	bool OpenModel( const QString &mdlPath );

protected:
	void closeEvent( QCloseEvent *event ) override;

private slots:
	// File
	void OnOpenModel();
	void OnExit();
	void OnAbout();

	// View
	void OnToggleWireframe( bool checked );
	void OnToggleBones( bool checked );
	void OnToggleAttachments( bool checked );
	void OnToggleGround( bool checked );

	// Sequences dock
	void OnSequenceSelected( int row );
	void OnPlayPauseClicked();
	void OnSpeedSliderChanged( int value );

	// Bodygroups dock (one combo per bodypart)
	void OnBodygroupChanged( int iBodypart, int iVariant );

	// Skins dock
	void OnSkinChanged( int index );

	// Animation clock
	void OnAnimationTick();

	// Control Panel: Render page and the Force To Frame slider.
	void OnRenderModeChanged( int index );
	void OnFrameSliderChanged( int value );

private:
	void CreateMenus();
	void CreateCentralWidget();
	void CreateDockWidgets();
	void CreateStatusBar();
	void UpdateTitle();
	// Refreshes fps/frame labels and the Force To Frame slider (no feedback).
	void UpdateFrameControls();
	// Switches the Control Panel between a docked panel and a separate window.
	void SetControlPanelFloating( bool bFloating );

	// Recent models (persisted via QSettings) and merged hands models.
	QStringList RecentModels() const;
	void AddRecentModel( const QString &path );
	void UpdateRecentMenu();
	void UpdateMergedState();
	void AttachHandsModel( const QString &path );
	void DetachHandsModel();
	void PopulateModelInfo();
	void PopulateSequences();
	void PopulateBodygroups();
	void PopulateSkins();
	void PopulateBones();
	void PopulateAttachments();
	void PopulateMaterials();
	void PopulatePhysics();
	void PopulateAllPanels();
	void ClearPanels();

	CStudioModelData *m_pModel = nullptr;
	CStudioModelData *m_pHandsModel = nullptr;
	CModelViewport *m_pViewport = nullptr;
	QString m_sCurrentModelPath;

	// Menus / actions
	QMenu *m_pMenuFile = nullptr;
	QMenu *m_pMenuView = nullptr;
	QMenu *m_pMenuHelp = nullptr;
	QAction *m_pActionWireframe = nullptr;
	QAction *m_pActionBones = nullptr;
	QAction *m_pActionAttachments = nullptr;
	QAction *m_pActionGround = nullptr;
	QAction *m_pActionTextures = nullptr;
	QAction *m_pActionUnloadWeapon = nullptr;
	QAction *m_pActionLoadWeapon = nullptr;
	QAction *m_pActionControlPanel = nullptr;
	QMenu *m_pMenuRecent = nullptr;

	// Left info dock
	QDockWidget *m_pInfoDock = nullptr;
	QDockWidget *m_pControlPanelDock = nullptr;
	QTreeWidget *m_pInfoTree = nullptr;

	// Right docks
	QDockWidget *m_pSequencesDock = nullptr;
	QDockWidget *m_pBodygroupsDock = nullptr;
	QDockWidget *m_pSkinsDock = nullptr;
	QDockWidget *m_pBonesDock = nullptr;
	QDockWidget *m_pAttachmentsDock = nullptr;
	QDockWidget *m_pMaterialsDock = nullptr;
	QDockWidget *m_pPhysicsDock = nullptr;

	QListWidget *m_pSequenceList = nullptr;
	QSlider *m_pSpeedSlider = nullptr;
	QComboBox *m_pSkinCombo = nullptr;
	QWidget *m_pBodygroupPage = nullptr;   // one label + combo row per bodypart
	QVector<QComboBox *> m_lstBodygroupCombos;
	// Per-texture VMT/VTF resolution trail (shown as tooltips in Materials).
	QStringList m_lstMaterialStatus;
	QTreeWidget *m_pBonesTree = nullptr;
	QListWidget *m_pAttachmentList = nullptr;
	QListWidget *m_pMaterialList = nullptr;
	QTreeWidget *m_pPhysicsTree = nullptr;

	QPushButton *m_pPlayButton = nullptr;

	// Stock Control Panel widgets
	QTabWidget *m_pPanelTabs = nullptr;
	QComboBox *m_pRenderMode = nullptr;
	QSlider *m_pFrameSlider = nullptr;
	QLabel *m_pFrameLabel = nullptr;
	QLabel *m_pFpsLabel = nullptr;

	// Playback controls
	QTimer *m_pAnimTimer = nullptr;
	QElapsedTimer m_AnimClock;
	bool m_bPlaying = false;

	// Status bar
	QLabel *m_pStatusModel = nullptr;
	QLabel *m_pStatusCamera = nullptr;

	// Guards combo rebuild while switching models.
	bool m_bPopulating = false;
};

#endif // HLMV2_MAINWINDOW_H
