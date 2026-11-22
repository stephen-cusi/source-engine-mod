//========= Half-Life Model Viewer 2 (Qt6 port of utils/hlmv) ================
//
// Purpose: Implementation of the standalone studio model parser. See
//          studiodata.h for the design notes (why the engine studio.h
//          structures are reinterpreted straight from disk, and why mdllib is
//          not used).
//
//          The animation/bone pipeline is NOT implemented here. hlmv2/wscript
//          compiles public/bone_setup.cpp and public/studio.cpp (the exact
//          engine sources utils/studiomdl uses) into this program; this file
//          only feeds the on-disk studiohdr_t into CStudioHdr/IBoneSetup and
//          converts the resulting matrix3x4_t array for the Qt GL front end.
//          All sequence timing, frame selection, RLE anim values, 48/64-bit
//          quaternions, pose parameters, autoplay layers, bone controllers,
//          IK locks and sequence blending are the engine's own code.
//
//          The 64-bit build of this repo keeps studio structures disk-layout-
//          compatible (that is what CMDLCache::UnserializeMDL relies on); the
//          only 64-bit difference is inside mstudio_modelvertexdata_t, which
//          this parser never touches.
//
//=============================================================================

#include "studiodata.h"

#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QtGlobal>

#include <cstring>

#include "bone_setup.h"
#include "mathlib/mathlib.h"

// Studio file magic ("IDST"/"IDSQ"), read byte-wise so endianness does not
// matter on any platform.
static const char kMdlIdModel[] = "IDST";
static const char kMdlIdSeqGroup[] = "IDSQ";
// Vvd magic ("IDSV").
static const char kVvdId[] = "IDSV";

// Longest name the inspector extracts from a model file.
static const int kMaxStudioNameLen = 128;

namespace
{
	// Purpose: Reads a whole file into a byte array; returns false if missing.
	bool ReadFileBytes( const QString &path, QByteArray &out )
	{
		QFile file( path );
		if ( !file.open( QIODevice::ReadOnly ) )
			return false;
		out = file.readAll();
		return true;
	}

	// Purpose: Copies a NUL-terminated name out of the mdl string table.
	QString StudioString( const char *psz )
	{
		return QString::fromLatin1( psz, qstrnlen( psz, kMaxStudioNameLen ) );
	}

	// Purpose: True when [off, off + count * elemSize) lies inside a buffer of
	//          nTotal bytes. 64-bit math, rejects negatives; count == 0 only
	//          needs off to be a valid position.
	bool RangeOk( qint64 off, qint64 count, qint64 elemSize, qint64 nTotal )
	{
		if ( off < 0 || count < 0 || elemSize <= 0 || off > nTotal )
			return false;
		return count <= ( nTotal - off ) / elemSize;
	}

	// Purpose: Reads a NUL-terminated string at an absolute buffer offset,
	//          clamped to the buffer; empty when the offset is out of range.
	QString SafeStringAt( const QByteArray &buf, qint64 off )
	{
		if ( off < 0 || off >= buf.size() )
			return QString();
		const char *p = buf.constData() + off;
		const qint64 nMax = qMin<qint64>( kMaxStudioNameLen, buf.size() - off );
		return QString::fromLatin1( p, (int)qstrnlen( p, nMax ) );
	}

	// Purpose: Name stored at (pStruct + nameIndex) where pStruct points into buf.
	QString SafeStructString( const QByteArray &buf, const void *pStruct, qint64 nameIndex )
	{
		const qint64 base = (const char *)pStruct - buf.constData();
		return SafeStringAt( buf, base + nameIndex );
	}

	// Animation decoding lives in public/bone_setup.cpp, compiled
	// into this target. No local reimplementation.

}

//-----------------------------------------------------------------------------
// Purpose: Construction/destruction
//-----------------------------------------------------------------------------
CStudioModelData::CStudioModelData()
{
}

CStudioModelData::~CStudioModelData()
{
	Clear();
}

//-----------------------------------------------------------------------------
// Purpose: Frees all parsed data.
//-----------------------------------------------------------------------------
void CStudioModelData::Clear()
{
	// The engine views point into the raw buffers: release them first.
	delete m_pStudioHdr;
	m_pStudioHdr = nullptr;
	delete m_pIncludeStudioHdr;
	m_pIncludeStudioHdr = nullptr;
	m_flAnimTime = 0.0f;

	m_bufMdl.clear();
	m_bufIncludeMdl.clear();
	m_pIncludeHdr = nullptr;
	m_bufVvd.clear();
	m_bufVtx.clear();
	m_bufPhy.clear();
	m_pHdr = nullptr;
	m_lstTextures.clear();
	m_lstCdTextures.clear();
	m_lstAllDrawList.clear();
	m_lstDrawList.clear();
	m_VvdFixed.clear();
	m_pVvdVertices = nullptr;
	m_SetupBoneToWorld.clear();
	m_AnimBoneToWorld.clear();
	m_lstBodygroupSelection.clear();
	m_nVvdVertexCount = 0;
	m_nTriangleCount = 0;
	m_nMeshCount = 0;
	m_nSkin = 0;
	m_nCurrentSequence = -1;
	m_flCycle = 0.0f;
	m_bAnimDataAvailable = false;
	m_bAnimDirty = true;
	m_PhysSummary = PhysicsSummary_t();
	m_sError.clear();
	m_sMissingPeers.clear();
}

//-----------------------------------------------------------------------------
// Purpose: Loads model.mdl plus the peer files (.vvd/.vtx/.phy) found next to
//          it. Missing peer files are tolerated (the inspector still works);
//          the loaded/missing state is reported via GetMissingPeers().
//-----------------------------------------------------------------------------
bool CStudioModelData::LoadFromFile( const QString &mdlPath, QString &sError )
{
	Clear();

	QFileInfo info( mdlPath );
	m_sFileName = info.fileName();
	QString basePath = info.canonicalPath() + QLatin1Char( '/' ) + info.completeBaseName();

	if ( !ReadFileBytes( mdlPath, m_bufMdl ) )
	{
		m_sError = QStringLiteral( "cannot open %1" ).arg( mdlPath );
		sError = m_sError;
		return false;
	}

	if ( !ParseHeader( sError ) )
	{
		m_sError = sError;
		return false;
	}

	// Peer files are optional for inspection purposes.
	QString dummy;
	QStringList missing;
	if ( !ParseVvd( basePath, dummy ) )
		missing << QStringLiteral( ".vvd" );
	if ( !ParseVtx( basePath, dummy ) )
		missing << QStringLiteral( ".vtx" );
	if ( !ParsePhy( basePath, dummy ) )
		missing << QStringLiteral( ".phy" );
	m_sMissingPeers = missing.join( QStringLiteral( ", " ) );

	LoadIncludeModels( mdlPath );

	// Build the engine's runtime views over the on-disk headers. CStudioHdr
	// is what IBoneSetup consumes. A $includemodel host would normally ask
	// mdlcache to materialise its virtual model during Init(); we have no
	// mdlcache, so hide the include count just for construction. Sequences of
	// such hosts are driven from the include header's own CStudioHdr (see
	// SequenceStudioHdr), which has no includes of its own.
	const int nIncludeModels = m_pHdr->numincludemodels;
	m_pHdr->numincludemodels = 0;
	m_pStudioHdr = new CStudioHdr( m_pHdr );
	m_pHdr->numincludemodels = nIncludeModels;

	if ( m_pIncludeHdr )
	{
		const int nNested = m_pIncludeHdr->numincludemodels;
		m_pIncludeHdr->numincludemodels = 0;
		m_pIncludeStudioHdr = new CStudioHdr( m_pIncludeHdr );
		m_pIncludeHdr->numincludemodels = nNested;
	}

	BuildSetupBoneMatrices();

	// Bodygroup default selections: first variant of every part.
	const int nBodyparts = qBound( 0, m_pHdr->numbodyparts, 65536 );
	m_lstBodygroupSelection.resize( nBodyparts );
	for ( int i = 0; i < nBodyparts; ++i )
		m_lstBodygroupSelection[ i ] = 0;

	BuildDrawList();

	// Start paused on the first sequence (if any); the user starts playback.
	if ( GetSequenceCount() > 0 )
	{
		m_nCurrentSequence = 0;
		m_flCycle = 0.0f;
	}
	m_bAnimDirty = true;

	// Decode frame 0 once so HasAnimationData() reflects this model instead of
	// defaulting to "no data" until the first animation tick.
	GetAnimatedBoneMatrices();

	sError.clear();
	return true;
}

