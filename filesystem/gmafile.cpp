//========= Copyright Valve Corporation, All rights reserved. ============//
//
// Purpose: Read-only in-place mounting of Garry's Mod .gma addon archives.
//
//          A .gma is a flat (uncompressed) archive: a small header, a file
//          index, then the raw bytes of every indexed file in index order.
//          Mounting adds a CPackFile-backed search path, so files inside the
//          archive resolve through the normal filesystem machinery and are
//          read on demand straight out of the .gma - nothing is extracted.
//
//===========================================================================//

#include "basefilesystem.h"
#include "packfile.h"

#include "tier0/basetypes.h"
#include "tier1/generichash.h"

// memdbgon must be the last include file in a .cpp file!!!
#include "tier0/memdbgon.h"

#define GMA_MAGIC			"GMAD"
#define GMA_MIN_VERSION		1
#define GMA_MAX_VERSION		3
#define GMA_MAX_STRING		4096		// hard cap on any header/index string
#define GMA_MAX_ENTRY_NAME	512
#define GMA_MAX_ENTRIES		( 256 * 1024 )
#define GMA_MAX_FILE_SIZE	0xFFFFFFF0ll	// handles carry 32-bit lengths
#define GMA_MAX_REQUIRED	64			// bound on the requiredcontent list

// Header layout variants tried in order.  "rc" is how the requiredcontent
// field is read: 0 = NUL-separated list ended by an empty string (the
// documented layout), 1 = one single string (some packers omit the
// terminator).  "av" is whether the v3-only addonversion int32 is assumed
// present (1), absent (0), or decided by the archive version (2).
struct GMA_Variant_t
{
	int nRequiredContentMode;
	int nAddonVersionMode;
};

static const GMA_Variant_t s_GmaVariants[] =
{
	{ 0, 2 },
	{ 1, 2 },
	{ 0, 0 },
	{ 1, 0 },
	{ 0, 1 },
	{ 1, 1 },
};

// A printable, non-empty name we are willing to key the directory on.
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

// Security gate for an entry path read out of the archive: rejects absolute
// paths, drive letters / NTFS streams, ".." (including the ".. " / "..."
// spellings Windows normalizes), empty components and characters Windows
// cannot create, so an entry can never escape its archive.
static bool GMA_IsSafeEntryPath( const char *pszPath )
{
	if ( !pszPath || !pszPath[0] )
		return false;

	if ( pszPath[0] == '/' || pszPath[0] == '\\' )
		return false;

	if ( strchr( pszPath, ':' ) != NULL )
		return false;

	const int nLength = Q_strlen( pszPath );
	if ( nLength <= 0 || pszPath[ nLength - 1 ] == '/' || pszPath[ nLength - 1 ] == '\\' )
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

		int nTrimmed = nComponent;
		while ( nTrimmed > 0 && ( p[ nTrimmed - 1 ] == ' ' || p[ nTrimmed - 1 ] == '.' ) )
			--nTrimmed;

		if ( nTrimmed != nComponent )
			return false;

		if ( ( nComponent == 1 && p[0] == '.' )
			|| ( nComponent == 2 && p[0] == '.' && p[1] == '.' ) )
			return false;

		for ( int i = 0; i < nComponent; ++i )
		{
			if ( p[i] == '*' || p[i] == '?' || p[i] == '"' || p[i] == '<' || p[i] == '>' || p[i] == '|' )
				return false;
		}

		p = ( *pEnd ) ? pEnd + 1 : pEnd;
	}
	return true;
}

// True when a requiredcontent string does not look like a content path - a
// sign that the single-string layout was misread as the list layout (which
// also swallows the addon name).
static bool GMA_SuspiciousContent( const char *pszText )
{
	if ( !pszText || !pszText[0] )
		return false;

	return strchr( pszText, '/' ) == NULL
		&& strchr( pszText, '\\' ) == NULL
		&& strchr( pszText, '.' ) == NULL;
}

// Normalize an entry path in place: backslashes to forward slashes.
static void GMA_NormalizeSlashes( char *pszPath )
{
	for ( char *p = pszPath; *p; ++p )
	{
		if ( *p == '\\' )
			*p = '/';
	}
}

//-----------------------------------------------------------------------------
// Buffered sequential reader over the (already open) archive FILE*.
// Tracks the absolute offset so the caller can find where the data section
// begins, and always consumes what it reads, so a hostile length cannot
// desync the parse - it can only make it fail.
//-----------------------------------------------------------------------------
class CGmaReader
{
public:
	CGmaReader( CBaseFileSystem *fs, FILE *fp ) : m_fs( fs ), m_fp( fp )
	{
		Reset();
	}

