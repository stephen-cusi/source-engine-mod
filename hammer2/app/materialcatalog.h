#pragma once

#include <QByteArray>
#include <QDir>
#include <QDirIterator>
#include <QFile>
#include <QFileInfo>
#include <QRegularExpression>
#include <QSet>
#include <QStringList>
#include <QtEndian>
#include "tier1/KeyValues.h"

// Index VPK directory trees only. No archive payloads are read or modified.
inline bool IndexVpkMaterials( const QString &filename, QSet<QString> &materials )
{
	QFile file( filename );
	if ( !file.open( QIODevice::ReadOnly ) ) return false;
	const QByteArray header = file.read( 28 );
	if ( header.size() < 12 ) return false;
	const auto u32 = [&]( int p ) { return qFromLittleEndian<quint32>( reinterpret_cast<const uchar *>( header.constData() + p ) ); };
	const quint32 version = u32( 4 ), length = u32( 8 );
	if ( u32( 0 ) != 0x55aa1234u || ( version != 1 && version != 2 ) || length > 64u * 1024u * 1024u ) return false;
	const qint64 offset = version == 1 ? 12 : 28;
	if ( file.size() < offset + length || !file.seek( offset ) ) return false;
	const QByteArray tree = file.read( length );
	if ( tree.size() != static_cast<int>( length ) ) return false;
	int pos = 0;
	const auto string = [&]( QByteArray &out ) {
		const int end = tree.indexOf( '\0', pos );
		if ( end < 0 ) return false;
		out = tree.mid( pos, end - pos ); pos = end + 1; return true;
	};
	QSet<QString> found;
	QByteArray ext, path, name;
	while ( true )
	{
		if ( !string( ext ) ) return false;
		if ( ext.isEmpty() ) break;
		while ( true )
		{
			if ( !string( path ) ) return false;
			if ( path.isEmpty() ) break;
			while ( true )
			{
				if ( !string( name ) ) return false;
				if ( name.isEmpty() ) break;
				if ( pos > tree.size() - 18 ) return false;
				const uchar *entry = reinterpret_cast<const uchar *>( tree.constData() + pos );
				const quint16 preload = qFromLittleEndian<quint16>( entry + 4 );
				if ( qFromLittleEndian<quint16>( entry + 16 ) != 0xffff || preload > tree.size() - pos - 18 ) return false;
				pos += 18 + preload;
				if ( ext.compare( "vmt", Qt::CaseInsensitive ) != 0 ) continue;
				QString rel = QString::fromUtf8( path ) + QLatin1Char( '/' ) + QString::fromUtf8( name );
				rel.replace( QLatin1Char( '\\' ), QLatin1Char( '/' ) );
				if ( !rel.startsWith( QLatin1String( "materials/" ), Qt::CaseInsensitive ) ) continue;
				rel.remove( 0, 10 );
				const QStringList parts = rel.split( QLatin1Char( '/' ) );
				if ( rel.contains( QLatin1Char( ':' ) ) || parts.contains( QString() ) || parts.contains( QLatin1String( "." ) ) || parts.contains( QLatin1String( ".." ) ) ) continue;
				found.insert( rel.toLower() );
			}
		}
	}
	materials.unite( found );
	return true;
}

struct MaterialCatalogResult { QStringList names; QStringList failedArchives; };