//-----------------------------------------------------------------------------
// Purpose: Validates and maps the .mdl header image. The raw buffer stays
//          alive as long as this object does; all sub-structures are
//          self-relative offsets into it.
//-----------------------------------------------------------------------------
bool CStudioModelData::ParseHeader( QString &sError )
{
	if ( m_bufMdl.size() < (int)sizeof( studiohdr_t ) )
	{
		sError = QStringLiteral( "mdl too small (%1 bytes)" ).arg( m_bufMdl.size() );
		return false;
	}

	const byte *pBytes = (const byte *)m_bufMdl.constData();
	if ( memcmp( pBytes, kMdlIdModel, 4 ) != 0 && memcmp( pBytes, kMdlIdSeqGroup, 4 ) != 0 )
	{
		sError = QStringLiteral( "not a studio model (bad magic)" );
		return false;
	}

	m_pHdr = (studiohdr_t *)m_bufMdl.data();

	if ( m_pHdr->version < 44 || m_pHdr->version > 49 )
	{
		sError = QStringLiteral( "unsupported mdl version %1 (supported: 44..49)" ).arg( m_pHdr->version );
		m_pHdr = nullptr;
		return false;
	}

	// Sanity: bone table must be inside the buffer.
	if ( m_pHdr->numbones < 0 || m_pHdr->numbones > 1024 ||
		m_pHdr->boneindex <= 0 || !RangeOk( m_pHdr->boneindex, m_pHdr->numbones, sizeof( mstudiobone_t ), m_bufMdl.size() ) )
	{
		sError = QStringLiteral( "bone table out of file bounds" );
		m_pHdr = nullptr;
		return false;
	}

	// Every other table the accessors/engine views index must also be inside
	// the file. Counts are capped so UI code that builds one widget per entry
	// (e.g. one combo item per bodypart model) stays sane.
	{
		const qint64 nSize = m_bufMdl.size();
		const int kMaxCount = 65536;
		struct TableCheck_t { const char *pszName; int nCount; int nIndex; qint64 nElem; };
		const TableCheck_t tables[] = {
			{ "sequence", m_pHdr->numlocalseq, m_pHdr->localseqindex, (qint64)sizeof( mstudioseqdesc_t ) },
			{ "animdesc", m_pHdr->numlocalanim, m_pHdr->localanimindex, (qint64)sizeof( mstudioanimdesc_t ) },
			{ "attachment", m_pHdr->numlocalattachments, m_pHdr->localattachmentindex, (qint64)sizeof( mstudioattachment_t ) },
			{ "poseparameter", m_pHdr->numlocalposeparameters, m_pHdr->localposeparamindex, (qint64)sizeof( mstudioposeparamdesc_t ) },
			{ "bodypart", m_pHdr->numbodyparts, m_pHdr->bodypartindex, (qint64)sizeof( mstudiobodyparts_t ) },
			{ "cdtexture", m_pHdr->numcdtextures, m_pHdr->cdtextureindex, 4 },
		};
		for ( const TableCheck_t &t : tables )
		{
			if ( t.nCount < 0 || t.nCount > kMaxCount ||
				( t.nCount > 0 && ( t.nIndex <= 0 || !RangeOk( t.nIndex, t.nCount, t.nElem, nSize ) ) ) )
			{
				sError = QStringLiteral( "%1 table out of file bounds" ).arg( QLatin1String( t.pszName ) );
				m_pHdr = nullptr;
				return false;
			}
		}

		// Skin family table: numskinfamilies * numskinref shorts.
		if ( m_pHdr->numskinref < 0 || m_pHdr->numskinfamilies < 0 ||
			( m_pHdr->numskinref > 0 && m_pHdr->numskinfamilies > 0 &&
			( m_pHdr->skinindex <= 0 ||
			!RangeOk( m_pHdr->skinindex, (qint64)m_pHdr->numskinref * m_pHdr->numskinfamilies, 2, nSize ) ) ) )
		{
			sError = QStringLiteral( "skin table out of file bounds" );
			m_pHdr = nullptr;
			return false;
		}

		// Bodypart -> model -> mesh tables.
		for ( int iBp = 0; iBp < m_pHdr->numbodyparts; ++iBp )
		{
			const mstudiobodyparts_t *pBp = m_pHdr->pBodypart( iBp );
			const qint64 nBpOff = (const char *)pBp - m_bufMdl.constData();
			if ( pBp->nummodels < 0 || pBp->nummodels > 1024 ||
				( pBp->nummodels > 0 && !RangeOk( nBpOff + pBp->modelindex, pBp->nummodels, sizeof( mstudiomodel_t ), nSize ) ) ||
				( pBp->nummodels > 0 && nBpOff + pBp->modelindex < 0 ) )
			{
				sError = QStringLiteral( "bodypart %1 model table out of file bounds" ).arg( iBp );
				m_pHdr = nullptr;
				return false;
			}
			for ( int iModel = 0; iModel < pBp->nummodels; ++iModel )
			{
				const mstudiomodel_t *pModel = pBp->pModel( iModel );
				const qint64 nModelOff = (const char *)pModel - m_bufMdl.constData();
				if ( pModel->nummeshes < 0 || pModel->nummeshes > kMaxCount ||
					( pModel->nummeshes > 0 && !RangeOk( nModelOff + pModel->meshindex, pModel->nummeshes, sizeof( mstudiomesh_t ), nSize ) ) )
				{
					sError = QStringLiteral( "bodypart %1 model %2 mesh table out of file bounds" ).arg( iBp ).arg( iModel );
					m_pHdr = nullptr;
					return false;
				}
			}
		}
	}

	// The canonical name for version >= 47 models lives in studiohdr2_t, but
	// older files (and most content) still carry it in the inline 64-byte
	// field; studiohdr2 access is version-guarded, so use the inline field.
	if ( m_pHdr->name[ 0 ] != '\0' )
	{
		m_sModelName = StudioString( m_pHdr->name );
	}

	// Textures. Note: mstudiotexture_t's pointer fields differ between the
	// 32-bit disk layout and the 64-bit runtime struct, but sznameindex/flags
	// (offsets 0 and 4) are identical, which is all we read here.
	m_lstTextures.clear();
	if ( m_pHdr->numtextures > 0 && m_pHdr->textureindex > 0 )
	{
		if ( m_pHdr->numtextures <= 65536 &&
			RangeOk( m_pHdr->textureindex, m_pHdr->numtextures, sizeof( mstudiotexture_t ), m_bufMdl.size() ) )
		{
			for ( int i = 0; i < m_pHdr->numtextures; ++i )
			{
				const mstudiotexture_t *pTex = m_pHdr->pTexture( i );
				TextureInfo_t tex;
				if ( pTex->sznameindex > 0 )
				{
					tex.name = SafeStructString( m_bufMdl, pTex, pTex->sznameindex );
					// Strip the .vtf extension ("foo.vtf" -> "foo").
					int nDot = tex.name.lastIndexOf( QLatin1Char( '.' ) );
					if ( nDot > 0 )
						tex.name.truncate( nDot );
				}
				tex.nFlags = pTex->flags;
				m_lstTextures.append( tex );
			}
		}
	}

	// Texture search paths (cdtextures)
	// (table range validated above; each entry is an absolute string offset)
	for ( int i = 0; i < m_pHdr->numcdtextures; ++i )
	{
		const int nStrOff = *( (const int *)( m_bufMdl.constData() + m_pHdr->cdtextureindex ) + i );
		m_lstCdTextures.append( SafeStringAt( m_bufMdl, nStrOff ) );
	}

	return true;
}