	void Reset()
	{
		m_nBufferPos = 0;
		m_nBufferEnd = 0;
		m_nOffset = 0;
	}

	int64 Offset() const { return m_nOffset; }

	bool ReadBytes( void *pDest, int nCount )
	{
		unsigned char *pOut = (unsigned char *)pDest;
		int nDone = 0;

		while ( nDone < nCount )
		{
			if ( m_nBufferPos >= m_nBufferEnd )
			{
				m_nBufferPos = 0;
				m_nBufferEnd = (int)m_fs->FS_fread( m_Buffer, 1, (int)sizeof( m_Buffer ), m_fp );
				if ( m_nBufferEnd <= 0 )
					return false;
			}

			const int nAvailable = m_nBufferEnd - m_nBufferPos;
			const int nTake = MIN( nAvailable, nCount - nDone );
			memcpy( pOut + nDone, m_Buffer + m_nBufferPos, nTake );

			m_nBufferPos += nTake;
			m_nOffset += nTake;
			nDone += nTake;
		}
		return true;
	}

	bool ReadUInt8( unsigned char &nOut ) { return ReadBytes( &nOut, 1 ); }

	bool ReadUInt32( uint32 &nOut )
	{
		unsigned char bytes[4];
		if ( !ReadBytes( bytes, 4 ) )
			return false;
		nOut = (uint32)bytes[0] | ( (uint32)bytes[1] << 8 ) | ( (uint32)bytes[2] << 16 ) | ( (uint32)bytes[3] << 24 );
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

	// Reads a NUL-terminated string, always consuming the terminator.
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
	CBaseFileSystem *m_fs;
	FILE *m_fp;
	int m_nBufferPos;
	int m_nBufferEnd;
	int64 m_nOffset;
	unsigned char m_Buffer[ 8 * 1024 ];
};

//-----------------------------------------------------------------------------
// Parse header + index with one layout variant.  Returns false when the parse
// is self-inconsistent (magic, versions, oversize fields, bogus first entry).
// pbSuspicious is set when the requiredcontent list swallowed a string that
// does not look like a content path - such a parse is only a last resort.
// On success nDataOffset is where the raw file data begins.
//-----------------------------------------------------------------------------
static bool GMA_ParseHeaderAndIndex( CGmaReader &reader, const GMA_Variant_t &variant,
									 int64 &nDataOffset, char *pszName, int nNameSize,
									 CUtlVector< CGmaPackFile::CGmaFileEntry > &entries,
									 bool *pbSuspicious )
{
	char szMagic[ 4 ];
	if ( !reader.ReadBytes( szMagic, 4 ) || memcmp( szMagic, GMA_MAGIC, 4 ) != 0 )
		return false;

	unsigned char nVersion = 0;
	if ( !reader.ReadUInt8( nVersion ) )
		return false;
	if ( nVersion < GMA_MIN_VERSION || nVersion > GMA_MAX_VERSION )
		return false;

	if ( nVersion >= 3 )
	{
		// steamid + timestamp
		uint64 nDiscard = 0;
		if ( !reader.ReadUInt64( nDiscard ) || !reader.ReadUInt64( nDiscard ) )
			return false;
	}

	char szSink[ GMA_MAX_STRING ];
	int nLength = 0;

	if ( variant.nRequiredContentMode == 1 )
	{
		if ( !reader.ReadCString( szSink, sizeof( szSink ), GMA_MAX_STRING, &nLength ) )
			return false;

		if ( pbSuspicious && GMA_SuspiciousContent( szSink ) )
			*pbSuspicious = true;
	}
	else
	{
		for ( int i = 0; i < GMA_MAX_REQUIRED; ++i )
		{
			if ( !reader.ReadCString( szSink, sizeof( szSink ), GMA_MAX_STRING, &nLength ) )
				return false;

			if ( nLength == 0 )
				break;

			if ( pbSuspicious && GMA_SuspiciousContent( szSink ) )
				*pbSuspicious = true;

			if ( i == GMA_MAX_REQUIRED - 1 )
				return false;		// list never terminated
		}
	}

	char szDescription[ GMA_MAX_STRING ];
	char szAuthor[ GMA_MAX_STRING ];

	if ( !reader.ReadCString( pszName, nNameSize, GMA_MAX_STRING, &nLength ) )
		return false;
	if ( !reader.ReadCString( szDescription, sizeof( szDescription ), GMA_MAX_STRING, &nLength ) )
		return false;
	if ( !reader.ReadCString( szAuthor, sizeof( szAuthor ), GMA_MAX_STRING, &nLength ) )
		return false;

	const bool bReadAddonVersion =
		( variant.nAddonVersionMode == 1 ) ||
		( variant.nAddonVersionMode == 2 && nVersion >= 3 );

	if ( bReadAddonVersion )
	{
		int32 nAddonVersion = 0;
		if ( !reader.ReadInt32( nAddonVersion ) )
			return false;
	}

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
			// Archive-level sanity check: a mis-parsed header lands here.
			if ( nEntryLength <= 0 || nEntryLength >= GMA_MAX_ENTRY_NAME )
				return false;
			if ( !GMA_IsPrintableName( szEntryName ) )
				return false;
			if ( nEntrySize < 0 || nEntrySize > GMA_MAX_FILE_SIZE )
				return false;
		}

