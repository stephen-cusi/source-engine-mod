#pragma once

#include <QCryptographicHash>
#include <QDateTime>
#include <QFile>
#include <QFileInfo>

// Detect a fresh compiler output without treating a leftover .lin as a leak.
struct PointFileState
{
	bool exists = false;
	bool readable = false;
	QDateTime modified;
	QByteArray digest;
};

inline PointFileState CapturePointFileState( const QString &path )
{
	PointFileState state;
	QFileInfo info( path );
	state.exists = info.exists();
	if ( !state.exists ) return state;
	state.modified = info.lastModified();
	QFile file( path );
	if ( !file.open( QIODevice::ReadOnly ) ) return state;
	QCryptographicHash hash( QCryptographicHash::Sha256 );
	state.readable = hash.addData( &file );
	if ( state.readable ) state.digest = hash.result();
	return state;
}

inline bool IsFreshPointFile( const PointFileState &before, const PointFileState &after )
{
	return after.exists && after.readable &&
		( !before.exists || ( before.readable &&
		  ( before.modified != after.modified || before.digest != after.digest ) ) );
}