//-----------------------------------------------------------------------------
// Purpose: Loads and validates the .vvd vertex file. If the file has a fixup
//          table the vertex array is flattened for LOD0, mirroring
//          Studio_LoadVertexes() from public/studio.h.
//-----------------------------------------------------------------------------
bool CStudioModelData::ParseVvd( const QString &basePath, QString &sError )
{
	m_nVvdVertexCount = 0;
	m_pVvdVertices = nullptr;

	if ( !ReadFileBytes( basePath + QStringLiteral( ".vvd" ), m_bufVvd ) || m_bufVvd.size() < 64 )
	{
		sError = QStringLiteral( "no .vvd" );
		return false;
	}

	const byte *pBytes = (const byte *)m_bufVvd.constData();
	if ( memcmp( pBytes, kVvdId, 4 ) != 0 )
	{
		sError = QStringLiteral( ".vvd bad magic" );
		return false;
	}

	const vertexFileHeader_t *pVvd = (const vertexFileHeader_t *)pBytes;
	if ( pVvd->version != MODEL_VERTEX_FILE_VERSION )
	{
		sError = QStringLiteral( ".vvd unsupported version %1" ).arg( pVvd->version );
		return false;
	}
	if ( m_pHdr && pVvd->checksum != m_pHdr->checksum )
	{
		sError = QStringLiteral( ".vvd checksum mismatch" );
		return false;
	}

	const qint64 nVvdSize = m_bufVvd.size();
	const int nNumVertexes = pVvd->numLODVertexes[ 0 ];
	if ( nNumVertexes <= 0 || pVvd->vertexDataStart <= 0 ||
		!RangeOk( pVvd->vertexDataStart, nNumVertexes, sizeof( VvdVertex_t ), nVvdSize ) )
	{
		sError = QStringLiteral( ".vvd vertex block out of bounds" );
		return false;
	}
	// Number of vertexes actually present in the file after vertexDataStart:
	// fixup sources may reference any of them (not just LOD0's count).
	const qint64 nAvailVerts = ( nVvdSize - pVvd->vertexDataStart ) / (qint64)sizeof( VvdVertex_t );

	if ( pVvd->numFixups == 0 )
	{
		m_pVvdVertices = (const VvdVertex_t *)( pBytes + pVvd->vertexDataStart );
		m_nVvdVertexCount = nNumVertexes;
	}
	else
	{
		if ( pVvd->numFixups < 0 || pVvd->fixupTableStart <= 0 ||
			!RangeOk( pVvd->fixupTableStart, pVvd->numFixups, sizeof( vertexFileFixup_t ), nVvdSize ) )
		{
			sError = QStringLiteral( ".vvd fixup table out of bounds" );
			return false;
		}

		// Rebuild mesh-ordered vertexes (fixup application identical to
		// Studio_LoadVertexes with rootLOD = 0).
		m_VvdFixed.resize( nNumVertexes );
		VvdVertex_t *pDest = m_VvdFixed.data();
		const VvdVertex_t *pSource = (const VvdVertex_t *)( pBytes + pVvd->vertexDataStart );
		const vertexFileFixup_t *pFixupTable = (const vertexFileFixup_t *)( pBytes + pVvd->fixupTableStart );

		int target = 0;
		for ( int i = 0; i < pVvd->numFixups && target < nNumVertexes; ++i )
		{
			if ( pFixupTable[ i ].lod < 0 )
				continue; // rootLOD is always 0 here
			const qint64 nSrc = pFixupTable[ i ].sourceVertexID;
			qint64 nCount = pFixupTable[ i ].numVertexes;
			if ( nSrc < 0 || nCount < 0 || nSrc > nAvailVerts || nCount > nAvailVerts - nSrc )
			{
				m_VvdFixed.clear();
				sError = QStringLiteral( ".vvd fixup %1 out of bounds" ).arg( i );
				return false;
			}
			if ( target + nCount > nNumVertexes )
				nCount = nNumVertexes - target;
			memcpy( pDest + target, pSource + nSrc, (size_t)nCount * sizeof( VvdVertex_t ) );
			target += (int)nCount;
		}
		m_pVvdVertices = m_VvdFixed.constData();
		m_nVvdVertexCount = target;
	}

	return true;
}