		if ( entries.Count() >= GMA_MAX_ENTRIES )
			return false;

		if ( !GMA_IsSafeEntryPath( szEntryName ) )
			continue;			// the whole blob was consumed above, stream stays aligned
		if ( nEntrySize < 0 || nEntrySize >= GMA_MAX_FILE_SIZE )
			continue;

		CGmaPackFile::CGmaFileEntry &entry = entries[ entries.AddToTail() ];
		char szNormalized[ GMA_MAX_ENTRY_NAME ];
		Q_strncpy( szNormalized, szEntryName, sizeof( szNormalized ) );
		GMA_NormalizeSlashes( szNormalized );

		entry.m_Name = szNormalized;
		entry.m_nOriginalSize = (unsigned int)nEntrySize;
		entry.m_HashName = HashStringCaselessConventional( entry.m_Name.String() );
	}

	nDataOffset = reader.Offset();
	return true;
}

//-----------------------------------------------------------------------------
// CGmaPackFile
//-----------------------------------------------------------------------------

CGmaPackFile::CGmaPackFile( CBaseFileSystem* fs ) : CPackFile()
{
	m_fs = fs;
	m_szName[0] = 0;
}

CGmaPackFile::~CGmaPackFile()
{
}

bool CGmaPackFile::CGmaFileLessFunc::Less( CGmaFileEntry const& src1, CGmaFileEntry const& src2, void *pCtx )
{
	return ( src1.m_HashName < src2.m_HashName );
}

