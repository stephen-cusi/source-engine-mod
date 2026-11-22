//========= Hammer Qt6 (hammer2/app) ==========================================
//
// Purpose: Optional runtime application translations stored beside the
//          executable, independent of Qt's translation packages.
//
//=============================================================================

#include "localization.h"
#include "portableconfig.h"

#include <QCoreApplication>
#include <QDir>
#include <QLocale>
#include <QRegularExpression>
#include <QSettings>
#include <QTranslator>

HammerLocalization::HammerLocalization( QObject *parent )
	: QObject( parent )
	, m_strLanguageDirectory( QDir( QCoreApplication::applicationDirPath() ).filePath( QStringLiteral( "hammerlang" ) ) )
	, m_pTranslator( nullptr )
{
}

HammerLocalization::~HammerLocalization()
{
	if ( m_pTranslator && QCoreApplication::instance() )
		QCoreApplication::removeTranslator( m_pTranslator );
	delete m_pTranslator;
}

QString HammerLocalization::CatalogPath( const QString &locale ) const
{
	return QDir( m_strLanguageDirectory ).filePath( QStringLiteral( "hammer_%1.qm" ).arg( locale ) );
}

QList<HammerLanguageInfo> HammerLocalization::AvailableLanguages() const
{
	QList<HammerLanguageInfo> languages;
	HammerLanguageInfo sourceLanguage;
	sourceLanguage.displayName = QStringLiteral( "English (built-in)" );
	languages.append( sourceLanguage );

	const QDir dir( m_strLanguageDirectory );
	const QStringList catalogs = dir.entryList( QStringList() << QStringLiteral( "hammer_*.qm" ), QDir::Files, QDir::Name );
	for ( const QString &catalog : catalogs )
	{
		const QString localeName = catalog.mid( 7, catalog.length() - 10 );
		if ( localeName.isEmpty() )
			continue;

		const QLocale locale( localeName );
		QString displayName = locale.nativeLanguageName();
		if ( displayName.isEmpty() || locale.language() == QLocale::C )
			displayName = localeName;
		else if ( !locale.nativeTerritoryName().isEmpty() )
			displayName += QStringLiteral( " (%1)" ).arg( locale.nativeTerritoryName() );

		HammerLanguageInfo info;
		info.locale = localeName;
		info.displayName = displayName;
		info.filePath = dir.filePath( catalog );
		languages.append( info );
	}
	return languages;
}

bool HammerLocalization::LoadLanguage( const QString &locale )
{
	const QString normalizedLocale = locale.trimmed();
	static const QRegularExpression validLocale( QStringLiteral( "^[A-Za-z0-9_]+$" ) );
	if ( !normalizedLocale.isEmpty() && !validLocale.match( normalizedLocale ).hasMatch() )
		return false;
	if ( normalizedLocale == m_strCurrentLocale )
		return true;

	QTranslator *newTranslator = nullptr;
	if ( !normalizedLocale.isEmpty() )
	{
		const QString path = CatalogPath( normalizedLocale );
		newTranslator = new QTranslator( this );
		if ( !newTranslator->load( path ) )
		{
			delete newTranslator;
			return false;
		}
	}

	QTranslator *oldTranslator = m_pTranslator;
	if ( oldTranslator && QCoreApplication::instance() )
		QCoreApplication::removeTranslator( oldTranslator );

	if ( newTranslator && QCoreApplication::instance() &&
		!QCoreApplication::installTranslator( newTranslator ) )
	{
		delete newTranslator;
		if ( oldTranslator )
			QCoreApplication::installTranslator( oldTranslator );
		return false;
	}

	m_pTranslator = newTranslator;
	m_strCurrentLocale = normalizedLocale;
	delete oldTranslator;
	SaveLanguagePreference( m_strCurrentLocale );
	emit LanguageChanged( m_strCurrentLocale );
	return true;
}

bool HammerLocalization::LoadSavedLanguage()
{
	QSettings settings;
	const QString locale = settings.value( QStringLiteral( "language/locale" ),
		PortableDefault( QCoreApplication::applicationDirPath(), QStringLiteral( "language/locale" ) ) ).toString();
	return locale.isEmpty() || LoadLanguage( locale );
}

void HammerLocalization::SaveLanguagePreference( const QString &locale )
{
	QSettings settings;
	settings.setValue( QStringLiteral( "language/locale" ), locale );
}
