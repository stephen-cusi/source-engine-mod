//========= Half-Life Model Viewer 2 (Qt6 port of utils/hlmv) ================
//
// Purpose: --selftest implementation. Steps:
//            load        - parse the .mdl header and peer files
//            sequences   - sequence table is readable and non-empty
//            bones       - bone table is readable with a valid parent chain
//            bodygroups  - bodypart/model tables are readable
//            materials   - texture table is readable and skin remaps work
//            vtf-textures- VMT/VTF resolve + decode via loose files or VPK
//            attachments - attachment table resolves to world positions
//            physmesh    - .phy header + key-value block parsed (optional)
//            skinning    - skinning transforms are finite and bounded
//            animation   - playing frames keeps bone matrices well formed
//            render-init - GL viewport renders one frame and reports geometry
//                          bounds; HLMV2_FRAME_OUT=<path> also dumps a PNG
//
//=============================================================================

#include "selftest.h"

#include "mainwindow.h"
#include "materialloader.h"
#include "studiodata.h"
#include "viewport.h"

#include <QCoreApplication>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QTemporaryDir>
#include <QtGlobal>

#include <climits>
#include <cmath>
#include <cstdio>
#include <cstring>

namespace
{
	//-----------------------------------------------------------------------------
	// Purpose: Pass/fail bookkeeping with one printed line per step.
	//-----------------------------------------------------------------------------
	struct SelfTestContext
	{
		int nPassed = 0;
		int nFailed = 0;

		void Step( const char *step, bool bOk, const QString &detail = QString() )
		{
			std::printf( "SELFTEST: %-22s %s %s\n", step, bOk ? "PASS" : "FAIL",
				detail.toUtf8().constData() );
			std::fflush( stdout );

			if ( bOk )
				++nPassed;
			else
				++nFailed;
		}
	};
}

namespace
{
	//-----------------------------------------------------------------------------
	// Purpose: Malformed-input helpers. A model's .mdl/.vvd/.vtx are copied (in
	//          memory) into a temp dir with selected count/offset fields
	//          patched, then loaded; the parser must reject or tolerate them
	//          without crashing.
	//-----------------------------------------------------------------------------
	struct MalformedSet_t
	{
		QByteArray mdl, vvd, vtx;
		QString vtxSuffix;
	};

	struct MalformedResult_t
	{
		bool bSetupOk = false;
		bool bLoaded = false;
		QString sError;
		QString sMissing;
		int nMeshes = 0;
	};

	bool SelfTestReadAll( const QString &path, QByteArray &out )
	{
		QFile f( path );
		if ( !f.open( QIODevice::ReadOnly ) )
			return false;
		out = f.readAll();
		return true;
	}

	bool SelfTestWriteAll( const QString &path, const QByteArray &data )
	{
		QFile f( path );
		if ( !f.open( QIODevice::WriteOnly | QIODevice::Truncate ) )
			return false;
		const bool bOk = f.write( data ) == data.size();
		f.close();
		return bOk;
	}

	void PatchInt( QByteArray &buf, qint64 off, int value )
	{
		if ( off < 0 || off + 4 > buf.size() )
			return;
		std::memcpy( buf.data() + off, &value, sizeof( value ) );
	}

	MalformedResult_t LoadMalformed( const MalformedSet_t &set )
	{
		MalformedResult_t res;
		QTemporaryDir dir;
		if ( !dir.isValid() )
			return res;

		const QString base = dir.path() + QStringLiteral( "/mal" );
		if ( !SelfTestWriteAll( base + QStringLiteral( ".mdl" ), set.mdl ) )
			return res;
		if ( !set.vvd.isEmpty() && !SelfTestWriteAll( base + QStringLiteral( ".vvd" ), set.vvd ) )
			return res;
		if ( !set.vtx.isEmpty() && !SelfTestWriteAll( base + set.vtxSuffix, set.vtx ) )
			return res;
		res.bSetupOk = true;

		CStudioModelData model;
		res.bLoaded = model.LoadFromFile( base + QStringLiteral( ".mdl" ), res.sError );
		if ( res.bLoaded )
		{
			res.sMissing = model.GetMissingPeers();
			res.nMeshes = model.GetMeshCount();
		}
		return res;
	}