//-----------------------------------------------------------------------------
//	Parse the archive and build the file directory with absolute offsets.
//-----------------------------------------------------------------------------
bool CGmaPackFile::Prepare( int64 fileLen, int64 nFileOfs )
{
	if ( !m_hPackFileHandleFS || fileLen < 16 )
		return false;

	m_FileLength = fileLen;
	m_nBaseOffset = nFileOfs;

	CGmaReader reader( m_fs, m_hPackFileHandleFS );

	char szName[ sizeof( m_szName ) ];
	int64 nBestDataOffset = -1;
	int nBestVariant = -1;
	int nFallbackVariant = -1;

	const int nVariantCount = (int)( sizeof( s_GmaVariants ) / sizeof( s_GmaVariants[0] ) );

	// First pass: find the layout variant that validates without suspicion.
	for ( int i = 0; i < nVariantCount; ++i )
	{
		CUtlVector< CGmaFileEntry > rawEntries;
		szName[0] = 0;

		m_fs->FS_fseek( m_hPackFileHandleFS, m_nBaseOffset, FILESYSTEM_SEEK_HEAD );
		reader.Reset();

		bool bSuspicious = false;
		int64 nDataOffset = 0;

		if ( !GMA_ParseHeaderAndIndex( reader, s_GmaVariants[i], nDataOffset, szName, (int)sizeof( szName ), rawEntries, &bSuspicious ) )
			continue;

		if ( nDataOffset > fileLen || rawEntries.Count() == 0 )
			continue;

		// every blob must actually be present in the file
		int64 nDataNeeded = 0;
		bool bOverflow = false;
		for ( int j = 0; j < rawEntries.Count(); ++j )
		{
			nDataNeeded += rawEntries[ j ].m_nOriginalSize;
			if ( nDataNeeded > fileLen - nDataOffset )
			{
				bOverflow = true;
				break;
			}
		}
		if ( bOverflow )
			continue;

		if ( bSuspicious )
		{
			if ( nFallbackVariant < 0 )
			{
				nFallbackVariant = i;
				nBestDataOffset = nDataOffset;
				Q_strncpy( m_szName, szName, sizeof( m_szName ) );
			}
			continue;			// prefer a layout that did not eat the addon name
		}

		nBestVariant = i;
		nBestDataOffset = nDataOffset;
		Q_strncpy( m_szName, szName, sizeof( m_szName ) );
		break;
	}

	if ( nBestVariant < 0 && nFallbackVariant < 0 )
		return false;

	// Second pass: parse the chosen variant and keep its entries.
	CUtlVector< CGmaFileEntry > rawEntries;
	szName[0] = 0;

	m_fs->FS_fseek( m_hPackFileHandleFS, m_nBaseOffset, FILESYSTEM_SEEK_HEAD );
	reader.Reset();

	int64 nDataOffset = 0;
	if ( !GMA_ParseHeaderAndIndex( reader, s_GmaVariants[ ( nBestVariant >= 0 ) ? nBestVariant : nFallbackVariant ],
								   nDataOffset, szName, (int)sizeof( szName ), rawEntries, NULL ) )
	{
		return false;
	}

	if ( nBestVariant < 0 )
	{
		nBestDataOffset = nDataOffset;
		Q_strncpy( m_szName, szName, sizeof( m_szName ) );
	}

	// absolute positions: data blobs follow the index in index order
	int64 nOffset = nBestDataOffset;
	m_PackFiles.EnsureCapacity( rawEntries.Count() );
	for ( int i = 0; i < rawEntries.Count(); ++i )
	{
		CGmaFileEntry entry;
		entry.m_Name = rawEntries[ i ].m_Name;
		entry.m_nPosition = nOffset;
		entry.m_nOriginalSize = rawEntries[ i ].m_nOriginalSize;
		entry.m_HashName = rawEntries[ i ].m_HashName;
		m_PackFiles.Insert( entry );
		nOffset += entry.m_nOriginalSize;
	}

	return true;
}

//-----------------------------------------------------------------------------
//	Find a file in the archive.
//-----------------------------------------------------------------------------
const CGmaPackFile::CGmaFileEntry* CGmaPackFile::FindEntry( const char *pFileName )
{
	char szFixedName[ MAX_PATH ] = { 0 };
	V_strcpy_safe( szFixedName, pFileName );
	V_RemoveDotSlashes( szFixedName, '/', true );
	GMA_NormalizeSlashes( szFixedName );

	if ( !szFixedName[0] )
		return NULL;

	CGmaFileEntry lookup;
	lookup.m_HashName = HashStringCaselessConventional( szFixedName );

	int idx = m_PackFiles.Find( lookup );
	if ( idx == m_PackFiles.InvalidIndex() )
		return NULL;

	// guard against hash collisions on the name itself
	if ( m_PackFiles[ idx ].m_HashName != lookup.m_HashName ||
		 V_stricmp( m_PackFiles[ idx ].m_Name.String(), szFixedName ) != 0 )
	{
		return NULL;
	}

	return &m_PackFiles[ idx ];
}

bool CGmaPackFile::ContainsFile( const char *pFileName )
{
	return FindEntry( pFileName ) != NULL;
}

//-----------------------------------------------------------------------------
//	Open a file inside the archive.
//-----------------------------------------------------------------------------
CFileHandle *CGmaPackFile::OpenFile( const char *pFileName, const char *pOptions )
{
	const CGmaFileEntry *pEntry = FindEntry( pFileName );
	if ( !pEntry )
		return NULL;

	m_mutex.Lock();
	if ( m_nOpenFiles == 0 && m_hPackFileHandleFS == NULL )
	{
		m_hPackFileHandleFS = m_fs->Trace_FOpen( m_ZipName, "rb", 0, NULL );
	}
	m_nOpenFiles++;
	m_mutex.Unlock();

	CPackFileHandle* ph = new CGmaPackFileHandle( this, pEntry->m_nPosition, pEntry->m_nOriginalSize );

	CFileHandle *fh = new CFileHandle( m_fs );
	fh->m_pPackFileHandle = ph;
	fh->m_nLength = pEntry->m_nOriginalSize;

	// The default mode for fopen is text, so require 'b' for binary
	if ( strstr( pOptions, "b" ) == NULL )
	{
		fh->m_type = FT_PACK_TEXT;
	}
	else
	{
		fh->m_type = FT_PACK_BINARY;
	}

#if !defined( _RETAIL )
	fh->SetName( pFileName );
#endif
	return fh;
}

