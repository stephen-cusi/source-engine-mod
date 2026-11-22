#include "materialpreviewrenderer.h"

#include "brushwindings.h"
#include "materialcatalog.h"

#include "tier1/utlbuffer.h"
#include "tier1/KeyValues.h"
#include "vtf/vtf.h"

#include <QDir>
#include <QCryptographicHash>
#include <QDateTime>
#include <QDirIterator>
#include <QFile>
#include <QFileInfo>
#include <QImage>
#include <QMatrix4x4>
#include <QMap>
#include <QMutex>
#include <QMutexLocker>
#include <QRegularExpression>
#include <QOpenGLContext>
#include <QSharedPointer>
#include <QtEndian>

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <cstring>

namespace
{
constexpr quint32 kVpkSignature = 0x55aa1234u;
constexpr quint16 kVpkInlineArchive = 0x7fff;
constexpr qint64 kMaxAssetBytes = 256ll * 1024 * 1024;

struct VpkEntry
{
	quint16 archive = kVpkInlineArchive;
	quint32 offset = 0;
	quint32 length = 0;
	QByteArray preload;
};

class VpkDirectory
{
public:
	bool Open( const QString &directoryFile )
	{
		m_directoryFile = directoryFile;
		QFile file( directoryFile );
		if ( !file.open( QIODevice::ReadOnly ) ) return false;
		const QByteArray header = file.read( 28 );
		if ( header.size() < 12 ) return false;
		const auto u32 = [&]( int p ) { return qFromLittleEndian<quint32>( reinterpret_cast<const uchar *>( header.constData() + p ) ); };
		if ( u32( 0 ) != kVpkSignature ) return false;
		const quint32 version = u32( 4 ), treeSize = u32( 8 );
		if ( ( version != 1 && version != 2 ) || treeSize > 64u * 1024u * 1024u ) return false;
		const qint64 headerSize = version == 1 ? 12 : 28;
		if ( file.size() < headerSize + treeSize || !file.seek( headerSize ) ) return false;
		const QByteArray tree = file.read( treeSize );
		if ( tree.size() != static_cast<int>( treeSize ) ) return false;
		m_inlineDataOffset = headerSize + treeSize;
		int pos = 0;
		const auto nextString = [&]( QByteArray &out ) {
			const int end = tree.indexOf( '\0', pos );
			if ( end < 0 ) return false;
			out = tree.mid( pos, end - pos );
			pos = end + 1;
			return true;
		};
		QByteArray ext, path, name;
		while ( true )
		{
			if ( !nextString( ext ) ) return false;
			if ( ext.isEmpty() ) break;
			while ( true )
			{
				if ( !nextString( path ) ) return false;
				if ( path.isEmpty() ) break;
				while ( true )
				{
					if ( !nextString( name ) ) return false;
					if ( name.isEmpty() ) break;
					if ( pos > tree.size() - 18 ) return false;
					const uchar *record = reinterpret_cast<const uchar *>( tree.constData() + pos );
					const quint16 preloadSize = qFromLittleEndian<quint16>( record + 4 );
					VpkEntry entry;
					entry.archive = qFromLittleEndian<quint16>( record + 6 );
					entry.offset = qFromLittleEndian<quint32>( record + 8 );
					entry.length = qFromLittleEndian<quint32>( record + 12 );
					if ( qFromLittleEndian<quint16>( record + 16 ) != 0xffff || preloadSize > tree.size() - pos - 18 ) return false;
					pos += 18;
					entry.preload = tree.mid( pos, preloadSize );
					pos += preloadSize;
					QString fileName = QString::fromUtf8( name );
					QString extension = QString::fromUtf8( ext );
					if ( fileName == QLatin1String( " " ) ) fileName.clear();
					QString relative = ( path == QLatin1String( " " ) ? QString() : QString::fromUtf8( path ) + QLatin1Char( '/' ) ) + fileName + QLatin1Char( '.' ) + extension;
					relative.replace( QLatin1Char( '\\' ), QLatin1Char( '/' ) );
					m_entries.insert( relative.toLower(), entry );
				}
			}
		}
		return true;
	}