//-----------------------------------------------------------------------------
// Purpose: Parses the .vtx hardware mesh file (version 7 strip format) into
//          per-mesh triangle index lists. Vertex_t.origMeshVertID maps strip
//          indices onto the mesh vertex range of the vvd global vertex array.
//
//          Only v7 (every HL2-era model) is handled; other versions are
//          reported as unsupported but do not fail the load.
//-----------------------------------------------------------------------------
bool CStudioModelData::ParseVtx( const QString &basePath, QString &sError )
{
	m_lstAllDrawList.clear();
	m_nTriangleCount = 0;
	m_nMeshCount = 0;

	QString candidate = basePath + QStringLiteral( ".dx90.vtx" );
	if ( !QFileInfo::exists( candidate ) )
		candidate = basePath + QStringLiteral( ".dx80.vtx" );
	if ( !QFileInfo::exists( candidate ) )
		candidate = basePath + QStringLiteral( ".sw.vtx" );
	if ( !ReadFileBytes( candidate, m_bufVtx ) || m_bufVtx.size() < 32 )
	{
		sError = QStringLiteral( "no .vtx" );
		return false;
	}

	const byte *pBase = (const byte *)m_bufVtx.constData();
	const int nFileSize = m_bufVtx.size();

	// FileHeader_t (packed, public/optimize.h): version(0), vertCacheSize(4),
	// maxBonesPerStrip u16(8), maxBonesPerTri u16(10), maxBonesPerVert(12),
	// checkSum(16), numLODs(20), materialReplacementListOffset(24),
	// numBodyParts(28), bodyPartOffset(32). The two shorts before
	// maxBonesPerVert shift every later field by 4 bytes against the
	// all-ints layout most mdl format docs show.
	int nVersion = *(const int *)( pBase + 0 );
	int nNumBodyParts = *(const int *)( pBase + 28 );
	int nBodyPartOffset = *(const int *)( pBase + 32 );

	if ( nVersion != 7 )
	{
		sError = QStringLiteral( ".vtx unsupported version %1 (only 7)" ).arg( nVersion );
		return false;
	}
	if ( nNumBodyParts < 0 || !RangeOk( nBodyPartOffset, nNumBodyParts, 8, nFileSize ) )
	{
		sError = QStringLiteral( ".vtx bodypart table out of bounds" );
		return false;
	}

	// All on-disk record sizes for vtx version 7 (packed):
	//   BodyPartHeader_t   8
	//   ModelHeader_t      8
	//   ModelLODHeader_t  12
	//   MeshHeader_t        9  (numStripGroups, stripGroupHeaderOffset, flags)
	//   StripGroupHeader_t 25  (v7 has no topology tail)
	//   Vertex_t            9
	//   StripHeader_t      27
	const int kBodyPartSize = 8;
	const int kModelSize = 8;
	const int kLodSize = 12;
	const int kMeshSize = 9;
	const int kStripGroupSize = 25;
	const int kVertexSize = 9;
	const int kStripSize = 27;

	// Hostile files can make many tables overlap; bound the total work.
	const int kMaxDrawMeshes = 65536;
	const qint64 kMaxTotalIndices = 1 << 26;
	qint64 nTotalIndices = 0;

	auto Fail = [ & ]( const QString &sMsg ) -> bool
	{
		m_lstAllDrawList.clear();
		m_nTriangleCount = 0;
		m_nMeshCount = 0;
		sError = sMsg;
		return false;
	};
	auto ReadInt = [ & ]( qint64 off ) -> int
	{
		int n;
		memcpy( &n, pBase + off, sizeof( n ) );
		return n;
	};
	auto ReadUShort = [ & ]( qint64 off ) -> unsigned short
	{
		unsigned short n;
		memcpy( &n, pBase + off, sizeof( n ) );
		return n;
	};

	for ( int iBodyPart = 0; iBodyPart < nNumBodyParts; ++iBodyPart )
	{
		// All offsets below are absolute file positions (qint64).
		const qint64 nBpPos = (qint64)nBodyPartOffset + (qint64)iBodyPart * kBodyPartSize;
		const int nNumModels = ReadInt( nBpPos + 0 );
		const int nModelOffset = ReadInt( nBpPos + 4 );
		if ( nNumModels < 0 || !RangeOk( nBpPos + nModelOffset, nNumModels, kModelSize, nFileSize ) )
			return Fail( QStringLiteral( ".vtx bodypart %1 model table out of bounds" ).arg( iBodyPart ) );

		for ( int iModel = 0; iModel < nNumModels; ++iModel )
		{
			const qint64 nModelPos = nBpPos + nModelOffset + (qint64)iModel * kModelSize;
			const int nNumLods = ReadInt( nModelPos + 0 );
			const int nLodOffset = ReadInt( nModelPos + 4 );
			if ( nNumLods <= 0 )
				continue;
			if ( !RangeOk( nModelPos + nLodOffset, nNumLods, kLodSize, nFileSize ) )
				return Fail( QStringLiteral( ".vtx model lod table out of bounds" ) );

			// Always draw LOD 0 (highest detail).
			const qint64 nLodPos = nModelPos + nLodOffset;
			const int nNumMeshes = ReadInt( nLodPos + 0 );
			const int nMeshOffset = ReadInt( nLodPos + 4 );
			if ( nNumMeshes < 0 || !RangeOk( nLodPos + nMeshOffset, nNumMeshes, kMeshSize, nFileSize ) )
				return Fail( QStringLiteral( ".vtx lod mesh table out of bounds" ) );

			for ( int iMesh = 0; iMesh < nNumMeshes; ++iMesh )
			{
				if ( m_lstAllDrawList.count() >= kMaxDrawMeshes )
					return Fail( QStringLiteral( ".vtx has too many meshes" ) );

				const qint64 nMeshPos = nLodPos + nMeshOffset + (qint64)iMesh * kMeshSize;
				const int nNumStripGroups = ReadInt( nMeshPos + 0 );
				const int nStripGroupHeaderOffset = ReadInt( nMeshPos + 4 );
				if ( nNumStripGroups < 0 ||
					!RangeOk( nMeshPos + nStripGroupHeaderOffset, nNumStripGroups, kStripGroupSize, nFileSize ) )
					return Fail( QStringLiteral( ".vtx mesh strip group table out of bounds" ) );

				MeshDrawData_t draw;
				draw.nBodypart = iBodyPart;
				draw.nModel = iModel;
				draw.nMaterialRef = -1;
				draw.nVertexBase = -1;
				draw.nVertexCount = 0;

				for ( int iGroup = 0; iGroup < nNumStripGroups; ++iGroup )
				{
					const qint64 nGroupPos = nMeshPos + nStripGroupHeaderOffset + (qint64)iGroup * kStripGroupSize;
					const int nGroupVerts = ReadInt( nGroupPos + 0 );
					const int nGroupVertOffset = ReadInt( nGroupPos + 4 );
					const int nGroupIndices = ReadInt( nGroupPos + 8 );
					const int nGroupIndexOffset = ReadInt( nGroupPos + 12 );
					const int nGroupStrips = ReadInt( nGroupPos + 16 );
					const int nGroupStripOffset = ReadInt( nGroupPos + 20 );

					if ( nGroupVerts < 0 || nGroupIndices < 0 || nGroupStrips < 0 )
						return Fail( QStringLiteral( ".vtx strip group has negative counts" ) );
					if ( nGroupVerts == 0 || nGroupIndices == 0 )
						continue;

					// Vertex_t table, index table and strip table are all
					// relative to the strip group header.
					if ( !RangeOk( nGroupPos + nGroupVertOffset, nGroupVerts, kVertexSize, nFileSize ) ||
						!RangeOk( nGroupPos + nGroupIndexOffset, nGroupIndices, 2, nFileSize ) ||
						!RangeOk( nGroupPos + nGroupStripOffset, nGroupStrips, kStripSize, nFileSize ) )
						return Fail( QStringLiteral( ".vtx strip group tables out of bounds" ) );

					// The group's Vertex_t table maps strip indices to mesh
					// vertex IDs (origMeshVertID sits at offset 4).
					QVector<int> groupToMeshVertex( nGroupVerts );
					for ( int v = 0; v < nGroupVerts; ++v )
					{
						const qint64 nVtxPos = nGroupPos + nGroupVertOffset + (qint64)v * kVertexSize;
						groupToMeshVertex[ v ] = ReadUShort( nVtxPos + 4 );
					}

					// Walk the strips so that tristrips are expanded into
					// triangle lists (the engine draws them with the native
					// strip topology; a flat list needs conversion).
					const qint64 nIndexBase = nGroupPos + nGroupIndexOffset;
					for ( int iStrip = 0; iStrip < nGroupStrips; ++iStrip )
					{
						const qint64 nStripPos = nGroupPos + nGroupStripOffset + (qint64)iStrip * kStripSize;
						const int nStripIndices = ReadInt( nStripPos + 0 );
						const int nStripIndexOffset = ReadInt( nStripPos + 4 );
						// StripHeader_t (packed): numIndices(0), indexOffset(4),
						// numVerts(8), vertOffset(12), numBones short(16),
						// flags(18), numBoneStateChanges(19), boneStateChangeOffset(23).
						const byte stripFlags = *( pBase + nStripPos + 18 );
						const bool bTriStrip = ( stripFlags & 0x02 ) != 0; // STRIP_IS_TRISTRIP

						if ( nStripIndices < 0 || nStripIndexOffset < 0 ||
							(qint64)nStripIndexOffset + nStripIndices > nGroupIndices )
							continue;

						nTotalIndices += (qint64)nStripIndices * ( bTriStrip ? 3 : 1 );
						if ( nTotalIndices > kMaxTotalIndices )
							return Fail( QStringLiteral( ".vtx index count exceeds limit" ) );

						// Reads a strip index and maps it through the group's
						// vertex table; -1 when the index is out of range.
						auto MapIndex = [ & ]( int nPos ) -> int
						{
							const unsigned short idx = ReadUShort( nIndexBase + (qint64)nPos * 2 );
							return idx < nGroupVerts ? groupToMeshVertex[ idx ] : -1;
						};

						if ( bTriStrip )
						{
							// Strip -> triangle list conversion.
							for ( int t = 2; t < nStripIndices; ++t )
							{
								const unsigned short a = ReadUShort( nIndexBase + (qint64)( nStripIndexOffset + t - 2 ) * 2 );
								unsigned short b = ReadUShort( nIndexBase + (qint64)( nStripIndexOffset + t - 1 ) * 2 );
								unsigned short c = ReadUShort( nIndexBase + (qint64)( nStripIndexOffset + t ) * 2 );
								if ( a == b || b == c || a == c )
									continue; // degenerate
								if ( t & 1 )
									qSwap( b, c );
								if ( a >= nGroupVerts || b >= nGroupVerts || c >= nGroupVerts )
									return Fail( QStringLiteral( ".vtx strip index outside group vertex table" ) );
								draw.indices.append( groupToMeshVertex[ a ] );
								draw.indices.append( groupToMeshVertex[ b ] );
								draw.indices.append( groupToMeshVertex[ c ] );
							}
						}
						else
						{
							for ( int t = 0; t + 2 < nStripIndices; t += 3 )
							{
								const int a = MapIndex( nStripIndexOffset + t );
								const int b = MapIndex( nStripIndexOffset + t + 1 );
								const int c = MapIndex( nStripIndexOffset + t + 2 );
								if ( a < 0 || b < 0 || c < 0 )
									return Fail( QStringLiteral( ".vtx strip index outside group vertex table" ) );
								draw.indices.append( a );
								draw.indices.append( b );
								draw.indices.append( c );
							}
						}
					}
				}

				// Empty meshes are kept so the list stays 1:1 with the mdl
				// mesh order (FillMeshMetadata pairs them); the renderer
				// simply skips them.
				m_lstAllDrawList.append( draw );
				m_nTriangleCount += draw.indices.count() / 3;
			}
		}
	}

	m_nMeshCount = m_lstAllDrawList.count();
	FillMeshMetadata();
	return !m_lstAllDrawList.isEmpty();
}

//-----------------------------------------------------------------------------
// Purpose: Pairs the vtx draw entries (file order: bodypart/model/mesh) with
//          the mdl mesh metadata: skinref, global vvd vertex base and count.
//          The vtx and mdl files always enumerate meshes in the same order.
//-----------------------------------------------------------------------------
void CStudioModelData::FillMeshMetadata()
{
	if ( !m_pHdr )
		return;

	int nIndex = 0;
	for ( int iBp = 0; iBp < m_pHdr->numbodyparts && nIndex < m_lstAllDrawList.count(); ++iBp )
	{
		const mstudiobodyparts_t *pBp = m_pHdr->pBodypart( iBp );
		for ( int iModel = 0; iModel < pBp->nummodels && nIndex < m_lstAllDrawList.count(); ++iModel )
		{
			const mstudiomodel_t *pModel = pBp->pModel( iModel );
			for ( int iMesh = 0; iMesh < pModel->nummeshes && nIndex < m_lstAllDrawList.count(); ++iMesh, ++nIndex )
			{
				const mstudiomesh_t *pMesh = pModel->pMesh( iMesh );
				MeshDrawData_t &draw = m_lstAllDrawList[ nIndex ];
				draw.nBodypart = iBp;
				draw.nModel = iModel;
				draw.nMaterialRef = pMesh->material;
				// Global vvd vertex index of mesh vertex 0:
				//   model.vertexindex/sizeof(mstudiovertex_t) + mesh.vertexoffset
				draw.nVertexBase = pModel->vertexindex / (int)sizeof( VvdVertex_t ) + pMesh->vertexoffset;
				draw.nVertexCount = pMesh->numvertices;
			}
		}
	}
}

