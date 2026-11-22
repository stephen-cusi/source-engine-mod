//========= Hammer Qt6 (hammer2/app) ==========================================
//
// Purpose: VMF save/load implementation for MapDocLite. The loader is a small
//          recursive brace/token parser that stays compatible with what the
//          writer emits (and with the format vbsp reads: three plane points,
//          four-component texture axes with a separate scale, standalone
//          material/rotation/lightmapscale/smoothing_groups lines).
//
//=============================================================================

#include "vmfio.h"

#include "mapmodel.h"
#include "displacementinspect.h"

#include <QFile>
#include <QCoreApplication>
#include <QList>
#include <QPair>
#include <QRegularExpression>
#include <QSaveFile>
#include <QSet>
#include <QStringList>
#include <QTextStream>
#include <cmath>
#include <functional>

namespace
{
	//-----------------------------------------------------------------------------
	// Purpose: One generic VMF chunk: a name, key/value pairs and nested chunks.
	//-----------------------------------------------------------------------------
	struct VmfChunk
	{
		QString name;
		QList<QPair<QString, QString> > keys;
		QList<VmfChunk> children;
		bool hiddenWrapper = false;
	};

	HammerDisplacementInspect::Chunk DisplacementChunk( const VmfChunk &source )
	{
		HammerDisplacementInspect::Chunk result;
		result.name = source.name;
		result.keys = source.keys;
		for ( const VmfChunk &child : source.children ) result.children.append( DisplacementChunk( child ) );
		return result;
	}

	// Expand only the exact hidden wrapper forms understood by original Hammer.
	// Preserve the wrapper bit, since VBSP treats hidden chunks differently.
	bool ExpandHiddenChildren( VmfChunk &parent, const char *childName, QString *errorOut )
	{
		QList<VmfChunk> children;
		for ( const VmfChunk &child : parent.children )
		{
			if ( child.name.compare( QLatin1String( "hidden" ), Qt::CaseInsensitive ) != 0 ) { children.append( child ); continue; }
			if ( !child.keys.isEmpty() || child.children.isEmpty() )
			{
				if ( errorOut ) *errorOut = QCoreApplication::translate( "VmfIO", "Unsupported VMF data: invalid hidden block." );
				return false;
			}
			for ( VmfChunk nested : child.children )
			{
				if ( nested.name.compare( QLatin1String( childName ), Qt::CaseInsensitive ) != 0 )
				{
					if ( errorOut ) *errorOut = QCoreApplication::translate( "VmfIO", "Unsupported VMF data: invalid hidden block." );
					return false;
				}
				nested.hiddenWrapper = true; children.append( nested );
			}
		}
		parent.children = children;
		return true;
	}

	//-----------------------------------------------------------------------------
	// Purpose: Minimal whitespace/comment aware tokenizer over the whole file.
	//-----------------------------------------------------------------------------
	class VmfTokenizer
	{
	public:
		VmfTokenizer( const QString &text )
			: m_strText( text )
			, m_nPos( 0 )
		{
		}

		// Reads the next token; returns false at end of file
		bool Next( QString &token, bool *quotedOut = nullptr )
		{
			if ( quotedOut )
				*quotedOut = false;
			SkipWhitespaceAndComments();
			if ( m_nPos >= m_strText.length() )
				return false;

			const QChar ch = m_strText.at( m_nPos );
			if ( ch == QLatin1Char( '"' ) )
			{
				if ( quotedOut )
					*quotedOut = true;
				++m_nPos;
				token.clear();
				while ( m_nPos < m_strText.length() )
				{
					const QChar c = m_strText.at( m_nPos );
					if ( c == QLatin1Char( '"' ) )
					{
						++m_nPos;
						break;
					}
					if ( c == QLatin1Char( '\\' ) && m_nPos + 1 < m_strText.length() &&
						( m_strText.at( m_nPos + 1 ) == QLatin1Char( '\\' ) ||
						  m_strText.at( m_nPos + 1 ) == QLatin1Char( '"' ) ) )
					{
						token += m_strText.at( m_nPos + 1 );
						m_nPos += 2;
						continue;
					}
					token += c;
					++m_nPos;
				}
				return true;
			}

			// Braces are single-character tokens
			if ( ch == QLatin1Char( '{' ) || ch == QLatin1Char( '}' ) )
			{
				token = ch;
				++m_nPos;
				return true;
			}

			// Bare word
			token.clear();
			while ( m_nPos < m_strText.length() )
			{
				const QChar c = m_strText.at( m_nPos );
				if ( c.isSpace() || c == QLatin1Char( '{' ) || c == QLatin1Char( '}' ) || c == QLatin1Char( '"' ) )
					break;
				token += c;
				++m_nPos;
			}
			return true;
		}

		// Peeks the next token without consuming it
		bool Peek( QString &token, bool *quotedOut = nullptr )
		{
			const int savedPos = m_nPos;
			const bool bOk = Next( token, quotedOut );
			m_nPos = savedPos;
			return bOk;
		}

	private:
		void SkipWhitespaceAndComments()
		{
			while ( m_nPos < m_strText.length() )
			{
				const QChar ch = m_strText.at( m_nPos );
				if ( ch.isSpace() )
				{
					++m_nPos;
					continue;
				}
				if ( ch == QLatin1Char( '/' ) && m_nPos + 1 < m_strText.length() &&
					m_strText.at( m_nPos + 1 ) == QLatin1Char( '/' ) )
				{
					while ( m_nPos < m_strText.length() && m_strText.at( m_nPos ) != QLatin1Char( '\n' ) )
						++m_nPos;
					continue;
				}
				break;
			}
		}

		QString m_strText;
		int m_nPos;
	};

	//-----------------------------------------------------------------------------
	// Purpose: Parses a chunk body: '{', then key/value pairs and nested
	//          chunks (whose name has already been consumed by the caller),
	//          until the matching '}'. The chunk name must be pre-filled by
	//          the caller in chunk.name.
	//          Returns false on malformed input.
	//-----------------------------------------------------------------------------
	bool ParseChunkBody( VmfTokenizer &t, VmfChunk &chunk, int depth = 0 )
	{
		// Real VMFs nest at most ~4 levels; bound recursion against hostile input.
		if ( depth > 64 )
			return false;

		QString token;
		bool bQuoted = false;
		if ( !t.Next( token, &bQuoted ) || bQuoted || token != QLatin1String( "{" ) )
			return false;

		while ( true )
		{
			token.clear();
			if ( !t.Next( token, &bQuoted ) )
				return false; // unexpected EOF inside the chunk

			// Only unquoted braces are structure; a quoted "{" or "}" is plain text
			if ( !bQuoted && token == QLatin1String( "}" ) )
				return true;
			if ( !bQuoted && token == QLatin1String( "{" ) )
				return false; // a block with no name

			const QString key = token;

			QString next;
			bool bNextQuoted = false;
			if ( !t.Peek( next, &bNextQuoted ) )
				return false;

			if ( !bNextQuoted && next == QLatin1String( "{" ) )
			{
				// Nested chunk: the key just read is its name
				VmfChunk child;
				child.name = key;
				if ( !ParseChunkBody( t, child, depth + 1 ) )
					return false;
				chunk.children.append( child );
			}
			else
			{
				if ( !bNextQuoted && next == QLatin1String( "}" ) )
					return false; // key without a value
				QString value;
				if ( !t.Next( value ) )
					return false;
				chunk.keys.append( qMakePair( key, value ) );
			}
		}
	}