	QByteArray Read( const QString &relativePath ) const
	{
		const auto it = m_entries.constFind( relativePath.toLower() );
		if ( it == m_entries.cend() ) return {};
		const VpkEntry &entry = it.value();
		if ( entry.length > kMaxAssetBytes || entry.preload.size() + entry.length > kMaxAssetBytes ) return {};
		QByteArray result = entry.preload;
		if ( entry.length == 0 ) return result;
		QString archivePath;
		qint64 base = 0;
		if ( entry.archive == kVpkInlineArchive )
		{
			archivePath = m_directoryFile;
			base = m_inlineDataOffset;
		}
		else
		{
			QString stem = m_directoryFile;
			if ( stem.endsWith( QLatin1String( "_dir.vpk" ), Qt::CaseInsensitive ) ) stem.chop( 8 );
			archivePath = QStringLiteral( "%1_%2.vpk" ).arg( stem ).arg( entry.archive, 3, 10, QLatin1Char( '0' ) );
		}
		QFile file( archivePath );
		const qint64 offset = base + entry.offset;
		if ( !file.open( QIODevice::ReadOnly ) || offset < 0 || offset + entry.length > file.size() || !file.seek( offset ) ) return {};
		const QByteArray payload = file.read( entry.length );
		if ( payload.size() != static_cast<int>( entry.length ) ) return {};
		result.append( payload );
		return result;
	}

private:
	QString m_directoryFile;
	qint64 m_inlineDataOffset = 0;
	QHash<QString, VpkEntry> m_entries;
};

bool ValidAssetName( QString &name, const QString &extension )
{
	name = name.trimmed();
	name.replace( QLatin1Char( '\\' ), QLatin1Char( '/' ) );
	if ( name.startsWith( QLatin1String( "materials/" ), Qt::CaseInsensitive ) ) name.remove( 0, 10 );
	if ( name.endsWith( extension, Qt::CaseInsensitive ) ) name.chop( extension.size() );
	const QStringList parts = name.split( QLatin1Char( '/' ) );
	return !name.isEmpty() && !name.contains( QLatin1Char( ':' ) ) &&
		!parts.contains( QString() ) && !parts.contains( QLatin1String( "." ) ) && !parts.contains( QLatin1String( ".." ) );
}

// KeyValues::LoadFromBuffer reports some malformed input only to the global
// error stack and still returns true. Validate the supported VMT grammar first.
bool ValidatePreviewVmt( const QByteArray &bytes )
{
	if ( bytes.isEmpty() || bytes.size() > 4 * 1024 * 1024 || bytes.contains( '\0' ) ) return false;
	struct Token { QByteArray text; bool quoted; };
	QVector<Token> tokens;
	for ( qsizetype i = 0; i < bytes.size(); )
	{
		const char c = bytes[i];
		if ( c == ' ' || c == '\t' || c == '\r' || c == '\n' ) { ++i; continue; }
		if ( c == '/' && i + 1 < bytes.size() && bytes[i + 1] == '/' )
		{
			while ( i < bytes.size() && bytes[i] != '\n' ) ++i;
			continue;
		}
		if ( c == '"' )
		{
			const qsizetype start = ++i;
			while ( i < bytes.size() && bytes[i] != '"' ) ++i;
			if ( i == bytes.size() ) return false;
			tokens.append( { bytes.mid( start, i - start ), true } );
			++i;
		}
		else if ( c == '{' || c == '}' )
		{
			tokens.append( { QByteArray( 1, c ), false } );
			++i;
		}
		else
		{
			const qsizetype start = i;
			while ( i < bytes.size() && bytes[i] != ' ' && bytes[i] != '\t' && bytes[i] != '\r' && bytes[i] != '\n' && bytes[i] != '{' && bytes[i] != '}' && bytes[i] != '"' ) ++i;
			if ( start == i ) return false;
			const QByteArray token = bytes.mid( start, i - start );
			if ( token.startsWith( '#' ) || token.contains( '[' ) || token.contains( ']' ) ) return false;
			tokens.append( { token, false } );
		}
		if ( tokens.size() > 100000 ) return false;
	}
	auto isBrace = [&]( qsizetype i, char brace ) {
		return i < tokens.size() && !tokens[i].quoted && tokens[i].text == QByteArray( 1, brace );
	};
	if ( tokens.size() < 3 || tokens[0].text.startsWith( '#' ) || isBrace( 0, '{' ) || isBrace( 0, '}' ) || !isBrace( 1, '{' ) ) return false;
	qsizetype pos = 2;
	int depth = 1;
	while ( pos < tokens.size() && depth > 0 )
	{
		if ( isBrace( pos, '}' ) ) { --depth; ++pos; continue; }
		if ( isBrace( pos, '{' ) || tokens[pos].text.isEmpty() ) return false;
		++pos;
		if ( pos >= tokens.size() || isBrace( pos, '}' ) ) return false;
		if ( isBrace( pos, '{' ) ) { if ( ++depth > 32 ) return false; }
		++pos;
	}
	return depth == 0 && pos == tokens.size();
}

QByteArray ReadLooseFile( const QString &root, const QString &relative )
{
	const QString path = QDir( root ).absoluteFilePath( relative );
	const QString cleanRoot = QDir::cleanPath( QFileInfo( root ).absoluteFilePath() );
	const QString cleanPath = QDir::cleanPath( QFileInfo( path ).absoluteFilePath() );
	if ( cleanPath.compare( cleanRoot, Qt::CaseInsensitive ) == 0 ||
		!cleanPath.startsWith( cleanRoot + QLatin1Char( '/' ), Qt::CaseInsensitive ) ) return {};
	QFile file( cleanPath );
	if ( !file.open( QIODevice::ReadOnly ) || file.size() < 0 || file.size() > kMaxAssetBytes ) return {};
	return file.readAll();
}

QByteArray ReadMountedFile( const QStringList &searchPaths, const QString &relative )
{
	struct CachedVpk
	{
		QSharedPointer<VpkDirectory> directory;
		qint64 size = -1;
		QDateTime modified;
	};
	static QHash<QString, CachedVpk> vpkCache;
	// Browser previews may read an index while the 3D viewport is rendering.
	// Protect the shared cache and its entries across both callers.
	static QMutex vpkMutex;
	for ( const QString &root : searchPaths )
	{
		if ( root.endsWith( QLatin1String( ".vpk" ), Qt::CaseInsensitive ) )
		{
			QMutexLocker lock( &vpkMutex );
			QString indexPath = root;
			if ( !indexPath.endsWith( QLatin1String( "_dir.vpk" ), Qt::CaseInsensitive ) )
			{
				const QString candidate = indexPath.left( indexPath.size() - 4 ) + QStringLiteral( "_dir.vpk" );
				// A GAME search path may name either a split archive by its base
				// name or an actual single-file VPK. Match the catalog's choice.
				if ( QFileInfo::exists( candidate ) ) indexPath = candidate;
			}
			const QString key = QDir::cleanPath( indexPath ).toLower();
			const QFileInfo info( indexPath );
			auto cached = vpkCache.find( key );
			if ( !info.isFile() || ( cached != vpkCache.end() &&
				( cached->size != info.size() || cached->modified != info.lastModified() ) ) )
			{
				vpkCache.remove( key );
				cached = vpkCache.end();
			}
			if ( !info.isFile() ) continue;
			if ( cached == vpkCache.end() )
			{
				auto archive = QSharedPointer<VpkDirectory>::create();
				if ( !archive->Open( indexPath ) ) continue;
				cached = vpkCache.insert( key, CachedVpk{ archive, info.size(), info.lastModified() } );
			}
			// The index describes offsets in both the directory VPK and external
			// numbered chunks. Read() opens those files anew for every lookup.
			QByteArray data = cached->directory->Read( relative );
			if ( !data.isEmpty() ) return data;
		}
		else
		{
			QByteArray data = ReadLooseFile( root, relative );
			if ( !data.isEmpty() ) return data;
		}
	}
	return {};
}
}