//-----------------------------------------------------------------------------
//	Read bytes out of the archive (the one core IO primitive).
//-----------------------------------------------------------------------------
int CGmaPackFile::ReadFromPack( int nIndex, void* pBuffer, int nDestBytes, int nBytes, int64 nOffset )
{
	if ( nBytes <= 0 )
		return 0;

	m_mutex.Lock();

	int nBytesRead = 0;
	if ( m_hPackFileHandleFS )
	{
		m_fs->FS_fseek( m_hPackFileHandleFS, m_nBaseOffset + nOffset, SEEK_SET );
		nBytesRead = (int)m_fs->FS_fread( pBuffer, 1, nBytes, m_hPackFileHandleFS );
	}

	m_mutex.Unlock();

	return nBytesRead;
}

bool CGmaPackFile::IndexToFilename( int nIndex, char *pBuffer, int nBufferSize )
{
	if ( nIndex >= 0 && nIndex < m_PackFiles.Count() )
	{
		Q_strncpy( pBuffer, m_PackFiles[ nIndex ].m_Name.String(), nBufferSize );
		return true;
	}

	Q_strncpy( pBuffer, "unknown", nBufferSize );
	return false;
}

//-----------------------------------------------------------------------------
//	Build a list of matching files and directories for FindFirst().
//	Same algorithm as CZipPackFile (archives carry no empty directories;
//	directories are derived from entry paths).
//-----------------------------------------------------------------------------
void CGmaPackFile::GetFileAndDirLists( const char *pRawWildCard, CUtlStringList &outDirnames, CUtlStringList &outFilenames, bool bSortedOutput )
{
	char szWildCard[MAX_PATH] = { 0 };
	char szWildCardPath[MAX_PATH] = { 0 };
	char szWildCardBase[MAX_PATH] = { 0 };
	char szWildCardExt[MAX_PATH] = { 0 };

	size_t nLenWildcardPath = 0;
	size_t nLenWildcardBase = 0;

	bool bBaseWildcard = true;
	bool bExtWildcard = true;

	V_strncpy( szWildCard, pRawWildCard, sizeof( szWildCard ) );
	V_FixSlashes( szWildCard, '/' );
	V_RemoveDotSlashes( szWildCard, '/', true );

	size_t nLenWildCard = V_strlen( szWildCard );
	if ( nLenWildCard && szWildCard[ nLenWildCard - 1 ] == '/' )
	{
		V_strncpy( szWildCardPath, szWildCard, sizeof( szWildCardPath ) );
	}
	else
	{
		V_ExtractFilePath( szWildCard, szWildCardPath, sizeof( szWildCardPath ) );
	}

	V_FileBase( szWildCard, szWildCardBase, sizeof( szWildCardBase ) );
	bool bWildcardHasExt = !!V_strrchr( szWildCard, '.' );
	V_ExtractFileExtension( szWildCard, szWildCardExt, sizeof( szWildCardExt ) );

	// No partial wildcards (foo*bar.*); same rule as the zip implementation.
	bBaseWildcard = ( V_strcmp( szWildCardBase, "*" ) == 0 );
	bExtWildcard = ( V_strcmp( szWildCardExt, "*" ) == 0 );

	if ( !bWildcardHasExt && bBaseWildcard )
	{
		// For the special case of just '*' (and not, e.g., '*.') match '*.*'
		bExtWildcard = true;
	}

	nLenWildcardPath = V_strlen( szWildCardPath );
	nLenWildcardBase = V_strlen( szWildCardBase );

	FOR_EACH_VEC( m_PackFiles, filesIdx )
	{
		char szCandidateName[MAX_PATH] = { 0 };
		IndexToFilename( filesIdx, szCandidateName, sizeof( szCandidateName ) );

		if ( !szCandidateName[0] )
			continue;

		// Check if this file starts with the wildcard selector's path.
		CUtlDict<int,int> ConsideredDirectories;
		if  ( ( nLenWildcardPath && ( 0 == V_strnicmp( szCandidateName, szWildCardPath, (int)nLenWildcardPath ) ) )
		      || ( !nLenWildcardPath && strchr( szCandidateName, '/' ) ) )
		{
			char szCandidateBaseName[MAX_PATH] = { 0 };
			bool bIsDir = false;
			size_t nSubDirLen = 0;
			char *pSubDirSlash = strchr( szCandidateName + nLenWildcardPath, '/' );
			if ( pSubDirSlash )
			{
				// Subdirectory match: only the first directory level under the
				// wildcard path is a candidate, and it is reported as a dir.
				nSubDirLen = (size_t)( (ptrdiff_t)pSubDirSlash - (ptrdiff_t)( szCandidateName + nLenWildcardPath ) );
				V_strncpy( szCandidateBaseName, szCandidateName + nLenWildcardPath, (int)nSubDirLen + 1 );
				bIsDir = true;

				if ( ConsideredDirectories.Find( szCandidateBaseName ) != ConsideredDirectories.InvalidIndex() )
					continue;

				ConsideredDirectories.Insert( szCandidateBaseName, 0 );
			}
			else
			{
				V_strncpy( szCandidateBaseName, szCandidateName + nLenWildcardPath, sizeof( szCandidateBaseName ) );
			}

			char *pExt = strchr( szCandidateBaseName, '.' );
			if ( pExt )
			{
				*pExt = '\0';
				pExt++;
			}

			bool bBaseMatch = false;
			bool bExtMatch = false;

			if ( bBaseWildcard )
				bBaseMatch = true;
			else
				bBaseMatch = ( nLenWildcardBase && 0 == V_stricmp( szCandidateBaseName, szWildCardBase ) );

			if ( ( bExtWildcard && pExt ) || ( !pExt && !bWildcardHasExt ) )
				bExtMatch = true;
			else
				bExtMatch = bWildcardHasExt && pExt && ( 0 == V_stricmp( pExt, szWildCardExt ) );

			if ( bBaseMatch && bExtMatch )
			{
				if ( bIsDir )
				{
					size_t nMatchSize = nLenWildcardPath + nSubDirLen + 1;
					char *pszFullMatch = new char[ nMatchSize ];
					V_strncpy( pszFullMatch, szCandidateName, (int)nMatchSize );
					outDirnames.AddToTail( pszFullMatch );
				}
				else
				{
					size_t nMatchSize = V_strlen( szCandidateName ) + 1;
					char *pszFullMatch = new char[ nMatchSize ];
					V_strncpy( pszFullMatch, szCandidateName, (int)nMatchSize );
					outFilenames.AddToTail( pszFullMatch );
				}
			}
		}
	}

	if ( bSortedOutput )
	{
		outDirnames.Sort( &CUtlStringList::SortFunc );
		outFilenames.Sort( &CUtlStringList::SortFunc );
	}
}