	//-----------------------------------------------------------------------------
	// Purpose: Finds a key inside a chunk (case-insensitive, like vbsp).
	//-----------------------------------------------------------------------------
	QString GetChunkValue( const VmfChunk &chunk, const char *key )
	{
		for ( const QPair<QString, QString> &kv : chunk.keys )
		{
			if ( kv.first.compare( QLatin1String( key ), Qt::CaseInsensitive ) == 0 )
				return kv.second;
		}
		return QString();
	}

	QString QuoteKeyValue( const QString &value )
	{
		// The reader only treats \\ and \" as escapes, so a lone backslash (as in
		// "models\props\x.mdl") is written as-is to match what Hammer/vbsp expect;
		// a backslash is doubled only where it would otherwise be misread.
		QString escaped;
		escaped.reserve( value.length() + 2 );
		for ( int i = 0; i < value.length(); ++i )
		{
			const QChar c = value.at( i );
			if ( c == QLatin1Char( '"' ) )
				escaped += QStringLiteral( "\\\"" );
			else if ( c == QLatin1Char( '\\' ) &&
				( i + 1 >= value.length() || value.at( i + 1 ) == QLatin1Char( '\\' ) || value.at( i + 1 ) == QLatin1Char( '"' ) ) )
				escaped += QStringLiteral( "\\\\" );
			else
				escaped += c;
		}
		return QStringLiteral( "\"%1\"" ).arg( escaped );
	}

	// Reject data that the lite model cannot write back rather than silently
	// dropping it on Save. This guard can be relaxed as support is added.
	bool CheckSupportedChunk( const VmfChunk &chunk, QString *errorOut )
	{
		const auto is = []( const QString &a, const char *b ) {
			return a.compare( QLatin1String( b ), Qt::CaseInsensitive ) == 0;
		};
		const auto fail = [&]( const QString &what ) {
			if ( errorOut )
				*errorOut = QCoreApplication::translate( "VmfIO", "Unsupported VMF data: %1. The file was not opened to prevent data loss." ).arg( what );
			return false;
		};
		const auto checkEditor = [&]( const VmfChunk &editor ) {
			for ( const auto &key : editor.keys )
			{
				if ( is( key.first, "color" ) ||
					 ( ( is( key.first, "visgroupshown" ) || is( key.first, "visgroupautoshown" ) ) && ( key.second == QLatin1String( "0" ) || key.second == QLatin1String( "1" ) ) ) ||
					 is( key.first, "logicalpos" ) )
					continue;
				if ( is( key.first, "visgroupid" ) || is( key.first, "colorvisgroupid" ) )
				{
					bool ok = false;
					const int id = key.second.toInt( &ok );
					if ( ok && id > 0 ) continue;
				}
					return fail( QStringLiteral( "editor/%1" ).arg( key.first ) );
			}
			return editor.children.isEmpty() || fail( QCoreApplication::translate( "VmfIO", "editor child block" ) );
		};
		if ( is( chunk.name, "versioninfo" ) )
			return chunk.children.isEmpty() || fail( QStringLiteral( "versioninfo" ) );
		if ( is( chunk.name, "viewsettings" ) || is( chunk.name, "palette_plus" ) ||
			 is( chunk.name, "colorcorrection_plus" ) || is( chunk.name, "light_plus" ) ||
			 is( chunk.name, "postprocess_plus" ) || is( chunk.name, "bgimages_plus" ) ||
			 is( chunk.name, "cordons" ) )
			return chunk.children.isEmpty() || fail( chunk.name );
		if ( is( chunk.name, "visgroups" ) )
		{
			if ( !chunk.keys.isEmpty() ) return fail( QStringLiteral( "visgroups" ) );
			QSet<int> groupIds;
			std::function<bool( const VmfChunk & )> validateGroup = [&]( const VmfChunk &group ) {
				if ( !is( group.name, "visgroup" ) ) return fail( QStringLiteral( "visgroups/%1" ).arg( group.name ) );
				int id = 0; bool hasId = false;
				for ( const auto &key : group.keys )
				{
					if ( is( key.first, "visgroupid" ) )
					{
						bool ok = false; const int parsed = key.second.toInt( &ok );
						if ( !ok || parsed <= 0 ) return fail( QStringLiteral( "visgroups/visgroupid" ) );
						id = parsed; hasId = true;
					}
					else if ( !is( key.first, "name" ) && !is( key.first, "color" ) )
						return fail( QStringLiteral( "visgroups/%1" ).arg( key.first ) );
				}
				if ( !hasId || groupIds.contains( id ) ) return fail( QStringLiteral( "visgroups/duplicate or missing id" ) );
				groupIds.insert( id );
				for ( const VmfChunk &child : group.children ) if ( !validateGroup( child ) ) return false;
				return true;
			};
			for ( const VmfChunk &group : chunk.children ) if ( !validateGroup( group ) ) return false;
			return true;
		}
		if ( is( chunk.name, "cameras" ) )
		{
			for ( const VmfChunk &camera : chunk.children )
				if ( !is( camera.name, "camera" ) || !camera.children.isEmpty() )
					return fail( QStringLiteral( "cameras/%1" ).arg( camera.name ) );
			return true;
		}
		if ( is( chunk.name, "cordon" ) )
			return chunk.children.isEmpty() || fail( QStringLiteral( "cordon" ) );
		if ( is( chunk.name, "world" ) )
		{
			QStringList seenKeys;
			for ( const auto &key : chunk.keys )
			{
				if ( seenKeys.contains( key.first, Qt::CaseInsensitive ) )
					return fail( QCoreApplication::translate( "VmfIO", "duplicate world key %1" ).arg( key.first ) );
				if ( is( key.first, "id" ) && key.second != QLatin1String( "1" ) )
					return fail( QCoreApplication::translate( "VmfIO", "world id other than 1" ) );
				seenKeys.append( key.first );
			}
			for ( const VmfChunk &solid : chunk.children )
			{
				if ( !is( solid.name, "solid" ) )
					return fail( QStringLiteral( "world/%1" ).arg( solid.name ) );
				bool seenEditor = false;
				for ( const VmfChunk &child : solid.children )
				{
					if ( is( child.name, "editor" ) )
					{
						if ( seenEditor ) return fail( QCoreApplication::translate( "VmfIO", "duplicate solid editor" ) );
						seenEditor = true;
						if ( !checkEditor( child ) ) return false;
					}
					else if ( is( child.name, "side" ) )
					{
						bool seenVertices = false;
						bool seenDisplacement = false;
						for ( const VmfChunk &vertices : child.children )
						{
							if ( is( vertices.name, "dispinfo" ) )
							{
								QString reason;
								if ( seenDisplacement || !HammerDisplacementInspect::InspectCanonical( DisplacementChunk( vertices ), &reason ) )
									return fail( QStringLiteral( "world/solid/side/dispinfo: %1" ).arg(
										seenDisplacement ? QStringLiteral( "duplicate block" ) : reason ) );
								seenDisplacement = true;
								continue;
							}
							if ( !is( vertices.name, "vertices_plus" ) || seenVertices || !vertices.children.isEmpty() )
								return fail( QStringLiteral( "world/solid/side/%1" ).arg( vertices.name ) );
							seenVertices = true;
							for ( const auto &vertex : vertices.keys )
							{
								double xyz[3];
								if ( !is( vertex.first, "v" ) || !ParseVec3( vertex.second, xyz ) ||
									 !std::isfinite( xyz[0] ) || !std::isfinite( xyz[1] ) || !std::isfinite( xyz[2] ) )
									return fail( QStringLiteral( "world/solid/side/vertices_plus" ) );
							}
						}
						for ( const auto &key : child.keys )
							if ( !is( key.first, "id" ) && !is( key.first, "plane" ) && !is( key.first, "material" ) &&
								 !is( key.first, "uaxis" ) && !is( key.first, "vaxis" ) && !is( key.first, "rotation" ) &&
								 !is( key.first, "lightmapscale" ) && !is( key.first, "smoothing_groups" ) )
								return fail( QStringLiteral( "world/solid/side/%1" ).arg( key.first ) );
					}
					else return fail( QStringLiteral( "world/solid/%1" ).arg( child.name ) );
				}
				for ( const auto &key : solid.keys )
					if ( !is( key.first, "id" ) ) return fail( QStringLiteral( "world/solid/%1" ).arg( key.first ) );
			}
			return true;
		}
		if ( is( chunk.name, "entity" ) )
		{
			QStringList seenKeys;
			for ( const auto &key : chunk.keys )
			{
				if ( seenKeys.contains( key.first, Qt::CaseInsensitive ) )
					return fail( QCoreApplication::translate( "VmfIO", "duplicate entity key %1" ).arg( key.first ) );
				seenKeys.append( key.first );
			}
			bool seenEditor = false;
			for ( const VmfChunk &child : chunk.children )
			{
				if ( is( child.name, "solid" ) )
				{
					// Reuse exactly the same loss-prevention checks as world brushes.
					VmfChunk world;
					world.name = QStringLiteral( "world" );
					world.children.append( child );
					if ( !CheckSupportedChunk( world, errorOut ) ) return false;
				}
				else if ( is( child.name, "editor" ) )
				{
					if ( seenEditor ) return fail( QCoreApplication::translate( "VmfIO", "duplicate entity editor" ) );
					seenEditor = true;
					if ( !checkEditor( child ) ) return false;
				}
				else if ( is( child.name, "connections" ) )
				{
					if ( !child.children.isEmpty() ) return fail( QStringLiteral( "entity/connections/%1" ).arg( child.children.first().name ) );
				}
				else return fail( QStringLiteral( "entity/%1" ).arg( child.name ) );
			}
			return true;
		}
		return fail( chunk.name );
	}

