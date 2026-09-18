//========== Copyleft (c) 2026, HL2SB, Some rights reserved. ===========//
//
// Purpose: Garry's Mod .gma addon archive support for HL2SB.
//
//   GMod distributes addons as .gma archives.  HL2SB mounts addon *folders*
//   (gameinfo.txt: "game+mod hl2sb/addons/*"), so a dropped-in
//   addons/<name>.gma used to do nothing at all.  This file unpacks such an
//   archive once into addons/<sanitized header name>/ and mounts that folder on
//   both the MOD and the GAME search path, after which the addon behaves exactly
//   like one the user had unzipped by hand - Lua, materials and models all
//   resolve, and the Lua loaders (which use FindFirstEx( "lua/weapons/*", "MOD",
//   ... )) see it in the same session because CBaseFileSystem::FindFirstHelper
//   walks m_SearchPaths live on every call.
//
//   The .gma format (v3, little-endian):
//       char[4]  magic  = "GMAD"
//       uint8    version           (1, 2 or 3)
//       uint64   steamid           (v3+ only)
//       uint64   timestamp         (v3+ only)
//       cstring  requiredcontent   (usually empty)
//       cstring  name
//       cstring  description
//       cstring  author
//       int32    addonversion      (v3+ only)
//       then, repeated until filenumber == 0:
//           uint32   filenumber
//           cstring  filename
//           int64    filesize
//           uint32   crc            (standard CRC-32, same as zlib.crc32)
//       then the raw bytes of every indexed file, in index order.
//       A trailing "file number lookup" table may follow; it is ignored.
//
//===========================================================================//

#include "cbase.h"
#include "filesystem.h"
#include "checksum_crc.h"
#include "utlstring.h"
#include "utlvector.h"

#include "hl2sb_gma.h"

// memdbgon must be the last include file in a .cpp file!!!
#include "tier0/memdbgon.h"

#define GMA_MAGIC				"GMAD"
#define GMA_MARKER_VERSION		1			// bump when the marker layout changes
#define GMA_MARKER_FILE			".hl2sb_gma"
#define GMA_MIN_VERSION			1
#define GMA_MAX_VERSION			3			// highest layout we understand
#define GMA_MAX_STRING			4096		// hard cap on any header/index string
#define GMA_MAX_ENTRY_NAME		512
#define GMA_MAX_ENTRIES			( 256 * 1024 )
#define GMA_MAX_FILE_SIZE		( (int64)4 * 1024 * 1024 * 1024 )
#define GMA_MAX_IO_OFFSET		0x7FFFFFF0	// FileSystemSeek_t takes an int
#define GMA_IO_CHUNK			( 64 * 1024 )
#define GMA_MAX_LOG_SKIPS		20
#define GMA_MAX_LOG_CRC			10

// How the "addonversion" int32 that v3 added is treated for old archives, which
// are not guaranteed to carry it: trust the version (default), assume it is
// absent, or assume it is present.  The old-archive layout is probed both ways.
enum GMAAddonVersionMode_t
{
	GMA_AV_DEFAULT = 0,		// read it for v3+, skip it for v1/v2
	GMA_AV_SKIP,			// never read it
	GMA_AV_READ,			// always read it
};

// Scratch buffer for streaming file data in and out.  File scope (not the
// stack) so neither realm pushes 64 KB onto the stack on every map load.
static unsigned char s_GMA_IoBuffer[ GMA_IO_CHUNK ];

//-----------------------------------------------------------------------------
// Small helpers
//-----------------------------------------------------------------------------

// A file name we are willing to hand to the filesystem: non-empty and free of
// control characters.  High bytes are allowed on purpose - real addons do ship
// UTF-8 file names.
static bool GMA_IsPrintableName( const char *pszText )
{
	if ( !pszText || !pszText[0] )
		return false;

	for ( const char *p = pszText; *p; ++p )
	{
		unsigned char c = (unsigned char)*p;
		if ( c < 32 || c == 127 )
			return false;
	}
	return true;
}

// Security gate for an entry path read out of an archive.  Rejects absolute
// paths, drive letters / alternate data streams, UNC prefixes, ".." traversal
// (including the Windows ".. " / "..." forms, which the OS normalizes back to a
// parent reference) and empty components, so a hostile .gma can never escape
// its own folder.
static bool GMA_IsSafeEntryPath( const char *pszPath )
{
	if ( !pszPath || !pszPath[0] )
		return false;

	// Absolute, UNC or root-relative.
	if ( pszPath[0] == '/' || pszPath[0] == '\\' )
		return false;

	// Windows drive letters ("c:...") and NTFS alternate data streams.
	if ( strchr( pszPath, ':' ) != NULL )
		return false;

	const int nLength = Q_strlen( pszPath );
	if ( nLength <= 0 || pszPath[nLength - 1] == '/' || pszPath[nLength - 1] == '\\' )
		return false;

	const char *p = pszPath;
	while ( *p )
	{
		const char *pEnd = p;
		while ( *pEnd && *pEnd != '/' && *pEnd != '\\' )
			++pEnd;

		int nComponent = (int)( pEnd - p );
		if ( nComponent == 0 )
			return false;

		// Windows strips trailing spaces and dots from every path component, so
		// "..", ".. " and "..." are all the parent reference.  Refuse anything
		// that trimming would change, which also closes the aliasing hole where
		// "lua." and "lua" would be the same folder.
		int nTrimmed = nComponent;
		while ( nTrimmed > 0 && ( p[ nTrimmed - 1 ] == ' ' || p[ nTrimmed - 1 ] == '.' ) )
			--nTrimmed;

		if ( nTrimmed != nComponent )
			return false;

		if ( ( nComponent == 1 && p[0] == '.' )
			|| ( nComponent == 2 && p[0] == '.' && p[1] == '.' ) )
			return false;

		// Refuse the remaining components Windows cannot create.
		for ( int i = 0; i < nComponent; ++i )
		{
			if ( p[i] == '*' || p[i] == '?' || p[i] == '"' || p[i] == '<' || p[i] == '>' || p[i] == '|' )
				return false;
		}

		p = ( *pEnd ) ? pEnd + 1 : pEnd;
	}
	return true;
}

