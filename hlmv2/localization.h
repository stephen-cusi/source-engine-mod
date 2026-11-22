//========= Half-Life Model Viewer 2 (hlmv2) ==================================
//
// Purpose: Optional runtime UI translations stored beside the executable in
//          hlmvlang/ (hlmv_<locale>.qm). Same scheme as hammer2's
//          HammerLocalization, but a separate class and catalog directory so
//          the two tools stay independent. A language change takes effect on
//          the next start.
//
//=============================================================================

#ifndef HLMV2_LOCALIZATION_H
#define HLMV2_LOCALIZATION_H

#include <QList>
#include <QObject>
#include <QString>

class QTranslator;

struct HlmvLanguageInfo
{
	QString locale;       // empty = built-in English
	QString displayName;
};

class HlmvLocalization : public QObject
{
	Q_OBJECT

public:
	explicit HlmvLocalization( QObject *parent = nullptr );
	~HlmvLocalization() override;

	QList<HlmvLanguageInfo> AvailableLanguages() const;
	QString CurrentLanguage() const { return m_strCurrentLocale; }

	// Empty locale restores the built-in English strings. Persists the choice.
	bool LoadLanguage( const QString &locale );
	bool LoadSavedLanguage();

private:
	QString m_strLanguageDirectory;
	QString m_strCurrentLocale;
	QTranslator *m_pTranslator = nullptr;
};

#endif // HLMV2_LOCALIZATION_H