	//-----------------------------------------------------------------------------
	// Purpose: Splits text on any run of whitespace, dropping empty parts.
	//-----------------------------------------------------------------------------
	QStringList SplitWhitespace( const QString &text )
	{
		static const QRegularExpression re( QStringLiteral( "\\s+" ) );
		return text.trimmed().split( re, Qt::SkipEmptyParts );
	}

	//-----------------------------------------------------------------------------
	// Purpose: Parses the "[x y z offset] scale" texture axis form used by VMF.
	//-----------------------------------------------------------------------------
	bool ParseTexAxis( const QString &text, float axis[4], float &scale )
	{
		QString numbers = text;
		numbers.remove( QLatin1Char( '[' ) );
		numbers.remove( QLatin1Char( ']' ) );

		const QStringList parts = SplitWhitespace( numbers );
		if ( parts.count() != 5 )
			return false;

		bool bOk[5] = { false, false, false, false, false };
		axis[0] = parts[0].toFloat( &bOk[0] );
		axis[1] = parts[1].toFloat( &bOk[1] );
		axis[2] = parts[2].toFloat( &bOk[2] );
		axis[3] = parts[3].toFloat( &bOk[3] );
		scale = parts[4].toFloat( &bOk[4] );
		return bOk[0] && bOk[1] && bOk[2] && bOk[3] && bOk[4];
	}

	//-----------------------------------------------------------------------------
	// Purpose: Converts a VMF side chunk into a SideLite.
	//-----------------------------------------------------------------------------
	bool LoadSide( const VmfChunk &chunk, SideLite &side )
	{
		for ( const VmfChunk &child : chunk.children )
		{
			if ( child.name.compare( QLatin1String( "vertices_plus" ), Qt::CaseInsensitive ) == 0 )
			{
				side.hasVerticesPlus = true;
				for ( const auto &key : child.keys ) side.verticesPlus.append( key.second );
			}
			else if ( child.name.compare( QLatin1String( "dispinfo" ), Qt::CaseInsensitive ) == 0 )
				side.displacement = DisplacementChunk( child );
		}
		const QString plane = GetChunkValue( chunk, "plane" );
		if ( plane.isEmpty() )
			return false;

		// plane: "( x1 y1 z1 ) ( x2 y2 z2 ) ( x3 y3 z3 )" - strip parens too
		QString planeNumbers = plane;
		planeNumbers.remove( QLatin1Char( '(' ) );
		planeNumbers.remove( QLatin1Char( ')' ) );

		const QStringList parts = SplitWhitespace( planeNumbers );
		if ( parts.count() != 9 )
			return false;

		for ( int i = 0; i < 9; ++i )
		{
			bool bOk = false;
			const double v = parts[i].toDouble( &bOk );
			if ( !bOk || !std::isfinite( v ) )
				return false;
			side.planePts[i / 3][i % 3] = v;
		}

		const QString material = GetChunkValue( chunk, "material" );
		if ( !material.isEmpty() )
			side.texture = material;

		const QString uaxis = GetChunkValue( chunk, "uaxis" );
		const QString vaxis = GetChunkValue( chunk, "vaxis" );
		float axis[4];
		float scale = 1.0f;
		if ( !uaxis.isEmpty() && ParseTexAxis( uaxis, axis, scale ) )
		{
			for ( int a = 0; a < 4; ++a )
				side.uaxis[a] = axis[a];
			side.uscale = scale;
		}
		if ( !vaxis.isEmpty() && ParseTexAxis( vaxis, axis, scale ) )
		{
			for ( int a = 0; a < 4; ++a )
				side.vaxis[a] = axis[a];
			side.vscale = scale;
		}

		bool bOk = false;
		const QString rotation = GetChunkValue( chunk, "rotation" );
		if ( !rotation.isEmpty() )
			side.rotation = rotation.toFloat( &bOk );

		const QString lightmap = GetChunkValue( chunk, "lightmapscale" );
		if ( !lightmap.isEmpty() )
			side.lightmapscale = lightmap.toInt( &bOk );

		const QString smoothing = GetChunkValue( chunk, "smoothing_groups" );
		if ( !smoothing.isEmpty() )
			side.smoothing = smoothing.toInt( &bOk );

		const QString id = GetChunkValue( chunk, "id" );
		if ( !id.isEmpty() )
			side.id = id.toInt( &bOk );

		return true;
	}