// Keep letters, digits, '_' and '-'; turn spaces into '_'; drop everything else
// (which also removes '.', so the result can never be "." or "..").
static void GMA_SanitizeFolderName( const char *pszIn, char *pszOut, int nOutSize )
{
	int nOut = 0;
	pszOut[0] = 0;

	for ( const char *p = pszIn; p && *p && nOut < nOutSize - 1; ++p )
	{
		unsigned char c = (unsigned char)*p;

		if ( ( c >= 'a' && c <= 'z' ) || ( c >= 'A' && c <= 'Z' ) || ( c >= '0' && c <= '9' ) || c == '_' || c == '-' )
		{
			pszOut[ nOut++ ] = (char)c;
		}
		else if ( c == ' ' || c == '\t' )
		{
			pszOut[ nOut++ ] = '_';
		}
	}

	pszOut[ nOut ] = 0;
}

// "addons/Foo Bar.gma" -> "Foo_Bar"
static void GMA_FolderNameFromArchiveFile( const char *pszArchiveFile, char *pszOut, int nOutSize )
{
	char szBase[ MAX_PATH ];
	Q_strncpy( szBase, pszArchiveFile ? pszArchiveFile : "", sizeof( szBase ) );

	const char *pBase = szBase;
	for ( const char *p = szBase; *p; ++p )
	{
		if ( *p == '/' || *p == '\\' )
			pBase = p + 1;
	}

	const int nLength = Q_strlen( pBase );
	if ( nLength > 4 && !V_stricmp( pBase + nLength - 4, ".gma" ) )
	{
		char szStem[ MAX_PATH ];
		Q_strncpy( szStem, pBase, MIN( (int)sizeof( szStem ), nLength - 3 ) );
		GMA_SanitizeFolderName( szStem, pszOut, nOutSize );
	}
	else
	{
		GMA_SanitizeFolderName( pBase, pszOut, nOutSize );
	}
}

// Strip the last path component in place (forward or back slashes).
static void GMA_StripFileName( char *pszPath )
{
	char *pLast = NULL;
	for ( char *p = pszPath; *p; ++p )
	{
		if ( *p == '/' || *p == '\\' )
			pLast = p;
	}
	if ( pLast )
		*pLast = 0;
}

// Normalize separators in place (backslashes -> forward slashes).  Duplicated
// separators cannot reach here: GMA_IsSafeEntryPath already rejects them.
static void GMA_NormalizeSlashes( char *pszPath )
{
	char *pOut = pszPath;
	for ( const char *p = pszPath; *p; ++p )
	{
		if ( *p == '\\' )
			*pOut++ = '/';
		else
			*pOut++ = *p;
	}
	*pOut = 0;
}

static void GMA_FormatHex( const unsigned char *pData, int nCount, char *pOut, int nOutSize )
{
	int nOut = 0;
	pOut[0] = 0;

	for ( int i = 0; i < nCount && nOut < nOutSize - 4; ++i )
	{
		if ( i )
			pOut[ nOut++ ] = ' ';

		Q_snprintf( pOut + nOut, nOutSize - nOut, "%02x", pData[i] );
		nOut += 2;
	}
}

//-----------------------------------------------------------------------------
// Purpose: Sequential, buffered reader over an already-open archive.  Tracks
//          the absolute offset so the caller can find where the file data
//          section begins.  Never reads the whole archive into memory.
//-----------------------------------------------------------------------------
class CGMABufferedReader
{
public:
	CGMABufferedReader()
	{
		m_hFile = FILESYSTEM_INVALID_HANDLE;
		m_nBufferPos = 0;
		m_nBufferEnd = 0;
		m_nOffset = 0;
	}

	void Begin( FileHandle_t hFile )
	{
		m_hFile = hFile;
		m_nBufferPos = 0;
		m_nBufferEnd = 0;
		m_nOffset = 0;
	}

	int64 Offset() const { return m_nOffset; }

	bool ReadBytes( void *pDest, int nCount )
	{
		if ( !pDest || nCount < 0 )
			return false;

		unsigned char *pOut = (unsigned char *)pDest;
		int nDone = 0;

		while ( nDone < nCount )
		{
			if ( m_nBufferPos >= m_nBufferEnd && !Fill() )
				return false;

			const int nAvailable = m_nBufferEnd - m_nBufferPos;
			const int nTake = MIN( nAvailable, nCount - nDone );
			memcpy( pOut + nDone, m_Buffer + m_nBufferPos, nTake );

			m_nBufferPos += nTake;
			m_nOffset += nTake;
			nDone += nTake;
		}
		return true;
	}

	bool ReadUInt8( unsigned char &nOut )
	{
		return ReadBytes( &nOut, (int)sizeof( nOut ) );
	}

	bool ReadUInt32( uint32 &nOut )
	{
		unsigned char bytes[4];
		if ( !ReadBytes( bytes, 4 ) )
			return false;

		nOut = (uint32)bytes[0]
			| ( (uint32)bytes[1] << 8 )
			| ( (uint32)bytes[2] << 16 )
			| ( (uint32)bytes[3] << 24 );
		return true;
	}