CMaterialPreviewRenderer::CMaterialPreviewRenderer() = default;

CMaterialPreviewRenderer::~CMaterialPreviewRenderer()
{
	Release();
}

void CMaterialPreviewRenderer::SetGameDirectory( const QString &gameDir )
{
	const QString clean = QDir::cleanPath( QFileInfo( gameDir ).absoluteFilePath() );
	// Selecting the same game again is an explicit refresh: gameinfo.txt or
	// mount.cfg may have changed, and cached GPU textures may be stale too.
	m_gameDirectory = clean;
	m_searchPaths = MaterialSearchPaths( clean );
	QFile mountConfig( QDir( clean ).filePath( QStringLiteral( "cfg/mount.cfg" ) ) );
	if ( mountConfig.open( QIODevice::ReadOnly ) && mountConfig.size() <= 4 * 1024 * 1024 )
	{
		const QByteArray bytes = mountConfig.readAll();
		KeyValues *root = new KeyValues( "mountcfg" );
		if ( root->LoadFromBuffer( "mount.cfg", bytes.constData() ) )
		{
			KeyValues *mounts = root->FindKey( "mountcfg" );
			for ( KeyValues *entry = mounts ? mounts->GetFirstValue() : nullptr; entry; entry = entry->GetNextValue() )
			{
				QString path = QString::fromUtf8( entry->GetString() );
				if ( path.isEmpty() ) continue;
				if ( !QDir::isAbsolutePath( path ) ) path = QDir( clean ).absoluteFilePath( path );
				path = QDir::cleanPath( path );
				if ( path.endsWith( QLatin1String( ".vpk" ), Qt::CaseInsensitive ) )
					m_searchPaths.append( path );
				else if ( QFileInfo( path ).isDir() )
				{
					m_searchPaths.append( path );
					QDirIterator archives( path, QStringList() << QStringLiteral( "*_dir.vpk" ), QDir::Files, QDirIterator::NoIteratorFlags );
					while ( archives.hasNext() ) m_searchPaths.append( archives.next() );
				}
			}
		}
		root->deleteThis();
	}
	m_textureCacheDirty = true;
}

