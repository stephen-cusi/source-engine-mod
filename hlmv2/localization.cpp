//========= Half-Life Model Viewer 2 (hlmv2) ==================================
//
// Purpose: See localization.h.
//
//=============================================================================

#include "localization.h"

#include <QCoreApplication>
#include <QDir>
#include <QLocale>
#include <QRegularExpression>
#include <QSettings>
#include <QTranslator>

static const char kSettingsKey[] = "language/locale";

HlmvLocalization::HlmvLocalization( QObject *parent )
	: QObject( parent )
	, m_strLanguageDirectory( QDir( QCoreApplication::applicationDirPath() )
		.filePath( QStringLiteral( "hlmvlang" ) ) )
{
}

HlmvLocalization::~HlmvLocalization()
{
	if ( m_pTranslator && QCoreApplication::instance() )
		QCoreApplication::removeTranslator( m_pTranslator );
	delete m_pTranslator;
}

QList<HlmvLanguageInfo> HlmvLocalization::AvailableLanguages() const
{
	QList<HlmvLanguageInfo> languages;
	HlmvLanguageInfo builtin;
	builtin.displayName = QStringLiteral( "English (built-in)" );
	languages.append( builtin );

	const QDir dir( m_strLanguageDirectory );
	const QStringList catalogs = dir.entryList(
		QStringList() << QStringLiteral( "hlmv_*.qm" ), QDir::Files, QDir::Name );
	for ( const QString &catalog : catalogs )
	{
		// "hlmv_" prefix (5) and ".qm" suffix (3)
		const QString localeName = catalog.mid( 5, catalog.length() - 8 );
		if ( localeName.isEmpty() )
			continue;

		const QLocale locale( localeName );
		QString displayName = locale.nativeLanguageName();
		if ( displayName.isEmpty() || locale.language() == QLocale::C )
			displayName = localeName;
		else if ( !locale.nativeTerritoryName().isEmpty() )
			displayName += QStringLiteral( " (%1)" ).arg( locale.nativeTerritoryName() );

		HlmvLanguageInfo info;
		info.locale = localeName;
		info.displayName = displayName;
		languages.append( info );
	}
	return languages;
}

bool HlmvLocalization::LoadLanguage( const QString &locale )
{
	const QString normalized = locale.trimmed();
	static const QRegularExpression validLocale( QStringLiteral( "^[A-Za-z0-9_]+$" ) );
	if ( !normalized.isEmpty() && !validLocale.match( normalized ).hasMatch() )
		return false;

	QTranslator *pNew = nullptr;
	if ( !normalized.isEmpty() )
	{
		pNew = new QTranslator( this );
		const QString path = QDir( m_strLanguageDirectory )
			.filePath( QStringLiteral( "hlmv_%1.qm" ).arg( normalized ) );
		if ( !pNew->load( path ) )
		{
			delete pNew;
			return false;
		}
	}

	QTranslator *pOld = m_pTranslator;
	if ( pOld )
		QCoreApplication::removeTranslator( pOld );
	if ( pNew && !QCoreApplication::installTranslator( pNew ) )
	{
		delete pNew;
		if ( pOld )
			QCoreApplication::installTranslator( pOld );
		return false;
	}

	m_pTranslator = pNew;
	m_strCurrentLocale = normalized;
	delete pOld;

	QSettings settings;
	settings.setValue( QLatin1String( kSettingsKey ), m_strCurrentLocale );
	return true;
}

bool HlmvLocalization::LoadSavedLanguage()
{
	QSettings settings;
	const QString locale = settings.value( QLatin1String( kSettingsKey ) ).toString();
	return locale.isEmpty() || LoadLanguage( locale );
}