	bool ReadInt32( int32 &nOut )
	{
		uint32 nValue = 0;
		if ( !ReadUInt32( nValue ) )
			return false;

		nOut = (int32)nValue;
		return true;
	}

	bool ReadUInt64( uint64 &nOut )
	{
		unsigned char bytes[8];
		if ( !ReadBytes( bytes, 8 ) )
			return false;

		uint64 nValue = 0;
		for ( int i = 7; i >= 0; --i )
			nValue = ( nValue << 8 ) | (uint64)bytes[i];

		nOut = nValue;
		return true;
	}

	bool ReadInt64( int64 &nOut )
	{
		uint64 nValue = 0;
		if ( !ReadUInt64( nValue ) )
			return false;

		nOut = (int64)nValue;
		return true;
	}

	// Reads a NUL-terminated string.  The stream is ALWAYS advanced past the
	// terminator - even when the string is longer than pOut - so a hostile
	// length cannot desync the parse.  Returns false on EOF or when no
	// terminator shows up within nMaxBytes.
	bool ReadCString( char *pOut, int nOutSize, int nMaxBytes, int *pnLength )
	{
		int nLength = 0;
		bool bTerminated = false;

		while ( nLength < nMaxBytes )
		{
			unsigned char c = 0;
			if ( !ReadBytes( &c, 1 ) )
				break;

			if ( c == 0 )
			{
				bTerminated = true;
				break;
			}

			if ( nLength < nOutSize - 1 )
				pOut[ nLength ] = (char)c;

			++nLength;
		}

		pOut[ MIN( nLength, nOutSize - 1 ) ] = 0;

		if ( pnLength )
			*pnLength = nLength;

		return bTerminated;
	}

private:
	bool Fill()
	{
		if ( m_hFile == FILESYSTEM_INVALID_HANDLE )
			return false;

		const int nRead = filesystem->Read( m_Buffer, (int)sizeof( m_Buffer ), m_hFile );
		if ( nRead <= 0 )
		{
			m_nBufferPos = m_nBufferEnd = 0;
			return false;
		}

		m_nBufferPos = 0;
		m_nBufferEnd = nRead;
		return true;
	}

	FileHandle_t	m_hFile;
	int				m_nBufferPos;
	int				m_nBufferEnd;
	int64			m_nOffset;
	unsigned char	m_Buffer[ 8 * 1024 ];
};

//-----------------------------------------------------------------------------
// Parsed archive description
//-----------------------------------------------------------------------------
struct GMAFileEntry_t
{
	CUtlString	m_Name;
	int64		m_nSize;
	uint32		m_nCRC;
};

struct GMAHeader_t
{
	int			m_nVersion;
	uint64		m_nSteamID;
	uint64		m_nTimestamp;
	CUtlString	m_Name;
	CUtlString	m_Description;
	CUtlString	m_Author;
	int32		m_nAddonVersion;
	int64		m_nDataOffset;		// absolute offset of the first data blob
};

// The requiredcontent field holds content *paths* ("models/player/x.mdl",
// "materials/a.vmt", "lua/autorun/x.lua").  A string with neither a separator
// nor an extension is therefore a strong hint that a single-string
// requiredcontent field has been mistaken for a list - which eats the addon
// name - so such a parse is only used as a last resort.  See GMA_ParseArchive.
static bool GMA_LooksLikeContentPath( const char *pszText )
{
	if ( !pszText || !pszText[0] )
		return false;

	return strchr( pszText, '/' ) != NULL
		|| strchr( pszText, '\\' ) != NULL
		|| strchr( pszText, '.' ) != NULL;
}