//-----------------------------------------------------------------------------
// Purpose: Parses the .phy collision summary: the 16 byte header plus the
//          trailing "solid { }" text key-value block (the same data the old
//          viewer showed through vphysics, read directly here).
//-----------------------------------------------------------------------------
bool CStudioModelData::ParsePhy( const QString &basePath, QString &sError )
{
	m_PhysSummary = PhysicsSummary_t();

	if ( !ReadFileBytes( basePath + QStringLiteral( ".phy" ), m_bufPhy ) || m_bufPhy.size() < 16 )
	{
		sError = QStringLiteral( "no .phy" );
		return false;
	}

	const int *pHeader = (const int *)m_bufPhy.constData();
	m_PhysSummary.bLoaded = true;
	m_PhysSummary.nHeaderSize = pHeader[ 0 ];
	m_PhysSummary.nSolidCount = pHeader[ 2 ];
	m_PhysSummary.nChecksum = (uint32_t)pHeader[ 3 ];

	// Walk solid records: each is { int size; byte data[size]; }.
	int offset = 16;
	int nSolids = 0;
	while ( nSolids < m_PhysSummary.nSolidCount && offset + 4 <= m_bufPhy.size() )
	{
		int nSize = *(const int *)( m_bufPhy.constData() + offset );
		if ( nSize <= 0 || offset + 4 + nSize > m_bufPhy.size() )
			break; // corrupt; stop
		offset += 4 + nSize;
		++nSolids;
	}

	// The rest of the file is the text key-value block.
	QStringList lines;
	QString text = QString::fromLatin1( m_bufPhy.constData() + offset, m_bufPhy.size() - offset );
	const QStringList rawLines = text.split( QChar( '\n' ), Qt::SkipEmptyParts );
	for ( const QString &line : rawLines )
	{
		QString trimmed = line.trimmed();
		if ( trimmed.isEmpty() )
			continue;
		lines.append( trimmed );
		if ( lines.count() >= 64 )
			break;
	}
	m_PhysSummary.solidText = lines;

	// Pull the interesting scalar values out of the text block.
	for ( const QString &line : lines )
	{
		if ( line.startsWith( QLatin1String( "\"mass\"" ) ) )
		{
			QStringList parts = line.split( QChar( '"' ) );
			if ( parts.count() >= 4 )
				m_PhysSummary.flMass = parts.at( 3 ).toFloat();
		}
		else if ( line.startsWith( QLatin1String( "\"surfaceprop\"" ) ) )
		{
			QStringList parts = line.split( QChar( '"' ) );
			if ( parts.count() >= 4 )
				m_PhysSummary.surfaceprop = parts.at( 3 );
		}
	}

	return true;
}

//-----------------------------------------------------------------------------
// Purpose: Sequence accessors used by the UI.
//-----------------------------------------------------------------------------
QString CStudioModelData::GetSequenceLabel( int iSeq, QString *activity, float *fps, int *numFrames, bool *looping, int *numBlends ) const
{
	int nLocal = -1;
	const studiohdr_t *pHdr = SequenceHeader( iSeq, &nLocal );
	if ( !pHdr )
		return QString();

	const mstudioseqdesc_t *pSeq = pHdr->pLocalSeqdesc( nLocal );
	const QByteArray &seqBuf = ( pHdr == m_pHdr ) ? m_bufMdl : m_bufIncludeMdl;
	QString sLabel;
	if ( pSeq->szlabelindex > 0 )
		sLabel = SafeStructString( seqBuf, pSeq, pSeq->szlabelindex );

	if ( activity )
	{
		if ( pSeq->szactivitynameindex > 0 )
			*activity = SafeStructString( seqBuf, pSeq, pSeq->szactivitynameindex );
		else
			*activity = QString();
	}
	if ( fps || numFrames || looping || numBlends )
	{
		// fps/numframes come from the underlying animdesc (the sequence has
		// none of its own); the blend at (0,0) is the primary anim.
		int iAnim = pSeq->anim( 0, 0 );
		if ( iAnim >= 0 && iAnim < pHdr->numlocalanim )
		{
			const mstudioanimdesc_t *pAnim = pHdr->pLocalAnimdesc( iAnim );
			if ( fps ) *fps = pAnim->fps;
			if ( numFrames ) *numFrames = pAnim->numframes;
			if ( looping ) *looping = ( pSeq->flags & STUDIO_LOOPING ) != 0;
		}
		else
		{
			if ( fps ) *fps = 0.0f;
			if ( numFrames ) *numFrames = 1;
			if ( looping ) *looping = false;
		}
		if ( numBlends ) *numBlends = pSeq->numblends;
	}
	return sLabel;
}

QString CStudioModelData::GetCurrentSequenceLabel() const
{
	return GetSequenceLabel( m_nCurrentSequence );
}

//-----------------------------------------------------------------------------
// Purpose: Bone/attachment/bodypart/pose accessors.
//-----------------------------------------------------------------------------
QString CStudioModelData::GetBoneName( int iBone, int *parent ) const
{
	if ( !m_pHdr || iBone < 0 || iBone >= m_pHdr->numbones )
		return QString();
	const mstudiobone_t *pBone = m_pHdr->pBone( iBone );
	if ( parent ) *parent = pBone->parent;
	if ( pBone->sznameindex <= 0 )
		return QStringLiteral( "(unnamed)" );
	return SafeStructString( m_bufMdl, pBone, pBone->sznameindex );
}

QString CStudioModelData::GetAttachmentName( int iAttach, int *bone ) const
{
	if ( !m_pHdr || iAttach < 0 || iAttach >= m_pHdr->numlocalattachments )
		return QString();
	const mstudioattachment_t *pAtt = m_pHdr->pLocalAttachment( iAttach );
	if ( bone ) *bone = pAtt->localbone;
	if ( pAtt->sznameindex <= 0 )
		return QStringLiteral( "(unnamed)" );
	return SafeStructString( m_bufMdl, pAtt, pAtt->sznameindex );
}

QString CStudioModelData::GetBodypartName( int iBodypart, int *numModels ) const
{
	if ( !m_pHdr || iBodypart < 0 || iBodypart >= m_pHdr->numbodyparts )
		return QString();
	const mstudiobodyparts_t *pBp = m_pHdr->pBodypart( iBodypart );
	if ( numModels ) *numModels = pBp->nummodels;
	if ( pBp->sznameindex <= 0 )
		return QStringLiteral( "(unnamed)" );
	return SafeStructString( m_bufMdl, pBp, pBp->sznameindex );
}

QString CStudioModelData::GetModelName2( int iBodypart, int iModel ) const
{
	if ( !m_pHdr || iBodypart < 0 || iBodypart >= m_pHdr->numbodyparts )
		return QString();
	const mstudiobodyparts_t *pBp = m_pHdr->pBodypart( iBodypart );
	if ( iModel < 0 || iModel >= pBp->nummodels )
		return QString();
	const mstudiomodel_t *pModel = pBp->pModel( iModel );
	return QString::fromLatin1( pModel->name, qstrnlen( pModel->name, (int)sizeof( pModel->name ) ) );
}

QString CStudioModelData::GetPoseParameterName( int iPose, float *start, float *end, float *loop ) const
{
	if ( !m_pHdr || iPose < 0 || iPose >= m_pHdr->numlocalposeparameters )
		return QString();
	const mstudioposeparamdesc_t *pPose = m_pHdr->pLocalPoseParameter( iPose );
	if ( start ) *start = pPose->start;
	if ( end ) *end = pPose->end;
	if ( loop ) *loop = pPose->loop;
	if ( pPose->sznameindex <= 0 )
		return QStringLiteral( "(unnamed)" );
	return SafeStructString( m_bufMdl, pPose, pPose->sznameindex );
}

//-----------------------------------------------------------------------------
// Purpose: Bodygroup/skin state.
//-----------------------------------------------------------------------------
void CStudioModelData::SetBodygroupSelection( int iBodypart, int iModel )
{
	if ( iBodypart < 0 || iBodypart >= m_lstBodygroupSelection.count() )
		return;
	m_lstBodygroupSelection[ iBodypart ] = qMax( 0, iModel );
	BuildDrawList();
}

void CStudioModelData::SetSkin( int nSkin )
{
	if ( !m_pHdr || nSkin < 0 || nSkin >= m_pHdr->numskinfamilies )
		return;
	m_nSkin = nSkin;
}

//-----------------------------------------------------------------------------
// Purpose: Maps a mesh's skinref index through the active skin family to a
//          global texture index.
//-----------------------------------------------------------------------------
int CStudioModelData::GetMeshMaterial( int nMeshMaterialRef ) const
{
	if ( !m_pHdr || nMeshMaterialRef < 0 || nMeshMaterialRef >= m_pHdr->numskinref )
		return nMeshMaterialRef;

	int nIndex = m_nSkin * m_pHdr->numskinref + nMeshMaterialRef;
	if ( nIndex < 0 || m_pHdr->skinindex <= 0 )
		return nMeshMaterialRef;
	if ( m_pHdr->skinindex + ( nIndex + 1 ) * 2 > m_bufMdl.size() )
		return nMeshMaterialRef;

	return (int)m_pHdr->pSkinref( nIndex )[ 0 ];
}

const VvdVertex_t *CStudioModelData::GetVvdVertices( int *pCount ) const
{
	if ( pCount ) *pCount = m_nVvdVertexCount;
	return m_pVvdVertices;
}