	//-----------------------------------------------------------------------------
	// Purpose: Converts a VMF solid chunk into a SolidLite.
	//-----------------------------------------------------------------------------
	bool LoadSolid( const VmfChunk &chunk, SolidLite &solid, QString *errorOut )
	{
		const QString id = GetChunkValue( chunk, "id" );
		if ( !id.isEmpty() )
			solid.id = id.toInt();

		for ( const VmfChunk &child : chunk.children )
		{
			if ( child.name.compare( QLatin1String( "editor" ), Qt::CaseInsensitive ) == 0 )
			{
				solid.hasEditor = true;
				solid.editorProperties = child.keys;
			}
			if ( child.name.compare( QLatin1String( "side" ), Qt::CaseInsensitive ) == 0 )
			{
				SideLite side;
				if ( !LoadSide( child, side ) )
				{
					if ( errorOut )
						*errorOut = QCoreApplication::translate( "VmfIO", "malformed side chunk in solid %1" ).arg( solid.id );
					return false;
				}
				solid.sides.append( side );
			}
		}
		if ( solid.sides.isEmpty() )
		{
			if ( errorOut ) *errorOut = QCoreApplication::translate( "VmfIO", "solid %1 has no sides" ).arg( solid.id );
			return false;
		}
		return true;
	}
} // anonymous namespace

namespace VmfIO
{
	bool InspectDisplacements( const QString &path, DisplacementReport &report, QString *errorOut )
	{
		report = DisplacementReport();
		if ( errorOut ) errorOut->clear();
		QFile file( path );
		if ( !file.open( QIODevice::ReadOnly ) )
		{
			if ( errorOut ) *errorOut = QCoreApplication::translate( "VmfIO", "cannot open %1 for reading" ).arg( path );
			return false;
		}
		// Inspection is deliberately bounded and independent from the live document.
		constexpr qint64 maxBytes = 16 * 1024 * 1024;
		if ( file.size() > maxBytes )
		{
			if ( errorOut ) *errorOut = QCoreApplication::translate( "VmfIO", "File exceeds the 16 MiB inspection limit." );
			return false;
		}
		const QByteArray bytes = file.read( maxBytes + 1 );
		if ( file.error() != QFileDevice::NoError || bytes.size() > maxBytes )
		{
			if ( errorOut ) *errorOut = QCoreApplication::translate( "VmfIO", "Could not read the complete inspection file." );
			return false;
		}
		const QString text = QString::fromUtf8( bytes );
		// The existing tokenizer tolerates unterminated quoted tokens; diagnostics
		// must distinguish syntax errors, including an unclosed quote or comment.
		bool quoted = false, comment = false;
		for ( qsizetype i = 0; i < text.size(); ++i )
		{
			const QChar c = text[i];
			if ( comment ) { if ( c == QLatin1Char( '\n' ) || c == QLatin1Char( '\r' ) ) comment = false; continue; }
			if ( quoted && c == QLatin1Char( '\\' ) && i + 1 < text.size() &&
				 ( text[i+1] == QLatin1Char( '\\' ) || text[i+1] == QLatin1Char( '"' ) ) ) { ++i; continue; }
			if ( c == QLatin1Char( '"' ) ) quoted = !quoted;
			else if ( !quoted && c == QLatin1Char( '/' ) && i + 1 < text.size() && text[i+1] == QLatin1Char( '/' ) ) { comment = true; ++i; }
		}
		if ( quoted )
		{
			if ( errorOut ) *errorOut = QCoreApplication::translate( "VmfIO", "VMF syntax error: unterminated quoted value." );
			return false;
		}
		VmfTokenizer tokenizer( text );
		QList<VmfChunk> roots;
		QString name;
		bool quotedName = false;
		while ( tokenizer.Next( name, &quotedName ) )
		{
			if ( !quotedName && ( name == QLatin1String( "{" ) || name == QLatin1String( "}" ) ) )
			{
				if ( errorOut ) *errorOut = QCoreApplication::translate( "VmfIO", "VMF syntax error near top-level brace." );
				return false;
			}
			VmfChunk chunk; chunk.name = name;
			if ( !ParseChunkBody( tokenizer, chunk ) )
			{
				if ( errorOut ) *errorOut = QCoreApplication::translate( "VmfIO", "parse error near chunk '%1'" ).arg( name );
				return false;
			}
			roots.append( chunk );
		}
		if ( roots.isEmpty() )
		{
			if ( errorOut ) *errorOut = QCoreApplication::translate( "VmfIO", "VMF syntax error: empty file." );
			return false;
		}
		std::function<void( const VmfChunk &, const QString &, const QString &, bool, bool )> visit =
			[&]( const VmfChunk &chunk, const QString &solidId, const QString &sideId, bool parentIsSide, bool duplicate ) {
			const bool isSolid = chunk.name.compare( QLatin1String( "solid" ), Qt::CaseInsensitive ) == 0;
			const bool isSide = chunk.name.compare( QLatin1String( "side" ), Qt::CaseInsensitive ) == 0;
			const QString currentSolid = isSolid ? GetChunkValue( chunk, "id" ) : solidId;
			const QString currentSide = isSide ? GetChunkValue( chunk, "id" ) : sideId;
			if ( chunk.name.compare( QLatin1String( "dispinfo" ), Qt::CaseInsensitive ) == 0 )
			{
				++report.count;
				QString why;
				const bool ok = !duplicate && parentIsSide && !currentSolid.isEmpty() && !currentSide.isEmpty() &&
					HammerDisplacementInspect::InspectCanonical( DisplacementChunk( chunk ), &why );
				if ( !ok ) ++report.nonCanonical;
				if ( report.details.size() < 100 )
					report.details.append( QCoreApplication::translate( "VmfIO", "Solid %1, face %2: %3" )
						.arg( currentSolid.isEmpty() ? QStringLiteral( "?" ) : currentSolid,
							currentSide.isEmpty() ? QStringLiteral( "?" ) : currentSide,
							ok ? QCoreApplication::translate( "VmfIO", "canonical" ) :
							why.isEmpty() ? QCoreApplication::translate( "VmfIO", "duplicate block, invalid placement or missing ID" ) : why ) );
				return;
			}
			int displacementChildren = 0;
			if ( isSide ) for ( const VmfChunk &child : chunk.children )
				if ( child.name.compare( QLatin1String( "dispinfo" ), Qt::CaseInsensitive ) == 0 ) ++displacementChildren;
			for ( const VmfChunk &child : chunk.children ) visit( child, currentSolid, currentSide, isSide, displacementChildren > 1 );
		};
		for ( const VmfChunk &root : roots ) visit( root, QString(), QString(), false, false );
		return true;
	}
	// Source TokenReader terminates at a quote and handles backslash sequences
	// differently from this writer. Refuse unsafe text before opening the file.
	static bool ValidateForSave( const MapDocLite &map, QString *errorOut )
	{
		auto fail = [&]( const char *message ) {
			if ( errorOut ) *errorOut = QCoreApplication::translate( "VmfIO", message );
			return false;
		};
		auto textOk = []( const QString &text ) {
			return !text.contains( QLatin1Char( '"' ) ) && !text.contains( QLatin1Char( '\\' ) ) &&
				!text.contains( QLatin1Char( '\n' ) ) && !text.contains( QLatin1Char( '\r' ) ) && !text.contains( QChar( 0 ) );
		};
		auto failText = [&]( const QString &location ) {
			if ( errorOut ) *errorOut = QCoreApplication::translate( "VmfIO", "Cannot save: %1 contains a double quote, backslash, line break or NUL, which Source VMF cannot safely represent." ).arg( location );
			return false;
		};
		auto pairsOk = [&]( const QList<QPair<QString, QString> > &pairs ) {
			for ( const auto &pair : pairs ) if ( !textOk( pair.first ) || !textOk( pair.second ) ) return false;
			return true;
		};
		for ( auto it = map.worldspawn.cbegin(); it != map.worldspawn.cend(); ++it )
			if ( !textOk( it.key() ) || !textOk( it.value() ) )
				return failText( QCoreApplication::translate( "VmfIO", "worldspawn key '%1'" ).arg( it.key() ) );
		if ( !pairsOk( map.versionProperties ) || !pairsOk( map.cameraProperties ) || !pairsOk( map.cordonProperties ) )
			return failText( QCoreApplication::translate( "VmfIO", "map metadata" ) );
		for ( const CameraLite &camera : map.cameras ) if ( !pairsOk( camera.keyvalues ) )
			return failText( QCoreApplication::translate( "VmfIO", "camera metadata" ) );
		std::function<bool( const VisGroupLite & )> visgroupTextOk = [&]( const VisGroupLite &group ) {
			if ( !pairsOk( group.properties ) ) return false;
			for ( const VisGroupLite &child : group.children ) if ( !visgroupTextOk( child ) ) return false;
			return true;
		};
		for ( const VisGroupLite &group : map.visgroups ) if ( !visgroupTextOk( group ) )
			return failText( QCoreApplication::translate( "VmfIO", "visgroup metadata" ) );
		for ( const VmfMetadataBlock &block : map.extraMetadata ) if ( !textOk( block.name ) || !pairsOk( block.properties ) )
			return failText( QCoreApplication::translate( "VmfIO", "map metadata block '%1'" ).arg( block.name ) );
		bool hasBrushEntities = false;
		for ( const SolidLite &solid : map.solids ) hasBrushEntities = hasBrushEntities || solid.ownerEntityId != 0;
		if ( hasBrushEntities )
		{
			QSet<int> ids;
			for ( const EntityLite &entity : map.entities )
			{
				if ( entity.id <= 1 || ids.contains( entity.id ) ) return fail( "Cannot save: invalid or duplicate object id." );
				ids.insert( entity.id );
			}
			for ( const SolidLite &solid : map.solids )
			{
				if ( solid.id <= 1 || ids.contains( solid.id ) ) return fail( "Cannot save: invalid or duplicate object id." );
				ids.insert( solid.id );
			}
		}
		for ( const EntityLite &entity : map.entities )
		{
			if ( !textOk( entity.classname ) || !pairsOk( entity.connections ) || !pairsOk( entity.editorProperties ) )
				return failText( QCoreApplication::translate( "VmfIO", "entity %1 class, output or editor metadata" ).arg( entity.id ) );
			for ( auto it = entity.keyvalues.cbegin(); it != entity.keyvalues.cend(); ++it )
				if ( !textOk( it.key() ) || !textOk( it.value() ) )
					return failText( QCoreApplication::translate( "VmfIO", "entity %1 key '%2'" ).arg( entity.id ).arg( it.key() ) );
			for ( double value : entity.origin ) if ( !std::isfinite( value ) )
				return fail( "Cannot save: map geometry or texture coordinates contain a non-finite number." );
		}
		for ( const SolidLite &solid : map.solids )
		{
			if ( solid.ownerEntityId != 0 )
			{
				int owners = 0;
				for ( const EntityLite &entity : map.entities ) if ( entity.id == solid.ownerEntityId ) ++owners;
				if ( solid.ownerEntityId <= 1 || owners != 1 )
					return fail( "Cannot save: brush entity ownership is invalid or ambiguous." );
			}
			if ( !pairsOk( solid.editorProperties ) )
				return failText( QCoreApplication::translate( "VmfIO", "brush %1 editor metadata" ).arg( solid.id ) );
			for ( const SideLite &side : solid.sides )
			{
				if ( side.displacement )
				{
					if ( !HammerDisplacementInspect::InspectCanonical( *side.displacement ) )
						return fail( "Cannot save: invalid displacement data." );
					if ( !pairsOk( side.displacement->keys ) )
						return failText( QStringLiteral( "dispinfo" ) );
					for ( const auto &section : side.displacement->children )
						if ( !pairsOk( section.keys ) ) return failText( QStringLiteral( "dispinfo" ) );
				}
				if ( !textOk( side.texture ) )
					return failText( QCoreApplication::translate( "VmfIO", "brush %1 material" ).arg( solid.id ) );
				if ( side.hasVerticesPlus ) for ( const QString &vertex : side.verticesPlus ) if ( !textOk( vertex ) )
					return failText( QCoreApplication::translate( "VmfIO", "brush %1 vertex metadata" ).arg( solid.id ) );
				for ( const auto &point : side.planePts ) for ( double value : point ) if ( !std::isfinite( value ) )
					return fail( "Cannot save: map geometry or texture coordinates contain a non-finite number." );
				for ( int i = 0; i < 4; ++i ) if ( !std::isfinite( side.uaxis[i] ) || !std::isfinite( side.vaxis[i] ) )
					return fail( "Cannot save: map geometry or texture coordinates contain a non-finite number." );
				if ( !std::isfinite( side.uscale ) || !std::isfinite( side.vscale ) || !std::isfinite( side.rotation ) )
					return fail( "Cannot save: map geometry or texture coordinates contain a non-finite number." );
			}
		}
		return true;
	}