//-----------------------------------------------------------------------------
// Purpose: Reads magic + version + header + file index.
//
// nRequiredContentMode 0: the documented layout - "requiredcontent" is a run of
//                         NUL-terminated strings ended by an empty one (which
//                         for the usual empty field is a single NUL byte).
// nRequiredContentMode 1: same field read as ONE string.  Some packers write a
//                         single string there with no list terminator; if mode 0
//                         mis-parses the header, mode 1 is tried.
// nAddonVersionMode:      whether the v3-only addonversion int32 is present.
// pbRequiredContentSuspicious: set when mode 0 swallowed a string that does not
//                         look like a content path (i.e. the list layout may be
//                         wrong for this archive).
//
// Everything is validated: a mis-parse shows up as a nonsensical first index
// entry (filename not printable, negative/huge size) and the parse fails.
//-----------------------------------------------------------------------------
static bool GMA_ParseHeaderAndIndex( CGMABufferedReader &reader, int nRequiredContentMode,
									 GMAAddonVersionMode_t nAddonVersionMode,
									 GMAHeader_t &header, CUtlVector< GMAFileEntry_t > &entries,
									 bool *pbRequiredContentSuspicious )
{
	char szMagic[ 4 ];
	if ( !reader.ReadBytes( szMagic, 4 ) || memcmp( szMagic, GMA_MAGIC, 4 ) != 0 )
		return false;

	unsigned char nVersion = 0;
	if ( !reader.ReadUInt8( nVersion ) )
		return false;
	if ( nVersion < GMA_MIN_VERSION || nVersion > GMA_MAX_VERSION )
		return false;

	header.m_nVersion = nVersion;
	header.m_nSteamID = 0;
	header.m_nTimestamp = 0;
	header.m_nAddonVersion = 0;

	// v1/v2 predate steamid + timestamp; only v3 carries them (and only v3 is
	// guaranteed to carry addonversion).
	if ( nVersion >= 3 )
	{
		if ( !reader.ReadUInt64( header.m_nSteamID ) )
			return false;
		if ( !reader.ReadUInt64( header.m_nTimestamp ) )
			return false;
	}

	char szSink[ GMA_MAX_STRING ];
	int nLength = 0;

	if ( nRequiredContentMode == 1 )
	{
		if ( !reader.ReadCString( szSink, sizeof( szSink ), GMA_MAX_STRING, &nLength ) )
			return false;
	}
	else
	{
		// requiredcontent may hold several NUL-separated strings; the list is
		// terminated by an empty one.  Bounded so a corrupt file cannot loop.
		const int nMaxRequiredStrings = 64;
		for ( int i = 0; i < nMaxRequiredStrings; ++i )
		{
			if ( !reader.ReadCString( szSink, sizeof( szSink ), GMA_MAX_STRING, &nLength ) )
				return false;

			if ( nLength == 0 )
				break;

			if ( pbRequiredContentSuspicious && !GMA_LooksLikeContentPath( szSink ) )
				*pbRequiredContentSuspicious = true;

			if ( i == nMaxRequiredStrings - 1 )
				return false;		// list never terminated
		}
	}

	char szName[ GMA_MAX_STRING ];
	char szDescription[ GMA_MAX_STRING ];
	char szAuthor[ GMA_MAX_STRING ];

	if ( !reader.ReadCString( szName, sizeof( szName ), GMA_MAX_STRING, &nLength ) )
		return false;
	if ( !reader.ReadCString( szDescription, sizeof( szDescription ), GMA_MAX_STRING, &nLength ) )
		return false;
	if ( !reader.ReadCString( szAuthor, sizeof( szAuthor ), GMA_MAX_STRING, &nLength ) )
		return false;

	header.m_Name = szName;
	header.m_Description = szDescription;
	header.m_Author = szAuthor;

	const bool bReadAddonVersion =
		( nAddonVersionMode == GMA_AV_READ ) ||
		( nAddonVersionMode == GMA_AV_DEFAULT && nVersion >= 3 );

	if ( bReadAddonVersion && !reader.ReadInt32( header.m_nAddonVersion ) )
		return false;

	// ---- file index ----
	while ( true )
	{
		uint32 nFileNumber = 0;
		if ( !reader.ReadUInt32( nFileNumber ) )
			return false;

		if ( nFileNumber == 0 )
			break;					// end of index

		char szEntryName[ GMA_MAX_ENTRY_NAME ];
		int nEntryLength = 0;
		if ( !reader.ReadCString( szEntryName, sizeof( szEntryName ), GMA_MAX_STRING, &nEntryLength ) )
			return false;

		int64 nEntrySize = 0;
		if ( !reader.ReadInt64( nEntrySize ) )
			return false;

		uint32 nEntryCRC = 0;
		if ( !reader.ReadUInt32( nEntryCRC ) )
			return false;

		if ( entries.Count() == 0 )
		{
			// The first entry is the archive-level sanity check: if the header
			// was mis-parsed (wrong requiredcontent layout, say) this is where
			// it shows up.
			if ( nEntryLength <= 0 || nEntryLength >= GMA_MAX_ENTRY_NAME )
				return false;
			if ( !GMA_IsPrintableName( szEntryName ) )
				return false;
			if ( nEntrySize < 0 || nEntrySize > GMA_MAX_FILE_SIZE )
				return false;
		}

		if ( entries.Count() >= GMA_MAX_ENTRIES )
			return false;

		GMAFileEntry_t &entry = entries[ entries.AddToTail() ];
		entry.m_Name = szEntryName;
		entry.m_nSize = nEntrySize;
		entry.m_nCRC = nEntryCRC;
	}

	header.m_nDataOffset = reader.Offset();
	return true;
}

// Tries every plausible header layout and keeps the first one that validates.
//
// The version byte is probed up front so the variants we try are ones that can
// actually apply (re-reading a v3 addonversion as if it were absent, or vice
// versa, can otherwise "validate" against garbage).
//
// Ordering matters.  A "suspicious" mode-0 parse - one that swallowed a
// requiredcontent string which does not look like a content path - is held back
// and only used if nothing else validates.  That is exactly what a single
// non-empty requiredcontent string produces: the mode-0 list loop keeps going
// past the field, eats the addon name, and can still land on a self-consistent
// (index, data) pair whose CRCs all match.  Letting mode 1 (single string) win
// in that case is what keeps the addon name and file paths correct.
static bool GMA_ParseArchive( FileHandle_t hFile, CGMABufferedReader &reader,
							  GMAHeader_t &header, CUtlVector< GMAFileEntry_t > &entries )
{
	// Probe: magic + version.
	unsigned char szProbe[ 5 ];
	int nProbeVersion = 0;

	filesystem->Seek( hFile, 0, FILESYSTEM_SEEK_HEAD );
	if ( filesystem->Read( szProbe, (int)sizeof( szProbe ), hFile ) == (int)sizeof( szProbe )
		&& memcmp( szProbe, GMA_MAGIC, 4 ) == 0 )
	{
		nProbeVersion = szProbe[4];
	}

	const bool bModern = ( nProbeVersion >= 3 && nProbeVersion <= GMA_MAX_VERSION );

	// requiredcontent layout x addonversion assumption.
	static const int s_nModernVariants[][2] =
	{
		{ 0, GMA_AV_DEFAULT },
		{ 1, GMA_AV_DEFAULT },
	};
	static const int s_nLegacyVariants[][2] =
	{
		{ 0, GMA_AV_SKIP },
		{ 1, GMA_AV_SKIP },
		{ 0, GMA_AV_READ },
		{ 1, GMA_AV_READ },
	};

	const int ( *pVariants )[2] = bModern ? s_nModernVariants : s_nLegacyVariants;
	const int nVariantCount = bModern
		? (int)( sizeof( s_nModernVariants ) / sizeof( s_nModernVariants[0] ) )
		: (int)( sizeof( s_nLegacyVariants ) / sizeof( s_nLegacyVariants[0] ) );

	int nSuspiciousVariant = -1;

	for ( int nAttempt = 0; nAttempt < 2; ++nAttempt )
	{
		for ( int i = 0; i < nVariantCount; ++i )
		{
			entries.RemoveAll();

			header.m_nVersion = 0;
			header.m_nSteamID = 0;
			header.m_nTimestamp = 0;
			header.m_Name = "";
			header.m_Description = "";
			header.m_Author = "";
			header.m_nAddonVersion = 0;
			header.m_nDataOffset = 0;

			bool bSuspicious = false;

			filesystem->Seek( hFile, 0, FILESYSTEM_SEEK_HEAD );
			reader.Begin( hFile );

			if ( !GMA_ParseHeaderAndIndex( reader, pVariants[i][0], (GMAAddonVersionMode_t)pVariants[i][1],
										   header, entries, &bSuspicious ) )
			{
				continue;
			}

			if ( nAttempt == 0 )
			{
				if ( bSuspicious )
				{
					if ( nSuspiciousVariant < 0 )
						nSuspiciousVariant = i;
					continue;			// prefer a layout that does not eat the name
				}
				return true;
			}

			if ( i == nSuspiciousVariant )
				return true;			// last resort: the only layout that parsed
		}

		if ( nAttempt == 0 && nSuspiciousVariant < 0 )
			break;						// nothing was held back
	}

	return false;
}