bool CMaterialPreviewRenderer::Initialize()
{
	if ( m_initialized ) return true;
	if ( !QOpenGLContext::currentContext() ) return false;
	initializeOpenGLFunctions();
	m_initialized = EnsureProgram();
	return m_initialized;
}

bool CMaterialPreviewRenderer::EnsureProgram()
{
	if ( m_program.isLinked() ) return true;
	const char *vertexSource =
		"attribute vec3 aPosition;\n"
		"attribute vec2 aUv;\n"
		"uniform mat4 uMvp;\n"
		"varying vec2 vUv;\n"
		"void main(){ gl_Position=uMvp*vec4(aPosition,1.0); vUv=aUv; }\n";
	const char *fragmentSource =
		"uniform sampler2D uTexture;\n"
		"uniform vec4 uColor;\n"
		"uniform int uTextured;\n"
		"uniform int uFaceSelected;\n"
		"uniform vec2 uTextureSize;\n"
		"varying vec2 vUv;\n"
		"void main(){ vec4 base = uTextured!=0 ? texture2D(uTexture,vUv/uTextureSize)*uColor : uColor; "
		"gl_FragColor = uFaceSelected!=0 ? vec4(mix(base.rgb,vec3(1.0,0.25,0.12),0.55),base.a) : base; }\n";
	if ( !m_program.addShaderFromSourceCode( QOpenGLShader::Vertex, vertexSource ) ||
		!m_program.addShaderFromSourceCode( QOpenGLShader::Fragment, fragmentSource ) ) return false;
	m_program.bindAttributeLocation( "aPosition", 0 );
	m_program.bindAttributeLocation( "aUv", 1 );
	return m_program.link();
}