//-----------------------------------------------------------------------------
// CGmaPackFileHandle
//-----------------------------------------------------------------------------

CGmaPackFileHandle::CGmaPackFileHandle( CGmaPackFile* pOwner, int64 nBase, unsigned int nLength, unsigned int nIndex, unsigned int nFilePointer )
{
	m_nBase = nBase;
	m_nFilePointer = nFilePointer;
	m_pOwner = pOwner;
	m_nLength = nLength;
	m_nIndex = nIndex;
}

CGmaPackFileHandle::~CGmaPackFileHandle()
{
	m_pOwner->m_mutex.Lock();
	--m_pOwner->m_nOpenFiles;
	m_pOwner->m_mutex.Unlock();
}

int CGmaPackFileHandle::GetSectorSize()
{
	return m_pOwner->GetSectorSize();
}

void CGmaPackFileHandle::SetBufferSize( int nBytes )
{
}

int CGmaPackFileHandle::Read( void* pBuffer, int nDestSize, int nBytes )
{
	// Clamp nBytes to not go past the end of the file
	if ( nBytes + (int)m_nFilePointer > (int)m_nLength )
	{
		nBytes = m_nLength - m_nFilePointer;
	}

	if ( nBytes <= 0 )
		return 0;

	int nBytesRead = m_pOwner->ReadFromPack( m_nIndex, pBuffer, nDestSize, nBytes, m_nBase + m_nFilePointer );

	m_nFilePointer += nBytesRead;

	return nBytesRead;
}

int CGmaPackFileHandle::Seek( int nOffset, int nWhence )
{
	if ( nWhence == SEEK_SET )
	{
		m_nFilePointer = nOffset;
	}
	else if ( nWhence == SEEK_CUR )
	{
		m_nFilePointer += nOffset;
	}
	else if ( nWhence == SEEK_END )
	{
		m_nFilePointer = m_nLength + nOffset;
	}

	if ( m_nFilePointer > m_nLength )
	{
		m_nFilePointer = m_nLength;
	}

	return m_nFilePointer;
}