//-----------------------------------------------------------------------------
// Extraction helpers
//-----------------------------------------------------------------------------
static bool GMA_SkipBytes( FileHandle_t hFile, int64 nBytes )
{
	int64 nLeft = nBytes;
	while ( nLeft > 0 )
	{
		const int nChunk = (int)MIN( nLeft, (int64)GMA_IO_CHUNK );
		const int nRead = filesystem->Read( s_GMA_IoBuffer, nChunk, hFile );
		if ( nRead <= 0 )
			return false;

		nLeft -= nRead;
	}
	return true;
}

//-----------------------------------------------------------------------------
// Marker file: addons/<name>/.hl2sb_gma
//
// Records the archive it came from, its byte size, its last-write time and this
// layout's version.  When all four match, the folder on disk is up to date and
// the archive does not have to be unpacked again (which is what keeps startup
// fast with a handful of multi-megabyte addons).
//-----------------------------------------------------------------------------
static bool GMA_MarkerGetValue( const char *pszContent, const char *pszKey, char *pOut, int nOutSize )
{
	const int nKeyLength = V_strlen( pszKey );
	const char *p = pszContent;

	pOut[0] = 0;

	while ( p && *p )
	{
		const char *pEol = p;
		while ( *pEol && *pEol != '\n' && *pEol != '\r' )
			++pEol;

		if ( !V_strnicmp( p, pszKey, nKeyLength ) && p[ nKeyLength ] == '=' )
		{
			V_strncpy( pOut, p + nKeyLength + 1, nOutSize );

			char *pEnd = strpbrk( pOut, "\r\n" );
			if ( pEnd )
				*pEnd = 0;

			return true;
		}

		p = ( *pEol ) ? pEol + 1 : pEol;
	}
	return false;
}

static bool GMA_MarkerMatches( const char *pszMarkerPath, const char *pszArchiveFile,
							   int64 nArchiveSize, int64 nFileTime )
{
	if ( !filesystem->FileExists( pszMarkerPath, "MOD" ) )
		return false;

	FileHandle_t hFile = filesystem->Open( pszMarkerPath, "rb", "MOD" );
	if ( hFile == FILESYSTEM_INVALID_HANDLE )
		return false;

	char szBuffer[ 512 ];
	const int nRead = filesystem->Read( szBuffer, (int)sizeof( szBuffer ) - 1, hFile );
	filesystem->Close( hFile );

	if ( nRead <= 0 )
		return false;

	szBuffer[ nRead ] = 0;

	char szVersion[ 32 ];
	char szArchive[ MAX_PATH ];
	char szSize[ 64 ];
	char szTime[ 64 ];

	if ( !GMA_MarkerGetValue( szBuffer, "hl2sb_gma_version", szVersion, sizeof( szVersion ) ) )
		return false;
	if ( !GMA_MarkerGetValue( szBuffer, "archive", szArchive, sizeof( szArchive ) ) )
		return false;
	if ( !GMA_MarkerGetValue( szBuffer, "size", szSize, sizeof( szSize ) ) )
		return false;
	if ( !GMA_MarkerGetValue( szBuffer, "time", szTime, sizeof( szTime ) ) )
		return false;

	if ( V_atoi( szVersion ) != GMA_MARKER_VERSION )
		return false;
	if ( V_stricmp( szArchive, pszArchiveFile ) != 0 )
		return false;
	if ( V_atoi64( szSize ) != nArchiveSize )
		return false;
	if ( V_atoi64( szTime ) != nFileTime )
		return false;

	return true;
}

