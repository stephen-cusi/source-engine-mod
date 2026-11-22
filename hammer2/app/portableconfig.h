#pragma once

#include <QDir>
#include <QFileInfo>
#include <QSettings>
#include <QVariant>

// An explicitly supplied companion file enables an isolated portable profile.
// It is read-only; preferences remain in the user's writable configuration.
inline QString PortableConfigPath( const QString &exeDir )
{
	const QString path = QDir( exeDir ).filePath( QStringLiteral( "hammer2-portable.ini" ) );
	return QFileInfo( path ).isFile() ? path : QString();
}

inline QVariant PortableDefault( const QString &exeDir, const QString &key, const QVariant &fallback = QVariant() )
{
	const QString path = PortableConfigPath( exeDir );
	if ( path.isEmpty() ) return fallback;
	QSettings defaults( path, QSettings::IniFormat );
	return defaults.value( QStringLiteral( "defaults/" ) + key, fallback );
}
