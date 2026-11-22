//========= Hammer Qt6 (hammer2/app) ==========================================
//
// Purpose: GameConfig.txt loading and default generation. Parsing is a small
//          quoted-token/brace scanner (a subset of the KeyValues text format)
//          implemented with QDir/QFile so it stays portable across platforms.
//
//=============================================================================

#include "gameconfig.h"

#include <QDir>
#include <QCoreApplication>
#include <QFile>
#include <QMap>
#include <QSaveFile>
#include <QTextStream>

namespace
{
	//-----------------------------------------------------------------------------
	// Purpose: A minimal quoted-token/brace tokenizer for KeyValues-style text.
	//-----------------------------------------------------------------------------
	class KvTokenizer
	{
	public:
		KvTokenizer( const QString &text )
			: m_strText( text )
			, m_nPos( 0 )
		{
		}

		bool Next( QString &token )
		{
			SkipWhitespaceAndComments();
			if ( m_nPos >= m_strText.length() )
				return false;

			const QChar ch = m_strText.at( m_nPos );
			if ( ch == QLatin1Char( '"' ) )
			{
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
					token += c;
					++m_nPos;
				}
				return true;
			}

			if ( ch == QLatin1Char( '{' ) || ch == QLatin1Char( '}' ) )
			{
				token = ch;
				++m_nPos;
				return true;
			}

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

		bool Peek( QString &token )
		{
			const int savedPos = m_nPos;
			const bool bOk = Next( token );
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
	// Purpose: Walks into the "GameConfigs" block and picks the first game
	//          section, returning its "GameData" value.
	//-----------------------------------------------------------------------------
	bool ParseGameConfigText( const QString &text, QString &gameName, QStringList &fgdPaths )
	{
		KvTokenizer t( text );

		QString token;
		if ( !t.Next( token ) || ( token.compare( QLatin1String( "GameConfigs" ), Qt::CaseInsensitive ) != 0 &&
			 token.compare( QLatin1String( "Configs" ), Qt::CaseInsensitive ) != 0 ) )
			return false;

		if ( !t.Next( token ) || token != QLatin1String( "{" ) )
			return false;

		// First game section inside GameConfigs
		QString section;
		for ( ;; )
		{
			if ( !t.Next( section ) || section == QLatin1String( "}" ) || !t.Next( token ) || token == QLatin1String( "}" ) ) return false;
			if ( token == QLatin1String( "{" ) ) break;
		}

		// Valve's GameConfig.txt wraps profiles in a Games block; older Qt
		// configurations stored the first game directly under GameConfigs.
		if ( section.compare( QLatin1String( "Games" ), Qt::CaseInsensitive ) == 0 )
		{
			if ( !t.Next( section ) || section == QLatin1String( "}" ) || !t.Next( token ) || token != QLatin1String( "{" ) )
				return false;
		}
		gameName = section;

		QString directFgd;
		QMap<int, QString> indexedFgds;
		int depth = 1;
		bool inHammer = false;
		while ( depth > 0 )
		{
			token.clear();
			if ( !t.Next( token ) )
				return false;
			if ( token == QLatin1String( "}" ) )
			{
				if ( depth == 2 ) inHammer = false;
				--depth;
				continue;
			}
			const QString key = token;
			QString value;
			if ( !t.Next( value ) || value == QLatin1String( "}" ) )
				return false;
			if ( value == QLatin1String( "{" ) ) { if ( depth == 1 ) inHammer = key.compare( QLatin1String( "Hammer" ), Qt::CaseInsensitive ) == 0; ++depth; continue; }
			if ( depth == 1 && key.compare( QLatin1String( "GameData" ), Qt::CaseInsensitive ) == 0 )
				directFgd = value;
			if ( depth == 2 && inHammer && key.startsWith( QLatin1String( "GameData" ), Qt::CaseInsensitive ) )
			{
				bool valid = false;
				const int index = key.mid( 8 ).toInt( &valid );
				if ( valid && index >= 0 ) indexedFgds.insert( index, value );
			}
		}
		// Original Hammer reads sequential GameData0..N and stops at the first gap.
		for ( int index = 0; indexedFgds.contains( index ) && !indexedFgds.value( index ).isEmpty(); ++index )
			fgdPaths.append( indexedFgds.value( index ) );
		if ( fgdPaths.isEmpty() && !directFgd.isEmpty() ) fgdPaths.append( directFgd );
		return !fgdPaths.isEmpty();
	}

	//-----------------------------------------------------------------------------
	// Purpose: Writes the default GameConfig.txt (Garry's Mod, ./fgd/garrysmod.fgd).
	//-----------------------------------------------------------------------------
	bool WriteDefaultGameConfig( const QString &path )
	{
		// Atomic so a crash cannot leave a truncated (then "malformed") config behind
		QSaveFile file( path );
		if ( !file.open( QIODevice::WriteOnly | QIODevice::Text ) )
			return false;

		QTextStream stream( &file );
		stream << "\"GameConfigs\"\n";
		stream << "{\n";
		stream << "\t\"Garry's Mod\"\n";
		stream << "\t{\n";
		stream << "\t\t\"GameData\"\t\t\"fgd/garrysmod.fgd\"\n";
		stream << "\t}\n";
		stream << "}\n";
		stream.flush();
		return stream.status() == QTextStream::Ok && file.commit();
	}
} // anonymous namespace

//-----------------------------------------------------------------------------
// Purpose: Loads (or creates) GameConfig.txt in exeDir.
//-----------------------------------------------------------------------------
bool LoadGameConfig( const QString &exeDir, GameConfigInfo &outInfo, QString *errorOut )
{
	outInfo = GameConfigInfo();

	const QString configPath = QDir( exeDir ).filePath( QStringLiteral( "GameConfig.txt" ) );

	if ( !QFile::exists( configPath ) )
	{
		if ( !WriteDefaultGameConfig( configPath ) )
		{
			if ( errorOut )
				*errorOut = QCoreApplication::translate( "GameConfig", "cannot create default %1" ).arg( configPath );
			return false;
		}
		outInfo.bCreatedDefault = true;
	}

	QFile file( configPath );
	if ( !file.open( QIODevice::ReadOnly | QIODevice::Text ) )
	{
		if ( errorOut )
			*errorOut = QCoreApplication::translate( "GameConfig", "cannot read %1" ).arg( configPath );
		return false;
	}

	QTextStream stream( &file );
	const QString text = stream.readAll();
	file.close();

	QString gameName;
	QStringList fgdValues;
	if ( !ParseGameConfigText( text, gameName, fgdValues ) )
	{
		if ( errorOut )
			*errorOut = QCoreApplication::translate( "GameConfig", "malformed GameConfig.txt: %1" ).arg( configPath );
		return false;
	}

	outInfo.gameName = gameName;

	// Resolve the FGD path relative to the executable directory when it is
	// not already absolute
	const QDir exePathDir( exeDir );
	for ( const QString &fgdValue : fgdValues )
		outInfo.fgdPaths.append( QDir::cleanPath( QDir::isAbsolutePath( fgdValue ) ? fgdValue : exePathDir.filePath( fgdValue ) ) );
	outInfo.fgdPath = outInfo.fgdPaths.first();

	return true;
}