	// Purpose: Runs the malformed-input cases against the model at mdlPath.
	void RunMalformedTests( SelfTestContext &ctx, const QString &mdlPath )
	{
		MalformedSet_t orig;
		const QFileInfo info( mdlPath );
		const QString base = info.canonicalPath() + QLatin1Char( '/' ) + info.completeBaseName();
		if ( !SelfTestReadAll( mdlPath, orig.mdl ) || orig.mdl.size() < (int)sizeof( studiohdr_t ) )
		{
			ctx.Step( "malformed-mdl", true, QStringLiteral( "skipped (cannot read mdl)" ) );
			return;
		}
		SelfTestReadAll( base + QStringLiteral( ".vvd" ), orig.vvd );
		static const char *const suffixes[] = { ".dx90.vtx", ".dx80.vtx", ".sw.vtx" };
		for ( const char *suffix : suffixes )
		{
			if ( SelfTestReadAll( base + QLatin1String( suffix ), orig.vtx ) )
			{
				orig.vtxSuffix = QLatin1String( suffix );
				break;
			}
			orig.vtx.clear();
		}

		// --- mdl header fields: every case must be rejected ---
		{
			struct Case_t { const char *pszName; int nWhich; };
			const Case_t cases[] = {
				{ "numbones=INT_MAX", 0 }, { "boneindex=huge", 1 }, { "bodypartindex=huge", 2 },
				{ "numcdtextures=huge", 3 }, { "numlocalseq=-5", 4 }, { "bodypart.nummodels=huge", 5 },
			};
			int nBad = 0;
			QString sDetail;
			for ( const Case_t &c : cases )
			{
				MalformedSet_t set = orig;
				set.vvd.clear();
				set.vtx.clear();
				studiohdr_t *pHdr = (studiohdr_t *)set.mdl.data();
				bool bApplied = true;
				switch ( c.nWhich )
				{
				case 0: pHdr->numbones = INT_MAX; break;
				case 1: pHdr->boneindex = INT_MAX - 16; break;
				case 2: pHdr->numbodyparts = 4; pHdr->bodypartindex = INT_MAX - 16; break;
				case 3: pHdr->numcdtextures = 0x40000000; break;
				case 4: pHdr->numlocalseq = -5; break;
				case 5:
					if ( pHdr->numbodyparts > 0 && pHdr->bodypartindex > 0 &&
						(qint64)pHdr->bodypartindex + (qint64)sizeof( mstudiobodyparts_t ) <= set.mdl.size() )
						pHdr->pBodypart( 0 )->nummodels = INT_MAX;
					else
						bApplied = false;
					break;
				}
				if ( !bApplied )
					continue;
				const MalformedResult_t res = LoadMalformed( set );
				if ( !res.bSetupOk )
					continue;
				if ( res.bLoaded )
				{
					++nBad;
					sDetail += QStringLiteral( " [%1 accepted]" ).arg( QLatin1String( c.pszName ) );
				}
			}
			ctx.Step( "malformed-mdl", nBad == 0, nBad == 0 ? QStringLiteral( "all corrupted headers rejected" ) : sDetail );
		}

		// --- truncated files: must not crash (either outcome is fine) ---
		{
			MalformedSet_t set = orig;
			set.mdl.truncate( orig.mdl.size() / 2 );
			const MalformedResult_t res = LoadMalformed( set );
			ctx.Step( "malformed-truncated", true,
				res.bLoaded ? QStringLiteral( "loaded safely" ) : QStringLiteral( "rejected: %1" ).arg( res.sError ) );
		}

		// --- vvd: corrupted peer must be dropped (never read out of range) ---
		if ( orig.vvd.size() < 64 )
		{
			ctx.Step( "malformed-vvd", true, QStringLiteral( "skipped (no .vvd peer)" ) );
		}
		else
		{
			int nBad = 0;
			for ( int which = 0; which < 4; ++which )
			{
				MalformedSet_t set = orig;
				set.vtx.clear();
				vertexFileHeader_t *pVvd = (vertexFileHeader_t *)set.vvd.data();
				if ( which == 0 )
				{
					pVvd->numLODVertexes[ 0 ] = INT_MAX;
				}
				else if ( which == 1 )
				{
					pVvd->vertexDataStart = INT_MAX - 16;
				}
				else if ( which == 2 )
				{
					pVvd->numFixups = INT_MAX;
				}
				else
				{
					if ( pVvd->numFixups <= 0 || pVvd->fixupTableStart <= 0 ||
						(qint64)pVvd->fixupTableStart + (qint64)sizeof( vertexFileFixup_t ) > set.vvd.size() )
						continue;
					( (vertexFileFixup_t *)( set.vvd.data() + pVvd->fixupTableStart ) )->sourceVertexID = INT_MAX;
				}
				const MalformedResult_t res = LoadMalformed( set );
				if ( res.bSetupOk && res.bLoaded && !res.sMissing.contains( QLatin1String( ".vvd" ) ) )
					++nBad;
			}
			// Truncated vvd as well (no crash expected).
			{
				MalformedSet_t set = orig;
				set.vtx.clear();
				set.vvd.truncate( orig.vvd.size() / 2 );
				LoadMalformed( set );
			}
			ctx.Step( "malformed-vvd", nBad == 0, nBad == 0 ? QStringLiteral( "corrupted .vvd rejected" ) : QStringLiteral( "%1 case(s) accepted" ).arg( nBad ) );
		}

		// --- vtx ---
		if ( orig.vtx.size() < 36 )
		{
			ctx.Step( "malformed-vtx", true, QStringLiteral( "skipped (no .vtx peer)" ) );
		}
		else
		{
			int nBad = 0;
			for ( int which = 0; which < 3; ++which )
			{
				MalformedSet_t set = orig;
				if ( which == 0 )
				{
					PatchInt( set.vtx, 28, INT_MAX );                       // numBodyParts
				}
				else if ( which == 1 )
				{
					PatchInt( set.vtx, 28, 1 );                             // numBodyParts
					PatchInt( set.vtx, 32, INT_MAX - 16 );                  // bodyPartOffset
				}
				else
				{
					int nBpOff = 0;
					std::memcpy( &nBpOff, set.vtx.constData() + 32, sizeof( nBpOff ) );
					if ( nBpOff <= 0 )
						continue;
					PatchInt( set.vtx, nBpOff, INT_MAX );                   // bodypart[0].numModels
				}
				const MalformedResult_t res = LoadMalformed( set );
				if ( res.bSetupOk && res.bLoaded &&
					( !res.sMissing.contains( QLatin1String( ".vtx" ) ) || res.nMeshes != 0 ) )
					++nBad;
			}
			// Truncated vtx as well (no crash expected).
			{
				MalformedSet_t set = orig;
				set.vtx.truncate( orig.vtx.size() / 2 );
				LoadMalformed( set );
			}
			ctx.Step( "malformed-vtx", nBad == 0, nBad == 0 ? QStringLiteral( "corrupted .vtx rejected" ) : QStringLiteral( "%1 case(s) accepted" ).arg( nBad ) );
		}
	}
}

