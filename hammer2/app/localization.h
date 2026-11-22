//========= Hammer Qt6 (hammer2/app) ==========================================
//
// Purpose: Loads optional application translations from the sibling
//          hammerlang directory without relying on Qt's own translation
//          catalogs. Catalog names are hammer_<locale>.qm.
//
//=============================================================================

#pragma once

#include <QObject>
#include <QString>
#include <QList>

class QTranslator;

struct HammerLanguageInfo
{
	QString locale;
	QString displayName;
	QString filePath;
};

class HammerLocalization : public QObject
{
	Q_OBJECT

public:
	explicit HammerLocalization( QObject *parent = nullptr );
	~HammerLocalization() override;

	QList<HammerLanguageInfo> AvailableLanguages() const;
	QString CurrentLanguage() const { return m_strCurrentLocale; }
	QString LanguageDirectory() const { return m_strLanguageDirectory; }

	// Empty locale unloads the catalog and restores source-language strings.
	bool LoadLanguage( const QString &locale );
	bool LoadSavedLanguage();

signals:
	void LanguageChanged( const QString &locale );

private:
	QString CatalogPath( const QString &locale ) const;
	void SaveLanguagePreference( const QString &locale );

	QString m_strLanguageDirectory;
	QString m_strCurrentLocale;
	QTranslator *m_pTranslator;
};