//-----------------------------------------------------------------------------
// Purpose: Rebuilds the per-mesh draw list, honouring the bodygroup
//          selections. Mesh geometry and metadata come from the same vtx walk
//          (m_lstAllDrawList covers every variant).
//-----------------------------------------------------------------------------
void CStudioModelData::BuildDrawList()
{
	m_lstDrawList.clear();
	if ( !m_pHdr )
		return;

	for ( const MeshDrawData_t &draw : m_lstAllDrawList )
	{
		int nSelected = m_lstBodygroupSelection.value( draw.nBodypart, 0 );
		if ( draw.nModel != nSelected )
			continue;
		m_lstDrawList.append( draw );
	}

	m_nMeshCount = m_lstDrawList.count();
}

//-----------------------------------------------------------------------------
// Purpose: Computes the bind-pose (setup) transform of every bone in model
//          space: bone.pos/quat chained through parents.
//
//          Mapping note: matrix3x4_t is used with the column-vector convention
//          (VectorTransform = M*v, ConcatTransforms(A,B) = A*B). Mapping each
//          matrix3x4_t row into a QMatrix4x4 row preserves exactly that
//          arithmetic for affine transforms, so QMatrix4x4 composition
//          (parent * local) is equivalent to ConcatTransforms(parent, local).
//-----------------------------------------------------------------------------
void CStudioModelData::BuildSetupBoneMatrices()
{
	m_SetupBoneToWorld.clear();
	if ( !m_pHdr )
		return;

	auto ToQt = []( const matrix3x4_t &m )
	{
		QMatrix4x4 out;
		for ( int r = 0; r < 3; ++r )
			out.setRow( r, QVector4D( m[ r ][ 0 ], m[ r ][ 1 ], m[ r ][ 2 ], m[ r ][ 3 ] ) );
		out.setRow( 3, QVector4D( 0.0f, 0.0f, 0.0f, 1.0f ) );
		return out;
	};

	// ComputePoseToWorld() (studiorender/r_studio.cpp) takes poseToBone from
	// the mstudiolinearbone_t table when the model has one, falling back to
	// the per-bone copy only for older files. Use the very same source here.
	const mstudiolinearbone_t *pLinearBones = nullptr;
	if ( m_pHdr->studiohdr2index > 0 )
	{
		// pLinearBones() trusts studiohdr2index/linearboneindex; validate the
		// header2, the linear bone header and its poseToBone table first.
		const qint64 nSize = m_bufMdl.size();
		const qint64 nHdr2Off = m_pHdr->studiohdr2index;
		if ( RangeOk( nHdr2Off, 1, sizeof( studiohdr2_t ), nSize ) )
		{
			const int nLinearIndex = m_pHdr->pStudioHdr2()->linearboneindex;
			const qint64 nLinearOff = nHdr2Off + nLinearIndex;
			if ( nLinearIndex > 0 && RangeOk( nLinearOff, 1, sizeof( mstudiolinearbone_t ), nSize ) )
			{
				const mstudiolinearbone_t *pCand = m_pHdr->pLinearBones();
				if ( pCand && pCand->numbones == m_pHdr->numbones &&
					RangeOk( nLinearOff + pCand->posetoboneindex, pCand->numbones, sizeof( matrix3x4_t ), nSize ) )
					pLinearBones = pCand;
			}
		}
	}

	m_SetupBoneToWorld.resize( m_pHdr->numbones );
	m_BindInverse.resize( m_pHdr->numbones );
	for ( int i = 0; i < m_pHdr->numbones; ++i )
	{
		const mstudiobone_t *pBone = m_pHdr->pBone( i );
		const QMatrix4x4 poseToBone = pLinearBones
			? ToQt( pLinearBones->poseToBone( i ) ) : ToQt( pBone->poseToBone );

		// The setup BoneToWorld is the inverse of poseToBone; keep the exact
		// poseToBone matrix for ComputePoseToWorld instead of inverting twice.
		m_BindInverse[ i ] = poseToBone;

		bool bInvertible = false;
		const QMatrix4x4 bindToWorld = poseToBone.inverted( &bInvertible );
		if ( bInvertible )
		{
			m_SetupBoneToWorld[ i ] = bindToWorld;
			continue;
		}

		// Fallback for malformed/legacy files: derive the local transform
		// from the quaternion (or Euler rotation) and chain through parents.
		Quaternion q = pBone->quat;
		if ( q.x == 0.0f && q.y == 0.0f && q.z == 0.0f && q.w == 0.0f )
			AngleQuaternion( pBone->rot, q );

		matrix3x4_t local;
		QuaternionMatrix( q, pBone->pos, local );
		const QMatrix4x4 localQ = ToQt( local );

		int iParent = pBone->parent;
		if ( iParent >= 0 && iParent < i )
			m_SetupBoneToWorld[ i ] = m_SetupBoneToWorld[ iParent ] * localQ;
		else
			m_SetupBoneToWorld[ i ] = localQ;
		m_BindInverse[ i ] = m_SetupBoneToWorld[ i ].inverted();
	}
}

//-----------------------------------------------------------------------------
// Purpose: Drops sequence playback so the setup (bind) pose is shown.
//-----------------------------------------------------------------------------
void CStudioModelData::SetBindPoseOnly()
{
	m_nCurrentSequence = -1;
	m_flCycle = 0.0f;
	m_bPaused = true;
	m_bAnimDirty = true;
}

//-----------------------------------------------------------------------------
// Purpose: Selects the current sequence and resets its cycle.
//-----------------------------------------------------------------------------
bool CStudioModelData::SetCurrentSequence( int iSeq )
{
	if ( !m_pHdr || iSeq < 0 || iSeq >= GetSequenceCount() )
		return false;
	if ( m_nCurrentSequence != iSeq )
	{
		m_nCurrentSequence = iSeq;
		m_flCycle = 0.0f;
		m_bAnimDirty = true;
	}
	return true;
}

//-----------------------------------------------------------------------------
// Purpose: Maps a (possibly $includemodel) sequence to the engine CStudioHdr
//          that owns it. Everything downstream is driven from that header.
//-----------------------------------------------------------------------------
CStudioHdr *CStudioModelData::SequenceStudioHdr( int iSeq, int *pLocalSeq ) const
{
	if ( pLocalSeq )
		*pLocalSeq = -1;
	if ( !m_pHdr || iSeq < 0 || iSeq >= GetSequenceCount() )
		return nullptr;

	if ( iSeq < m_pHdr->numlocalseq )
	{
		if ( pLocalSeq )
			*pLocalSeq = iSeq;
		return m_pStudioHdr;
	}
	if ( m_pIncludeStudioHdr )
	{
		if ( pLocalSeq )
			*pLocalSeq = iSeq - m_pHdr->numlocalseq;
		return m_pIncludeStudioHdr;
	}
	return nullptr;
}

//-----------------------------------------------------------------------------
// Purpose: StudioModel::GetDuration() is exactly Studio_Duration() from
//          public/bone_setup.cpp. Sequence timing (fps, pose-parameter
//          weighted blends) therefore comes from the engine, not from a
//          hand-written numframes/fps estimate.
//-----------------------------------------------------------------------------
float CStudioModelData::SequenceDuration( int iSeq ) const
{
	int nLocal = -1;
	CStudioHdr *pHdr = SequenceStudioHdr( iSeq, &nLocal );
	if ( !pHdr || nLocal < 0 )
		return 0.0f;

	float poseParams[ MAXSTUDIOPOSEPARAM ];
	std::memset( poseParams, 0, sizeof( poseParams ) );
	const float d = Studio_Duration( pHdr, nLocal, poseParams );
	return d;
}

//-----------------------------------------------------------------------------
// Purpose: StudioModel::AdvanceFrame(): clamp dt to 100 ms, advance by
//          dt / duration, wrap the cycle unconditionally. There is no
//          "stop at the last frame" branch in the stock viewer.
//-----------------------------------------------------------------------------
void CStudioModelData::AdvanceAnimation( float dt )
{
	if ( m_bPaused || m_nCurrentSequence < 0 )
		return;
	if ( dt > 0.1f )
		dt = 0.1f;

	const float flDuration = SequenceDuration( m_nCurrentSequence );
	if ( flDuration > 0.0f )
	{
		if ( dt > 0.0f )
		{
			m_flCycle += dt * m_flPlaybackSpeed / flDuration;
			m_flCycle -= (int)m_flCycle;
			m_flAnimTime += dt;
			m_bAnimDirty = true;
		}
	}
	else
	{
		m_flCycle = 0.0f;
	}
}