//-----------------------------------------------------------------------------
// Purpose: Runs the whole self-test pipeline against one .mdl file.
//-----------------------------------------------------------------------------
int RunHlmvSelfTest( const QString &mdlPath )
{
	SelfTestContext ctx;

	QCoreApplication *pApp = QCoreApplication::instance();
	const QString appDir = pApp ? pApp->applicationDirPath() : QString();

	// Headless GL: must happen before any widget exists.
#ifdef _WIN32
	// The "windows" platform needs no forcing: a desktop session is always
	// present here and QOpenGLWidget renders into its own FBO; the transient
	// FBO path also works for widgets that are never exposed.
#else
	// Unix/CI: force the offscreen platform when there is no X display so
	// QOpenGLWidget still gets a usable context.
	if ( qEnvironmentVariableIsEmpty( "DISPLAY" ) && qEnvironmentVariableIsEmpty( "QT_QPA_PLATFORM" ) )
	{
		qputenv( "QT_QPA_PLATFORM", "offscreen" );
	}
#endif

	// --- load ---
	QString sError;
	CStudioModelData *pModel = new CStudioModelData();
	bool bLoaded = pModel->LoadFromFile( mdlPath, sError );
	ctx.Step( "load", bLoaded, bLoaded
			? QStringLiteral( "%1 version %2 checksum 0x%3 peers missing: %4" )
				.arg( pModel->GetFileName() )
				.arg( pModel->GetVersion() )
				.arg( ( uint )pModel->GetChecksum(), 8, 16, QChar( '0' ) )
				.arg( pModel->GetMissingPeers().isEmpty() ? QStringLiteral( "none" ) : pModel->GetMissingPeers() )
		: sError );

	if ( !bLoaded )
	{
		delete pModel;
		return 1;
	}

	const bool bBindOnly = !qgetenv( "HLMV2_BIND_ONLY" ).isEmpty();
	if ( bBindOnly )
		pModel->SetBindPoseOnly();

	// --- sequences ---
	const int nSeqs = pModel->GetSequenceCount();
	{
		QString sDetail;
		bool bOk = nSeqs > 0;
		if ( bOk )
		{
			QString activity;
			float fps;
			int numFrames;
			bool looping;
			int numBlends;
			QString label = pModel->GetSequenceLabel( 0, &activity, &fps, &numFrames, &looping, &numBlends );
			bOk = !label.isEmpty() && numFrames > 0;
			// HasAnimationData() was primed by the load-time frame 0 decode:
			// inline anim records vs external .ani animblock (bind pose only).
			const char *animKind = pModel->HasAnimationData() ? "inline-anim" : "external-animblock";
			sDetail = QStringLiteral( "%1 sequences; first=\"%2\" fps=%3 frames=%4 %5" )
				.arg( nSeqs ).arg( label ).arg( fps, 0, 'f', 1 ).arg( numFrames )
				.arg( QLatin1String( animKind ) );
		}
		else
		{
			sDetail = QStringLiteral( "no local sequences" );
		}
		ctx.Step( "sequences", bOk, sDetail );
	}

	// --- bones ---
	const int nBones = pModel->GetBoneCount();
	{
		QString sDetail;
		bool bOk = nBones > 0;
		if ( bOk )
		{
			// Every parent must reference an earlier (or self) bone index.
			bool bChainValid = true;
			QString sFirst;
			for ( int i = 0; i < nBones; ++i )
			{
				int iParent = -1;
				QString name = pModel->GetBoneName( i, &iParent );
				if ( i == 0 )
					sFirst = name;
				if ( iParent >= i || iParent >= nBones )
					bChainValid = false;
			}
			bOk = bChainValid && !sFirst.isEmpty();
			sDetail = QStringLiteral( "%1 bones; root=\"%2\"" ).arg( nBones ).arg( sFirst );
		}
		else
		{
			sDetail = QStringLiteral( "no bones (single-bone prop?) bonecount=%1" ).arg( nBones );
			// A zero-bone model is still a loadable model; treat as pass.
			bOk = ( nBones == 0 );
		}
		ctx.Step( "bones", bOk, sDetail );
	}

	// --- bodygroups ---
	const int nParts = pModel->GetBodypartCount();
	{
		QString sDetail;
		bool bOk = nParts > 0;
		if ( bOk )
		{
			int numModels = 0;
			QString name = pModel->GetBodypartName( 0, &numModels );
			QString sub = pModel->GetModelName2( 0, 0 );
			bOk = numModels > 0;
			sDetail = QStringLiteral( "%1 bodyparts; first=\"%2\" (%3 variants, sub=\"%4\")" )
				.arg( nParts ).arg( name ).arg( numModels ).arg( sub );
		}
		else
		{
			sDetail = QStringLiteral( "no bodyparts" );
		}
		ctx.Step( "bodygroups", bOk, sDetail );
	}

	// --- materials ---
	const int nTextures = pModel->GetTextureCount();
	{
		QString sDetail;
		// Animation hosts such as c_arms.mdl legitimately have no materials.
		bool bOk = true;
		if ( nTextures > 0 )
		{
			QString first = pModel->GetTextures().first().name;
			// Skin remap must stay inside the texture range for family 0.
			bool bSkinSane = true;
			for ( int i = 0; i < pModel->GetSkinRefs(); ++i )
			{
				int nTex = pModel->GetMeshMaterial( i );
				if ( nTex < 0 || nTex >= nTextures )
				{
					bSkinSane = false;
					break;
				}
			}
			bOk = bSkinSane;
			sDetail = QStringLiteral( "%1 textures; first=\"%2\"; skin remaps in range" )
				.arg( nTextures ).arg( first );
		}
		else
		{
			sDetail = QStringLiteral( "no textures (bone/animation host, informational)" );
		}
		ctx.Step( "materials", bOk, sDetail );
	}

	// --- vtf-textures ---
	// Exercises the cross-platform material chain without any GL context:
	// loose files first, then VPK v1/v2 archives, VMT -> VTF -> QImage.
	{
		CMaterialLoader loader;
		loader.SetModelPath( mdlPath );
		const QVector<TextureInfo_t> &textures = pModel->GetTextures();
		int nResolved = 0;
		int nTintable = 0;
		QString sFirst;
		for ( const TextureInfo_t &tex : textures )
		{
			QImage img;
			QString sStatus;
			bool bTintable = false;
			if ( !loader.LoadTextureImage( tex.name, pModel->GetCdTextures(),
				img, sStatus, &bTintable ) )
				continue;
			++nResolved;
			if ( bTintable )
				++nTintable;
			if ( sFirst.isEmpty() )
				sFirst = QStringLiteral( "%1 %2x%3" ).arg( tex.name ).arg( img.width() ).arg( img.height() );
		}
		// Missing game content is not a viewer bug: only fail when content is
		// actually available (archives were indexed) but nothing decoded.
		const bool bContentAvailable = loader.GetIndexedFileCount() > 0;
		const bool bOk = ( textures.isEmpty() ) || ( nResolved > 0 ) || !bContentAvailable;
		ctx.Step( "vtf-textures", bOk,
			QStringLiteral( "%1/%2 decoded, %3 tintable, %4 VPK entries indexed, first=%5" )
				.arg( nResolved ).arg( textures.count() ).arg( nTintable )
				.arg( loader.GetIndexedFileCount() )
				.arg( sFirst.isEmpty() ? QStringLiteral( "none (content not available)" ) : sFirst ) );
	}

	// --- vmt colour detection ---
	// HLMV2_VMT_CHECK=<path to a .vmt> verifies the PlayerColor / $color
	// detection against real material files (e.g. hl2sb's v_hand_sheet.vmt).
	{
		const QByteArray vmtPath = qgetenv( "HLMV2_VMT_CHECK" );
		if ( !vmtPath.isEmpty() )
		{
			QFile f( QString::fromLocal8Bit( vmtPath ) );
			bool bOk = f.open( QIODevice::ReadOnly );
			QString sDetail;
			if ( bOk )
			{
				const VmtColorInfo_t info = InspectVmtColor( f.readAll() );
				sDetail = QStringLiteral( "tintable=%1 default=(%2,%3,%4)" )
					.arg( info.bTintable ? 1 : 0 )
					.arg( info.defaultTint.x(), 0, 'f', 3 )
					.arg( info.defaultTint.y(), 0, 'f', 3 )
					.arg( info.defaultTint.z(), 0, 'f', 3 );
			}
			else
			{
				sDetail = QStringLiteral( "cannot read %1" ).arg( QString::fromLocal8Bit( vmtPath ) );
			}
			ctx.Step( "vmt-color", bOk, sDetail );
		}
	}

	// --- attachments ---
	const int nAttachments = pModel->GetAttachmentCount();
	{
		QString sDetail;
		bool bOk = true; // attachments are optional on many props
		int nResolved = 0;
		const QVector<QMatrix4x4> &bones = pModel->GetSetupBoneMatrices();
		for ( int i = 0; i < nAttachments; ++i )
		{
			CStudioModelData::Vec3 origin{ 0.0f, 0.0f, 0.0f };
			if ( pModel->GetAttachmentWorldTransform( i, bones, origin ) )
				++nResolved;
		}
		sDetail = QStringLiteral( "%1 attachments, %2 resolved to world coords" )
			.arg( nAttachments ).arg( nResolved );
		ctx.Step( "attachments", bOk && nResolved == nAttachments, sDetail );
	}

	// --- physmesh ---
	{
		const PhysicsSummary_t &phys = pModel->GetPhysicsSummary();
		QString sDetail;
		// .phy is optional: most viewmodels ship without collision data.
		const bool bOk = true;
		if ( phys.bLoaded )
		{
			bool bChecksumOk = ( phys.nChecksum == (uint32_t)pModel->GetChecksum() );
			sDetail = QStringLiteral( "%1 solids, checksum %2, mass %3, surf \"%4\"%5" )
				.arg( phys.nSolidCount )
				.arg( bChecksumOk ? QStringLiteral( "match" ) : QStringLiteral( "MISMATCH" ) )
				.arg( phys.flMass )
				.arg( phys.surfaceprop )
				.arg( bChecksumOk ? QString() : QStringLiteral( " (phy is stale or swapped)" ) );
		}
		else
		{
			sDetail = QStringLiteral( "no .phy (optional for viewmodels, informational)" );
		}
		ctx.Step( "physmesh", bOk, sDetail );
	}

	// --- skinning sanity ---
	// Skinning transforms must stay finite and their translations bounded:
	// a runaway translation is what tears a playing viewmodel apart.
	{
		float flMaxT = 0.0f;
		int nBad = 0;
		const QVector<QMatrix4x4> &mats = pModel->GetSkinningMatrices();
		for ( const QMatrix4x4 &mat : mats )
		{
			const float *f = mat.constData();
			for ( int k = 0; k < 16; ++k )
				if ( !std::isfinite( f[ k ] ) )
					++nBad;
			const QVector3D t( mat( 0, 3 ), mat( 1, 3 ), mat( 2, 3 ) );
			flMaxT = qMax( flMaxT, t.length() );
		}
		ctx.Step( "skinning", nBad == 0 && flMaxT < 10000.0f,
			QStringLiteral( "%1 bones, %2 non-finite, max |translation|=%3" )
				.arg( mats.count() ).arg( nBad ).arg( flMaxT, 0, 'f', 1 ) );
	}

	// --- animation ---
	// Play a few frames and verify the skinning matrices stay finite and
	// bounded. A broken anim decode produces NaN/astronomic transforms, which
	// throws the mesh out of the frustum while the bone lines stretch away.
	{
		if ( bBindOnly )
			pModel->SetBindPoseOnly();
		else
		{
			pModel->SetCurrentSequence( 0 );
			pModel->SetPaused( false );
		}
		int nBad = 0;
		float flMaxAbs = 0.0f;
		const QByteArray stepsEnv = qgetenv( "HLMV2_ANIM_STEPS" );
		const int nSteps = stepsEnv.isEmpty() ? 8 : stepsEnv.toInt();
		for ( int step = 0; step < nSteps; ++step )
		{
			pModel->AdvanceAnimation( 0.137f );
			const QVector<QMatrix4x4> &mats = pModel->GetSkinningMatrices();
			for ( const QMatrix4x4 &mat : mats )
			{
				const float *f = mat.constData();
				for ( int k = 0; k < 16; ++k )
				{
					if ( !std::isfinite( f[ k ] ) )
					{
						++nBad;
						continue;
					}
					flMaxAbs = qMax( flMaxAbs, qAbs( f[ k ] ) );
				}
			}
		}
		const bool bOk = ( nBad == 0 && flMaxAbs < 100000.0f );
		ctx.Step( "animation", bOk,
			QStringLiteral( "%1 frames, %2 non-finite, max |element|=%3" )
				.arg( nSteps ).arg( nBad ).arg( flMaxAbs, 0, 'f', 1 ) );
	}

	// --- render-init ---
	{
		// Route B2: own Qt OpenGL renderer. PASS here means the GL context and
		// shader pipeline came up and one frame was rendered. Triangles are
		// drawn from the CPU-skinned corner stream; material textures are
		// uploaded by CModelViewport::SetMaterialImages when available.
		QString sDetail;
		bool bOk = false;

		if ( pApp )
		{
			CStudioModelData *pHands = nullptr;
			{
				// The viewport is never shown: QOpenGLWidget renders into a
				// transient FBO for unexposed widgets, so the whole pipeline
				// runs without a visible window on the desktop session.
				CModelViewport viewport;
				viewport.SetModel( pModel );
				viewport.resize( 900, 600 );

				// Load the model's own materials so texture and tint paths are
				// covered, not just the geometry.
				{
					CMaterialLoader loader;
					loader.SetModelPath( mdlPath );
					const QVector<TextureInfo_t> &tex = pModel->GetTextures();
					QVector<QImage> images( tex.count() );
					QVector<bool> tintable( tex.count(), false );
					QVector3D defaultTint( 1.0f, 1.0f, 1.0f );
					// HLMV2_TINT_FORCE=1 tints every slot, which exercises the
					// shader path even when the content has no PlayerColor proxy.
					const bool bForceTint = !qgetenv( "HLMV2_TINT_FORCE" ).isEmpty();
					for ( int i = 0; i < tex.count(); ++i )
					{
						QString sStatus;
						bool bTint = false;
						QVector3D tintDefault( 1.0f, 1.0f, 1.0f );
						loader.LoadTextureImage( tex[ i ].name,
							pModel->GetCdTextures(), images[ i ], sStatus, &bTint, &tintDefault );
						tintable[ i ] = bTint || bForceTint;
						if ( bTint )
							defaultTint = tintDefault;
					}
					viewport.SetMaterialImages( images );
					viewport.SetTintableSlots( tintable );
					if ( defaultTint != QVector3D( 1.0f, 1.0f, 1.0f ) )
						viewport.SetTintColor( defaultTint );

					// HLMV2_TINT="r,g,b" exercises the player-colour path.
					const QByteArray tint = qgetenv( "HLMV2_TINT" );
					if ( !tint.isEmpty() )
					{
						const QStringList parts = QString::fromLatin1( tint ).split( QLatin1Char( ',' ) );
						if ( parts.count() >= 3 )
						{
							viewport.SetTintColor( QVector3D(
								parts[ 0 ].toFloat(), parts[ 1 ].toFloat(), parts[ 2 ].toFloat() ) );
							sDetail += QStringLiteral( " tint=%1" ).arg( tint );
						}
					}
				}

				// HLMV2_HANDS=<path> exercises the bone-merge path offline.
				const QByteArray handsPath = qgetenv( "HLMV2_HANDS" );
				if ( !handsPath.isEmpty() )
				{
					const QString sPath = QString::fromLocal8Bit( handsPath );
					QString sError;
					pHands = new CStudioModelData();
					if ( pHands->LoadFromFile( sPath, sError ) )
					{
						CMaterialLoader handsLoader;
						handsLoader.SetModelPath( sPath );
						const QVector<TextureInfo_t> &tex = pHands->GetTextures();
						QVector<QImage> images( tex.count() );
						for ( int i = 0; i < tex.count(); ++i )
						{
							QString sStatus;
							handsLoader.LoadTextureImage( tex[ i ].name,
								pHands->GetCdTextures(), images[ i ], sStatus );
						}
						viewport.SetHandsMaterialImages( images );
						viewport.SetHandsModel( pHands );

						int nMatched = 0;
						for ( int i = 0; i < pHands->GetBoneCount(); ++i )
						{
							int iParent = -1;
							if ( pModel->FindBoneIndexByName(
								pHands->GetBoneName( i, &iParent ) ) >= 0 )
								++nMatched;
						}
						sDetail = QStringLiteral( " hands=%1 (%2/%3 bones matched)" )
							.arg( QFileInfo( sPath ).fileName() )
							.arg( nMatched ).arg( pHands->GetBoneCount() );
					}
					else
					{
						sDetail = QStringLiteral( " hands=LOAD FAILED: %1" ).arg( sError );
						delete pHands;
						pHands = nullptr;
					}
				}

				bOk = viewport.RenderOneFrameForSelfTest();
				sDetail = ( bOk ? viewport.GetRenderDiagnostics()
					: QStringLiteral( "gl context/pipeline failed" ) ) + sDetail;

				// Optional offline frame dump (HLMV2_FRAME_OUT=<path>). Keeps
				// the default self-test side-effect free.
				const QByteArray shotPath = qgetenv( "HLMV2_FRAME_OUT" );
				if ( !shotPath.isEmpty() )
				{
					const QImage frame = viewport.GrabFrame();
					const bool bSaved = frame.save( QString::fromLocal8Bit( shotPath ) );
					sDetail += QStringLiteral( " shot=%1x%2 saved=%3" )
						.arg( frame.width() ).arg( frame.height() ).arg( bSaved );
				}
			}
			delete pHands;
		}
		ctx.Step( "render-init", bOk, sDetail );
	}

	delete pModel;

	RunMalformedTests( ctx, mdlPath );

	std::printf( "SELFTEST: %-22s %d passed, %d failed\n", "summary", ctx.nPassed, ctx.nFailed );
	std::fflush( stdout );

	return ctx.nFailed == 0 ? 0 : 1;
}
