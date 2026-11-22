//========= Hammer Qt6 (hammer2/app) ==========================================
//
// Purpose: Application entry point. Boots the QApplication with the Fusion
//          style, registers resources and shows the main window.
//
//          Supports a fully automated headless verification mode:
//              hammer_qt --selftest [vmfpath]
//          which runs the load-FGD/build-map/save-VMF/reload pipeline and
//          exits with 0 (all steps passed) or 1 (failures). The default VMF
//          path is D:/tmp/hammer2_selftest.vmf on Windows and
//          /tmp/hammer2_selftest.vmf elsewhere.
//
//=============================================================================

#include "mainwindow.h"
#include "localization.h"
#include "selftest.h"
#include "portableconfig.h"
#include "theme.h"

#include <QApplication>
#include <QIcon>
#include <QMessageBox>
#include <QString>
#include <QSettings>

#ifdef _WIN32
static const char kDefaultSelfTestVmf[] = "D:/tmp/hammer2_selftest.vmf";
#else
static const char kDefaultSelfTestVmf[] = "/tmp/hammer2_selftest.vmf";
#endif

int main( int argc, char *argv[] )
{
    QApplication app( argc, argv );

    // Qt obtains Unicode arguments from the OS on Windows, including paths
    // passed by a language-switch restart.
    const QStringList arguments = QCoreApplication::arguments();
    bool bSelfTest = false;
    QString selfTestVmfPath = QString::fromLatin1( kDefaultSelfTestVmf );
    QString openMapPath;

    for ( int i = 1; i < arguments.size(); ++i )
    {
        const QString arg = arguments.at( i );
        if ( arg == QLatin1String( "--selftest" ) )
        {
            bSelfTest = true;
            if ( i + 1 < arguments.size() )
            {
                const QString candidate = arguments.at( i + 1 );
                if ( !candidate.startsWith( QLatin1String( "--" ) ) )
                    selfTestVmfPath = candidate;
            }
        }
        else if ( arg == QLatin1String( "--open" ) && i + 1 < arguments.size() )
        {
            openMapPath = arguments.at( ++i );
        }
    }

    // QSettings storage (window layout, recent files) is keyed off these
    app.setOrganizationName( "hammer2" );
    if ( !PortableConfigPath( QCoreApplication::applicationDirPath() ).isEmpty() )
    {
        QSettings::setDefaultFormat( QSettings::IniFormat );
        app.setApplicationName( "hammer_qt_portable_gmod64" );
    }
    else
        app.setApplicationName( "hammer_qt" );
    HammerTheme::Apply( app );
    app.setWindowIcon( QIcon( ":/hammer-256.png" ) );

    HammerLocalization localization( &app );
    localization.LoadSavedLanguage();

    // TODO(hammer-port): register compiled-in resources here
    // (Q_INIT_RESOURCE(hammer_app)) once a .qrc file is added to the build.

    if ( bSelfTest )
        return RunHammerSelfTest( selfTestVmfPath );

    CMainWindow window;
    if ( !openMapPath.isEmpty() && !window.OpenDocumentPath( openMapPath ) )
    {
        QMessageBox::warning( &window, QObject::tr( "Hammer" ), QObject::tr( "Could not reopen map: %1" ).arg( openMapPath ) );
        return 1;
    }
    if ( openMapPath.isEmpty() )
        window.OfferRecovery();
    window.show();

    return app.exec();
}