	//-----------------------------------------------------------------------------
	// Purpose: Writes the document as VMF.
	//-----------------------------------------------------------------------------
	bool SaveMap( const QString &path, MapDocLite &map, QString *errorOut )
	{
		if ( !ValidateForSave( map, errorOut ) ) return false;
		QSaveFile file( path );
		if ( !file.open( QIODevice::WriteOnly | QIODevice::Text ) )
		{
			if ( errorOut )
				*errorOut = QCoreApplication::translate( "VmfIO", "cannot open %1 for writing" ).arg( path );
			return false;
		}

		QTextStream stream( &file );
		// Qt6 default encoding is UTF-8, which is what VMF files use

		// Face ids are allocated at save time when the caller did not assign one
		int nFaceIdCounter = map.PeekNextId();

		// versioninfo
		const int mapVersion = map.WorldspawnValue( "mapversion" ).toInt();
		stream << "versioninfo\n{\n";
		if ( map.hasVersionInfo )
		{
			for ( const auto &key : map.versionProperties )
				stream << "\t" << QuoteKeyValue( key.first ) << " " << QuoteKeyValue( key.second ) << "\n";
		}
		else
		{
			stream << "\t\"editorversion\" \"400\"\n";
			stream << "\t\"editorbuild\" \"8864\"\n";
			stream << "\t\"mapversion\" \"" << mapVersion << "\"\n";
			stream << "\t\"formatversion\" \"100\"\n";
			stream << "\t\"prefab\" \"0\"\n";
		}
		stream << "}\n";

		stream << "visgroups\n{\n";
		std::function<void( const VisGroupLite &, int )> writeVisgroup = [&]( const VisGroupLite &group, int depth ) {
			const QString indent( depth, QLatin1Char( '\t' ) );
			stream << indent << "visgroup\n" << indent << "{\n";
			for ( const auto &key : group.properties ) stream << indent << "\t" << QuoteKeyValue( key.first ) << " " << QuoteKeyValue( key.second ) << "\n";
			for ( const VisGroupLite &child : group.children ) writeVisgroup( child, depth + 1 );
			stream << indent << "}\n";
		};
		for ( const VisGroupLite &group : map.visgroups ) writeVisgroup( group, 1 );
		stream << "}\n";
		bool hasCordons = false;
		for ( const VmfMetadataBlock &block : map.extraMetadata )
		{
			if ( block.name.compare( QLatin1String( "cordons" ), Qt::CaseInsensitive ) == 0 )
				hasCordons = true;
			stream << block.name << "\n{\n";
			for ( const auto &key : block.properties )
				stream << "\t" << QuoteKeyValue( key.first ) << " " << QuoteKeyValue( key.second ) << "\n";
			stream << "}\n";
		}

		// world
		stream << "world\n{\n";
		stream << "\t\"id\" \"1\"\n";
		stream << "\t\"mapversion\" \"" << mapVersion << "\"\n";
		for ( auto it = map.worldspawn.constBegin(); it != map.worldspawn.constEnd(); ++it )
		{
			if ( it.key() == QLatin1String( "mapversion" ) || it.key() == QLatin1String( "id" ) )
				continue;
			stream << "\t" << QuoteKeyValue( it.key() ) << " " << QuoteKeyValue( it.value() ) << "\n";
		}

		auto writeSolid = [&]( const SolidLite &solid ) {
			if ( solid.hiddenWrapper ) stream << "\thidden\n\t{\n";
			stream << "\tsolid\n\t{\n";
			stream << "\t\t\"id\" \"" << solid.id << "\"\n";
			for ( const SideLite &side : solid.sides )
			{
				const int faceId = ( side.id > 0 ) ? side.id : nFaceIdCounter++;
				stream << "\t\tside\n\t\t{\n";
				stream << "\t\t\t\"id\" \"" << faceId << "\"\n";
				// Plane format matches Hammer/vbsp: "(x1 y1 z1) (x2 y2 z2) (x3 y3 z3)".
				// Spaces inside the parens would break vbsp's sscanf pattern
				// "(%f %f %f) (%f %f %f) (%f %f %f)" (verified against the MSVC CRT).
				stream << "\t\t\t\"plane\" \"(" << FormatVec3( side.planePts[0] )
					<< ") (" << FormatVec3( side.planePts[1] )
					<< ") (" << FormatVec3( side.planePts[2] ) << ")\"\n";
				stream << "\t\t\t\"material\" " << QuoteKeyValue( side.texture ) << "\n";
				stream << "\t\t\t\"uaxis\" \"[" << FormatVmfNumber( side.uaxis[0] ) << " "
					<< FormatVmfNumber( side.uaxis[1] ) << " " << FormatVmfNumber( side.uaxis[2] ) << " "
					<< FormatVmfNumber( side.uaxis[3] ) << "] " << FormatVmfNumber( side.uscale ) << "\"\n";
				stream << "\t\t\t\"vaxis\" \"[" << FormatVmfNumber( side.vaxis[0] ) << " "
					<< FormatVmfNumber( side.vaxis[1] ) << " " << FormatVmfNumber( side.vaxis[2] ) << " "
					<< FormatVmfNumber( side.vaxis[3] ) << "] " << FormatVmfNumber( side.vscale ) << "\"\n";
				stream << "\t\t\t\"rotation\" \"" << FormatVmfNumber( side.rotation ) << "\"\n";
				stream << "\t\t\t\"lightmapscale\" \"" << side.lightmapscale << "\"\n";
				stream << "\t\t\t\"smoothing_groups\" \"" << side.smoothing << "\"\n";
				if ( side.hasVerticesPlus )
				{
					stream << "\t\t\tvertices_plus\n\t\t\t{\n";
					for ( const QString &vertex : side.verticesPlus )
						stream << "\t\t\t\t\"v\" " << QuoteKeyValue( vertex ) << "\n";
					stream << "\t\t\t}\n";
				}
				if ( side.displacement )
				{
					const auto &disp = *side.displacement;
					stream << "\t\t\t" << disp.name << "\n\t\t\t{\n";
					for ( const auto &key : disp.keys )
						stream << "\t\t\t\t" << QuoteKeyValue( key.first ) << " " << QuoteKeyValue( key.second ) << "\n";
					for ( const auto &section : disp.children )
					{
						stream << "\t\t\t\t" << section.name << "\n\t\t\t\t{\n";
						for ( const auto &key : section.keys )
							stream << "\t\t\t\t\t" << QuoteKeyValue( key.first ) << " " << QuoteKeyValue( key.second ) << "\n";
						stream << "\t\t\t\t}\n";
					}
					stream << "\t\t\t}\n";
				}
				stream << "\t\t}\n";
			}
			if ( solid.hasEditor )
			{
				stream << "\t\teditor\n\t\t{\n";
				for ( const auto &key : solid.editorProperties )
					stream << "\t\t\t" << QuoteKeyValue( key.first ) << " " << QuoteKeyValue( key.second ) << "\n";
				stream << "\t\t}\n";
			}
			stream << "\t}\n";
			if ( solid.hiddenWrapper ) stream << "\t}\n";
		};
		for ( const SolidLite &solid : map.solids )
			if ( solid.ownerEntityId == 0 ) writeSolid( solid );
		stream << "}\n";

		// entities
		for ( const EntityLite &entity : map.entities )
		{
			if ( entity.hiddenWrapper ) stream << "hidden\n{\n";
			stream << "entity\n{\n";
			stream << "\t\"id\" \"" << entity.id << "\"\n";
			stream << "\t\"classname\" " << QuoteKeyValue( entity.classname ) << "\n";
			bool hasBrushes = false;
			for ( const SolidLite &solid : map.solids ) hasBrushes = hasBrushes || solid.ownerEntityId == entity.id;
			if ( entity.hasExplicitOrigin || !hasBrushes )
				stream << "\t\"origin\" \"" << FormatVec3( entity.origin ) << "\"\n";
			for ( auto it = entity.keyvalues.constBegin(); it != entity.keyvalues.constEnd(); ++it )
				stream << "\t" << QuoteKeyValue( it.key() ) << " " << QuoteKeyValue( it.value() ) << "\n";
			for ( const SolidLite &solid : map.solids )
				if ( solid.ownerEntityId == entity.id ) writeSolid( solid );
			if ( !entity.connections.isEmpty() )
			{
				stream << "\tconnections\n\t{\n";
				for ( const QPair<QString, QString> &connection : entity.connections )
					stream << "\t\t" << QuoteKeyValue( connection.first ) << " " << QuoteKeyValue( connection.second ) << "\n";
				stream << "\t}\n";
			}
			stream << "\teditor\n\t{\n";
			if ( entity.hasEditor )
				for ( const auto &key : entity.editorProperties )
					stream << "\t\t" << QuoteKeyValue( key.first ) << " " << QuoteKeyValue( key.second ) << "\n";
			else
				stream << "\t\t\"color\" \"220 30 220\"\n\t\t\"visgroupshown\" \"1\"\n\t\t\"visgroupautoshown\" \"1\"\n\t\t\"logicalpos\" \"[0 0]\"\n";
			stream << "\t}\n";
			stream << "}\n";
			if ( entity.hiddenWrapper ) stream << "}\n";
		}

		// Preserve Hammer's camera list and active camera across edits.
		stream << "cameras\n{\n";
		for ( const auto &key : map.cameraProperties )
			stream << "\t" << QuoteKeyValue( key.first ) << " " << QuoteKeyValue( key.second ) << "\n";
		for ( const CameraLite &camera : map.cameras )
		{
			stream << "\tcamera\n\t{\n";
			for ( const auto &key : camera.keyvalues )
				stream << "\t\t" << QuoteKeyValue( key.first ) << " " << QuoteKeyValue( key.second ) << "\n";
			stream << "\t}\n";
		}
		stream << "}\n";
		if ( !hasCordons )
		{
			stream << "cordon\n{\n";
			if ( map.cordonProperties.isEmpty() )
				stream << "\t\"active\" \"0\"\n";
			else
				for ( const auto &key : map.cordonProperties )
					stream << "\t" << QuoteKeyValue( key.first ) << " " << QuoteKeyValue( key.second ) << "\n";
			stream << "}\n";
		}

		// Persist any lazily allocated face ids
		if ( nFaceIdCounter > map.PeekNextId() )
			map.SetNextId( nFaceIdCounter );

		stream.flush();
		if ( stream.status() != QTextStream::Ok || !file.commit() )
		{
			if ( errorOut ) *errorOut = QCoreApplication::translate( "VmfIO", "cannot save %1: %2" ).arg( path, file.errorString() );
			return false;
		}
		return true;
	}