void CMaterialPreviewRenderer::Release()
{
	if ( !m_initialized || !QOpenGLContext::currentContext() ) return;
	for ( const TextureEntry &entry : m_textures ) if ( entry.id ) glDeleteTextures( 1, &entry.id );
	m_textures.clear();
	m_program.removeAllShaders();
	m_initialized = false;
}

bool CMaterialPreviewRenderer::ResolveBaseTexture( const QString &materialName, QString &textureName, QString *detail ) const
{
	QString name = materialName;
	if ( !ValidAssetName( name, QStringLiteral( ".vmt" ) ) )
	{
		if ( detail ) *detail = QStringLiteral( "invalid material name" );
		return false;
	}
	QMap<QString, QString> inserts, replacements, base;
	QSet<QString> visited;
	bool foundBase = false;
	for ( int depth = 0; depth <= 10; ++depth )
	{
		const QString identity = name.toLower();
		if ( visited.contains( identity ) ) { if ( detail ) *detail = QStringLiteral( "VMT patch include cycle" ); return false; }
		visited.insert( identity );
		const QByteArray vmt = ReadMountedFile( m_searchPaths, QStringLiteral( "materials/%1.vmt" ).arg( name ) );
		if ( vmt.isEmpty() ) { if ( detail ) *detail = QStringLiteral( "VMT not found: %1" ).arg( name ); return false; }
		// Directives and conditional tokens need an engine filesystem and game-
		// specific evaluation; reject them without misreading quoted values.
		if ( !ValidatePreviewVmt( vmt ) )
		{ if ( detail ) *detail = QStringLiteral( "invalid or unsupported VMT syntax" ); return false; }
		KeyValues::AutoDelete root( new KeyValues( "material" ) );
		if ( !root->LoadFromBuffer( "material.vmt", vmt.constData() ) )
		{ if ( detail ) *detail = QStringLiteral( "invalid VMT" ); return false; }
		const QString shader = QString::fromUtf8( root->GetName() );
		const bool patch = shader.compare( QLatin1String( "patch" ), Qt::CaseInsensitive ) == 0;
		if ( !patch && shader.compare( QLatin1String( "LightmappedGeneric" ), Qt::CaseInsensitive ) != 0 &&
			shader.compare( QLatin1String( "VertexLitGeneric" ), Qt::CaseInsensitive ) != 0 &&
			shader.compare( QLatin1String( "UnlitGeneric" ), Qt::CaseInsensitive ) != 0 )
		{ if ( detail ) *detail = QStringLiteral( "unsupported VMT shader" ); return false; }
		auto collect = []( KeyValues *section, QMap<QString, QString> &values, bool allowProxies = false ) {
			for ( KeyValues *entry = section->GetFirstSubKey(); entry; entry = entry->GetNextKey() )
			{
				if ( entry->GetDataType() == KeyValues::TYPE_NONE )
				{
					// A static preview can show the base image without executing
					// material proxies. Other nested shader sections remain unsupported.
					if ( allowProxies && !QByteArray( entry->GetName() ).compare( "Proxies", Qt::CaseInsensitive ) ) continue;
					return false;
				}
				values.insert( QString::fromUtf8( entry->GetName() ).toLower(), QString::fromUtf8( entry->GetString() ) );
			}
			return true;
		};
		if ( !patch )
		{
			if ( !collect( root, base, true ) ) { if ( detail ) *detail = QStringLiteral( "unsupported VMT nested section" ); return false; }
			foundBase = true;
			break;
		}
		if ( depth == 10 ) { if ( detail ) *detail = QStringLiteral( "VMT patch nesting limit exceeded" ); return false; }
		for ( KeyValues *entry = root->GetFirstSubKey(); entry; entry = entry->GetNextKey() )
		{
			const QString key = QString::fromUtf8( entry->GetName() ).toLower();
			if ( key == QLatin1String( "include" ) && entry->GetDataType() != KeyValues::TYPE_NONE ) continue;
			if ( ( key == QLatin1String( "insert" ) || key == QLatin1String( "replace" ) ) && entry->GetDataType() == KeyValues::TYPE_NONE &&
				collect( entry, key == QLatin1String( "insert" ) ? inserts : replacements ) ) continue;
			if ( detail ) *detail = QStringLiteral( "unsupported VMT patch section" );
			return false;
		}
		name = QString::fromUtf8( root->GetString( "include", "" ) );
		if ( !ValidAssetName( name, QStringLiteral( ".vmt" ) ) )
		{ if ( detail ) *detail = QStringLiteral( "invalid VMT patch include" ); return false; }
	}
	if ( !foundBase ) return false;
	for ( auto it = inserts.cbegin(); it != inserts.cend(); ++it ) base.insert( it.key(), it.value() );
	for ( auto it = replacements.cbegin(); it != replacements.cend(); ++it ) if ( base.contains( it.key() ) ) base.insert( it.key(), it.value() );
	QString result = base.value( QStringLiteral( "$basetexture" ) );
	if ( !ValidAssetName( result, QStringLiteral( ".vtf" ) ) )
	{ if ( detail ) *detail = QStringLiteral( "VMT found; no valid $basetexture" ); return false; }
	textureName = result;
	return true;
}

