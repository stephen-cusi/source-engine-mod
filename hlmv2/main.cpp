//========= Half-Life Model Viewer 2 (Qt6 port of utils/hlmv) ================
//
// Purpose: Application entry point. Boots the QApplication with the Fusion
//          style and shows the main window.
//
//          Automated verification mode:
//              hlmv2 --selftest <path/to/model.mdl>
//          runs the headless model-inspection pipeline and exits with 0 (all
//          steps passed) or 1 (failures). Without --selftest the model path
//          (if given) is opened directly in the UI.
//
//=============================================================================

#include "localization.h"
#include "mainwindow.h"
#include "selftest.h"

#include <QApplication>
#include <QIcon>
#include <QSize>
#include <QString>


int main( int argc, char *argv[] )
{
	// Detect --selftest (and its model path argument) before any UI is
	// created; the self-test runs headless and never shows a window.
	bool bSelfTest = false;
	QString modelPath;

	for ( int i = 1; i < argc; ++i )
	{
		const QString arg = QString::fromLocal8Bit( argv[ i ] );
		if ( arg == QLatin1String( "--selftest" ) )
		{
			bSelfTest = true;
			if ( i + 1 < argc )
			{
				const QString candidate = QString::fromLocal8Bit( argv[ i + 1 ] );
				if ( !candidate.startsWith( QLatin1String( "--" ) ) )
					modelPath = candidate;
			}
			continue;
		}
		// A plain path argument: open it in the UI (ignored with --selftest
		// unless it follows the flag).
		if ( modelPath.isEmpty() && !arg.startsWith( QLatin1String( "--" ) ) )
			modelPath = arg;
	}

	// Qt looks up its platform plugins relative to the executable first; adding
	// the application directory explicitly keeps the viewer working on machines
	// with a stale QT_PLUGIN_PATH in the environment.
	QCoreApplication::addLibraryPath( QCoreApplication::applicationDirPath() );

	QApplication app( argc, argv );

	app.setOrganizationName( "hlmv2" );
	app.setApplicationName( "hlmv2" );
	app.setStyle( "Fusion" );

	// The application icon is drawn by res/gen_icon.py and embedded through
	// res/hlmv2.qrc. The Win32 .rc copy only serves Explorer; this is what
	// the window manager shows on win32, linux and darwin alike.
	QIcon appIcon;
	for ( int nSize : { 16, 24, 32, 48, 64, 128, 256 } )
	{
		appIcon.addFile( QStringLiteral( ":/icons/hlmv-%1.png" ).arg( nSize ),
			QSize( nSize, nSize ) );
	}
	app.setWindowIcon( appIcon );

	// Optional UI translation (hlmvlang/hlmv_<locale>.qm); skipped for the
	// headless self-test so its output stays English.
	HlmvLocalization localization( &app );
	if ( !bSelfTest )
		localization.LoadSavedLanguage();

	if ( bSelfTest )
		return RunHlmvSelfTest( modelPath );

	CMainWindow window;
	window.show();

	// Optional convenience: open a model passed on the command line.
	if ( !modelPath.isEmpty() )
		window.OpenModel( modelPath );

	return app.exec();
}