	//-----------------------------------------------------------------------------
	// Purpose: Reads a VMF file into the document.
	//-----------------------------------------------------------------------------
	bool LoadMap( const QString &path, MapDocLite &map, QString *errorOut )
	{
		QFile file( path );
		if ( !file.open( QIODevice::ReadOnly | QIODevice::Text ) )
		{
			if ( errorOut )
				*errorOut = QCoreApplication::translate( "VmfIO", "cannot open %1 for reading" ).arg( path );
			return false;
		}

		QTextStream stream( &file );
		const QString text = stream.readAll();
		file.close();

		VmfTokenizer tokenizer( text );
		map.worldspawn.clear();
		map.entities.clear();
		map.solids.clear();
		map.cameraProperties.clear();
		map.cameras.clear();
		map.visgroups.clear();
		map.cordonProperties.clear();
		map.hasVersionInfo = false;
		map.versionProperties.clear();
		map.extraMetadata.clear();

		int nMaxId = 0;
		bool seenCameras = false;
		bool seenCordon = false;
		bool seenVisgroups = false;
		QList<VmfChunk> pending;
		while ( true )
		{
			VmfChunk chunk;
			if ( !pending.isEmpty() ) chunk = pending.takeFirst();
			else
			{
				QString name;
				if ( !tokenizer.Next( name ) ) break;
				chunk.name = name;
				if ( !ParseChunkBody( tokenizer, chunk ) )
				{
					if ( errorOut ) *errorOut = QCoreApplication::translate( "VmfIO", "parse error near chunk '%1'" ).arg( chunk.name );
					return false;
				}
			}
			if ( chunk.name.compare( QLatin1String( "hidden" ), Qt::CaseInsensitive ) == 0 )
			{
				VmfChunk root; root.children.append( chunk );
				if ( !ExpandHiddenChildren( root, "entity", errorOut ) ) return false;
				pending = root.children + pending;
				continue;
			}
			if ( chunk.name.compare( QLatin1String( "world" ), Qt::CaseInsensitive ) == 0 ||
				 chunk.name.compare( QLatin1String( "entity" ), Qt::CaseInsensitive ) == 0 )
				if ( !ExpandHiddenChildren( chunk, "solid", errorOut ) ) return false;
			if ( !CheckSupportedChunk( chunk, errorOut ) )
				return false;
			if ( chunk.name.compare( QLatin1String( "visgroups" ), Qt::CaseInsensitive ) == 0 )
			{
				if ( seenVisgroups )
				{
					if ( errorOut ) *errorOut = QCoreApplication::translate( "VmfIO", "Unsupported VMF data: duplicate visgroups block." );
					return false;
				}
				seenVisgroups = true;
				std::function<VisGroupLite( const VmfChunk & )> readVisgroup = [&]( const VmfChunk &input ) {
					VisGroupLite group; group.properties = input.keys;
					for ( const VmfChunk &child : input.children ) group.children.append( readVisgroup( child ) );
					return group;
				};
				for ( const VmfChunk &child : chunk.children ) map.visgroups.append( readVisgroup( child ) );
				continue;
			}
			if ( chunk.name.compare( QLatin1String( "viewsettings" ), Qt::CaseInsensitive ) == 0 ||
				 chunk.name.compare( QLatin1String( "palette_plus" ), Qt::CaseInsensitive ) == 0 ||
				 chunk.name.compare( QLatin1String( "colorcorrection_plus" ), Qt::CaseInsensitive ) == 0 ||
				 chunk.name.compare( QLatin1String( "light_plus" ), Qt::CaseInsensitive ) == 0 ||
				 chunk.name.compare( QLatin1String( "postprocess_plus" ), Qt::CaseInsensitive ) == 0 ||
				 chunk.name.compare( QLatin1String( "bgimages_plus" ), Qt::CaseInsensitive ) == 0 ||
				 chunk.name.compare( QLatin1String( "cordons" ), Qt::CaseInsensitive ) == 0 )
			{
				for ( const auto &block : map.extraMetadata )
					if ( block.name.compare( chunk.name, Qt::CaseInsensitive ) == 0 )
					{
						if ( errorOut ) *errorOut = QCoreApplication::translate( "VmfIO", "Unsupported VMF data: %1. The file was not opened to prevent data loss." ).arg( QCoreApplication::translate( "VmfIO", "duplicate block %1" ).arg( chunk.name ) );
						return false;
					}
				if ( chunk.name.compare( QLatin1String( "cordons" ), Qt::CaseInsensitive ) == 0 && seenCordon )
				{
					if ( errorOut ) *errorOut = QCoreApplication::translate( "VmfIO", "Unsupported VMF data: %1. The file was not opened to prevent data loss." ).arg( QCoreApplication::translate( "VmfIO", "multiple cordon blocks" ) );
					return false;
				}
				if ( chunk.name.compare( QLatin1String( "cordons" ), Qt::CaseInsensitive ) == 0 ) seenCordon = true;
				map.extraMetadata.append( VmfMetadataBlock{ chunk.name, chunk.keys } );
				continue;
			}
			if ( chunk.name.compare( QLatin1String( "cordon" ), Qt::CaseInsensitive ) == 0 )
			{
				if ( seenCordon )
				{
					if ( errorOut )
						*errorOut = QCoreApplication::translate( "VmfIO", "Unsupported VMF data: %1. The file was not opened to prevent data loss." ).arg( QCoreApplication::translate( "VmfIO", "duplicate cordon" ) );
					return false;
				}
				seenCordon = true;
				map.cordonProperties = chunk.keys;
				continue;
			}
			if ( chunk.name.compare( QLatin1String( "cameras" ), Qt::CaseInsensitive ) == 0 )
			{
				if ( seenCameras )
				{
					if ( errorOut )
						*errorOut = QCoreApplication::translate( "VmfIO", "Unsupported VMF data: %1. The file was not opened to prevent data loss." ).arg( QCoreApplication::translate( "VmfIO", "duplicate cameras" ) );
					return false;
				}
				seenCameras = true;
				map.cameraProperties = chunk.keys;
				for ( const VmfChunk &child : chunk.children )
				{
					CameraLite camera;
					camera.keyvalues = child.keys;
					map.cameras.append( camera );
				}
				continue;
			}

			if ( chunk.name.compare( QLatin1String( "versioninfo" ), Qt::CaseInsensitive ) == 0 )
			{
				if ( map.hasVersionInfo )
				{
					if ( errorOut )
						*errorOut = QCoreApplication::translate( "VmfIO", "Unsupported VMF data: %1. The file was not opened to prevent data loss." ).arg( QCoreApplication::translate( "VmfIO", "duplicate versioninfo" ) );
					return false;
				}
				map.hasVersionInfo = true;
				map.versionProperties = chunk.keys;
				continue;
			}
			else if ( chunk.name.compare( QLatin1String( "world" ), Qt::CaseInsensitive ) == 0 )
			{
				for ( const QPair<QString, QString> &kv : chunk.keys )
				{
					if ( kv.first == QLatin1String( "id" ) )
					{
						nMaxId = qMax( nMaxId, kv.second.toInt() );
						continue;
					}
					map.worldspawn.insert( kv.first, kv.second );
				}
				for ( const VmfChunk &child : chunk.children )
				{
					if ( child.name.compare( QLatin1String( "solid" ), Qt::CaseInsensitive ) == 0 )
					{
						SolidLite solid;
						if ( !LoadSolid( child, solid, errorOut ) )
							return false;
						solid.hiddenWrapper = child.hiddenWrapper;
						map.RecalcSolidBounds( solid );
						map.solids.append( solid );
						nMaxId = qMax( nMaxId, solid.id );
						for ( const SideLite &side : solid.sides )
							nMaxId = qMax( nMaxId, side.id );
					}
				}
			}
			else if ( chunk.name.compare( QLatin1String( "entity" ), Qt::CaseInsensitive ) == 0 )
			{
				EntityLite entity;
				entity.hiddenWrapper = chunk.hiddenWrapper;
				entity.hasExplicitOrigin = false;
				for ( const QPair<QString, QString> &kv : chunk.keys )
				{
					if ( kv.first == QLatin1String( "id" ) )
					{
						entity.id = kv.second.toInt();
					}
					else if ( kv.first == QLatin1String( "classname" ) )
					{
						entity.classname = kv.second;
					}
					else if ( kv.first == QLatin1String( "origin" ) )
					{
						entity.hasExplicitOrigin = true;
						double origin[3] = { 0.0, 0.0, 0.0 };
						if ( ParseVec3( kv.second, origin ) )
						{
							entity.origin[0] = origin[0];
							entity.origin[1] = origin[1];
							entity.origin[2] = origin[2];
						}
						else
						{
							if ( errorOut )
								*errorOut = QCoreApplication::translate( "VmfIO", "Unsupported VMF data: %1. The file was not opened to prevent data loss." ).arg( QCoreApplication::translate( "VmfIO", "malformed entity origin '%1'" ).arg( kv.second ) );
							return false;
						}
					}
					else
					{
						entity.keyvalues.insert( kv.first, kv.second );
					}
				}

				// Connections are ordered pairs because VMF permits repeated names.
				for ( const VmfChunk &child : chunk.children )
				{
					if ( child.name.compare( QLatin1String( "solid" ), Qt::CaseInsensitive ) == 0 )
					{
						if ( entity.id <= 1 )
						{
							if ( errorOut ) *errorOut = QCoreApplication::translate( "VmfIO", "Unsupported VMF data: brush entity has invalid id." );
							return false;
						}
						SolidLite solid;
						if ( !LoadSolid( child, solid, errorOut ) ) return false;
						solid.hiddenWrapper = child.hiddenWrapper;
						solid.ownerEntityId = entity.id;
						map.RecalcSolidBounds( solid );
						map.solids.append( solid );
						nMaxId = qMax( nMaxId, solid.id );
						for ( const SideLite &side : solid.sides ) nMaxId = qMax( nMaxId, side.id );
					}
					if ( child.name.compare( QLatin1String( "editor" ), Qt::CaseInsensitive ) == 0 )
					{
						entity.hasEditor = true;
						entity.editorProperties = child.keys;
					}
					if ( child.name.compare( QLatin1String( "connections" ), Qt::CaseInsensitive ) == 0 )
						for ( const QPair<QString, QString> &connection : child.keys )
							entity.connections.append( connection );
				}

				// Mirror the yaw out of "angles" into the entity.angle field
				const QString angles = entity.keyvalues.value( "angles" );
				if ( !angles.isEmpty() )
				{
					double a[3] = { 0.0, 0.0, 0.0 };
					if ( ParseVec3( angles, a ) )
						entity.angle = a[1];
				}

				map.entities.append( entity );
				nMaxId = qMax( nMaxId, entity.id );
			}
			// Empty visgroups chunks are skipped.
		}

		// Object selection uses IDs shared by entities and brushes. Reject
		// ambiguous brush-entity maps instead of selecting or deleting the wrong
		// object. Face IDs are checked separately by Map Check.
		bool hasBrushEntities = false;
		for ( const SolidLite &solid : map.solids ) hasBrushEntities = hasBrushEntities || solid.ownerEntityId != 0;
		if ( hasBrushEntities )
		{
			QSet<int> ids;
			for ( const EntityLite &entity : map.entities )
			{
				if ( entity.id <= 1 || ids.contains( entity.id ) )
				{
					if ( errorOut ) *errorOut = QCoreApplication::translate( "VmfIO", "Unsupported VMF data: invalid or duplicate object id." );
					return false;
				}
				ids.insert( entity.id );
			}
			for ( const SolidLite &solid : map.solids )
			{
				if ( solid.id <= 1 || ids.contains( solid.id ) )
				{
					if ( errorOut ) *errorOut = QCoreApplication::translate( "VmfIO", "Unsupported VMF data: invalid or duplicate object id." );
					return false;
				}
				ids.insert( solid.id );
			}
		}
		// Ambiguous entity IDs would merge multiple brush entities on save.
		for ( const SolidLite &solid : map.solids )
			if ( solid.ownerEntityId != 0 )
			{
				int owners = 0;
				for ( const EntityLite &entity : map.entities ) if ( entity.id == solid.ownerEntityId ) ++owners;
				if ( owners != 1 )
				{
					if ( errorOut ) *errorOut = QCoreApplication::translate( "VmfIO", "Unsupported VMF data: duplicate brush entity id." );
					return false;
				}
			}
		if ( nMaxId >= 0x7ffffff0 )
		{
			if ( errorOut )
				*errorOut = QCoreApplication::translate( "VmfIO", "Unsupported VMF data: %1. The file was not opened to prevent data loss." ).arg( QCoreApplication::translate( "VmfIO", "object id out of range" ) );
			return false;
		}
		map.SetNextId( qMax( nMaxId + 1, 1 ) );
		return true;
	}
} // namespace VmfIO