bool CMaterialPreviewRenderer::LoadMaterialImage( const QString &materialName, QImage &image )
{
	image = QImage();
	QString textureName;
	if ( !ResolveBaseTexture( materialName, textureName ) ) return false;
	const QString vtfPath = QStringLiteral( "materials/%1.vtf" ).arg( textureName );
	const QByteArray vtf = ReadMountedFile( m_searchPaths, vtfPath );
	if ( vtf.isEmpty() ) return false;
	CUtlBuffer buffer( vtf.constData(), vtf.size(), CUtlBuffer::READ_ONLY );
	IVTFTexture *decoded = CreateVTFTexture();
	if ( !decoded ) return false;
	const bool parsed = decoded->Unserialize( buffer, false, 0 );
	if ( !parsed || decoded->Width() <= 0 || decoded->Height() <= 0 ||
		static_cast<qint64>( decoded->Width() ) * decoded->Height() > 16777216 )
	{
		DestroyVTFTexture( decoded );
		return false;
	}
	decoded->ConvertImageFormat( IMAGE_FORMAT_RGBA8888, false );
	const unsigned char *pixels = decoded->ImageData( 0, 0, 0 );
	if ( decoded->Format() != IMAGE_FORMAT_RGBA8888 || !pixels ) { DestroyVTFTexture( decoded ); return false; }
	image = QImage( pixels, decoded->Width(), decoded->Height(), decoded->Width() * 4, QImage::Format_RGBA8888 ).copy().mirrored();
	DestroyVTFTexture( decoded );
	if ( image.isNull() ) return false;
	return true;
}