static void GMA_WriteMarker( const char *pszMarkerPath, const char *pszArchiveFile,
							 int64 nArchiveSize, int64 nFileTime )
{
	FileHandle_t hFile = filesystem->Open( pszMarkerPath, "wb", "MOD" );
	if ( hFile == FILESYSTEM_INVALID_HANDLE )
	{
		Warning( "[HL2SB] GMA: cannot write marker '%s'; the archive will be re-extracted next run\n", pszMarkerPath );
		return;
	}

	char szBuffer[ 512 ];
	Q_snprintf( szBuffer, sizeof( szBuffer ),
		"hl2sb_gma_version=%d\n"
		"archive=%s\n"
		"size=%lld\n"
		"time=%lld\n",
		GMA_MARKER_VERSION, pszArchiveFile, (long long)nArchiveSize, (long long)nFileTime );

	filesystem->Write( szBuffer, Q_strlen( szBuffer ), hFile );
	filesystem->Close( hFile );
}

//-----------------------------------------------------------------------------
// Purpose: Parses and (if needed) unpacks ONE archive, then mounts the folder.
//-----------------------------------------------------------------------------
static void GMA_ProcessArchive( const char *pszArchiveFile, int &nTotalFiles, int64 &nTotalBytes,
								int &nFailed, CUtlVector< CUtlString > &usedNames )
{
	char szArchiveRelative[ MAX_PATH ];
	Q_snprintf( szArchiveRelative, sizeof( szArchiveRelative ), "addons/%s", pszArchiveFile );

	const int64 nArchiveSize = (int64)filesystem->Size( szArchiveRelative, "MOD" );
	const int64 nFileTime = (int64)filesystem->GetFileTime( szArchiveRelative, "MOD" );

	FileHandle_t hFile = filesystem->Open( szArchiveRelative, "rb", "MOD" );
	if ( hFile == FILESYSTEM_INVALID_HANDLE )
	{
		Warning( "[HL2SB] GMA: cannot open '%s'; skipped\n", szArchiveRelative );
		++nFailed;
		return;
	}

	// Snapshot of the first bytes, so a malformed archive can name itself in the
	// log even after the parse has chewed through the file.
	unsigned char szHeader[ 32 ];
	int nHeaderBytes = filesystem->Read( szHeader, (int)sizeof( szHeader ), hFile );
	if ( nHeaderBytes < 0 )
		nHeaderBytes = 0;

	char szHeaderHex[ sizeof( szHeader ) * 3 + 4 ];
	GMA_FormatHex( szHeader, nHeaderBytes, szHeaderHex, sizeof( szHeaderHex ) );

	filesystem->Seek( hFile, 0, FILESYSTEM_SEEK_HEAD );

	CGMABufferedReader reader;
	GMAHeader_t header;
	CUtlVector< GMAFileEntry_t > entries;

	if ( !GMA_ParseArchive( hFile, reader, header, entries ) )
	{
		Warning( "[HL2SB] GMA: '%s' is malformed (not a readable GMA v1-v3 archive); skipped. first bytes: %s\n",
			szArchiveRelative, szHeaderHex );
		filesystem->Close( hFile );
		++nFailed;
		return;
	}

	// Sanity: every blob must actually be present in the file.
	int64 nDataNeeded = 0;
	for ( int i = 0; i < entries.Count(); ++i )
		nDataNeeded += entries[ i ].m_nSize;

	const int64 nDataAvailable = ( header.m_nDataOffset <= nArchiveSize )
		? ( nArchiveSize - header.m_nDataOffset )
		: 0;

	if ( nDataNeeded > nDataAvailable )
	{
		Warning( "[HL2SB] GMA: '%s' is truncated (%lld byte(s) of file data expected, %lld available); skipped. first bytes: %s\n",
			szArchiveRelative, (long long)nDataNeeded, (long long)nDataAvailable, szHeaderHex );
		filesystem->Close( hFile );
		++nFailed;
		return;
	}

	if ( header.m_nDataOffset > GMA_MAX_IO_OFFSET )
	{
		Warning( "[HL2SB] GMA: '%s' is too large to unpack with this build (data starts at %lld); skipped\n",
			szArchiveRelative, (long long)header.m_nDataOffset );
		filesystem->Close( hFile );
		++nFailed;
		return;
	}

	// ---- folder name ----
	char szFolderName[ 128 ];
	GMA_SanitizeFolderName( header.m_Name.String(), szFolderName, sizeof( szFolderName ) );
	if ( !szFolderName[0] )
		GMA_FolderNameFromArchiveFile( pszArchiveFile, szFolderName, sizeof( szFolderName ) );
	if ( !szFolderName[0] )
		Q_strncpy( szFolderName, "unnamed_addon", sizeof( szFolderName ) );

	// Two archives may claim the same name; keep them in separate folders.
	bool bNameTaken = false;
	for ( int i = 0; i < usedNames.Count(); ++i )
	{
		if ( !V_stricmp( usedNames[ i ].String(), szFolderName ) )
		{
			bNameTaken = true;
			break;
		}
	}

	if ( bNameTaken )
	{
		char szUnique[ 160 ];
		for ( int n = 2; n < 1000; ++n )
		{
			Q_snprintf( szUnique, sizeof( szUnique ), "%s_%d", szFolderName, n );

			bool bInUse = false;
			for ( int i = 0; i < usedNames.Count(); ++i )
			{
				if ( !V_stricmp( usedNames[ i ].String(), szUnique ) )
				{
					bInUse = true;
					break;
				}
			}

			if ( !bInUse )
			{
				Q_strncpy( szFolderName, szUnique, sizeof( szFolderName ) );
				break;
			}
		}
	}

	CUtlString &usedName = usedNames[ usedNames.AddToTail() ];
	usedName = szFolderName;

	char szFolderRelative[ MAX_PATH ];
	Q_snprintf( szFolderRelative, sizeof( szFolderRelative ), "addons/%s", szFolderName );

	char szMarkerRelative[ MAX_PATH ];
	Q_snprintf( szMarkerRelative, sizeof( szMarkerRelative ), "%s/" GMA_MARKER_FILE, szFolderRelative );

	// ---- extract (unless the marker says this folder is current) ----
	const bool bAlreadyExtracted = GMA_MarkerMatches( szMarkerRelative, pszArchiveFile, nArchiveSize, nFileTime );

	int nWrittenFiles = 0;
	int nSkippedEntries = 0;
	int nCRCMismatches = 0;

	if ( !bAlreadyExtracted )
	{
		filesystem->Seek( hFile, (int)header.m_nDataOffset, FILESYSTEM_SEEK_HEAD );

		char szLastDir[ MAX_PATH ] = { 0 };
		bool bInputTruncated = false;

		for ( int i = 0; i < entries.Count(); ++i )
		{
			const GMAFileEntry_t &entry = entries[ i ];

			// Security gate.  The blob still has to be consumed to keep the
			// stream aligned with the index.
			if ( !GMA_IsSafeEntryPath( entry.m_Name.String() ) )
			{
				if ( nSkippedEntries < GMA_MAX_LOG_SKIPS )
				{
					Warning( "[HL2SB] GMA: '%s': unsafe entry path '%s'; skipped\n",
						szArchiveRelative, entry.m_Name.String() );
				}
				else if ( nSkippedEntries == GMA_MAX_LOG_SKIPS )
				{
					Warning( "[HL2SB] GMA: '%s': more unsafe entry paths follow; not logging the rest\n",
						szArchiveRelative );
				}

				++nSkippedEntries;

				if ( !GMA_SkipBytes( hFile, entry.m_nSize ) )
				{
					bInputTruncated = true;
					break;
				}
				continue;
			}

			char szEntryName[ GMA_MAX_ENTRY_NAME ];
			Q_strncpy( szEntryName, entry.m_Name.String(), sizeof( szEntryName ) );
			GMA_NormalizeSlashes( szEntryName );

			char szOutRelative[ MAX_PATH ];
			Q_snprintf( szOutRelative, sizeof( szOutRelative ), "%s/%s", szFolderRelative, szEntryName );

			if ( Q_strlen( szOutRelative ) >= MAX_PATH - 1 )
			{
				Warning( "[HL2SB] GMA: '%s': entry path too long ('%s'); skipped\n",
					szArchiveRelative, szEntryName );
				++nSkippedEntries;

				if ( !GMA_SkipBytes( hFile, entry.m_nSize ) )
				{
					bInputTruncated = true;
					break;
				}
				continue;
			}

			char szOutDir[ MAX_PATH ];
			Q_strncpy( szOutDir, szOutRelative, sizeof( szOutDir ) );
			GMA_StripFileName( szOutDir );

			if ( V_stricmp( szOutDir, szLastDir ) != 0 )
			{
				filesystem->CreateDirHierarchy( szOutDir, "MOD" );
				Q_strncpy( szLastDir, szOutDir, sizeof( szLastDir ) );
			}

			FileHandle_t hOut = filesystem->Open( szOutRelative, "wb", "MOD" );
			if ( hOut == FILESYSTEM_INVALID_HANDLE )
			{
				Warning( "[HL2SB] GMA: '%s': cannot write '%s'; skipped\n", szArchiveRelative, szOutRelative );
				++nSkippedEntries;

				if ( !GMA_SkipBytes( hFile, entry.m_nSize ) )
				{
					bInputTruncated = true;
					break;
				}
				continue;
			}

			// Stream the blob to disk in chunks, verifying the index CRC as we go.
			CRC32_t crc;
			CRC32_Init( &crc );

			int64 nLeft = entry.m_nSize;
			bool bShortRead = false;

			while ( nLeft > 0 )
			{
				const int nChunk = (int)MIN( nLeft, (int64)GMA_IO_CHUNK );
				const int nRead = filesystem->Read( s_GMA_IoBuffer, nChunk, hFile );
				if ( nRead <= 0 )
				{
					bShortRead = true;
					break;
				}

				CRC32_ProcessBuffer( &crc, s_GMA_IoBuffer, nRead );
				filesystem->Write( s_GMA_IoBuffer, nRead, hOut );
				nLeft -= nRead;
			}

			filesystem->Close( hOut );

			if ( bShortRead )
			{
				bInputTruncated = true;
				break;
			}

			CRC32_Final( &crc );

			if ( crc != entry.m_nCRC )
			{
				++nCRCMismatches;
				if ( nCRCMismatches <= GMA_MAX_LOG_CRC )
				{
					Warning( "[HL2SB] GMA: '%s': CRC mismatch on '%s' (index %08X, unpacked %08X); file kept\n",
						szArchiveRelative, szEntryName, entry.m_nCRC, (unsigned)crc );
				}
				else if ( nCRCMismatches == GMA_MAX_LOG_CRC + 1 )
				{
					Warning( "[HL2SB] GMA: '%s': more CRC mismatches follow; not logging the rest\n",
						szArchiveRelative );
				}
			}

			++nWrittenFiles;
		}

		if ( bInputTruncated )
		{
			Warning( "[HL2SB] GMA: '%s' ended while reading file data; partial extraction left in '%s' and no marker written. first bytes: %s\n",
				szArchiveRelative, szFolderRelative, szHeaderHex );
			filesystem->Close( hFile );
			++nFailed;
			return;
		}

		// Only stamp the marker when something actually landed on disk: an
		// archive that yielded nothing must be retried next run, and its folder
		// does not even exist yet.
		if ( nWrittenFiles > 0 )
			GMA_WriteMarker( szMarkerRelative, pszArchiveFile, nArchiveSize, nFileTime );
	}

	filesystem->Close( hFile );

	if ( nSkippedEntries )
		Msg( "[HL2SB] GMA: '%s': %d entry(ies) skipped\n", szArchiveRelative, nSkippedEntries );

	// An archive that yielded nothing at all (empty index, or every entry
	// refused) must not get a folder or a search path of its own.
	const bool bNothingExtracted = ( !bAlreadyExtracted && nWrittenFiles == 0 );

	// ---- mount ----
	// Resolving the marker gives us the real absolute folder, and the folder can
	// only ever be addons/<sanitized>/ because the marker lives there.
	char szFolderFull[ MAX_PATH ] = { 0 };
	if ( !bNothingExtracted )
		filesystem->RelativePathToFullPath( szMarkerRelative, "MOD", szFolderFull, sizeof( szFolderFull ) );
	GMA_StripFileName( szFolderFull );

	bool bMounted = false;
	if ( szFolderFull[0] )
	{
		filesystem->AddSearchPath( szFolderFull, "MOD" );
		filesystem->AddSearchPath( szFolderFull, "GAME" );

		// Runtime-added search paths ARE walked by FindFirstEx() - it reads
		// m_SearchPaths on every call (CBaseFileSystem::FindFirstHelper) and
		// filters on the path ID, which is why the Lua weapon/entity loaders
		// pick this up in the same session.  No restart is needed.
		bMounted = true;

		Msg( "[HL2SB] GMA: mounted '%s' on the MOD + GAME search paths\n", szFolderFull );
	}
	else if ( bNothingExtracted )
	{
		Msg( "[HL2SB] GMA: '%s' yielded no files; nothing extracted or mounted\n", szArchiveRelative );
	}
	else
	{
		Warning( "[HL2SB] GMA: could not resolve the absolute path of '%s'; a restart is required to load it\n",
			szFolderRelative );
	}

	Msg( "[HL2SB] addon '%s' <- addons/%s: %d files, %.1f MB, %s\n",
		szFolderName, pszArchiveFile, entries.Count(),
		(double)nArchiveSize / ( 1024.0 * 1024.0 ),
		bAlreadyExtracted ? "already extracted" : "extracted" );

	if ( !bMounted && !bNothingExtracted )
		Msg( "[HL2SB] GMA: '%s': restart required to load it\n", szArchiveRelative );

	nTotalFiles += bAlreadyExtracted ? entries.Count() : nWrittenFiles;
	nTotalBytes += nArchiveSize;
}