//-----------------------------------------------------------------------------
// Purpose: Control Panel's "Force To Frame" slider sets the cycle directly.
//          Sequence timing itself still comes from the engine's Studio_Duration.
//-----------------------------------------------------------------------------
void CStudioModelData::SetCycle( float flCycle )
{
	m_flCycle = qBound( 0.0f, flCycle, 1.0f );
	m_bAnimDirty = true;
}

//-----------------------------------------------------------------------------
// Purpose: StudioModel::SetUpBones() from utils/hlmv/studio_render.cpp,
//          executed by the engine's own IBoneSetup (public/bone_setup.cpp):
//            InitPose -> AccumulatePose -> CalcAutoplaySequences -> CalcBoneAdj
//          and then ConcatTransforms(parent, local) in bone-table order.
//          hlmv2 only converts the resulting matrix3x4_t into QMatrix4x4.
//-----------------------------------------------------------------------------
const QVector<QMatrix4x4> &CStudioModelData::GetAnimatedBoneMatrices()
{
	if ( !m_bAnimDirty )
		return m_AnimBoneToWorld;
	m_bAnimDirty = false;
	m_bAnimDataAvailable = false;
	if ( !m_pHdr )
		return m_AnimBoneToWorld;

	const int nBones = m_pHdr->numbones;
	m_AnimBoneToWorld = m_SetupBoneToWorld;
	if ( m_AnimBoneToWorld.count() < nBones )
		m_AnimBoneToWorld.resize( nBones );

	int nLocalSeq = -1;
	CStudioHdr *pHdr = ( m_nCurrentSequence >= 0 )
		? SequenceStudioHdr( m_nCurrentSequence, &nLocalSeq ) : nullptr;
	if ( !pHdr || nLocalSeq < 0 || pHdr->numbones() > MAXSTUDIOBONES )
		return m_AnimBoneToWorld;

	float poseParams[ MAXSTUDIOPOSEPARAM ];
	float controllers[ MAXSTUDIOBONECTRLS ];
	std::memset( poseParams, 0, sizeof( poseParams ) );
	std::memset( controllers, 0, sizeof( controllers ) );

	Vector pos[ MAXSTUDIOBONES ];
	Quaternion q[ MAXSTUDIOBONES ];

	IBoneSetup boneSetup( pHdr, BONE_USED_BY_ANYTHING, poseParams );
	boneSetup.InitPose( pos, q );
	boneSetup.AccumulatePose( pos, q, nLocalSeq, m_flCycle, 1.0f, m_flAnimTime, nullptr );
	// Stock SetUpBones() also calls IBoneSetup::CalcAutoplaySequences(). That
	// helper walks studiohdr_t::GetAutoplayList(), whose process-global table
	// belongs to the mdlcache runtime; the viewer has no mdlcache. Enumerate
	// the STUDIO_AUTOPLAY sequences from the header and feed them to the
	// engine's own AccumulatePose() - same code path, no new animation logic.
	for ( int i = 0; i < pHdr->GetNumSeq(); ++i )
	{
		mstudioseqdesc_t &seq = pHdr->pSeqdesc( i );
		if ( !( seq.flags & STUDIO_AUTOPLAY ) )
			continue;
		const float cps = Studio_CPS( pHdr, seq, i, poseParams );
		float cycle = m_flAnimTime * cps;
		cycle -= (int)cycle;
		boneSetup.AccumulatePose( pos, q, i, cycle, 1.0f, m_flAnimTime, nullptr );
	}
	boneSetup.CalcBoneAdj( pos, q, controllers );

	const int nSrcBones = pHdr->numbones();
	matrix3x4_t local, world[ MAXSTUDIOBONES ];
	for ( int i = 0; i < nSrcBones; ++i )
	{
		QuaternionMatrix( q[ i ], local );
		local[ 0 ][ 3 ] = pos[ i ].x;
		local[ 1 ][ 3 ] = pos[ i ].y;
		local[ 2 ][ 3 ] = pos[ i ].z;

		const int iParent = pHdr->pBone( i )->parent;
		if ( iParent >= 0 && iParent < i )
			ConcatTransforms( world[ iParent ], local, world[ i ] );
		else
			MatrixCopy( local, world[ i ] );
	}

	auto ToQt = []( const matrix3x4_t &m )
	{
		QMatrix4x4 out;
		for ( int r = 0; r < 3; ++r )
			out.setRow( r, QVector4D( m[ r ][ 0 ], m[ r ][ 1 ], m[ r ][ 2 ], m[ r ][ 3 ] ) );
		out.setRow( 3, QVector4D( 0.0f, 0.0f, 0.0f, 1.0f ) );
		return out;
	};

	if ( pHdr == m_pStudioHdr )
	{
		for ( int i = 0; i < nBones && i < nSrcBones; ++i )
			m_AnimBoneToWorld[ i ] = ToQt( world[ i ] );
	}
	else
	{
		// The sequence belongs to an $includemodel animation library with its
		// own bone table; map the result onto the host skeleton by bone name.
		for ( int i = 0; i < nSrcBones; ++i )
		{
			const mstudiobone_t *pIncBone = m_pIncludeHdr->pBone( i );
			const QString name = SafeStructString( m_bufIncludeMdl, pIncBone, pIncBone->sznameindex );
			const int j = FindBoneIndexByName( name );
			if ( j >= 0 && j < nBones )
				m_AnimBoneToWorld[ j ] = ToQt( world[ i ] );
		}
	}

	m_bAnimDataAvailable = SequenceDuration( m_nCurrentSequence ) > 0.0f;
	return m_AnimBoneToWorld;
}

//-----------------------------------------------------------------------------
// Purpose: ComputePoseToWorld() from studiorender/r_studio.cpp:
//          ConcatTransforms( BoneToWorld[i], poseToBone[i] ). The poseToBone
//          source is the mstudiolinearbone_t table when present, exactly as in
//          the engine. No viewmodel-specific adjustments belong here.
//-----------------------------------------------------------------------------
const QVector<QMatrix4x4> &CStudioModelData::GetSkinningMatrices()
{
	const QVector<QMatrix4x4> &bones = GetAnimatedBoneMatrices();
	m_SkinningMatrices.resize( bones.count() );

	QMatrix4x4 identity;
	for ( int i = 0; i < bones.count(); ++i )
		m_SkinningMatrices[ i ] = ( i < m_BindInverse.count() )
			? bones[ i ] * m_BindInverse[ i ] : identity;
	return m_SkinningMatrices;
}

//-----------------------------------------------------------------------------
// Purpose: Bone-name lookup used when attaching hands.
//-----------------------------------------------------------------------------
int CStudioModelData::FindBoneIndexByName( const QString &name ) const
{
	if ( !m_pHdr )
		return -1;
	for ( int i = 0; i < m_pHdr->numbones; ++i )
	{
		if ( GetBoneName( i ).compare( name, Qt::CaseInsensitive ) == 0 )
			return i;
	}
	return -1;
}

//-----------------------------------------------------------------------------
// Purpose: Bind-pose vertex bounds of this model.
//-----------------------------------------------------------------------------
QVector3D CStudioModelData::GetBindBoundsCenter() const
{
	if ( !m_pVvdVertices || m_nVvdVertexCount <= 0 )
		return QVector3D();
	Vector vMin( 1e9f, 1e9f, 1e9f ), vMax( -1e9f, -1e9f, -1e9f );
	for ( int i = 0; i < m_nVvdVertexCount; ++i )
	{
		const Vector &p = m_pVvdVertices[ i ].m_vecPosition;
		for ( int k = 0; k < 3; ++k )
		{
			vMin[ k ] = qMin( vMin[ k ], p[ k ] );
			vMax[ k ] = qMax( vMax[ k ], p[ k ] );
		}
	}
	return QVector3D( ( vMin.x + vMax.x ) * 0.5f,
		( vMin.y + vMax.y ) * 0.5f, ( vMin.z + vMax.z ) * 0.5f );
}

float CStudioModelData::GetBindBoundsExtent() const
{
	if ( !m_pVvdVertices || m_nVvdVertexCount <= 0 )
		return 0.0f;
	Vector vMin( 1e9f, 1e9f, 1e9f ), vMax( -1e9f, -1e9f, -1e9f );
	for ( int i = 0; i < m_nVvdVertexCount; ++i )
	{
		const Vector &p = m_pVvdVertices[ i ].m_vecPosition;
		for ( int k = 0; k < 3; ++k )
		{
			vMin[ k ] = qMin( vMin[ k ], p[ k ] );
			vMax[ k ] = qMax( vMax[ k ], p[ k ] );
		}
	}
	return ( vMax - vMin ).Length();
}