bool CMaterialPreviewRenderer::ProbeMaterial( const QString &materialName, QString *detail )
{
	QImage image;
	QString textureName;
	if ( !ResolveBaseTexture( materialName, textureName, detail ) ) return false;
	const QByteArray vtf = ReadMountedFile( m_searchPaths, QStringLiteral( "materials/%1.vtf" ).arg( textureName ) );
	if ( vtf.isEmpty() )
	{
		if ( detail ) *detail = QStringLiteral( "VMT found; VTF missing: %1" ).arg( textureName );
		return false;
	}
	CUtlBuffer buffer( vtf.constData(), vtf.size(), CUtlBuffer::READ_ONLY );
	IVTFTexture *decoded = CreateVTFTexture();
	if ( !decoded ) return false;
	const bool parsed = decoded->Unserialize( buffer, false, 0 );
	if ( !parsed || decoded->Width() <= 0 || decoded->Height() <= 0 ||
		static_cast<qint64>( decoded->Width() ) * decoded->Height() > 16777216 )
	{
		DestroyVTFTexture( decoded );
		if ( detail ) *detail = QStringLiteral( "VMT found; VTF read but decode failed: %1" ).arg( textureName );
		return false;
	}
	decoded->ConvertImageFormat( IMAGE_FORMAT_RGBA8888, false );
	const unsigned char *pixels = decoded->ImageData( 0, 0, 0 );
	if ( decoded->Format() != IMAGE_FORMAT_RGBA8888 || !pixels )
	{
		DestroyVTFTexture( decoded );
		if ( detail ) *detail = QStringLiteral( "VTF conversion failed: %1" ).arg( textureName );
		return false;
	}
	image = QImage( pixels, decoded->Width(), decoded->Height(), decoded->Width() * 4, QImage::Format_RGBA8888 ).copy().mirrored();
	DestroyVTFTexture( decoded );
	if ( image.isNull() )
	{
		if ( detail ) *detail = QStringLiteral( "VMT found; VTF decoded but image is empty: %1" ).arg( textureName );
		return false;
	}
	const QByteArray digest = QCryptographicHash::hash(
		QByteArray( reinterpret_cast<const char *>( image.constBits() ), static_cast<qsizetype>( image.sizeInBytes() ) ), QCryptographicHash::Sha256 ).toHex();
	if ( detail ) *detail = QStringLiteral( "VMT hit; VTF decoded; pixels=%1x%2; sha256=%3" )
		.arg( image.width() ).arg( image.height() ).arg( QString::fromLatin1( digest ) );
	return true;
}

GLuint CMaterialPreviewRenderer::TextureForMaterial( const QString &materialName )
{
	const QString key = materialName.toLower();
	auto found = m_textures.find( key );
	if ( found != m_textures.end() ) return found->id;
	TextureEntry entry;
	QImage image;
	if ( LoadMaterialImage( materialName, image ) )
	{
		image = image.convertToFormat( QImage::Format_RGBA8888 );
		entry.width = image.width();
		entry.height = image.height();
		glGenTextures( 1, &entry.id );
		glBindTexture( GL_TEXTURE_2D, entry.id );
		glTexParameteri( GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR_MIPMAP_LINEAR );
		glTexParameteri( GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR );
		glTexParameteri( GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_REPEAT );
		glTexParameteri( GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_REPEAT );
		glPixelStorei( GL_UNPACK_ALIGNMENT, 1 );
		glTexImage2D( GL_TEXTURE_2D, 0, GL_RGBA, image.width(), image.height(), 0, GL_RGBA, GL_UNSIGNED_BYTE, image.constBits() );
		glGenerateMipmap( GL_TEXTURE_2D );
	}
	else entry.missing = true;
	m_textures.insert( key, entry );
	return entry.id;
}