// Sorting the archive list keeps folder-name de-duplication deterministic
// across runs (two addons may share the same header name).
static int GMA_SortArchiveNames( const CUtlString *pLeft, const CUtlString *pRight )
{
	return V_stricmp( pLeft->String(), pRight->String() );
}

//-----------------------------------------------------------------------------
// Purpose: Finds every addons/*.gma and makes it usable as a mounted addon
//          folder.  Called by both realms before their Lua passes.
//-----------------------------------------------------------------------------
void HL2SB_MountGMAAddons()
{
	if ( !filesystem )
		return;

	// Collect the names first: extracting while a find handle sits on the same
	// folder would be asking for trouble.
	CUtlVector< CUtlString > archives;

	FileFindHandle_t hFind = FILESYSTEM_INVALID_FIND_HANDLE;
	const char *pszFound = filesystem->FindFirstEx( "addons/*.gma", "MOD", &hFind );

	while ( pszFound )
	{
		if ( !filesystem->FindIsDirectory( hFind ) && pszFound[0] != '.' )
		{
			const int nLength = Q_strlen( pszFound );
			if ( nLength > 4 && !V_stricmp( pszFound + nLength - 4, ".gma" ) )
			{
				// HL2SB: honour the main-menu Addons dialog.  The extracted FOLDER
				// is what the game mounts, and MountAddons() already skips a
				// disabled folder -- but an archive that is switched off should not
				// be parsed or extracted again either.
				char szArchiveBase[ 256 ];
				Q_strncpy( szArchiveBase, pszFound, sizeof( szArchiveBase ) );
				if ( nLength - 4 < (int)sizeof( szArchiveBase ) )
				{
					szArchiveBase[ nLength - 4 ] = '\0';

					if ( HL2SB_IsAddonDisabled( szArchiveBase ) )
					{
						Msg( "[HL2SB] GMA: '%s' disabled by addons_disabled.txt - skipped\n", pszFound );
						pszFound = filesystem->FindNext( hFind );
						continue;
					}
				}

				CUtlString &archive = archives[ archives.AddToTail() ];
				archive = pszFound;
			}
		}

		pszFound = filesystem->FindNext( hFind );
	}

	if ( hFind != FILESYSTEM_INVALID_FIND_HANDLE )
		filesystem->FindClose( hFind );

	// The verification signal: this line must appear exactly once per realm.
	Msg( "[HL2SB] GMA: found %d archive(s) in addons/\n", archives.Count() );

	if ( archives.Count() == 0 )
		return;

	archives.Sort( GMA_SortArchiveNames );

	CUtlVector< CUtlString > usedNames;

	int nTotalFiles = 0;
	int64 nTotalBytes = 0;
	int nFailed = 0;

	for ( int i = 0; i < archives.Count(); ++i )
	{
		GMA_ProcessArchive( archives[ i ].String(), nTotalFiles, nTotalBytes, nFailed, usedNames );
	}

	Msg( "[HL2SB] GMA: total %d archive(s), %d file(s), %.1f MB from addons/",
		archives.Count(), nTotalFiles, (double)nTotalBytes / ( 1024.0 * 1024.0 ) );

	if ( nFailed )
		Msg( " (%d skipped)\n", nFailed );
	else
		Msg( "\n" );
}