//-----------------------------------------------------------------------------
// Purpose: Merged ("hands") skinning. The original viewer caches the main
//          model's bone matrices and copies them into the merged model by
//          bone name (utils/hlmv/studio_render.cpp, SetUpBones(mergeBones)),
//          then plays the merged model on the same sequence. Reusing the
//          owner's skinning transforms gives exactly that, and keeps hands
//          and weapon deforming together.
//-----------------------------------------------------------------------------
QVector<QMatrix4x4> CStudioModelData::BuildMergedSkinningMatrices( CStudioModelData &owner )
{
	QVector<QMatrix4x4> out;
	if ( !m_pHdr )
		return out;

	// Weapon viewmodels and hands live in different pose spaces: c_357's
	// mesh sits around z=60 while c_arms_citizen's hands are near the origin.
	// Align the two spaces through a shared root bone (both are ValveBiped
	// rigs), then apply the owner's animated skinning delta. This is the
	// standalone equivalent of the engine's bone-merge cache.
	// Stock HLMV SetUpBones(mergeBones): the main model builds its bone
	// matrices first, and each merged-model bone copies the matrix of the
	// same-named bone from that cache. ComputePoseToWorld then composes it
	// with THIS model's poseToBone. No bounds alignment or rigid follow.
	const QVector<QMatrix4x4> &ownerBones = owner.GetAnimatedBoneMatrices();
	out.resize( m_pHdr->numbones );
	QMatrix4x4 identity;
	for ( int i = 0; i < m_pHdr->numbones; ++i )
	{
		const int j = owner.FindBoneIndexByName( GetBoneName( i ) );
		out[ i ] = ( j >= 0 && j < ownerBones.count() && i < m_BindInverse.count() )
			? ownerBones[ j ] * m_BindInverse[ i ] : identity;
	}
	return out;
}

//-----------------------------------------------------------------------------
// Purpose: World-space origin of an attachment under the given bone matrices.
//-----------------------------------------------------------------------------
bool CStudioModelData::GetAttachmentWorldTransform( int iAttach, const QVector<QMatrix4x4> &boneToWorld, Vec3 &outOrigin ) const
{
	if ( !m_pHdr || iAttach < 0 || iAttach >= m_pHdr->numlocalattachments )
		return false;

	const mstudioattachment_t *pAtt = m_pHdr->pLocalAttachment( iAttach );
	if ( pAtt->localbone < 0 || pAtt->localbone >= boneToWorld.count() )
		return false;

	// The attachment local matrix is stored as a matrix3x4_t (row-major).
	QMatrix4x4 localQ;
	for ( int r = 0; r < 3; ++r )
	{
		localQ.setRow( r, QVector4D( pAtt->local[ r ][ 0 ], pAtt->local[ r ][ 1 ], pAtt->local[ r ][ 2 ], pAtt->local[ r ][ 3 ] ) );
	}
	localQ.setRow( 3, QVector4D( 0.0f, 0.0f, 0.0f, 1.0f ) );

	QMatrix4x4 world = boneToWorld[ pAtt->localbone ] * localQ;
	QVector4D origin = world.column( 3 );
	outOrigin.x = origin.x();
	outOrigin.y = origin.y();
	outOrigin.z = origin.z();
	return true;
}

//-----------------------------------------------------------------------------
// Purpose: Loads the first $includemodel animation host. HL2/GMod hands
//          models (c_arms.mdl) have no mesh and no local sequences; their
//          animation library lives in c_arms_animations.mdl and is referenced
//          through the header's include-model table.
//-----------------------------------------------------------------------------
bool CStudioModelData::LoadIncludeModels( const QString &mdlPath )
{
	m_bufIncludeMdl.clear();
	m_pIncludeHdr = nullptr;
	if ( !m_pHdr || m_bufMdl.size() < 344 )
		return false;

	const int nNum = *(const int *)( m_bufMdl.constData() + 336 );
	const int nIndex = *(const int *)( m_bufMdl.constData() + 340 );
	if ( nNum <= 0 || nIndex <= 0 || !RangeOk( nIndex, 1, 8, m_bufMdl.size() ) )
		return false;

	const int nNameIndex = *(const int *)( m_bufMdl.constData() + nIndex + 4 );
	const qint64 nNameOffset = (qint64)nIndex + nNameIndex;
	if ( nNameOffset < 0 || nNameOffset >= m_bufMdl.size() )
		return false;

	QString rel = SafeStringAt( m_bufMdl, nNameOffset );
	rel.replace( QLatin1Char( '\\' ), QLatin1Char( '/' ) );

	// Reject absolute paths, drive letters and ".." segments: the include
	// path comes from an untrusted file and must stay under the game root.
	if ( rel.isEmpty() || rel.startsWith( QLatin1Char( '/' ) ) || rel.contains( QLatin1Char( ':' ) ) ||
		rel.split( QLatin1Char( '/' ) ).contains( QLatin1String( ".." ) ) )
		return false;

	// Include paths are game-relative ("models/..."). Resolve against the
	// game root by walking up to the "models" directory.
	QString root;
	QDir walk = QFileInfo( mdlPath ).absoluteDir();
	for ( int i = 0; i < 8; ++i )
	{
		if ( walk.dirName().compare( QLatin1String( "models" ), Qt::CaseInsensitive ) == 0 )
		{
			QDir r = walk;
			if ( r.cdUp() )
				root = r.absolutePath();
			break;
		}
		if ( !walk.cdUp() )
			break;
	}
	if ( root.isEmpty() )
		root = QFileInfo( mdlPath ).absoluteDir().absolutePath();

	QFile f( root + QLatin1Char( '/' ) + rel );
	if ( !f.open( QIODevice::ReadOnly ) )
		return false;
	m_bufIncludeMdl = f.readAll();
	if ( m_bufIncludeMdl.size() < (int)sizeof( studiohdr_t ) ||
		std::memcmp( m_bufIncludeMdl.constData(), "IDST", 4 ) != 0 )
	{
		m_bufIncludeMdl.clear();
		return false;
	}

	// The include header is used as a full studiohdr_t (CStudioHdr, sequence
	// and bone tables): validate everything those paths dereference.
	{
		const studiohdr_t *pInc = (const studiohdr_t *)m_bufIncludeMdl.constData();
		const qint64 nSize = m_bufIncludeMdl.size();
		const bool bOk = pInc->version >= 44 && pInc->version <= 49 &&
			pInc->numbones >= 0 && pInc->numbones <= 1024 &&
			( pInc->numbones == 0 || ( pInc->boneindex > 0 && RangeOk( pInc->boneindex, pInc->numbones, sizeof( mstudiobone_t ), nSize ) ) ) &&
			pInc->numlocalseq >= 0 && pInc->numlocalseq <= 65536 &&
			( pInc->numlocalseq == 0 || ( pInc->localseqindex > 0 && RangeOk( pInc->localseqindex, pInc->numlocalseq, sizeof( mstudioseqdesc_t ), nSize ) ) ) &&
			pInc->numlocalanim >= 0 && pInc->numlocalanim <= 65536 &&
			( pInc->numlocalanim == 0 || ( pInc->localanimindex > 0 && RangeOk( pInc->localanimindex, pInc->numlocalanim, sizeof( mstudioanimdesc_t ), nSize ) ) ) &&
			pInc->numlocalposeparameters >= 0 && pInc->numlocalposeparameters <= 65536 &&
			( pInc->numlocalposeparameters == 0 || ( pInc->localposeparamindex > 0 && RangeOk( pInc->localposeparamindex, pInc->numlocalposeparameters, sizeof( mstudioposeparamdesc_t ), nSize ) ) );
		if ( !bOk )
		{
			m_bufIncludeMdl.clear();
			return false;
		}
	}
	m_pIncludeHdr = (studiohdr_t *)m_bufIncludeMdl.data();
	return true;
}

//-----------------------------------------------------------------------------
// Purpose: Resolves a merged sequence index to its owning header and the
//          local sequence index inside that header.
//-----------------------------------------------------------------------------
const studiohdr_t *CStudioModelData::SequenceHeader( int iSeq, int *pLocalSeq ) const
{
	if ( pLocalSeq )
		*pLocalSeq = -1;
	if ( !m_pHdr || iSeq < 0 )
		return nullptr;
	if ( iSeq < m_pHdr->numlocalseq )
	{
		if ( pLocalSeq )
			*pLocalSeq = iSeq;
		return m_pHdr;
	}
	if ( m_pIncludeHdr )
	{
		const int nLocal = iSeq - m_pHdr->numlocalseq;
		if ( nLocal >= 0 && nLocal < m_pIncludeHdr->numlocalseq )
		{
			if ( pLocalSeq )
				*pLocalSeq = nLocal;
			return m_pIncludeHdr;
		}
	}
	return nullptr;
}