// GameInfo paths without a token are relative to the engine base directory;
// |gameinfo_path| paths are relative to the game directory (filesystem_init.cpp).
inline QStringList MaterialSearchPaths( const QString &gameDir )
{
	QStringList result;
	QFile file( QDir( gameDir ).filePath( QStringLiteral( "gameinfo.txt" ) ) );
	if ( !file.open( QIODevice::ReadOnly ) || file.size() > 4 * 1024 * 1024 ) return result;
	const QByteArray bytes = file.readAll();
	// KeyValues requires an engine filesystem for these directives, which this
	// standalone scanner does not own. Keep the ordinary game scan in that case.
	if ( QRegularExpression( QStringLiteral( "(^|\\n)\\s*#(?:base|include)\\b" ), QRegularExpression::CaseInsensitiveOption ).match( QString::fromUtf8( bytes ) ).hasMatch() ) return result;
	KeyValues *info = new KeyValues( "GameInfo" );
	if ( !info->LoadFromBuffer( "gameinfo.txt", bytes.constData() ) ) { info->deleteThis(); return result; }
	KeyValues *paths = info->FindKey( "FileSystem/SearchPaths" );
	const QDir game( gameDir );
	const QDir base( game.absoluteFilePath( QStringLiteral( ".." ) ) );
	for ( KeyValues *entry = paths ? paths->GetFirstValue() : nullptr; entry; entry = entry->GetNextValue() )
	{
		const QStringList ids = QString::fromUtf8( entry->GetName() ).split( QLatin1Char( '+' ) );
		bool isGame = false;
		for ( const QString &id : ids ) if ( id.trimmed().compare( QLatin1String( "game" ), Qt::CaseInsensitive ) == 0 ) isGame = true;
		if ( !isGame ) continue;
		QString path = QString::fromUtf8( entry->GetString() );
		const QString localToken = QStringLiteral( "|gameinfo_path|" );
		const QString engineToken = QStringLiteral( "|all_source_engine_paths|" );
		const QDir *root = &base;
		if ( path.startsWith( localToken, Qt::CaseInsensitive ) ) { path.remove( 0, localToken.size() ); root = &game; }
		else if ( path.startsWith( engineToken, Qt::CaseInsensitive ) ) path.remove( 0, engineToken.size() );
		else if ( path.startsWith( QLatin1Char( '|' ) ) ) continue;
		path = QDir::cleanPath( root->absoluteFilePath( path ) );
		if ( path.contains( QLatin1Char( '*' ) ) || path.contains( QLatin1Char( '?' ) ) )
		{
			const QFileInfo pattern( path );
			const QDir dir = pattern.dir();
			for ( const QFileInfo &match : dir.entryInfoList( QStringList() << pattern.fileName(), QDir::Files | QDir::Dirs | QDir::NoDotAndDotDot, QDir::Name | QDir::IgnoreCase ) )
				if ( match.isDir() || match.suffix().compare( QLatin1String( "vpk" ), Qt::CaseInsensitive ) == 0 ) result.append( match.absoluteFilePath() );
		}
		else result.append( path );
	}
	info->deleteThis();
	return result;
}

inline MaterialCatalogResult ScanGameMaterials( const QString &gameDir, const QStringList &searchPaths )
{
	MaterialCatalogResult result;
	QSet<QString> names;
	QStringList paths = QStringList() << QDir( gameDir ).absolutePath();
	paths.append( searchPaths );
	const QString gameIdentity = QDir::cleanPath( QDir( gameDir ).absolutePath() ).toLower();
	QSet<QString> visited;
	for ( QString path : paths )
	{
		if ( path.endsWith( QLatin1String( ".vpk" ), Qt::CaseInsensitive ) && !path.endsWith( QLatin1String( "_dir.vpk" ), Qt::CaseInsensitive ) )
		{
			const QString dirVpk = path.left( path.size() - 4 ) + QStringLiteral( "_dir.vpk" );
			if ( QFile::exists( dirVpk ) ) path = dirVpk;
			else if ( QRegularExpression( QStringLiteral( "_[0-9]{3}\\.vpk$" ), QRegularExpression::CaseInsensitiveOption ).match( path ).hasMatch() ) continue;
		}
		const QString identity = QDir::cleanPath( path ).toLower();
		if ( visited.contains( identity ) ) continue;
		visited.insert( identity );
		if ( path.endsWith( QLatin1String( ".vpk" ), Qt::CaseInsensitive ) )
		{
			if ( QFile::exists( path ) && !IndexVpkMaterials( path, names ) ) result.failedArchives.append( path );
			continue;
		}
		const QDir root( path );
		const QString materialsDir = root.filePath( QStringLiteral( "materials" ) );
		QDirIterator it( materialsDir, QStringList() << QStringLiteral( "*.vmt" ), QDir::Files, QDirIterator::Subdirectories );
		const QDir base( materialsDir );
		while ( it.hasNext() )
		{
			QString rel = base.relativeFilePath( it.next() );
			rel.chop( 4 ); names.insert( rel.toLower() );
		}
		// Preserve the original catalog's compatibility scan for the chosen game
		// directory only. Other directory search paths do not mount nested VPKs.
		const QStringList archives = identity == gameIdentity ? root.entryList( QStringList() << QStringLiteral( "*_dir.vpk" ), QDir::Files ) : QStringList();
		for ( const QString &file : archives )
		{
			const QString archive = root.absoluteFilePath( file );
			const QString archiveIdentity = QDir::cleanPath( archive ).toLower();
			if ( visited.contains( archiveIdentity ) ) continue;
			visited.insert( archiveIdentity );
			if ( !IndexVpkMaterials( archive, names ) ) result.failedArchives.append( archive );
		}
	}
	result.names = names.values(); result.names.sort( Qt::CaseInsensitive );
	return result;
}

// Call on the UI thread: Source KeyValues has a process-wide parser error stack.
inline MaterialCatalogResult ScanGameMaterials( const QString &gameDir )
{
	return ScanGameMaterials( gameDir, MaterialSearchPaths( gameDir ) );
}