void CMaterialPreviewRenderer::DrawSolid( const SolidLite &solid, const QMatrix4x4 &mvp, bool textured, bool selected, const QSet<int> &selectedFaces )
{
	QList<HammerWinding::Polygon> faces;
	if ( !HammerWinding::BuildFaces( solid, faces ) ) return;
	m_program.setUniformValue( "uMvp", mvp );
	for ( int i = 0; i < faces.size() && i < solid.sides.size(); ++i )
	{
		const SideLite &side = solid.sides[i];
		const QVector4D color = selected ? QVector4D( 1.0f, 0.72f, 0.24f, 1.0f ) : QVector4D( 0.76f, 0.78f, 0.82f, 1.0f );
		m_program.setUniformValue( "uColor", color );
		m_program.setUniformValue( "uFaceSelected", selectedFaces.contains( side.id ) ? 1 : 0 );
		const bool validUv = HammerPreviewValidUvScale( side );
		const GLuint texture = textured && validUv ? TextureForMaterial( side.texture ) : 0;
		m_program.setUniformValue( "uTextured", texture != 0 ? 1 : 0 );
		const TextureEntry textureEntry = m_textures.value( side.texture.toLower() );
		m_program.setUniformValue( "uTextureSize", QVector2D( textureEntry.width, textureEntry.height ) );
		if ( texture ) { glActiveTexture( GL_TEXTURE0 ); glBindTexture( GL_TEXTURE_2D, texture ); }
		m_program.setUniformValue( "uTexture", 0 );
		QVector<Vertex> vertices;
		vertices.reserve( ( faces[i].size() - 2 ) * 3 );
		for ( int tri = 1; tri + 1 < faces[i].size(); ++tri )
		{
			const int indices[3] = { 0, tri, tri + 1 };
			for ( int index : indices )
			{
				const auto &p = faces[i][index];
				const float u = validUv ? static_cast<float>( ( p[0] * side.uaxis[0] + p[1] * side.uaxis[1] + p[2] * side.uaxis[2] ) / side.uscale + side.uaxis[3] ) : 0.0f;
				const float v = validUv ? static_cast<float>( ( p[0] * side.vaxis[0] + p[1] * side.vaxis[1] + p[2] * side.vaxis[2] ) / side.vscale + side.vaxis[3] ) : 0.0f;
				vertices.append( { QVector3D( p[0], p[1], p[2] ), QVector2D( u, v ) } );
			}
		}
		glVertexAttribPointer( 0, 3, GL_FLOAT, GL_FALSE, sizeof( Vertex ), &vertices[0].position );
		glVertexAttribPointer( 1, 2, GL_FLOAT, GL_FALSE, sizeof( Vertex ), &vertices[0].uv );
		glDrawArrays( GL_TRIANGLES, 0, vertices.size() );
	}
}

void CMaterialPreviewRenderer::Render( const MapDocLite *map, const QSet<int> &selectedIds,
	const double eye[3], const double forward[3], const double up[3], int width, int height, bool textured, const QMap<int, QSet<int>> &selectedFaces )
{
	if ( !Initialize() || !map || width <= 0 || height <= 0 ) return;
	if ( m_textureCacheDirty )
	{
		for ( const TextureEntry &entry : m_textures ) if ( entry.id ) glDeleteTextures( 1, &entry.id );
		m_textures.clear();
		m_textureCacheDirty = false;
	}
	glViewport( 0, 0, width, height );
	glEnable( GL_DEPTH_TEST );
	glDepthFunc( GL_LEQUAL );
	glDisable( GL_CULL_FACE );
	glEnable( GL_BLEND );
	glBlendFunc( GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA );
	glClearColor( 0.12f, 0.13f, 0.16f, 1.0f );
	glClear( GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT );
	QMatrix4x4 view;
	const QVector3D cameraEye( eye[0], eye[1], eye[2] );
	const QVector3D look( forward[0], forward[1], forward[2] );
	view.lookAt( cameraEye, cameraEye + look * 1024.0f, QVector3D( up[0], up[1], up[2] ) );
	QMatrix4x4 projection;
	projection.perspective( 90.0f, static_cast<float>( width ) / height, 1.0f, 32768.0f );
	if ( !m_program.bind() ) return;
	glEnableVertexAttribArray( 0 );
	glEnableVertexAttribArray( 1 );
	const QMatrix4x4 mvp = projection * view;
	for ( const SolidLite &solid : map->solids ) DrawSolid( solid, mvp, textured, selectedIds.contains( solid.id ), selectedFaces.value( solid.id ) );
	glDisableVertexAttribArray( 0 );
	glDisableVertexAttribArray( 1 );
	m_program.release();
	glDisable( GL_DEPTH_TEST );
	glDisable( GL_BLEND );
}
