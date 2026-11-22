#pragma once

#include "mapmodel.h"
#include "vmfio.h"

#include <QDateTime>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QJsonDocument>
#include <QJsonObject>
#include <QSaveFile>
#include <QStandardPaths>
#include <QUuid>

struct HammerRecoveryInfo
{
	QString vmfPath;
	QString sourcePath;
	QDateTime savedAt;
};

class CHammerRecoveryStore
{
public:
	explicit CHammerRecoveryStore( const QString &root = QStandardPaths::writableLocation( QStandardPaths::AppLocalDataLocation ) + QStringLiteral( "/recovery" ) )
		: m_root( root ) {}

	QString ManifestPath() const { return QDir( m_root ).filePath( QStringLiteral( "current.json" ) ); }

	bool Write( const MapDocLite &map, const QString &sourcePath, QString *error = nullptr ) const
	{
		if ( m_root.isEmpty() || ( !QDir().mkpath( m_root ) ) )
		{
			if ( error ) *error = QStringLiteral( "Could not create recovery directory." );
			return false;
		}
		const QString oldSnapshot = CurrentSnapshotPath();
		const QString snapshotName = QStringLiteral( "recovery_%1.vmf" ).arg( QUuid::createUuid().toString( QUuid::WithoutBraces ) );
		const QString snapshotPath = QDir( m_root ).filePath( snapshotName );
		QString saveError;
		MapDocLite snapshot = map;
		if ( !VmfIO::SaveMap( snapshotPath, snapshot, &saveError ) )
		{
			if ( error ) *error = saveError;
			return false;
		}
		const QDateTime now = QDateTime::currentDateTimeUtc();
		QJsonObject record;
		record.insert( QStringLiteral( "version" ), 1 );
		record.insert( QStringLiteral( "file" ), snapshotName );
		record.insert( QStringLiteral( "source" ), sourcePath );
		record.insert( QStringLiteral( "savedAt" ), now.toString( Qt::ISODateWithMs ) );
		QSaveFile file( ManifestPath() );
		if ( !file.open( QIODevice::WriteOnly ) || file.write( QJsonDocument( record ).toJson( QJsonDocument::Compact ) ) < 0 || !file.commit() )
		{
			QFile::remove( snapshotPath );
			if ( error ) *error = file.errorString();
			return false;
		}
		if ( !oldSnapshot.isEmpty() && oldSnapshot != snapshotPath ) QFile::remove( oldSnapshot );
		return true;
	}

	bool Read( HammerRecoveryInfo &info ) const
	{
		if ( m_root.isEmpty() ) return false; // an empty root would resolve against the working directory
		QFile manifest( ManifestPath() );
		if ( !manifest.open( QIODevice::ReadOnly ) ) return false;
		QJsonParseError parseError;
		const QJsonDocument json = QJsonDocument::fromJson( manifest.readAll(), &parseError );
		if ( parseError.error != QJsonParseError::NoError || !json.isObject() || json.object().value( QStringLiteral( "version" ) ).toInt() != 1 ) return false;
		const QJsonObject record = json.object();
		const QString fileName = record.value( QStringLiteral( "file" ) ).toString();
		if ( fileName.isEmpty() || QFileInfo( fileName ).fileName() != fileName ||
			!fileName.startsWith( QLatin1String( "recovery_" ) ) || !fileName.endsWith( QLatin1String( ".vmf" ), Qt::CaseInsensitive ) ) return false;
		const QFileInfo vmf( QDir( m_root ).filePath( fileName ) );
		if ( !vmf.isFile() || vmf.size() <= 0 ) return false;
		info.vmfPath = vmf.absoluteFilePath();
		info.sourcePath = record.value( QStringLiteral( "source" ) ).toString();
		info.savedAt = QDateTime::fromString( record.value( QStringLiteral( "savedAt" ) ).toString(), Qt::ISODateWithMs );
		return true;
	}

	void Clear() const
	{
		if ( m_root.isEmpty() ) return; // never delete from the working directory
		const QDir root( m_root );
		for ( const QString &file : root.entryList( QStringList() << QStringLiteral( "recovery_*.vmf" ), QDir::Files ) )
			QFile::remove( root.filePath( file ) );
		QFile::remove( ManifestPath() );
	}

private:
	QString CurrentSnapshotPath() const
	{
		HammerRecoveryInfo info;
		return Read( info ) ? info.vmfPath : QString();
	}

	QString m_root;
};
