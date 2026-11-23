//========= Hammer Qt6 (hammer2/app) ==========================================
//
// Purpose: --selftest implementation. Runs without any window: it only needs
//          the (already constructed) QApplication for path handling. See the
//          header for the step list and output format.
//
//=============================================================================

#include "selftest.h"

#include "document.h"
#include "displacementinspect.h"
#include "displacementedit.h"
#include "displacementmeshtest.h"
#include "displacementsewtest.h"
#include "groupinspect.h"
#include "corebounds.h"
#include "entityreportdialog.h"
#include "entitypropertyrows.h"
#include "entityoutputsdialog.h"
#include "facepropertiesdialog.h"
#include "facehit.h"
#include "fgddata.h"
#include "gameconfig.h"
#include "localization.h"
#include "mapmodel.h"
#include "overlaydata.h"
#include "brushwindings.h"
#include "mapcheck.h"
#include "mapcheckgoto.h"
#include "gridsettings.h"
#include "materialcatalog.h"
#include "materialpreviewrenderer.h"
#include "decalprojection.h"
#include "overlayprojection.h"
#include "materialbrowserpreview.h"
#include "mappropertiesdialog.h"
#include "mainwindow.h"
#include "pointfilestate.h"
#include "pastespecialsettings.h"
#include "portableconfig.h"
#include "overlaydata.h"
#include "propertyvaluedelegate.h"
#include "recovery.h"
#include "tier1/tokenreader.h"
#include "selectioncenter.h"
#include "savedcameras.h"
#include "theme.h"
#include "navigation.h"
#include "viewpick.h"
#include "viewport2d.h"
#include "viewport3d.h"
#include "vmfio.h"
#include "cordon.h"
#include "visgroupdialog.h"

#include <QCoreApplication>
#include <QEventLoop>
#include <QApplication>
#include <QAction>
#include <QComboBox>
#include <QStyleOptionViewItem>
#include <QKeySequence>
#include <QMenu>
#include <QMessageBox>
#include <QDialogButtonBox>
#include <QPushButton>
#include <QTimer>
#include <QCheckBox>
#include <QCompleter>
#include <QLabel>
#include <QListWidget>
#include <QLineEdit>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QFocusEvent>
#include <QImage>
#include <QFontMetrics>
#include <QKeyEvent>
#include <QMouseEvent>
#include <QWheelEvent>
#include <QtOpenGLWidgets/QOpenGLWidget>
#include <QFocusEvent>
#include <QSettings>
#include <QSignalBlocker>
#include <QSplitter>
#include <QTableWidget>
#include <QToolBar>
#include <QToolButton>
#include <QItemSelectionModel>
#include <QTreeWidget>
#include <QTemporaryDir>
#include <QtGlobal>

#include <cmath>
#include <limits>
#include <optional>
#include <cstring>
#include <cstdio>
#include <vector>

#ifdef _WIN32
#include <windows.h>
#endif

#ifdef HAMMER_GMOD_COMPAT
#include "vstdlib/IKeyValuesSystem.h"
bool HammerGmodCommandLineSelfTest();

namespace
{
QString LoadedModulePath( const wchar_t *moduleName )
{
	const HMODULE module = GetModuleHandleW( moduleName );
	if ( !module ) return QString();
	std::vector<wchar_t> path( 32768 );
	const DWORD length = GetModuleFileNameW( module, path.data(), static_cast<DWORD>( path.size() ) );
	if ( length == 0 || length >= path.size() ) return QString();
	return QDir::cleanPath( QString::fromWCharArray( path.data(), static_cast<int>( length ) ) );
}

bool ModuleIsInDirectory( const QString &modulePath, const QString &directory )
{
	return !modulePath.isEmpty() && QFileInfo( modulePath ).absolutePath().compare(
		QDir( directory ).absolutePath(), Qt::CaseInsensitive ) == 0;
}
}
#endif

namespace
{
	//-----------------------------------------------------------------------------
	// Purpose: Minimal built-in FGD used when the game config points at an FGD
	//          that is not present on disk. Defines the classes the self-test
	//          places: light, info_player_start, prop_static.
	//-----------------------------------------------------------------------------
	const char k_builtinFgd[] =
		"// Built-in fallback FGD for the hammer_qt self-test\n"
		"@mapsize(-16384, 16384)\n"
		"\n"
		"@BaseClass = Targetname\n"
		"[\n"
		"\ttargetname(target_source) : \"Name\" : : \"Variable name for targeting this entity\"\n"
		"]\n"
		"\n"
		"@PointClass base(Targetname) = light :\n"
		"\t\"Invisible light source.\"\n"
		"[\n"
		"\t_light(color255) : \"Brightness\" : \"255 255 255 200\" : \"Light color.\"\n"
		"]\n"
		"\n"
		"@PointClass base(Targetname) = info_player_start :\n"
		"\t\"Player spawn point.\"\n"
		"[\n"
		"]\n"
		"\n"
		"@PointClass base(Targetname) = prop_static :\n"
		"\t\"Static prop.\"\n"
		"[\n"
		"\tmodel(studio) : \"World Model\" : \"\" : \"Model to use.\"\n"
		"]\n";

	//-----------------------------------------------------------------------------
	// Purpose: Pass/fail bookkeeping with one printed line per step.
	//-----------------------------------------------------------------------------
	struct SelfTestContext
	{
		int nPassed;
		int nFailed;
		QFile logFile;

		SelfTestContext()
			: nPassed( 0 )
			, nFailed( 0 )
		{
		}

		void Step( const char *step, bool bOk, const QString &detail = QString() )
		{
			const QByteArray line = QStringLiteral( "SELFTEST: %1 %2 %3\n" )
				.arg( QString::fromLatin1( step ).leftJustified( 22, QLatin1Char( ' ' ) ) )
				.arg( bOk ? QLatin1String( "PASS" ) : QLatin1String( "FAIL" ), detail )
				.toUtf8();
			std::fwrite( line.constData(), 1, static_cast<size_t>( line.size() ), stdout );
			std::fflush( stdout );
			if ( logFile.isOpen() )
			{
				logFile.write( line );
				logFile.flush();
			}

			if ( bOk )
				++nPassed;
			else
				++nFailed;
		}
	};

	//-----------------------------------------------------------------------------
	// Purpose: Compares two key/value maps (order-independent).
	//-----------------------------------------------------------------------------
	bool KeyValuesEqual( const QMap<QString, QString> &a, const QMap<QString, QString> &b,
		QString *mismatch )
	{
		if ( a.count() != b.count() )
		{
			if ( mismatch )
				*mismatch = QStringLiteral( "kv count %1 != %2" ).arg( a.count() ).arg( b.count() );
			return false;
		}

		for ( auto it = a.constBegin(); it != a.constEnd(); ++it )
		{
			if ( !b.contains( it.key() ) || b.value( it.key() ) != it.value() )
			{
				if ( mismatch )
					*mismatch = QStringLiteral( "key '%1': '%2' vs '%3'" )
						.arg( it.key(), it.value(), b.value( it.key() ) );
				return false;
			}
		}
		return true;
	}

	//-----------------------------------------------------------------------------
	// Purpose: Compares origins with a small numeric tolerance.
	//-----------------------------------------------------------------------------
	bool OriginsEqual( const double a[3], const double b[3], QString *mismatch )
	{
		for ( int i = 0; i < 3; ++i )
		{
			if ( qAbs( a[i] - b[i] ) > 1e-6 )
			{
				if ( mismatch )
					*mismatch = QStringLiteral( "axis %1: %2 vs %3" ).arg( i ).arg( a[i] ).arg( b[i] );
				return false;
			}
		}
		return true;
	}

	//-----------------------------------------------------------------------------
	// Purpose: Places an entity with FGD defaults and returns its id.
	//-----------------------------------------------------------------------------
	int AddEntity( MapDocLite &map, CFgdData &fgd, const QString &className,
		double x, double y, double z )
	{
		EntityLite entity;
		entity.classname = className;
		entity.id = map.AllocId();
		entity.keyvalues = fgd.DefaultKeyValues( className );
		entity.origin[0] = x;
		entity.origin[1] = y;
		entity.origin[2] = z;
		map.entities.append( entity );
		return entity.id;
	}

	//-----------------------------------------------------------------------------
	// Purpose: Grid Settings apply/cancel through the dialog's apply function.
	//-----------------------------------------------------------------------------
	bool GridSettingsApplyCancelTest()
	{
		CHammerDocument doc;
		doc.NewDocument();
		const GridSettingsState before = CurrentGridSettings( doc );
		if ( before.size != 16 || !before.visible || !before.snap ) return false;
		// Cancel never calls ApplyGridSettings; state must be untouched.
		const GridSettingsState untouched = CurrentGridSettings( doc );
		if ( untouched.size != before.size || untouched.visible != before.visible || untouched.snap != before.snap ) return false;
		GridSettingsState wanted;
		wanted.size = 64; wanted.visible = false; wanted.snap = false;
		if ( !ApplyGridSettings( doc, wanted ) ) return false;
		const GridSettingsState after = CurrentGridSettings( doc );
		if ( after.size != 64 || after.visible || after.snap ) return false;
		// Unsupported sizes are rejected without partially applying anything.
		GridSettingsState bad;
		bad.size = 48; bad.visible = true; bad.snap = true;
		if ( ApplyGridSettings( doc, bad ) || IsValidGridSize( 0 ) || IsValidGridSize( 1024 ) || !IsValidGridSize( 512 ) ) return false;
		const GridSettingsState rejected = CurrentGridSettings( doc );
		return rejected.size == 64 && !rejected.visible && !rejected.snap;
	}

	//-----------------------------------------------------------------------------
	// Purpose: Map Check issue -> object id -> selection after Go to Problem.
	//-----------------------------------------------------------------------------
	bool MapCheckGoToTest()
	{
		CHammerDocument doc;
		doc.NewDocument();
		const double origin[3] = { 512.0, 256.0, 64.0 };
		const int entityId = doc.PlaceEntity( QStringLiteral( "light" ), origin );
		const double mins[3] = { -64.0, -64.0, 0.0 };
		const double maxs[3] = { 64.0, 64.0, 64.0 };
		const int brushId = doc.CreateBrush( mins, maxs );
		if ( entityId <= 0 || brushId <= 0 ) return false;

		// Break a face and an output on a copy; the live document keeps the ids.
		MapDocLite map = doc.Map();
		for ( SolidLite &solid : map.solids )
			if ( solid.id == brushId && !solid.sides.isEmpty() ) solid.sides[0].uscale = 0;
		for ( EntityLite &entity : map.entities )
			if ( entity.id == entityId )
				entity.connections.append( qMakePair( QStringLiteral( "OnUser1" ), QStringLiteral( ",Kill,,0,-1" ) ) );
		const QList<MapCheckIssue> issues = CheckMapProblems( map );
		const MapCheckIssue *noStart = nullptr, *face = nullptr, *output = nullptr;
		for ( const MapCheckIssue &issue : issues )
		{
			if ( issue.kind == MapCheckKind::NoPlayerStart ) noStart = &issue;
			if ( issue.kind == MapCheckKind::InvalidTextureAxes ) face = &issue;
			if ( issue.kind == MapCheckKind::OutputEmptyTarget ) output = &issue;
		}
		if ( !noStart || !face || !output ) return false;
		if ( MapCheckIssueObjectId( *noStart ) != -1 || MapCheckIssueObjectId( *face ) != brushId ||
			MapCheckIssueObjectId( *output ) != entityId ) return false;

		double center[3] = {};
		doc.SelectObjects( QSet<int>{ entityId } );
		// Issue without an object leaves the selection alone.
		if ( MapCheckGoToIssue( doc, *noStart, center ) || !doc.IsSelected( entityId ) ) return false;
		if ( !MapCheckGoToIssue( doc, *face, center ) || doc.SelectedIds().size() != 1 || !doc.IsSelected( brushId ) ||
			qAbs( center[0] ) > 1e-3 || qAbs( center[1] ) > 1e-3 ) return false;
		if ( !MapCheckGoToIssue( doc, *output, center ) || doc.SelectedIds().size() != 1 || !doc.IsSelected( entityId ) ||
			qAbs( center[0] - origin[0] ) > 1e-3 || qAbs( center[1] - origin[1] ) > 1e-3 ||
			qAbs( center[2] - origin[2] ) > 1e-3 ) return false;
		// A stale id (object deleted since the check) must not change selection.
		const MapCheckIssue stale = { MapCheckKind::InvalidTextureAxes, 999999, 1 };
		return !MapCheckGoToIssue( doc, stale, center ) && doc.IsSelected( entityId );
	}
} // anonymous namespace

//-----------------------------------------------------------------------------
// Purpose: VMF loader/saver and recovery-store hardening (io-* steps).
//-----------------------------------------------------------------------------
static void RunIoHardeningSelfTest( SelfTestContext &ctx )
{
	QTemporaryDir dir;
	if ( !dir.isValid() )
	{
		ctx.Step( "io-temp-dir", false, QStringLiteral( "cannot create temporary directory" ) );
		return;
	}

	int fileCounter = 0;
	const auto writeFile = [&]( const QString &text ) -> QString {
		const QString path = dir.filePath( QStringLiteral( "io%1.vmf" ).arg( fileCounter++ ) );
		QFile f( path );
		if ( !f.open( QIODevice::WriteOnly ) )
			return QString();
		const QByteArray bytes = text.toUtf8();
		const bool ok = f.write( bytes ) == bytes.size();
		f.close();
		return ok ? path : QString();
	};
	const auto accepts = [&]( const QString &text, MapDocLite &map ) {
		const QString path = writeFile( text );
		return !path.isEmpty() && VmfIO::LoadMap( path, map );
	};
	const auto rejects = [&]( const QString &text ) {
		const QString path = writeFile( text );
		MapDocLite map;
		return !path.isEmpty() && !VmfIO::LoadMap( path, map );
	};

	const QString versionBlock = QStringLiteral( "versioninfo { \"editorversion\" \"400\" \"formatversion\" \"100\" \"prefab\" \"0\" } " );
	const QString entityEditor = QStringLiteral( "editor { \"color\" \"220 30 220\" \"visgroupshown\" \"1\" \"visgroupautoshown\" \"1\" \"logicalpos\" \"[0 0]\" } " );
	const QString validSide = QStringLiteral( "side { \"id\" \"4\" \"plane\" \"(0 0 0) (1 0 0) (0 1 0)\" \"material\" \"dev/test\" } " );
	const auto worldText = [&]( const QString &body ) {
		return versionBlock + QStringLiteral( "world { \"id\" \"1\" \"classname\" \"worldspawn\" " ) + body + QStringLiteral( " } " );
	};
	const auto entityText = [&]( const QString &body ) {
		return worldText( QString() ) + QStringLiteral( "entity { \"id\" \"2\" \"classname\" \"info_target\" " ) + body + QStringLiteral( " " ) + entityEditor + QStringLiteral( "} " );
	};
	const QString plainOrigin = QStringLiteral( "\"origin\" \"0 0 0\"" );

	// a) quoted braces are plain text, not structure
	{
		const QString text = entityText( plainOrigin + QStringLiteral( " \"message\" \"{\" \"message2\" \"}\"" ) );
		MapDocLite first, second;
		bool ok = accepts( text, first ) && first.entities.size() == 1 &&
			first.entities[0].keyvalues.value( QStringLiteral( "message" ) ) == QLatin1String( "{" ) &&
			first.entities[0].keyvalues.value( QStringLiteral( "message2" ) ) == QLatin1String( "}" );
		const QString savedPath = dir.filePath( QStringLiteral( "braces_saved.vmf" ) );
		QString err;
		ok = ok && VmfIO::SaveMap( savedPath, first, &err ) && VmfIO::LoadMap( savedPath, second, &err ) &&
			second.entities.size() == 1 &&
			second.entities[0].keyvalues.value( QStringLiteral( "message" ) ) == QLatin1String( "{" ) &&
			second.entities[0].keyvalues.value( QStringLiteral( "message2" ) ) == QLatin1String( "}" );
		ctx.Step( "io-brace-values", ok, err );
	}

	// b) Source's TokenReader ends a value at the next quote. HM2 must not
	// overwrite an existing file with a value that Source will parse differently.
	{
		const QString tokenPath = dir.filePath( QStringLiteral( "source_tokens.txt" ) );
		QFile tokenFile( tokenPath );
		bool originalTokenizerOk = tokenFile.open( QIODevice::WriteOnly ) &&
			tokenFile.write( "\"key\" \"a\"b\"\n" ) > 0;
		tokenFile.close();
		TokenReader sourceReader;
		char token[128] = {};
		const QByteArray nativePath = QFile::encodeName( tokenPath );
		originalTokenizerOk = originalTokenizerOk && sourceReader.Open( nativePath.constData() ) &&
			sourceReader.NextToken( token, sizeof( token ) ) == STRING && QByteArray( token ) == "key" &&
			sourceReader.NextToken( token, sizeof( token ) ) == STRING && QByteArray( token ) == "a" &&
			sourceReader.NextToken( token, sizeof( token ) ) == IDENT && QByteArray( token ) == "b";
		sourceReader.Close();
		ctx.Step( "io-original-tokenreader-quote", originalTokenizerOk, QStringLiteral( "Source TokenReader ends value at the embedded quote" ) );
		const auto originalToken = [&]( const char *raw, const QByteArray &expected, bool equal ) {
			QFile file( tokenPath );
			if ( !file.open( QIODevice::WriteOnly | QIODevice::Truncate ) || file.write( raw ) < 0 ) return false;
			file.close();
			TokenReader reader;
			char value[128] = {};
			if ( !reader.Open( nativePath.constData() ) ) return false;
			const bool ok = reader.NextToken( value, sizeof( value ) ) == STRING &&
				( ( QByteArray( value ) == expected ) == equal );
			reader.Close();
			return ok;
		};
		const bool originalSlashes = originalToken( "\"a\\nb\"", QByteArray( "a\nb" ), true ) &&
			originalToken( "\"a\\xb\"", QByteArray( "a\\xb" ), false ) &&
			originalToken( "\"a\\\\b\"", QByteArray( "a\\b" ), false );
		ctx.Step( "io-original-tokenreader-backslash", originalSlashes, QStringLiteral( "Source converts backslash-n and cannot preserve other backslash sequences" ) );
		const QString quoteValue = QStringLiteral( "plain" );
		const QString trailValue = QStringLiteral( "end" );
		const QString pathValue = QStringLiteral( "models/props/x.mdl" );
		const QString quoteMaterial = QStringLiteral( "dev/test" );

		MapDocLite src;
		src.worldspawn.insert( QStringLiteral( "classname" ), QStringLiteral( "worldspawn" ) );
		EntityLite ent;
		ent.id = src.AllocId();
		ent.classname = QStringLiteral( "info_target" );
		ent.origin[0] = ent.origin[1] = ent.origin[2] = 0.0;
		ent.keyvalues.insert( QStringLiteral( "quote" ), quoteValue );
		ent.keyvalues.insert( QStringLiteral( "trail" ), trailValue );
		ent.keyvalues.insert( QStringLiteral( "model" ), pathValue );
		src.entities.append( ent );
		const double mins[3] = { 0.0, 0.0, 0.0 };
		const double maxs[3] = { 64.0, 64.0, 64.0 };
		SolidLite solid;
		solid.id = src.AllocId();
		solid.sides = MakeBoxSides( mins, maxs, quoteMaterial );
		src.RecalcSolidBounds( solid );
		src.solids.append( solid );

		const QString savedPath = dir.filePath( QStringLiteral( "quotes_saved.vmf" ) );
		QString err;
		MapDocLite back;
		bool ok = VmfIO::SaveMap( savedPath, src, &err ) && VmfIO::LoadMap( savedPath, back, &err ) &&
			back.entities.size() == 1 && back.solids.size() == 1 && !back.solids[0].sides.isEmpty() &&
			back.entities[0].keyvalues.value( QStringLiteral( "quote" ) ) == quoteValue &&
			back.entities[0].keyvalues.value( QStringLiteral( "trail" ) ) == trailValue &&
			back.entities[0].keyvalues.value( QStringLiteral( "model" ) ) == pathValue;
		for ( const SideLite &side : back.solids.isEmpty() ? QList<SideLite>() : back.solids[0].sides )
			ok = ok && side.texture == quoteMaterial;
		QFile savedFile( savedPath );
		QByteArray savedBytes;
		if ( savedFile.open( QIODevice::ReadOnly ) )
			savedBytes = savedFile.readAll();
		savedFile.close();
		ok = ok && savedBytes.contains( "\"models/props/x.mdl\"" );
		ctx.Step( "io-quote-backslash-roundtrip", ok, err );
		QFile sentinel( savedPath );
		const QByteArray before = savedBytes;
		src.entities[0].keyvalues[QStringLiteral( "quote" )] = QStringLiteral( "a\"b" );
		QString quoteError;
		bool rejectedQuote = !VmfIO::SaveMap( savedPath, src, &quoteError ) && !quoteError.isEmpty() &&
			sentinel.open( QIODevice::ReadOnly ) && sentinel.readAll() == before &&
			src.entities[0].keyvalues.value( QStringLiteral( "quote" ) ) == QLatin1String( "a\"b" );
		sentinel.close();
		src.entities[0].keyvalues[QStringLiteral( "quote" )] = quoteValue;
		src.entities[0].connections.append( qMakePair( QStringLiteral( "OnTrigger" ), QStringLiteral( "target,Use,\"arg\",0,-1" ) ) );
		rejectedQuote = rejectedQuote && !VmfIO::SaveMap( savedPath, src, &quoteError );
		src.entities[0].connections.clear();
		src.solids[0].sides[0].texture = QStringLiteral( "dev/a\"b" );
		rejectedQuote = rejectedQuote && !VmfIO::SaveMap( savedPath, src, &quoteError );
		src.solids[0].sides[0].texture = quoteMaterial;
		src.versionProperties.append( qMakePair( QStringLiteral( "extra" ), QStringLiteral( "a\"b" ) ) );
		rejectedQuote = rejectedQuote && !VmfIO::SaveMap( savedPath, src, &quoteError ) &&
			sentinel.open( QIODevice::ReadOnly ) && sentinel.readAll() == before;
		sentinel.close();
		src.versionProperties.clear();
		ctx.Step( "io-save-quote-guard", rejectedQuote, quoteError );
		bool rejectedBackslash = true;
		for ( const QString &unsafe : { QStringLiteral( "models\\new\\x.mdl" ), QStringLiteral( "a\\\\b" ), QStringLiteral( "line\nbreak" ) } )
		{
			src.entities[0].keyvalues[QStringLiteral( "model" )] = unsafe;
			rejectedBackslash = rejectedBackslash && !VmfIO::SaveMap( savedPath, src, &quoteError ) && !quoteError.isEmpty();
		}
		rejectedBackslash = rejectedBackslash && sentinel.open( QIODevice::ReadOnly ) && sentinel.readAll() == before;
		sentinel.close();
		src.entities[0].keyvalues[QStringLiteral( "model" )] = pathValue;
		ctx.Step( "io-save-backslash-guard", rejectedBackslash, quoteError );
		src.solids[0].sides[0].uaxis[0] = std::numeric_limits<float>::quiet_NaN();
		QString finiteError;
		const bool rejectedNonfinite = !VmfIO::SaveMap( savedPath, src, &finiteError ) && !finiteError.isEmpty() &&
			sentinel.open( QIODevice::ReadOnly ) && sentinel.readAll() == before && std::isnan( src.solids[0].sides[0].uaxis[0] );
		sentinel.close();
		ctx.Step( "io-save-nonfinite-guard", rejectedNonfinite, finiteError );
	}

	// c) hostile nesting depth is rejected rather than overflowing the stack
	{
		QString nested;
		for ( int i = 0; i < 100; ++i )
			nested += QStringLiteral( "a { " );
		for ( int i = 0; i < 100; ++i )
			nested += QStringLiteral( "} " );
		ctx.Step( "io-deep-nesting", rejects( worldText( nested ) ), QStringLiteral( "100 nested blocks" ) );
	}

	// d) malformed or non-finite entity origins fail the load (valid origin is the control)
	{
		MapDocLite control;
		const bool ok = accepts( entityText( plainOrigin ), control ) &&
			rejects( entityText( QStringLiteral( "\"origin\" \"1 2\"" ) ) ) &&
			rejects( entityText( QStringLiteral( "\"origin\" \"nan 0 0\"" ) ) );
		ctx.Step( "io-bad-origin", ok, QStringLiteral( "\"1 2\" and \"nan 0 0\"" ) );
	}

	// e) non-finite plane points fail the load
	{
		MapDocLite control;
		const QString badSide = QStringLiteral( "side { \"id\" \"4\" \"plane\" \"(nan 0 0) (1 0 0) (0 1 0)\" \"material\" \"dev/test\" } " );
		const QString solidFmt = QStringLiteral( "solid { \"id\" \"3\" %1 }" );
		const bool ok = accepts( worldText( solidFmt.arg( validSide ) ), control ) && control.solids.size() == 1 &&
			rejects( worldText( solidFmt.arg( badSide ) ) );
		ctx.Step( "io-nan-plane", ok, QStringLiteral( "plane with nan point" ) );
	}

	// f) a key directly followed by a closing brace has no value
	{
		MapDocLite control;
		const bool ok = accepts( entityText( plainOrigin + QStringLiteral( " \"orphan\" \"v\"" ) ), control ) &&
			rejects( entityText( plainOrigin + QStringLiteral( " \"orphan\"" ) ) );
		ctx.Step( "io-valueless-key", ok, QStringLiteral( "key followed by }" ) );
	}

	// g) ids that would overflow the next-id counter are rejected
	{
		MapDocLite control;
		const QString solidFmt = QStringLiteral( "solid { \"id\" \"%1\" " ) + validSide + QStringLiteral( "}" );
		const bool ok = accepts( worldText( solidFmt.arg( 5 ) ), control ) && control.solids.size() == 1 &&
			rejects( worldText( solidFmt.arg( 2147483647 ) ) );
		ctx.Step( "io-id-overflow", ok, QStringLiteral( "solid id 2147483647" ) );
	}

	// h) a recovery store with an empty root must never touch the working directory
	{
		QTemporaryDir cwdDir;
		const QString previousCwd = QDir::currentPath();
		bool ok = cwdDir.isValid() && QDir::setCurrent( cwdDir.path() );
		const QString manifest = cwdDir.filePath( QStringLiteral( "current.json" ) );
		const QString snapshot = cwdDir.filePath( QStringLiteral( "recovery_x.vmf" ) );
		if ( ok )
		{
			for ( const QString &path : { manifest, snapshot } )
			{
				QFile f( path );
				ok = ok && f.open( QIODevice::WriteOnly ) && f.write( "{}" ) == 2;
				f.close();
			}
			const CHammerRecoveryStore emptyStore{ QString() };
			emptyStore.Clear();
			HammerRecoveryInfo info;
			ok = ok && !emptyStore.Read( info ) && QFile::exists( manifest ) && QFile::exists( snapshot );
		}
		QDir::setCurrent( previousCwd );
		ctx.Step( "io-recovery-empty-root", ok, QStringLiteral( "Clear() with empty root keeps cwd files" ) );
	}

	// i) number formatting is finite and exponent-free
	{
		const QString nanText = FormatVmfNumber( double( NAN ) );
		const QString infText = FormatVmfNumber( double( INFINITY ) );
		const QString bigText = FormatVmfNumber( 1e20 );
		const bool ok = nanText == QLatin1String( "0" ) && infText == QLatin1String( "0" ) &&
			!bigText.isEmpty() && !bigText.contains( QLatin1Char( 'e' ), Qt::CaseInsensitive );
		ctx.Step( "io-number-format", ok, QStringLiteral( "1e20 -> %1" ).arg( bigText ) );
	}
}

//-----------------------------------------------------------------------------
// Purpose: View > Filter state, picking and document independence.
//-----------------------------------------------------------------------------
static void RunViewFilterSelfTest( SelfTestContext &ctx )
{
	CHammerDocument doc;
	const double bmin[3] = { -16, -16, -16 }, bmax[3] = { 16, 16, 16 };
	const int brushId = doc.CreateBrush( bmin, bmax );
	const int lightId = AddEntity( doc.Map(), doc.Fgd(), QStringLiteral( "light" ), -60, 0, 0 );
	const int propId = AddEntity( doc.Map(), doc.Fgd(), QStringLiteral( "prop_static" ), 100, 100, 0 );
	// Last history entry: a distant brush that no pick or region below reaches.
	const double fmin[3] = { 1000, 1000, 1000 }, fmax[3] = { 1032, 1032, 1032 };
	const int farId = doc.CreateBrush( fmin, fmax );

	// Default shows everything.
	bool ok = doc.GetViewFilter().IsDefault() && brushId > 0 && lightId > 0 && propId > 0 && farId > 0;
	const SolidLite *solid = doc.Map().SolidById( brushId );
	const EntityLite *light = doc.Map().EntityById( lightId );
	const EntityLite *prop = doc.Map().EntityById( propId );
	ok = ok && solid && light && prop && doc.GetViewFilter().SolidVisible( *solid ) &&
		doc.GetViewFilter().EntityVisible( *light ) && doc.GetViewFilter().EntityVisible( *prop );
	ctx.Step( "view-filter-default", ok, QStringLiteral( "everything visible by default" ) );
	if ( !ok ) return;

	const double eye[3] = { -100, 0, 0 }, ray[3] = { 1, 0, 0 };
	const double fullMin[2] = { -200, -50 }, fullMax[2] = { 250, 150 };
	const auto pick = [&]( double u, double v ) { return HammerViewPick::Pick2D( doc, 0, 1, u, v, 0.0, 8.0 ); };
	const auto region = [&]() { return HammerViewPick::Region2D( doc, 0, 1, fullMin, fullMax, false ); };
	const QSet<int> all = QSet<int>() << brushId << lightId << propId;
	// The 3D face-hit ray (y=0,z=0) and all 2D probes stay clear of the far brush.

	// Unfiltered picking.
	bool pickOk = pick( 0, 0 ) == brushId && pick( -60, 0 ) == lightId && pick( 100, 100 ) == propId &&
		region() == all && HammerFaceHit::Find( doc, eye, ray, 1.0 ).solidId == -1;

	// Visible selection survives filtering; hidden selection is removed.
	doc.SelectObject( propId );
	QTemporaryDir dir;
	const QString pathBefore = dir.filePath( QStringLiteral( "before.vmf" ) );
	const QString pathAfter = dir.filePath( QStringLiteral( "after.vmf" ) );
	const bool dirtyBefore = doc.IsDirty();
	bool saveOk = dir.isValid() && VmfIO::SaveMap( pathBefore, doc.Map() );

	// Hide brushes.
	ViewFilter filter;
	filter.showBrushes = false;
	doc.SetViewFilter( filter );
	bool brushOk = !filter.IsDefault() && !doc.GetViewFilter().SolidVisible( *solid ) &&
		doc.GetViewFilter().EntityVisible( *light ) &&
		pick( 0, 0 ) == -1 && pick( -60, 0 ) == lightId && region() == ( QSet<int>() << lightId << propId ) &&
		HammerFaceHit::Find( doc, eye, ray, 1.0 ).solidId == -1 && doc.IsSelected( propId );

	// Hide entities: brushes become pickable through the former occluder.
	filter = ViewFilter();
	filter.showEntities = false;
	doc.SetViewFilter( filter );
	bool entityOk = pick( -60, 0 ) == -1 && pick( 100, 100 ) == -1 && pick( 0, 0 ) == brushId &&
		region() == ( QSet<int>() << brushId ) &&
		HammerFaceHit::Find( doc, eye, ray, 1.0 ).solidId == brushId && doc.SelectedIds().isEmpty();
	ctx.Step( "view-filter-visibility", pickOk && brushOk && entityOk,
		QStringLiteral( "pick=%1 brushes=%2 entities=%3" ).arg( pickOk ).arg( brushOk ).arg( entityOk ) );

	// Class substring filter (case-insensitive, entities only).
	filter = ViewFilter();
	filter.classFilter = QStringLiteral( "PROP_" );
	doc.SetViewFilter( filter );
	bool classOk = !doc.GetViewFilter().EntityVisible( *light ) && doc.GetViewFilter().EntityVisible( *prop ) &&
		doc.GetViewFilter().SolidVisible( *solid ) &&
		pick( -60, 0 ) == -1 && pick( 100, 100 ) == propId && pick( 0, 0 ) == brushId &&
		region() == ( QSet<int>() << brushId << propId ) &&
		HammerFaceHit::Find( doc, eye, ray, 1.0 ).solidId == brushId;
	CEntityReportDialog filteredReport( &doc );
	QTableWidget *filteredTable = filteredReport.findChild<QTableWidget *>( QStringLiteral( "entityReportTable" ) );
	bool reportFilterOk = filteredTable && filteredTable->rowCount() == 1;
	if ( filteredTable && reportFilterOk )
	{
		filteredTable->selectRow( 0 );
		reportFilterOk = filteredReport.SelectedEntityIds() == ( QSet<int>() << propId );
	}
	filter.classFilter = QStringLiteral( "nomatch" );
	doc.SetViewFilter( filter );
	classOk = classOk && pick( 100, 100 ) == -1 && region() == ( QSet<int>() << brushId );
	reportFilterOk = reportFilterOk && filteredTable && filteredTable->rowCount() == 0 && filteredReport.SelectedEntityIds().isEmpty();
	ctx.Step( "view-filter-class", classOk, QStringLiteral( "substring filter hides non-matching entities" ) );
	ctx.Step( "entity-report-view-filter", reportFilterOk, QStringLiteral( "report exposes only visible entities and refreshes on filter changes" ) );

	// The filter is view-only: no dirty change, same VMF, undo history untouched.
	const bool sameSelection = doc.SelectedIds().isEmpty();
	saveOk = saveOk && VmfIO::SaveMap( pathAfter, doc.Map() );
	QFile before( pathBefore ), after( pathAfter );
	const bool sameBytes = saveOk && before.open( QIODevice::ReadOnly ) && after.open( QIODevice::ReadOnly ) &&
		before.readAll() == after.readAll();
	const bool dirtyKept = doc.IsDirty() == dirtyBefore;
	const bool undoOk = doc.Undo() && !doc.Map().SolidById( farId ) && doc.Map().SolidById( brushId ) &&
		doc.Map().EntityById( propId ) && doc.Map().EntityById( lightId );
	// Reset on New: a new document shows everything again.
	doc.SetViewFilter( filter );
	doc.NewDocument();
	const bool resetOk = doc.GetViewFilter().IsDefault() && !doc.IsDirty();
	ctx.Step( "view-filter-document", sameSelection && sameBytes && dirtyKept && undoOk && resetOk,
		QStringLiteral( "selection=%1 vmf=%2 dirty=%3 undo=%4 reset=%5" )
			.arg( sameSelection ).arg( sameBytes ).arg( dirtyKept ).arg( undoOk ).arg( resetOk ) );

	// Hidden objects cannot be selected via bulk commands, direct selection,
	// property search or selection restored by undo.
	CHammerDocument selectionDoc;
	const int hiddenBrush = selectionDoc.CreateBrush( bmin, bmax );
	const int visibleEntity = selectionDoc.PlaceEntity( QStringLiteral( "light" ), bmin );
	ViewFilter hideBrushes;
	hideBrushes.showBrushes = false;
	selectionDoc.SelectAll();
	selectionDoc.SetViewFilter( hideBrushes );
	bool selectionOk = selectionDoc.SelectedIds() == ( QSet<int>() << visibleEntity );
	double ignoredCenter[3] = {};
	const MapCheckIssue hiddenIssue { MapCheckKind::DuplicateFaceId, hiddenBrush, 0 };
	selectionOk = selectionOk && !MapCheckGoToIssue( selectionDoc, hiddenIssue, ignoredCenter ) &&
		selectionDoc.SelectedIds() == ( QSet<int>() << visibleEntity ) &&
		!SelectBrushById( selectionDoc, hiddenBrush ) && selectionDoc.SelectedIds() == ( QSet<int>() << visibleEntity );
	selectionDoc.SelectObject( hiddenBrush );
	selectionOk = selectionOk && selectionDoc.SelectedIds().isEmpty();
	selectionDoc.SelectAll();
	selectionOk = selectionOk && selectionDoc.SelectedIds() == ( QSet<int>() << visibleEntity );
	selectionDoc.InvertSelection();
	selectionOk = selectionOk && selectionDoc.SelectedIds().isEmpty();
	selectionDoc.InvertSelection();
	selectionOk = selectionOk && selectionDoc.SelectedIds() == ( QSet<int>() << visibleEntity );
	selectionOk = selectionOk && selectionDoc.DeleteSelection() &&
		selectionDoc.Map().SolidById( hiddenBrush ) && !selectionDoc.Map().EntityById( visibleEntity );
	selectionOk = selectionOk && selectionDoc.Undo() &&
		selectionDoc.Map().SolidById( hiddenBrush ) && selectionDoc.Map().EntityById( visibleEntity ) &&
		selectionDoc.SelectedIds() == ( QSet<int>() << visibleEntity );
	ViewFilter hideEntities;
	hideEntities.showEntities = false;
	selectionDoc.SetViewFilter( hideEntities );
	selectionOk = selectionOk && selectionDoc.SelectedIds().isEmpty() &&
		selectionDoc.SelectEntitiesByProperty( QStringLiteral( "classname" ), QStringLiteral( "light" ), true ) == 0;
	selectionDoc.SelectAll();
	selectionOk = selectionOk && selectionDoc.SelectedIds() == ( QSet<int>() << hiddenBrush );
	ViewFilter onlyLight;
	onlyLight.classFilter = QStringLiteral( "light" );
	selectionDoc.SetViewFilter( onlyLight );
	selectionDoc.SelectObject( visibleEntity );
	selectionOk = selectionOk && selectionDoc.SetSelectedEntityClass( QStringLiteral( "prop_static" ) ) &&
		selectionDoc.SelectedIds().isEmpty();
	selectionOk = selectionOk && selectionDoc.Undo() && selectionDoc.IsSelected( visibleEntity ) &&
		selectionDoc.Redo() && selectionDoc.SelectedIds().isEmpty();
	CHammerDocument historyDoc;
	const int historyBrush = historyDoc.CreateBrush( bmin, bmax );
	const int historyEntity = historyDoc.PlaceEntity( QStringLiteral( "light" ), bmin );
	historyDoc.SelectAll();
	const double moveDelta[3] = { 16, 0, 0 };
	selectionOk = selectionOk && historyDoc.MoveSelection( moveDelta );
	historyDoc.SetViewFilter( hideBrushes );
	selectionOk = selectionOk && historyDoc.Undo() && historyDoc.Map().SolidById( historyBrush ) &&
		historyDoc.SelectedIds() == ( QSet<int>() << historyEntity );
	ctx.Step( "view-filter-selection", selectionOk,
		QStringLiteral( "hidden objects excluded from selection, deletion and undo; class changes reapply filter" ) );
	CMainWindow quickWindow( nullptr, false );
	CHammerDocument *quickDoc = quickWindow.findChild<CHammerDocument *>();
	QAction *hideSelectedAction = quickWindow.findChild<QAction *>( QStringLiteral( "quickHideSelected" ) );
	QAction *hideUnselectedAction = quickWindow.findChild<QAction *>( QStringLiteral( "quickHideUnselected" ) );
	QAction *unhideAction = quickWindow.findChild<QAction *>( QStringLiteral( "quickUnhideAll" ) );
	bool quickOk = quickDoc && hideSelectedAction && hideUnselectedAction && unhideAction;
	if ( quickOk )
	{
		const int brush = quickDoc->CreateBrush( bmin, bmax );
		const int entity = quickDoc->PlaceEntity( QStringLiteral( "light" ), bmin );
		quickDoc->SelectObject( brush );
		const QString quickBefore = dir.filePath( QStringLiteral( "quick-before.vmf" ) );
		const QString quickAfter = dir.filePath( QStringLiteral( "quick-after.vmf" ) );
		const bool wroteBefore = VmfIO::SaveMap( quickBefore, quickDoc->Map() );
		hideUnselectedAction->trigger();
		quickOk = quickDoc->GetViewFilter().SolidVisible( *quickDoc->Map().SolidById( brush ) ) &&
			!quickDoc->GetViewFilter().EntityVisible( *quickDoc->Map().EntityById( entity ) ) && quickDoc->IsSelected( brush );
		hideSelectedAction->trigger();
		quickOk = quickOk && quickDoc->SelectedIds().isEmpty() && !quickDoc->GetViewFilter().SolidVisible( *quickDoc->Map().SolidById( brush ) );
		quickDoc->SelectAll();
		quickOk = quickOk && quickDoc->SelectedIds().isEmpty() && !quickDoc->CopySelection() && !quickDoc->DeleteSelection();
		const bool wroteAfter = VmfIO::SaveMap( quickAfter, quickDoc->Map() );
		QFile quickBeforeFile( quickBefore ), quickAfterFile( quickAfter );
		quickOk = quickOk && wroteBefore && wroteAfter && quickBeforeFile.open( QIODevice::ReadOnly ) &&
			quickAfterFile.open( QIODevice::ReadOnly ) && quickBeforeFile.readAll() == quickAfterFile.readAll();
		unhideAction->trigger();
		quickOk = quickOk && quickDoc->GetViewFilter().IsDefault() && quickDoc->Map().SolidById( brush ) && quickDoc->Map().EntityById( entity );
	}
	ctx.Step( "quick-hide", quickOk, QStringLiteral( "hide unselected, selected and restore without removing objects" ) );
}

//-----------------------------------------------------------------------------
// Purpose: --selftest entry point.
//-----------------------------------------------------------------------------
int RunHammerSelfTest( const QString &vmfPath )
{
	SelfTestContext ctx;
	const QString logPath = qEnvironmentVariable( "HAMMER_SELFTEST_LOG" );
	if ( !logPath.isEmpty() )
	{
		ctx.logFile.setFileName( logPath );
		ctx.logFile.open( QIODevice::WriteOnly | QIODevice::Truncate | QIODevice::Text );
	}
	// Structural preflight only: grouped VMFs are still rejected by the loader.
	{
		using namespace HammerGroupInspect;
		const Properties parent = { { QStringLiteral( "groupid" ), QStringLiteral( "10" ) } };
		const QList<Group> groups = { { 10, {} }, { 11, parent } };
		const QList<Object> objects = { { 20, 0, parent }, { 21, 20, {} }, { 22, 0, { { QStringLiteral( "groupid" ), QStringLiteral( "11" ) } } } };
		QString error;
		bool ok = Inspect( groups, objects, &error );
		QList<Group> badGroups = groups;
		badGroups[0].editor = { { QStringLiteral( "groupid" ), QStringLiteral( "11" ) } };
		ok = ok && !Inspect( badGroups, objects, &error ) && error.contains( QLatin1String( "cycle" ) );
		badGroups = groups; badGroups[1].editor = { { QStringLiteral( "groupid" ), QStringLiteral( "99" ) } };
		ok = ok && !Inspect( badGroups, objects, &error ) && error.contains( QLatin1String( "unknown" ) );
		badGroups = groups; badGroups[1].id = 10;
		ok = ok && !Inspect( badGroups, objects, &error ) && error.contains( QLatin1String( "duplicate" ) );
		QList<Object> badObjects = objects; badObjects[1].editor = parent;
		ok = ok && !Inspect( groups, badObjects, &error ) && error.contains( QLatin1String( "entity-owned" ) );
		badObjects = objects; badObjects[0].editor.append( parent.first() );
		ok = ok && !Inspect( groups, badObjects, &error ) && error.contains( QLatin1String( "duplicate groupid" ) );
		badObjects = objects; badObjects[0].id = 10;
		ok = ok && !Inspect( groups, badObjects, &error ) && error.contains( QLatin1String( "duplicate object" ) );
		ctx.Step( "group-forest-inspector", ok, QStringLiteral( "structural preflight only; no VMF loader integration" ) );
	}
#ifdef HAMMER_GMOD_COMPAT
	IKeyValuesSystem *kvSystem = KeyValuesSystem();
	// The bridge loads GMod vstdlib on its first method call, not on lookup.
	kvSystem->GetSymbolForString( nullptr, false );
	const QString appDir = QDir::cleanPath( QCoreApplication::applicationDirPath() );
	QString runtimeDir = qEnvironmentVariable( "HAMMER_GMOD_RUNTIME_DIR" );
	if ( runtimeDir.isEmpty() ) runtimeDir = QDir( appDir ).absoluteFilePath( QStringLiteral( ".." ) );
	const QString tier0Path = LoadedModulePath( L"tier0.dll" );
	const QString vstdlibPath = LoadedModulePath( L"vstdlib.dll" );
	const bool sourceRuntimePaths = ModuleIsInDirectory( tier0Path, runtimeDir ) &&
		ModuleIsInDirectory( vstdlibPath, runtimeDir );
	ctx.Step( "gmod-parent-runtime-paths", sourceRuntimePaths,
		QStringLiteral( "tier0=%1; vstdlib=%2; expected=%3" ).arg( tier0Path, vstdlibPath, QDir( runtimeDir ).absolutePath() ) );
	const QString qtCorePath = LoadedModulePath( L"Qt6Core.dll" );
	const QString qtGuiPath = LoadedModulePath( L"Qt6Gui.dll" );
	const QString qtWidgetsPath = LoadedModulePath( L"Qt6Widgets.dll" );
	const bool qtAppPaths = ModuleIsInDirectory( qtCorePath, appDir ) &&
		ModuleIsInDirectory( qtGuiPath, appDir ) && ModuleIsInDirectory( qtWidgetsPath, appDir );
	ctx.Step( "qt-app-module-paths", qtAppPaths,
		QStringLiteral( "Qt6Core=%1; Qt6Gui=%2; Qt6Widgets=%3; appDir=%4" )
			.arg( qtCorePath, qtGuiPath, qtWidgetsPath, appDir ) );
	const HKeySymbol missingSymbol = kvSystem->GetSymbolForString( nullptr, false );
	const HKeySymbol testSymbol = kvSystem->GetSymbolForString( "hammer2_gmod_bridge_selftest", true );
	const char *testName = kvSystem->GetStringForSymbol( testSymbol );
	void *testAllocation = kvSystem->AllocKeyValuesMemory( sizeof( KeyValues ) );
	const bool kvBridgeOk = missingSymbol == INVALID_KEY_SYMBOL && testSymbol != INVALID_KEY_SYMBOL &&
		testName && QByteArray( testName ) == "hammer2_gmod_bridge_selftest" && testAllocation;
	if ( testAllocation ) kvSystem->FreeKeyValuesMemory( testAllocation );
	ctx.Step( "gmod-keyvalues-bridge", kvBridgeOk, QStringLiteral( "symbol width, lookup and allocation" ) );
	ctx.Step( "gmod-commandline-bridge", HammerGmodCommandLineSelfTest(), QStringLiteral( "arguments and quoted paths" ) );
#endif

	//--- 1. Application bootstrap ---------------------------------------------
	ctx.Step( "appinit", true, QStringLiteral( "QApplication ready, qApp=%1" )
		.arg( QCoreApplication::instance() ? 1 : 0 ) );
	ctx.Step( "hammer-core-bounds", RunHammerCoreBoundsSmokeTest(),
		QStringLiteral( "original BoundBox linked and exercised" ) );
	{
		// Check contrast by role, not by a fixed colour, so later theme changes
		// can preserve readability without keeping the same palette values.
		const auto luminance = []( const QColor &color ) {
			const auto channel = []( double c ) {
				return c <= 0.04045 ? c / 12.92 : std::pow( ( c + 0.055 ) / 1.055, 2.4 );
			};
			return 0.2126 * channel( color.redF() ) + 0.7152 * channel( color.greenF() ) +
				0.0722 * channel( color.blueF() );
		};
		const auto readable = [&]( const QColor &foreground, const QColor &background ) {
			if ( !foreground.isValid() || !background.isValid() ) return false;
			const double a = luminance( foreground ), b = luminance( background );
			return ( qMax( a, b ) + 0.05 ) / ( qMin( a, b ) + 0.05 ) >= 4.5;
		};
		const QPalette palette = QApplication::palette();
		QPushButton button;
		QLineEdit input;
		bool ok = !HammerTheme::StyleSheet().isEmpty() &&
			readable( palette.color( QPalette::WindowText ), palette.color( QPalette::Window ) ) &&
			readable( palette.color( QPalette::Text ), palette.color( QPalette::Base ) ) &&
			readable( palette.color( QPalette::HighlightedText ), palette.color( QPalette::Highlight ) ) &&
			( button.focusPolicy() & Qt::TabFocus ) && ( input.focusPolicy() & Qt::TabFocus );
		for ( const char *name : { HammerTheme::Icons::Select, HammerTheme::Icons::Entity,
			HammerTheme::Icons::Block, HammerTheme::Icons::Camera, HammerTheme::Icons::ApplyMaterial } )
		{
			const QImage rendered = HammerTheme::ToolIcon( QString::fromLatin1( name ) ).pixmap( 24, 24 ).toImage();
			bool visible = false;
			for ( int y = 0; !visible && y < rendered.height(); ++y )
				for ( int x = 0; x < rendered.width(); ++x )
					if ( qAlpha( rendered.pixel( x, y ) ) ) { visible = true; break; }
			ok = ok && !rendered.isNull() && visible;
		}
		ctx.Step( "ui-theme", ok, QStringLiteral( "text contrast, keyboard focus and tool icons" ) );
	}
	{
		// The test-only constructor skips saved layout and game configuration.
		CMainWindow window( nullptr, false );
		QSplitter *root = window.findChild<QSplitter *>( QStringLiteral( "ViewportSplitter" ) );
		QSplitter *left = window.findChild<QSplitter *>( QStringLiteral( "ViewLeftSplitter" ) );
		QSplitter *right = window.findChild<QSplitter *>( QStringLiteral( "View2DSplitter" ) );
		QWidget *views[] = {
			window.findChild<QWidget *>( QStringLiteral( "Viewport3D" ) ),
			window.findChild<QWidget *>( QStringLiteral( "ViewportFront" ) ),
			window.findChild<QWidget *>( QStringLiteral( "ViewportTop" ) ),
			window.findChild<QWidget *>( QStringLiteral( "ViewportSide" ) )
		};
		bool ok = root && left && right && views[0] && views[1] && views[2] && views[3];
		if ( ok )
		{
			ok = root->orientation() == Qt::Horizontal && root->count() == 2 &&
				root->widget( 0 ) == left && root->widget( 1 ) == right &&
				left->orientation() == Qt::Vertical && left->count() == 2 &&
				left->widget( 0 ) == views[0] && left->widget( 1 ) == views[1] &&
				right->orientation() == Qt::Vertical && right->count() == 2 &&
				right->widget( 0 ) == views[2] && right->widget( 1 ) == views[3];
			for ( QWidget *selected : views )
			{
				QFocusEvent focus( QEvent::FocusIn );
				QApplication::sendEvent( selected, &focus );
				ok = QMetaObject::invokeMethod( &window, "OnMaximizeViewport" ) && ok;
				for ( QWidget *view : views ) ok = ( view->isHidden() == ( view != selected ) ) && ok;
				ok = QMetaObject::invokeMethod( &window, "OnMaximizeViewport" ) && ok;
				for ( QWidget *view : views ) ok = !view->isHidden() && ok;
			}
		}
		ctx.Step( "ui-layout", ok, QStringLiteral( "four viewports and maximize/restore" ) );

		// Rendering a hidden widget into an image checks the assembled layout
		// without displaying a window or changing the user's saved layout.
		window.resize( 1280, 800 );
		window.ensurePolished();
		QImage image( window.size(), QImage::Format_ARGB32_Premultiplied );
		image.fill( Qt::transparent );
		window.render( &image );
		QToolBar *toolbar = window.findChild<QToolBar *>( QStringLiteral( "EditingToolBar" ) );
		bool renderOk = ok && !window.isVisible() && !image.isNull() && toolbar && !toolbar->isHidden();
		if ( renderOk )
		{
			QList<QRect> viewRects;
			for ( QWidget *view : views )
			{
				const QRect rect( view->mapTo( &window, QPoint( 0, 0 ) ), view->size() );
				renderOk = renderOk && !view->isHidden() && rect.width() >= 80 && rect.height() >= 80 &&
					image.rect().contains( rect ) && image.pixelColor( rect.center() ).alpha() > 0;
				for ( const QRect &previous : viewRects ) renderOk = renderOk && !rect.intersects( previous );
				viewRects.append( rect );
			}
			int toolButtons = 0;
			for ( QAction *action : toolbar->actions() )
			{
				QToolButton *tool = qobject_cast<QToolButton *>( toolbar->widgetForAction( action ) );
				if ( !tool || action->icon().isNull() ) continue;
				const QRect rect( tool->mapTo( &window, QPoint( 0, 0 ) ), tool->size() );
				renderOk = renderOk && !tool->isHidden() && rect.width() >= 24 && rect.height() >= 24 &&
					image.rect().contains( rect ) && image.pixelColor( rect.center() ).alpha() > 0 &&
					!action->icon().pixmap( 24, 24 ).toImage().isNull();
				++toolButtons;
			}
			renderOk = renderOk && toolButtons >= 6;
		}
		ctx.Step( "ui-render", renderOk, QStringLiteral( "hidden-window viewport and toolbar layout" ) );
		QToolBar *mapToolbar = window.findChild<QToolBar *>( QStringLiteral( "MapToolBar" ) );
		QToolBar *entityToolbar = window.findChild<QToolBar *>( QStringLiteral( "EntityToolBar" ) );
		QAction *classicTools = window.findChild<QAction *>( QStringLiteral( "classicToolBarLayout" ) );
		bool classicLayoutOk = toolbar && mapToolbar && entityToolbar && classicTools &&
			window.toolBarArea( toolbar ) == Qt::LeftToolBarArea &&
			window.toolBarArea( entityToolbar ) == Qt::TopToolBarArea;
		if ( classicLayoutOk )
		{
			window.addToolBar( Qt::RightToolBarArea, toolbar );
			classicTools->trigger();
			classicLayoutOk = window.toolBarArea( toolbar ) == Qt::LeftToolBarArea &&
				window.toolBarArea( entityToolbar ) == Qt::TopToolBarArea &&
				window.toolBarArea( mapToolbar ) == Qt::TopToolBarArea;
		}
		QAction *cameraToolAction = window.findChild<QAction *>( QStringLiteral( "toolAction_camera" ) );
		QAction *decalToolAction = window.findChild<QAction *>( QStringLiteral( "toolAction_decal" ) );
		QAction *overlayToolAction = window.findChild<QAction *>( QStringLiteral( "toolAction_overlay" ) );
		QAction *undoAction = window.findChild<QAction *>( QStringLiteral( "actionUndo" ) );
		QAction *redoAction = window.findChild<QAction *>( QStringLiteral( "actionRedo" ) );
		QAction *tieEntityAction = window.findChild<QAction *>( QStringLiteral( "tieToEntityAction" ) );
		QMenu *selectionMenu = window.findChild<QMenu *>( QStringLiteral( "SelectionMenu" ) );
		QMenu *toolsMenu = window.findChild<QMenu *>( QStringLiteral( "ToolsMenu" ) );
		QComboBox *placeEntityCombo = window.findChild<QComboBox *>( QStringLiteral( "EntityToolCombo" ) );
		QComboBox *selectedEntityCombo = window.findChild<QComboBox *>( QStringLiteral( "SelectedEntityClass" ) );
		QLabel *materialStatus = window.findChild<QLabel *>( QStringLiteral( "MaterialStatus" ) );
		QLabel *placeEntityLabel = window.findChild<QLabel *>( QStringLiteral( "PlaceEntityLabel" ) );
		QLabel *selectedEntityLabel = window.findChild<QLabel *>( QStringLiteral( "SelectedEntityClassLabel" ) );
		QToolButton *undoButton = mapToolbar && undoAction ? qobject_cast<QToolButton *>( mapToolbar->widgetForAction( undoAction ) ) : nullptr;
		QToolButton *redoButton = mapToolbar && redoAction ? qobject_cast<QToolButton *>( mapToolbar->widgetForAction( redoAction ) ) : nullptr;
		const QRect undoButtonRect = undoButton ? QRect( undoButton->mapTo( &window, QPoint( 0, 0 ) ), undoButton->size() ) : QRect();
		const QRect redoButtonRect = redoButton ? QRect( redoButton->mapTo( &window, QPoint( 0, 0 ) ), redoButton->size() ) : QRect();
		bool mapToolbarFits = mapToolbar != nullptr;
		if ( mapToolbar ) for ( QAction *action : mapToolbar->actions() )
		{
			if ( action->isSeparator() ) continue;
			QToolButton *button = qobject_cast<QToolButton *>( mapToolbar->widgetForAction( action ) );
			const QRect rect = button ? QRect( button->mapTo( &window, QPoint( 0, 0 ) ), button->size() ) : QRect();
			mapToolbarFits = mapToolbarFits && button && !button->isHidden() && !button->text().isEmpty() && image.rect().contains( rect );
		}
		int namedEditTools = 0;
		if ( toolbar ) for ( QAction *action : toolbar->actions() )
		{
			if ( action->isSeparator() || action->icon().isNull() ) continue;
			QToolButton *button = qobject_cast<QToolButton *>( toolbar->widgetForAction( action ) );
			if ( !button || action->text().isEmpty() || action->toolTip().isEmpty() || button->isHidden() ) continue;
			++namedEditTools;
		}
		const bool commandSurfaceOk = mapToolbar && toolbar && undoAction && redoAction && undoButton && redoButton &&
			mapToolbar->toolButtonStyle() == Qt::ToolButtonTextUnderIcon &&
			toolbar->toolButtonStyle() == Qt::ToolButtonIconOnly && classicLayoutOk &&
			mapToolbarFits && namedEditTools == 9 &&
			undoAction->shortcut() == QKeySequence::Undo && redoAction->shortcut() == QKeySequence::Redo &&
			!undoButton->text().isEmpty() && !redoButton->text().isEmpty() &&
			image.rect().contains( undoButtonRect ) && image.rect().contains( redoButtonRect ) &&
			placeEntityCombo && selectedEntityCombo && placeEntityLabel && selectedEntityLabel &&
			materialStatus && !materialStatus->text().isEmpty() &&
			placeEntityLabel->buddy() == placeEntityCombo && selectedEntityLabel->buddy() == selectedEntityCombo &&
			!placeEntityCombo->accessibleName().isEmpty() && !selectedEntityCombo->accessibleName().isEmpty() &&
			tieEntityAction && tieEntityAction->shortcut() == QKeySequence( "Ctrl+T" ) &&
			!tieEntityAction->toolTip().isEmpty() && tieEntityAction->toolTip().contains( QStringLiteral( "Ctrl+T" ) ) &&
			selectionMenu && selectionMenu->actions().contains( tieEntityAction ) &&
			toolsMenu && toolsMenu->actions().contains( tieEntityAction ) &&
			cameraToolAction && cameraToolAction->shortcuts().contains( QKeySequence( "Shift+C" ) ) &&
			decalToolAction && decalToolAction->shortcuts().contains( QKeySequence( "Shift+D" ) ) && !decalToolAction->icon().isNull() &&
			overlayToolAction && overlayToolAction->shortcuts().contains( QKeySequence( "Shift+O" ) ) && !overlayToolAction->icon().isNull() &&
			!window.findChild<QAction *>( QStringLiteral( "actionAutoCombine" ) );
		ctx.Step( "ui-command-surface", commandSurfaceOk,
			QStringLiteral( "classic toolbar layout, tool tips, undo/redo, entity labels and tie-to-entity entry" ) );
		{
			CHammerDocument decalDoc;
			CQtViewport3D decalView;
			decalView.setDocument( &decalDoc );
			decalView.resize( 400, 300 );
			const double mins[3] = { 64, -32, 68 }, maxs[3] = { 96, 32, 132 };
			const double eye[3] = { 0, 0, 100 }, angles[3] = { 0, 0, 0 };
			const QString texture = QStringLiteral( "decals/stock_decal" );
			bool ok = decalDoc.CreateBrush( mins, maxs ) > 0 && decalDoc.SetActiveMaterial( texture );
			decalDoc.SetTool( TOOL_DECAL );
			decalView.setCameraPositionAngles( eye, angles );
			QMouseEvent press( QEvent::MouseButtonPress, QPointF( 200, 150 ), Qt::LeftButton, Qt::LeftButton, Qt::NoModifier );
			QCoreApplication::sendEvent( &decalView, &press );
			const auto hasDecal = [&]( const MapDocLite &map ) {
				if ( map.entities.size() != 1 ) return false;
				const EntityLite &entity = map.entities.front();
				return entity.classname == QLatin1String( "infodecal" ) &&
					entity.keyvalues.value( QStringLiteral( "texture" ) ) == texture &&
					std::abs( entity.origin[0] - 64 ) < 0.01 && std::abs( entity.origin[2] - 100 ) < 0.01;
			};
			ok = ok && hasDecal( decalDoc.Map() ) && decalDoc.Undo() && decalDoc.Map().entities.isEmpty() &&
				decalDoc.Redo() && hasDecal( decalDoc.Map() );
			QTemporaryDir dir;
			MapDocLite save = decalDoc.Map(), reloaded;
			const QString path = dir.filePath( QStringLiteral( "decal.vmf" ) );
			ok = ok && dir.isValid() && VmfIO::SaveMap( path, save ) && VmfIO::LoadMap( path, reloaded ) && hasDecal( reloaded );
			ctx.Step( "decal-placement", ok, QStringLiteral( "3D face hit, material binding, undo/redo and VMF round trip" ) );
		}
		{
			CHammerDocument doc;
			CQtViewport3D view;
			view.setDocument( &doc );
			view.resize( 400, 300 );
			const double mins[3] = { 64, -48, 52 }, maxs[3] = { 96, 48, 148 };
			const double eye[3] = { 0, 0, 100 }, angles[3] = { 0, 0, 0 };
			bool ok = doc.CreateBrush( mins, maxs ) > 0 && doc.SetActiveMaterial( QStringLiteral( "decals/test_overlay" ) );
			doc.SetTool( TOOL_OVERLAY );
			view.setCameraPositionAngles( eye, angles );
			const auto event = [&]( QEvent::Type type, const QPointF &point, Qt::MouseButton button, Qt::MouseButtons buttons ) {
				QMouseEvent mouse( type, point, button, buttons, Qt::NoModifier );
				QCoreApplication::sendEvent( &view, &mouse );
			};
			const QPointF center( 200, 150 );
			event( QEvent::MouseButtonPress, center, Qt::LeftButton, Qt::LeftButton );
			event( QEvent::MouseButtonRelease, center, Qt::LeftButton, Qt::NoButton );
			HammerOverlay::Data overlay;
			ok = ok && doc.Map().entities.size() == 1 && HammerOverlay::Parse( doc.Map().entities.front(), overlay );
			if ( ok )
			{
				const int id = doc.Map().entities.front().id;
				doc.SelectObject( -1 );
				event( QEvent::MouseButtonPress, center, Qt::LeftButton, Qt::LeftButton );
				event( QEvent::MouseButtonRelease, center, Qt::LeftButton, Qt::NoButton );
				ok = doc.IsSelected( id ) && doc.Map().entities.size() == 1;
				const auto point = HammerOverlay::WorldPoint( overlay, 0 );
				double cameraEye[3], cameraForward[3], cameraUp[3];
				view.cameraBasis( cameraEye, cameraForward, cameraUp );
				const HammerOverlay::Point delta{ point[0] - cameraEye[0], point[1] - cameraEye[1], point[2] - cameraEye[2] };
				const HammerOverlay::Point forward{ cameraForward[0], cameraForward[1], cameraForward[2] };
				const HammerOverlay::Point up{ cameraUp[0], cameraUp[1], cameraUp[2] };
				const auto right = HammerWinding::Cross( forward, up );
				const double scale = 150.0 / HammerWinding::Dot( delta, forward );
				const QPointF corner = center + QPointF( scale * HammerWinding::Dot( delta, right ), -scale * HammerWinding::Dot( delta, up ) );
				const QPointF moved = corner + ( corner - center ) * 0.4;
				const QString oldUv = HammerOverlay::Value( doc.Map().entities.front(), QStringLiteral( "uv0" ) );
				event( QEvent::MouseButtonPress, corner, Qt::LeftButton, Qt::LeftButton );
				event( QEvent::MouseMove, moved, Qt::NoButton, Qt::LeftButton );
				event( QEvent::MouseButtonRelease, moved, Qt::LeftButton, Qt::NoButton );
				const QString newUv = HammerOverlay::Value( doc.Map().entities.front(), QStringLiteral( "uv0" ) );
				ok = ok && oldUv != newUv && doc.Undo() &&
					HammerOverlay::Value( doc.Map().entities.front(), QStringLiteral( "uv0" ) ) == oldUv;
			}
			ctx.Step( "overlay-tool-gesture", ok, QStringLiteral( "3D face placement, overlay pick, corner drag and undo" ) );
		}
		{
			CMainWindow selectionWindow( nullptr, false );
			CHammerDocument *doc = selectionWindow.findChild<CHammerDocument *>();
			QComboBox *combo = selectionWindow.findChild<QComboBox *>( QStringLiteral( "SelectedEntityClass" ) );
			bool selectionClassOk = doc && combo && !combo->isEnabled() && combo->currentIndex() == -1 &&
				combo->currentText().isEmpty() && !combo->placeholderText().isEmpty();
			if ( selectionClassOk )
			{
				// Use deliberately unrelated first item to catch stale-class display.
				{ const QSignalBlocker blocker( combo ); combo->addItems( QStringList() << QStringLiteral( "ai_ally_manager" ) << QStringLiteral( "light" ) ); }
				EntityLite first; first.id = doc->Map().AllocId(); first.classname = QStringLiteral( "light" );
				first.keyvalues.insert( QStringLiteral( "custom_preserved" ), QStringLiteral( "value" ) );
				EntityLite custom; custom.id = doc->Map().AllocId(); custom.classname = QStringLiteral( "custom_external_class" );
				custom.keyvalues.insert( QStringLiteral( "custom_preserved" ), QStringLiteral( "other" ) );
				doc->Map().entities.append( first ); doc->Map().entities.append( custom );
				const double lo[3] = { 0, 0, 0 }, hi[3] = { 64, 64, 64 };
				const int brushId = CreateBoxSolid( doc->Map(), lo, hi );
				doc->SelectObject( first.id );
				selectionClassOk = combo->isEnabled() && combo->currentText() == first.classname;
				doc->SelectObject( custom.id );
				selectionClassOk = selectionClassOk && combo->isEnabled() && combo->currentText() == custom.classname;
				doc->SelectObjects( QSet<int>() << first.id << custom.id );
				selectionClassOk = selectionClassOk && !combo->isEnabled() && combo->currentIndex() == -1 && combo->currentText().isEmpty();
				doc->SelectObject( brushId );
				selectionClassOk = selectionClassOk && !combo->isEnabled() && combo->currentIndex() == -1 && combo->currentText().isEmpty();
				doc->SelectObjects( QSet<int>() );
				selectionClassOk = selectionClassOk && !combo->isEnabled() && combo->currentIndex() == -1 && combo->currentText().isEmpty() &&
					doc->Map().EntityById( first.id )->classname == first.classname && doc->Map().EntityById( first.id )->keyvalues == first.keyvalues &&
					doc->Map().EntityById( custom.id )->classname == custom.classname && doc->Map().EntityById( custom.id )->keyvalues == custom.keyvalues &&
					!doc->IsDirty() && !doc->Undo();
			}
			ctx.Step( "ui-selected-entity-class", selectionClassOk,
				QStringLiteral( "empty, multiple and brush selection clear class display; known/custom entities do not mutate on selection" ) );
		}
		// Exercise the actual View menu command and verify the 2D views stay put.
		CHammerDocument *center3dDoc = window.findChild<CHammerDocument *>();
		QAction *center3dAction = window.findChild<QAction *>( QStringLiteral( "center3dSelection" ) );
		QAction *followCameraAction = window.findChild<QAction *>( QStringLiteral( "followCamera2d" ) );
		QSettings followCameraSettings;
		const QString followCameraKey = QStringLiteral( "view2d/centerOnCamera" );
		const bool followCameraHadValue = followCameraSettings.contains( followCameraKey );
		const QVariant followCameraPrevious = followCameraSettings.value( followCameraKey );
		if ( followCameraAction ) followCameraAction->setChecked( false );
		CQtViewport3D *center3dView = window.findChild<CQtViewport3D *>( QStringLiteral( "Viewport3D" ) );
		CQtViewport2D *center2dView = window.findChild<CQtViewport2D *>( QStringLiteral( "ViewportTop" ) );
		CQtViewport2D *centerFront = window.findChild<CQtViewport2D *>( QStringLiteral( "ViewportFront" ) );
		CQtViewport2D *centerSide = window.findChild<CQtViewport2D *>( QStringLiteral( "ViewportSide" ) );
		bool center3dOk = center3dDoc && center3dAction && center3dView && center2dView && centerFront && centerSide &&
			center3dAction->shortcut() == QKeySequence( "Ctrl+Shift+E" );
		if ( center3dOk )
		{
			const double originalEye[3] = { 7, 8, 9 }, originalAngles[3] = { 0, 0, 0 };
			center3dView->setCameraPositionAngles( originalEye, originalAngles );
			const double topCenter[3] = { 400, 500, 600 };
			center2dView->centerOn( topCenter );
			centerFront->centerOn( topCenter ); centerSide->centerOn( topCenter );
			const QPointF topBefore = center2dView->centerWorld();
			const QPointF frontBefore = centerFront->centerWorld(), sideBefore = centerSide->centerWorld();
			center3dAction->trigger(); // empty selection must not move the camera
			double eye[3], forward[3], up[3];
			center3dView->cameraBasis( eye, forward, up );
			center3dOk = std::abs( eye[0] - 7 ) < 1e-6 && std::abs( eye[1] - 8 ) < 1e-6 && std::abs( eye[2] - 9 ) < 1e-6;
			EntityLite entity{}; entity.id = center3dDoc->Map().AllocId();
			entity.origin[0] = 100; entity.origin[1] = 20; entity.origin[2] = 30;
			center3dDoc->Map().entities.append( entity );
			center3dDoc->SelectObject( entity.id );
			center3dAction->trigger();
			center3dView->cameraBasis( eye, forward, up );
			center3dOk = center3dOk && std::abs( eye[0] - 100 ) < 1e-6 && std::abs( eye[1] + 80 ) < 1e-6 &&
				std::abs( eye[2] - 30 ) < 1e-6 && std::abs( forward[0] ) < 1e-6 && forward[1] > 0.999 &&
				center2dView->centerWorld() == topBefore && centerFront->centerWorld() == frontBefore &&
				centerSide->centerWorld() == sideBefore && !center3dDoc->IsDirty();
		}
		ctx.Step( "center-3d-selection", center3dOk, QStringLiteral( "original camera pose, empty selection, 2D view preserved and shortcut" ) );
		QAction *center2dAction = window.findChild<QAction *>( QStringLiteral( "center2dSelection" ) );
		QAction *centerCameraAction = window.findChild<QAction *>( QStringLiteral( "center2dCamera" ) );
		bool center2dOk = center3dOk && center2dAction && centerCameraAction;
		if ( center2dOk )
		{
			double eyeBefore[3], forward[3], up[3], eyeAfter[3];
			center3dView->cameraBasis( eyeBefore, forward, up );
			center2dAction->trigger();
			center3dView->cameraBasis( eyeAfter, forward, up );
			center2dOk = center2dView->centerWorld() == QPointF( 100, 20 ) &&
				centerFront->centerWorld() == QPointF( 100, 30 ) && centerSide->centerWorld() == QPointF( 20, 30 ) &&
				std::abs( eyeAfter[0] - eyeBefore[0] ) < 1e-6 && std::abs( eyeAfter[1] - eyeBefore[1] ) < 1e-6 &&
				std::abs( eyeAfter[2] - eyeBefore[2] ) < 1e-6;
			const double cameraEye[3] = { 11, 22, 33 }, cameraAngles[3] = { 0, 0, 0 };
			center3dView->setCameraPositionAngles( cameraEye, cameraAngles );
			centerCameraAction->trigger();
			const auto nearPoint = []( const QPointF &a, const QPointF &b ) {
				return std::abs( a.x() - b.x() ) < 1e-6 && std::abs( a.y() - b.y() ) < 1e-6;
			};
			center2dOk = center2dOk && nearPoint( center2dView->centerWorld(), QPointF( 11, 22 ) ) &&
				nearPoint( centerFront->centerWorld(), QPointF( 11, 33 ) ) && nearPoint( centerSide->centerWorld(), QPointF( 22, 33 ) ) &&
				!center3dDoc->IsDirty();
		}
		ctx.Step( "center-2d-views", center2dOk, QStringLiteral( "selection and camera position center all 2D views without changing 3D" ) );
		bool followOk = center2dOk && followCameraAction && followCameraAction->isCheckable();
		if ( followOk )
		{
			followCameraAction->setChecked( true );
			const double eye[3] = { 13, 24, 35 }, angles[3] = { 0, 90, 0 };
			center3dView->setCameraPositionAngles( eye, angles );
			const auto nearPoint = []( const QPointF &a, const QPointF &b ) {
				return std::abs( a.x() - b.x() ) < 1e-6 && std::abs( a.y() - b.y() ) < 1e-6;
			};
			followOk = nearPoint( center2dView->centerWorld(), QPointF( 13, 24 ) ) &&
				nearPoint( centerFront->centerWorld(), QPointF( 13, 35 ) ) &&
				nearPoint( centerSide->centerWorld(), QPointF( 24, 35 ) ) &&
				QSettings().value( followCameraKey ).toBool() && !center3dDoc->IsDirty();
			followCameraAction->setChecked( false );
			const QPointF before = center2dView->centerWorld();
			const double nextEye[3] = { 50, 60, 70 };
			center3dView->setCameraPositionAngles( nextEye, angles );
			followOk = followOk && center2dView->centerWorld() == before &&
				!QSettings().value( followCameraKey ).toBool() && !center3dDoc->IsDirty();
		}
		if ( followCameraHadValue ) followCameraSettings.setValue( followCameraKey, followCameraPrevious );
		else followCameraSettings.remove( followCameraKey );
		ctx.Step( "follow-camera-2d", followOk, QStringLiteral( "camera position follows only when enabled, setting persists and document remains unchanged" ) );
	}
	{
		QTemporaryDir tmp;
		const QString path = QDir( tmp.path() ).filePath( QStringLiteral( "hammer2-portable.ini" ) );
		bool ok = tmp.isValid() && PortableConfigPath( tmp.path() ).isEmpty() &&
			PortableDefault( tmp.path(), QStringLiteral( "game/fgdFiles" ), QStringLiteral( "fallback" ) ).toString() == QLatin1String( "fallback" );
		QSettings marker( path, QSettings::IniFormat );
		const QString value = QString::fromUtf8( "D:/\xe6\xb5\x8b\xe8\xaf\x95/game" );
		marker.setValue( QStringLiteral( "defaults/compile/gameDir" ), value );
		marker.sync();
		ok = ok && marker.status() == QSettings::NoError && PortableConfigPath( tmp.path() ) == path &&
			PortableDefault( tmp.path(), QStringLiteral( "compile/gameDir" ) ).toString() == value;
		ctx.Step( "portable-config", ok, QStringLiteral( "opt-in marker, defaults and Unicode paths" ) );
	}
	if ( qEnvironmentVariableIsSet( "HAMMER_SELFTEST_EXPECT_PORTABLE" ) )
	{
		const QString base = QCoreApplication::applicationDirPath();
		QSettings saved;
		const QString game = saved.value( QStringLiteral( "compile/gameDir" ), PortableDefault( base, QStringLiteral( "compile/gameDir" ) ) ).toString();
		const QString bin = saved.value( QStringLiteral( "compile/binDir" ), PortableDefault( base, QStringLiteral( "compile/binDir" ) ) ).toString();
		const QStringList fgds = saved.value( QStringLiteral( "game/fgdFiles" ), PortableDefault( base, QStringLiteral( "game/fgdFiles" ) ) ).toStringList();
		bool ok = QCoreApplication::applicationName() == QLatin1String( "hammer_qt_portable_gmod64" ) &&
			saved.format() == QSettings::IniFormat && !fgds.isEmpty() && QFileInfo::exists( fgds.first() ) &&
			QFileInfo::exists( QDir( game ).filePath( QStringLiteral( "gameinfo.txt" ) ) );
		for ( const char *tool : { "vbsp.exe", "vvis.exe", "vrad.exe" } )
			ok = ok && QFileInfo::exists( QDir( bin ).filePath( QString::fromLatin1( tool ) ) );
		ctx.Step( "portable-gmod-defaults", ok, QStringLiteral( "isolated INI, FGD, game and compiler paths" ) );
	}

	HammerLocalization *localization = QCoreApplication::instance()->findChild<HammerLocalization *>();
	const QString previousLanguage = localization ? localization->CurrentLanguage() : QString();
	bool bLanguageOk = localization != nullptr;
	if ( localization )
	{
		bool foundChinese = false;
		for ( const HammerLanguageInfo &language : localization->AvailableLanguages() )
			foundChinese = foundChinese || language.locale == QLatin1String( "zh_CN" );
		if ( foundChinese )
		{
			bLanguageOk = localization->LoadLanguage( QStringLiteral( "zh_CN" ) ) &&
				QCoreApplication::translate( "CMainWindow", "&File" ) != QLatin1String( "&File" ) &&
				QCoreApplication::translate( "VmfIO", "duplicate block %1" ).arg( QStringLiteral( "viewsettings" ) ) ==
					QString::fromUtf8( "重复的数据块 viewsettings" ) &&
				!localization->LoadLanguage( QStringLiteral( "missing_catalog" ) ) &&
				localization->CurrentLanguage() == QLatin1String( "zh_CN" );
			// Real Qt standard buttons use QPlatformTheme translations, not the
			// Hammer dialog's translation context.
			QMessageBox savePrompt;
			savePrompt.setStandardButtons( QMessageBox::Save | QMessageBox::Discard | QMessageBox::Cancel );
			QDialogButtonBox dialogButtons( QDialogButtonBox::Ok | QDialogButtonBox::Cancel );
			bLanguageOk = bLanguageOk &&
				savePrompt.button( QMessageBox::Save )->text() == QString::fromUtf8( "保存" ) &&
				savePrompt.button( QMessageBox::Discard )->text() == QString::fromUtf8( "不保存" ) &&
				savePrompt.button( QMessageBox::Cancel )->text() == QString::fromUtf8( "取消" ) &&
				dialogButtons.button( QDialogButtonBox::Ok )->text() == QString::fromUtf8( "确定" );
			// A freshly created dialog must return to English after switching.
			bLanguageOk = localization->LoadLanguage( QString() ) && bLanguageOk;
			QMessageBox englishPrompt;
			englishPrompt.setStandardButtons( QMessageBox::Save | QMessageBox::Discard | QMessageBox::Cancel );
			bLanguageOk = bLanguageOk && englishPrompt.button( QMessageBox::Save )->text() == QLatin1String( "Save" ) &&
				englishPrompt.button( QMessageBox::Discard )->text() == QLatin1String( "Discard" ) &&
				englishPrompt.button( QMessageBox::Cancel )->text() == QLatin1String( "Cancel" );
			bLanguageOk = localization->LoadLanguage( previousLanguage ) && bLanguageOk;
		}
		else
		{
			bLanguageOk = !localization->LoadLanguage( QStringLiteral( "missing_catalog" ) ) &&
				localization->CurrentLanguage() == previousLanguage;
		}
	}
	ctx.Step( "language-loading", bLanguageOk, QStringLiteral( "catalog discovery, translation, missing-catalog fallback" ) );

	//--- 2. GameConfig.txt ------------------------------------------------------
	const QString exeDir = QCoreApplication::applicationDirPath();
	GameConfigInfo info;
	QString error;
	const bool bConfigOk = LoadGameConfig( exeDir, info, &error );
	ctx.Step( "gameconfig", bConfigOk,
		bConfigOk ? QStringLiteral( "game '%1' fgd=%2%3" ).arg( info.gameName, info.fgdPath,
			info.bCreatedDefault ? QStringLiteral( " (default created)" ) : QString() )
			: error );

	//--- 3. FGD load (with built-in fallback) -----------------------------------
	CFgdData fgd;
	QString fgdUsed = info.fgdPath;
	bool bFgdOk = bConfigOk && fgd.Load( fgdUsed, &error );
	if ( !bFgdOk )
	{
		// Write the built-in minimal FGD to a temporary file and load that
		QTemporaryDir tempDir;
		if ( tempDir.isValid() )
		{
			const QString tempFgd = tempDir.filePath( QStringLiteral( "builtin_selftest.fgd" ) );
			QFile file( tempFgd );
			if ( file.open( QIODevice::WriteOnly | QIODevice::Text ) )
			{
				file.write( k_builtinFgd, sizeof( k_builtinFgd ) - 1 );
				file.close();
			}

			fgdUsed = tempFgd;
			bFgdOk = fgd.Load( fgdUsed, &error );
			ctx.Step( "fgd-fallback", bFgdOk,
				bFgdOk ? QStringLiteral( "builtin FGD at %1" ).arg( tempFgd ) : error );
		}
		else
		{
			ctx.Step( "fgd-fallback", false, QStringLiteral( "no temp dir: %1" ).arg( error ) );
		}
	}
	else
	{
		ctx.Step( "fgd-load", true, QStringLiteral( "%1" ).arg( fgdUsed ) );
	}

	const int nPointClasses = fgd.PointClassNames().count();
	const bool bClassesOk = fgd.IsValid() && nPointClasses >= 3 &&
		fgd.HasClass( "light" ) && fgd.HasClass( "info_player_start" ) && fgd.HasClass( "prop_static" );
	ctx.Step( "fgd-classes", bClassesOk, QStringLiteral( "%1 point classes" ).arg( nPointClasses ) );
	{
		QTemporaryDir multiDir;
		const QString one = multiDir.filePath( QStringLiteral( "one.fgd" ) );
		const QString two = multiDir.filePath( QStringLiteral( "two.fgd" ) );
		QFile first( one ), second( two );
		bool ok = multiDir.isValid() && first.open( QIODevice::WriteOnly ) &&
			first.write( "@PointClass = first_class []\n@PointClass = shared_class [ x(string) : \"X\" : \"one\" ]\n" ) > 0;
		first.close();
		ok = ok && second.open( QIODevice::WriteOnly ) &&
			second.write( "@PointClass = second_class []\n@PointClass = shared_class [ x(string) : \"X\" : \"two\" ]\n" ) > 0;
		second.close();
		CFgdData multiple;
		ok = ok && multiple.LoadFiles( QStringList() << one << two ) && multiple.HasClass( "first_class" ) &&
			multiple.HasClass( "second_class" ) && multiple.DefaultKeyValues( "shared_class" ).value( "x" ) == QLatin1String( "two" );
		ok = ok && !multiple.LoadFiles( QStringList() << one << multiDir.filePath( "missing.fgd" ) ) &&
			multiple.HasClass( "second_class" );
		QFile config( multiDir.filePath( QStringLiteral( "GameConfig.txt" ) ) );
		ok = ok && config.open( QIODevice::WriteOnly ) && config.write( "\"Configs\" { \"Games\" { \"Test\" { \"GameDir\" \"game\" \"Hammer\" { \"GameData0\" \"one.fgd\" \"GameData1\" \"two.fgd\" } } } \"SDKVersion\" \"3\" }" ) > 0;
		config.close();
		GameConfigInfo parsed;
		ok = ok && LoadGameConfig( multiDir.path(), parsed ) && parsed.fgdPaths == ( QStringList() << one << two );
		ctx.Step( "fgd-multiple", ok, QStringLiteral( "ordered overrides, failure preserves previous data, nested config" ) );
	}
	QTemporaryDir choiceDir;
	QFile choiceFile( choiceDir.filePath( QStringLiteral( "choices.fgd" ) ) );
	bool choicesOk = choiceDir.isValid() && choiceFile.open( QIODevice::WriteOnly );
	if ( choicesOk )
	{
		choiceFile.write( "@PointClass = choice_test : \"Test\" [ mode(choices) : \"Mode\" : 0 = [ 0 : \"Same\" 1 : \"Same\" 2 : \"Other\" ] ]\n" );
		choiceFile.close();
		CFgdData choiceFgd;
		choicesOk = choiceFgd.Load( choiceFile.fileName() );
		const auto choices = choiceFgd.PropertyChoices( QStringLiteral( "choice_test" ), QStringLiteral( "MODE" ) );
		choicesOk = choicesOk && choices.size() == 3 && choices[0].first == QLatin1String( "0" ) &&
			choices[1].first == QLatin1String( "1" ) && choices[0].second == choices[1].second &&
			choiceFgd.PropertyChoices( QStringLiteral( "choice_test" ), QStringLiteral( "missing" ) ).isEmpty();
	}
	ctx.Step( "fgd-choice-values", choicesOk, QStringLiteral( "case-insensitive lookup preserves values with duplicate captions" ) );
	CHammerDocument choiceDoc;
	choiceDoc.NewDocument();
	const double choiceOrigin[3] = { 0, 0, 0 };
	bool choiceEditorOk = choicesOk && choiceDoc.LoadFgd( choiceFile.fileName() ) &&
		choiceDoc.PlaceEntity( QStringLiteral( "choice_test" ), choiceOrigin ) > 0 &&
		choiceDoc.SetSelectedEntityKey( QStringLiteral( "mode" ), QStringLiteral( "custom" ) ) &&
		choiceDoc.SaveToFile( choiceDir.filePath( QStringLiteral( "choice.vmf" ) ) );
	QTreeWidget choiceTree;
	choiceTree.setColumnCount( 2 );
	QTreeWidgetItem *choiceItem = new QTreeWidgetItem( &choiceTree );
	choiceItem->setText( 1, QStringLiteral( "custom" ) );
	QVariantList choicePairs;
	for ( const auto &pair : choiceDoc.Fgd().PropertyChoices( QStringLiteral( "choice_test" ), QStringLiteral( "mode" ) ) )
		choicePairs.append( QStringList() << pair.first << pair.second );
	choiceItem->setData( 1, Qt::UserRole + 1, choicePairs );
	QObject::connect( &choiceTree, &QTreeWidget::itemChanged, &choiceDoc,
		[&choiceDoc]( QTreeWidgetItem *item, int column ) {
			if ( column == 1 ) choiceDoc.SetSelectedEntityKey( QStringLiteral( "mode" ), item->text( 1 ) );
		} );
	const QModelIndex choiceIndex = choiceTree.model()->index( 0, 1 );
	CPropertyValueDelegate choiceDelegate;
	QScopedPointer<QWidget> choiceEditor( choiceDelegate.createEditor( &choiceTree, QStyleOptionViewItem(), choiceIndex ) );
	QComboBox *choiceCombo = qobject_cast<QComboBox *>( choiceEditor.data() );
	choiceEditorOk = choiceEditorOk && choiceCombo && choiceCombo->currentData().toString() == QLatin1String( "custom" ) && !choiceDoc.IsDirty();
	if ( choiceCombo )
	{
		choiceDelegate.setEditorData( choiceCombo, choiceIndex );
		choiceCombo->setCurrentIndex( choiceCombo->findData( QStringLiteral( "1" ) ) );
		choiceDelegate.setModelData( choiceCombo, choiceTree.model(), choiceIndex );
		const EntityLite *choiceEntity = choiceDoc.Map().EntityById( choiceDoc.SelectedId() );
		choiceEditorOk = choiceEditorOk && choiceEntity && choiceEntity->keyvalues.value( QStringLiteral( "mode" ) ) == QLatin1String( "1" ) &&
			choiceDoc.Undo();
		choiceEntity = choiceDoc.Map().EntityById( choiceDoc.SelectedId() );
		choiceEditorOk = choiceEditorOk && choiceEntity && choiceEntity->keyvalues.value( QStringLiteral( "mode" ) ) == QLatin1String( "custom" ) && !choiceDoc.IsDirty();
	}
	ctx.Step( "fgd-choice-editor", choiceEditorOk, QStringLiteral( "unknown value, duplicate captions, raw value writeback and undo" ) );

	QFile flagFile( choiceDir.filePath( QStringLiteral( "flags.fgd" ) ) );
	bool flagsOk = flagFile.open( QIODevice::WriteOnly );
	if ( flagsOk )
	{
		flagFile.write( "@PointClass = flag_test : \"Test\" [ spawnflags(flags) = [ 1 : \"One\" : 0 2 : \"Two\" : 0 ] ]\n" );
		flagFile.close();
	}
	CFgdData flagFgd;
	flagsOk = flagsOk && flagFgd.Load( flagFile.fileName() );
	const auto flagPairs = flagFgd.PropertyFlags( QStringLiteral( "flag_test" ), QStringLiteral( "SPAWNFLAGS" ) );
	flagsOk = flagsOk && flagPairs.size() == 2 && flagPairs[0].first == QLatin1String( "1" ) && flagPairs[1].first == QLatin1String( "2" );
	QTreeWidget flagTree;
	flagTree.setColumnCount( 2 );
	QTreeWidgetItem *flagItem = new QTreeWidgetItem( &flagTree );
	flagItem->setText( 1, QStringLiteral( "9" ) ); // Unknown bit 8 must survive.
	QVariantList flagValues;
	for ( const auto &pair : flagPairs ) flagValues.append( QStringList() << pair.first << pair.second );
	flagItem->setData( 1, Qt::UserRole + 2, flagValues );
	const QModelIndex flagIndex = flagTree.model()->index( 0, 1 );
	CPropertyValueDelegate flagDelegate;
	QScopedPointer<QWidget> flagEditor( flagDelegate.createEditor( &flagTree, QStyleOptionViewItem(), flagIndex ) );
	CPropertyFlagsEditor *flagCombo = dynamic_cast<CPropertyFlagsEditor *>( flagEditor.data() );
	flagsOk = flagsOk && flagCombo && flagCombo->count() == 2;
	if ( flagCombo )
	{
		QMetaObject::invokeMethod( flagCombo, "activated", Q_ARG( int, 1 ) );
		flagDelegate.setModelData( flagCombo, flagTree.model(), flagIndex );
		flagsOk = flagsOk && flagItem->text( 1 ) == QLatin1String( "11" );
	}
	flagItem->setText( 1, QStringLiteral( "invalid" ) );
	QScopedPointer<QWidget> invalidFlagEditor( flagDelegate.createEditor( &flagTree, QStyleOptionViewItem(), flagIndex ) );
	flagsOk = flagsOk && !dynamic_cast<CPropertyFlagsEditor *>( invalidFlagEditor.data() );
	ctx.Step( "fgd-flag-editor", flagsOk, QStringLiteral( "case-insensitive lookup, toggle, preserve unknown bits and invalid text" ) );

	//--- 4. Build the test map ---------------------------------------------------
	MapDocLite doc;
	MapDocLite idDoc;
	ctx.Step( "worldspawn-id-reserved", idDoc.AllocId() == 2, QStringLiteral( "first object id follows worldspawn id 1" ) );
	doc.SetWorldspawnValue( "classname", "worldspawn" );
	doc.SetWorldspawnValue( "detailmaterial", "detail/detailsprites" );
	doc.SetWorldspawnValue( "detailvbsp", "detail.vbsp" );
	doc.SetWorldspawnValue( "maxpropscreenwidth", "-1" );
	doc.SetWorldspawnValue( "skyname", "sky_day01_01" );
	doc.SetWorldspawnValue( "mapversion", "1" );
	ctx.Step( "worldspawn", doc.WorldspawnValue( "classname" ) == QLatin1String( "worldspawn" ),
		QStringLiteral( "skyname=%1" ).arg( doc.WorldspawnValue( "skyname" ) ) );

	// Two brushes with different sizes (grid aligned)
	const double mins1[3] = { -64.0, -64.0, 0.0 };
	const double maxs1[3] = { 64.0, 64.0, 64.0 };
	const double mins2[3] = { 128.0, 0.0, 0.0 };
	const double maxs2[3] = { 256.0, 128.0, 32.0 };
	const int nSolid1 = CreateBoxSolid( doc, mins1, maxs1 );
	const int nSolid2 = CreateBoxSolid( doc, mins2, maxs2 );
	ctx.Step( "brushes", nSolid1 >= 0 && nSolid2 >= 0 && doc.solids.count() == 2 &&
		doc.solids[0].sides.count() == 6 && doc.solids[1].sides.count() == 6,
		QStringLiteral( "ids %1,%2 (6 sides each)" ).arg( nSolid1 ).arg( nSolid2 ) );

	// Three entities: light with FGD defaults, player start, prop_static
	const int nLight = AddEntity( doc, fgd, QStringLiteral( "light" ), -128.0, 0.0, 64.0 );
	const int nPlayerStart = AddEntity( doc, fgd, QStringLiteral( "info_player_start" ), 0.0, 0.0, 0.0 );
	const int nPropStatic = AddEntity( doc, fgd, QStringLiteral( "prop_static" ), 64.0, 0.0, 0.0 );

	// Give the prop a model so the assertion is independent of FGD defaults
	EntityLite *pProp = doc.EntityById( nPropStatic );
	if ( pProp )
		pProp->keyvalues.insert( "model", "models/error.mdl" );

	const EntityLite *pLight = doc.EntityById( nLight );
	const bool bLightDefault = pLight && pLight->keyvalues.contains( "_light" );
	ctx.Step( "entity-defaults", bLightDefault, pLight ?
		QStringLiteral( "_light=%1" ).arg( pLight->keyvalues.value( "_light" ) ) : QString() );

	ctx.Step( "entities", doc.entities.count() == 3,
		QStringLiteral( "ids %1,%2,%3" ).arg( nLight ).arg( nPlayerStart ).arg( nPropStatic ) );

	//--- 5. Save the VMF ----------------------------------------------------------
	QFileInfo vmfInfo( vmfPath );
	QDir( vmfInfo.absolutePath() ).mkpath( "." );

	QString ioError;
	const bool bSaved = VmfIO::SaveMap( vmfPath, doc, &ioError );
	ctx.Step( "save-vmf", bSaved, bSaved ? vmfPath : ioError );

	//--- 6. Reload the VMF ---------------------------------------------------------
	MapDocLite loaded;
	const bool bLoaded = bSaved && VmfIO::LoadMap( vmfPath, loaded, &ioError );
	ctx.Step( "load-vmf", bLoaded, bLoaded ? vmfPath : ioError );

	QTemporaryDir recoveryDir;
	const QString protectedSource = recoveryDir.filePath( QStringLiteral( "source.vmf" ) );
	QFile sourceSentinel( protectedSource );
	const bool sourceCreated = sourceSentinel.open( QIODevice::WriteOnly ) && sourceSentinel.write( "original" ) == 8;
	sourceSentinel.close();
	CHammerDocument recoverySource;
	recoverySource.NewDocument();
	const double recoveryMins[3] = { 0, 0, 0 }, recoveryMaxs[3] = { 64, 64, 64 };
	const int recoveryBrush = recoverySource.CreateBrush( recoveryMins, recoveryMaxs );
	CHammerRecoveryStore recoveryStore( recoveryDir.filePath( QStringLiteral( "store" ) ) );
	HammerRecoveryInfo recoveryInfo;
	bool recoveryOk = sourceCreated && recoveryBrush >= 0 &&
		recoveryStore.Write( recoverySource.Map(), protectedSource ) && recoveryStore.Read( recoveryInfo );
	const QString firstRecoveryPath = recoveryInfo.vmfPath;
	const QString secondSourcePath = recoveryDir.filePath( QStringLiteral( "second.vmf" ) );
	#ifdef _WIN32
	HANDLE manifestLock = CreateFileW( reinterpret_cast<LPCWSTR>( recoveryStore.ManifestPath().utf16() ), GENERIC_READ,
		FILE_SHARE_READ, nullptr, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, nullptr );
	HammerRecoveryInfo retainedRecovery;
	QString manifestError;
	const bool failedCommitPreserved = manifestLock != INVALID_HANDLE_VALUE &&
		!recoveryStore.Write( recoverySource.Map(), secondSourcePath, &manifestError ) &&
		recoveryStore.Read( retainedRecovery ) && retainedRecovery.vmfPath == firstRecoveryPath &&
		QFile::exists( firstRecoveryPath ) && QDir( recoveryDir.filePath( QStringLiteral( "store" ) ) )
			.entryList( QStringList() << QStringLiteral( "recovery_*.vmf" ), QDir::Files ).size() == 1;
	if ( manifestLock != INVALID_HANDLE_VALUE ) CloseHandle( manifestLock );
	recoveryOk = recoveryOk && failedCommitPreserved;
	#endif
	recoveryOk = recoveryOk && recoveryStore.Write( recoverySource.Map(), secondSourcePath ) &&
		recoveryStore.Read( recoveryInfo ) && recoveryInfo.sourcePath == secondSourcePath &&
		recoveryInfo.vmfPath != firstRecoveryPath && !QFile::exists( firstRecoveryPath );
	CHammerDocument recoveredDocument;
	recoveryOk = recoveryOk &&
		recoveredDocument.RestoreFromRecovery( recoveryInfo.vmfPath, recoveryInfo.sourcePath ) &&
		recoveredDocument.IsDirty() && recoveredDocument.GetFileName() == secondSourcePath &&
		recoveredDocument.Map().solids.size() == 1 && !QFile::exists( secondSourcePath );
	QFile sourceCheck( protectedSource );
	recoveryOk = recoveryOk && sourceCheck.open( QIODevice::ReadOnly ) && sourceCheck.readAll() == QByteArray( "original" );
	sourceCheck.close();
	recoveryStore.Clear();
	HammerRecoveryInfo discardedRecovery;
	recoveryOk = recoveryOk && !recoveryStore.Read( discardedRecovery ) && !QFile::exists( recoveryInfo.vmfPath ) && !QFile::exists( recoveryStore.ManifestPath() );
	ctx.Step( "unsaved-map-recovery", recoveryOk, QStringLiteral( "generation swap, source path, dirty restore and discard without source overwrite" ) );

	//--- 7. Assertions --------------------------------------------------------------
	ctx.Step( "assert-entity-count", loaded.entities.count() == doc.entities.count(),
		QStringLiteral( "%1 loaded vs %2 original" ).arg( loaded.entities.count() ).arg( doc.entities.count() ) );

	ctx.Step( "assert-solid-count", loaded.solids.count() == doc.solids.count(),
		QStringLiteral( "%1 loaded vs %2 original" ).arg( loaded.solids.count() ).arg( doc.solids.count() ) );

	ctx.Step( "assert-worldspawn",
		loaded.WorldspawnValue( "classname" ) == QLatin1String( "worldspawn" ) &&
		loaded.WorldspawnValue( "skyname" ) == doc.WorldspawnValue( "skyname" ),
		QStringLiteral( "skyname=%1" ).arg( loaded.WorldspawnValue( "skyname" ) ) );

	// Key/values, origins and planes must survive the round trip
	QString mismatch;
	bool bKvOk = true;
	for ( const EntityLite &original : doc.entities )
	{
		const EntityLite *pLoaded = nullptr;
		for ( const EntityLite &candidate : loaded.entities )
		{
			if ( candidate.classname == original.classname )
			{
				pLoaded = &candidate;
				break;
			}
		}

		if ( !pLoaded )
		{
			mismatch = QStringLiteral( "missing entity %1" ).arg( original.classname );
			bKvOk = false;
			break;
		}

		if ( !KeyValuesEqual( original.keyvalues, pLoaded->keyvalues, &mismatch ) ||
			!OriginsEqual( original.origin, pLoaded->origin, &mismatch ) )
		{
			bKvOk = false;
			break;
		}
	}
	ctx.Step( "assert-kv-roundtrip", bKvOk, bKvOk ? QStringLiteral( "3 entities match" ) : mismatch );

	bool bPlanesOk = ( loaded.solids.count() == doc.solids.count() );
	mismatch.clear();
	if ( bPlanesOk )
	{
		for ( int i = 0; i < doc.solids.count() && bPlanesOk; ++i )
		{
			const SolidLite &src = doc.solids[i];
			const SolidLite &dst = loaded.solids[i];
			if ( src.sides.count() != dst.sides.count() )
			{
				mismatch = QStringLiteral( "solid %1: %2 sides vs %3" )
					.arg( i ).arg( src.sides.count() ).arg( dst.sides.count() );
				bPlanesOk = false;
				break;
			}

			for ( int s = 0; s < src.sides.count(); ++s )
			{
				for ( int p = 0; p < 3; ++p )
				{
					if ( !OriginsEqual( src.sides[s].planePts[p], dst.sides[s].planePts[p], &mismatch ) )
					{
						mismatch.prepend( QStringLiteral( "solid %1 side %2 pt %3: " ).arg( i ).arg( s ).arg( p ) );
						bPlanesOk = false;
						break;
					}
				}
				if ( !bPlanesOk )
					break;
			}
		}
	}
	ctx.Step( "assert-plane-roundtrip", bPlanesOk, bPlanesOk ? QStringLiteral( "12 sides match" ) : mismatch );

	// Unsupported nested data used to be silently discarded on save. Confirm
	// that the loader refuses representative original-Hammer constructs.
	QTemporaryDir guardDir;
	bool bGuardOk = guardDir.isValid();
	const char *guardFixtures[] = {
		"world { \"id\" \"1\" \"classname\" \"worldspawn\" } entity { \"id\" \"2\" \"classname\" \"func_door\" solid { \"id\" \"3\" unsupported { } } }",
		"world { \"id\" \"1\" \"classname\" \"worldspawn\" solid { \"id\" \"2\" side { \"id\" \"3\" dispinfo { \"power\" \"2\" } } } }",
		"visgroups { visgroup { \"name\" \"test\" } } world { \"id\" \"1\" \"classname\" \"worldspawn\" }",
		"world { \"id\" \"1\" \"classname\" \"worldspawn\" } cameras { camera { nested { \"key\" \"value\" } } }",
		"world { \"id\" \"1\" \"classname\" \"worldspawn\" } cameras { } cameras { }",
		"world { \"id\" \"1\" \"classname\" \"worldspawn\" } cordon { nested { \"key\" \"value\" } }",
		"world { \"id\" \"1\" \"classname\" \"worldspawn\" } cordon { } cordon { }",
		"world { \"id\" \"1\" \"classname\" \"worldspawn\" } versioninfo { nested { \"key\" \"value\" } }",
		"world { \"id\" \"1\" \"classname\" \"worldspawn\" } versioninfo { } versioninfo { }",
		"viewsettings { nested { } } world { \"id\" \"1\" \"classname\" \"worldspawn\" }",
		"viewsettings { } viewsettings { } world { \"id\" \"1\" \"classname\" \"worldspawn\" }",
		"cordons { cordon { } } world { \"id\" \"1\" \"classname\" \"worldspawn\" }",
		"cordon { } cordons { } world { \"id\" \"1\" \"classname\" \"worldspawn\" }",
		"world { \"id\" \"1\" \"classname\" \"worldspawn\" solid { \"id\" \"2\" side { \"id\" \"3\" vertices_plus { \"v\" \"not a point\" } } } }",
		"world { \"id\" \"1\" \"classname\" \"worldspawn\" solid { \"id\" \"2\" side { \"id\" \"3\" vertices_plus { \"unknown\" \"1 2 3\" } } } }",
		"world { \"id\" \"1\" \"classname\" \"worldspawn\" solid { \"id\" \"2\" side { \"id\" \"3\" vertices_plus { } vertices_plus { } } } }",
		"world { \"id\" \"1\" \"classname\" \"worldspawn\" } entity { \"id\" \"2\" \"classname\" \"info_target\" editor { \"visgroupshown\" \"maybe\" } }",
		"world { \"id\" \"1\" \"classname\" \"worldspawn\" } entity { \"id\" \"2\" \"classname\" \"info_target\" editor { \"visgroupid\" \"bad\" } }",
		"visgroups { visgroup { \"visgroupid\" \"7\" \"visible\" \"0\" } } world { \"id\" \"1\" \"classname\" \"worldspawn\" }",
		"world { \"id\" \"1\" \"classname\" \"worldspawn\" hidden { entity { \"id\" \"2\" } } }",
		"hidden { unknown { } } world { \"id\" \"1\" \"classname\" \"worldspawn\" }",
		"world { \"id\" \"1\" \"classname\" \"worldspawn\" } entity { \"id\" \"2\" \"classname\" \"info_target\" editor { } editor { } }"
	};
	for ( int i = 0; bGuardOk && i < int( sizeof( guardFixtures ) / sizeof( guardFixtures[0] ) ); ++i )
	{
		const QString fixturePath = guardDir.filePath( QStringLiteral( "guard%1.vmf" ).arg( i ) );
		QFile fixture( fixturePath );
		if ( !fixture.open( QIODevice::WriteOnly ) || fixture.write( guardFixtures[i] ) < 0 )
		{
			bGuardOk = false;
			break;
		}
		fixture.close();
		MapDocLite rejected;
		QString guardError;
		bGuardOk = !VmfIO::LoadMap( fixturePath, rejected, &guardError ) && !guardError.isEmpty();
	}
	ctx.Step( "unsupported-vmf-guard", bGuardOk, QStringLiteral( "unsupported brush-entity nesting, displacements and malformed visgroups" ) );

	// Preserve owner IDs and solids from several brush entities alongside world geometry.
	MapDocLite brushMap;
	EntityLite brushEntA, brushEntB;
	brushEntA.id = brushMap.AllocId(); brushEntA.classname = QStringLiteral( "func_detail" );
	brushEntA.keyvalues.insert( QStringLiteral( "targetname" ), QStringLiteral( "detail_a" ) );
	brushEntB.id = brushMap.AllocId(); brushEntB.classname = QStringLiteral( "func_door" );
	brushEntB.connections.append( qMakePair( QStringLiteral( "OnOpen" ), QStringLiteral( "detail_a,Kill,,0,-1" ) ) );
	brushMap.entities.append( brushEntA ); brushMap.entities.append( brushEntB );
	const double brushMins[3] = { 64, 64, 64 }, brushMaxs[3] = { 96, 96, 96 };
	const int worldBrush = CreateBoxSolid( brushMap, brushMins, brushMaxs );
	const int ownedA = CreateBoxSolid( brushMap, brushMins, brushMaxs );
	const int ownedB = CreateBoxSolid( brushMap, brushMins, brushMaxs );
	brushMap.SolidById( ownedA )->ownerEntityId = brushEntA.id;
	brushMap.SolidById( ownedB )->ownerEntityId = brushEntB.id;
	const int ownedFace = brushMap.SolidById( ownedA )->sides.first().id;
	const QString brushPath = guardDir.filePath( "brush_entity.vmf" );
	QString brushError;
	MapDocLite brushReload;
	bool brushRoundTrip = VmfIO::SaveMap( brushPath, brushMap, &brushError ) &&
		VmfIO::LoadMap( brushPath, brushReload, &brushError ) && brushReload.solids.size() == 3 &&
		brushReload.SolidById( worldBrush ) && brushReload.SolidById( worldBrush )->ownerEntityId == 0 &&
		brushReload.SolidById( ownedA ) && brushReload.SolidById( ownedA )->ownerEntityId == brushEntA.id &&
		brushReload.SolidById( ownedA )->sides.first().id == ownedFace &&
		brushReload.SolidById( ownedB ) && brushReload.SolidById( ownedB )->ownerEntityId == brushEntB.id &&
		brushReload.EntityById( brushEntB.id ) && brushReload.EntityById( brushEntB.id )->connections == brushEntB.connections;
	MapDocLite orphan = brushMap;
	orphan.SolidById( ownedA )->ownerEntityId = 999999;
	brushRoundTrip = brushRoundTrip && !VmfIO::SaveMap( guardDir.filePath( "orphan.vmf" ), orphan, &brushError );
	ctx.Step( "brush-entity-vmf-roundtrip", brushRoundTrip, QStringLiteral( "world and multiple brush entities, ownership, face IDs, outputs and orphan guard: %1" ).arg( brushError ) );
	QFile savedBrushFile( brushPath );
	bool badBrushGuard = savedBrushFile.open( QIODevice::ReadOnly );
	const QByteArray validBrushFixture = badBrushGuard ? savedBrushFile.readAll() : QByteArray();
	const QByteArray ownedIdLine = "\t\t\"id\" \"" + QByteArray::number( ownedA ) + "\"";
	badBrushGuard = badBrushGuard && validBrushFixture.count( ownedIdLine ) == 1;
	QByteArray duplicateBrushFixture = validBrushFixture;
	duplicateBrushFixture.replace( ownedIdLine, "\t\t\"id\" \"" + QByteArray::number( brushEntA.id ) + "\"" );
	QByteArray missingBrushFixture = validBrushFixture;
	missingBrushFixture.replace( ownedIdLine, QByteArray() );
	const QList<QByteArray> badBrushFixtures = {
		duplicateBrushFixture, missingBrushFixture,
		"world { \"id\" \"1\" \"classname\" \"worldspawn\" } entity { \"id\" \"2\" \"classname\" \"func_detail\" solid { \"id\" \"3\" side { \"id\" \"4\" dispinfo { \"power\" \"2\" } } } }"
	};
	for ( int i = 0; i < badBrushFixtures.size(); ++i )
	{
		const QString path = guardDir.filePath( QStringLiteral( "bad_brush_%1.vmf" ).arg( i ) );
		QFile file( path );
		if ( !file.open( QIODevice::WriteOnly ) || file.write( badBrushFixtures[i] ) != badBrushFixtures[i].size() ) { badBrushGuard = false; break; }
		file.close();
		MapDocLite rejected = brushMap;
		QString error;
		if ( VmfIO::LoadMap( path, rejected, &error ) || error.isEmpty() ) badBrushGuard = false;
		if ( i < 2 && error != QCoreApplication::translate( "VmfIO", "Unsupported VMF data: invalid or duplicate object id." ) ) badBrushGuard = false;
		if ( !file.open( QIODevice::ReadOnly ) || file.readAll() != badBrushFixtures[i] ) badBrushGuard = false;
	}
	ctx.Step( "brush-entity-vmf-guard", badBrushGuard, QStringLiteral( "duplicate or missing object ID and unsupported nested data are rejected without changing the file" ) );
	CHammerDocument brushViewDocument;
	brushViewDocument.Map() = brushMap;
	const int brushPick = HammerViewPick::Pick2D( brushViewDocument, 0, 1, 80, 80, 0, 1 );
	const double regionLo[2] = { 63, 63 }, regionHi[2] = { 97, 97 };
	const QSet<int> brushRegion = HammerViewPick::Region2D( brushViewDocument, 0, 1, regionLo, regionHi, false );
	bool brushViewOk = brushPick == brushEntB.id && brushRegion.contains( worldBrush ) &&
		brushRegion.contains( brushEntA.id ) && brushRegion.contains( brushEntB.id ) &&
		!brushRegion.contains( ownedA ) && !brushRegion.contains( ownedB );
	ViewFilter hiddenOwner = brushViewDocument.GetViewFilter();
	hiddenOwner.quickHiddenIds.insert( brushEntB.id );
	brushViewDocument.SetViewFilter( hiddenOwner );
	brushViewOk = brushViewOk && HammerViewPick::Pick2D( brushViewDocument, 0, 1, 80, 80, 0, 1 ) == brushEntA.id;
	brushViewOk = brushViewOk && !SelectBrushById( brushViewDocument, ownedB ) &&
		SelectBrushById( brushViewDocument, ownedA ) && brushViewDocument.IsSelected( brushEntA.id );
	double brushIssueCenter[3] = {};
	const MapCheckIssue brushIssue = { MapCheckKind::InvalidTextureAxes, ownedA, ownedFace };
	brushViewOk = brushViewOk && MapCheckGoToIssue( brushViewDocument, brushIssue, brushIssueCenter ) &&
		brushViewDocument.IsSelected( brushEntA.id ) && brushIssueCenter[0] == 80;
	ctx.Step( "brush-entity-view-picking", brushViewOk, QStringLiteral( "owned solids pick their entity and honor hidden owner" ) );
	// A map-check issue on an entity must not clear an unrelated selection
	// when all of the entity's child brushes are hidden.
	CHammerDocument hiddenBrushIssueDoc;
	hiddenBrushIssueDoc.Map() = brushMap;
	const int anotherOwned = CreateBoxSolid( hiddenBrushIssueDoc.Map(), brushMins, brushMaxs );
	hiddenBrushIssueDoc.Map().SolidById( anotherOwned )->ownerEntityId = brushEntA.id;
	ViewFilter hiddenChildren = hiddenBrushIssueDoc.GetViewFilter();
	hiddenChildren.quickHiddenIds.insert( ownedA );
	hiddenChildren.quickHiddenIds.insert( anotherOwned );
	hiddenBrushIssueDoc.SetViewFilter( hiddenChildren );
	hiddenBrushIssueDoc.SelectObjects( QSet<int>{ worldBrush } );
	double hiddenIssueCenter[3] = { 7, 8, 9 };
	const MapCheckIssue ownedOutputIssue = { MapCheckKind::OutputEmptyTarget, brushEntA.id, 0 };
	bool hiddenBrushIssueOk = hiddenBrushIssueDoc.IsSelected( worldBrush ) &&
		!MapCheckGoToIssue( hiddenBrushIssueDoc, ownedOutputIssue, hiddenIssueCenter ) &&
		hiddenBrushIssueDoc.SelectedIds() == QSet<int>{ worldBrush } &&
		hiddenIssueCenter[0] == 7 && hiddenIssueCenter[1] == 8 && hiddenIssueCenter[2] == 9;
	hiddenChildren.quickHiddenIds.remove( anotherOwned );
	hiddenBrushIssueDoc.SetViewFilter( hiddenChildren );
	hiddenBrushIssueOk = hiddenBrushIssueOk && MapCheckGoToIssue( hiddenBrushIssueDoc, ownedOutputIssue, hiddenIssueCenter ) &&
		hiddenBrushIssueDoc.SelectedIds() == QSet<int>{ brushEntA.id };
	ctx.Step( "mapcheck-hidden-brush-owner", hiddenBrushIssueOk,
		QStringLiteral( "hidden child brushes preserve selection; partially visible owners remain selectable" ) );
	bool brushFaceEdit = brushViewOk && brushViewDocument.SingleSelectedSolid() &&
		brushViewDocument.SingleSelectedSolid()->id == ownedA;
	const QString originalBrushMaterial = brushViewDocument.Map().SolidById( ownedA )->sides.first().texture;
	QList<SideLite> editedBrushFaces = brushViewDocument.Map().SolidById( ownedA )->sides;
	editedBrushFaces[0].texture = QStringLiteral( "dev/brush_face_test" );
	editedBrushFaces[0].uaxis[3] += 16;
	brushFaceEdit = brushFaceEdit && brushViewDocument.SetSelectedFaceProperties( editedBrushFaces ) &&
		brushViewDocument.Map().SolidById( ownedA )->sides[0].texture == QLatin1String( "dev/brush_face_test" ) &&
		brushViewDocument.Undo() && brushViewDocument.Map().SolidById( ownedA )->sides[0].texture == originalBrushMaterial &&
		brushViewDocument.Redo() && brushViewDocument.Map().SolidById( ownedA )->sides[0].texture == QLatin1String( "dev/brush_face_test" );
	const QString worldBrushMaterial = brushViewDocument.Map().SolidById( worldBrush )->sides.first().texture;
	brushFaceEdit = brushFaceEdit && brushViewDocument.ReplaceMaterial( QStringLiteral( "dev/brush_face_test" ),
		QStringLiteral( "dev/brush_face_replaced" ), true ) == 1 &&
		brushViewDocument.Map().SolidById( ownedA )->sides[0].texture == QLatin1String( "dev/brush_face_replaced" ) &&
		brushViewDocument.Map().SolidById( worldBrush )->sides[0].texture == worldBrushMaterial &&
		brushViewDocument.Undo() && brushViewDocument.Map().SolidById( ownedA )->sides[0].texture == QLatin1String( "dev/brush_face_test" );
	SolidLite secondOwned = *brushViewDocument.Map().SolidById( ownedA );
	secondOwned.id = brushViewDocument.Map().AllocId();
	brushViewDocument.Map().solids.append( secondOwned );
	brushFaceEdit = brushFaceEdit && !brushViewDocument.SingleSelectedSolid() &&
		!brushViewDocument.SetSelectedFaceProperties( editedBrushFaces );
	ctx.Step( "brush-entity-face-properties", brushFaceEdit,
		QStringLiteral( "single-brush entity face properties, selection-only replacement, undo/redo and multi-brush guard" ) );
	const QList<int> multiFaceIds = brushViewDocument.SelectedVisibleSolidIds();
	QList<SideLite> secondFaces = brushViewDocument.Map().SolidById( secondOwned.id )->sides;
	secondFaces[0].texture = QStringLiteral( "dev/second_brush" );
	bool multiFaceEdit = multiFaceIds.contains( ownedA ) && multiFaceIds.contains( secondOwned.id ) &&
		brushViewDocument.SetSelectedSolidFaceProperties( secondOwned.id, secondFaces ) &&
		brushViewDocument.Map().SolidById( secondOwned.id )->sides[0].texture == QLatin1String( "dev/second_brush" ) &&
		brushViewDocument.Map().SolidById( ownedA )->sides[0].texture == QLatin1String( "dev/brush_face_test" ) &&
		brushViewDocument.Undo() && brushViewDocument.Map().SolidById( secondOwned.id )->sides[0].texture != QLatin1String( "dev/second_brush" );
	ViewFilter hideChild = brushViewDocument.GetViewFilter();
	hideChild.quickHiddenIds.insert( secondOwned.id );
	brushViewDocument.SetViewFilter( hideChild );
	multiFaceEdit = multiFaceEdit && !brushViewDocument.SelectedVisibleSolidIds().contains( secondOwned.id ) &&
		!brushViewDocument.SetSelectedSolidFaceProperties( secondOwned.id, secondFaces ) &&
		!brushViewDocument.SetSelectedSolidFaceProperties( worldBrush, secondFaces );
	ctx.Step( "brush-entity-multiple-face-properties", multiFaceEdit,
		QStringLiteral( "edit one visible child, preserve other brushes, undo and reject hidden/unselected solids" ) );
	// Optional end-to-end fixture: retain an existing sealed world and write
	// two real brush entities to a caller-selected independent VMF path.
	const QString brushCompileInput = qEnvironmentVariable( "HAMMER_SELFTEST_BRUSH_COMPILE_INPUT" );
	const QString decalCompileOutput = qEnvironmentVariable( "HAMMER_SELFTEST_DECAL_COMPILE_OUTPUT" );
	if ( !brushCompileInput.isEmpty() && !decalCompileOutput.isEmpty() )
	{
		CHammerDocument decalFixture;
		QString decalError;
		bool ok = VmfIO::LoadMap( brushCompileInput, decalFixture.Map(), &decalError );
		std::optional<HammerWinding::Point> location;
		double lowestTop = std::numeric_limits<double>::infinity();
		for ( const SolidLite &solid : decalFixture.Map().solids )
		{
			QList<HammerWinding::Polygon> faces;
			QList<HammerWinding::Plane> planes;
			if ( solid.ownerEntityId || !HammerWinding::BuildFaces( solid, faces ) ||
				!HammerWinding::BuildPlanes( solid, planes ) ) continue;
			for ( int i = 0; i < faces.size(); ++i )
			{
				if ( planes[i].normal[2] < 0.999 || solid.sides[i].displacement || faces[i].isEmpty() ) continue;
				HammerWinding::Point center{};
				for ( const auto &vertex : faces[i] )
					for ( int axis = 0; axis < 3; ++axis ) center[axis] += vertex[axis] / faces[i].size();
				if ( center[2] >= lowestTop ) continue;
				lowestTop = center[2];
				location = center;
			}
		}
		ok = ok && location && decalFixture.SetActiveMaterial( QStringLiteral( "decals/shot1" ) ) &&
			decalFixture.PlaceDecal( location->data() ) > 0 && VmfIO::SaveMap( decalCompileOutput, decalFixture.Map(), &decalError );
		ctx.Step( "decal-compile-fixture", ok, decalError );
	}
	const QString overlayCompileOutput = qEnvironmentVariable( "HAMMER_SELFTEST_OVERLAY_COMPILE_OUTPUT" );
	{
		CHammerDocument sample;
		const double lo[3] = { -64, -64, -32 }, hi[3] = { 64, 64, 0 }, click[3] = {};
		const int solidId = CreateBoxSolid( sample.Map(), lo, hi );
		const SolidLite *solid = sample.Map().SolidById( solidId );
		QList<HammerWinding::Plane> planes;
		int faceId = 0;
		if ( solid && HammerWinding::BuildPlanes( *solid, planes ) )
			for ( int i = 0; i < planes.size(); ++i )
				if ( planes[i].normal[2] > 0.999 ) faceId = solid->sides[i].id;
		bool ok = faceId > 0 && sample.SetActiveMaterial( QStringLiteral( "decals/shot1" ) ) &&
			sample.PlaceOverlay( solidId, -1, click ) < 0;
		const int id = sample.PlaceOverlay( solidId, faceId, click, 24, 16 );
		HammerOverlay::Data data;
		const auto parse = [&] {
			const EntityLite *entity = sample.Map().EntityById( id );
			return entity && HammerOverlay::Parse( *entity, data );
		};
		ok = ok && id > 0 && parse() && data.sides == QList<int>{ faceId } &&
			data.material == QLatin1String( "decals/shot1" ) && data.points[0][0] == -12 && data.points[0][1] == -8;
		const double moved[3] = { -20, -10, 0 }, inverted[3] = { 64, 64, 0 };
		ok = ok && sample.MoveOverlayHandle( id, 0, moved ) && parse() && data.points[0][0] == -20 &&
			!sample.MoveOverlayHandle( id, 0, inverted ) && sample.Undo() && parse() && data.points[0][0] == -12 &&
			sample.Redo() && parse() && data.points[0][0] == -20;
		QTemporaryDir overlayTemp;
		QString error;
		const QString path = overlayTemp.filePath( QStringLiteral( "overlay.vmf" ) );
		MapDocLite reloaded;
		ok = ok && overlayTemp.isValid() && VmfIO::SaveMap( path, sample.Map(), &error ) &&
			VmfIO::LoadMap( path, reloaded, &error );
		const EntityLite *reopened = reloaded.EntityById( id );
		ok = ok && reopened && sample.Map().EntityById( id )->keyvalues == reopened->keyvalues;
		ctx.Step( "overlay-document", ok, error );

		const double scale[3] = { 2, 1, 1 };
		ok = ok && !sample.ScaleSelectedBoxes( scale ) && parse() &&
			HammerOverlay::WorldPoint( data, 0 ) == HammerOverlay::Point{ -20, -10, 0 };
		sample.SelectObjects( { solidId, id } );
		ok = ok && sample.ScaleSelectedBoxes( scale ) && parse() &&
			HammerOverlay::WorldPoint( data, 0 ) == HammerOverlay::Point{ -40, -10, 0 } &&
			sample.RotateSelectedBoxes90( false, 2 ) && parse() &&
			HammerOverlay::WorldPoint( data, 0 ) == HammerOverlay::Point{ 10, -40, 0 } &&
			sample.FlipSelectedBoxes( 0 ) && parse() &&
			HammerOverlay::WorldPoint( data, 0 ) == HammerOverlay::Point{ -10, -40, 0 } &&
			data.sides == QList<int>{ faceId } && sample.Undo() && parse() &&
			HammerOverlay::WorldPoint( data, 0 ) == HammerOverlay::Point{ 10, -40, 0 } &&
			sample.Redo() && parse() && HammerOverlay::WorldPoint( data, 0 ) == HammerOverlay::Point{ -10, -40, 0 };
		ctx.Step( "overlay-brush-transform", ok, QStringLiteral( "receiving brush selection, scale, quarter turn, mirror and undo/redo" ) );
	}
	const QString shorelineOutput = qEnvironmentVariable( "HAMMER_SELFTEST_SHORELINE_COMPILE_OUTPUT" );
	{
		CHammerDocument sample;
		const double mins[3] = { -32, -32, -16 }, maxs[3] = { 32, 32, 0 };
		const int brush = sample.CreateBrush( mins, maxs );
		const SolidLite *solid = sample.Map().SolidById( brush );
		bool ok = solid && !solid->sides.isEmpty();
		const QString face = ok ? QString::number( solid->sides.first().id ) : QString();
		EntityLite transition;
		transition.id = sample.Map().AllocId();
		transition.classname = QStringLiteral( "info_overlay_transition" );
		transition.keyvalues.insert( QStringLiteral( "sides2" ), face );
		transition.shorelineOverlays.emplace().append( { { QStringLiteral( "sides" ), face },
			{ QStringLiteral( "material" ), QStringLiteral( "decals/shot1" ) } } );
		sample.Map().entities.append( transition );
		sample.SelectObjects( { brush } );
		const double delta[3] = { 16, 0, 0 };
		ok = ok && !sample.MoveSelection( delta ) && !sample.DeleteSelection();
		sample.SelectObjects( { transition.id } );
		ok = ok && !sample.CopySelection() && !sample.SetSelectedEntityKey( QStringLiteral( "sides2" ), QStringLiteral( "0" ) );
		QTemporaryDir temp;
		QString error;
		MapDocLite reopened;
		const QString path = temp.filePath( QStringLiteral( "shoreline.vmf" ) );
		ok = ok && temp.isValid() && VmfIO::SaveMap( path, sample.Map(), &error ) && VmfIO::LoadMap( path, reopened, &error );
		const EntityLite *loaded = ok ? reopened.EntityById( transition.id ) : nullptr;
		ctx.Step( "shoreline-preservation", loaded && loaded->shorelineOverlays == transition.shorelineOverlays, error );
	}
	if ( !brushCompileInput.isEmpty() && ( !overlayCompileOutput.isEmpty() || !shorelineOutput.isEmpty() ) )
	{
		CHammerDocument fixture;
		QString error;
		bool ok = VmfIO::LoadMap( brushCompileInput, fixture.Map(), &error );
		HammerWinding::Point location{};
		int solidId = 0, faceId = 0;
		double lowestTop = (std::numeric_limits<double>::max)();
		for ( const SolidLite &solid : fixture.Map().solids )
		{
			QList<HammerWinding::Polygon> faces;
			QList<HammerWinding::Plane> planes;
			if ( solid.ownerEntityId || !HammerWinding::BuildFaces( solid, faces ) ||
				!HammerWinding::BuildPlanes( solid, planes ) ) continue;
			for ( int i = 0; i < faces.size(); ++i )
			{
				if ( planes[i].normal[2] < 0.999 || solid.sides[i].displacement || faces[i].isEmpty() ) continue;
				HammerWinding::Point center{};
				for ( const auto &vertex : faces[i] )
					for ( int axis = 0; axis < 3; ++axis ) center[axis] += vertex[axis] / faces[i].size();
				if ( center[2] >= lowestTop ) continue;
				lowestTop = center[2];
				location = center;
				solidId = solid.id;
				faceId = solid.sides[i].id;
			}
		}
		const int id = ok && solidId && fixture.SetActiveMaterial( QStringLiteral( "decals/shot1" ) )
			? fixture.PlaceOverlay( solidId, faceId, location.data(), 24, 24 ) : -1;
		if ( !overlayCompileOutput.isEmpty() )
			ctx.Step( "overlay-compile-fixture", id > 0 && VmfIO::SaveMap( overlayCompileOutput, fixture.Map(), &error ), error );
		if ( !shorelineOutput.isEmpty() )
		{
			EntityLite *transition = fixture.Map().EntityById( id );
			if ( transition )
			{
				transition->classname = QStringLiteral( "info_overlay_transition" );
				auto &overlay = transition->shorelineOverlays.emplace().emplaceBack();
				for ( auto it = transition->keyvalues.cbegin(); it != transition->keyvalues.cend(); ++it )
					if ( it.key().compare( QLatin1String( "RenderOrder" ), Qt::CaseInsensitive ) != 0 )
						overlay.append( { it.key(), it.value() } );
				transition->keyvalues.clear();
				transition->keyvalues.insert( QStringLiteral( "sides" ), QString::number( faceId ) );
			}
			MapDocLite reloaded;
			ok = transition && VmfIO::SaveMap( shorelineOutput, fixture.Map(), &error ) &&
				VmfIO::LoadMap( shorelineOutput, reloaded, &error );
			const EntityLite *reopened = ok ? reloaded.EntityById( id ) : nullptr;
			ctx.Step( "shoreline-compile-fixture", reopened && reopened->shorelineOverlays == transition->shorelineOverlays, error );
		}
	}
	const QString brushCompileOutput = qEnvironmentVariable( "HAMMER_SELFTEST_BRUSH_COMPILE_OUTPUT" );
	const QString displacementCompileOutput = qEnvironmentVariable( "HAMMER_SELFTEST_DISPLACEMENT_COMPILE_OUTPUT" );
	if ( !brushCompileInput.isEmpty() && !displacementCompileOutput.isEmpty() )
	{
		CHammerDocument terrain;
		QString error;
		bool ok = VmfIO::LoadMap( brushCompileInput, terrain.Map(), &error );
		if ( ok )
		{
			const double lo[3] = { 60, 60, -48 }, hi[3] = { 96, 96, -40 };
			const int solidId = CreateBoxSolid( terrain.Map(), lo, hi, QStringLiteral( "concrete/concretewall001a" ) );
			const SolidLite *solid = terrain.Map().SolidById( solidId );
			int topId = 0;
			if ( solid ) for ( const SideLite &side : solid->sides )
				if ( side.planePts[0][2] == hi[2] && side.planePts[1][2] == hi[2] && side.planePts[2][2] == hi[2] )
					topId = side.id;
			QMap<int, QSet<int>> faces;
			faces.insert( solidId, QSet<int>() << topId );
			terrain.SelectObject( solidId );
			QList<double> heights( 25, 0.0 );
			heights[12] = 12.0;
			ok = topId > 0 && terrain.CreateDisplacements( faces, 2 ) &&
				terrain.SetDisplacementDistances( solidId, topId, heights ) &&
				VmfIO::SaveMap( displacementCompileOutput, terrain.Map(), &error );
		}
		ctx.Step( "displacement-compile-fixture", ok, error );
	}
	if ( !brushCompileInput.isEmpty() && !brushCompileOutput.isEmpty() )
	{
		MapDocLite compileMap;
		QString compileError;
		bool compileFixture = VmfIO::LoadMap( brushCompileInput, compileMap, &compileError );
		if ( compileFixture )
		{
			EntityLite detail, door;
			detail.id = compileMap.AllocId(); detail.classname = QStringLiteral( "func_detail" );
			door.id = compileMap.AllocId(); door.classname = QStringLiteral( "func_door" );
			door.keyvalues.insert( QStringLiteral( "targetname" ), QStringLiteral( "hm2_test_door" ) );
			door.keyvalues.insert( QStringLiteral( "speed" ), QStringLiteral( "100" ) );
			compileMap.entities.append( detail ); compileMap.entities.append( door );
			const double detailMin[3] = { -64, -32, -48 }, detailMax[3] = { -32, 32, -16 };
			const double doorMin[3] = { 32, -32, -48 }, doorMax[3] = { 48, 32, 32 };
			const int detailBrush = CreateBoxSolid( compileMap, detailMin, detailMax );
			const int doorBrush = CreateBoxSolid( compileMap, doorMin, doorMax );
			compileFixture = detailBrush > 0 && doorBrush > 0;
			if ( compileFixture )
			{
				compileMap.SolidById( detailBrush )->ownerEntityId = detail.id;
				compileMap.SolidById( doorBrush )->ownerEntityId = door.id;
				compileFixture = VmfIO::SaveMap( brushCompileOutput, compileMap, &compileError );
			}
		}
		ctx.Step( "brush-entity-compile-fixture", compileFixture, compileError );
		const QString planeCompileOutput = qEnvironmentVariable( "HAMMER_SELFTEST_PLANE_COMPILE_OUTPUT" );
		if ( !planeCompileOutput.isEmpty() )
		{
			CHammerDocument clipFixture;
			clipFixture.Map() = compileMap;
			int detailId = 0;
			for ( const EntityLite &entity : clipFixture.Map().entities ) if ( entity.classname == QLatin1String( "func_detail" ) ) { detailId = entity.id; break; }
			clipFixture.SelectObjects( QSet<int>() << detailId );
			const double points[3][3] = { {-48,0,-48}, {-80,32,-48}, {-48,0,-16} };
			const bool planeFixture = compileFixture && detailId > 0 && clipFixture.ClipSelectedByPlane( points, 0 ) &&
				VmfIO::SaveMap( planeCompileOutput, clipFixture.Map(), &compileError );
			ctx.Step( "clip-three-point-compile-fixture", planeFixture, compileError );
		}
		const QString vertexCompileOutput = qEnvironmentVariable( "HAMMER_SELFTEST_VERTEX_COMPILE_OUTPUT" );
		if ( !vertexCompileOutput.isEmpty() )
		{
			CHammerDocument vertexFixture;
			vertexFixture.Map() = compileMap;
			int detailId = 0;
			for ( const EntityLite &entity : vertexFixture.Map().entities ) if ( entity.classname == QLatin1String( "func_detail" ) ) { detailId = entity.id; break; }
			vertexFixture.SelectObjects( QSet<int>() << detailId );
			const bool vertexFixtureOk = compileFixture && detailId > 0 &&
				vertexFixture.MoveSelectedProjectedVertices( 0, 1, -32, 32, -16, 32 ) &&
				VmfIO::SaveMap( vertexCompileOutput, vertexFixture.Map(), &compileError );
			ctx.Step( "brush-entity-vertex-compile-fixture", vertexFixtureOk, compileError );
		}
	}

	// Entity output names can repeat in VMF. Preserve output order and values
	// through load/save so multiple targets, delays and fire counts survive.
	const char *connectionsFixture =
		"world { \"id\" \"1\" \"classname\" \"worldspawn\" } "
		"entity { \"id\" \"2\" \"classname\" \"logic_relay\" connections { "
		"\"OnTrigger\" \"door_a,Open,,0,-1\" "
		"\"OnTrigger\" \"door_b,Close,parameter,0.25,1,2\" "
		"} }";
	const QString connectionsPath = guardDir.filePath( "connections.vmf" );
	QFile connectionsFile( connectionsPath );
	bool bConnections = guardDir.isValid() && connectionsFile.open( QIODevice::WriteOnly ) &&
		connectionsFile.write( connectionsFixture ) == qint64( qstrlen( connectionsFixture ) );
	connectionsFile.close();
	MapDocLite connectionsLoaded;
	QString connectionsError;
	bConnections = bConnections && VmfIO::LoadMap( connectionsPath, connectionsLoaded, &connectionsError ) &&
		connectionsLoaded.entities.size() == 1 && connectionsLoaded.entities.first().connections.size() == 2 &&
		connectionsLoaded.entities.first().connections[0].first == QLatin1String( "OnTrigger" ) &&
		connectionsLoaded.entities.first().connections[1].first == QLatin1String( "OnTrigger" ) &&
		connectionsLoaded.entities.first().connections[0].second == QLatin1String( "door_a,Open,,0,-1" ) &&
		connectionsLoaded.entities.first().connections[1].second == QLatin1String( "door_b,Close,parameter,0.25,1,2" );
	const QString connectionsSavedPath = guardDir.filePath( "connections_saved.vmf" );
	MapDocLite connectionsRoundTrip;
	bConnections = bConnections && VmfIO::SaveMap( connectionsSavedPath, connectionsLoaded, &connectionsError ) &&
		VmfIO::LoadMap( connectionsSavedPath, connectionsRoundTrip, &connectionsError ) &&
		connectionsRoundTrip.entities.size() == 1 && connectionsRoundTrip.entities.first().connections == connectionsLoaded.entities.first().connections;
	ctx.Step( "entity-connections-roundtrip", bConnections, connectionsError );

	// Hammer stores a list of camera position/look pairs and an active index.
	// Preserve even duplicate or extension keys, while rejecting nested blocks.
	const char *cameraFixture =
		"world { \"id\" \"1\" \"classname\" \"worldspawn\" } "
		"cameras { \"activecamera\" \"1\" \"extra\" \"test_value\" "
		"camera { \"position\" \"[128 64 32]\" \"look\" \"[0 0 0]\" } "
		"camera { \"position\" \"[-1 2 3]\" \"look\" \"[4 5 6]\" \"extra\" \"a\" \"extra\" \"b\" } }";
	const QString cameraPath = guardDir.filePath( "camera.vmf" );
	QFile cameraFile( cameraPath );
	bool bCamera = cameraFile.open( QIODevice::WriteOnly ) &&
		cameraFile.write( cameraFixture ) == qint64( qstrlen( cameraFixture ) );
	cameraFile.close();
	MapDocLite cameraLoaded, cameraRoundTrip;
	QString cameraError;
	bCamera = bCamera && VmfIO::LoadMap( cameraPath, cameraLoaded, &cameraError ) &&
		cameraLoaded.cameraProperties.size() == 2 && cameraLoaded.cameras.size() == 2 &&
		cameraLoaded.cameraProperties[0].second == QLatin1String( "1" ) &&
		cameraLoaded.cameras[1].keyvalues.size() == 4;
	const QString cameraSavedPath = guardDir.filePath( "camera_saved.vmf" );
	bCamera = bCamera && VmfIO::SaveMap( cameraSavedPath, cameraLoaded, &cameraError ) &&
		VmfIO::LoadMap( cameraSavedPath, cameraRoundTrip, &cameraError ) &&
		cameraRoundTrip.cameraProperties == cameraLoaded.cameraProperties && cameraRoundTrip.cameras.size() == 2 &&
		cameraRoundTrip.cameras[0].keyvalues == cameraLoaded.cameras[0].keyvalues &&
		cameraRoundTrip.cameras[1].keyvalues == cameraLoaded.cameras[1].keyvalues;
	CHammerDocument cameraDocument;
	const double cameraEntityOrigin[3] = { 0.0, 0.0, 16.0 };
	bCamera = bCamera && cameraDocument.LoadFromFile( cameraPath ) &&
		cameraDocument.PlaceEntity( "info_player_start", cameraEntityOrigin ) >= 0 &&
		cameraDocument.Undo() && cameraDocument.Redo() &&
		cameraDocument.Map().cameraProperties == cameraLoaded.cameraProperties &&
		cameraDocument.Map().cameras.size() == 2 &&
		cameraDocument.Map().cameras[1].keyvalues == cameraLoaded.cameras[1].keyvalues;
	ctx.Step( "camera-vmf-roundtrip", bCamera, cameraError );
	// VMF look-at points translate to the Source angles used by the 3D view.
	// Adding/removing cameras is an undoable document edit that retains extra keys.
	const double savedEye[3] = { 12, 30, 45 }, savedForward[3] = { 0, 1, 0 };
	CameraLite saved = MakeSavedCamera( savedEye, savedForward );
	double parsedEye[3], parsedAngles[3];
	bool savedCameraOk = ReadSavedCamera( saved, parsedEye, parsedAngles ) &&
		parsedEye[0] == 12 && parsedEye[1] == 30 && parsedEye[2] == 45 &&
		std::abs( parsedAngles[0] ) < 1e-8 && std::abs( parsedAngles[1] - 90 ) < 1e-8;
	CameraLite broken = saved;
	broken.keyvalues[1].second = broken.keyvalues[0].second;
	savedCameraOk = savedCameraOk && !ReadSavedCamera( broken, parsedEye, parsedAngles );
	CHammerDocument cameraEditor;
	savedCameraOk = savedCameraOk && cameraEditor.LoadFromFile( cameraPath ) && SavedCameraActiveIndex( cameraEditor.Map() ) == 1;
	QList<CameraLite> cameras = cameraEditor.Map().cameras;
	cameras.append( saved );
	savedCameraOk = savedCameraOk && cameraEditor.SetSavedCameras( cameras, 2 ) && cameraEditor.IsDirty() &&
		SavedCameraActiveIndex( cameraEditor.Map() ) == 2 && cameraEditor.Map().cameraProperties[1].second == QLatin1String( "test_value" ) &&
		cameraEditor.Map().cameras[1].keyvalues == cameraLoaded.cameras[1].keyvalues &&
		cameraEditor.Undo() && cameraEditor.Map().cameras.size() == 2 && !cameraEditor.IsDirty() &&
		SavedCameraActiveIndex( cameraEditor.Map() ) == 1 && cameraEditor.Redo() && cameraEditor.Map().cameras.size() == 3;
	const QString editedCamerasPath = guardDir.filePath( "camera_edited.vmf" );
	MapDocLite editedCameras;
	savedCameraOk = savedCameraOk && cameraEditor.SaveToFile( editedCamerasPath ) &&
		VmfIO::LoadMap( editedCamerasPath, editedCameras, &cameraError ) && editedCameras.cameras.size() == 3 &&
		SavedCameraActiveIndex( editedCameras ) == 2 && editedCameras.cameras[1].keyvalues == cameraLoaded.cameras[1].keyvalues;
	ctx.Step( "saved-camera-navigation", savedCameraOk, QStringLiteral( "position/look, invalid target, metadata, undo and VMF persistence" ) );
	{
	CMainWindow cameraWindow( nullptr, false );
	QAction *cameraAction = cameraWindow.findChild<QAction *>( QStringLiteral( "savedCamerasAction" ) );
	CHammerDocument *cameraUiDoc = cameraWindow.findChild<CHammerDocument *>();
	bool cameraUiOk = cameraAction && cameraUiDoc;
	if ( cameraUiOk )
	{
		QTimer::singleShot( 0, [&] {
			QDialog *dialog = qobject_cast<QDialog *>( QApplication::activeModalWidget() );
			QListWidget *list = dialog ? dialog->findChild<QListWidget *>( QStringLiteral( "savedCameraList" ) ) : nullptr;
			QPushButton *add = dialog ? dialog->findChild<QPushButton *>( QStringLiteral( "savedCameraAdd" ) ) : nullptr;
			QPushButton *go = dialog ? dialog->findChild<QPushButton *>( QStringLiteral( "savedCameraGo" ) ) : nullptr;
			QPushButton *remove = dialog ? dialog->findChild<QPushButton *>( QStringLiteral( "savedCameraDelete" ) ) : nullptr;
			cameraUiOk = dialog && list && add && go && remove && list->count() == 0 && !go->isEnabled();
			if ( cameraUiOk )
			{
				add->click();
				cameraUiOk = list->count() == 1 && cameraUiDoc->Map().cameras.size() == 1 && go->isEnabled();
				go->click();
				remove->click();
				cameraUiOk = cameraUiOk && list->count() == 0 && cameraUiDoc->Map().cameras.isEmpty();
			}
			if ( dialog ) dialog->reject();
		} );
		cameraAction->trigger();
		cameraUiOk = cameraUiOk && cameraUiDoc->Undo() && cameraUiDoc->Map().cameras.size() == 1;
	}
	ctx.Step( "saved-camera-dialog", cameraUiOk, QStringLiteral( "menu, save, go to, delete and undo" ) );
	}

	// Original Hammer serializes cordon bounds and activation in a single block.
	// Preserve unknown and repeated keys too, and never silently lose the data.
	const char *cordonFixture =
		"world { \"id\" \"1\" \"classname\" \"worldspawn\" } "
		"cordon { \"mins\" \"(-128 -64 -32)\" \"maxs\" \"(128 64 32)\" "
		"\"active\" \"1\" \"extra\" \"a\" \"extra\" \"b\" }";
	const QString cordonPath = guardDir.filePath( "cordon.vmf" );
	QFile cordonFile( cordonPath );
	bool bCordon = cordonFile.open( QIODevice::WriteOnly ) &&
		cordonFile.write( cordonFixture ) == qint64( qstrlen( cordonFixture ) );
	cordonFile.close();
	MapDocLite cordonLoaded, cordonRoundTrip;
	QString cordonError;
	bCordon = bCordon && VmfIO::LoadMap( cordonPath, cordonLoaded, &cordonError ) &&
		cordonLoaded.IsCordonActive() && cordonLoaded.cordonProperties.size() == 5;
	const QString cordonSavedPath = guardDir.filePath( "cordon_saved.vmf" );
	bCordon = bCordon && VmfIO::SaveMap( cordonSavedPath, cordonLoaded, &cordonError ) &&
		VmfIO::LoadMap( cordonSavedPath, cordonRoundTrip, &cordonError ) &&
		cordonRoundTrip.cordonProperties == cordonLoaded.cordonProperties && cordonRoundTrip.IsCordonActive();
	CHammerDocument cordonDocument;
	const double cordonEntityOrigin[3] = { 0.0, 0.0, 16.0 };
	bCordon = bCordon && cordonDocument.LoadFromFile( cordonPath ) &&
		cordonDocument.PlaceEntity( "info_player_start", cordonEntityOrigin ) >= 0 &&
		cordonDocument.Undo() && cordonDocument.Redo() &&
		cordonDocument.Map().cordonProperties == cordonLoaded.cordonProperties;
	cordonRoundTrip.cordonProperties.append( qMakePair( QStringLiteral( "ACTIVE" ), QStringLiteral( "0" ) ) );
	bCordon = bCordon && !cordonRoundTrip.IsCordonActive();
	ctx.Step( "cordon-vmf-roundtrip", bCordon, cordonError );

	HammerCordon::Bounds cordonBounds;
	const double insideMin[3] = { -64, -16, -32 }, insideMax[3] = { 64, 16, -24 };
	const double outsideMin[3] = { 500, 500, 500 }, outsideMax[3] = { 516, 516, 516 };
	MapDocLite cordonCompile;
	const int insideSolid = CreateBoxSolid( cordonLoaded, insideMin, insideMax );
	const int outsideSolid = CreateBoxSolid( cordonLoaded, outsideMin, outsideMax );
	EntityLite cordonSpawn;
	cordonSpawn.id = cordonLoaded.AllocId();
	cordonSpawn.classname = QStringLiteral( "info_player_start" );
	for ( int axis = 0; axis < 3; ++axis ) cordonSpawn.origin[axis] = cordonEntityOrigin[axis];
	cordonLoaded.entities.append( cordonSpawn );
	EntityLite outsideEntity = cordonSpawn;
	outsideEntity.id = cordonLoaded.AllocId();
	outsideEntity.origin[0] = 500;
	cordonLoaded.entities.append( outsideEntity );
	bool bCordonCompile = insideSolid > 0 && outsideSolid > 0 &&
		HammerCordon::ReadBounds( cordonLoaded, cordonBounds ) &&
		cordonDocument.SetCordon( cordonBounds.mins, cordonBounds.maxs, false ) &&
		!cordonDocument.Map().IsCordonActive() && cordonDocument.Undo() &&
		cordonDocument.Map().cordonProperties == cordonLoaded.cordonProperties &&
		HammerCordon::BuildCompileMap( cordonLoaded, cordonCompile ) &&
		cordonCompile.SolidById( insideSolid ) && !cordonCompile.SolidById( outsideSolid ) &&
		cordonCompile.solids.size() == 7 && cordonCompile.entities.size() == 1 &&
		cordonCompile.entities.first().id == cordonSpawn.id && !cordonCompile.IsCordonActive() &&
		cordonLoaded.IsCordonActive() && cordonLoaded.solids.size() == 2;
	const QString cordonCompileOutput = qEnvironmentVariable( "HAMMER_SELFTEST_CORDON_COMPILE_OUTPUT" );
	const QString cordonCompilePath = cordonCompileOutput.isEmpty() ? guardDir.filePath( "cordon_compile.vmf" ) : cordonCompileOutput;
	bCordonCompile = bCordonCompile && VmfIO::SaveMap( cordonCompilePath, cordonCompile, &cordonError );
	ctx.Step( "cordon-compile-map", bCordonCompile, cordonError );

	// Keep prefab, version and extension keys, including duplicates and quoting.
	const char *versionFixture =
		"versioninfo { \"editorversion\" \"400\" \"mapversion\" \"73\" "
		"\"prefab\" \"1\" \"extra\" \"one\" \"extra\" \"two_quoted\" } "
		"world { \"id\" \"1\" \"classname\" \"worldspawn\" \"mapversion\" \"73\" }";
	const QString versionPath = guardDir.filePath( "version.vmf" );
	QFile versionFile( versionPath );
	bool bVersion = versionFile.open( QIODevice::WriteOnly ) &&
		versionFile.write( versionFixture ) == qint64( qstrlen( versionFixture ) );
	versionFile.close();
	MapDocLite versionLoaded, versionRoundTrip;
	QString versionError;
	bVersion = bVersion && VmfIO::LoadMap( versionPath, versionLoaded, &versionError ) &&
		versionLoaded.hasVersionInfo && versionLoaded.versionProperties.size() == 5;
	const QString versionSavedPath = guardDir.filePath( "version_saved.vmf" );
	bVersion = bVersion && VmfIO::SaveMap( versionSavedPath, versionLoaded, &versionError ) &&
		VmfIO::LoadMap( versionSavedPath, versionRoundTrip, &versionError ) &&
		versionRoundTrip.hasVersionInfo && versionRoundTrip.versionProperties == versionLoaded.versionProperties;
	CHammerDocument versionDocument;
	bVersion = bVersion && versionDocument.LoadFromFile( versionPath ) &&
		versionDocument.PlaceEntity( "info_player_start", cordonEntityOrigin ) >= 0 &&
		versionDocument.Undo() && versionDocument.Redo() &&
		versionDocument.Map().versionProperties == versionLoaded.versionProperties;
	ctx.Step( "versioninfo-vmf-roundtrip", bVersion, versionError );

	// Hammer++ metadata must survive without treating cached vertices as immutable
	// when a brush is translated or copied.
	const QByteArray plusFixture =
		"viewsettings { \"nGridSpacing\" \"64\" \"extra\" \"a\" \"extra\" \"b\" } "
		"palette_plus { \"color0\" \"255 255 255\" } bgimages_plus { } "
		"cordons { \"active\" \"1\" } world { \"id\" \"1\" \"classname\" \"worldspawn\" "
		"solid { \"id\" \"2\" side { \"id\" \"3\" \"plane\" \"(0 0 0) (1 0 0) (0 1 0)\" "
		"vertices_plus { \"v\" \"0 0 0\" \"v\" \"1 0 0\" \"v\" \"0 1 0\" } } } }";
	const QString plusPath = guardDir.filePath( "plus.vmf" );
	QFile plusFile( plusPath );
	bool bPlus = plusFile.open( QIODevice::WriteOnly ) && plusFile.write( plusFixture ) == plusFixture.size();
	plusFile.close();
	MapDocLite plusLoaded, plusReloaded;
	QString plusError;
	const QString plusSaved = guardDir.filePath( "plus_saved.vmf" );
	bPlus = bPlus && VmfIO::LoadMap( plusPath, plusLoaded, &plusError ) && plusLoaded.extraMetadata.size() == 4 &&
		plusLoaded.IsCordonActive() && plusLoaded.solids.size() == 1 &&
		plusLoaded.solids[0].sides[0].verticesPlus.size() == 3 &&
		VmfIO::SaveMap( plusSaved, plusLoaded, &plusError ) && VmfIO::LoadMap( plusSaved, plusReloaded, &plusError ) &&
		plusReloaded.extraMetadata.size() == 4 && plusReloaded.extraMetadata[0].properties == plusLoaded.extraMetadata[0].properties &&
		plusReloaded.solids[0].sides[0].verticesPlus == plusLoaded.solids[0].sides[0].verticesPlus;
	CHammerDocument plusDoc;
	const double plusDelta[3] = { 4, -2, 9 };
	if ( bPlus )
	{
		bPlus = plusDoc.LoadFromFile( plusPath );
		plusDoc.SelectObject( 2 );
		bPlus = bPlus && plusDoc.MoveSelection( plusDelta ) &&
			plusDoc.Map().solids[0].sides[0].verticesPlus[0] == QLatin1String( "4 -2 9" ) &&
			plusDoc.Undo() && plusDoc.Map().solids[0].sides[0].verticesPlus[0] == QLatin1String( "0 0 0" ) &&
			plusDoc.Redo() && plusDoc.Map().solids[0].sides[0].verticesPlus[0] == QLatin1String( "4 -2 9" );
		bPlus = bPlus && plusDoc.CopySelection();
		const int copiedId = plusDoc.PasteSpecial( 1, plusDelta );
		const SolidLite *copied = plusDoc.Map().SolidById( copiedId );
		bPlus = bPlus && copied && copied->sides[0].verticesPlus[0] == QLatin1String( "8 -4 18" ) &&
			plusDoc.Undo() && !plusDoc.Map().SolidById( copiedId ) && plusDoc.Redo() &&
			plusDoc.Map().SolidById( copiedId ) && plusDoc.Map().SolidById( copiedId )->sides[0].verticesPlus[0] == QLatin1String( "8 -4 18" );
	}
	ctx.Step( "hammer-plus-metadata-roundtrip", bPlus, plusError );
	// Optional real-world input, explicitly supplied by the caller; the source is
	// opened read-only and all output is written beneath the temporary directory.
	const QString externalVmf = qEnvironmentVariable( "HAMMER_SELFTEST_INPUT_VMF" );
	if ( !externalVmf.isEmpty() )
	{
		MapDocLite source, restored;
		QString error;
		const QString output = guardDir.filePath( "external_roundtrip.vmf" );
		bool ok = VmfIO::LoadMap( externalVmf, source, &error ) &&
			VmfIO::SaveMap( output, source, &error ) && VmfIO::LoadMap( output, restored, &error ) &&
			source.solids.size() == restored.solids.size() && source.entities.size() == restored.entities.size() &&
			source.extraMetadata.size() == restored.extraMetadata.size();
		for ( int i = 0; ok && i < source.extraMetadata.size(); ++i )
			ok = source.extraMetadata[i].name == restored.extraMetadata[i].name &&
				source.extraMetadata[i].properties == restored.extraMetadata[i].properties;
		for ( int i = 0; ok && i < source.solids.size(); ++i )
		{
			ok = source.solids[i].sides.size() == restored.solids[i].sides.size();
			for ( int j = 0; ok && j < source.solids[i].sides.size(); ++j )
				ok = source.solids[i].sides[j].hasVerticesPlus == restored.solids[i].sides[j].hasVerticesPlus &&
					source.solids[i].sides[j].verticesPlus == restored.solids[i].sides[j].verticesPlus;
		}
		ctx.Step( "external-vmf-roundtrip", ok, error );
	}

	// Preserve visual editor metadata for supported point entities and brushes.
	const char *editorFixture =
		"world { \"id\" \"1\" \"classname\" \"worldspawn\" solid { \"id\" \"2\" "
		"side { \"id\" \"3\" \"plane\" \"(0 0 0) (1 0 0) (0 1 0)\" } "
		"editor { \"color\" \"20 40 60\" \"logicalpos\" \"[10 15]\" } } } "
		"entity { \"id\" \"4\" \"classname\" \"info_target\" editor { \"color\" \"100 120 140\" "
		"\"logicalpos\" \"[-20 30]\" \"visgroupshown\" \"1\" } }";
	const QString editorPath = guardDir.filePath( "editor.vmf" );
	QFile editorFile( editorPath );
	bool bEditor = editorFile.open( QIODevice::WriteOnly ) &&
		editorFile.write( editorFixture ) == qint64( qstrlen( editorFixture ) );
	editorFile.close();
	MapDocLite editorLoaded, editorReloaded;
	QString editorError;
	const QString editorSavedPath = guardDir.filePath( "editor_saved.vmf" );
	bEditor = bEditor && VmfIO::LoadMap( editorPath, editorLoaded, &editorError ) &&
		editorLoaded.solids.size() == 1 && editorLoaded.entities.size() == 1 &&
		editorLoaded.solids[0].hasEditor && editorLoaded.solids[0].editorProperties.size() == 2 &&
		editorLoaded.entities[0].hasEditor && editorLoaded.entities[0].editorProperties.size() == 3 &&
		VmfIO::SaveMap( editorSavedPath, editorLoaded, &editorError ) &&
		VmfIO::LoadMap( editorSavedPath, editorReloaded, &editorError ) &&
		editorReloaded.solids.size() == 1 && editorReloaded.entities.size() == 1 &&
		editorReloaded.solids[0].editorProperties == editorLoaded.solids[0].editorProperties &&
		editorReloaded.entities[0].editorProperties == editorLoaded.entities[0].editorProperties;
	ctx.Step( "editor-metadata-vmf-roundtrip", bEditor, editorError );

	// Original Hammer wraps hidden objects and stores visgroup definitions as a
	// separate nested tree. Keep unknown memberships and repeated membership keys.
	const char *visFixture =
		"visgroups { visgroup { \"name\" \"Outer\" \"visgroupid\" \"7\" \"color\" \"1 2 3\" "
		"visgroup { \"name\" \"Inner\" \"visgroupid\" \"8\" \"color\" \"4 5 6\" } } } "
		"world { \"id\" \"1\" \"classname\" \"worldspawn\" hidden { solid { \"id\" \"2\" "
		"side { \"id\" \"3\" \"plane\" \"(0 0 0) (1 0 0) (0 1 0)\" } "
		"editor { \"visgroupid\" \"7\" \"visgroupid\" \"999\" \"visgroupshown\" \"0\" } } } } "
		"hidden { entity { \"id\" \"4\" \"classname\" \"info_target\" \"origin\" \"0 0 0\" "
		"editor { \"visgroupid\" \"8\" \"visgroupautoshown\" \"0\" } } } "
		"entity { \"id\" \"5\" \"classname\" \"func_detail\" hidden { solid { \"id\" \"6\" "
		"side { \"id\" \"9\" \"plane\" \"(0 0 0) (1 0 0) (0 1 0)\" } "
		"editor { \"visgroupid\" \"8\" } } } }";
	const QString visPath = guardDir.filePath( "visgroup_fixture.vmf" ), visSaved = guardDir.filePath( "visgroup_saved.vmf" );
	QFile visFile( visPath );
	bool bVis = visFile.open( QIODevice::WriteOnly ) && visFile.write( visFixture ) == qint64( qstrlen( visFixture ) );
	visFile.close();
	MapDocLite visLoaded, visReloaded; QString visError;
	bVis = bVis && VmfIO::LoadMap( visPath, visLoaded, &visError ) && visLoaded.visgroups.size() == 1 &&
		visLoaded.visgroups[0].children.size() == 1 && visLoaded.solids.size() == 2 && visLoaded.entities.size() == 2 &&
		visLoaded.solids[0].hiddenWrapper && visLoaded.entities[0].hiddenWrapper && visLoaded.solids[1].hiddenWrapper &&
		visLoaded.solids[1].ownerEntityId == 5 && visLoaded.solids[0].editorProperties.size() == 3;
	if ( bVis )
	{
		ViewFilter savedFilter;
		bVis = !savedFilter.SolidVisible( visLoaded.solids[0] ) && !savedFilter.EntityVisible( visLoaded.entities[0] ) &&
			!savedFilter.SolidVisible( visLoaded.solids[1] );
		savedFilter.showVisgroupHidden = true;
		bVis = bVis && savedFilter.SolidVisible( visLoaded.solids[0] ) && savedFilter.EntityVisible( visLoaded.entities[0] );
	}
	bVis = bVis && VmfIO::SaveMap( visSaved, visLoaded, &visError ) && VmfIO::LoadMap( visSaved, visReloaded, &visError ) &&
		visReloaded.visgroups.size() == 1 && visReloaded.visgroups[0].properties == visLoaded.visgroups[0].properties &&
		visReloaded.visgroups[0].children.size() == 1 && visReloaded.visgroups[0].children[0].properties == visLoaded.visgroups[0].children[0].properties &&
		visReloaded.solids.size() == 2 && visReloaded.solids[0].editorProperties == visLoaded.solids[0].editorProperties &&
		visReloaded.solids[0].hiddenWrapper && visReloaded.solids[1].hiddenWrapper && visReloaded.solids[1].ownerEntityId == 5 &&
		visReloaded.entities.size() == 2 && visReloaded.entities[0].hiddenWrapper;
	ctx.Step( "visgroup-hidden-vmf-roundtrip", bVis, visError );

	// Material selection must affect new brushes, survive a VMF round trip,
	// and remain independently undoable when applied to an existing brush.
	CHammerDocument materialDoc;
	materialDoc.NewDocument();
	const bool bMaterialName = materialDoc.SetActiveMaterial( "materials/brick/brickwall055a.vmt" ) &&
		materialDoc.ActiveMaterial() == QLatin1String( "brick/brickwall055a" ) &&
		!materialDoc.SetActiveMaterial( "bad\"name" );
	const int materialSolidId = materialDoc.CreateBrush( mins1, maxs1 );
	bool bMaterialCreate = materialSolidId >= 0 && materialDoc.Map().solids.size() == 1;
	if ( bMaterialCreate )
	{
		for ( const SideLite &side : materialDoc.Map().solids.first().sides )
			bMaterialCreate = bMaterialCreate && side.texture == QLatin1String( "brick/brickwall055a" );
	}
	ctx.Step( "material-create", bMaterialName && bMaterialCreate );
	bool bMaterialApply = materialDoc.SetActiveMaterial( "concrete/concretewall074a" ) &&
		materialDoc.ApplyMaterialToSelection();
	if ( bMaterialApply )
	{
		for ( const SideLite &side : materialDoc.Map().solids.first().sides )
			bMaterialApply = bMaterialApply && side.texture == QLatin1String( "concrete/concretewall074a" );
	}
	const QString materialVmf = guardDir.filePath( "material.vmf" );
	MapDocLite materialLoaded;
	QString materialError;
	bMaterialApply = bMaterialApply && VmfIO::SaveMap( materialVmf, materialDoc.Map(), &materialError ) &&
		VmfIO::LoadMap( materialVmf, materialLoaded, &materialError ) && !materialLoaded.solids.isEmpty() &&
		materialLoaded.solids.first().sides.first().texture == QLatin1String( "concrete/concretewall074a" );
	ctx.Step( "material-apply-save", bMaterialApply, materialError );
	const bool bMaterialUndo = bMaterialApply && materialDoc.Undo() && !materialDoc.Map().solids.isEmpty() &&
		materialDoc.Map().solids.first().sides.first().texture == QLatin1String( "brick/brickwall055a" ) &&
		materialDoc.Redo() && !materialDoc.Map().solids.isEmpty() &&
		materialDoc.Map().solids.first().sides.first().texture == QLatin1String( "concrete/concretewall074a" );
	ctx.Step( "material-undo-redo", bMaterialUndo );

	// Exercise real document edits, including grouped mouse movement, saved
	// revision tracking and invalidating the redo branch after a new edit.
	CHammerDocument historyDoc;
	historyDoc.NewDocument();
	const double playerOrigin[3] = { 0.0, 0.0, 16.0 };
	const int playerId = historyDoc.PlaceEntity( "info_player_start", playerOrigin );
	bool bHistory = playerId >= 0 && historyDoc.IsDirty() && historyDoc.Undo() &&
		historyDoc.Map().entities.isEmpty() && !historyDoc.IsDirty() && historyDoc.Redo() &&
		historyDoc.Map().EntityById( playerId ) != nullptr;
	const double delta[3] = { 16.0, 0.0, 0.0 };
	historyDoc.BeginEdit();
	historyDoc.MoveSelection( delta );
	historyDoc.MoveSelection( delta );
	historyDoc.EndEdit();
	bHistory = bHistory && historyDoc.Map().EntityById( playerId )->origin[0] == 32.0 &&
		historyDoc.Undo() && historyDoc.Map().EntityById( playerId )->origin[0] == 0.0 &&
		historyDoc.Redo() && historyDoc.Map().EntityById( playerId )->origin[0] == 32.0;
	bHistory = bHistory && historyDoc.SetSelectedEntityKey( "targetname", "test" ) &&
		historyDoc.Undo() && !historyDoc.Map().EntityById( playerId )->keyvalues.contains( "targetname" ) &&
		historyDoc.Redo() && historyDoc.Map().EntityById( playerId )->keyvalues.value( "targetname" ) == "test";
	bHistory = bHistory && historyDoc.SetSelectedEntityClass( "light" ) && historyDoc.Undo() &&
		historyDoc.Map().EntityById( playerId )->classname == "info_player_start" && historyDoc.Redo();
	const QString historyVmf = guardDir.filePath( "history.vmf" );
	bHistory = bHistory && historyDoc.SaveToFile( historyVmf ) && !historyDoc.IsDirty() &&
		historyDoc.DeleteSelection() && historyDoc.IsDirty() && historyDoc.Undo() &&
		!historyDoc.IsDirty() && historyDoc.Redo() && historyDoc.IsDirty() && historyDoc.Undo();
	bHistory = bHistory && historyDoc.SetSelectedEntityKey( "targetname", "new branch" ) && !historyDoc.Redo();
	const int historyBrushId = historyDoc.CreateBrush( mins1, maxs1 );
	bHistory = bHistory && historyBrushId >= 0 && historyDoc.Undo() &&
		historyDoc.Map().SolidById( historyBrushId ) == nullptr && historyDoc.Redo() &&
		historyDoc.Map().SolidById( historyBrushId ) != nullptr;
	ctx.Step( "edit-undo-redo", bHistory, QStringLiteral( "create, move, properties, delete, save and branch" ) );

	CHammerDocument brushHistory;
	brushHistory.NewDocument();
	const int brushId = brushHistory.CreateBrush( mins1, maxs1 );
	bool bBrushHistory = brushId >= 0 && brushHistory.MoveSelection( delta );
	const SolidLite *brush = brushHistory.Map().SolidById( brushId );
	bBrushHistory = bBrushHistory && brush && brush->mins[0] == mins1[0] + delta[0] &&
		brushHistory.Undo();
	brush = brushHistory.Map().SolidById( brushId );
	bBrushHistory = bBrushHistory && brush && brush->mins[0] == mins1[0] &&
		brushHistory.Redo() && brushHistory.DeleteSelection() &&
		brushHistory.Map().SolidById( brushId ) == nullptr && brushHistory.Undo();
	brush = brushHistory.Map().SolidById( brushId );
	bBrushHistory = bBrushHistory && brush && brush->mins[0] == mins1[0] + delta[0];
	ctx.Step( "brush-undo-redo", bBrushHistory, QStringLiteral( "move and delete preserve brush" ) );

	// A rejected open must leave the current map and its undo history intact.
	const QString rejectedPath = guardDir.filePath( "guard0.vmf" );
	const bool bLoadKeepsHistory = bGuardOk && !brushHistory.LoadFromFile( rejectedPath ) &&
		brushHistory.Map().SolidById( brushId ) != nullptr && brushHistory.Redo() &&
		brushHistory.Map().SolidById( brushId ) == nullptr;
	ctx.Step( "failed-load-history", bLoadKeepsHistory );

	CHammerDocument clipboardDoc;
	clipboardDoc.NewDocument();
	const int sourceEntityId = clipboardDoc.PlaceEntity( "logic_relay", playerOrigin );
	EntityLite *sourceEntity = clipboardDoc.Map().EntityById( sourceEntityId );
	if ( sourceEntity )
	{
		sourceEntity->keyvalues.insert( "targetname", "relay_a" );
		sourceEntity->connections.append( qMakePair( QStringLiteral( "OnTrigger" ),
			QStringLiteral( "relay_a,Trigger,,0,-1" ) ) );
	}
	const bool copiedEntity = sourceEntity && clipboardDoc.CopySelection();
	const int pastedEntityId = copiedEntity ? clipboardDoc.PasteSelection() : -1;
	const EntityLite *pastedEntity = clipboardDoc.Map().EntityById( pastedEntityId );
	bool bEntityClipboard = pastedEntityId >= 0 && pastedEntityId != sourceEntityId && pastedEntity &&
		pastedEntity->origin[0] == playerOrigin[0] + 32.0 &&
		pastedEntity->keyvalues.value( "targetname" ) == QLatin1String( "relay_a" ) &&
		pastedEntity->connections.size() == 1 &&
		pastedEntity->connections.first().second == QLatin1String( "relay_a,Trigger,,0,-1" );
	bEntityClipboard = bEntityClipboard && clipboardDoc.Undo() &&
		clipboardDoc.Map().EntityById( pastedEntityId ) == nullptr && clipboardDoc.Redo() &&
		clipboardDoc.Map().EntityById( pastedEntityId ) != nullptr;
	ctx.Step( "entity-copy-paste", bEntityClipboard, QStringLiteral( "fresh id, offset, original targetname/output, undo/redo" ) );
	CHammerDocument specialDoc;
	specialDoc.NewDocument();
	const double specialOrigin[3] = { 10, 20, 30 };
	const int specialEntity = specialDoc.PlaceEntity( "logic_relay", specialOrigin );
	EntityLite *originalSpecial = specialDoc.Map().EntityById( specialEntity );
	if ( originalSpecial )
	{
		originalSpecial->keyvalues.insert( "targetname", "relay_a" );
		originalSpecial->connections.append( qMakePair( QStringLiteral( "OnTrigger" ), QStringLiteral( "relay_a,Trigger,,0,-1" ) ) );
	}
	const int specialBrush = specialDoc.CreateBrush( mins1, maxs1 );
	specialDoc.SelectObjects( QSet<int>() << specialEntity << specialBrush );
	const double specialStep[3] = { 64, -32, 16 };
	double copiedSize[3] = {};
	bool specialOk = specialDoc.CopySelection() && specialDoc.ClipboardSize( copiedSize ) &&
		copiedSize[0] == 128 && copiedSize[1] == 128 && copiedSize[2] == 64;
	// The size belongs to the copied objects, even after selection changes.
	specialDoc.SelectObject( specialEntity );
	specialOk = specialOk && specialDoc.ClipboardSize( copiedSize ) && copiedSize[0] == 128;
	const double invalidSpecialStep[3] = { std::numeric_limits<double>::quiet_NaN(), 0, 0 };
	specialOk = specialOk && specialDoc.PasteSpecial( 3, invalidSpecialStep ) < 0 &&
		specialDoc.Map().entities.size() == 1 && specialDoc.Map().solids.size() == 1 &&
		specialDoc.PasteSpecial( 3, specialStep ) >= 0 &&
		specialDoc.Map().entities.size() == 4 && specialDoc.Map().solids.size() == 4 &&
		specialDoc.SelectedIds().size() == 6;
	if ( specialOk )
	{
		for ( int i = 1; i <= 3; ++i )
		{
			const EntityLite &e = specialDoc.Map().entities[i];
			const SolidLite &s = specialDoc.Map().solids[i];
			specialOk = specialOk && e.id != specialEntity && e.origin[0] == 10 + i * 64 &&
				e.origin[1] == 20 - i * 32 && e.origin[2] == 30 + i * 16 &&
				e.keyvalues.value( "targetname" ) == QLatin1String( "relay_a" ) && e.connections.size() == 1 &&
				s.id != specialBrush && s.mins[0] == mins1[0] + i * 64;
		}
	}
	specialOk = specialOk && specialDoc.Undo() && specialDoc.Map().entities.size() == 1 &&
		specialDoc.Map().solids.size() == 1 && specialDoc.Redo() &&
		specialDoc.Map().entities.size() == 4 && specialDoc.Map().solids.size() == 4;
	ctx.Step( "paste-special-offsets", specialOk, QStringLiteral( "mixed copies, incremental offsets, names, I/O and single undo" ) );
	CHammerDocument centeredPaste;
	centeredPaste.NewDocument();
	const double centeredLo[3] = { 0, 0, 0 }, centeredHi[3] = { 32, 32, 32 };
	const int centeredId = centeredPaste.CreateBrush( centeredLo, centeredHi );
	double copiedCenter[3] = {};
	const double initialOffset[3] = { 84, 184, 284 }, centeredStep[3] = { 32, -16, 8 };
	bool centeredOk = centeredId >= 0 && centeredPaste.CopySelection() && centeredPaste.ClipboardCenter( copiedCenter ) &&
		copiedCenter[0] == 16 && copiedCenter[1] == 16 && copiedCenter[2] == 16 &&
		centeredPaste.PasteSpecial( 2, centeredStep, initialOffset ) >= 0 && centeredPaste.Map().solids.size() == 3;
	if ( centeredOk )
	{
		const SolidLite &firstCopy = centeredPaste.Map().solids[1];
		const SolidLite &secondCopy = centeredPaste.Map().solids[2];
		centeredOk = firstCopy.mins[0] == 84 && firstCopy.mins[1] == 184 && firstCopy.mins[2] == 284 &&
			secondCopy.mins[0] == 116 && secondCopy.mins[1] == 168 && secondCopy.mins[2] == 292;
	}
	centeredOk = centeredOk && centeredPaste.Undo() && centeredPaste.Map().solids.size() == 1 &&
		centeredPaste.Redo() && centeredPaste.Map().solids.size() == 3;
	const double invalidInitial[3] = { std::numeric_limits<double>::infinity(), 0, 0 };
	centeredOk = centeredOk && centeredPaste.PasteSpecial( 1, centeredStep, invalidInitial ) < 0 && centeredPaste.Map().solids.size() == 3;
	ctx.Step( "paste-special-centered", centeredOk, QStringLiteral( "first offset, incremental copies, undo/redo and invalid offset rejection" ) );
	CHammerDocument invalidCacheDoc;
	invalidCacheDoc.NewDocument();
	const int invalidCacheBrush = invalidCacheDoc.CreateBrush( mins1, maxs1 );
	SolidLite *invalidCacheSolid = invalidCacheDoc.Map().SolidById( invalidCacheBrush );
	bool invalidCacheOk = invalidCacheSolid && !invalidCacheSolid->sides.isEmpty();
	if ( invalidCacheOk )
	{
		invalidCacheSolid->sides[0].hasVerticesPlus = true;
		invalidCacheSolid->sides[0].verticesPlus = { QStringLiteral( "not a vertex" ) };
		invalidCacheOk = invalidCacheDoc.CopySelection() && invalidCacheDoc.PasteSpecial( 1, specialStep ) < 0 &&
			invalidCacheDoc.Map().solids.size() == 1 && invalidCacheDoc.IsSelected( invalidCacheBrush ) &&
			invalidCacheDoc.Map().SolidById( invalidCacheBrush )->sides[0].verticesPlus[0] == QLatin1String( "not a vertex" ) &&
			invalidCacheDoc.Undo() && invalidCacheDoc.Map().solids.isEmpty();
	}
	ctx.Step( "paste-special-invalid-cache", invalidCacheOk, QStringLiteral( "malformed vertices_plus rejects without editing map or history" ) );
	clipboardDoc.SelectObject( sourceEntityId );
	const bool bCutPaste = clipboardDoc.CutSelection() &&
		clipboardDoc.Map().EntityById( sourceEntityId ) == nullptr;
	const int cutPastedId = bCutPaste ? clipboardDoc.PasteSelection() : -1;
	const bool bCutHistory = cutPastedId >= 0 && clipboardDoc.Undo() &&
		clipboardDoc.Map().EntityById( cutPastedId ) == nullptr && clipboardDoc.Undo() &&
		clipboardDoc.Map().EntityById( sourceEntityId ) != nullptr && clipboardDoc.Redo() &&
		clipboardDoc.Map().EntityById( sourceEntityId ) == nullptr && clipboardDoc.Redo() &&
		clipboardDoc.Map().EntityById( cutPastedId ) != nullptr;
	ctx.Step( "entity-cut-paste", bCutPaste && bCutHistory, QStringLiteral( "cut, paste and independent undo steps" ) );

	CHammerDocument clipboardBrushDoc;
	clipboardBrushDoc.NewDocument();
	const int sourceBrushId = clipboardBrushDoc.CreateBrush( mins1, maxs1 );
	const SolidLite *sourceBrush = clipboardBrushDoc.Map().SolidById( sourceBrushId );
	QList<int> sourceFaceIds;
	if ( sourceBrush )
		for ( const SideLite &side : sourceBrush->sides ) sourceFaceIds.append( side.id );
	const bool copiedBrush = sourceBrush && clipboardBrushDoc.CopySelection();
	const int pastedBrushId = copiedBrush ? clipboardBrushDoc.PasteSelection() : -1;
	const SolidLite *pastedBrush = clipboardBrushDoc.Map().SolidById( pastedBrushId );
	bool bBrushClipboard = pastedBrush && pastedBrushId != sourceBrushId && pastedBrush->sides.size() == sourceFaceIds.size() &&
		pastedBrush->mins[0] == mins1[0] + 32.0 && clipboardBrushDoc.Map().solids.size() == 2;
	if ( bBrushClipboard )
	{
		for ( int i = 0; i < pastedBrush->sides.size(); ++i )
			bBrushClipboard = bBrushClipboard && pastedBrush->sides[i].id > 0 &&
				!sourceFaceIds.contains( pastedBrush->sides[i].id );
	}
	bBrushClipboard = bBrushClipboard && clipboardBrushDoc.Undo() &&
		clipboardBrushDoc.Map().SolidById( pastedBrushId ) == nullptr && clipboardBrushDoc.Redo() &&
		clipboardBrushDoc.Map().SolidById( pastedBrushId ) != nullptr;
	ctx.Step( "brush-copy-paste", bBrushClipboard, QStringLiteral( "new solid/face ids, translated geometry, undo/redo" ) );

	// Multi-object copy preserves relative geometry and original names/references,
	// including duplicate names, as ordinary Hammer paste does.
	CHammerDocument multiClipboard;
	multiClipboard.NewDocument();
	const int multiA = multiClipboard.PlaceEntity( "logic_relay", playerOrigin );
	EntityLite *multiFirst = multiClipboard.Map().EntityById( multiA );
	multiFirst->keyvalues.insert( "targetname", "shared" );
	multiFirst->keyvalues.insert( "parentname", "shared" );
	multiFirst->connections.append( qMakePair( QStringLiteral( "OnTrigger" ), QStringLiteral( "shared,Trigger,,0,-1" ) ) );
	const double multiOrigin[3] = { 64.0, 48.0, 16.0 };
	const int multiB = multiClipboard.PlaceEntity( "logic_relay", multiOrigin );
	multiClipboard.Map().EntityById( multiB )->keyvalues.insert( "targetname", "shared" );
	const int multiSolid = multiClipboard.CreateBrush( mins1, maxs1 );
	multiClipboard.SelectAll();
	const bool multiCopied = multiClipboard.CopySelection();
	const int multiPrimary = multiCopied ? multiClipboard.PasteSelection() : -1;
	bool bMultiClipboard = multiPrimary >= 0 && multiClipboard.SelectedIds().size() == 3 &&
		multiClipboard.Map().entities.size() == 4 && multiClipboard.Map().solids.size() == 2 &&
		multiClipboard.IsSelected( multiPrimary ) && multiPrimary != multiSolid;
	if ( bMultiClipboard )
	{
		const EntityLite &copyA = multiClipboard.Map().entities[2];
		const EntityLite &copyB = multiClipboard.Map().entities[3];
		const SolidLite &copySolid = multiClipboard.Map().solids[1];
		QSet<int> ids;
		for ( const EntityLite &entity : multiClipboard.Map().entities ) ids.insert( entity.id );
		for ( const SolidLite &solid : multiClipboard.Map().solids )
		{
			ids.insert( solid.id );
			for ( const SideLite &side : solid.sides ) ids.insert( side.id );
		}
		bMultiClipboard = ids.size() == 18 && copyA.origin[0] == playerOrigin[0] + 32.0 &&
			copyB.origin[0] == multiOrigin[0] + 32.0 && copyB.origin[1] == multiOrigin[1] &&
			copySolid.mins[0] == mins1[0] + 32.0 &&
			copyA.keyvalues.value( "targetname" ) == QLatin1String( "shared" ) &&
			copyB.keyvalues.value( "targetname" ) == QLatin1String( "shared" ) &&
			copyA.keyvalues.value( "parentname" ) == QLatin1String( "shared" ) &&
			copyA.connections.first().second == QLatin1String( "shared,Trigger,,0,-1" );
	}
	bMultiClipboard = bMultiClipboard && multiClipboard.Undo() && multiClipboard.SelectedIds().size() == 3 &&
		multiClipboard.IsSelected( multiA ) && multiClipboard.IsSelected( multiB ) && multiClipboard.IsSelected( multiSolid ) &&
		multiClipboard.Redo() && multiClipboard.SelectedIds().size() == 3 && multiClipboard.CutSelection() &&
		multiClipboard.Map().entities.size() == 2 && multiClipboard.Map().solids.size() == 1 &&
		multiClipboard.Undo() && multiClipboard.SelectedIds().size() == 3 && multiClipboard.Map().entities.size() == 4;
	ctx.Step( "multi-copy-cut-paste", bMultiClipboard, QStringLiteral( "mixed selection, unique ids, same names and references, undo/redo" ) );

	// Multi-selection commands include mixed point entities and world brushes.
	CHammerDocument selectionDoc;
	selectionDoc.NewDocument();
	const int selectEntityA = selectionDoc.PlaceEntity( "info_player_start", playerOrigin );
	const double entityBOrigin[3] = { 64.0, 0.0, 16.0 };
	const int selectEntityB = selectionDoc.PlaceEntity( "light", entityBOrigin );
	const int selectBrushA = selectionDoc.CreateBrush( mins1, maxs1 );
	const double minsB[3] = { 128.0, -64.0, 0.0 };
	const double maxsB[3] = { 256.0, 64.0, 64.0 };
	const int selectBrushB = selectionDoc.CreateBrush( minsB, maxsB );
	selectionDoc.SelectAll();
	bool bMultiSelection = selectionDoc.SelectedIds().size() == 4 &&
		selectionDoc.IsSelected( selectEntityA ) && selectionDoc.IsSelected( selectEntityB ) &&
		selectionDoc.IsSelected( selectBrushA ) && selectionDoc.IsSelected( selectBrushB );
	selectionDoc.ToggleSelection( selectBrushA );
	bMultiSelection = bMultiSelection && selectionDoc.SelectedIds().size() == 3 &&
		!selectionDoc.IsSelected( selectBrushA ) && selectionDoc.SelectedId() == selectBrushB;
	selectionDoc.InvertSelection();
	bMultiSelection = bMultiSelection && selectionDoc.SelectedIds().size() == 1 &&
		selectionDoc.IsSelected( selectBrushA );
	selectionDoc.SelectObject( selectEntityA );
	selectionDoc.ToggleSelection( selectBrushA );
	const double multiDelta[3] = { 32.0, 0.0, 0.0 };
	bMultiSelection = bMultiSelection && selectionDoc.SelectedIds().size() == 2 &&
		selectionDoc.MoveSelection( multiDelta ) &&
		selectionDoc.Map().EntityById( selectEntityA )->origin[0] == playerOrigin[0] + 32.0 &&
		selectionDoc.Map().SolidById( selectBrushA )->mins[0] == mins1[0] + 32.0 &&
		selectionDoc.Undo() && selectionDoc.SelectedIds().size() == 2 &&
		selectionDoc.Map().EntityById( selectEntityA )->origin[0] == playerOrigin[0] &&
		selectionDoc.Map().SolidById( selectBrushA )->mins[0] == mins1[0];
	bMultiSelection = bMultiSelection && selectionDoc.DeleteSelection() &&
		selectionDoc.Map().EntityById( selectEntityA ) == nullptr &&
		selectionDoc.Map().SolidById( selectBrushA ) == nullptr &&
		selectionDoc.SelectedIds().isEmpty() && selectionDoc.Undo() &&
		selectionDoc.Map().EntityById( selectEntityA ) != nullptr &&
		selectionDoc.Map().SolidById( selectBrushA ) != nullptr &&
		selectionDoc.SelectedIds().size() == 2;
	selectionDoc.SelectAll();
	selectionDoc.DeleteSelection();
	bMultiSelection = bMultiSelection && selectionDoc.Map().entities.isEmpty() &&
		selectionDoc.Map().solids.isEmpty() && selectionDoc.Undo() &&
		selectionDoc.Map().entities.size() == 2 && selectionDoc.Map().solids.size() == 2;
	ctx.Step( "multi-selection-edit", bMultiSelection,
		QStringLiteral( "all, invert, toggle, mixed move/delete and undo restore selection" ) );

	// Entity Report-style key/value selection: case-insensitive, partial or
	// exact, empty key searches all properties, and brushes are excluded.
	// FGD properties without defaults should be visible without changing the map.
	QMap<QString, QString> testSchema;
	testSchema.insert( QStringLiteral( "targetname" ), QStringLiteral( "Name" ) );
	testSchema.insert( QStringLiteral( "model" ), QStringLiteral( "World Model" ) );
	QMap<QString, QString> testValues;
	testValues.insert( QStringLiteral( "TargetName" ), QStringLiteral( "lamp" ) );
	testValues.insert( QStringLiteral( "custom_key" ), QStringLiteral( "custom" ) );
	const auto propertyRows = EntityPropertyRows( testValues, testSchema );
	bool haveName = false, haveModel = false, haveCustom = false;
	for ( const EntityPropertyRow &row : propertyRows )
	{
		if ( row.key == QLatin1String( "TargetName" ) && row.caption == QLatin1String( "Name" ) && row.value == QLatin1String( "lamp" ) ) haveName = true;
		if ( row.key == QLatin1String( "model" ) && row.value.isEmpty() ) haveModel = true;
		if ( row.key == QLatin1String( "custom_key" ) && row.value == QLatin1String( "custom" ) ) haveCustom = true;
	}
	ctx.Step( "entity-property-rows", propertyRows.size() == 3 && haveName && haveModel && haveCustom && !testValues.contains( "model" ),
		QStringLiteral( "FGD labels, unset and custom keys" ) );

	// Actual property panel and document batch editing: display placeholders must
	// never become VMF values merely by entering and leaving the editor.
	{
		CMainWindow batchWindow( nullptr, false );
		CHammerDocument *doc = batchWindow.findChild<CHammerDocument *>();
		QTreeWidget *tree = batchWindow.findChild<QTreeWidget *>( QStringLiteral( "EntityPropertyTree" ) );
		QTemporaryDir fgdDir;
		QFile fgdFile( fgdDir.filePath( QStringLiteral( "batch.fgd" ) ) );
		bool fgdOk = fgdDir.isValid() && fgdFile.open( QIODevice::WriteOnly ) &&
			fgdFile.write( "@PointClass = light : \"Test\" [ targetname(string) : \"Name\" optional(string) : \"Optional\" unset(string) : \"Unset\" ]\n" ) > 0;
		fgdFile.close();
		fgdOk = fgdOk && doc->LoadFgd( fgdFile.fileName() );
		EntityLite a, b, other;
		a.id = doc->Map().AllocId(); a.classname = QStringLiteral( "light" );
		b.id = doc->Map().AllocId(); b.classname = QStringLiteral( "LIGHT" );
		other.id = doc->Map().AllocId(); other.classname = QStringLiteral( "other_class" );
		a.keyvalues.insert( QStringLiteral( "targetname" ), QStringLiteral( "first" ) );
		b.keyvalues.insert( QStringLiteral( "TargetName" ), QStringLiteral( "second" ) );
		a.keyvalues.insert( QStringLiteral( "shared_empty" ), QString() );
		b.keyvalues.insert( QStringLiteral( "shared_empty" ), QString() );
		b.keyvalues.insert( QStringLiteral( "optional" ), QString() );
		a.keyvalues.insert( QStringLiteral( "only_first" ), QStringLiteral( "keep" ) );
		doc->Map().entities.append( a ); doc->Map().entities.append( b ); doc->Map().entities.append( other );
		doc->SelectObjects( QSet<int>() << a.id << b.id );
		QTreeWidgetItem *mixed = nullptr, *empty = nullptr, *optional = nullptr, *unset = nullptr; bool hiddenNonCommon = true;
		if ( tree ) for ( int i = 0; i < tree->topLevelItemCount(); ++i )
		{
			QTreeWidgetItem *row = tree->topLevelItem( i );
			const QString key = row->data( 0, Qt::UserRole ).toString();
			if ( key.compare( QLatin1String( "targetname" ), Qt::CaseInsensitive ) == 0 ) mixed = row;
			if ( key == QLatin1String( "shared_empty" ) ) empty = row;
			if ( key == QLatin1String( "optional" ) ) optional = row;
			if ( key == QLatin1String( "unset" ) ) unset = row;
			if ( key == QLatin1String( "only_first" ) ) hiddenNonCommon = false;
		}
		bool ok = fgdOk && mixed && empty && optional && unset && hiddenNonCommon && mixed->data( 1, Qt::UserRole + 3 ).toBool() &&
			optional->data( 1, Qt::UserRole + 3 ).toBool() && unset->data( 1, Qt::UserRole + 3 ).toBool() &&
			optional->text( 1 ) != unset->text( 1 ) &&
			!empty->data( 1, Qt::UserRole + 3 ).toBool() && empty->text( 1 ).isEmpty() && !doc->IsDirty();
		if ( ok )
		{
			QModelIndex index = tree->model()->index( tree->indexOfTopLevelItem( mixed ), 1 );
			auto *delegate = tree->itemDelegateForColumn( 1 );
			QStyleOptionViewItem option;
			QWidget *editor = delegate->createEditor( tree, option, index );
			delegate->setEditorData( editor, index );
			delegate->setModelData( editor, tree->model(), index );
			delete editor;
			ok = !doc->IsDirty() && doc->Map().EntityById( a.id )->keyvalues.value( QStringLiteral( "targetname" ) ) == QLatin1String( "first" );
			mixed->setText( 1, QStringLiteral( "both" ) );
			ok = ok && doc->Map().EntityById( a.id )->keyvalues.value( QStringLiteral( "targetname" ) ) == QLatin1String( "both" ) &&
				doc->Map().EntityById( b.id )->keyvalues.value( QStringLiteral( "TargetName" ) ) == QLatin1String( "both" ) &&
				!doc->Map().EntityById( b.id )->keyvalues.contains( QStringLiteral( "targetname" ) ) && doc->Undo() &&
				doc->Map().EntityById( a.id )->keyvalues.value( QStringLiteral( "targetname" ) ) == QLatin1String( "first" ) &&
				doc->Map().EntityById( b.id )->keyvalues.value( QStringLiteral( "TargetName" ) ) == QLatin1String( "second" ) && doc->Redo();
			ok = ok && doc->SetSelectedEntitiesKey( QStringLiteral( "optional" ), QString() ) &&
				doc->Map().EntityById( a.id )->keyvalues.contains( QStringLiteral( "optional" ) ) &&
				doc->Map().EntityById( a.id )->keyvalues.value( QStringLiteral( "optional" ) ).isEmpty() && doc->Undo() &&
				!doc->Map().EntityById( a.id )->keyvalues.contains( QStringLiteral( "optional" ) ) &&
				doc->Map().EntityById( b.id )->keyvalues.contains( QStringLiteral( "optional" ) );
			doc->SelectObjects( QSet<int>() << a.id << other.id );
			ok = ok && !doc->SetSelectedEntitiesKey( QStringLiteral( "targetname" ), QStringLiteral( "bad" ) ) &&
				doc->Map().EntityById( a.id )->keyvalues.value( QStringLiteral( "targetname" ) ) == QLatin1String( "both" );
			// Force both entity class names to differ in case from the FGD name.
			// This must work regardless of QSet iteration order.
			doc->Map().EntityById( a.id )->classname = QStringLiteral( "LIGHT" );
			doc->SelectObjects( QSet<int>() << a.id << b.id );
			bool foundCanonicalSchema = false;
			for ( int i = 0; i < tree->topLevelItemCount(); ++i )
				if ( tree->topLevelItem( i )->data( 0, Qt::UserRole ).toString() == QLatin1String( "unset" ) &&
					 tree->topLevelItem( i )->data( 1, Qt::UserRole + 3 ).toBool() ) foundCanonicalSchema = true;
			ok = ok && foundCanonicalSchema;
		}
		ctx.Step( "entity-batch-properties", ok,
			QStringLiteral( "mixed and empty values, editor no-op, case-preserving edit, undo/redo and mixed class guard" ) );
	}

	CHammerDocument propertySelection;
	propertySelection.NewDocument();
	EntityLite propertyA;
	propertyA.id = propertySelection.Map().AllocId();
	propertyA.classname = QStringLiteral( "light" );
	propertyA.keyvalues.insert( QStringLiteral( "targetname" ), QStringLiteral( "Hall_Light" ) );
	propertyA.origin[0] = 64;
	propertySelection.Map().entities.append( propertyA );
	EntityLite propertyB;
	propertyB.id = propertySelection.Map().AllocId();
	propertyB.classname = QStringLiteral( "info_player_start" );
	propertyB.keyvalues.insert( QStringLiteral( "targetname" ), QStringLiteral( "hall" ) );
	propertySelection.Map().entities.append( propertyB );
	const int propertyBrush = propertySelection.CreateBrush( mins1, maxs1 );
	propertySelection.SetDirty( false );
	bool bPropertySelection = propertySelection.SelectEntitiesByProperty( "TARGETNAME", "hAlL", false ) == 2 &&
		propertySelection.IsSelected( propertyA.id ) && propertySelection.IsSelected( propertyB.id ) &&
		!propertySelection.IsSelected( propertyBrush ) && !propertySelection.IsDirty();
	bPropertySelection = bPropertySelection && propertySelection.SelectEntitiesByProperty( "targetname", "HALL", true ) == 1 &&
		propertySelection.IsSelected( propertyB.id );
	bPropertySelection = bPropertySelection && propertySelection.SelectEntitiesByProperty( "", "LIGHT", false ) == 1 &&
		propertySelection.IsSelected( propertyA.id );
	bPropertySelection = bPropertySelection && propertySelection.SelectEntitiesByProperty( "origin", "64 0 0", true ) == 1 &&
		propertySelection.IsSelected( propertyA.id );
	bPropertySelection = bPropertySelection && propertySelection.SelectEntitiesByProperty( "missing", "hall", false ) == 0 &&
		propertySelection.SelectedIds().isEmpty();
	propertySelection.SelectObject( propertyB.id );
	bPropertySelection = bPropertySelection && propertySelection.SelectEntitiesByProperty( "", "", false ) == 0 &&
		propertySelection.IsSelected( propertyB.id );
	ctx.Step( "property-selection", bPropertySelection,
		QStringLiteral( "case, exact/partial, any key, classname, origin and selection preservation" ) );

	// A drag owns one open undo transaction even if selection or mouse capture
	// changes before release. Exercise the actual widget event handlers.
	bool bDragTransactions = true;
	for ( int mode = 0; mode < 7 && bDragTransactions; ++mode )
	{
		CHammerDocument dragDoc;
		dragDoc.NewDocument();
		const int dragBrush = dragDoc.CreateBrush( mins1, maxs1 );
		CQtViewport2D view( CQtViewport2D::VIEWAXIS_TOP );
		view.resize( 200, 200 );
		view.setDocument( &dragDoc );
		QMouseEvent press( QEvent::MouseButtonPress, QPointF( 100, 100 ), Qt::LeftButton, Qt::LeftButton, Qt::NoModifier );
		QCoreApplication::sendEvent( &view, &press );
		QMouseEvent move( QEvent::MouseMove, QPointF( 116, 100 ), Qt::NoButton, Qt::LeftButton, Qt::NoModifier );
		QCoreApplication::sendEvent( &view, &move );
		const SolidLite *moved = dragDoc.Map().SolidById( dragBrush );
		bDragTransactions = moved && moved->mins[0] == mins1[0] + 32.0;
		if ( mode == 1 )
		{
			QMouseEvent middle( QEvent::MouseButtonPress, QPointF( 116, 100 ), Qt::MiddleButton,
				Qt::LeftButton | Qt::MiddleButton, Qt::NoModifier );
			QCoreApplication::sendEvent( &view, &middle );
		}
		else if ( mode == 2 )
		{
			QEvent ungrab( QEvent::UngrabMouse );
			QCoreApplication::sendEvent( &view, &ungrab );
		}
		else if ( mode == 3 )
		{
			QFocusEvent focusOut( QEvent::FocusOut );
			QCoreApplication::sendEvent( &view, &focusOut );
		}
		else if ( mode == 4 )
		{
			QEvent hide( QEvent::Hide );
			QCoreApplication::sendEvent( &view, &hide );
		}
		else if ( mode == 5 )
		{
			bDragTransactions = bDragTransactions && dragDoc.DeleteSelection();
		}
		else if ( mode == 6 )
		{
			view.setViewAxis( CQtViewport2D::VIEWAXIS_FRONT );
		}
		QMouseEvent release( QEvent::MouseButtonRelease, QPointF( 116, 100 ), Qt::LeftButton,
			Qt::NoButton, Qt::NoModifier );
		QCoreApplication::sendEvent( &view, &release );
		bDragTransactions = bDragTransactions && dragDoc.Undo();
		const SolidLite *restored = dragDoc.Map().SolidById( dragBrush );
		bDragTransactions = bDragTransactions && restored && restored->mins[0] == mins1[0];
	}
	ctx.Step( "viewport-drag-transactions", bDragTransactions,
		QStringLiteral( "release, middle press, ungrab, focus loss, hide, selection deletion, projection change" ) );

	// Escape restores the drag snapshot without consuming undo/redo, and a
	// later mouse release or capture loss must not reopen the transaction.
	CHammerDocument cancelDoc;
	cancelDoc.NewDocument();
	const int cancelBrush = cancelDoc.CreateBrush( mins1, maxs1 );
	double cancelDelta[3] = { 16.0, 0.0, 0.0 };
	bool bCancelDrag = cancelDoc.MoveSelection( cancelDelta ) && cancelDoc.Undo();
	bCancelDrag = bCancelDrag && cancelDoc.SaveToFile( guardDir.filePath( "cancel-drag.vmf" ) );
	CQtViewport2D cancelView( CQtViewport2D::VIEWAXIS_TOP );
	cancelView.resize( 200, 200 );
	cancelView.setDocument( &cancelDoc );
	QMouseEvent cancelPress( QEvent::MouseButtonPress, QPointF( 100, 100 ), Qt::LeftButton, Qt::LeftButton, Qt::NoModifier );
	QCoreApplication::sendEvent( &cancelView, &cancelPress );
	QMouseEvent cancelMove( QEvent::MouseMove, QPointF( 116, 100 ), Qt::NoButton, Qt::LeftButton, Qt::NoModifier );
	QCoreApplication::sendEvent( &cancelView, &cancelMove );
	QKeyEvent escape( QEvent::KeyPress, Qt::Key_Escape, Qt::NoModifier );
	QCoreApplication::sendEvent( &cancelView, &escape );
	const SolidLite *cancelled = cancelDoc.Map().SolidById( cancelBrush );
	bCancelDrag = bCancelDrag && cancelled && cancelled->mins[0] == mins1[0] &&
		cancelDoc.IsSelected( cancelBrush ) && !cancelDoc.IsDirty();
	QEvent cancelUngrab( QEvent::UngrabMouse );
	QCoreApplication::sendEvent( &cancelView, &cancelUngrab );
	QMouseEvent cancelRelease( QEvent::MouseButtonRelease, QPointF( 116, 100 ), Qt::LeftButton, Qt::NoButton, Qt::NoModifier );
	QCoreApplication::sendEvent( &cancelView, &cancelRelease );
	bCancelDrag = bCancelDrag && cancelDoc.Redo() && cancelDoc.Map().SolidById( cancelBrush )->mins[0] == mins1[0] + 16.0 &&
		cancelDoc.Undo() && cancelDoc.Map().SolidById( cancelBrush )->mins[0] == mins1[0];
	QCoreApplication::sendEvent( &cancelView, &escape );
	bCancelDrag = bCancelDrag && cancelDoc.SelectedIds().isEmpty();
	ctx.Step( "viewport-cancel-drag", bCancelDrag, QStringLiteral( "escape restores position and history; ungrab and release remain safe" ) );

	// Arrow nudging is opt-in as in Hammer; the three projections use their
	// own horizontal and vertical axes, with Ctrl bypassing grid increments.
	CHammerDocument nudgeDoc;
	nudgeDoc.NewDocument();
	nudgeDoc.SetGridSize( 16 );
	const int nudgeBrush = nudgeDoc.CreateBrush( mins1, maxs1 );
	const double nudgeOrigin[3] = { 0, 0, 0 };
	const int nudgeEntity = nudgeDoc.PlaceEntity( QStringLiteral( "info_target" ), nudgeOrigin );
	nudgeDoc.SelectObjects( QSet<int>() << nudgeBrush << nudgeEntity );
	CQtViewport2D nudgeTop( CQtViewport2D::VIEWAXIS_TOP );
	CQtViewport2D nudgeFront( CQtViewport2D::VIEWAXIS_FRONT );
	CQtViewport2D nudgeSide( CQtViewport2D::VIEWAXIS_SIDE );
	nudgeTop.setDocument( &nudgeDoc ); nudgeFront.setDocument( &nudgeDoc ); nudgeSide.setDocument( &nudgeDoc );
	QKeyEvent rightKey( QEvent::KeyPress, Qt::Key_Right, Qt::NoModifier );
	QCoreApplication::sendEvent( &nudgeTop, &rightKey );
	bool bNudge = nudgeDoc.Map().EntityById( nudgeEntity )->origin[0] == 0;
	nudgeTop.setNudgeEnabled( true ); nudgeFront.setNudgeEnabled( true ); nudgeSide.setNudgeEnabled( true );
	QCoreApplication::sendEvent( &nudgeTop, &rightKey );
	QKeyEvent fineUp( QEvent::KeyPress, Qt::Key_Up, Qt::ControlModifier );
	QCoreApplication::sendEvent( &nudgeFront, &fineUp );
	QKeyEvent leftKey( QEvent::KeyPress, Qt::Key_Left, Qt::NoModifier );
	QCoreApplication::sendEvent( &nudgeSide, &leftKey );
	const EntityLite *nudgePoint = nudgeDoc.Map().EntityById( nudgeEntity );
	const SolidLite *nudgeSolid = nudgeDoc.Map().SolidById( nudgeBrush );
	bNudge = bNudge && nudgePoint && nudgeSolid && nudgePoint->origin[0] == 16 && nudgePoint->origin[1] == -16 &&
		nudgePoint->origin[2] == 1 && nudgeSolid->mins[0] == mins1[0] + 16 && nudgeSolid->mins[1] == mins1[1] - 16 &&
		nudgeSolid->mins[2] == mins1[2] + 1 && nudgeDoc.Undo() && nudgeDoc.Map().EntityById( nudgeEntity )->origin[1] == 0 &&
		nudgeDoc.Redo() && nudgeDoc.Map().EntityById( nudgeEntity )->origin[1] == -16;
	ctx.Step( "viewport-arrow-nudge", bNudge, QStringLiteral( "opt-in, view axes, mixed selection, fine motion and undo" ) );

	// Hiding the visual grid must not disable snapping or change map content.
	CHammerDocument gridDoc;
	gridDoc.NewDocument();
	CQtViewport2D gridView( CQtViewport2D::VIEWAXIS_TOP );
	gridView.resize( 200, 200 );
	gridView.setDocument( &gridDoc );
	QImage gridShown( gridView.size(), QImage::Format_ARGB32 );
	gridView.render( &gridShown );
	gridView.setGridVisible( false );
	QImage gridHidden( gridView.size(), QImage::Format_ARGB32 );
	gridView.render( &gridHidden );
	bool bGridVisibility = !gridView.gridVisible() && gridDoc.SnapEnabled() &&
		!gridDoc.IsDirty() && gridShown != gridHidden;
	gridView.setGridVisible( true );
	QImage gridRestored( gridView.size(), QImage::Format_ARGB32 );
	gridView.render( &gridRestored );
	bGridVisibility = bGridVisibility && gridView.gridVisible() && gridShown == gridRestored;
	ctx.Step( "grid-visibility", bGridVisibility,
		QStringLiteral( "rendered grid hides and restores without changing snap or map" ) );
	ctx.Step( "grid-settings-dialog", GridSettingsApplyCancelTest(),
		QStringLiteral( "apply spacing/show/snap, cancel untouched, invalid size rejected" ) );
	ctx.Step( "mapcheck-go-to", MapCheckGoToTest(),
		QStringLiteral( "issue object ids, selection and center, no-object and stale ids" ) );

	// 2D navigation keeps middle-button panning continuous and preserves the
	// world point under the cursor when the wheel changes zoom.
	CHammerDocument navigationDoc;
	navigationDoc.NewDocument();
	CQtViewport2D nav2dView( CQtViewport2D::VIEWAXIS_TOP );
	nav2dView.resize( 240, 180 );
	nav2dView.setDocument( &navigationDoc );
	const QPointF centerBeforePan = nav2dView.centerWorld();
	QMouseEvent panPress( QEvent::MouseButtonPress, QPointF( 120, 90 ),
		Qt::MiddleButton, Qt::MiddleButton, Qt::NoModifier );
	QCoreApplication::sendEvent( &nav2dView, &panPress );
	QMouseEvent panMove( QEvent::MouseMove, QPointF( 144, 66 ), Qt::NoButton,
		Qt::MiddleButton, Qt::NoModifier );
	QCoreApplication::sendEvent( &nav2dView, &panMove );
	QMouseEvent panRelease( QEvent::MouseButtonRelease, QPointF( 144, 66 ),
		Qt::MiddleButton, Qt::NoButton, Qt::NoModifier );
	QCoreApplication::sendEvent( &nav2dView, &panRelease );
	const QPointF centerAfterPan = nav2dView.centerWorld();
	const bool panMoved = centerAfterPan != centerBeforePan;
	const QPointF anchor( 168, 72 );
	double anchorU = 0.0, anchorV = 0.0;
	// ScreenToWorld is private; render-independent anchoring is checked by
	// comparing the center delta against the known zoom ratio and cursor.
	const double oldZoom = nav2dView.zoom();
	QWheelEvent zoomEvent( anchor, anchor, QPoint( 0, 120 ), QPoint( 0, 120 ),
		Qt::NoButton, Qt::NoModifier, Qt::NoScrollPhase, false );
	QCoreApplication::sendEvent( &nav2dView, &zoomEvent );
	const double newZoom = nav2dView.zoom();
	const QPointF centerAfterZoom = nav2dView.centerWorld();
	const double expectedU = ( anchor.x() - nav2dView.width() / 2.0 ) * ( 1.0 / oldZoom - 1.0 / newZoom );
	const double expectedV = -( anchor.y() - nav2dView.height() / 2.0 ) * ( 1.0 / oldZoom - 1.0 / newZoom );
	const bool zoomAnchored = newZoom > oldZoom &&
		qAbs( ( centerAfterZoom.x() - centerAfterPan.x() ) - expectedU ) < 1e-6 &&
		qAbs( ( centerAfterZoom.y() - centerAfterPan.y() ) - expectedV ) < 1e-6;
	ctx.Step( "viewport-navigation", panMoved && zoomAnchored,
		QStringLiteral( "middle-button pan and cursor-anchored wheel zoom" ) );
	// The hint must keep its primary action readable in a narrow four-view pane.
	// Full instructions remain available by hovering, rather than covering the map.
	CHammerDocument hintDoc;
	hintDoc.NewDocument();
	CQtViewport2D hint2d( CQtViewport2D::VIEWAXIS_TOP );
	CQtViewport3D hint3d;
	hint2d.setDocument( &hintDoc ); hint3d.setDocument( &hintDoc );
	const QFontMetrics hintMetrics( HammerTheme::ViewportFont( -1 ) );
	bool compactHints = true;
	QString previous2dHint, previous3dHint;
	for ( EHammerTool tool : { TOOL_SELECT, TOOL_ENTITY, TOOL_BLOCK, TOOL_TEXTURE, TOOL_CLIP } )
	{
		hintDoc.SetTool( tool );
		const QString text2d = hint2d.interactionHint(), text3d = hint3d.interactionHint();
		compactHints = compactHints && !text2d.isEmpty() && !text3d.isEmpty() &&
			hintMetrics.horizontalAdvance( text2d ) <= 184 && hintMetrics.horizontalAdvance( text3d ) <= 184 &&
			text2d != previous2dHint && text3d != previous3dHint &&
			hint2d.interactionHelp().size() > text2d.size() && hint3d.interactionHelp().size() > text3d.size();
		previous2dHint = text2d; previous3dHint = text3d;
	}
	QKeyEvent hintLook( QEvent::KeyPress, Qt::Key_Z, Qt::NoModifier );
	QCoreApplication::sendEvent( &hint3d, &hintLook );
	compactHints = compactHints && hintMetrics.horizontalAdvance( hint3d.interactionHint() ) <= 184 &&
		hint3d.interactionHint() != previous3dHint && hint3d.interactionHelp().contains( QStringLiteral( "WASD" ) );
	QKeyEvent hintExit( QEvent::KeyPress, Qt::Key_Escape, Qt::NoModifier );
	QCoreApplication::sendEvent( &hint3d, &hintExit );
	compactHints = compactHints && hint3d.interactionHint() == previous3dHint && !hintDoc.IsDirty();
	ctx.Step( "viewport-compact-hints", compactHints,
		QStringLiteral( "primary actions fit 200px panes, tool-aware help, mouse-look exit and no content changes" ) );
	// Each 2D pane can change projection without changing the document or zoom.
	const double viewCenter[3] = { 100, 200, 300 };
	nav2dView.centerOn( viewCenter );
	const double projectionZoom = nav2dView.zoom();
	nav2dView.setViewAxis( CQtViewport2D::VIEWAXIS_FRONT );
	bool bViewProjection = nav2dView.centerWorld() == QPointF( 100, 300 ) && nav2dView.zoom() == projectionZoom;
	nav2dView.setViewAxis( CQtViewport2D::VIEWAXIS_SIDE );
	bViewProjection = bViewProjection && nav2dView.centerWorld() == QPointF( 200, 300 );
	nav2dView.setViewAxis( CQtViewport2D::VIEWAXIS_TOP );
	double centerAfterProjection[3]; nav2dView.centerCoordinates( centerAfterProjection );
	bViewProjection = bViewProjection && nav2dView.centerWorld() == QPointF( 100, 200 ) &&
		centerAfterProjection[2] == 300 && !navigationDoc.IsDirty();
	ctx.Step( "viewport-projection-switch", bViewProjection,
		QStringLiteral( "XY/XZ/YZ switching preserves world center, zoom and document" ) );
	bool bProjectionPicking = true;
	for ( int axis = 0; axis < 3 && bProjectionPicking; ++axis )
	{
		CHammerDocument projectionDoc; projectionDoc.NewDocument();
		const int id = projectionDoc.CreateBrush( mins1, maxs1 );
		CQtViewport2D projectionView( CQtViewport2D::VIEWAXIS_TOP );
		projectionView.resize( 200, 200 ); projectionView.setDocument( &projectionDoc );
		projectionView.setViewAxis( static_cast<CQtViewport2D::EViewAxis>( axis ) );
		QMouseEvent press( QEvent::MouseButtonPress, QPointF( 100, 100 ), Qt::LeftButton, Qt::LeftButton, Qt::NoModifier );
		QCoreApplication::sendEvent( &projectionView, &press );
		QMouseEvent move( QEvent::MouseMove, QPointF( 116, 100 ), Qt::NoButton, Qt::LeftButton, Qt::NoModifier );
		QCoreApplication::sendEvent( &projectionView, &move );
		QMouseEvent release( QEvent::MouseButtonRelease, QPointF( 116, 100 ), Qt::LeftButton, Qt::NoButton, Qt::NoModifier );
		QCoreApplication::sendEvent( &projectionView, &release );
		const int worldAxis = axis == CQtViewport2D::VIEWAXIS_SIDE ? 1 : 0;
		const SolidLite *moved = projectionDoc.Map().SolidById( id );
		bProjectionPicking = moved && moved->mins[worldAxis] == mins1[worldAxis] + 32 &&
			projectionDoc.Undo() && projectionDoc.Map().SolidById( id )->mins[worldAxis] == mins1[worldAxis];
	}
	ctx.Step( "viewport-projection-picking", bProjectionPicking,
		QStringLiteral( "selection and drag use the switched view axes and undo" ) );
	{
		CHammerDocument cameraDoc;
		cameraDoc.NewDocument();
		cameraDoc.SetTool( TOOL_CAMERA );
		CQtViewport2D cameraView( CQtViewport2D::VIEWAXIS_TOP );
		cameraView.resize( 200, 200 );
		cameraView.setDocument( &cameraDoc );
		int activated = -1;
		QObject::connect( &cameraView, &CQtViewport2D::savedCameraActivated, &cameraView, [&]( int index ) { activated = index; } );
		const auto drag = [&]( QPointF start, QPointF end, Qt::KeyboardModifiers modifiers, bool cancel = false ) {
			QMouseEvent press( QEvent::MouseButtonPress, start, Qt::LeftButton, Qt::LeftButton, modifiers );
			QCoreApplication::sendEvent( &cameraView, &press );
			if ( cancel )
			{
				QKeyEvent escape( QEvent::KeyPress, Qt::Key_Escape, Qt::NoModifier );
				QCoreApplication::sendEvent( &cameraView, &escape );
			}
			QMouseEvent release( QEvent::MouseButtonRelease, end, Qt::LeftButton, Qt::NoButton, modifiers );
			QCoreApplication::sendEvent( &cameraView, &release );
		};
		drag( QPointF( 100, 100 ), QPointF( 116, 100 ), Qt::ShiftModifier, true );
		bool ok = cameraDoc.Map().cameras.isEmpty() && activated == -1;
		drag( QPointF( 100, 100 ), QPointF( 116, 100 ), Qt::ShiftModifier );
		double eye[3], look[3];
		ok = ok && cameraDoc.Map().cameras.size() == 1 && activated == 0 &&
			SavedCameraActiveIndex( cameraDoc.Map() ) == 0 &&
			ReadSavedCameraPoints( cameraDoc.Map().cameras.first(), eye, look ) && eye[0] == 0 && look[0] == 32;
		drag( QPointF( 116, 100 ), QPointF( 132, 100 ), Qt::NoModifier );
		ok = ok && ReadSavedCameraPoints( cameraDoc.Map().cameras.first(), eye, look ) && look[0] == 64 &&
			cameraDoc.Undo() && ReadSavedCameraPoints( cameraDoc.Map().cameras.first(), eye, look ) && look[0] == 32 &&
			cameraDoc.Undo() && cameraDoc.Map().cameras.isEmpty();
		ctx.Step( "viewport-camera-tool", ok, QStringLiteral( "create, edit handle, activate, cancel and undo" ) );
	}
	{
		CHammerDocument doc; doc.NewDocument();
		const double lo[3] = { 0,0,0 }, hi[3] = { 64,64,64 };
		const int id = doc.CreateBrush( lo, hi ); doc.SelectObject( id ); doc.SetTool( TOOL_VERTEX );
		CQtViewport2D view( CQtViewport2D::VIEWAXIS_TOP ); view.resize( 200,200 ); view.setDocument( &doc );
		auto press = [&] { QMouseEvent event( QEvent::MouseButtonPress, QPointF( 132,68 ), Qt::LeftButton, Qt::LeftButton, Qt::NoModifier ); QCoreApplication::sendEvent( &view, &event ); };
		auto release = [&] { QMouseEvent event( QEvent::MouseButtonRelease, QPointF( 140,68 ), Qt::LeftButton, Qt::NoButton, Qt::NoModifier ); QCoreApplication::sendEvent( &view, &event ); };
		press(); QKeyEvent escape( QEvent::KeyPress, Qt::Key_Escape, Qt::NoModifier ); QCoreApplication::sendEvent( &view, &escape ); release();
		bool ok = qAbs( doc.Map().SolidById( id )->maxs[0] - 64 ) < 0.001;
		press(); QEvent focusOut( QEvent::FocusOut ); QCoreApplication::sendEvent( &view, &focusOut ); release();
		ok = ok && qAbs( doc.Map().SolidById( id )->maxs[0] - 64 ) < 0.001;
		press(); release();
		ok = ok && qAbs( doc.Map().SolidById( id )->maxs[0] - 80 ) < 0.001 && doc.Undo() &&
			qAbs( doc.Map().SolidById( id )->maxs[0] - 64 ) < 0.001;
		ctx.Step( "viewport-vertex-edit", ok, QStringLiteral( "projected handle drag, Escape/focus cancellation and undo" ) );
	}
	// Draw a horizontal clip line in each orthographic view. Its normal uses
	// the VMF cross(depth,line) convention, including the reversed XZ view.
	{
		bool clipInteractionOk = true;
		for ( int projection = 0; projection < 3; ++projection ) for ( int mode = 0; mode < 3; ++mode )
		{
			CHammerDocument doc; doc.NewDocument();
			const double lo[3] = { -64,-64,-64 }, hi[3] = { 64,64,64 };
			const int id = doc.CreateBrush( lo, hi ); doc.SelectObject( id ); doc.SetTool( TOOL_CLIP );
			CQtViewport2D view( static_cast<CQtViewport2D::EViewAxis>( projection ) );
			view.resize( 200, 200 ); view.setDocument( &doc );
			for ( int i = 0; i < mode; ++i ) view.cycleClipMode();
			QMouseEvent down( QEvent::MouseButtonPress, QPointF( 84,100 ), Qt::LeftButton, Qt::LeftButton, Qt::NoModifier );
			QMouseEvent move( QEvent::MouseMove, QPointF( 116,100 ), Qt::NoButton, Qt::LeftButton, Qt::NoModifier );
			QMouseEvent up( QEvent::MouseButtonRelease, QPointF( 116,100 ), Qt::LeftButton, Qt::NoButton, Qt::NoModifier );
			QCoreApplication::sendEvent( &view, &down ); QCoreApplication::sendEvent( &view, &move ); QCoreApplication::sendEvent( &view, &up );
			clipInteractionOk = clipInteractionOk && doc.Map().solids.size() == 1;
			QKeyEvent enter( QEvent::KeyPress, Qt::Key_Return, Qt::NoModifier ); QCoreApplication::sendEvent( &view, &enter );
			const int vertical = projection == 0 ? 1 : 2;
			const int parity = projection == 1 ? -1 : 1;
			const int keep = mode == 0 ? -1 : mode == 1 ? 1 : 0;
			clipInteractionOk = clipInteractionOk && doc.Map().solids.size() == ( keep == 0 ? 2 : 1 );
			if ( keep != 0 && !doc.Map().solids.isEmpty() )
			{
				const SolidLite &result = doc.Map().solids.first();
				clipInteractionOk = clipInteractionOk && ( keep * parity > 0 ? qAbs( result.mins[vertical] ) < 1e-6 : qAbs( result.maxs[vertical] ) < 1e-6 );
			}
			clipInteractionOk = clipInteractionOk && doc.Undo() && doc.Map().solids.size() == 1 && doc.Map().SolidById( id );
		}
		CHammerDocument doc; doc.NewDocument(); const double lo[3] = { -64,-64,-64 }, hi[3] = { 64,64,64 };
		const int id = doc.CreateBrush( lo, hi ); doc.SelectObject( id ); doc.SetTool( TOOL_CLIP );
		CQtViewport2D view( CQtViewport2D::VIEWAXIS_TOP ); view.resize( 200,200 ); view.setDocument( &doc );
		QMouseEvent down( QEvent::MouseButtonPress, QPointF( 84,100 ), Qt::LeftButton, Qt::LeftButton, Qt::NoModifier );
		QMouseEvent up( QEvent::MouseButtonRelease, QPointF( 116,100 ), Qt::LeftButton, Qt::NoButton, Qt::NoModifier );
		QCoreApplication::sendEvent( &view, &down ); QCoreApplication::sendEvent( &view, &up );
		QKeyEvent escape( QEvent::KeyPress, Qt::Key_Escape, Qt::NoModifier ); QCoreApplication::sendEvent( &view, &escape );
		QKeyEvent enter( QEvent::KeyPress, Qt::Key_Return, Qt::NoModifier ); QCoreApplication::sendEvent( &view, &enter );
		clipInteractionOk = clipInteractionOk && doc.Map().solids.size() == 1 && doc.Map().SolidById( id )->sides.size() == 6;
		QCoreApplication::sendEvent( &view, &down ); QCoreApplication::sendEvent( &view, &up );
		QEvent lostFocus( QEvent::FocusOut ); QCoreApplication::sendEvent( &view, &lostFocus ); QCoreApplication::sendEvent( &view, &enter );
		clipInteractionOk = clipInteractionOk && doc.Map().solids.size() == 1 && doc.Map().SolidById( id )->sides.size() == 6;
		QCoreApplication::sendEvent( &view, &down ); QCoreApplication::sendEvent( &view, &up );
		doc.SetTool( TOOL_SELECT ); doc.SetTool( TOOL_CLIP ); QCoreApplication::sendEvent( &view, &enter );
		clipInteractionOk = clipInteractionOk && doc.Map().solids.size() == 1 && doc.Map().SolidById( id )->sides.size() == 6;
		const double origin[3] = { 0,0,0 }; const int point = doc.PlaceEntity( QStringLiteral( "light" ), origin );
		doc.SelectObjects( QSet<int>{ id, point } );
		QCoreApplication::sendEvent( &view, &down ); QCoreApplication::sendEvent( &view, &up ); QCoreApplication::sendEvent( &view, &enter );
		clipInteractionOk = clipInteractionOk && doc.Map().solids.size() == 1 && doc.Map().SolidById( id )->sides.size() == 6 && doc.Map().EntityById( point );
		ctx.Step( "viewport-interactive-clipping", clipInteractionOk,
			QStringLiteral( "XY/XZ/YZ line, all modes, preview cancellation, mixed rejection and undo" ) );
	}
	// Handle edits only change the preview until Enter; Ctrl preserves the line
	// direction and Shift on blank space replaces it, as the original clipper.
	{
		bool clipHandlesOk = true;
		for ( int scenario = 0; scenario < 7; ++scenario )
		{
			CHammerDocument doc; doc.NewDocument();
			const double lo[3] = { -64,-64,-64 }, hi[3] = { 64,64,64 };
			const int id = doc.CreateBrush( lo, hi ); doc.SelectObject( id ); doc.SetTool( TOOL_CLIP );
			CQtViewport2D view( CQtViewport2D::VIEWAXIS_TOP ); view.resize( 200,200 ); view.setDocument( &doc );
			auto dragClip = [&]( QPointF from, QPointF to, Qt::KeyboardModifiers modifiers, bool checkDuring ) {
				QMouseEvent down( QEvent::MouseButtonPress, from, Qt::LeftButton, Qt::LeftButton, modifiers );
				QMouseEvent move( QEvent::MouseMove, to, Qt::NoButton, Qt::LeftButton, modifiers );
				QMouseEvent up( QEvent::MouseButtonRelease, to, Qt::LeftButton, Qt::NoButton, modifiers );
				QCoreApplication::sendEvent( &view, &down ); QCoreApplication::sendEvent( &view, &move );
				if ( checkDuring )
				{
					QKeyEvent prematureEnter( QEvent::KeyPress, Qt::Key_Return, Qt::NoModifier );
					QCoreApplication::sendEvent( &view, &prematureEnter );
					clipHandlesOk = clipHandlesOk && doc.Map().solids.size() == 1 && doc.Map().SolidById( id )->sides.size() == 6;
				}
				QCoreApplication::sendEvent( &view, &up );
			};
			dragClip( QPointF(84,100), QPointF(116,100), Qt::NoModifier, false );
			switch ( scenario )
			{
				case 0: dragClip( QPointF(84,100), QPointF(84,92), Qt::NoModifier, true ); break;
				case 1: dragClip( QPointF(84,100), QPointF(84,92), Qt::ControlModifier, true ); break;
				case 2: dragClip( QPointF(100,80), QPointF(100,72), Qt::NoModifier, false ); break;
				case 3: dragClip( QPointF(80,92), QPointF(120,92), Qt::ShiftModifier, false ); break;
				case 4: dragClip( QPointF(84,100), QPointF(116,100), Qt::NoModifier, false ); break;
				case 5: dragClip( QPointF(84,100), QPointF(132,100), Qt::NoModifier, false ); break;
				case 6: dragClip( QPointF(84,91), QPointF(84,83), Qt::NoModifier, false ); break;
			}
			clipHandlesOk = clipHandlesOk && doc.Map().solids.size() == 1 && doc.Map().SolidById( id )->sides.size() == 6;
			QKeyEvent enter( QEvent::KeyPress, Qt::Key_Return, Qt::NoModifier ); QCoreApplication::sendEvent( &view, &enter );
			const SolidLite *result = doc.Map().SolidById( id );
			clipHandlesOk = clipHandlesOk && result && doc.Map().solids.size() == 1;
			if ( result && scenario == 0 )
			{
				QList<HammerWinding::Polygon> polygons;
				const bool valid = HammerWinding::BuildFaces( *result, polygons );
				bool cutPlaneFound = false;
				for ( const auto &polygon : polygons ) for ( const auto &vertex : polygon )
				{
					const double distance = vertex[0] + 4 * vertex[1] - 32;
					clipHandlesOk = clipHandlesOk && distance <= 1e-4;
					cutPlaneFound = cutPlaneFound || qAbs( distance ) < 1e-4;
				}
				clipHandlesOk = clipHandlesOk && valid && cutPlaneFound;
			}
			else if ( result && scenario == 5 ) clipHandlesOk = clipHandlesOk && qAbs( result->mins[1] ) < 1e-6;
			else if ( result ) clipHandlesOk = clipHandlesOk && qAbs( result->maxs[1] - ( scenario == 1 || scenario == 3 ? 16.0 : 0.0 ) ) < 1e-6;
			clipHandlesOk = clipHandlesOk && doc.Undo() && doc.Map().SolidById( id ) && doc.Map().SolidById( id )->sides.size() == 6;
		}
		ctx.Step( "viewport-clipping-handles", clipHandlesOk,
			QStringLiteral( "endpoint, Ctrl move, Shift redraw, hit radius, plane reversal, collapse and undo" ) );
	}

	// Dialog edits are staged; accepting a batch is one undoable change and
	// absent FGD properties must not be materialized by simply opening it.
	CHammerDocument worldDoc;
	worldDoc.NewDocument();
	const QMap<QString, QString> worldBefore = worldDoc.Map().worldspawn;
	QMap<QString, QString> worldSchema;
	worldSchema.insert( QStringLiteral( "skyname" ), QStringLiteral( "SkyBox Texture Name" ) );
	worldSchema.insert( QStringLiteral( "message" ), QStringLiteral( "Map Description" ) );
	CMapPropertiesDialog worldDialog( worldBefore, worldSchema );
	QTableWidget *worldTable = worldDialog.findChild<QTableWidget *>( QStringLiteral( "worldProperties" ) );
	QLineEdit *worldFilter = worldDialog.findChild<QLineEdit *>( QStringLiteral( "worldPropertyFilter" ) );
	QCheckBox *worldRawKeys = worldDialog.findChild<QCheckBox *>( QStringLiteral( "worldPropertyRawKeys" ) );
	bool bWorldProperties = worldTable && worldFilter && worldRawKeys && worldDialog.Properties() == worldBefore;
	if ( worldTable )
	{
		for ( int row = 0; row < worldTable->rowCount(); ++row )
		{
			const QString key = worldTable->item( row, 0 )->data( Qt::UserRole ).toString();
			if ( key == QLatin1String( "skyname" ) )
			{
				bWorldProperties = bWorldProperties && worldTable->item( row, 0 )->text() == QLatin1String( "SkyBox Texture Name" );
				worldTable->item( row, 1 )->setText( QStringLiteral( "sky_day02_01" ) );
			}
			if ( key == QLatin1String( "message" ) )
				worldTable->item( row, 1 )->setText( QStringLiteral( "A map" ) );
		}
		if ( worldFilter && worldRawKeys )
		{
			worldFilter->setText( QStringLiteral( "SKYNAME" ) );
			for ( int row = 0; row < worldTable->rowCount(); ++row )
				bWorldProperties = bWorldProperties && ( worldTable->isRowHidden( row ) !=
					( worldTable->item( row, 0 )->data( Qt::UserRole ).toString() == QLatin1String( "skyname" ) ) );
			worldRawKeys->setChecked( true );
			for ( int row = 0; row < worldTable->rowCount(); ++row )
				bWorldProperties = bWorldProperties && worldTable->item( row, 0 )->text() == worldTable->item( row, 0 )->data( Qt::UserRole ).toString();
			worldFilter->clear();
		}
	}
	const QMap<QString, QString> worldEdited = worldDialog.Properties();
	worldDialog.reject();
	bWorldProperties = bWorldProperties && worldDoc.Map().worldspawn == worldBefore &&
		worldDoc.SetWorldProperties( worldEdited ) && worldDoc.IsDirty() &&
		worldDoc.Map().WorldspawnValue( "skyname" ) == QLatin1String( "sky_day02_01" ) &&
		worldDoc.Undo() && worldDoc.Map().worldspawn == worldBefore && !worldDoc.IsDirty() &&
		worldDoc.Redo() && worldDoc.Map().worldspawn == worldEdited;
	const QString worldPath = guardDir.filePath( "world_properties.vmf" );
	MapDocLite worldReloaded;
	QString worldError;
	bWorldProperties = bWorldProperties && worldDoc.SaveToFile( worldPath ) &&
		VmfIO::LoadMap( worldPath, worldReloaded, &worldError ) && worldReloaded.worldspawn == worldEdited;
	QMap<QString, QString> badWorld = worldEdited;
	badWorld[QStringLiteral( "classname" )] = QStringLiteral( "other" );
	bWorldProperties = bWorldProperties && !worldDoc.SetWorldProperties( badWorld ) &&
		worldDoc.Map().worldspawn == worldEdited;
	ctx.Step( "map-properties", bWorldProperties, QStringLiteral( "staged changes, undo/redo, save/reload, reserved keys" ) );

	// Worldspawn choices use the same value-preserving editor as entity properties.
	QFile worldFgdFile( guardDir.filePath( QStringLiteral( "world_choices.fgd" ) ) );
	bool worldChoicesOk = worldFgdFile.open( QIODevice::WriteOnly );
	if ( worldChoicesOk )
	{
		worldFgdFile.write( "@PointClass = worldspawn : \"World\" [ mode(choices) : \"Mode\" : 0 = [ 0 : \"Same\" 1 : \"Same\" ] options(flags) = [ 1 : \"Enabled\" : 0 2 : \"Extra\" : 0 ] ]\n" );
		worldFgdFile.close();
	}
	CFgdData worldFgd;
	worldChoicesOk = worldChoicesOk && worldFgd.Load( worldFgdFile.fileName() );
	QMap<QString, QString> unknownWorld = worldBefore;
	unknownWorld.insert( QStringLiteral( "mode" ), QStringLiteral( "custom" ) );
	unknownWorld.insert( QStringLiteral( "options" ), QStringLiteral( "8" ) );
	CMapPropertiesDialog choiceWorldDialog( unknownWorld, worldFgd.ClassPropertyNames( QStringLiteral( "worldspawn" ) ), nullptr, &worldFgd );
	QTableWidget *choiceWorldTable = choiceWorldDialog.findChild<QTableWidget *>( QStringLiteral( "worldProperties" ) );
	int choiceWorldRow = -1, flagsWorldRow = -1;
	if ( choiceWorldTable )
		for ( int row = 0; row < choiceWorldTable->rowCount(); ++row )
		{
			const QString key = choiceWorldTable->item( row, 0 )->data( Qt::UserRole ).toString();
			if ( key == QLatin1String( "mode" ) ) choiceWorldRow = row;
			if ( key == QLatin1String( "options" ) ) flagsWorldRow = row;
		}
	worldChoicesOk = worldChoicesOk && choiceWorldTable && choiceWorldRow >= 0 && choiceWorldDialog.Properties() == unknownWorld;
	if ( choiceWorldRow >= 0 )
	{
		const QModelIndex index = choiceWorldTable->model()->index( choiceWorldRow, 1 );
		QAbstractItemDelegate *delegate = choiceWorldTable->itemDelegateForColumn( 1 );
		QScopedPointer<QWidget> editor( delegate->createEditor( choiceWorldTable, QStyleOptionViewItem(), index ) );
		QComboBox *combo = qobject_cast<QComboBox *>( editor.data() );
		worldChoicesOk = worldChoicesOk && combo && combo->currentData().toString() == QLatin1String( "custom" );
		if ( combo )
		{
			delegate->setEditorData( combo, index );
			combo->setCurrentIndex( combo->findData( QStringLiteral( "1" ) ) );
			delegate->setModelData( combo, choiceWorldTable->model(), index );
			worldChoicesOk = worldChoicesOk && choiceWorldDialog.Properties().value( QStringLiteral( "mode" ) ) == QLatin1String( "1" );
		}
	}
	worldChoicesOk = worldChoicesOk && flagsWorldRow >= 0;
	if ( flagsWorldRow >= 0 )
	{
		const QModelIndex index = choiceWorldTable->model()->index( flagsWorldRow, 1 );
		QAbstractItemDelegate *delegate = choiceWorldTable->itemDelegateForColumn( 1 );
		QScopedPointer<QWidget> editor( delegate->createEditor( choiceWorldTable, QStyleOptionViewItem(), index ) );
		CPropertyFlagsEditor *flags = dynamic_cast<CPropertyFlagsEditor *>( editor.data() );
		worldChoicesOk = worldChoicesOk && flags && flags->count() == 2 && flags->bits() == 8;
		if ( flags )
		{
			QMetaObject::invokeMethod( flags, "activated", Qt::DirectConnection, Q_ARG( int, 0 ) );
			delegate->setModelData( flags, choiceWorldTable->model(), index );
			worldChoicesOk = worldChoicesOk && choiceWorldDialog.Properties().value( QStringLiteral( "options" ) ) == QLatin1String( "9" );
		}
	}
	worldChoicesOk = worldChoicesOk && worldDoc.SetWorldProperties( choiceWorldDialog.Properties() ) &&
		worldDoc.Undo() && worldDoc.Map().worldspawn == worldEdited;
	ctx.Step( "map-property-choices", worldChoicesOk, QStringLiteral( "choices and flags preserve unknown values, staged writeback and undo" ) );

	// The report filters point entities case-insensitively and selects the shown IDs.
	CHammerDocument reportDoc;
	reportDoc.NewDocument();
	EntityLite reportA; reportA.id = reportDoc.Map().AllocId(); reportA.classname = QStringLiteral( "light" );
	reportA.keyvalues.insert( QStringLiteral( "targetname" ), QStringLiteral( "Hall_Light" ) );
	EntityLite reportB; reportB.id = reportDoc.Map().AllocId(); reportB.classname = QStringLiteral( "info_player_start" );
	reportDoc.Map().entities << reportA << reportB;
	CEntityReportDialog report( &reportDoc );
	QTableWidget *reportTable = report.findChild<QTableWidget *>( QStringLiteral( "entityReportTable" ) );
	QLineEdit *reportClass = report.findChild<QLineEdit *>( QStringLiteral( "entityReportClass" ) );
	QLineEdit *reportKey = report.findChild<QLineEdit *>( QStringLiteral( "entityReportKey" ) );
	QLineEdit *reportValue = report.findChild<QLineEdit *>( QStringLiteral( "entityReportValue" ) );
	QCheckBox *reportUseKey = report.findChild<QCheckBox *>( QStringLiteral( "entityReportKeyFilter" ) );
	QCheckBox *reportExact = report.findChild<QCheckBox *>( QStringLiteral( "entityReportExact" ) );
	bool bReport = reportTable && reportClass && reportKey && reportValue && reportUseKey && reportExact && reportTable->rowCount() == 2;
	if ( bReport )
	{
		reportClass->setText( QStringLiteral( "LIGHT" ) );
		bReport = reportTable->rowCount() == 1;
		reportTable->selectRow( 0 );
		bReport = bReport && report.SelectedEntityIds() == ( QSet<int>() << reportA.id );
		reportDoc.SelectObjects( report.SelectedEntityIds() );
		bReport = bReport && reportDoc.IsSelected( reportA.id ) && !reportDoc.IsSelected( reportB.id );
		reportClass->clear();
		reportUseKey->setChecked( true );
		reportKey->setText( QStringLiteral( "TARGETNAME" ) );
		reportValue->setText( QStringLiteral( "hall" ) );
		bReport = bReport && reportTable->rowCount() == 1;
		reportExact->setChecked( true );
		bReport = bReport && reportTable->rowCount() == 0;
		reportValue->setText( QStringLiteral( "HALL_LIGHT" ) );
		bReport = bReport && reportTable->rowCount() == 1;
	}
	ctx.Step( "entity-report", bReport, QStringLiteral( "class filter and selected IDs" ) );
	// Sorting must keep IDs attached to their rows through a filter refresh.
	CEntityReportDialog reportSorting( &reportDoc );
	QTableWidget *sortedTable = reportSorting.findChild<QTableWidget *>( QStringLiteral( "entityReportTable" ) );
	QLineEdit *sortedClass = reportSorting.findChild<QLineEdit *>( QStringLiteral( "entityReportClass" ) );
	QLabel *sortedCount = reportSorting.findChild<QLabel *>( QStringLiteral( "entityReportCount" ) );
	bool reportSortingOk = sortedTable && sortedClass && sortedClass->completer() && sortedCount && sortedTable->isSortingEnabled();
	if ( reportSortingOk )
	{
		sortedTable->sortItems( 0, Qt::DescendingOrder );
		int lightRow = sortedTable->item( 0, 0 )->data( Qt::UserRole ).toInt() == reportA.id ? 0 : 1;
		sortedTable->selectRow( lightRow );
		sortedClass->setText( QStringLiteral( "light" ) );
		reportSortingOk = sortedTable->rowCount() == 1 && reportSorting.SelectedEntityIds() == ( QSet<int>() << reportA.id ) &&
			sortedCount->text().contains( QStringLiteral( "1" ) ) && sortedCount->text().contains( QStringLiteral( "2" ) );
		sortedClass->clear();
		reportSortingOk = reportSortingOk && sortedTable->rowCount() == 2 && reportSorting.SelectedEntityIds() == ( QSet<int>() << reportA.id );
	}
	ctx.Step( "entity-report-sort", reportSortingOk, QStringLiteral( "sorting, count and selection after filtering" ) );
	// Properties returns all selected IDs without mutating or dirtying the map.
	CEntityReportDialog reportProperties( &reportDoc );
	QTableWidget *propertiesTable = reportProperties.findChild<QTableWidget *>( QStringLiteral( "entityReportTable" ) );
	QPushButton *propertiesButton = reportProperties.findChild<QPushButton *>( QStringLiteral( "entityReportProperties" ) );
	bool bReportProperties = propertiesTable && propertiesButton && !propertiesButton->isEnabled() && !reportDoc.IsDirty();
	if ( bReportProperties )
	{
		propertiesTable->selectRow( 0 );
		propertiesTable->selectionModel()->select( propertiesTable->model()->index( 1, 0 ),
			QItemSelectionModel::Select | QItemSelectionModel::Rows );
		bReportProperties = propertiesButton->isEnabled() && reportProperties.SelectedEntityIds().size() == 2;
		propertiesButton->click();
		bReportProperties = bReportProperties && reportProperties.result() == CEntityReportDialog::PropertiesResult &&
			reportProperties.SelectedEntityIds() == ( QSet<int>() << reportA.id << reportB.id ) && !reportDoc.IsDirty();
	}
	ctx.Step( "entity-report-properties", bReportProperties, QStringLiteral( "selection and property action preserve map" ) );
	int modelessAction = QDialog::Rejected;
	QSet<int> modelessIds;
	CEntityReportDialog modelessReport( &reportDoc, nullptr, [&]( int action, const QSet<int> &ids ) {
		modelessAction = action;
		modelessIds = ids;
	} );
	modelessReport.show();
	auto *modelessTable = modelessReport.findChild<QTableWidget *>( QStringLiteral( "entityReportTable" ) );
	auto *modelessGoTo = modelessReport.findChild<QPushButton *>( QStringLiteral( "entityReportGoTo" ) );
	auto *modelessProperties = modelessReport.findChild<QPushButton *>( QStringLiteral( "entityReportProperties" ) );
	bool modelessOk = modelessTable && modelessGoTo && modelessProperties && modelessTable->rowCount() == 2;
	if ( modelessOk )
	{
		modelessTable->selectRow( 0 );
		const QSet<int> expected = modelessReport.SelectedEntityIds();
		modelessGoTo->click();
		modelessOk = modelessReport.isVisible() && !modelessReport.isModal() && !expected.isEmpty() &&
			modelessAction == CEntityReportDialog::GoToResult && modelessIds == expected;
		modelessProperties->click();
		modelessOk = modelessOk && modelessReport.isVisible() &&
			modelessAction == CEntityReportDialog::PropertiesResult && modelessIds == expected;
	}
	modelessReport.close();
	ctx.Step( "entity-report-modeless", modelessOk, QStringLiteral( "Go To and Properties execute without closing the report" ) );
	// Go To returns the chosen IDs; deleting uses one undoable document operation.
	bool bReportActions = false;
	QString reportDiagnostic;
	QPushButton *goTo = report.findChild<QPushButton *>( QStringLiteral( "entityReportGoTo" ) );
	QPushButton *remove = report.findChild<QPushButton *>( QStringLiteral( "entityReportDelete" ) );
	if ( reportTable && goTo && remove && reportTable->rowCount() == 1 )
	{
		reportTable->selectRow( 0 );
		goTo->click();
		bReportActions = report.result() == CEntityReportDialog::GoToResult &&
			report.SelectedEntityIds() == ( QSet<int>() << reportA.id );
		QTimer::singleShot( 0, [] {
			if ( QMessageBox *box = qobject_cast<QMessageBox *>( QApplication::activeModalWidget() ) )
				box->button( QMessageBox::Yes )->click();
		} );
		const bool before = bReportActions;
		const bool enabled = remove->isEnabled();
		remove->click();
		const int afterDelete = reportDoc.Map().entities.size();
		const bool undone = reportDoc.Undo();
		const int afterUndo = reportDoc.Map().entities.size();
		reportDiagnostic = QStringLiteral( "go=%1 enabled=%2 delete=%3 undo=%4 size=%5" ).arg(before).arg(enabled).arg(afterDelete).arg(undone).arg(afterUndo);
		bReportActions = before && afterDelete == 1 && undone && afterUndo == 2;
	}
	ctx.Step( "entity-report-actions", bReportActions, reportDiagnostic );

	// Report filtering can scope the list to current map selection, or find an
	// existing key even when its value is empty. It never changes selection.
	CHammerDocument scopedDoc;
	scopedDoc.NewDocument();
	EntityLite scopedA; scopedA.id = scopedDoc.Map().AllocId(); scopedA.classname = QStringLiteral( "light" );
	scopedA.keyvalues.insert( QStringLiteral( "TargetName" ), QStringLiteral( "UpperCaseKey" ) );
	scopedA.keyvalues.insert( QStringLiteral( "custom_key" ), QString() );
	EntityLite scopedB; scopedB.id = scopedDoc.Map().AllocId(); scopedB.classname = QStringLiteral( "info_target" );
	scopedB.keyvalues.insert( QStringLiteral( "custom_key" ), QStringLiteral( "value" ) );
	scopedDoc.Map().entities << scopedA << scopedB;
	scopedDoc.SelectObjects( QSet<int>{ scopedA.id } );
	CEntityReportDialog scopedReport( &scopedDoc );
	QTableWidget *scopedTable = scopedReport.findChild<QTableWidget *>( QStringLiteral( "entityReportTable" ) );
	QCheckBox *scopedOnly = scopedReport.findChild<QCheckBox *>( QStringLiteral( "entityReportSelectedOnly" ) );
	QCheckBox *scopedUseKey = scopedReport.findChild<QCheckBox *>( QStringLiteral( "entityReportKeyFilter" ) );
	QCheckBox *scopedExact = scopedReport.findChild<QCheckBox *>( QStringLiteral( "entityReportExact" ) );
	QLineEdit *scopedKey = scopedReport.findChild<QLineEdit *>( QStringLiteral( "entityReportKey" ) );
	bool scopedReportOk = scopedTable && scopedOnly && scopedUseKey && scopedExact && scopedKey;
	if ( scopedReportOk )
	{
		scopedOnly->setChecked( true );
		scopedReportOk = scopedTable->rowCount() == 1 && scopedTable->item( 0, 1 )->text() == QLatin1String( "UpperCaseKey" );
		scopedTable->selectRow( 0 );
		scopedReportOk = scopedReportOk && scopedReport.SelectedEntityIds() == QSet<int>{ scopedA.id };
		scopedDoc.SelectObjects( QSet<int>{ scopedB.id } );
		scopedReportOk = scopedReportOk && scopedTable->rowCount() == 1 &&
			scopedTable->item( 0, 0 )->data( Qt::UserRole ).toInt() == scopedB.id;
		scopedOnly->setChecked( false );
		scopedKey->setText( QStringLiteral( "CUSTOM_KEY" ) );
		scopedUseKey->setChecked( true );
		scopedReportOk = scopedReportOk && scopedTable->rowCount() == 2;
		scopedExact->setChecked( true );
		scopedReportOk = scopedReportOk && scopedTable->rowCount() == 1 &&
			scopedTable->item( 0, 0 )->data( Qt::UserRole ).toInt() == scopedA.id &&
			scopedDoc.SelectedIds() == QSet<int>{ scopedB.id } && !scopedDoc.IsDirty();
	}
	ctx.Step( "entity-report-scoped-filter", scopedReportOk,
		QStringLiteral( "selected-only, live selection, case-insensitive name and empty property values" ) );

	// Entity type follows actual brush ownership, including unknown FGD classes.
	CHammerDocument typedReportDoc;
	typedReportDoc.NewDocument();
	EntityLite pointType; pointType.id = typedReportDoc.Map().AllocId(); pointType.classname = QStringLiteral( "info_target" );
	EntityLite brushType; brushType.id = typedReportDoc.Map().AllocId(); brushType.classname = QStringLiteral( "custom_brush" );
	typedReportDoc.Map().entities << pointType << brushType;
	const double reportLo[3] = { 0, 0, 0 }, reportHi[3] = { 32, 32, 32 };
	const int reportBrushId = CreateBoxSolid( typedReportDoc.Map(), reportLo, reportHi );
	typedReportDoc.Map().SolidById( reportBrushId )->ownerEntityId = brushType.id;
	CEntityReportDialog typedReport( &typedReportDoc );
	QTableWidget *typedTable = typedReport.findChild<QTableWidget *>( QStringLiteral( "entityReportTable" ) );
	QComboBox *typeFilter = typedReport.findChild<QComboBox *>( QStringLiteral( "entityReportType" ) );
	QLineEdit *typedClass = typedReport.findChild<QLineEdit *>( QStringLiteral( "entityReportClass" ) );
	bool typedReportOk = typedTable && typeFilter && typedClass && typedTable->rowCount() == 2;
	if ( typedReportOk )
	{
		typeFilter->setCurrentIndex( typeFilter->findData( 1 ) );
		typedReportOk = typedTable->rowCount() == 1 && typedTable->item( 0, 0 )->data( Qt::UserRole ).toInt() == pointType.id;
		typedTable->selectRow( 0 );
		typeFilter->setCurrentIndex( typeFilter->findData( 2 ) );
		typedReportOk = typedReportOk && typedTable->rowCount() == 1 && typedTable->item( 0, 0 )->data( Qt::UserRole ).toInt() == brushType.id && typedReport.SelectedEntityIds().isEmpty();
		typedClass->setText( QStringLiteral( "info" ) );
		typedReportOk = typedReportOk && typedTable->rowCount() == 0;
		typedClass->clear();
		typeFilter->setCurrentIndex( typeFilter->findData( 0 ) );
		typedReportOk = typedReportOk && typedTable->rowCount() == 2 && !typedReportDoc.IsDirty();
	}
	ctx.Step( "entity-report-type-filter", typedReportOk, QStringLiteral( "point/brush ownership, combined filters and selection safety" ) );


	// A pointfile is transient view state: a bad load must preserve the old path,
	// unloading and opening a new document must not dirty the map.
	const QString pointPath = guardDir.filePath( QString::fromUtf8( "leak_测试.lin" ) );
	QFile pointFile( pointPath );
	bool bPointFile = pointFile.open( QIODevice::WriteOnly | QIODevice::Text );
	if ( bPointFile ) { pointFile.write( "-64 0 0\n64 0 0\n" ); pointFile.close(); }
	CHammerDocument pointDoc;
	pointDoc.NewDocument();
	int pointError = 0;
	bPointFile = bPointFile && pointDoc.LoadPointFile( pointPath, &pointError ) &&
		pointDoc.PointFilePoints().size() == 2 && !pointDoc.IsDirty();
	PointFilePoint stepped{};
	bPointFile = bPointFile && pointDoc.PointFileIndex() == -1 && pointDoc.StepPointFile( 1, stepped ) && stepped.x == -64 &&
		pointDoc.StepPointFile( 1, stepped ) && stepped.x == 64 && pointDoc.StepPointFile( 1, stepped ) && stepped.x == -64 &&
		pointDoc.StepPointFile( -1, stepped ) && stepped.x == 64 && !pointDoc.IsDirty();
	CQtViewport2D pointView( CQtViewport2D::VIEWAXIS_TOP );
	pointView.setDocument( &pointDoc );
	pointView.resize( 200, 160 );
	QImage pointImage( pointView.size(), QImage::Format_ARGB32 );
	pointView.render( &pointImage );
	bPointFile = bPointFile && pointImage.pixelColor( 100, 80 ) == QColor( 255, 0, 0 );
	CQtViewport3D point3D;
	point3D.setDocument( &pointDoc );
	point3D.resize( 320, 240 );
	QImage point3DImage( point3D.size(), QImage::Format_ARGB32 );
	point3D.render( &point3DImage );
	bool sawRed = false;
	for ( int y = 0; y < point3DImage.height() && !sawRed; ++y )
		for ( int x = 0; x < point3DImage.width(); ++x )
			if ( point3DImage.pixelColor( x, y ) == QColor( 255, 0, 0 ) ) { sawRed = true; break; }
	bPointFile = bPointFile && sawRed;
	if ( pointFile.open( QIODevice::WriteOnly | QIODevice::Truncate | QIODevice::Text ) )
	{ pointFile.write( "1 2 3\ninvalid\n" ); pointFile.close(); }
	else bPointFile = false;
	bPointFile = bPointFile && !pointDoc.LoadPointFile( pointPath, &pointError ) && pointError == 2 &&
		pointDoc.PointFilePoints().size() == 2 && !pointDoc.IsDirty();
	// A truncated or empty compiler output must not clear a valid leak path.
	if ( pointFile.open( QIODevice::WriteOnly | QIODevice::Truncate | QIODevice::Text ) )
	{
		pointFile.write( " \n\t\n" );
		pointFile.close();
	}
	else bPointFile = false;
	bPointFile = bPointFile && !pointDoc.LoadPointFile( pointPath, &pointError ) && pointError == 0 &&
		pointDoc.PointFilePoints().size() == 2 && pointDoc.PointFilePoints().first().x == -64 &&
		pointDoc.PointFileIndex() == 1 && !pointDoc.IsDirty();
	pointDoc.UnloadPointFile();
	bPointFile = bPointFile && pointDoc.PointFilePoints().isEmpty() && pointDoc.PointFileIndex() == -1 &&
		!pointDoc.StepPointFile( 1, stepped ) && !pointDoc.IsDirty();
	ctx.Step( "pointfile", bPointFile, QStringLiteral( "Unicode path, 2D/3D render, navigation, invalid load and unload" ) );
	PointFileState missing, oldFile, freshFile;
	oldFile.exists = oldFile.readable = true;
	oldFile.modified = QDateTime::fromMSecsSinceEpoch( 1000 );
	oldFile.digest = QByteArray( "old" );
	freshFile = oldFile;
	bool freshPointFile = IsFreshPointFile( missing, freshFile ) &&
		!IsFreshPointFile( oldFile, freshFile );
	freshFile.digest = QByteArray( "new" );
	freshPointFile = freshPointFile && IsFreshPointFile( oldFile, freshFile );
	freshFile.readable = false;
	freshPointFile = freshPointFile && !IsFreshPointFile( oldFile, freshFile );
	ctx.Step( "pointfile-freshness", freshPointFile, QStringLiteral( "Ignore unchanged or unreadable old compiler output" ) );

	// 3D flat mode fills supported box faces; switching back restores wireframe.
	CHammerDocument flatDoc;
	flatDoc.NewDocument();
	const double flatMins[3] = { -128, -128, -128 }, flatMaxs[3] = { 128, 128, 128 };
	CreateBoxSolid( flatDoc.Map(), flatMins, flatMaxs );
	CQtViewport3D flatView;
	flatView.setDocument( &flatDoc );
	flatView.resize( 480, 320 );
	QImage flatImage( flatView.size(), QImage::Format_ARGB32 );
	flatView.render( &flatImage );
	flatView.setDisplayMode( CQtViewport3D::Wireframe );
	QImage wireImage( flatView.size(), QImage::Format_ARGB32 );
	flatView.render( &wireImage );
	int flatPixels = 0, wirePixels = 0;
	for ( int y = 0; y < flatImage.height(); ++y )
		for ( int x = 0; x < flatImage.width(); ++x )
		{
			const auto isFaceColor = []( const QColor &c ) {
				return c.blue() == c.red() + 5 && c.green() == c.red() && c.red() >= 70 && c.red() <= 210;
			};
			if ( isFaceColor( flatImage.pixelColor( x, y ) ) ) ++flatPixels;
			if ( isFaceColor( wireImage.pixelColor( x, y ) ) ) ++wirePixels;
		}
	ctx.Step( "3d-display-modes", flatPixels > 100 && wirePixels == 0 &&
		flatView.displayMode() == CQtViewport3D::Wireframe, QStringLiteral( "flat pixels %1, wire pixels %2" ).arg( flatPixels ).arg( wirePixels ) );
	if ( QGuiApplication::platformName() == QLatin1String( "offscreen" ) )
	{
		CQtViewport3D fallbackView;
		fallbackView.setDocument( &flatDoc );
		fallbackView.resize( 400, 300 );
		fallbackView.show();
		fallbackView.setDisplayMode( CQtViewport3D::Textured );
		QCoreApplication::processEvents();
		QCoreApplication::processEvents();
		ctx.Step( "3d-textured-no-gl-fallback", fallbackView.displayMode() == CQtViewport3D::Flat,
			QStringLiteral( "unavailable OpenGL surface returns to software flat mode" ) );
	}
	const QString textureGame = qEnvironmentVariable( "HAMMER_SELFTEST_MATERIAL_GAME" );
	if ( !textureGame.isEmpty() )
	{
		CHammerDocument textureDoc;
		textureDoc.NewDocument();
		const double textureMins[3] = { -96, -96, -96 }, textureMaxs[3] = { 96, 96, 96 };
		CreateBoxSolid( textureDoc.Map(), textureMins, textureMaxs );
		for ( SideLite &side : textureDoc.Map().solids.last().sides )
			side.texture = QStringLiteral( "dev/dev_measuregeneric01" );
		textureDoc.SelectObjects( {} );
		CQtViewport3D textureView;
		textureView.setDocument( &textureDoc );
		textureView.setMaterialGameDirectory( textureGame );
		textureView.resize( 640, 480 );
		textureView.setDisplayMode( CQtViewport3D::Textured );
		const double textureEye[3] = { 320, 0, 0 }, textureAngles[3] = { 0, 180, 0 };
		textureView.setCameraPositionAngles( textureEye, textureAngles );
		textureView.show();
		QCoreApplication::processEvents();
		QOpenGLWidget *surface = textureView.findChild<QOpenGLWidget *>();
		ctx.Step( "3d-textured-input-transparency", surface && surface->testAttribute( Qt::WA_TransparentForMouseEvents ) &&
			surface->focusPolicy() == Qt::NoFocus,
			QStringLiteral( "OpenGL child passes pointer and focus to the parent viewport" ) );
		const QImage texturedImage = textureView.texturedFramebuffer();
		const bool loadedTexture = textureView.hasLoadedMaterialTexture( QStringLiteral( "dev/dev_measuregeneric01" ) );
		for ( SideLite &side : textureDoc.Map().solids.last().sides )
			side.texture = QStringLiteral( "hammer2_nonexistent_texture_fixture" );
		surface->update();
		QCoreApplication::processEvents();
		const QImage fallbackImage = textureView.texturedFramebuffer();
		const double projectionOrigin[3] = { 96, 0, 0 };
		const SolidLite &receiver = textureDoc.Map().solids.last();
		const int receiverId = receiver.id;
		int receiverSide = -1;
		for ( const SideLite &side : receiver.sides )
			if ( side.planePts[0][0] == 96 && side.planePts[1][0] == 96 && side.planePts[2][0] == 96 )
				receiverSide = side.id;
		textureDoc.SetActiveMaterial( QStringLiteral( "dev/dev_measuregeneric01" ) );
		QImage projectionBaseline = fallbackImage;
		const auto changedProjectionPixels = [&]( const QImage &projection ) {
			if ( projection.isNull() || projection.size() != projectionBaseline.size() ) return 0;
			int changed = 0;
			const QPoint center( projection.width() / 2, projection.height() / 2 );
			const int radius = qRound( 24 * textureView.devicePixelRatioF() );
			// Exclude the helper marker and text; these are painted above the GL pass.
			for ( int y = center.y() + radius / 2; y < center.y() + radius; ++y )
				for ( int x = center.x() - radius; x < center.x() + radius; ++x )
					if ( projection.pixel( x, y ) != projectionBaseline.pixel( x, y ) ) ++changed;
			return changed;
		};
		const int decal = textureDoc.PlaceDecal( projectionOrigin );
		textureDoc.SelectObjects( {} );
		const int decalPixels = changedProjectionPixels( textureView.texturedFramebuffer() );
		ViewFilter projectionFilter;
		projectionFilter.showEntities = false;
		textureDoc.SetViewFilter( projectionFilter );
		const int filteredPixels = changedProjectionPixels( textureView.texturedFramebuffer() );
		textureDoc.SetViewFilter( {} );
		textureDoc.Undo();
		const int overlay = textureDoc.PlaceOverlay( receiverId, receiverSide, projectionOrigin, 96, 96 );
		textureDoc.SelectObjects( {} );
		const int overlayPixels = changedProjectionPixels( textureView.texturedFramebuffer() );
		ctx.Step( "3d-projected-material-framebuffer",
			decal > 0 && overlay > 0 && decalPixels > 100 && overlayPixels > 100 && filteredPixels == 0,
			QStringLiteral( "decal pixels=%1; overlay pixels=%2; filtered pixels=%3" ).arg( decalPixels ).arg( overlayPixels ).arg( filteredPixels ) );
		const double terrainDecalOrigin[3] = { 136, 0, 0 };
		const bool raisedTerrain = textureDoc.Undo() &&
			textureDoc.CreateDisplacements( { { receiverId, { receiverSide } } }, 2 ) &&
			textureDoc.SetDisplacementDistances( receiverId, receiverSide, QList<double>( 25, 40.0 ) );
		textureDoc.SelectObjects( {} );
		projectionBaseline = textureView.texturedFramebuffer();
		const int terrainDecal = textureDoc.PlaceDecal( terrainDecalOrigin );
		textureDoc.SelectObjects( {} );
		const int terrainPixels = changedProjectionPixels( textureView.texturedFramebuffer() );
		ctx.Step( "3d-displacement-decal-framebuffer", raisedTerrain && terrainDecal > 0 && terrainPixels > 100,
			QStringLiteral( "raised terrain decal pixels=%1" ).arg( terrainPixels ) );
		if ( terrainDecal > 0 ) textureDoc.Undo();
		if ( raisedTerrain ) { textureDoc.Undo(); textureDoc.Undo(); }
		textureView.setDisplayMode( CQtViewport3D::Flat );
		QCoreApplication::processEvents();
		const QImage comparisonImage = textureView.grab().toImage();
		QSet<QRgb> colors;
		int changedPixels = 0, texturePixels = 0;
		const qreal pixelRatio = textureView.devicePixelRatioF();
		const QRect sampleRect( qRound( 250 * pixelRatio ), qRound( 170 * pixelRatio ),
			qRound( 140 * pixelRatio ), qRound( 140 * pixelRatio ) );
		for ( int y = sampleRect.top(); y < sampleRect.bottom() && y < texturedImage.height(); ++y )
			for ( int x = sampleRect.left(); x < sampleRect.right() && x < texturedImage.width(); ++x )
			{
				const QRgb pixel = texturedImage.pixel( x, y );
				colors.insert( pixel );
				if ( comparisonImage.pixel( x, y ) != pixel ) ++changedPixels;
				if ( fallbackImage.pixel( x, y ) != pixel ) ++texturePixels;
			}
		const QString screenshotPath = qEnvironmentVariable( "HAMMER_SELFTEST_TEXTURE_SCREENSHOT" );
		if ( !screenshotPath.isEmpty() ) texturedImage.save( screenshotPath );
		const QSize physicalSize = textureView.size() * textureView.devicePixelRatioF();
		ctx.Step( "3d-textured-framebuffer", loadedTexture && fallbackImage.size() == texturedImage.size() &&
			texturedImage.size() == physicalSize && colors.size() > 16 && changedPixels > 500 && texturePixels > 500,
			QStringLiteral( "loaded texture=%1; sample colors=%2; flat diff=%3; fallback diff=%4; framebuffer=%5x%6; screenshot=%7" )
				.arg( loadedTexture ).arg( colors.size() ).arg( changedPixels ).arg( texturePixels )
				.arg( texturedImage.width() ).arg( texturedImage.height() ).arg( screenshotPath ) );
	}

	// Real viewport mouse events: nearest hit wins; shift toggles; clicks outside
	// clear selection, while an orbit drag must never select an object.
	CHammerDocument pickDoc;
	pickDoc.NewDocument();
	pickDoc.SetTool( TOOL_SELECT );
	const double pickNearLo[3] = { 100, -20, 80 }, pickNearHi[3] = { 120, 20, 120 };
	const double pickFarLo[3] = { 200, -20, 80 }, pickFarHi[3] = { 220, 20, 120 };
	CreateBoxSolid( pickDoc.Map(), pickNearLo, pickNearHi );
	const int nearId = pickDoc.Map().solids.last().id;
	CreateBoxSolid( pickDoc.Map(), pickFarLo, pickFarHi );
	const int farId = pickDoc.Map().solids.last().id;
	CQtViewport3D pickView;
	pickView.setDocument( &pickDoc ); pickView.resize( 400, 300 );
	const double pickEye[3] = { 0, 0, 100 }, pickAngles[3] = { 0, 0, 0 };
	pickView.setCameraPositionAngles( pickEye, pickAngles );
	auto pickClick = [&]( QPointF pos, Qt::KeyboardModifiers modifiers ) {
		QMouseEvent press( QEvent::MouseButtonPress, pos, Qt::LeftButton, Qt::LeftButton, modifiers );
		QMouseEvent release( QEvent::MouseButtonRelease, pos, Qt::LeftButton, Qt::NoButton, modifiers );
		QCoreApplication::sendEvent( &pickView, &press ); QCoreApplication::sendEvent( &pickView, &release );
	};
	pickClick( QPointF( 200, 150 ), Qt::NoModifier );
	bool pickOk = pickDoc.SelectedIds().size() == 1 && pickDoc.IsSelected( nearId ) && !pickDoc.IsSelected( farId );
	pickClick( QPointF( 200, 150 ), Qt::ShiftModifier );
	pickOk = pickOk && pickDoc.SelectedIds().isEmpty();
	pickDoc.SelectObject( farId );
	pickClick( QPointF( 200, 150 ), Qt::ShiftModifier );
	pickOk = pickOk && pickDoc.IsSelected( nearId ) && pickDoc.IsSelected( farId );
	pickClick( QPointF( 0, 0 ), Qt::ShiftModifier );
	pickOk = pickOk && pickDoc.SelectedIds().size() == 2;
	pickClick( QPointF( 0, 0 ), Qt::NoModifier );
	pickOk = pickOk && pickDoc.SelectedIds().isEmpty();
	EntityLite pickEntity{};
	pickEntity.id = pickDoc.Map().AllocId(); pickEntity.classname = QStringLiteral( "unknown_test" );
	pickEntity.origin[0] = 50; pickEntity.origin[2] = 100;
	pickDoc.Map().entities.append( pickEntity );
	pickClick( QPointF( 200, 150 ), Qt::NoModifier );
	pickOk = pickOk && pickDoc.SelectedIds().size() == 1 && pickDoc.IsSelected( pickEntity.id );
	QMouseEvent orbitPress( QEvent::MouseButtonPress, QPointF( 0, 0 ), Qt::LeftButton, Qt::LeftButton, Qt::NoModifier );
	QMouseEvent orbitMove( QEvent::MouseMove, QPointF( 25, 0 ), Qt::NoButton, Qt::LeftButton, Qt::NoModifier );
	QMouseEvent orbitRelease( QEvent::MouseButtonRelease, QPointF( 25, 0 ), Qt::LeftButton, Qt::NoButton, Qt::NoModifier );
	QCoreApplication::sendEvent( &pickView, &orbitPress ); QCoreApplication::sendEvent( &pickView, &orbitMove );
	QCoreApplication::sendEvent( &pickView, &orbitRelease );
	pickOk = pickOk && pickDoc.SelectedIds().size() == 1 && pickDoc.IsSelected( pickEntity.id ) && !pickDoc.IsDirty();
	// Right drag remains a camera gesture and does not change selection.
	QMouseEvent rightPress( QEvent::MouseButtonPress, QPointF( 200, 150 ), Qt::RightButton, Qt::RightButton, Qt::NoModifier );
	QMouseEvent rightMove( QEvent::MouseMove, QPointF( 215, 150 ), Qt::NoButton, Qt::RightButton, Qt::NoModifier );
	QMouseEvent rightRelease( QEvent::MouseButtonRelease, QPointF( 215, 150 ), Qt::RightButton, Qt::NoButton, Qt::NoModifier );
	QCoreApplication::sendEvent( &pickView, &rightPress ); QCoreApplication::sendEvent( &pickView, &rightMove );
	QCoreApplication::sendEvent( &pickView, &rightRelease );
	pickOk = pickOk && pickDoc.SelectedIds().size() == 1 && pickDoc.IsSelected( pickEntity.id );
	// A missing intermediate move must not turn a long drag into a click.
	QMouseEvent missedPress( QEvent::MouseButtonPress, QPointF( 200, 150 ), Qt::LeftButton, Qt::LeftButton, Qt::NoModifier );
	QMouseEvent missedRelease( QEvent::MouseButtonRelease, QPointF( 10, 10 ), Qt::LeftButton, Qt::NoButton, Qt::NoModifier );
	QCoreApplication::sendEvent( &pickView, &missedPress ); QCoreApplication::sendEvent( &pickView, &missedRelease );
	pickOk = pickOk && pickDoc.SelectedIds().size() == 1 && pickDoc.IsSelected( pickEntity.id );
	// Losing capture or focus ends the gesture; a later release/no-button move
	// must neither select nor rotate the camera.
	for ( QEvent::Type lostType : { QEvent::UngrabMouse, QEvent::FocusOut } )
	{
		QMouseEvent lostPress( QEvent::MouseButtonPress, QPointF( 200, 150 ), Qt::LeftButton, Qt::LeftButton, Qt::NoModifier );
		QEvent lostCapture( QEvent::UngrabMouse );
		QFocusEvent lostFocus( QEvent::FocusOut );
		QCoreApplication::sendEvent( &pickView, &lostPress );
		QCoreApplication::sendEvent( &pickView, lostType == QEvent::FocusOut ? static_cast<QEvent *>( &lostFocus ) : &lostCapture );
		double beforeEye[3], beforeForward[3], beforeUp[3], afterEye[3], afterForward[3], afterUp[3];
		pickView.cameraBasis( beforeEye, beforeForward, beforeUp );
		QMouseEvent lostMove( QEvent::MouseMove, QPointF( 260, 150 ), Qt::NoButton, Qt::NoButton, Qt::NoModifier );
		QMouseEvent lostRelease( QEvent::MouseButtonRelease, QPointF( 200, 150 ), Qt::LeftButton, Qt::NoButton, Qt::NoModifier );
		QCoreApplication::sendEvent( &pickView, &lostMove ); QCoreApplication::sendEvent( &pickView, &lostRelease );
		pickView.cameraBasis( afterEye, afterForward, afterUp );
		for ( int i = 0; i < 3; ++i ) pickOk = pickOk && beforeEye[i] == afterEye[i] && beforeForward[i] == afterForward[i];
		pickOk = pickOk && pickDoc.SelectedIds().size() == 1 && pickDoc.IsSelected( pickEntity.id );
	}
	// A convex wedge is clipped from its actual face planes. It occludes the
	// farther brush at the centre but allows hits through its empty bounding box.
	pickDoc.Map().entities.clear();
	SolidLite &wedge = pickDoc.Map().solids[0];
	wedge.sides[2].planePts[0][1] = 0;
	wedge.sides[2].planePts[2][1] = 0;
	const double wideFarLo[3] = { 200, -40, 80 }, wideFarHi[3] = { 220, 40, 120 };
	pickDoc.Map().solids[1].sides = MakeBoxSides( wideFarLo, wideFarHi );
	pickDoc.Map().RecalcSolidBounds( pickDoc.Map().solids[1] );
	QList<HammerWinding::Polygon> wedgeFaces;
	pickOk = pickOk && HammerWinding::BuildFaces( wedge, wedgeFaces ) && wedgeFaces.size() == 6;
	pickView.setCameraPositionAngles( pickEye, pickAngles );
	pickClick( QPointF( 200, 150 ), Qt::NoModifier );
	pickOk = pickOk && pickDoc.SelectedIds().size() == 1 && pickDoc.IsSelected( nearId );
	pickClick( QPointF( 177.5, 150 ), Qt::NoModifier );
	pickOk = pickOk && pickDoc.SelectedIds().size() == 1 && pickDoc.IsSelected( farId );
	// Invalid and unbounded planes are never treated as exact geometry. Their
	// existing cached bounds conservatively block selecting objects behind.
	wedge.sides[2].planePts[2][0] = wedge.sides[2].planePts[1][0];
	wedge.sides[2].planePts[2][1] = wedge.sides[2].planePts[1][1];
	wedge.sides[2].planePts[2][2] = wedge.sides[2].planePts[1][2];
	pickOk = pickOk && !HammerWinding::BuildFaces( wedge, wedgeFaces ) && wedgeFaces.isEmpty();
	pickClick( QPointF( 200, 150 ), Qt::NoModifier );
	pickOk = pickOk && pickDoc.SelectedIds().isEmpty();
	ctx.Step( "3d-selection", pickOk, QStringLiteral( "nearest boxes, convex wedge, occlusion, invalid fallback and input gestures" ) );

	// A selected 3D object moves in the camera plane as a single undo step.
	CHammerDocument move3dDoc; move3dDoc.NewDocument(); move3dDoc.SetSnapEnabled( false );
	const double move3dLo[3] = { 100, -20, 80 }, move3dHi[3] = { 120, 20, 120 };
	const int move3dId = CreateBoxSolid( move3dDoc.Map(), move3dLo, move3dHi );
	move3dDoc.SelectObject( move3dId );
	CQtViewport3D move3dView; move3dView.resize( 400, 300 ); move3dView.setDocument( &move3dDoc );
	move3dView.setCameraPositionAngles( pickEye, pickAngles );
	auto move3dPress = [&] { QMouseEvent e( QEvent::MouseButtonPress, QPointF( 200, 150 ), Qt::LeftButton, Qt::LeftButton, Qt::NoModifier ); QCoreApplication::sendEvent( &move3dView, &e ); };
	auto move3dMotion = [&] { QMouseEvent e( QEvent::MouseMove, QPointF( 230, 150 ), Qt::NoButton, Qt::LeftButton, Qt::NoModifier ); QCoreApplication::sendEvent( &move3dView, &e ); };
	auto move3dRelease = [&] { QMouseEvent e( QEvent::MouseButtonRelease, QPointF( 230, 150 ), Qt::LeftButton, Qt::NoButton, Qt::NoModifier ); QCoreApplication::sendEvent( &move3dView, &e ); };
	move3dPress(); move3dMotion(); move3dRelease();
	const SolidLite *moved3d = move3dDoc.Map().SolidById( move3dId );
	bool move3dOk = moved3d && std::abs( moved3d->mins[1] - move3dLo[1] ) > 1.0;
	const bool move3dUndone = move3dDoc.Undo(); moved3d = move3dDoc.Map().SolidById( move3dId );
	move3dOk = move3dOk && move3dUndone && moved3d && std::abs( moved3d->mins[1] - move3dLo[1] ) < 1e-6;
	const bool noExtraUndo = !move3dDoc.Undo();
	const bool move3dRedone = move3dDoc.Redo(); moved3d = move3dDoc.Map().SolidById( move3dId );
	move3dOk = move3dOk && noExtraUndo && move3dRedone;
	move3dOk = move3dOk && moved3d && std::abs( moved3d->mins[1] - move3dLo[1] ) > 1.0;
	move3dDoc.Undo();
	move3dPress(); move3dMotion();
	QKeyEvent move3dEscape( QEvent::KeyPress, Qt::Key_Escape, Qt::NoModifier ); QCoreApplication::sendEvent( &move3dView, &move3dEscape );
	move3dRelease(); moved3d = move3dDoc.Map().SolidById( move3dId );
	move3dOk = move3dOk && moved3d && std::abs( moved3d->mins[1] - move3dLo[1] ) < 1e-6;
	const bool redoAfterCancel = move3dDoc.Redo(); move3dDoc.Undo();
	move3dOk = move3dOk && redoAfterCancel;
	move3dPress(); move3dMotion(); QEvent move3dFocusLost( QEvent::FocusOut ); QCoreApplication::sendEvent( &move3dView, &move3dFocusLost );
	move3dRelease(); moved3d = move3dDoc.Map().SolidById( move3dId );
	move3dOk = move3dOk && moved3d && std::abs( moved3d->mins[1] - move3dLo[1] ) < 1e-6 && move3dDoc.Redo();
	move3dDoc.Undo();
	ctx.Step( "3d-move", move3dOk, QStringLiteral( "camera-plane drag, undo/redo, escape and focus cancellation" ) );

	move3dDoc.SetTool( TOOL_TEXTURE );
	move3dDoc.SetActiveMaterial( QStringLiteral( "test/3d_painted_face" ) );
	move3dPress();
	QMouseEvent paintLeftRelease( QEvent::MouseButtonRelease, QPointF( 200, 150 ), Qt::LeftButton, Qt::NoButton, Qt::NoModifier );
	QCoreApplication::sendEvent( &move3dView, &paintLeftRelease );
	int beforePaint = 0;
	for ( const SideLite &side : move3dDoc.Map().SolidById( move3dId )->sides )
		if ( side.texture == QLatin1String( "test/3d_painted_face" ) ) ++beforePaint;
	QMouseEvent paintPress( QEvent::MouseButtonPress, QPointF( 200, 150 ), Qt::RightButton, Qt::RightButton, Qt::NoModifier );
	QMouseEvent paintRelease( QEvent::MouseButtonRelease, QPointF( 200, 150 ), Qt::RightButton, Qt::NoButton, Qt::NoModifier );
	QCoreApplication::sendEvent( &move3dView, &paintPress ); QCoreApplication::sendEvent( &move3dView, &paintRelease );
	int paintedFaces = 0;
	for ( const SideLite &side : move3dDoc.Map().SolidById( move3dId )->sides )
		if ( side.texture == QLatin1String( "test/3d_painted_face" ) ) ++paintedFaces;
	bool paint3dOk = beforePaint == 0 && paintedFaces == 1;
	move3dDoc.Undo();
	QMouseEvent paintDragPress( QEvent::MouseButtonPress, QPointF( 200, 150 ), Qt::RightButton, Qt::RightButton, Qt::NoModifier );
	QMouseEvent paintDragMove( QEvent::MouseMove, QPointF( 230, 150 ), Qt::NoButton, Qt::RightButton, Qt::NoModifier );
	QMouseEvent paintDragRelease( QEvent::MouseButtonRelease, QPointF( 230, 150 ), Qt::RightButton, Qt::NoButton, Qt::NoModifier );
	QCoreApplication::sendEvent( &move3dView, &paintDragPress ); QCoreApplication::sendEvent( &move3dView, &paintDragMove ); QCoreApplication::sendEvent( &move3dView, &paintDragRelease );
	paintedFaces = 0;
	for ( const SideLite &side : move3dDoc.Map().SolidById( move3dId )->sides )
		if ( side.texture == QLatin1String( "test/3d_painted_face" ) ) ++paintedFaces;
	paint3dOk = paint3dOk && paintedFaces == 0;
	// The preceding right-drag deliberately orbits the camera; reset it so
	// the following centre click is guaranteed to hit the same brush.
	move3dView.setCameraPositionAngles( pickEye, pickAngles );
	QMouseEvent paintAllPress( QEvent::MouseButtonPress, QPointF( 200, 150 ), Qt::RightButton, Qt::RightButton, Qt::ShiftModifier );
	QMouseEvent paintAllRelease( QEvent::MouseButtonRelease, QPointF( 200, 150 ), Qt::RightButton, Qt::NoButton, Qt::ShiftModifier );
	QCoreApplication::sendEvent( &move3dView, &paintAllPress ); QCoreApplication::sendEvent( &move3dView, &paintAllRelease );
	paintedFaces = 0;
	for ( const SideLite &side : move3dDoc.Map().SolidById( move3dId )->sides )
		if ( side.texture == QLatin1String( "test/3d_painted_face" ) ) ++paintedFaces;
	paint3dOk = paint3dOk && paintedFaces == move3dDoc.Map().SolidById( move3dId )->sides.size() && move3dDoc.Undo();
	paintedFaces = 0;
	for ( const SideLite &side : move3dDoc.Map().SolidById( move3dId )->sides )
		if ( side.texture == QLatin1String( "test/3d_painted_face" ) ) ++paintedFaces;
	paint3dOk = paint3dOk && paintedFaces == 0 && move3dDoc.Redo();
	paintedFaces = 0;
	for ( const SideLite &side : move3dDoc.Map().SolidById( move3dId )->sides )
		if ( side.texture == QLatin1String( "test/3d_painted_face" ) ) ++paintedFaces;
	paint3dOk = paint3dOk && paintedFaces == move3dDoc.Map().SolidById( move3dId )->sides.size();
	ctx.Step( "3d-material-gesture", paint3dOk, QStringLiteral( "right click paints one face; Shift paints whole brush with undo/redo; drag does not paint" ) );

	// Plane-defining points can be arbitrarily far along a face and are not
	// necessarily brush vertices. Bounds must come from clipped face windings.
	CHammerDocument boundsDoc;
	boundsDoc.NewDocument();
	const double boundsLo[3] = { -20, -10, -4 }, boundsHi[3] = { 20, 10, 4 };
	const int boundsId = CreateBoxSolid( boundsDoc.Map(), boundsLo, boundsHi );
	SolidLite *boundsSolid = boundsDoc.Map().SolidById( boundsId );
	for ( int face = 0; face < boundsSolid->sides.size(); ++face )
	{
		const int tangent = ( face / 2 + 1 ) % 3;
		for ( int point = 0; point < 3; ++point ) boundsSolid->sides[face].planePts[point][tangent] += 1000;
	}
	boundsDoc.Map().RecalcSolidBounds( *boundsSolid );
	const auto boundsMatch = []( const SolidLite &solid, const double lo[3], const double hi[3] ) {
		for ( int axis = 0; axis < 3; ++axis )
			if ( std::abs( solid.mins[axis] - lo[axis] ) > 1e-6 || std::abs( solid.maxs[axis] - hi[axis] ) > 1e-6 ) return false;
		return true;
	};
	bool bConvexBounds = boundsMatch( *boundsSolid, boundsLo, boundsHi );
	const bool boundsInitial = bConvexBounds;
	QTemporaryDir boundsTemp;
	QString boundsError;
	MapDocLite boundsReloaded;
	const QString boundsPath = boundsTemp.filePath( QStringLiteral( "convex-bounds.vmf" ) );
	bConvexBounds = bConvexBounds && boundsTemp.isValid() && VmfIO::SaveMap( boundsPath, boundsDoc.Map(), &boundsError ) &&
		VmfIO::LoadMap( boundsPath, boundsReloaded, &boundsError ) && boundsReloaded.solids.size() == 1 &&
		boundsMatch( boundsReloaded.solids[0], boundsLo, boundsHi );
	const bool boundsRoundtrip = bConvexBounds;
	boundsDoc.SelectObject( boundsId );
	const double boundsDelta[3] = { 8, -3, 5 };
	const double movedLo[3] = { -12, -13, 1 }, movedHi[3] = { 28, 7, 9 };
	bConvexBounds = bConvexBounds && boundsDoc.MoveSelection( boundsDelta ) &&
		boundsMatch( *boundsDoc.Map().SolidById( boundsId ), movedLo, movedHi ) && boundsDoc.Undo() &&
		boundsMatch( *boundsDoc.Map().SolidById( boundsId ), boundsLo, boundsHi ) && boundsDoc.Redo() &&
		boundsMatch( *boundsDoc.Map().SolidById( boundsId ), movedLo, movedHi );
	const bool boundsUndo = bConvexBounds;
	SolidLite boundsWedge;
	boundsWedge.sides = MakeBoxSides( boundsLo, boundsHi );
	boundsWedge.sides[2].planePts[0][1] = 0;
	boundsWedge.sides[2].planePts[2][1] = 0;
	// X remains tangent to this slanted Y/Z plane; translating its defining
	// points along Z instead would move the plane and change the wedge itself.
	for ( int point = 0; point < 3; ++point ) boundsWedge.sides[2].planePts[point][0] += 1000;
	QList<HammerWinding::Polygon> boundsFaces;
	bConvexBounds = bConvexBounds && HammerWinding::BuildFaces( boundsWedge, boundsFaces );
	boundsDoc.Map().RecalcSolidBounds( boundsWedge );
	bConvexBounds = bConvexBounds && boundsMatch( boundsWedge, boundsLo, boundsHi );
	const bool boundsWedgeOk = bConvexBounds;
	// Unsupported degenerate planes retain the prior plane-point fallback.
	SolidLite invalidBounds = boundsWedge;
	for ( int axis = 0; axis < 3; ++axis )
		invalidBounds.sides[2].planePts[2][axis] = invalidBounds.sides[2].planePts[1][axis];
	bConvexBounds = bConvexBounds && !HammerWinding::BuildFaces( invalidBounds, boundsFaces );
	boundsDoc.Map().RecalcSolidBounds( invalidBounds );
	bConvexBounds = bConvexBounds && invalidBounds.maxs[0] > 900;
	ctx.Step( "convex-brush-bounds", bConvexBounds,
		QStringLiteral( "initial=%1 roundtrip=%2 undo=%3 wedge=%4 fallback=%5 error=%6" )
			.arg( boundsInitial ).arg( boundsRoundtrip ).arg( boundsUndo ).arg( boundsWedgeOk ).arg( bConvexBounds ).arg( boundsError ) );

	// Center on a mixed selection using its bounds, without editing the map.
	CHammerDocument centerDoc;
	centerDoc.NewDocument();
	const double centerMins[3] = { -20, -10, -4 }, centerMaxs[3] = { 20, 10, 4 };
	CreateBoxSolid( centerDoc.Map(), centerMins, centerMaxs );
	const int centerSolidId = centerDoc.Map().solids.last().id;
	EntityLite centerEntity;
	centerEntity.id = centerDoc.Map().AllocId();
	centerEntity.origin[0] = 100; centerEntity.origin[1] = 30; centerEntity.origin[2] = 20;
	centerDoc.Map().entities.append( centerEntity );
	double selectionCenter[3] = {};
	bool bCenter = !SelectionCenter( centerDoc, selectionCenter );
	centerDoc.SelectObjects( QSet<int>() << centerSolidId << centerEntity.id );
	bCenter = bCenter && SelectionCenter( centerDoc, selectionCenter ) &&
		selectionCenter[0] == 40 && selectionCenter[1] == 10 && selectionCenter[2] == 8 && !centerDoc.IsDirty();
	ctx.Step( "selection-center", bCenter, QStringLiteral( "mixed bounds, empty selection and clean document" ) );
	double goPoint[3] = { 11, 22, 33 };
	bool bNavigation = ParseNavigationCoordinates( QStringLiteral( "  200  -4096  1154.5  " ), goPoint ) &&
		goPoint[0] == 200 && goPoint[1] == -4096 && goPoint[2] == 1154.5 &&
		!ParseNavigationCoordinates( QStringLiteral( "1 2 nan" ), goPoint ) && goPoint[0] == 200 &&
		!ParseNavigationCoordinates( QStringLiteral( "1 2" ), goPoint ) &&
		!SelectBrushById( centerDoc, centerEntity.id ) && centerDoc.IsSelected( centerEntity.id ) &&
		SelectBrushById( centerDoc, centerSolidId ) && centerDoc.IsSelected( centerSolidId ) &&
		!centerDoc.IsSelected( centerEntity.id ) && !centerDoc.IsDirty();
	double cmdPoint[3] = { 11, 22, 33 }, cmdAngles[3] = { 44, 55, 66 };
	bNavigation = bNavigation && ParseNavigationCommand( QStringLiteral( "setpos 10 -20 30; setang 0 90 0" ), cmdPoint, cmdAngles ) &&
		cmdPoint[0] == 10 && cmdPoint[1] == -20 && cmdPoint[2] == 30 && cmdAngles[1] == 90 &&
		!ParseNavigationCommand( QStringLiteral( "setpos 1 2 3; setang 0 nan 0" ), cmdPoint, cmdAngles ) && cmdPoint[0] == 10 &&
		!ParseNavigationCommand( QStringLiteral( "setpos 1 2 3; quit 0 0 0" ), cmdPoint, cmdAngles );
	CQtViewport3D navigationView;
	const double eyePosition[3] = { 10, -20, 94 }; // Source origin + standing eye height
	navigationView.setCameraPositionAngles( eyePosition, cmdAngles );
	double cameraEye[3], cameraForward[3], cameraUp[3];
	navigationView.cameraBasis( cameraEye, cameraForward, cameraUp );
	bNavigation = bNavigation && std::abs( cameraEye[0] - 10 ) < 1e-8 && std::abs( cameraEye[1] + 20 ) < 1e-8 &&
		std::abs( cameraEye[2] - 94 ) < 1e-8 && std::abs( cameraForward[0] ) < 1e-8 &&
		std::abs( cameraForward[1] - 1 ) < 1e-8 && std::abs( cameraUp[2] - 1 ) < 1e-8;
	const double verticalAngles[3] = { -90, 45, 30 };
	navigationView.setCameraPositionAngles( eyePosition, verticalAngles );
	navigationView.cameraBasis( cameraEye, cameraForward, cameraUp );
	bNavigation = bNavigation && std::abs( cameraForward[2] - 1 ) < 1e-8 &&
		std::abs( cameraUp[0] * cameraUp[0] + cameraUp[1] * cameraUp[1] + cameraUp[2] * cameraUp[2] - 1 ) < 1e-8;
	ctx.Step( "go-to-coordinates-and-brush", bNavigation, QStringLiteral( "finite XYZ, setpos/setang camera pose and exact solid ID selection" ) );

	// Original Hammer snaps the selection bounds together, or each object's
	// own reference point separately, without changing brush dimensions.
	CHammerDocument snapDoc;
	snapDoc.NewDocument();
	snapDoc.SetGridSize( 16 );
	const double snapLo[3] = { 3, -21, 2 }, snapHi[3] = { 35, 11, 34 };
	const int snapBrush = snapDoc.CreateBrush( snapLo, snapHi );
	const double snapOrigin[3] = { 51, -9, 18 };
	const int snapEntity = snapDoc.PlaceEntity( QStringLiteral( "info_target" ), snapOrigin );
	snapDoc.SelectObjects( QSet<int>() << snapBrush << snapEntity );
	bool bSnap = snapDoc.SnapSelectionToGrid( false );
	const SolidLite *snapSolid = snapDoc.Map().SolidById( snapBrush );
	const EntityLite *snapPoint = snapDoc.Map().EntityById( snapEntity );
	bSnap = bSnap && snapSolid && snapPoint && snapSolid->mins[0] == 0 && snapSolid->mins[1] == -16 &&
		snapSolid->maxs[0] - snapSolid->mins[0] == 32 && snapPoint->origin[0] == 48 && snapPoint->origin[1] == -4;
	bSnap = bSnap && snapDoc.Undo() && snapDoc.Map().SolidById( snapBrush )->mins[0] == 3 &&
		snapDoc.Redo() && snapDoc.Map().EntityById( snapEntity )->origin[0] == 48;
	bSnap = bSnap && snapDoc.SnapSelectionToGrid( true ) &&
		snapDoc.Map().EntityById( snapEntity )->origin[1] == 0 &&
		snapDoc.Undo() && snapDoc.Map().EntityById( snapEntity )->origin[1] == -4;
	CHammerDocument integerSnapDoc;
	integerSnapDoc.NewDocument();
	const double fractionalOrigin[3] = { 1.25, -2.25, 4.1 };
	const int fractionalEntity = integerSnapDoc.PlaceEntity( QStringLiteral( "info_target" ), fractionalOrigin );
	integerSnapDoc.SetSnapEnabled( false );
	bSnap = bSnap && integerSnapDoc.SnapSelectionToGrid( false ) &&
		integerSnapDoc.Map().EntityById( fractionalEntity )->origin[0] == 1 &&
		integerSnapDoc.Map().EntityById( fractionalEntity )->origin[1] == -2 &&
		!integerSnapDoc.SnapSelectionToGrid( false );
	ctx.Step( "snap-selection", bSnap, QStringLiteral( "group and individual snap, brush size and undo/redo" ) );

	// Preserve ordered duplicate outputs and stage edits as one undo transaction.
	CHammerDocument outputsDoc;
	outputsDoc.NewDocument();
	const double outputsOrigin[3] = { 0, 0, 0 };
	const int outputsId = outputsDoc.PlaceEntity( QStringLiteral( "logic_relay" ), outputsOrigin );
	CEntityOutputsDialog::OutputList initialOutputs;
	initialOutputs.append( qMakePair( QStringLiteral( "OnTrigger" ), QStringLiteral( "door,Open,,0,-1" ) ) );
	initialOutputs.append( qMakePair( QStringLiteral( "OnTrigger" ), QStringLiteral( "relay,Trigger,,1,1" ) ) );
	bool bOutputs = outputsDoc.SetSelectedEntityOutputs( initialOutputs );
	CEntityOutputsDialog outputsDialog( initialOutputs );
	QTableWidget *outputsTable = outputsDialog.findChild<QTableWidget *>( "entityOutputsTable" );
	bOutputs = bOutputs && outputsTable && outputsDialog.Outputs() == initialOutputs;
	if ( outputsTable ) outputsTable->item( 1, 4 )->setText( QStringLiteral( "2" ) );
	// Unknown formats and existing ESC-separated connections remain intact.
	CEntityOutputsDialog::OutputList unusualOutputs;
	unusualOutputs.append( qMakePair( QStringLiteral( "OnTrigger" ), QStringLiteral( "a,b,c,d,e,extra" ) ) );
	unusualOutputs.append( qMakePair( QStringLiteral( "OnTrigger" ), QStringList({ QStringLiteral("a"), QStringLiteral("b"), QStringLiteral("c,d"), QStringLiteral("0"), QStringLiteral("-1") }).join( QChar( 27 ) ) ) );
	CEntityOutputsDialog unusualDialog( unusualOutputs );
	QTableWidget *unusualTable = unusualDialog.findChild<QTableWidget *>( "entityOutputsTable" );
	bOutputs = bOutputs && unusualTable && unusualDialog.Outputs() == unusualOutputs &&
		!( unusualTable->item( 0, 1 )->flags() & Qt::ItemIsEditable );
	if ( unusualTable )
	{
		unusualTable->item( 1, 3 )->setText( QStringLiteral( "a,b" ) );
		const QString value = unusualDialog.Outputs().last().second;
		bOutputs = bOutputs && value.split( QChar( 27 ) ).size() == 5 &&
			value.split( QChar( 27 ) )[2] == QLatin1String( "a,b" );
	}
	const auto editedOutputs = outputsDialog.Outputs();
	bOutputs = bOutputs && outputsDoc.SetSelectedEntityOutputs( editedOutputs ) &&
		outputsDoc.Map().EntityById( outputsId )->connections == editedOutputs && outputsDoc.Undo() &&
		outputsDoc.Map().EntityById( outputsId )->connections == initialOutputs && outputsDoc.Redo() &&
		outputsDoc.Map().EntityById( outputsId )->connections == editedOutputs;
	ctx.Step( "entity-outputs", bOutputs, QStringLiteral( "ordered duplicate outputs and undo/redo" ) );
	// Copy multiple outputs across dialogs without interpreting raw connections.
	QPushButton *copyOutputs = unusualDialog.findChild<QPushButton *>( QStringLiteral( "entityOutputsCopy" ) );
	bool bOutputCopy = unusualTable && copyOutputs;
	if ( bOutputCopy )
	{
		unusualTable->clearSelection();
		unusualTable->selectionModel()->select( unusualTable->model()->index( 1, 0 ), QItemSelectionModel::Select | QItemSelectionModel::Rows );
		unusualTable->selectionModel()->select( unusualTable->model()->index( 0, 0 ), QItemSelectionModel::Select | QItemSelectionModel::Rows );
		copyOutputs->click();
		CEntityOutputsDialog destination( initialOutputs );
		QPushButton *pasteOutputs = destination.findChild<QPushButton *>( QStringLiteral( "entityOutputsPaste" ) );
		QPushButton *removeOutputs = destination.findChild<QPushButton *>( QStringLiteral( "entityOutputsRemove" ) );
		bOutputCopy = pasteOutputs && removeOutputs;
		if ( bOutputCopy )
		{
			pasteOutputs->click();
			const auto pasted = destination.Outputs();
			bOutputCopy = pasted == initialOutputs + unusualDialog.Outputs();
			removeOutputs->click();
			bOutputCopy = bOutputCopy && destination.Outputs() == initialOutputs;
			pasteOutputs->click();
			destination.reject();
			bOutputCopy = bOutputCopy && outputsDoc.Map().EntityById( outputsId )->connections == editedOutputs;
		}
	}
	ctx.Step( "entity-outputs-copy-paste", bOutputCopy, QStringLiteral( "multi-row copy, cross-dialog paste, raw preservation, removal and cancel" ) );
	// Reordering must retain opaque raw connections and preserve selection order.
	CEntityOutputsDialog::OutputList orderedOutputs = initialOutputs + unusualOutputs;
	CEntityOutputsDialog reorderDialog( orderedOutputs );
	QTableWidget *reorderTable = reorderDialog.findChild<QTableWidget *>( QStringLiteral( "entityOutputsTable" ) );
	QPushButton *moveOutputUp = reorderDialog.findChild<QPushButton *>( QStringLiteral( "entityOutputsMoveUp" ) );
	QPushButton *moveOutputDown = reorderDialog.findChild<QPushButton *>( QStringLiteral( "entityOutputsMoveDown" ) );
	bool bOutputOrder = reorderTable && moveOutputUp && moveOutputDown;
	if ( bOutputOrder )
	{
		reorderTable->selectionModel()->select( reorderTable->model()->index( 2, 0 ), QItemSelectionModel::Select | QItemSelectionModel::Rows );
		reorderTable->selectionModel()->select( reorderTable->model()->index( 3, 0 ), QItemSelectionModel::Select | QItemSelectionModel::Rows );
		moveOutputUp->click();
		bOutputOrder = reorderDialog.Outputs() == CEntityOutputsDialog::OutputList({ orderedOutputs[0], orderedOutputs[2], orderedOutputs[3], orderedOutputs[1] });
		moveOutputDown->click();
		bOutputOrder = bOutputOrder && reorderDialog.Outputs() == orderedOutputs &&
			!( reorderTable->item( 2, 1 )->flags() & Qt::ItemIsEditable );
		reorderDialog.reject();
		bOutputOrder = bOutputOrder && outputsDoc.Map().EntityById( outputsId )->connections == editedOutputs;
	}
	ctx.Step( "entity-outputs-reorder", bOutputOrder, QStringLiteral( "move multiple outputs while preserving raw connections and cancel" ) );
	// FGD I/O suggestions must include inherited names, follow the selected
	// target, and preserve unknown map values rather than coercing them.
	QTemporaryDir ioDir;
	QFile ioFile( ioDir.filePath( QStringLiteral( "io.fgd" ) ) );
	bool bIoSuggestions = ioDir.isValid() && ioFile.open( QIODevice::WriteOnly );
	if ( bIoSuggestions )
	{
		bIoSuggestions = ioFile.write( "@BaseClass = IoBase [ input Enable(void) : \"Enable\" output OnBase(void) : \"Base\" ]\n"
			"@PointClass base(IoBase) = io_source [ output OnTrigger(void) : \"Trigger\" ]\n"
			"@PointClass base(IoBase) = io_target [ input Open(void) : \"Open\" ]\n"
			"@PointClass base(IoBase) = io_other [ input Close(void) : \"Close\" ]\n" ) > 0;
		ioFile.close();
	}
	CFgdData ioFgd;
	bIoSuggestions = bIoSuggestions && ioFgd.Load( ioFile.fileName() ) &&
		ioFgd.ClassOutputs( QStringLiteral( "io_source" ) ).contains( QStringLiteral( "OnBase" ) ) &&
		ioFgd.ClassInputs( QStringLiteral( "io_target" ) ).contains( QStringLiteral( "Enable" ) );
	MapDocLite ioMap;
	EntityLite ioTarget{};
	ioTarget.classname = QStringLiteral( "io_target" );
	ioTarget.keyvalues.insert( QStringLiteral( "TargetName" ), QStringLiteral( "door" ) );
	ioMap.entities.append( ioTarget );
	EntityLite ioOther{};
	ioOther.classname = QStringLiteral( "io_other" );
	ioOther.keyvalues.insert( QStringLiteral( "targetname" ), QStringLiteral( "door_side" ) );
	ioMap.entities.append( ioOther );
	CEntityOutputsDialog::OutputList ioRows;
	ioRows.append( qMakePair( QStringLiteral( "CustomOutput" ), QStringLiteral( "door,CustomInput,,0,-1" ) ) );
	CEntityOutputsDialog ioDialog( ioRows, nullptr, &ioFgd, &ioMap, QStringLiteral( "io_source" ) );
	QTableWidget *ioTable = ioDialog.findChild<QTableWidget *>( "entityOutputsTable" );
	bIoSuggestions = bIoSuggestions && ioTable && ioDialog.Outputs() == ioRows;
	if ( ioTable )
	{
		bIoSuggestions = bIoSuggestions && ioTable->item( 0, 0 )->data( Qt::UserRole + 1 ).toStringList().contains( QStringLiteral( "OnTrigger" ) ) &&
			ioTable->item( 0, 1 )->data( Qt::UserRole + 1 ).toStringList().contains( QStringLiteral( "door" ) ) &&
			ioTable->item( 0, 2 )->data( Qt::UserRole + 1 ).toStringList().contains( QStringLiteral( "Open" ) );
		ioTable->item( 0, 1 )->setText( QStringLiteral( "DOOR*" ) );
		const QStringList wildcardInputs = ioTable->item( 0, 2 )->data( Qt::UserRole + 1 ).toStringList();
		bIoSuggestions = bIoSuggestions && wildcardInputs.contains( QStringLiteral( "Open" ) ) &&
			wildcardInputs.contains( QStringLiteral( "Close" ) ) && wildcardInputs.count( QStringLiteral( "Enable" ) ) == 1 &&
			ioDialog.Outputs().first().second == QLatin1String( "DOOR*,CustomInput,,0,-1" );
		ioTable->item( 0, 6 )->setText( QStringLiteral( "door_*,CustomInput,,0,-1" ) );
		const QStringList rawWildcardInputs = ioTable->item( 0, 2 )->data( Qt::UserRole + 1 ).toStringList();
		bIoSuggestions = bIoSuggestions && rawWildcardInputs.contains( QStringLiteral( "Close" ) ) &&
			!rawWildcardInputs.contains( QStringLiteral( "Open" ) );
		ioTable->item( 0, 1 )->setText( QStringLiteral( "!self" ) );
		bIoSuggestions = bIoSuggestions && ioTable->item( 0, 2 )->data( Qt::UserRole + 1 ).toStringList().contains( QStringLiteral( "Enable" ) ) &&
			ioDialog.Outputs().first().second == QLatin1String( "!self,CustomInput,,0,-1" );
		const QModelIndex ioIndex = ioTable->model()->index( 0, 0 );
		QAbstractItemDelegate *ioDelegate = ioTable->itemDelegateForColumn( 0 );
		QWidget *ioEditor = ioDelegate->createEditor( ioTable, QStyleOptionViewItem(), ioIndex );
		QComboBox *ioCombo = qobject_cast<QComboBox *>( ioEditor );
		if ( ioCombo )
		{
			ioDelegate->setEditorData( ioCombo, ioIndex );
			bIoSuggestions = bIoSuggestions && ioCombo->currentText() == QLatin1String( "CustomOutput" );
			ioCombo->setEditText( QStringLiteral( "AnotherCustomOutput" ) );
			ioDelegate->setModelData( ioCombo, ioTable->model(), ioIndex );
			bIoSuggestions = bIoSuggestions && ioDialog.Outputs().first().first == QLatin1String( "AnotherCustomOutput" );
		}
		else bIoSuggestions = false;
		delete ioEditor;
	}
	ctx.Step( "entity-io-suggestions", bIoSuggestions, QStringLiteral( "inherited FGD names, target lookup and unknown I/O preservation" ) );

	// Exact material replacement preserves per-face texture alignment and history.
	CHammerDocument replaceDoc;
	replaceDoc.NewDocument();
	const double replaceLo1[3] = { 0, 0, 0 }, replaceHi1[3] = { 16, 16, 16 };
	const double replaceLo2[3] = { 32, 0, 0 }, replaceHi2[3] = { 48, 16, 16 };
	const int replaceId1 = replaceDoc.CreateBrush( replaceLo1, replaceHi1 );
	const int replaceId2 = replaceDoc.CreateBrush( replaceLo2, replaceHi2 );
	replaceDoc.Map().SolidById( replaceId1 )->sides[0].uaxis[3] = 23;
	replaceDoc.Map().SolidById( replaceId1 )->sides[0].uscale = .75f;
	replaceDoc.SelectObject( replaceId1 );
	const QString originalTexture = replaceDoc.Map().SolidById( replaceId1 )->sides[0].texture;
	const int selectedCount = replaceDoc.ReplaceMaterial( originalTexture.toUpper(), QStringLiteral( "materials/brick/example.vmt" ), true );
	bool bReplace = selectedCount == 6 && replaceDoc.Map().SolidById( replaceId1 )->sides[0].texture == QLatin1String( "brick/example" ) &&
		replaceDoc.Map().SolidById( replaceId2 )->sides[0].texture == originalTexture &&
		replaceDoc.Map().SolidById( replaceId1 )->sides[0].uaxis[3] == 23 && replaceDoc.Map().SolidById( replaceId1 )->sides[0].uscale == .75f;
	bReplace = bReplace && replaceDoc.Undo() && replaceDoc.Map().SolidById( replaceId1 )->sides[0].texture == originalTexture &&
		replaceDoc.Redo() && replaceDoc.Map().SolidById( replaceId1 )->sides[0].texture == QLatin1String( "brick/example" );
	bReplace = bReplace && replaceDoc.ReplaceMaterial( originalTexture, QStringLiteral( "brick/example" ), false ) == 6 &&
		replaceDoc.Map().SolidById( replaceId2 )->sides[0].texture == QLatin1String( "brick/example" ) &&
		replaceDoc.ReplaceMaterial( QStringLiteral( "../bad" ), QStringLiteral( "brick/example" ), false ) == -1 &&
		replaceDoc.ReplaceMaterial( QStringLiteral( "brick/example" ), QStringLiteral( "brick/example" ), false ) == 0;
	using ReplaceMode = CHammerDocument::MaterialReplaceMode;
	bReplace = bReplace && replaceDoc.ReplaceMaterial( QStringLiteral( "EXAMPLE" ), QStringLiteral( "stone/whole" ), true, ReplaceMode::PartialWhole ) == 6 &&
		replaceDoc.Map().SolidById( replaceId2 )->sides[0].texture == QLatin1String( "brick/example" ) &&
		replaceDoc.ReplaceMaterial( QStringLiteral( "STONE" ), QStringLiteral( "wood" ), true, ReplaceMode::PartialSubstring ) == 6 &&
		replaceDoc.Map().SolidById( replaceId1 )->sides[0].texture == QLatin1String( "wood/whole" ) &&
		replaceDoc.Undo() && replaceDoc.Map().SolidById( replaceId1 )->sides[0].texture == QLatin1String( "stone/whole" ) &&
		replaceDoc.Redo() && replaceDoc.Map().SolidById( replaceId1 )->sides[0].texture == QLatin1String( "wood/whole" ) &&
		replaceDoc.ReplaceMaterial( QStringLiteral( "wood" ), QStringLiteral( ".." ), false, ReplaceMode::PartialSubstring ) == -1 &&
		replaceDoc.Map().SolidById( replaceId1 )->sides[0].texture == QLatin1String( "wood/whole" );
	replaceDoc.Map().SolidById( replaceId1 )->sides[0].texture = QStringLiteral( "wood/wood" );
	bReplace = bReplace && replaceDoc.ReplaceMaterial( QStringLiteral( "wood" ), QStringLiteral( "metal" ), true, ReplaceMode::PartialSubstring ) == 6 &&
		replaceDoc.Map().SolidById( replaceId1 )->sides[0].texture == QLatin1String( "metal/wood" ) &&
		replaceDoc.Undo() && replaceDoc.Map().SolidById( replaceId1 )->sides[0].texture == QLatin1String( "wood/wood" );
	replaceDoc.Map().SolidById( replaceId1 )->sides[0].texture = QStringLiteral( "prefixwood/foo" );
	replaceDoc.Map().SolidById( replaceId1 )->sides[1].texture = QStringLiteral( "wood/foo" );
	bReplace = bReplace && replaceDoc.ReplaceMaterial( QStringLiteral( "WOOD" ), QString(), true, ReplaceMode::PartialSubstring ) == -1 &&
		replaceDoc.Map().SolidById( replaceId1 )->sides[0].texture == QLatin1String( "prefixwood/foo" ) &&
		replaceDoc.Map().SolidById( replaceId1 )->sides[1].texture == QLatin1String( "wood/foo" );
	ctx.Step( "replace-material", bReplace, QStringLiteral( "selection, whole map, texture axes, validation and undo/redo" ) );

	CHammerDocument faceDoc;
	faceDoc.NewDocument();
	const double faceLo[3] = { 0, 0, 0 }, faceHi[3] = { 64, 64, 64 };
	const int faceBrush = faceDoc.CreateBrush( faceLo, faceHi );
	faceDoc.SelectObject( faceBrush );
	faceDoc.Map().SolidById( faceBrush )->sides[0].hasVerticesPlus = true;
	faceDoc.Map().SolidById( faceBrush )->sides[0].verticesPlus << QStringLiteral( "0 0 0" );
	const QList<SideLite> beforeFaces = faceDoc.Map().SolidById( faceBrush )->sides;
	CFacePropertiesDialog faceDialog( beforeFaces );
	QTableWidget *faceTable = faceDialog.findChild<QTableWidget *>( QStringLiteral( "facePropertiesTable" ) );
	QList<SideLite> editedFaces;
	bool bFaces = faceTable && faceDialog.ReadFaces( editedFaces );
	if ( faceTable )
	{
		faceTable->item( 0, 1 )->setText( QStringLiteral( "materials/brick/example.vmt" ) );
		faceTable->item( 0, 2 )->setText( QStringLiteral( "1 0 0 24" ) );
		faceTable->item( 0, 3 )->setText( QStringLiteral( "0.5" ) );
		bFaces = bFaces && faceDialog.ReadFaces( editedFaces );
		faceTable->item( 1, 3 )->setText( QStringLiteral( "0" ) );
		QList<SideLite> invalidFaces;
		bFaces = bFaces && !faceDialog.ReadFaces( invalidFaces );
	}
	bFaces = bFaces && faceDoc.SetSelectedFaceProperties( editedFaces );
	const SideLite &changedFace = faceDoc.Map().SolidById( faceBrush )->sides[0];
	bFaces = bFaces && changedFace.texture == QLatin1String( "brick/example" ) && changedFace.uaxis[3] == 24 &&
		changedFace.uscale == .5f && changedFace.hasVerticesPlus && changedFace.verticesPlus == beforeFaces[0].verticesPlus &&
		faceDoc.Map().SolidById( faceBrush )->sides[1].texture == beforeFaces[1].texture;
	bFaces = bFaces && faceDoc.Undo() && faceDoc.Map().SolidById( faceBrush )->sides[0].texture == beforeFaces[0].texture &&
		faceDoc.Redo() && faceDoc.Map().SolidById( faceBrush )->sides[0].uaxis[3] == 24;
	// The rotation field changes the axes, not merely the VMF metadata.
	CFacePropertiesDialog rotationDialog( faceDoc.Map().SolidById( faceBrush )->sides );
	QTableWidget *rotationTable = rotationDialog.findChild<QTableWidget *>( QStringLiteral( "facePropertiesTable" ) );
	QList<SideLite> rotatedFaces;
	if ( rotationTable )
	{
		rotationTable->item( 0, 2 )->setText( QStringLiteral( "1 0 0 24" ) );
		rotationTable->item( 0, 4 )->setText( QStringLiteral( "0 1 0 13" ) );
		rotationTable->item( 0, 6 )->setText( QStringLiteral( "90" ) );
		bFaces = bFaces && rotationDialog.ReadFaces( rotatedFaces );
		if ( !rotatedFaces.isEmpty() )
			bFaces = bFaces && std::abs( rotatedFaces[0].uaxis[0] ) < 0.0001f &&
				std::abs( rotatedFaces[0].uaxis[1] + 1.0f ) < 0.0001f &&
				std::abs( rotatedFaces[0].vaxis[0] - 1.0f ) < 0.0001f &&
				rotatedFaces[0].uaxis[3] == 24 && rotatedFaces[0].vaxis[3] == 13;
		rotationTable->item( 0, 4 )->setText( QStringLiteral( "2 0 0 13" ) );
		QList<SideLite> invalidRotation;
		bFaces = bFaces && !rotationDialog.ReadFaces( invalidRotation );
	}
	else bFaces = false;
	bFaces = bFaces && faceDoc.SetSelectedFaceProperties( rotatedFaces ) && faceDoc.Undo() &&
		faceDoc.Map().SolidById( faceBrush )->sides[0].rotation == beforeFaces[0].rotation && faceDoc.Redo() &&
		faceDoc.Map().SolidById( faceBrush )->sides[0].rotation == 90.0f;
	QTemporaryDir faceDir;
	const QString facePath = faceDir.filePath( QStringLiteral( "rotated.vmf" ) );
	CHammerDocument faceReload;
	bFaces = bFaces && faceDir.isValid() && faceDoc.SaveToFile( facePath ) && faceReload.LoadFromFile( facePath );
	if ( bFaces )
	{
		const SolidLite *reloaded = faceReload.Map().SolidById( faceBrush );
		bFaces = reloaded && !reloaded->sides.isEmpty() && reloaded->sides[0].rotation == 90.0f &&
			std::abs( reloaded->sides[0].uaxis[1] + 1.0f ) < 0.0001f && reloaded->sides[0].uaxis[3] == 24;
	}
	ctx.Step( "face-properties", bFaces, QStringLiteral( "staged per-face edits, validation, cache preservation and undo/redo" ) );

	// Batch application affects only selected rows and remains staged until OK.
	CHammerDocument batchDoc;
	const int batchBrush = batchDoc.CreateBrush( faceLo, faceHi );
	batchDoc.SelectObject( batchBrush );
	const QList<SideLite> batchBefore = batchDoc.Map().SolidById( batchBrush )->sides;
	CFacePropertiesDialog batchDialog( batchBefore, nullptr, QStringLiteral( "brick/batch" ) );
	QTableWidget *batchTable = batchDialog.findChild<QTableWidget *>( QStringLiteral( "facePropertiesTable" ) );
	QLineEdit *batchMaterial = batchDialog.findChild<QLineEdit *>( QStringLiteral( "faceBatchMaterial" ) );
	QPushButton *batchApply = batchDialog.findChild<QPushButton *>( QStringLiteral( "faceBatchApply" ) );
	bool batchOk = batchTable && batchMaterial && batchApply && batchMaterial->text() == QLatin1String( "brick/batch" ) && !batchApply->isEnabled();
	QList<SideLite> batchFaces;
	if ( batchOk )
	{
		batchTable->selectionModel()->select( batchTable->model()->index( 0, 0 ), QItemSelectionModel::Select | QItemSelectionModel::Rows );
		batchTable->selectionModel()->select( batchTable->model()->index( 2, 0 ), QItemSelectionModel::Select | QItemSelectionModel::Rows );
		batchOk = batchApply->isEnabled();
		batchApply->click();
		batchOk = batchOk && batchDialog.ReadFaces( batchFaces ) && batchFaces[0].texture == QLatin1String( "brick/batch" ) &&
			batchFaces[2].texture == QLatin1String( "brick/batch" ) && batchFaces[1].texture == batchBefore[1].texture &&
			batchFaces[0].uaxis[3] == batchBefore[0].uaxis[3] && batchDoc.Map().SolidById( batchBrush )->sides[0].texture == batchBefore[0].texture;
		batchDialog.reject();
		batchOk = batchOk && batchDoc.Map().SolidById( batchBrush )->sides[0].texture == batchBefore[0].texture &&
			batchDoc.SetSelectedFaceProperties( batchFaces ) && batchDoc.Undo() &&
			batchDoc.Map().SolidById( batchBrush )->sides[0].texture == batchBefore[0].texture && batchDoc.Redo() &&
			batchDoc.Map().SolidById( batchBrush )->sides[2].texture == QLatin1String( "brick/batch" );
	}
	ctx.Step( "face-batch-material", batchOk, QStringLiteral( "browser material, multiple selected faces, staging and undo/redo" ) );
	// A single staged face editor may apply to several selected brushes, but a
	// stale selection, hidden brush or invalid face must reject the whole edit.
	CHammerDocument multiMaterialDoc;
	const int materialBrushA = multiMaterialDoc.CreateBrush( faceLo, faceHi );
	double materialLo[3] = { faceLo[0] + 128, faceLo[1], faceLo[2] };
	double materialHi[3] = { faceHi[0] + 128, faceHi[1], faceHi[2] };
	const int materialBrushB = multiMaterialDoc.CreateBrush( materialLo, materialHi );
	multiMaterialDoc.SelectObjects( QSet<int>{ materialBrushA, materialBrushB } );
	const QList<SideLite> materialBeforeA = multiMaterialDoc.Map().SolidById( materialBrushA )->sides;
	const QList<SideLite> materialBeforeB = multiMaterialDoc.Map().SolidById( materialBrushB )->sides;
	QList<SideLite> materialCombined = materialBeforeA; materialCombined.append( materialBeforeB );
	QStringList materialLabels;
	for ( int i = 0; i < materialBeforeA.size(); ++i ) materialLabels.append( QStringLiteral( "A %1" ).arg( i ) );
	for ( int i = 0; i < materialBeforeB.size(); ++i ) materialLabels.append( QStringLiteral( "B %1" ).arg( i ) );
	CFacePropertiesDialog multiMaterialDialog( materialCombined, nullptr, QStringLiteral( "brick/multi" ), materialLabels );
	QTableWidget *multiMaterialTable = multiMaterialDialog.findChild<QTableWidget *>( QStringLiteral( "facePropertiesTable" ) );
	QList<SideLite> materialEdited;
	bool multiMaterialOk = multiMaterialDoc.SelectedVisibleSolidIds().size() == 2 && multiMaterialTable &&
		multiMaterialTable->item( materialBeforeA.size(), 0 )->text() == materialLabels[materialBeforeA.size()];
	if ( multiMaterialOk )
	{
		multiMaterialTable->selectionModel()->select( multiMaterialTable->model()->index( 0, 0 ), QItemSelectionModel::Select | QItemSelectionModel::Rows );
		multiMaterialTable->selectionModel()->select( multiMaterialTable->model()->index( materialBeforeA.size(), 0 ), QItemSelectionModel::Select | QItemSelectionModel::Rows );
		multiMaterialDialog.ApplyMaterialToSelected( QStringLiteral( "brick/multi" ) );
		multiMaterialOk = multiMaterialDialog.ReadFaces( materialEdited );
	}
	QMap<int, QList<SideLite>> materialChanges;
	if ( multiMaterialOk )
	{
		materialChanges.insert( materialBrushA, materialEdited.mid( 0, materialBeforeA.size() ) );
		materialChanges.insert( materialBrushB, materialEdited.mid( materialBeforeA.size() ) );
		QMap<int, QList<SideLite>> invalidChanges = materialChanges;
		invalidChanges[materialBrushB][0].uscale = 0;
		multiMaterialOk = !multiMaterialDoc.SetSelectedSolidsFaceProperties( invalidChanges ) &&
			multiMaterialDoc.Map().SolidById( materialBrushA )->sides[0].texture == materialBeforeA[0].texture &&
			multiMaterialDoc.SetSelectedSolidsFaceProperties( materialChanges ) &&
			multiMaterialDoc.Map().SolidById( materialBrushA )->sides[0].texture == QLatin1String( "brick/multi" ) &&
			multiMaterialDoc.Map().SolidById( materialBrushB )->sides[0].texture == QLatin1String( "brick/multi" ) &&
			multiMaterialDoc.Undo() && multiMaterialDoc.Map().SolidById( materialBrushA )->sides[0].texture == materialBeforeA[0].texture &&
			multiMaterialDoc.Map().SolidById( materialBrushB )->sides[0].texture == materialBeforeB[0].texture && multiMaterialDoc.Redo();
		// The second selected object may instead be an entity owning the brush.
		EntityLite materialOwner; materialOwner.id = multiMaterialDoc.Map().AllocId(); materialOwner.classname = QStringLiteral( "func_detail" );
		multiMaterialDoc.Map().entities.append( materialOwner );
		multiMaterialDoc.Map().SolidById( materialBrushB )->ownerEntityId = materialOwner.id;
		multiMaterialDoc.SelectObjects( QSet<int>{ materialBrushA, materialOwner.id } );
		materialChanges[materialBrushA][0].texture = QStringLiteral( "brick/mixed" );
		materialChanges[materialBrushB][0].texture = QStringLiteral( "brick/mixed" );
		multiMaterialOk = multiMaterialOk && multiMaterialDoc.SelectedVisibleSolidIds().size() == 2 &&
			multiMaterialDoc.SetSelectedSolidsFaceProperties( materialChanges ) &&
			multiMaterialDoc.Map().SolidById( materialBrushB )->ownerEntityId == materialOwner.id &&
			multiMaterialDoc.Undo() && multiMaterialDoc.Map().SolidById( materialBrushB )->sides[0].texture == QLatin1String( "brick/multi" );
		ViewFilter materialFilter = multiMaterialDoc.GetViewFilter(); materialFilter.quickHiddenIds.insert( materialBrushB );
		multiMaterialDoc.SetViewFilter( materialFilter );
		multiMaterialOk = multiMaterialOk && !multiMaterialDoc.SetSelectedSolidsFaceProperties( materialChanges );
	}
	ctx.Step( "face-multiple-brushes", multiMaterialOk, QStringLiteral( "multiple brushes staged and applied atomically, validation, hidden guard and undo/redo" ) );
	CFacePropertiesDialog selectionDialog( batchBefore, nullptr, QStringLiteral( "brick/batch" ) );
	QTableWidget *selectionTable = selectionDialog.findChild<QTableWidget *>( QStringLiteral( "facePropertiesTable" ) );
	QPushButton *selectAllFaces = selectionDialog.findChild<QPushButton *>( QStringLiteral( "faceSelectAll" ) );
	QPushButton *invertFaces = selectionDialog.findChild<QPushButton *>( QStringLiteral( "faceInvertSelection" ) );
	QPushButton *clearFaces = selectionDialog.findChild<QPushButton *>( QStringLiteral( "faceClearSelection" ) );
	QPushButton *selectionApply = selectionDialog.findChild<QPushButton *>( QStringLiteral( "faceBatchApply" ) );
	bool selectionOk = selectionTable && selectAllFaces && invertFaces && clearFaces && selectionApply;
	if ( selectionOk )
	{
		selectAllFaces->click();
		selectionOk = selectionTable->selectionModel()->selectedRows().size() == batchBefore.size() && selectionApply->isEnabled();
		invertFaces->click();
		selectionOk = selectionOk && selectionTable->selectionModel()->selectedRows().isEmpty() && !selectionApply->isEnabled();
		selectionTable->selectRow( 1 );
		invertFaces->click();
		selectionOk = selectionOk && selectionTable->selectionModel()->selectedRows().size() == batchBefore.size() - 1 &&
			!selectionTable->selectionModel()->isRowSelected( 1, QModelIndex() );
		clearFaces->click();
		selectionOk = selectionOk && selectionTable->selectionModel()->selectedRows().isEmpty() && !selectionApply->isEnabled();
	}
	ctx.Step( "face-selection-controls", selectionOk, QStringLiteral( "select all, invert and clear face rows for batch operations" ) );

	// Alignment follows CMapFace orientation tables while retaining shifts,
	// scales and staged changes until the user accepts the dialog.
	QList<SideLite> alignInput = beforeFaces;
	SideLite &sloped = alignInput[0];
	for ( int p = 0; p < 3; ++p ) for ( int axis = 0; axis < 3; ++axis ) sloped.planePts[p][axis] = 0;
	sloped.planePts[1][0] = 1; sloped.planePts[2][1] = -2; sloped.planePts[2][2] = 1;
	sloped.rotation = 0; sloped.uaxis[3] = 23; sloped.vaxis[3] = 17;
	sloped.uscale = .5f; sloped.vscale = .25f;
	CFacePropertiesDialog alignDialog( alignInput );
	QTableWidget *alignTable = alignDialog.findChild<QTableWidget *>( QStringLiteral( "facePropertiesTable" ) );
	QPushButton *alignWorld = alignDialog.findChild<QPushButton *>( QStringLiteral( "faceAlignWorld" ) );
	QPushButton *alignFace = alignDialog.findChild<QPushButton *>( QStringLiteral( "faceAlignFace" ) );
	QList<SideLite> aligned;
	bool alignmentOk = alignTable && alignWorld && alignFace;
	if ( alignmentOk )
	{
		alignTable->selectRow( 0 ); alignWorld->click();
		alignmentOk = alignDialog.ReadFaces( aligned ) && aligned.size() == alignInput.size() &&
			std::abs( aligned[0].uaxis[0] - 1 ) < 1e-6 && std::abs( aligned[0].vaxis[1] + 1 ) < 1e-6 &&
			aligned[0].uaxis[3] == 23 && aligned[0].vaxis[3] == 17 && aligned[0].uscale == .5f &&
			aligned[1].uaxis[0] == alignInput[1].uaxis[0];
		alignFace->click();
		alignmentOk = alignmentOk && alignDialog.ReadFaces( aligned ) &&
			std::abs( aligned[0].uaxis[0] - 1 ) < 1e-6 && std::abs( aligned[0].vaxis[1] + 0.89442719 ) < 1e-5 &&
			std::abs( aligned[0].vaxis[2] - 0.44721360 ) < 1e-5 && aligned[0].vaxis[3] == 17;
		alignTable->item( 0, 6 )->setText( QStringLiteral( "90" ) );
		alignFace->click();
		QList<SideLite> alignedRotation;
		alignmentOk = alignmentOk && alignDialog.ReadFaces( alignedRotation ) &&
			std::abs( alignedRotation[0].rotation - 90 ) < 1e-6 &&
			std::abs( alignedRotation[0].uaxis[0] ) < 1e-5;
		const float priorRotation = faceDoc.Map().SolidById( faceBrush )->sides[0].rotation;
		alignmentOk = alignmentOk && faceDoc.SetSelectedFaceProperties( aligned ) &&
			faceDoc.Map().SolidById( faceBrush )->sides[0].rotation == 0 && faceDoc.Undo() &&
			faceDoc.Map().SolidById( faceBrush )->sides[0].rotation == priorRotation && faceDoc.Redo() &&
			faceDoc.Map().SolidById( faceBrush )->sides[0].rotation == 0;
	}
	ctx.Step( "face-texture-alignment", alignmentOk, QStringLiteral( "world and sloped face axes, shifts, scales, staged rotation and undo" ) );
	QList<SideLite> badAlignInput = alignInput;
	for ( auto &point : badAlignInput[1].planePts ) for ( double &coordinate : point ) coordinate = 0;
	CFacePropertiesDialog badAlignDialog( badAlignInput );
	QTableWidget *badAlignTable = badAlignDialog.findChild<QTableWidget *>( QStringLiteral( "facePropertiesTable" ) );
	QPushButton *badAlignButton = badAlignDialog.findChild<QPushButton *>( QStringLiteral( "faceAlignFace" ) );
	bool badAlignmentOk = badAlignTable && badAlignButton;
	if ( badAlignmentOk )
	{
		badAlignTable->selectRow( 0 );
		badAlignTable->selectionModel()->select( badAlignTable->model()->index( 1, 0 ), QItemSelectionModel::Select | QItemSelectionModel::Rows );
		const QString beforeU = badAlignTable->item( 0, 2 )->text();
		const QString beforeV = badAlignTable->item( 0, 4 )->text();
		bool warningShown = false;
		QTimer::singleShot( 0, [&warningShown] {
			if ( QMessageBox *box = qobject_cast<QMessageBox *>( QApplication::activeModalWidget() ) )
			{ warningShown = !box->text().isEmpty(); box->accept(); }
		} );
		badAlignButton->click();
		badAlignmentOk = warningShown && badAlignTable->item( 0, 2 )->text() == beforeU &&
			badAlignTable->item( 0, 4 )->text() == beforeV;
	}
	ctx.Step( "face-texture-alignment-invalid", badAlignmentOk, QStringLiteral( "invalid face warns and leaves all selected rows unchanged" ) );

	// VMF view settings follow the document and preserve other keys.
	const QByteArray viewFixture = "viewsettings { \"bSnapToGrid\" \"0\" \"bShowGrid\" \"0\" \"nGridSpacing\" \"64\" \"extra\" \"keep\" } world { \"id\" \"1\" \"classname\" \"worldspawn\" }";
	QTemporaryDir viewDir;
	const QString viewInput = viewDir.filePath( "view_in.vmf" ), viewOutput = viewDir.filePath( "view_out.vmf" );
	QFile viewFile( viewInput );
	bool bViewSettings = viewFile.open( QIODevice::WriteOnly ) && viewFile.write( viewFixture ) == viewFixture.size();
	viewFile.close();
	CHammerDocument viewDoc;
	bViewSettings = bViewSettings && viewDoc.LoadFromFile( viewInput ) && !viewDoc.IsDirty() &&
		viewDoc.GridSize() == 64 && !viewDoc.SnapEnabled() && !viewDoc.GridVisible();
	viewDoc.SetGridSize( 32 ); viewDoc.SetSnapEnabled( true ); viewDoc.SetGridVisible( true );
	bViewSettings = bViewSettings && !viewDoc.IsDirty() && viewDoc.SaveToFile( viewOutput );
	CHammerDocument viewReload;
	bViewSettings = bViewSettings && viewReload.LoadFromFile( viewOutput ) && viewReload.GridSize() == 32 &&
		viewReload.SnapEnabled() && viewReload.GridVisible() && !viewReload.IsDirty();
	bool foundExtra = false;
	for ( const VmfMetadataBlock &block : viewReload.Map().extraMetadata )
		for ( const auto &key : block.properties ) if ( key.first == QLatin1String( "extra" ) && key.second == QLatin1String( "keep" ) ) foundExtra = true;
	bViewSettings = bViewSettings && foundExtra;
	viewDoc.SetGridSize( 128 );
	bViewSettings = bViewSettings && !viewDoc.IsDirty() && viewDoc.SaveDocument();
	CHammerDocument viewSavedAgain;
	bViewSettings = bViewSettings && viewSavedAgain.LoadFromFile( viewOutput ) && viewSavedAgain.GridSize() == 128;
	viewReload.NewDocument();
	bViewSettings = bViewSettings && viewReload.GridSize() == 16 && viewReload.SnapEnabled() && viewReload.GridVisible();
	ctx.Step( "vmf-viewsettings", bViewSettings, QStringLiteral( "grid state load/save, unknown keys and clean state" ) );

	// Paste Special remembers only accepted options; invalid stored values are
	// reset independently so corrupt settings cannot exceed dialog limits.
	QTemporaryDir pasteSettingsDir;
	QSettings pasteSettings( pasteSettingsDir.filePath( QStringLiteral( "settings.ini" ) ), QSettings::IniFormat );
	PasteSpecialSettings chosenPaste;
	chosenPaste.copies = 12;
	chosenPaste.offsets[0] = -123;
	chosenPaste.offsets[1] = 64;
	chosenPaste.offsets[2] = 32768;
	chosenPaste.centerViews = true;
	SavePasteSpecialSettings( pasteSettings, chosenPaste );
	pasteSettings.sync();
	QSettings reloadedPasteSettings( pasteSettingsDir.filePath( QStringLiteral( "settings.ini" ) ), QSettings::IniFormat );
	const PasteSpecialSettings restoredPaste = LoadPasteSpecialSettings( reloadedPasteSettings );
	bool bPasteSettings = pasteSettingsDir.isValid() && reloadedPasteSettings.status() == QSettings::NoError &&
		restoredPaste.copies == 12 && restoredPaste.offsets[0] == -123 && restoredPaste.offsets[1] == 64 &&
		restoredPaste.offsets[2] == 32768 && restoredPaste.centerViews;
	reloadedPasteSettings.setValue( QStringLiteral( "pasteSpecial/copies" ), 0 );
	reloadedPasteSettings.setValue( QStringLiteral( "pasteSpecial/offsetX" ), QStringLiteral( "bad" ) );
	reloadedPasteSettings.setValue( QStringLiteral( "pasteSpecial/offsetY" ), 32769 );
	const PasteSpecialSettings badPaste = LoadPasteSpecialSettings( reloadedPasteSettings );
	bPasteSettings = bPasteSettings && badPaste.copies == 1 && badPaste.offsets[0] == 0 && badPaste.offsets[1] == 0 &&
		badPaste.offsets[2] == 32768 && badPaste.centerViews;
	ctx.Step( "paste-special-settings", bPasteSettings, QStringLiteral( "accepted settings persist; invalid values reset safely" ) );

	// Basic map checks are read-only and identify the affected brush.
	MapDocLite problemMap;
	QList<MapCheckIssue> problems = CheckMapProblems( problemMap );
	bool bMapCheck = problems.size() == 1 && problems[0].kind == MapCheckKind::NoPlayerStart;
	EntityLite player;
	player.id = problemMap.AllocId(); player.classname = QStringLiteral( "INFO_PLAYER_START" );
	problemMap.entities.append( player );
	const double checkLo[3] = { 0, 0, 0 }, checkHi[3] = { 64, 64, 64 };
	const int checkSolid = CreateBoxSolid( problemMap, checkLo, checkHi );
	bMapCheck = bMapCheck && CheckMapProblems( problemMap ).isEmpty();
	SolidLite *badSolid = problemMap.SolidById( checkSolid );
	// Multiple unsaved, unallocated face IDs are valid and assigned on save.
	badSolid->sides[0].id = 0;
	badSolid->sides[1].id = 0;
	bMapCheck = bMapCheck && CheckMapProblems( problemMap ).isEmpty();
	// Original Hammer rejects nonzero, parallel U/V axes and axes that do
	// not span the face, while a degenerate face has no normal to check.
	const SideLite originalSide = badSolid->sides[0];
	for ( int i = 0; i < 3; ++i ) badSolid->sides[0].vaxis[i] = badSolid->sides[0].uaxis[i];
	problems = CheckMapProblems( problemMap );
	bool parallelRejected = false;
	for ( const auto &issue : problems ) parallelRejected |= issue.kind == MapCheckKind::InvalidTextureAxes && issue.solidId == checkSolid && issue.faceId == 0;
	bMapCheck = bMapCheck && parallelRejected;
	badSolid->sides[0] = originalSide;
	{
		const SideLite &s = originalSide;
		double a[3], b[3];
		for ( int i = 0; i < 3; ++i ) { a[i] = s.planePts[1][i] - s.planePts[0][i]; b[i] = s.planePts[2][i] - s.planePts[0][i]; }
		for ( int i = 0; i < 3; ++i ) badSolid->sides[0].uaxis[i] = float( a[( i + 1 ) % 3] * b[( i + 2 ) % 3] - a[( i + 2 ) % 3] * b[( i + 1 ) % 3] );
		for ( int i = 0; i < 3; ++i ) badSolid->sides[0].vaxis[i] = float( a[i] );
		problems = CheckMapProblems( problemMap );
		bool perpendicularRejected = false;
		for ( const auto &issue : problems ) perpendicularRejected |= issue.kind == MapCheckKind::InvalidTextureAxes && issue.solidId == checkSolid && issue.faceId == 0;
		bMapCheck = bMapCheck && perpendicularRejected;
	}
	badSolid->sides[0] = originalSide;
	for ( int i = 0; i < 3; ++i ) badSolid->sides[0].planePts[1][i] = badSolid->sides[0].planePts[0][i];
	problems = CheckMapProblems( problemMap );
	bool badGeometry = false, badTexture = false;
	for ( const auto &issue : problems )
	{
		badGeometry |= issue.kind == MapCheckKind::UnreconstructableSolid && issue.solidId == checkSolid;
		badTexture |= issue.kind == MapCheckKind::InvalidTextureAxes;
	}
	bMapCheck = bMapCheck && badGeometry && !badTexture;
	badSolid->sides[0] = originalSide;
	// A missing bounding plane leaves an unbounded solid and must be reported.
	const SideLite removedSide = badSolid->sides.takeLast();
	problems = CheckMapProblems( problemMap );
	badGeometry = false;
	for ( const auto &issue : problems ) badGeometry |= issue.kind == MapCheckKind::UnreconstructableSolid && issue.solidId == checkSolid;
	bMapCheck = bMapCheck && badGeometry;
	badSolid->sides.append( removedSide );
	badSolid->sides[0].id = 9999;
	badSolid->sides[0].uscale = 0;
	badSolid->sides[1].id = badSolid->sides[0].id;
	problems = CheckMapProblems( problemMap );
	bool foundAxes = false, foundDuplicate = false;
	for ( const MapCheckIssue &issue : problems )
	{
		foundAxes |= issue.kind == MapCheckKind::InvalidTextureAxes && issue.solidId == checkSolid;
		foundDuplicate |= issue.kind == MapCheckKind::DuplicateFaceId && issue.solidId == checkSolid;
	}
	bMapCheck = bMapCheck && foundAxes && foundDuplicate && badSolid->sides[0].uscale == 0;
	ctx.Step( "map-problems", bMapCheck, QStringLiteral( "advisory spawn, invalid axes, duplicate face ID and read-only behavior" ) );

	// Duplicate normals are flagged even if the planes have different offsets,
	// as in the original Hammer; opposite normals are valid box faces.
	MapDocLite duplicatePlaneMap;
	duplicatePlaneMap.entities.append( player );
	const int duplicateBrush = CreateBoxSolid( duplicatePlaneMap, checkLo, checkHi );
	SolidLite *duplicateSolid = duplicatePlaneMap.SolidById( duplicateBrush );
	bool bDuplicatePlane = CheckMapProblems( duplicatePlaneMap ).isEmpty();
	SideLite parallelFace = duplicateSolid->sides[0];
	parallelFace.id = duplicatePlaneMap.AllocId();
	for ( int p = 0; p < 3; ++p ) parallelFace.planePts[p][0] += 16;
	duplicateSolid->sides.append( parallelFace );
	int duplicateCount = 0;
	for ( const auto &issue : CheckMapProblems( duplicatePlaneMap ) )
		if ( issue.kind == MapCheckKind::DuplicatePlaneNormals && issue.solidId == duplicateBrush ) ++duplicateCount;
	bDuplicatePlane = bDuplicatePlane && duplicateCount == 1;
	ViewFilter hiddenBrushFilter;
	hiddenBrushFilter.showBrushes = false;
	for ( const auto &issue : CheckMapProblems( duplicatePlaneMap, &hiddenBrushFilter ) )
		if ( issue.kind == MapCheckKind::DuplicatePlaneNormals ) bDuplicatePlane = false;
	ctx.Step( "map-duplicate-plane-normals", bDuplicatePlane,
		QStringLiteral( "same-facing parallel faces, opposite faces and visible-only filter" ) );

	// Match Hammer's duplicate node check: ignore zero, invalid values and
	// info_node_link, and report both nodes involved in a duplicate.
	MapDocLite nodeMap;
	EntityLite nodeSpawn;
	nodeSpawn.id = nodeMap.AllocId(); nodeSpawn.classname = QStringLiteral( "info_player_start" );
	nodeMap.entities.append( nodeSpawn );
	const auto addTestNode = [&]( const QString &className, const QString &key, const QString &value ) {
		EntityLite entity;
		entity.id = nodeMap.AllocId(); entity.classname = className;
		entity.keyvalues.insert( key, value );
		nodeMap.entities.append( entity );
		return entity.id;
	};
	const int firstNode = addTestNode( QStringLiteral( "info_node" ), QStringLiteral( "nodeid" ), QStringLiteral( "7" ) );
	const int secondNode = addTestNode( QStringLiteral( "INFO_NODE_AIR" ), QStringLiteral( "NodeID" ), QStringLiteral( "007" ) );
	addTestNode( QStringLiteral( "info_node_link" ), QStringLiteral( "nodeid" ), QStringLiteral( "7" ) );
	addTestNode( QStringLiteral( "info_node_hint" ), QStringLiteral( "nodeid" ), QStringLiteral( "0" ) );
	addTestNode( QStringLiteral( "info_node_hint" ), QStringLiteral( "nodeid" ), QStringLiteral( "invalid" ) );
	const auto nodeIssues = CheckMapProblems( nodeMap );
	QSet<int> duplicateNodes;
	for ( const MapCheckIssue &issue : nodeIssues )
		if ( issue.kind == MapCheckKind::DuplicateNodeId && issue.faceId == 7 ) duplicateNodes.insert( issue.solidId );
	CHammerDocument nodeDoc;
	nodeDoc.Map() = nodeMap;
	double nodeCenter[3] = {};
	const bool nodeCheckOk = nodeIssues.size() == 2 && duplicateNodes == ( QSet<int>() << firstNode << secondNode ) &&
		MapCheckGoToIssue( nodeDoc, nodeIssues.first(), nodeCenter ) && nodeDoc.IsSelected( nodeIssues.first().solidId );
	ctx.Step( "map-duplicate-node-ids", nodeCheckOk, QStringLiteral( "both nodes, class/key case, link and invalid IDs" ) );

	// Visible-only checking must use the same object set for duplicate counting
	// and issue reporting, while the default check still examines the whole map.
	ViewFilter visibleNodes;
	visibleNodes.classFilter = QStringLiteral( "info_node_air" );
	const auto visibleNodeIssues = CheckMapProblems( nodeMap, &visibleNodes );
	bool visibleCheckOk = visibleNodeIssues.size() == 1 && visibleNodeIssues[0].kind == MapCheckKind::NoPlayerStart &&
		CheckMapProblems( nodeMap ).size() == 2;
	ViewFilter hiddenBrushes;
	hiddenBrushes.showBrushes = false;
	const auto hiddenBrushIssues = CheckMapProblems( problemMap, &hiddenBrushes );
	for ( const auto &issue : hiddenBrushIssues )
		visibleCheckOk = visibleCheckOk && issue.solidId != checkSolid;
	ViewFilter hiddenEntities;
	hiddenEntities.showEntities = false;
	const auto hiddenEntityIssues = CheckMapProblems( nodeMap, &hiddenEntities );
	visibleCheckOk = visibleCheckOk && hiddenEntityIssues.size() == 1 && hiddenEntityIssues[0].kind == MapCheckKind::NoPlayerStart;
	ctx.Step( "map-check-visible-only", visibleCheckOk,
		QStringLiteral( "visible spawn, duplicate node counts, hidden brushes and full-map default" ) );

	// I/O validation deliberately accepts runtime targets, default input and
	// ambiguous raw formats, and reports concrete malformed fields as advisories.
	MapDocLite ioProblemMap;
	EntityLite ioPlayer;
	ioPlayer.id = ioProblemMap.AllocId(); ioPlayer.classname = QStringLiteral( "info_player_start" );
	ioPlayer.connections.append( qMakePair( QStringLiteral( "OnTrigger" ), QStringLiteral( "!activator,,,,-1" ) ) );
	ioPlayer.connections.append( qMakePair( QStringLiteral( "OnTrigger" ), QStringLiteral( "door*,Use,,0,1" ) ) );
	ioPlayer.connections.append( qMakePair( QStringLiteral( "OnTrigger" ), QStringLiteral( "custom,unknown,format" ) ) );
	ioProblemMap.entities.append( ioPlayer );
	EntityLite ioDoor;
	ioDoor.id = ioProblemMap.AllocId(); ioDoor.classname = QStringLiteral( "func_door" );
	ioDoor.keyvalues.insert( QStringLiteral( "targetname" ), QStringLiteral( "DoorA" ) );
	ioProblemMap.entities.append( ioDoor );
	bool bIoCheck = CheckMapProblems( ioProblemMap ).isEmpty();
	ioProblemMap.entities[0].connections.append( qMakePair( QStringLiteral( "OnTrigger" ), QStringLiteral( ",Use,,bad,nope" ) ) );
	const auto ioProblems = CheckMapProblems( ioProblemMap );
	bool emptyTarget = false, invalidDelay = false, invalidCount = false;
	for ( const MapCheckIssue &issue : ioProblems )
	{
		emptyTarget |= issue.kind == MapCheckKind::OutputEmptyTarget && issue.solidId == ioPlayer.id && issue.faceId == 4;
		invalidDelay |= issue.kind == MapCheckKind::OutputInvalidDelay && issue.solidId == ioPlayer.id && issue.faceId == 4;
		invalidCount |= issue.kind == MapCheckKind::OutputInvalidCount && issue.solidId == ioPlayer.id && issue.faceId == 4;
	}
	bIoCheck = bIoCheck && ioProblems.size() == 3 && emptyTarget && invalidDelay && invalidCount &&
		ioProblemMap.entities[0].connections[3].second == QLatin1String( ",Use,,bad,nope" );
	ctx.Step( "map-output-advisories", bIoCheck, QStringLiteral( "safe I/O diagnostics and unchanged raw connections" ) );

	MapDocLite targetCheck;
	EntityLite source;
	source.id = targetCheck.AllocId(); source.classname = QStringLiteral( "info_player_start" );
	source.connections.append( qMakePair( QStringLiteral( "OnTrigger" ), QStringLiteral( "DOORa,Use,,0,-1" ) ) );
	source.connections.append( qMakePair( QStringLiteral( "OnTrigger" ), QStringLiteral( "door*ignored,Use,,0,-1" ) ) );
	source.connections.append( qMakePair( QStringLiteral( "OnTrigger" ), QStringLiteral( "!runtime_custom,Use,,0,-1" ) ) );
	source.connections.append( qMakePair( QStringLiteral( "OnTrigger" ), QStringLiteral( "missing,Use,,0,-1" ) ) );
	targetCheck.entities.append( source );
	ioDoor.id = targetCheck.AllocId(); targetCheck.entities.append( ioDoor );
	const auto targetIssues = CheckMapProblems( targetCheck );
	bool missingTargetOk = targetIssues.size() == 1 && targetIssues[0].kind == MapCheckKind::OutputMissingTarget &&
		targetIssues[0].solidId == source.id && targetIssues[0].faceId == 4;
	ViewFilter targetVisible;
	targetVisible.classFilter = QStringLiteral( "info_player_start" );
	const auto visibleTargetIssues = CheckMapProblems( targetCheck, &targetVisible );
	int hiddenTargetWarnings = 0;
	for ( const auto &issue : visibleTargetIssues ) if ( issue.kind == MapCheckKind::OutputMissingTarget ) ++hiddenTargetWarnings;
	missingTargetOk = missingTargetOk && hiddenTargetWarnings == 3 &&
		targetCheck.entities[0].connections[3].second == QLatin1String( "missing,Use,,0,-1" );
	ctx.Step( "map-output-missing-target", missingTargetOk,
		QStringLiteral( "literal and wildcard target names, runtime targets, visible-only and unchanged data" ) );

	// Original default 3D navigation bindings: movement is relative to the camera,
	// arrow rotation keeps the eye stationary, and modifiers do not alter the map.
	CHammerDocument cameraKeyDoc;
	CQtViewport3D keyView;
	keyView.setDocument( &cameraKeyDoc );
	const double keyEye[3] = { 20, 30, 40 }, keyAngles[3] = { 0, 0, 0 };
	keyView.setCameraPositionAngles( keyEye, keyAngles );
	double eyeBefore[3], eyeAfter[3], fwdBefore[3], fwdAfter[3], upBefore[3], upAfter[3];
	keyView.cameraBasis( eyeBefore, fwdBefore, upBefore );
	QKeyEvent forwardKey( QEvent::KeyPress, Qt::Key_D, Qt::NoModifier );
	QCoreApplication::sendEvent( &keyView, &forwardKey );
	keyView.cameraBasis( eyeAfter, fwdAfter, upAfter );
	bool bCameraKeys = std::abs( eyeAfter[0] - eyeBefore[0] - 32.0 ) < 1e-6 &&
		std::abs( eyeAfter[1] - eyeBefore[1] ) < 1e-6 && std::abs( eyeAfter[2] - eyeBefore[2] ) < 1e-6;
	QKeyEvent backKey( QEvent::KeyPress, Qt::Key_C, Qt::NoModifier );
	QCoreApplication::sendEvent( &keyView, &backKey );
	QKeyEvent turnKey( QEvent::KeyPress, Qt::Key_Left, Qt::NoModifier );
	QCoreApplication::sendEvent( &keyView, &turnKey );
	keyView.cameraBasis( eyeAfter, fwdAfter, upAfter );
	bCameraKeys = bCameraKeys && std::abs( eyeAfter[0] - eyeBefore[0] ) < 1e-6 &&
		std::abs( eyeAfter[1] - eyeBefore[1] ) < 1e-6 && fwdAfter[1] > fwdBefore[1];
	QKeyEvent pitchKey( QEvent::KeyPress, Qt::Key_Up, Qt::NoModifier );
	QCoreApplication::sendEvent( &keyView, &pitchKey );
	keyView.cameraBasis( eyeBefore, fwdBefore, upBefore );
	bCameraKeys = bCameraKeys && fwdBefore[2] > 0;
	QKeyEvent upKey( QEvent::KeyPress, Qt::Key_Up, Qt::ShiftModifier );
	QCoreApplication::sendEvent( &keyView, &upKey );
	keyView.cameraBasis( eyeAfter, fwdAfter, upAfter );
	bCameraKeys = bCameraKeys && eyeAfter[2] > eyeBefore[2] && !cameraKeyDoc.IsDirty() && !cameraKeyDoc.Undo();
	QKeyEvent ctrlKey( QEvent::KeyPress, Qt::Key_Left, Qt::ControlModifier );
	QCoreApplication::sendEvent( &keyView, &ctrlKey );
	keyView.cameraBasis( eyeBefore, fwdBefore, upBefore );
	bCameraKeys = bCameraKeys && std::abs( eyeAfter[0] - eyeBefore[0] ) < 1e-6 &&
		std::abs( fwdAfter[0] - fwdBefore[0] ) < 1e-6;
	ctx.Step( "3d-keyboard-navigation", bCameraKeys, QStringLiteral( "default camera keys, eye position, modifiers and document state" ) );
	// Mouselook keeps the eye fixed while turning and releases its cursor on
	// Escape or focus loss. Shift+Z remains available to the main-window shortcut.
	keyView.setCameraPositionAngles( keyEye, keyAngles );
	QKeyEvent toggleLook( QEvent::KeyPress, Qt::Key_Z, Qt::NoModifier );
	QCoreApplication::sendEvent( &keyView, &toggleLook );
	bool bMouseLook = keyView.cursor().shape() == Qt::BlankCursor;
	QKeyEvent repeatLook( QEvent::KeyPress, Qt::Key_Z, Qt::NoModifier, QString(), true );
	QCoreApplication::sendEvent( &keyView, &repeatLook );
	bMouseLook = bMouseLook && keyView.cursor().shape() == Qt::BlankCursor;
	keyView.cameraBasis( eyeBefore, fwdBefore, upBefore );
	const QPoint mouseCenter = keyView.rect().center();
	QMouseEvent lookMove( QEvent::MouseMove, QPointF( mouseCenter + QPoint( 20, 10 ) ), Qt::NoButton, Qt::NoButton, Qt::NoModifier );
	QCoreApplication::sendEvent( &keyView, &lookMove );
	keyView.cameraBasis( eyeAfter, fwdAfter, upAfter );
	bMouseLook = bMouseLook && std::abs( eyeBefore[0] - eyeAfter[0] ) < 1e-6 &&
		std::abs( eyeBefore[1] - eyeAfter[1] ) < 1e-6 && std::abs( eyeBefore[2] - eyeAfter[2] ) < 1e-6 &&
		fwdAfter[1] < fwdBefore[1] && fwdAfter[2] < fwdBefore[2];
	QKeyEvent walkForward( QEvent::KeyPress, Qt::Key_W, Qt::NoModifier );
	QCoreApplication::sendEvent( &keyView, &walkForward );
	keyView.cameraBasis( eyeAfter, fwdAfter, upAfter );
	bMouseLook = bMouseLook && std::abs( eyeAfter[0] - eyeBefore[0] - fwdAfter[0] * 32.0 ) < 1e-6;
	QKeyEvent stopLook( QEvent::KeyPress, Qt::Key_Escape, Qt::NoModifier );
	QCoreApplication::sendEvent( &keyView, &stopLook );
	bMouseLook = bMouseLook && keyView.cursor().shape() != Qt::BlankCursor;
	QCoreApplication::sendEvent( &keyView, &toggleLook );
	QFocusEvent lostFocus( QEvent::FocusOut );
	QCoreApplication::sendEvent( &keyView, &lostFocus );
	bMouseLook = bMouseLook && keyView.cursor().shape() != Qt::BlankCursor && !cameraKeyDoc.IsDirty();
	ctx.Step( "3d-mouselook", bMouseLook, QStringLiteral( "Z toggle and autorepeat, eye-stationary mouse rotation, WASD, Escape and focus loss" ) );
	// The grid toggle changes the software viewport and persists across widgets,
	// without touching the map. Preserve the user's setting after the test.
	QSettings grid3dSettings;
	const QString grid3dKey = QStringLiteral( "view3d/groundGridVisible" );
	const bool grid3dExisted = grid3dSettings.contains( grid3dKey );
	const QVariant grid3dOriginal = grid3dSettings.value( grid3dKey );
	CQtViewport3D grid3dView;
	grid3dView.resize( 640, 480 );
	grid3dView.setDocument( &cameraKeyDoc );
	grid3dView.setGroundGridVisible( true );
	QImage grid3dShown( grid3dView.size(), QImage::Format_ARGB32_Premultiplied );
	grid3dView.render( &grid3dShown );
	grid3dView.setGroundGridVisible( false );
	QImage grid3dHidden( grid3dView.size(), QImage::Format_ARGB32_Premultiplied );
	grid3dView.render( &grid3dHidden );
	CQtViewport3D grid3dRestored;
	const bool grid3dOk = grid3dShown != grid3dHidden && !grid3dRestored.groundGridVisible() &&
		!cameraKeyDoc.IsDirty();
	if ( grid3dExisted ) grid3dSettings.setValue( grid3dKey, grid3dOriginal );
	else grid3dSettings.remove( grid3dKey );
	ctx.Step( "3d-ground-grid-toggle", grid3dOk, QStringLiteral( "software rendering, persistent preference and unchanged document" ) );

	// A malformed VPK must never contribute a partial index.
	QTemporaryDir materialFixture;
	bool bMaterialCatalog = materialFixture.isValid();
	const QString fixtureRoot = materialFixture.path();
	QDir().mkpath( fixtureRoot + QStringLiteral( "/materials/dev" ) );
	QFile looseMaterial( fixtureRoot + QStringLiteral( "/materials/dev/loose.vmt" ) );
	bMaterialCatalog = bMaterialCatalog && looseMaterial.open( QIODevice::WriteOnly );
	looseMaterial.write( "// fixture" ); looseMaterial.close();
	QByteArray tree;
	const auto appendCString = [&tree]( const char *s ) { tree.append( s ); tree.append( '\0' ); };
	appendCString( "vmt" ); appendCString( "materials/dev" ); appendCString( "packed" );
	QByteArray entry( 18, '\0' ); entry[16] = char( 0xff ); entry[17] = char( 0xff ); tree.append( entry );
	tree.append( '\0' ); tree.append( '\0' ); tree.append( '\0' );
	QByteArray header( 12, '\0' );
	qToLittleEndian<quint32>( 0x55aa1234u, reinterpret_cast<uchar *>( header.data() ) );
	qToLittleEndian<quint32>( 1, reinterpret_cast<uchar *>( header.data() + 4 ) );
	qToLittleEndian<quint32>( static_cast<quint32>( tree.size() ), reinterpret_cast<uchar *>( header.data() + 8 ) );
	QFile packedMaterial( fixtureRoot + QStringLiteral( "/fixture_dir.vpk" ) );
	bMaterialCatalog = bMaterialCatalog && packedMaterial.open( QIODevice::WriteOnly );
	packedMaterial.write( header ); packedMaterial.write( tree ); packedMaterial.close();
	const MaterialCatalogResult catalog = ScanGameMaterials( fixtureRoot );
	bMaterialCatalog = bMaterialCatalog && catalog.names.contains( QStringLiteral( "dev/loose" ) ) &&
		catalog.names.contains( QStringLiteral( "dev/packed" ) ) && catalog.failedArchives.isEmpty();
	QFile malformed( fixtureRoot + QStringLiteral( "/bad_dir.vpk" ) );
	bMaterialCatalog = bMaterialCatalog && malformed.open( QIODevice::WriteOnly );
	malformed.write( header ); malformed.write( tree.left( tree.size() - 1 ) ); malformed.close();
	QSet<QString> unchanged; unchanged.insert( QStringLiteral( "existing" ) );
	bMaterialCatalog = bMaterialCatalog && !IndexVpkMaterials( malformed.fileName(), unchanged ) && unchanged.size() == 1;
	ctx.Step( "material-catalog", bMaterialCatalog, QStringLiteral( "loose and VPK indexes; malformed archive is atomic" ) );
	{
		QTemporaryDir resources;
		const QString modelsDir = resources.filePath( QStringLiteral( "models/props" ) );
		bool ok = resources.isValid() && QDir().mkpath( modelsDir );
		QFile modelFile( modelsDir + QStringLiteral( "/loose.mdl" ) );
		ok = ok && modelFile.open( QIODevice::WriteOnly );
		modelFile.close();
		QByteArray sounds;
		for ( const QByteArray &token : { QByteArray( "wav" ), QByteArray( "sound/ambient" ), QByteArray( "wind" ) } )
			sounds.append( token ).append( '\0' );
		sounds.append( entry ).append( QByteArray( 3, '\0' ) );
		QByteArray soundHeader = header;
		qToLittleEndian<quint32>( static_cast<quint32>( sounds.size() ), reinterpret_cast<uchar *>( soundHeader.data() + 8 ) );
		QFile soundArchive( resources.filePath( QStringLiteral( "sounds_dir.vpk" ) ) );
		ok = ok && soundArchive.open( QIODevice::WriteOnly ) &&
			soundArchive.write( soundHeader ) == soundHeader.size() && soundArchive.write( sounds ) == sounds.size();
		soundArchive.close();
		const auto models = ScanGameResources( resources.path(), {}, QStringLiteral( "models" ), { QStringLiteral( "mdl" ) }, true, true );
		const auto audio = ScanGameResources( resources.path(), {}, QStringLiteral( "sound" ), { QStringLiteral( "wav" ) }, false, true );
		QFile fgdFile( resources.filePath( QStringLiteral( "resource.fgd" ) ) );
		ok = ok && fgdFile.open( QIODevice::WriteOnly ) && fgdFile.write(
			"@PointClass = resource_probe : \"Probe\" [ model(studio) : \"Model\" message(sound) : \"Sound\" ]\n" ) > 0;
		fgdFile.close();
		CFgdData resourceFgd;
		ok = ok && resourceFgd.Load( fgdFile.fileName() ) &&
			resourceFgd.PropertyResourceType( QStringLiteral( "resource_probe" ), QStringLiteral( "MODEL" ) ) == CFgdData::ResourceType::StudioModel &&
			resourceFgd.PropertyResourceType( QStringLiteral( "resource_probe" ), QStringLiteral( "message" ) ) == CFgdData::ResourceType::Sound &&
			models.names == QStringList{ QStringLiteral( "models/props/loose.mdl" ) } &&
			audio.names == QStringList{ QStringLiteral( "ambient/wind.wav" ) } &&
			models.failedArchives.isEmpty() && audio.failedArchives.isEmpty();
		ctx.Step( "entity-resource-catalog", ok, QStringLiteral( "FGD studio/sound types, loose model and packed sound relative paths" ) );
	}
	const QString mountedGame = fixtureRoot + QStringLiteral( "/game" );
	QDir().mkpath( mountedGame + QStringLiteral( "/materials/local" ) );
	QDir().mkpath( fixtureRoot + QStringLiteral( "/mounted/addon/materials/shared" ) );
	QDir().mkpath( fixtureRoot + QStringLiteral( "/ignored/materials" ) );
	QFile mountedLoose( fixtureRoot + QStringLiteral( "/mounted/addon/materials/shared/mounted.vmt" ) );
	bool mountedOk = mountedLoose.open( QIODevice::WriteOnly ); mountedLoose.close();
	QFile localLoose( mountedGame + QStringLiteral( "/materials/local/local.vmt" ) );
	mountedOk = mountedOk && localLoose.open( QIODevice::WriteOnly ); localLoose.close();
	QFile ignoredLoose( fixtureRoot + QStringLiteral( "/ignored/materials/ignored.vmt" ) );
	mountedOk = mountedOk && ignoredLoose.open( QIODevice::WriteOnly ); ignoredLoose.close();
	QFile unmountedVpk( fixtureRoot + QStringLiteral( "/mounted/addon/unmounted_dir.vpk" ) );
	mountedOk = mountedOk && unmountedVpk.open( QIODevice::WriteOnly );
	QByteArray unmountedTree = tree; unmountedTree.replace( "packed", "hidden" );
	unmountedVpk.write( header ); unmountedVpk.write( unmountedTree ); unmountedVpk.close();
	QFile gameInfo( mountedGame + QStringLiteral( "/gameinfo.txt" ) );
	mountedOk = mountedOk && gameInfo.open( QIODevice::WriteOnly );
	gameInfo.write( "\"GameInfo\" { \"FileSystem\" { \"SearchPaths\" { game+mod \"mounted/*\" game \"fixture.vpk\" game \"|gameinfo_path|.\" gamebin \"ignored\" } } }" );
	gameInfo.close();
	const MaterialCatalogResult mountedCatalog = ScanGameMaterials( mountedGame );
	mountedOk = mountedOk && mountedCatalog.names.contains( QStringLiteral( "local/local" ) ) &&
		mountedCatalog.names.contains( QStringLiteral( "shared/mounted" ) ) &&
		mountedCatalog.names.contains( QStringLiteral( "dev/packed" ) ) &&
		!mountedCatalog.names.contains( QStringLiteral( "ignored" ) ) &&
		!mountedCatalog.names.contains( QStringLiteral( "dev/hidden" ) ) && mountedCatalog.failedArchives.isEmpty();
	ctx.Step( "material-gameinfo-paths", mountedOk, QStringLiteral( "game path IDs, wildcard directories, relative VPK and gameinfo token" ) );
	// Reselecting an unchanged game directory must reread its search paths.
	const QString refreshGame = fixtureRoot + QStringLiteral( "/refreshgame" );
	bool refreshOk = QDir().mkpath( refreshGame + QStringLiteral( "/first/materials/refresh" ) ) &&
		QDir().mkpath( refreshGame + QStringLiteral( "/second/materials/refresh" ) );
	const auto writeRefreshFile = []( const QString &path, const QByteArray &bytes ) {
		QFile file( path );
		return file.open( QIODevice::WriteOnly | QIODevice::Truncate ) && file.write( bytes ) == bytes.size();
	};
	refreshOk = refreshOk && writeRefreshFile( refreshGame + QStringLiteral( "/first/materials/refresh/test.vmt" ),
		"LightmappedGeneric { $basetexture refresh/first }" ) &&
		writeRefreshFile( refreshGame + QStringLiteral( "/second/materials/refresh/test.vmt" ),
		"LightmappedGeneric { $basetexture refresh/second }" );
	const QString refreshConfig = refreshGame + QStringLiteral( "/gameinfo.txt" );
	refreshOk = refreshOk && writeRefreshFile( refreshConfig, "GameInfo { FileSystem { SearchPaths { game \"|gameinfo_path|first\" } } }" );
	CMaterialPreviewRenderer refreshPreview;
	refreshPreview.SetGameDirectory( refreshGame );
	QString refreshDetail;
	refreshOk = refreshOk && !refreshPreview.ProbeMaterial( QStringLiteral( "refresh/test" ), &refreshDetail ) &&
		refreshDetail.contains( QStringLiteral( "refresh/first" ) );
	refreshOk = refreshOk && writeRefreshFile( refreshConfig, "GameInfo { FileSystem { SearchPaths { game \"|gameinfo_path|second\" } } }" );
	refreshPreview.SetGameDirectory( refreshGame );
	refreshOk = refreshOk && !refreshPreview.ProbeMaterial( QStringLiteral( "refresh/test" ), &refreshDetail ) &&
		refreshDetail.contains( QStringLiteral( "refresh/second" ) );
	ctx.Step( "material-game-path-refresh", refreshOk, QStringLiteral( "reselecting same game reloads modified material search paths" ) );
	// Rebuilding a mounted directory VPK must invalidate the shared index cache.
	const QString cacheGame = fixtureRoot + QStringLiteral( "/cachegame" );
	QDir().mkpath( cacheGame );
	QFile cacheGameInfo( cacheGame + QStringLiteral( "/gameinfo.txt" ) );
	bool cacheOk = cacheGameInfo.open( QIODevice::WriteOnly );
	if ( cacheOk ) { cacheGameInfo.write( "GameInfo { FileSystem { SearchPaths { game \"|gameinfo_path|cache.vpk\" } } }" ); cacheGameInfo.close(); }
	const QString cacheVpk = cacheGame + QStringLiteral( "/cache_dir.vpk" );
	auto writeCachedMaterial = [&]( const QByteArray &vmt ) {
		QByteArray cacheTree;
		auto str = [&]( const char *s ) { cacheTree.append( s ); cacheTree.append( '\0' ); };
		str( "vmt" ); str( "materials/cache" ); str( "test" );
		QByteArray record( 18, '\0' );
		qToLittleEndian<quint16>( static_cast<quint16>( vmt.size() ), reinterpret_cast<uchar *>( record.data() + 4 ) );
		record[16] = char( 0xff ); record[17] = char( 0xff );
		cacheTree.append( record ); cacheTree.append( vmt ); cacheTree.append( QByteArray( 3, '\0' ) );
		QByteArray cacheHeader( 12, '\0' );
		qToLittleEndian<quint32>( 0x55aa1234u, reinterpret_cast<uchar *>( cacheHeader.data() ) );
		qToLittleEndian<quint32>( 1, reinterpret_cast<uchar *>( cacheHeader.data() + 4 ) );
		qToLittleEndian<quint32>( static_cast<quint32>( cacheTree.size() ), reinterpret_cast<uchar *>( cacheHeader.data() + 8 ) );
		QFile file( cacheVpk );
		return file.open( QIODevice::WriteOnly | QIODevice::Truncate ) &&
			file.write( cacheHeader ) == cacheHeader.size() && file.write( cacheTree ) == cacheTree.size();
	};
	cacheOk = cacheOk && writeCachedMaterial( "LightmappedGeneric { $basetexture cache/old }" );
	// A mounted single-file VPK is not necessarily a split archive whose
	// index is called *_dir.vpk. The catalog and renderer must agree.
	const QString singleGame = fixtureRoot + QStringLiteral( "/singlegame" );
	QDir().mkpath( singleGame );
	QFile singleGameInfo( singleGame + QStringLiteral( "/gameinfo.txt" ) );
	bool singleVpkOk = cacheOk && singleGameInfo.open( QIODevice::WriteOnly );
	if ( singleVpkOk ) { singleGameInfo.write( "GameInfo { FileSystem { SearchPaths { game \"|gameinfo_path|single.vpk\" } } }" ); singleGameInfo.close(); }
	singleVpkOk = singleVpkOk && QFile::copy( cacheVpk, singleGame + QStringLiteral( "/single.vpk" ) );
	const MaterialCatalogResult singleCatalog = ScanGameMaterials( singleGame );
	CMaterialPreviewRenderer singlePreview;
	singlePreview.SetGameDirectory( singleGame );
	QString singleDetail;
	singleVpkOk = singleVpkOk && singleCatalog.failedArchives.isEmpty() && singleCatalog.names.contains( QStringLiteral( "cache/test" ) ) &&
		!singlePreview.ProbeMaterial( QStringLiteral( "cache/test" ), &singleDetail ) && singleDetail.contains( QStringLiteral( "cache/old" ) );
	ctx.Step( "material-single-file-vpk", singleVpkOk, QStringLiteral( "mounted standalone VPK is indexed and its VMT is read" ) );
	CMaterialPreviewRenderer cachePreview;
	cachePreview.SetGameDirectory( cacheGame );
	QString cacheDetail;
	cacheOk = cacheOk && !cachePreview.ProbeMaterial( QStringLiteral( "cache/test" ), &cacheDetail ) && cacheDetail.contains( QStringLiteral( "cache/old" ) );
	cacheOk = cacheOk && writeCachedMaterial( "LightmappedGeneric { $basetexture cache/new_texture }" );
	cacheOk = cacheOk && !cachePreview.ProbeMaterial( QStringLiteral( "cache/test" ), &cacheDetail ) && cacheDetail.contains( QStringLiteral( "cache/new_texture" ) );
	ctx.Step( "material-vpk-cache-refresh", cacheOk, QStringLiteral( "rebuilt VPK directory index is reloaded for subsequent material lookup" ) );
	// External chunks are reopened on each lookup even when the directory index
	// itself is unchanged (e.g. an archive writer replaced only a payload).
	QByteArray chunkTree;
	auto chunkString = [&]( const char *s ) { chunkTree.append( s ); chunkTree.append( '\0' ); };
	chunkString( "vmt" ); chunkString( "materials/cache" ); chunkString( "test" );
	const QByteArray firstChunk( "LightmappedGeneric { $basetexture cache/one }" );
	const QByteArray secondChunk( "LightmappedGeneric { $basetexture cache/two }" );
	QByteArray chunkRecord( 18, '\0' );
	qToLittleEndian<quint32>( static_cast<quint32>( firstChunk.size() ), reinterpret_cast<uchar *>( chunkRecord.data() + 12 ) );
	chunkRecord[16] = char( 0xff ); chunkRecord[17] = char( 0xff );
	chunkTree.append( chunkRecord ); chunkTree.append( QByteArray( 3, '\0' ) );
	QByteArray chunkHeader( 12, '\0' );
	qToLittleEndian<quint32>( 0x55aa1234u, reinterpret_cast<uchar *>( chunkHeader.data() ) );
	qToLittleEndian<quint32>( 1, reinterpret_cast<uchar *>( chunkHeader.data() + 4 ) );
	qToLittleEndian<quint32>( static_cast<quint32>( chunkTree.size() ), reinterpret_cast<uchar *>( chunkHeader.data() + 8 ) );
	QFile chunkIndex( cacheVpk );
	bool chunkOk = firstChunk.size() == secondChunk.size() && chunkIndex.open( QIODevice::WriteOnly | QIODevice::Truncate );
	if ( chunkOk ) { chunkOk = chunkIndex.write( chunkHeader ) == chunkHeader.size() && chunkIndex.write( chunkTree ) == chunkTree.size(); chunkIndex.close(); }
	auto writeChunk = [&]( const QByteArray &bytes ) {
		QFile file( cacheGame + QStringLiteral( "/cache_000.vpk" ) );
		return file.open( QIODevice::WriteOnly | QIODevice::Truncate ) && file.write( bytes ) == bytes.size();
	};
	chunkOk = chunkOk && writeChunk( firstChunk );
	chunkOk = chunkOk && !cachePreview.ProbeMaterial( QStringLiteral( "cache/test" ), &cacheDetail ) && cacheDetail.contains( QStringLiteral( "cache/one" ) );
	chunkOk = chunkOk && writeChunk( secondChunk );
	chunkOk = chunkOk && !cachePreview.ProbeMaterial( QStringLiteral( "cache/test" ), &cacheDetail ) && cacheDetail.contains( QStringLiteral( "cache/two" ) );
	ctx.Step( "material-vpk-chunk-refresh", chunkOk, QStringLiteral( "external VPK payload is reopened without rebuilding the directory index" ) );
	QFile loosePreviewVmt( mountedGame + QStringLiteral( "/materials/local/preview.vmt" ) );
	QFile invalidPreviewVtf( mountedGame + QStringLiteral( "/materials/local/broken.vtf" ) );
	bool loosePreviewOk = loosePreviewVmt.open( QIODevice::WriteOnly );
	if ( loosePreviewOk ) { loosePreviewVmt.write( "\"LightmappedGeneric\" { \"$basetexture\" \"local/broken\" }" ); loosePreviewVmt.close(); }
	loosePreviewOk = loosePreviewOk && invalidPreviewVtf.open( QIODevice::WriteOnly );
	if ( invalidPreviewVtf.isOpen() ) { invalidPreviewVtf.write( "not a VTF" ); invalidPreviewVtf.close(); }
	CMaterialPreviewRenderer loosePreview;
	loosePreview.SetGameDirectory( mountedGame );
	QString looseDetail;
	loosePreviewOk = loosePreviewOk && !loosePreview.ProbeMaterial( QStringLiteral( "local/preview" ), &looseDetail ) &&
		looseDetail.contains( QStringLiteral( "VTF read but decode failed" ) );
	ctx.Step( "material-loose-invalid-vtf", loosePreviewOk, looseDetail );
	// Browser decoding runs off the UI thread. An invalid texture must report
	// unavailable, and clearing the selection must not leave the old preview.
	QLabel browserImageLabel, browserStatusLabel;
	browserImageLabel.setFixedSize( 128, 128 );
	CMaterialBrowserPreview browserPreview( &browserImageLabel, &browserStatusLabel, nullptr );
	browserPreview.Request( mountedGame, QStringLiteral( "local/preview" ) );
	QEventLoop browserLoop;
	QTimer browserPoll, browserTimeout;
	browserPoll.setInterval( 10 ); browserTimeout.setSingleShot( true );
	QObject::connect( &browserPoll, &QTimer::timeout, &browserLoop, [&] {
		if ( browserStatusLabel.text() != QCoreApplication::translate( "CMainWindow", "Loading material preview..." ) ) browserLoop.quit();
	} );
	QObject::connect( &browserTimeout, &QTimer::timeout, &browserLoop, &QEventLoop::quit );
	browserPoll.start(); browserTimeout.start( 5000 ); browserLoop.exec();
	bool browserUnavailable = browserStatusLabel.text() == QCoreApplication::translate( "CMainWindow", "Preview unavailable for this material." );
	browserPreview.Request( {}, {} );
	browserUnavailable = browserUnavailable && browserStatusLabel.text() == QCoreApplication::translate( "CMainWindow", "Select a scanned material to preview it." );
	ctx.Step( "material-browser-preview-unavailable", browserUnavailable, QStringLiteral( "asynchronous invalid texture and cleared selection" ) );
	// Patch VMTs use the engine's insert/replace ordering without requiring GL.
	auto writePreviewVmt = [&]( const char *fileName, const char *contents ) {
		QFile file( mountedGame + QStringLiteral( "/materials/local/" ) + QLatin1String( fileName ) );
		if ( !file.open( QIODevice::WriteOnly ) ) return false;
		const qint64 length = static_cast<qint64>( std::strlen( contents ) );
		return file.write( contents, length ) == length;
	};
	bool patchFilesOk =
		writePreviewVmt( "decal_modulate.vmt", "DecalModulate { $basetexture local/original $decalscale 0.25 }" ) &&
		writePreviewVmt( "base.vmt", "LightmappedGeneric { $basetexture local/original }" ) &&
		writePreviewVmt( "empty.vmt", "LightmappedGeneric { $surfaceprop concrete }" ) &&
		writePreviewVmt( "insert.vmt", "patch { include materials/local/base.vmt insert { $basetexture local/inserted } }" ) &&
		writePreviewVmt( "replace.vmt", "patch { include materials/local/base.vmt replace { $basetexture local/replaced } }" ) &&
		writePreviewVmt( "replace_missing.vmt", "patch { include materials/local/empty.vmt replace { $basetexture local/ignored } }" ) &&
		writePreviewVmt( "outer.vmt", "patch { include materials/local/replace.vmt replace { $basetexture local/outer } }" ) &&
		writePreviewVmt( "missing.vmt", "patch { include materials/local/absent.vmt }" ) &&
		writePreviewVmt( "cycle_a.vmt", "patch { include materials/local/cycle_b.vmt }" ) &&
		writePreviewVmt( "cycle_b.vmt", "patch { include materials/local/cycle_a.vmt }" ) &&
		writePreviewVmt( "traversal.vmt", "patch { include materials/../outside.vmt }" ) &&
		writePreviewVmt( "conditional.vmt", "LightmappedGeneric { $basetexture local/original [$WIN32] }" ) &&
		writePreviewVmt( "quoted_values.vmt", "LightmappedGeneric { $color2 \"[1 0.5 0.25]\" $detail \"literal//value\" $basetexture local/original }" ) &&
		writePreviewVmt( "backslash.vmt", "patch { include \"materials\\local\\base.vmt\" }" ) &&
		writePreviewVmt( "unbalanced.vmt", "LightmappedGeneric { $basetexture local/original" ) &&
		writePreviewVmt( "quoted_directive.vmt", "\"#include\" { $basetexture local/original }" ) &&
		writePreviewVmt( "proxy.vmt", "LightmappedGeneric { $basetexture local/original Proxies { AnimatedTexture { animatedTextureVar $basetexture } } }" ) &&
		writePreviewVmt( "proxy_empty.vmt", "LightmappedGeneric { Proxies { AnimatedTexture { } } }" ) &&
		writePreviewVmt( "proxy_broken.vmt", "LightmappedGeneric { $basetexture local/original Proxies { AnimatedTexture { } }" ) &&
		writePreviewVmt( "proxy_conditional.vmt", "LightmappedGeneric { $basetexture local/original Proxies { AnimatedTexture { x y [$WIN32] } } }" ) &&
		writePreviewVmt( "nested_unknown.vmt", "LightmappedGeneric { $basetexture local/original fallback { x y } }" );
	QString previewTexture, patchDetail;
	auto resolvesTo = [&]( const char *material, const char *expected ) {
		previewTexture.clear(); patchDetail.clear();
		return loosePreview.ResolveBaseTexture( QLatin1String( material ), previewTexture, &patchDetail ) &&
			previewTexture == QLatin1String( expected );
	};
	auto rejectsPatch = [&]( const char *material ) {
		previewTexture = QStringLiteral( "unchanged" ); patchDetail.clear();
		return !loosePreview.ResolveBaseTexture( QLatin1String( material ), previewTexture, &patchDetail ) &&
			previewTexture == QLatin1String( "unchanged" ) && !patchDetail.isEmpty();
	};
	const bool patchPreviewOk = patchFilesOk &&
		resolvesTo( "local/base", "local/original" ) &&
		resolvesTo( "local/insert", "local/inserted" ) &&
		resolvesTo( "local/replace", "local/replaced" ) &&
		resolvesTo( "local/outer", "local/replaced" ) &&
		resolvesTo( "local/quoted_values", "local/original" ) &&
		resolvesTo( "local/backslash", "local/original" ) &&
		resolvesTo( "local/proxy", "local/original" ) &&
		rejectsPatch( "local/replace_missing" ) && rejectsPatch( "local/missing" ) &&
		rejectsPatch( "local/cycle_a" ) && rejectsPatch( "local/traversal" ) &&
		rejectsPatch( "local/conditional" ) && rejectsPatch( "local/proxy_empty" ) &&
		rejectsPatch( "local/proxy_broken" ) && rejectsPatch( "local/proxy_conditional" ) &&
		rejectsPatch( "local/nested_unknown" ) &&
		rejectsPatch( "local/unbalanced" ) && rejectsPatch( "local/quoted_directive" );
	ctx.Step( "material-preview-vmt-patch", patchPreviewOk,
		QStringLiteral( "ordinary VMT, insert/replace, nested priority and invalid include rejection: %1" ).arg( patchDetail ) );
	SideLite uvScaleSide{};
	uvScaleSide.uscale = 0.25f; uvScaleSide.vscale = 0.25f;
	bool uvScaleOk = HammerPreviewValidUvScale( uvScaleSide );
	uvScaleSide.uscale = 0.0f; uvScaleOk = uvScaleOk && !HammerPreviewValidUvScale( uvScaleSide );
	uvScaleSide.uscale = 0.25f; uvScaleSide.vscale = 0.0f; uvScaleOk = uvScaleOk && !HammerPreviewValidUvScale( uvScaleSide );
	ctx.Step( "material-preview-invalid-scale", uvScaleOk, QStringLiteral( "zero scale is rejected before texture mapping" ) );
	{
		const HammerWinding::Plane plane{ { 0, 0, 1 }, 0 };
		const HammerWinding::Polygon face{ { 0, 0, 0 }, { 8, 0, 0 }, { 8, 8, 0 }, { 0, 8, 0 } };
		const auto decal = HammerDecalProjection::Build( face, plane, { 0, 4, 1 }, 4, 4 );
		bool clipped = decal.size() == 4;
		for ( const auto &v : decal )
			clipped = clipped && ( v.position[0] == 0 || v.position[0] == 2 ) &&
				( v.position[1] == 2 || v.position[1] == 6 ) && v.position[2] == 0 &&
				v.uv[0] == 0.5 + v.position[0] / 4 && v.uv[1] == 0.5 - ( v.position[1] - 4 ) / 4;
		ctx.Step( "decal-projection", clipped && resolvesTo( "local/decal_modulate", "local/original" ) &&
			HammerDecalProjection::Build( face, plane, { 0, 4, -1 }, 4, 4 ).isEmpty() &&
			HammerDecalProjection::Build( face, plane, { 0, 4, 17 }, 4, 4 ).isEmpty(),
			QStringLiteral( "stock clipping, floor UVs, face distance and modulating shader support" ) );
		HammerDisplacementMesh::Mesh terrain;
		terrain.vertices = { { 0, 0, 40 }, { 8, 0, 44 }, { 8, 8, 48 }, { 0, 8, 44 } };
		terrain.triangles = { { 0, 1, 2 }, { 0, 2, 3 } };
		const auto projected = HammerDecalProjection::BuildDisplacement( terrain, plane.normal, { 4, 4, 44 }, 4, 4 );
		bool terrainClipped = projected.size() == 6;
		for ( const auto &v : projected )
			terrainClipped = terrainClipped && v.position[0] >= 2 && v.position[0] <= 6 &&
				v.position[1] >= 2 && v.position[1] <= 6 &&
				v.position[2] == 40 + ( v.position[0] + v.position[1] ) / 2 &&
				v.uv[0] == ( v.position[0] - 2 ) / 4 && v.uv[1] == ( 6 - v.position[1] ) / 4;
		ctx.Step( "decal-displacement-projection", terrainClipped &&
			HammerDecalProjection::BuildDisplacement( terrain, plane.normal, { 4, 4, 0 }, 4, 4 ).isEmpty(),
			QStringLiteral( "clip raised terrain triangles in a shared texture frame and reject remote surfaces" ) );
		HammerOverlay::Data overlay;
		overlay.origin = { 0, 4, 1 };
		overlay.u = { 1, 0, 0 };
		overlay.v = { 0, 1, 0 };
		overlay.normal = { 0, 0, 1 };
		overlay.points = {{ { -2, -2, 0 }, { -2, 2, 0 }, { 2, 2, 0 }, { 2, -2, 0 } }};
		overlay.startU = 1; overlay.endU = 3;
		const auto triangles = HammerOverlay::ProjectTriangles( overlay, face, plane );
		bool overlayClipped = triangles.size() == 9;
		for ( const auto &v : triangles )
			overlayClipped = overlayClipped && v.position[0] >= 0 && v.position[0] <= 2 &&
				v.position[1] >= 2 && v.position[1] <= 6 && v.position[2] == 0 &&
				std::abs( v.uv[0] - ( 2 + v.position[0] / 2 ) ) < 1e-9 &&
				std::abs( v.uv[1] - ( v.position[1] - 2 ) / 4 ) < 1e-9;
		ctx.Step( "overlay-projection", overlayClipped, QStringLiteral( "receiver clipping, basis projection and tiled UVs" ) );
	}
	const QString materialGame = qEnvironmentVariable( "HAMMER_SELFTEST_MATERIAL_GAME" );
	if ( !materialGame.isEmpty() )
	{
		const MaterialCatalogResult actualCatalog = ScanGameMaterials( materialGame );
		ctx.Step( "material-catalog-game", !actualCatalog.names.isEmpty() && actualCatalog.failedArchives.isEmpty(),
			QStringLiteral( "%1 materials, %2 failed VPK indexes" ).arg( actualCatalog.names.size() ).arg( actualCatalog.failedArchives.size() ) );
		CMaterialPreviewRenderer previewProbe;
		previewProbe.SetGameDirectory( materialGame );
		QString probeDetail;
		QString probedMaterial;
		bool textureProbeOk = false;
		const QStringList preferred = { QStringLiteral( "dev/dev_measuregeneric01" ),
			QStringLiteral( "debug/debugwhite" ), QStringLiteral( "brick/brickwall001a" ) };
		for ( const QString &name : preferred )
		{
			if ( previewProbe.ProbeMaterial( name, &probeDetail ) )
			{
				probedMaterial = name;
				textureProbeOk = true;
				break;
			}
		}
		if ( !textureProbeOk )
		{
			const int probeLimit = qMin( 64, actualCatalog.names.size() );
			for ( int i = 0; i < probeLimit; ++i )
			{
				const QString &name = actualCatalog.names[i];
				if ( previewProbe.ProbeMaterial( name, &probeDetail ) )
				{
					probedMaterial = name;
					textureProbeOk = true;
					break;
				}
			}
		}
		ctx.Step( "material-vmt-vtf-pixels", textureProbeOk,
			QStringLiteral( "material=%1; %2" ).arg( probedMaterial, probeDetail ) );
		QImage browserImage;
		const bool browserImageOk = textureProbeOk && previewProbe.LoadMaterialImage( probedMaterial, browserImage ) &&
			!browserImage.isNull() && browserImage.width() > 0 && browserImage.height() > 0 &&
			!previewProbe.LoadMaterialImage( QStringLiteral( "__hammer2_missing_preview__" ), browserImage ) && browserImage.isNull();
		ctx.Step( "material-browser-cpu-preview", browserImageOk,
			QStringLiteral( "real VMT/VTF decode and missing material clears previous preview" ) );
	}

	// Hollow produces a sealed six-brush shell without destroying face metadata.
	CHammerDocument hollowDoc;
	const double hollowLo[3] = { -64, -64, -64 }, hollowHi[3] = { 64, 64, 64 };
	const int hollowOriginal = hollowDoc.CreateBrush( hollowLo, hollowHi );
	SolidLite *hollowSource = hollowDoc.Map().SolidById( hollowOriginal );
	hollowSource->sides[0].texture = QStringLiteral( "test/custom" );
	hollowSource->sides[0].uaxis[3] = 17;
	bool bHollow = !hollowDoc.HollowSelection( 64 ) && hollowDoc.Map().solids.size() == 1;
	bHollow = bHollow && hollowDoc.HollowSelection( 16 ) && hollowDoc.Map().solids.size() == 6 && hollowDoc.SelectedIds().size() == 6;
	double wallVolume = 0;
	QSet<int> hollowIds;
	for ( const SolidLite &solid : hollowDoc.Map().solids )
	{
		wallVolume += ( solid.maxs[0] - solid.mins[0] ) * ( solid.maxs[1] - solid.mins[1] ) * ( solid.maxs[2] - solid.mins[2] );
		bHollow = bHollow && !hollowIds.contains( solid.id ); hollowIds.insert( solid.id );
		for ( const SideLite &side : solid.sides ) { bHollow = bHollow && !hollowIds.contains( side.id ); hollowIds.insert( side.id ); }
		bHollow = bHollow && solid.sides[0].texture == QLatin1String( "test/custom" ) && solid.sides[0].uaxis[3] == 17;
	}
	bHollow = bHollow && wallVolume == 128.0 * 128 * 128 - 96.0 * 96 * 96;
	QTemporaryDir hollowTemp;
	MapDocLite hollowReload;
	const QString hollowFile = hollowTemp.filePath( QStringLiteral( "hollow.vmf" ) );
	bHollow = bHollow && hollowTemp.isValid() && VmfIO::SaveMap( hollowFile, hollowDoc.Map() ) && VmfIO::LoadMap( hollowFile, hollowReload ) && hollowReload.solids.size() == 6;
	bHollow = bHollow && hollowDoc.Undo() && hollowDoc.Map().solids.size() == 1 && hollowDoc.SelectedIds().contains( hollowOriginal );
	bHollow = bHollow && hollowDoc.Redo() && hollowDoc.Map().solids.size() == 6;
	CHammerDocument outerDoc;
	outerDoc.CreateBrush( hollowLo, hollowHi );
	bHollow = bHollow && outerDoc.HollowSelection( -8 ) && outerDoc.Map().solids.size() == 6;
	CHammerDocument cachedDoc;
	const int cachedId = cachedDoc.CreateBrush( hollowLo, hollowHi );
	cachedDoc.Map().SolidById( cachedId )->sides[0].hasVerticesPlus = true;
	bHollow = bHollow && !cachedDoc.HollowSelection( 8 ) && cachedDoc.Map().solids.size() == 1;
	CHammerDocument mixedHollow;
	const int validHollowId = mixedHollow.CreateBrush( hollowLo, hollowHi );
	const int invalidHollowId = mixedHollow.CreateBrush( hollowLo, hollowHi );
	mixedHollow.Map().SolidById( invalidHollowId )->sides[0].planePts[1][0] += 1;
	mixedHollow.SelectObjects( QSet<int>() << validHollowId << invalidHollowId );
	bHollow = bHollow && !mixedHollow.HollowSelection( 8 ) && mixedHollow.Map().solids.size() == 2 && mixedHollow.SelectedIds().size() == 2;
	ctx.Step( "hollow-box-brush", bHollow, QStringLiteral( "inner and outer shell, face material, VMF, rejection, undo/redo" ) );

	// Scale around the aggregate center, preserving IDs and face attributes.
	CHammerDocument scaleDoc;
	const double scaleLoA[3] = { 0, 0, 0 }, scaleHiA[3] = { 16, 16, 16 };
	const double scaleLoB[3] = { 32, 0, 0 }, scaleHiB[3] = { 48, 16, 16 };
	const int scaleA = scaleDoc.CreateBrush( scaleLoA, scaleHiA );
	const int scaleB = scaleDoc.CreateBrush( scaleLoB, scaleHiB );
	scaleDoc.Map().SolidById( scaleA )->sides[0].texture = QStringLiteral( "test/scale" );
	scaleDoc.Map().SolidById( scaleA )->sides[0].uaxis[3] = 23;
	scaleDoc.SelectObjects( QSet<int>() << scaleA << scaleB );
	const double factors[3] = { 2, 0.5, 1 };
	bool scaleOk = scaleDoc.ScaleSelectedBoxes( factors );
	const SolidLite *scaledA = scaleDoc.Map().SolidById( scaleA );
	const SolidLite *scaledB = scaleDoc.Map().SolidById( scaleB );
	scaleOk = scaleOk && scaledA && scaledB && scaledA->mins[0] == -24 && scaledA->maxs[0] == 8 &&
		scaledB->mins[0] == 40 && scaledB->maxs[0] == 72 && scaledA->mins[1] == 4 && scaledA->maxs[1] == 12 &&
		scaledA->sides[0].texture == QLatin1String( "test/scale" ) && scaledA->sides[0].uaxis[3] == 23;
	QTemporaryDir scaleTemp;
	MapDocLite scaleReload;
	const QString scaleFile = scaleTemp.filePath( QStringLiteral( "scale.vmf" ) );
	scaleOk = scaleOk && scaleTemp.isValid() && VmfIO::SaveMap( scaleFile, scaleDoc.Map() ) &&
		VmfIO::LoadMap( scaleFile, scaleReload ) && scaleReload.SolidById( scaleA ) && scaleReload.SolidById( scaleA )->mins[0] == -24;
	scaleOk = scaleOk && scaleDoc.Undo() && scaleDoc.Map().SolidById( scaleA )->mins[0] == 0 &&
		scaleDoc.Redo() && scaleDoc.Map().SolidById( scaleA )->mins[0] == -24;
	scaleDoc.Map().SolidById( scaleB )->sides[0].hasVerticesPlus = true;
	scaleOk = scaleOk && !scaleDoc.ScaleSelectedBoxes( factors ) && scaleDoc.Map().SolidById( scaleA )->mins[0] == -24;
	const double invalidFactors[3] = { 0, 1, 1 };
	scaleOk = scaleOk && !scaleDoc.ScaleSelectedBoxes( invalidFactors );
	scaleDoc.Map().SolidById( scaleB )->sides[0].hasVerticesPlus = false;
	EntityLite scaleEntity{};
	scaleEntity.id = scaleDoc.Map().AllocId();
	scaleEntity.classname = QStringLiteral( "info_target" );
	scaleDoc.Map().entities.append( scaleEntity );
	scaleDoc.SelectObjects( QSet<int>() << scaleA << scaleEntity.id );
	scaleOk = scaleOk && !scaleDoc.ScaleSelectedBoxes( factors ) && scaleDoc.Map().SolidById( scaleA )->mins[0] == -24;
	ctx.Step( "scale-box-brushes", scaleOk, QStringLiteral( "aggregate center, metadata, VMF, rejection, undo/redo" ) );

	CHammerDocument rotateDoc;
	const int rotateA = rotateDoc.CreateBrush( scaleLoA, scaleHiA );
	const int rotateB = rotateDoc.CreateBrush( scaleLoB, scaleHiB );
	rotateDoc.SelectObjects( QSet<int>() << rotateA << rotateB );
	const SideLite beforeRotation = rotateDoc.Map().SolidById( rotateA )->sides[0];
	const auto uvAtFirstPoint = []( const SideLite &side, const float *axis, double scale ) {
		return ( axis[0] * side.planePts[0][0] + axis[1] * side.planePts[0][1] + axis[2] * side.planePts[0][2] ) / scale + axis[3];
	};
	const double oldU = uvAtFirstPoint( beforeRotation, beforeRotation.uaxis, beforeRotation.uscale );
	const double oldV = uvAtFirstPoint( beforeRotation, beforeRotation.vaxis, beforeRotation.vscale );
	bool rotateOk = rotateDoc.RotateSelectedBoxes90( false );
	const SolidLite *rotatedA = rotateDoc.Map().SolidById( rotateA );
	const SolidLite *rotatedB = rotateDoc.Map().SolidById( rotateB );
	rotateOk = rotateOk && rotatedA && rotatedB && rotatedA->mins[0] == 16 && rotatedA->maxs[0] == 32 &&
		rotatedA->mins[1] == -16 && rotatedA->maxs[1] == 0 && rotatedB->mins[1] == 16 && rotatedB->maxs[1] == 32 &&
		rotatedA->sides[0].id == beforeRotation.id && rotatedA->sides[0].texture == beforeRotation.texture &&
		std::abs( uvAtFirstPoint( rotatedA->sides[0], rotatedA->sides[0].uaxis, rotatedA->sides[0].uscale ) - oldU ) < 0.001 &&
		std::abs( uvAtFirstPoint( rotatedA->sides[0], rotatedA->sides[0].vaxis, rotatedA->sides[0].vscale ) - oldV ) < 0.001;
	rotateOk = rotateOk && rotateDoc.Undo() && rotateDoc.Map().SolidById( rotateA )->mins[0] == 0 &&
		rotateDoc.Redo() && rotateDoc.Map().SolidById( rotateA )->mins[0] == 16;
	rotateDoc.Map().SolidById( rotateB )->sides[0].hasVerticesPlus = true;
	rotateOk = rotateOk && !rotateDoc.RotateSelectedBoxes90( true ) && rotateDoc.Map().SolidById( rotateA )->mins[0] == 16;
	rotateDoc.Map().SolidById( rotateB )->sides[0].hasVerticesPlus = false;
	rotateOk = rotateOk && rotateDoc.RotateSelectedBoxes90( true ) && rotateDoc.Map().SolidById( rotateA )->mins[0] == 0;
	EntityLite rotateEntity{}; rotateEntity.id = rotateDoc.Map().AllocId(); rotateEntity.classname = QStringLiteral( "info_target" );
	rotateDoc.Map().entities.append( rotateEntity );
	rotateDoc.SelectObjects( QSet<int>() << rotateA << rotateEntity.id );
	rotateOk = rotateOk && !rotateDoc.RotateSelectedBoxes90( false ) && rotateDoc.Map().SolidById( rotateA )->mins[0] == 0;
	ctx.Step( "rotate-box-brushes", rotateOk, QStringLiteral( "quarter turns, texture lock, rejection and undo/redo" ) );
	{
		CHammerDocument vertexDoc;
		const double lo[3] = { 0, 0, 0 }, hi[3] = { 64, 64, 64 };
		const int id = vertexDoc.CreateBrush( lo, hi );
		const SolidLite original = *vertexDoc.Map().SolidById( id );
		vertexDoc.SelectObject( id );
		bool ok = vertexDoc.MoveSelectedProjectedVertices( 0, 1, 64, 64, 80, 64 );
		const SolidLite *moved = vertexDoc.Map().SolidById( id );
		QList<HammerWinding::Polygon> faces;
		ok = ok && moved && HammerWinding::BuildFaces( *moved, faces ) &&
			std::abs( moved->maxs[0] - 80 ) < 0.001 && moved->sides.size() == original.sides.size();
		for ( int i=0; ok && i<original.sides.size(); ++i )
			ok = moved->sides[i].id == original.sides[i].id && moved->sides[i].texture == original.sides[i].texture;
		ok = ok && !vertexDoc.MoveSelectedProjectedVertices( 0, 1, 80, 64, 0, 0 ) &&
			vertexDoc.Undo() && std::abs( vertexDoc.Map().SolidById( id )->maxs[0] - 64 ) < 0.001 &&
			vertexDoc.Redo() && std::abs( vertexDoc.Map().SolidById( id )->maxs[0] - 80 ) < 0.001;
		vertexDoc.Map().SolidById( id )->sides[0].hasVerticesPlus = true;
		ok = ok && !vertexDoc.MoveSelectedProjectedVertices( 0, 1, 80, 64, 96, 64 );
		// On the x=64 face this axis initially has a nonzero planar component,
		// but moving the (64,64) projected edge to (80,64) makes it normal to
		// the face. The whole operation must be rejected without a history edit.
		CHammerDocument uvDoc; const int uvId = uvDoc.CreateBrush( lo, hi ); uvDoc.SelectObject( uvId );
		QList<HammerWinding::Plane> uvPlanes;
		ok = ok && HammerWinding::BuildPlanes( *uvDoc.Map().SolidById( uvId ), uvPlanes );
		int xFace = -1; for ( int i=0; i<uvPlanes.size(); ++i ) if ( uvPlanes[i].normal[0] > 0.999 ) { xFace = i; break; }
		if ( xFace < 0 ) ok = false;
		else
		{
			SideLite &side = uvDoc.Map().SolidById( uvId )->sides[xFace];
			side.uaxis[0]=4; side.uaxis[1]=-1; side.uaxis[2]=0;
			side.vaxis[0]=0; side.vaxis[1]=0; side.vaxis[2]=1;
			ok = ok && !uvDoc.MoveSelectedProjectedVertices( 0, 1, 64, 64, 80, 64 ) &&
				std::abs( uvDoc.Map().SolidById( uvId )->maxs[0] - 64 ) < 0.001;
		}
		ctx.Step( "projected-vertex-edit", ok, QStringLiteral( "planar convex move, IDs/materials, topology/UV guard, undo/redo and cache rejection" ) );
	}
	{
		bool ok = true;
		for ( bool explicitOrigin : { false, true } )
		{
			CHammerDocument doc;
			const double lo[3] = { 0, 0, 0 }, hi[3] = { 64, 64, 64 };
			const int brush = doc.CreateBrush( lo, hi );
			EntityLite entity; entity.id = doc.Map().AllocId(); entity.classname = QStringLiteral( "func_detail" );
			entity.hasExplicitOrigin = explicitOrigin; entity.origin[0] = 11; entity.origin[1] = 12; entity.origin[2] = 13;
			doc.Map().entities.append( entity ); doc.Map().SolidById( brush )->ownerEntityId = entity.id;
			doc.SelectObject( entity.id );
			const SolidLite original = *doc.Map().SolidById( brush );
			ok = ok && doc.SelectedVertexEditableSolid() && doc.SelectedVertexEditableSolid()->id == brush &&
				doc.MoveSelectedProjectedVertices( 0, 1, 64, 64, 80, 64 );
			const SolidLite *edited = doc.Map().SolidById( brush );
			const EntityLite *owner = doc.Map().EntityById( entity.id );
			ok = ok && edited && owner && edited->ownerEntityId == entity.id && edited->maxs[0] > 79.99 &&
				owner->hasExplicitOrigin == explicitOrigin && owner->origin[0] == 11 && owner->origin[1] == 12 && owner->origin[2] == 13 && doc.IsSelected( entity.id );
			for ( int i = 0; ok && i < original.sides.size(); ++i ) ok = edited->sides[i].id == original.sides[i].id && edited->sides[i].texture == original.sides[i].texture;
			ok = ok && doc.Undo() && doc.Map().SolidById( brush )->maxs[0] < 64.01 && doc.Redo() && doc.Map().SolidById( brush )->maxs[0] > 79.99;
			ok = ok && doc.CopySelection() && doc.PasteSpecial( 1, lo ) > 0;
			doc.SelectObject( entity.id );
			doc.Map().SolidById( brush )->sides[0].hasVerticesPlus = true;
			ok = ok && !doc.MoveSelectedProjectedVertices( 0, 1, 80, 64, 96, 64 );
			doc.Map().SolidById( brush )->sides[0].hasVerticesPlus = false;
			ViewFilter filter = doc.GetViewFilter(); filter.quickHiddenIds.insert( brush ); doc.SetViewFilter( filter );
			doc.SelectObject( entity.id );
			ok = ok && !doc.SelectedVertexEditableSolid() && !doc.MoveSelectedProjectedVertices( 0, 1, 80, 64, 96, 64 );
		}
		CHammerDocument multiple; const double lo[3] = { 0, 0, 0 }, hi[3] = { 64, 64, 64 };
		const int first = multiple.CreateBrush( lo, hi ), second = multiple.CreateBrush( lo, hi );
		EntityLite owner; owner.id = multiple.Map().AllocId(); owner.classname = QStringLiteral( "func_detail" );
		multiple.Map().entities.append( owner ); multiple.Map().SolidById( first )->ownerEntityId = owner.id; multiple.Map().SolidById( second )->ownerEntityId = owner.id;
		multiple.SelectObject( owner.id );
		ok = ok && !multiple.SelectedVertexEditableSolid() && !multiple.MoveSelectedProjectedVertices( 0, 1, 64, 64, 80, 64 );
		ctx.Step( "brush-entity-projected-vertex-edit", ok, QStringLiteral( "single child owner, origins, copy/undo, hidden and multiple-child guards" ) );
	}
	bool rotateAxesOk = true;
	for ( int rotationAxis = 0; rotationAxis < 3; ++rotationAxis )
	{
		CHammerDocument doc;
		const double lo[3] = { 10, 20, 30 }, hi[3] = { 26, 52, 94 };
		const int id = doc.CreateBrush( lo, hi );
		const SolidLite original = *doc.Map().SolidById( id );
		doc.SelectObjects( QSet<int>() << id );
		rotateAxesOk = rotateAxesOk && !doc.RotateSelectedBoxes90( false, 3 ) && doc.RotateSelectedBoxes90( false, rotationAxis );
		const SolidLite result = *doc.Map().SolidById( id );
		const int a = ( rotationAxis + 1 ) % 3, b = ( rotationAxis + 2 ) % 3;
		rotateAxesOk = rotateAxesOk && result.mins[rotationAxis] == lo[rotationAxis] && result.maxs[rotationAxis] == hi[rotationAxis] &&
			std::abs( result.maxs[a] - result.mins[a] - ( hi[b] - lo[b] ) ) < 0.001 &&
			std::abs( result.maxs[b] - result.mins[b] - ( hi[a] - lo[a] ) ) < 0.001;
		for ( int i = 0; i < original.sides.size(); ++i )
		{
			const SideLite &before = original.sides[i], &after = result.sides[i];
			rotateAxesOk = rotateAxesOk && before.id == after.id && before.texture == after.texture &&
				std::abs( uvAtFirstPoint( before, before.uaxis, before.uscale ) - uvAtFirstPoint( after, after.uaxis, after.uscale ) ) < 0.001 &&
				std::abs( uvAtFirstPoint( before, before.vaxis, before.vscale ) - uvAtFirstPoint( after, after.vaxis, after.vscale ) ) < 0.001;
		}
		rotateAxesOk = rotateAxesOk && doc.Undo() && doc.Map().SolidById( id )->mins[a] == lo[a] && doc.Redo() &&
			doc.Map().SolidById( id )->mins[a] == result.mins[a] && doc.RotateSelectedBoxes90( true, rotationAxis ) &&
			doc.Map().SolidById( id )->mins[a] == lo[a] && doc.Map().SolidById( id )->maxs[b] == hi[b];
	}
	ctx.Step( "rotate-box-three-axes", rotateAxesOk, QStringLiteral( "XYZ rotation, texture lock, invalid axis and undo/redo" ) );
	bool flipOk = true;
	for ( int mirrorAxis = 0; mirrorAxis < 3; ++mirrorAxis )
	{
		CHammerDocument doc;
		const int first = doc.CreateBrush( scaleLoA, scaleHiA );
		const int second = doc.CreateBrush( scaleLoB, scaleHiB );
		doc.SelectObjects( QSet<int>() << first << second );
		const SolidLite original = *doc.Map().SolidById( first );
		const SolidLite other = *doc.Map().SolidById( second );
		const double sum = qMin( original.mins[mirrorAxis], other.mins[mirrorAxis] ) +
			qMax( original.maxs[mirrorAxis], other.maxs[mirrorAxis] );
		flipOk = flipOk && !doc.FlipSelectedBoxes( 3 ) && doc.FlipSelectedBoxes( mirrorAxis );
		const SolidLite flipped = *doc.Map().SolidById( first );
		QList<HammerWinding::Polygon> polygons;
		flipOk = flipOk && flipped.mins[mirrorAxis] == sum - original.maxs[mirrorAxis] &&
			flipped.maxs[mirrorAxis] == sum - original.mins[mirrorAxis] && HammerWinding::BuildFaces( flipped, polygons ) && polygons.size() == 6;
		for ( int i = 0; i < original.sides.size(); ++i )
		{
			const SideLite &before = original.sides[i], &after = flipped.sides[i];
			flipOk = flipOk && before.id == after.id && before.texture == after.texture &&
				std::abs( uvAtFirstPoint( before, before.uaxis, before.uscale ) - uvAtFirstPoint( after, after.uaxis, after.uscale ) ) < 0.001 &&
				std::abs( uvAtFirstPoint( before, before.vaxis, before.vscale ) - uvAtFirstPoint( after, after.vaxis, after.vscale ) ) < 0.001;
		}
		QTemporaryDir temp;
		MapDocLite loaded;
		const QString file = temp.filePath( QStringLiteral( "flip.vmf" ) );
		flipOk = flipOk && temp.isValid() && VmfIO::SaveMap( file, doc.Map() ) && VmfIO::LoadMap( file, loaded ) &&
			loaded.SolidById( first ) && HammerWinding::BuildFaces( *loaded.SolidById( first ), polygons );
		flipOk = flipOk && doc.Undo() && doc.Map().SolidById( first )->mins[mirrorAxis] == original.mins[mirrorAxis] &&
			doc.Redo() && doc.Map().SolidById( first )->mins[mirrorAxis] == flipped.mins[mirrorAxis] &&
			doc.FlipSelectedBoxes( mirrorAxis ) && doc.Map().SolidById( first )->mins[mirrorAxis] == original.mins[mirrorAxis];
		const SolidLite restored = *doc.Map().SolidById( first );
		for ( int i = 0; i < original.sides.size(); ++i )
			for ( int point = 0; point < 3; ++point ) for ( int k = 0; k < 3; ++k )
				flipOk = flipOk && restored.sides[i].planePts[point][k] == original.sides[i].planePts[point][k];
		doc.Map().SolidById( second )->sides[0].hasVerticesPlus = true;
		flipOk = flipOk && !doc.FlipSelectedBoxes( mirrorAxis ) && doc.Map().SolidById( first )->mins[mirrorAxis] == original.mins[mirrorAxis];
	}
	ctx.Step( "flip-box-brushes", flipOk, QStringLiteral( "XYZ reflection, winding, UV, VMF, undo/redo and rejection" ) );

	CHammerDocument clipDoc;
	const double clipLo[3] = { 0, 0, 0 }, clipHi[3] = { 32, 32, 32 };
	const int clipId = clipDoc.CreateBrush( clipLo, clipHi );
	clipDoc.Map().SolidById( clipId )->sides[0].texture = QStringLiteral( "test/clip" );
	clipDoc.Map().SolidById( clipId )->sides[0].uaxis[3] = 23;
	bool clipOk = !clipDoc.ClipSelectedBoxes( 0, 0, 0 ) && clipDoc.Map().solids.size() == 1 &&
		clipDoc.ClipSelectedBoxes( 0, 8, 0 ) && clipDoc.Map().solids.size() == 2 && clipDoc.SelectedIds().size() == 2;
	QSet<int> clipFaceIds;
	for ( const SolidLite &solid : clipDoc.Map().solids )
	{
		clipOk = clipOk && ( solid.maxs[0] == 8 || solid.mins[0] == 8 );
		for ( const SideLite &side : solid.sides ) { clipOk = clipOk && !clipFaceIds.contains( side.id ); clipFaceIds.insert( side.id ); }
	}
	const SolidLite *clipFirst = clipDoc.Map().SolidById( clipId );
	clipOk = clipOk && clipFirst && clipFirst->sides[0].texture == QLatin1String( "test/clip" ) && clipFirst->sides[0].uaxis[3] == 23;
	QTemporaryDir clipTemp;
	MapDocLite clipReload;
	const QString clipFile = clipTemp.filePath( QStringLiteral( "clip.vmf" ) );
	clipOk = clipOk && clipTemp.isValid() && VmfIO::SaveMap( clipFile, clipDoc.Map() ) && VmfIO::LoadMap( clipFile, clipReload ) && clipReload.solids.size() == 2;
	clipOk = clipOk && clipDoc.Undo() && clipDoc.Map().solids.size() == 1 && clipDoc.Map().SolidById( clipId )->maxs[0] == 32 &&
		clipDoc.Redo() && clipDoc.Map().solids.size() == 2;
	const bool clipBoxStage = clipOk;
	CHammerDocument clipUpper;
	const int upperId = clipUpper.CreateBrush( clipLo, clipHi );
	clipOk = clipOk && clipUpper.ClipSelectedBoxes( 2, 12, 1 ) && clipUpper.Map().SolidById( upperId )->mins[2] == 12;
	clipUpper.Map().SolidById( upperId )->sides[0].hasVerticesPlus = true;
	clipOk = clipOk && !clipUpper.ClipSelectedBoxes( 2, 16, -1 ) && clipUpper.Map().SolidById( upperId )->mins[2] == 12;
	CHammerDocument clipMixed;
	const int clipMixedId = clipMixed.CreateBrush( clipLo, clipHi );
	EntityLite clipEntity{};
	clipEntity.id = clipMixed.Map().AllocId(); clipEntity.classname = QStringLiteral( "info_target" );
	clipMixed.Map().entities.append( clipEntity );
	clipMixed.SelectObjects( QSet<int>() << clipMixedId << clipEntity.id );
	clipOk = clipOk && !clipMixed.ClipSelectedBoxes( 1, 8, -1 ) && clipMixed.Map().SolidById( clipMixedId )->maxs[1] == 32;
	clipMixed.SelectObjects( QSet<int>() << clipMixedId );
	clipOk = clipOk && clipMixed.ClipSelectedBoxes( 1, 8, -1 ) && clipMixed.Map().SolidById( clipMixedId )->maxs[1] == 8;
	const bool clipMixedStage = clipOk;
	CHammerDocument clipWedge;
	const int wedgeId = clipWedge.CreateBrush( clipLo, clipHi );
	SolidLite *wedgeBrush = clipWedge.Map().SolidById( wedgeId );
	wedgeBrush->sides[2].planePts[0][1] = 16;
	wedgeBrush->sides[2].planePts[2][1] = 16;
	clipWedge.Map().RecalcSolidBounds( *wedgeBrush );
	QList<HammerWinding::Polygon> clippedFaces;
	const bool wedgeBuild = HammerWinding::BuildFaces( *wedgeBrush, clippedFaces );
	const bool wedgeClip = clipWedge.ClipSelectedBoxes( 0, 16, 0 );
	const bool wedgeCount = clipWedge.Map().solids.size() == 2;
	clipOk = clipOk && wedgeBuild && wedgeClip && wedgeCount;
	for ( const SolidLite &solid : clipWedge.Map().solids )
		clipOk = clipOk && HammerWinding::BuildFaces( solid, clippedFaces );
	const QString wedgeFile = clipTemp.filePath( QStringLiteral( "clip_wedge.vmf" ) );
	MapDocLite wedgeReload;
	clipOk = clipOk && VmfIO::SaveMap( wedgeFile, clipWedge.Map() ) && VmfIO::LoadMap( wedgeFile, wedgeReload ) &&
		wedgeReload.solids.size() == 2 && clipWedge.Undo() && clipWedge.Map().solids.size() == 1 &&
		clipWedge.Redo() && clipWedge.Map().solids.size() == 2;
	const bool clipWedgeStage = clipOk;
	// A sloped face may have valid texture axes that are parallel to the new
	// axis-aligned cut. The generated cut face must receive valid axes.
	CHammerDocument clipTetra;
	const int tetraId = clipTetra.CreateBrush( clipLo, clipHi );
	SolidLite *tetra = clipTetra.Map().SolidById( tetraId );
	const QList<SideLite> boxFaces = tetra->sides;
	tetra->sides = { boxFaces[1], boxFaces[3], boxFaces[5] };
	SideLite diagonal = boxFaces[0];
	const double triangle[3][3] = { { 32, 0, 0 }, { 0, 0, 32 }, { 0, 32, 0 } };
	for ( int p = 0; p < 3; ++p ) for ( int k = 0; k < 3; ++k ) diagonal.planePts[p][k] = triangle[p][k];
	for ( int k = 0; k < 3; ++k ) { diagonal.uaxis[k] = k == 0 ? 1 : 0; diagonal.vaxis[k] = k == 2 ? 1 : 0; }
	tetra->sides.append( diagonal );
	clipTetra.Map().RecalcSolidBounds( *tetra );
	clipOk = clipOk && HammerWinding::BuildFaces( *tetra, clippedFaces ) && clipTetra.ClipSelectedBoxes( 0, 8, -1 );
	bool foundValidCut = false;
	for ( const SideLite &side : clipTetra.Map().SolidById( tetraId )->sides )
		if ( side.planePts[0][0] == 8 && side.planePts[1][0] == 8 && side.planePts[2][0] == 8 )
			foundValidCut = std::abs( side.vaxis[1] * side.uaxis[2] - side.vaxis[2] * side.uaxis[1] ) > 1e-8;
	clipOk = clipOk && foundValidCut;
	ctx.Step( "clip-box-brushes", clipOk, QStringLiteral( "box=%1 mixed=%2 wedge=%3 tetra-cut=%4 wedgeBuild=%5 wedgeClip=%6 wedgeCount=%7" )
		.arg( clipBoxStage ).arg( clipMixedStage ).arg( clipWedgeStage ).arg( foundValidCut ).arg( wedgeBuild ).arg( wedgeClip ).arg( wedgeCount ) );

	// This four-plane tetrahedron exercises the sloped-face path of every
	// transform. The former box-only guard rejected it before changing anything.
	bool convexTransform = clipTetra.Undo();
	const double stretch[3] = { 2, 1, 1 };
	convexTransform = convexTransform && clipTetra.ScaleSelectedBoxes( stretch ) &&
		clipTetra.RotateSelectedBoxes90( false, 2 ) && clipTetra.FlipSelectedBoxes( 1 );
	const SolidLite *transformedTetra = clipTetra.Map().SolidById( tetraId );
	convexTransform = convexTransform && transformedTetra &&
		transformedTetra->sides.size() == 4 && HammerWinding::BuildFaces( *transformedTetra, clippedFaces ) &&
		std::abs( transformedTetra->maxs[0] - transformedTetra->mins[0] - 32 ) < 1e-5 &&
		std::abs( transformedTetra->maxs[1] - transformedTetra->mins[1] - 64 ) < 1e-5;
	const QString transformedFile = clipTemp.filePath( QStringLiteral( "transformed_tetra.vmf" ) );
	MapDocLite transformedReload;
	convexTransform = convexTransform && VmfIO::SaveMap( transformedFile, clipTetra.Map() ) &&
		VmfIO::LoadMap( transformedFile, transformedReload ) && transformedReload.SolidById( tetraId ) &&
		HammerWinding::BuildFaces( *transformedReload.SolidById( tetraId ), clippedFaces ) &&
		clipTetra.Undo() && clipTetra.Undo() && clipTetra.Undo() &&
		std::abs( clipTetra.Map().SolidById( tetraId )->mins[0] ) < 1e-5;
	ctx.Step( "convex-brush-transforms", convexTransform, QStringLiteral( "scale, quarter-turn, mirror, VMF and undo on a sloped brush" ) );

	{
		QTemporaryDir dir;
		CHammerDocument doc;
		EntityLite owner;
		owner.id = doc.Map().AllocId();
		owner.classname = QStringLiteral( "func_door" );
		owner.keyvalues.insert( QStringLiteral( "targetname" ), QStringLiteral( "prefab_door" ) );
		owner.connections.append( { QStringLiteral( "OnOpen" ), QStringLiteral( "prefab_door,Close,,1,-1" ) } );
		doc.Map().entities.append( owner );
		const double mins[3] = { 32, 64, 16 }, maxs[3] = { 48, 96, 24 };
		const int brushId = CreateBoxSolid( doc.Map(), mins, maxs );
		doc.Map().SolidById( brushId )->ownerEntityId = owner.id;
		doc.SelectObject( owner.id );
		QString error;
		const QString path = dir.filePath( QStringLiteral( "prefab.vmf" ) );
		bool ok = dir.isValid() && doc.SaveSelectionAsPrefab( path, &error ) && !doc.HasClipboardObjects();
		MapDocLite reloaded;
		ok = ok && VmfIO::LoadMap( path, reloaded, &error ) && reloaded.entities.size() == 1 && reloaded.solids.size() == 1 &&
			reloaded.solids.first().ownerEntityId == owner.id &&
			reloaded.versionProperties.contains( { QStringLiteral( "prefab" ), QStringLiteral( "1" ) } );
		ok = ok && doc.InsertOriginalPrefab( path, &error ) && doc.Map().entities.size() == 2 && doc.Map().solids.size() == 2 &&
			doc.Map().entities.last().id != owner.id && doc.Map().solids.last().id != brushId &&
			doc.Map().solids.last().ownerEntityId == doc.Map().entities.last().id &&
			doc.Map().solids.last().mins[0] == mins[0] &&
			doc.Map().entities.last().connections == owner.connections &&
			!doc.HasClipboardObjects();
		ok = ok && doc.Undo() && doc.Map().solids.size() == 1 && doc.Redo() && doc.Map().solids.size() == 2 &&
			!doc.InsertOriginalPrefab( dir.filePath( QStringLiteral( "missing.vmf" ) ), &error ) &&
			doc.Map().solids.size() == 2;
		ctx.Step( "prefab-roundtrip", ok, error );
	}


	// Three world points cut along x+y=32; orientation is VMF plane normal.
	const double slantedPoints[3][3] = { {0,32,0}, {32,0,0}, {0,32,32} };
	const double collinearPoints[3][3] = { {0,0,0}, {1,1,1}, {2,2,2} };
	const double nonfinitePoints[3][3] = { {0,0,0}, {1,0,0}, {0,std::numeric_limits<double>::infinity(),0} };
	CHammerDocument planeDoc;
	const int planeId = planeDoc.CreateBrush( clipLo, clipHi );
	const int originalFaceId = planeDoc.Map().SolidById( planeId )->sides[4].id;
	planeDoc.Map().SolidById( planeId )->sides[4].texture = QStringLiteral( "test/clip" );
	bool planeOk = !planeDoc.ClipSelectedByPlane( collinearPoints, 0 ) && !planeDoc.ClipSelectedByPlane( nonfinitePoints, 0 ) && planeDoc.Map().solids.size() == 1 &&
		planeDoc.ClipSelectedByPlane( slantedPoints, 0 ) && planeDoc.Map().solids.size() == 2;
	QSet<int> planeIds; bool keptFace = false;
	for ( const SolidLite &solid : planeDoc.Map().solids )
	{
		QList<HammerWinding::Polygon> polys; planeOk = planeOk && HammerWinding::BuildFaces( solid, polys );
		for ( const SideLite &side : solid.sides )
		{
			planeOk = planeOk && side.id > 0 && !planeIds.contains( side.id ); planeIds.insert( side.id );
			if ( side.id == originalFaceId ) keptFace = side.texture == QLatin1String( "test/clip" );
		}
	}
	const QString planePath = clipTemp.filePath( QStringLiteral( "clip_plane.vmf" ) );
	MapDocLite planeReload;
	planeOk = planeOk && keptFace && VmfIO::SaveMap( planePath, planeDoc.Map() ) && VmfIO::LoadMap( planePath, planeReload ) &&
		planeReload.solids.size() == 2 && planeDoc.Undo() && planeDoc.Map().solids.size() == 1 &&
		planeDoc.Redo() && planeDoc.Map().solids.size() == 2;
	CHammerDocument planeMixed;
	const int planeNearId = planeMixed.CreateBrush( clipLo, clipHi );
	const double farLo[3] = {128,128,0}, farHi[3] = {160,160,32};
	const int planeFarId = planeMixed.CreateBrush( farLo, farHi );
	planeMixed.SelectObjects( QSet<int>() << planeNearId << planeFarId );
	planeOk = planeOk && !planeMixed.ClipSelectedByPlane( slantedPoints, 0 ) && planeMixed.Map().solids.size() == 2 &&
		planeMixed.Map().SolidById( planeNearId )->sides.size() == 6;
	EntityLite owner{}; owner.id = planeMixed.Map().AllocId(); owner.classname = QStringLiteral( "func_detail" );
	planeMixed.Map().entities.append( owner ); planeMixed.Map().SolidById( planeNearId )->ownerEntityId = owner.id;
	planeMixed.SelectObjects( QSet<int>() << owner.id );
	planeOk = planeOk && planeMixed.ClipSelectedByPlane( slantedPoints, 0 ) && planeMixed.IsSelected( owner.id );
	int ownerCount = 0; for ( const SolidLite &solid : planeMixed.Map().solids ) if ( solid.ownerEntityId == owner.id ) ++ownerCount;
	planeOk = planeOk && ownerCount == 2 && planeMixed.Undo() && planeMixed.Redo();
	for ( const int keepSide : { -1, 1 } )
	{
		CHammerDocument sideDoc; sideDoc.CreateBrush( clipLo, clipHi );
		planeOk = planeOk && sideDoc.ClipSelectedByPlane( slantedPoints, keepSide );
		QList<HammerWinding::Polygon> sideFaces;
		planeOk = planeOk && HammerWinding::BuildFaces( sideDoc.Map().solids.first(), sideFaces );
		for ( const auto &face : sideFaces ) for ( const auto &vertex : face )
			planeOk = planeOk && ( keepSide < 0 ? vertex[0] + vertex[1] <= 32.0001 : vertex[0] + vertex[1] >= 31.9999 );
	}
	ctx.Step( "clip-three-point-plane", planeOk, QStringLiteral( "oblique plane, UV/IDs, owner, VMF roundtrip, undo and atomic rejection" ) );

	QTemporaryDir resetDir;
	const QString resetFgdPath = resetDir.filePath( QStringLiteral( "reset.fgd" ) );
	QFile resetFgd( resetFgdPath );
	bool resetOk = resetDir.isValid() && resetFgd.open( QIODevice::WriteOnly );
	if ( resetOk )
	{
		resetFgd.write( "@BaseClass = ResetBase [ basekey(string) : \"Base\" : \"inherited\" basezero(integer) : \"Zero\" : 0 baseempty(string) : \"Empty\" : \"\" spawnflags(flags) = [ 1 : \"One\" : 1 2 : \"Two\" : 0 ] mode(choices) : \"Mode\" : \"base\" = [ \"base\" : \"Base\" \"same\" : \"Base same\" ] ]\n"
			"@PointClass base(ResetBase) = reset_test [ name(string) : \"Name\" : \"default\" "
			"empty(string) : \"Empty\" : \"\" missing(string) : \"Missing\" zero(integer) : \"Zero\" : 0 "
			"missingnum(integer) : \"Missing number\" flags(flags) = [ 1 : \"One\" : 0 ] spawnflags(flags) = [ 4 : \"Four\" : 0 ] mode(choices) : \"Mode\" : \"leaf\" = [ \"leaf\" : \"Leaf\" \"same\" : \"Leaf same\" ] ]\n" );
		resetFgd.close();
	}
	CHammerDocument resetDoc;
	QString resetValue;
	const double resetOrigin[3] = { 0, 0, 0 };
	resetOk = resetOk && resetDoc.LoadFgd( resetFgdPath ) &&
		resetDoc.PlaceEntity( QStringLiteral( "reset_test" ), resetOrigin ) >= 0 &&
		resetDoc.Fgd().PropertyDefault( QStringLiteral( "reset_test" ), QStringLiteral( "BASEKEY" ), resetValue ) &&
		resetValue == QLatin1String( "inherited" ) &&
		resetDoc.Fgd().PropertyDefault( QStringLiteral( "reset_test" ), QStringLiteral( "empty" ), resetValue ) && resetValue.isEmpty() &&
		resetDoc.Fgd().PropertyDefault( QStringLiteral( "reset_test" ), QStringLiteral( "baseempty" ), resetValue ) && resetValue.isEmpty() &&
		resetDoc.Fgd().PropertyDefault( QStringLiteral( "reset_test" ), QStringLiteral( "basezero" ), resetValue ) && resetValue == QLatin1String( "0" ) &&
		resetDoc.Fgd().PropertyDefault( QStringLiteral( "reset_test" ), QStringLiteral( "zero" ), resetValue ) && resetValue == QLatin1String( "0" ) &&
		resetDoc.Fgd().PropertyDefault( QStringLiteral( "reset_test" ), QStringLiteral( "flags" ), resetValue ) && resetValue == QLatin1String( "0" ) &&
		resetDoc.Fgd().PropertyDefault( QStringLiteral( "reset_test" ), QStringLiteral( "spawnflags" ), resetValue ) && resetValue == QLatin1String( "1" ) &&
		resetDoc.Fgd().PropertyChoices( QStringLiteral( "reset_test" ), QStringLiteral( "mode" ) ).size() == 3 &&
		resetDoc.Fgd().PropertyChoices( QStringLiteral( "reset_test" ), QStringLiteral( "mode" ) ).contains( qMakePair( QStringLiteral( "same" ), QStringLiteral( "Leaf same" ) ) ) &&
		resetDoc.Fgd().PropertyChoices( QStringLiteral( "reset_test" ), QStringLiteral( "mode" ) ).contains( qMakePair( QStringLiteral( "base" ), QStringLiteral( "Base" ) ) ) &&
		resetDoc.Fgd().PropertyChoices( QStringLiteral( "reset_test" ), QStringLiteral( "mode" ) ).contains( qMakePair( QStringLiteral( "leaf" ), QStringLiteral( "Leaf" ) ) ) &&
		!resetDoc.Fgd().PropertyDefault( QStringLiteral( "reset_test" ), QStringLiteral( "missing" ), resetValue ) &&
		!resetDoc.Fgd().PropertyDefault( QStringLiteral( "reset_test" ), QStringLiteral( "missingnum" ), resetValue ) &&
		resetDoc.SetSelectedEntityKey( QStringLiteral( "name" ), QStringLiteral( "edited" ) ) &&
		resetDoc.SetSelectedEntityKey( QStringLiteral( "custom" ), QStringLiteral( "keep" ) ) &&
		resetDoc.ResetSelectedEntityKey( QStringLiteral( "NAME" ) );
	const EntityLite *resetEntity = resetDoc.Map().EntityById( resetDoc.SelectedId() );
	resetOk = resetOk && resetEntity && resetEntity->keyvalues.value( QStringLiteral( "name" ) ) == QLatin1String( "default" ) &&
		resetEntity->keyvalues.value( QStringLiteral( "custom" ) ) == QLatin1String( "keep" ) &&
		!resetDoc.ResetSelectedEntityKey( QStringLiteral( "custom" ) ) && resetDoc.Undo();
	resetEntity = resetDoc.Map().EntityById( resetDoc.SelectedId() );
	resetOk = resetOk && resetEntity && resetEntity->keyvalues.value( QStringLiteral( "name" ) ) == QLatin1String( "edited" ) && resetDoc.Redo();
	resetOk = resetOk && resetDoc.SetSelectedEntityKey( QStringLiteral( "empty" ), QStringLiteral( "changed" ) ) &&
		resetDoc.ResetSelectedEntityKey( QStringLiteral( "empty" ) ) && resetDoc.Map().EntityById( resetDoc.SelectedId() )->keyvalues.value( QStringLiteral( "empty" ) ).isEmpty() && resetDoc.Undo() &&
		resetDoc.Map().EntityById( resetDoc.SelectedId() )->keyvalues.value( QStringLiteral( "empty" ) ) == QLatin1String( "changed" ) &&
		resetDoc.SetSelectedEntityKey( QStringLiteral( "zero" ), QStringLiteral( "5" ) ) && resetDoc.ResetSelectedEntityKey( QStringLiteral( "zero" ) ) &&
		resetDoc.Map().EntityById( resetDoc.SelectedId() )->keyvalues.value( QStringLiteral( "zero" ) ) == QLatin1String( "0" ) && resetDoc.Undo() &&
		resetDoc.Map().EntityById( resetDoc.SelectedId() )->keyvalues.value( QStringLiteral( "zero" ) ) == QLatin1String( "5" );
	ctx.Step( "entity-reset-default", resetOk, QStringLiteral( "inherited and explicit empty/zero defaults, omitted defaults and undo" ) );
	// Region selection uses the original BoundBox predicates on projected bounds.
	bool regionOk = true;
	for ( int axis = 0; axis < 3; ++axis )
	{
		CHammerDocument regionDoc;
		const double aMin[3] = { -16, -16, -16 }, aMax[3] = { 16, 16, 16 };
		const double bMin[3] = { 24, 24, 24 }, bMax[3] = { 56, 56, 56 };
		const int a = regionDoc.CreateBrush( aMin, aMax );
		const int b = regionDoc.CreateBrush( bMin, bMax );
		CQtViewport2D view( static_cast<CQtViewport2D::EViewAxis>( axis ) );
		view.resize( 200, 200 ); view.setDocument( &regionDoc );
		auto drag = [&]( Qt::KeyboardModifiers modifiers ) {
			QMouseEvent down( QEvent::MouseButtonPress, QPointF( 88, 84 ), Qt::LeftButton, Qt::LeftButton, modifiers );
			QMouseEvent move( QEvent::MouseMove, QPointF( 116, 112 ), Qt::NoButton, Qt::LeftButton, modifiers );
			QMouseEvent up( QEvent::MouseButtonRelease, QPointF( 116, 112 ), Qt::LeftButton, Qt::NoButton, modifiers );
			QCoreApplication::sendEvent( &view, &down ); QCoreApplication::sendEvent( &view, &move ); QCoreApplication::sendEvent( &view, &up );
		};
		drag( Qt::NoModifier );
		regionOk = regionOk && regionDoc.SelectedIds() == ( QSet<int>() << a << b );
		drag( Qt::ShiftModifier );
		regionOk = regionOk && regionDoc.SelectedIds() == ( QSet<int>() << a );
		QMouseEvent down( QEvent::MouseButtonPress, QPointF( 88, 84 ), Qt::LeftButton, Qt::LeftButton, Qt::NoModifier );
		QCoreApplication::sendEvent( &view, &down );
		QKeyEvent escape( QEvent::KeyPress, Qt::Key_Escape, Qt::NoModifier );
		QCoreApplication::sendEvent( &view, &escape );
		QMouseEvent up( QEvent::MouseButtonRelease, QPointF( 116, 112 ), Qt::LeftButton, Qt::NoButton, Qt::NoModifier );
		QCoreApplication::sendEvent( &view, &up );
		regionOk = regionOk && regionDoc.SelectedIds() == ( QSet<int>() << a );
		QCoreApplication::sendEvent( &view, &down );
		QEvent lostFocus( QEvent::FocusOut ); QCoreApplication::sendEvent( &view, &lostFocus );
		QCoreApplication::sendEvent( &view, &up );
		regionOk = regionOk && regionDoc.SelectedIds() == ( QSet<int>() << a );
	}
	ctx.Step( "2d-region-selection", regionOk, QStringLiteral( "all axes, intersection, shift containment, Escape and focus loss" ) );
	// A face hit must not paint a hidden brush or alter its other faces.
	CHammerDocument paintDoc;
	const double frontMin[3] = { -16, -16, -16 }, frontMax[3] = { 16, 16, 16 };
	const double backMin[3] = { 32, -16, -16 }, backMax[3] = { 64, 16, 16 };
	const int frontId = paintDoc.CreateBrush( frontMin, frontMax );
	const int backId = paintDoc.CreateBrush( backMin, backMax );
	const double paintEye[3] = { -100, 0, 0 }, paintRay[3] = { 1, 0, 0 };
	const auto faceHit = HammerFaceHit::Find( paintDoc, paintEye, paintRay, 1.0 );
	bool facePaintOk = faceHit.solidId == frontId && faceHit.faceIndex >= 0 &&
		paintDoc.SetActiveMaterial( QStringLiteral( "dev/dev_measuregeneric01" ) );
	QString oldFace;
	if ( facePaintOk )
	{
		oldFace = paintDoc.Map().SolidById( frontId )->sides[faceHit.faceIndex].texture;
		facePaintOk = paintDoc.ApplyMaterialToFace( frontId, faceHit.faceIndex );
	}
	if ( facePaintOk )
	{
		const SolidLite *front = paintDoc.Map().SolidById( frontId );
		const SolidLite *back = paintDoc.Map().SolidById( backId );
		for ( int i = 0; i < front->sides.size(); ++i )
			facePaintOk = facePaintOk && front->sides[i].texture == ( i == faceHit.faceIndex ? QStringLiteral( "dev/dev_measuregeneric01" ) : oldFace );
		for ( const SideLite &side : back->sides ) facePaintOk = facePaintOk && side.texture == oldFace;
	}
	QTemporaryDir facePaintTemp;
	MapDocLite reloadedFace;
	const QString facePaintPath = facePaintTemp.filePath( QStringLiteral( "paint.vmf" ) );
	facePaintOk = facePaintOk && facePaintTemp.isValid() && VmfIO::SaveMap( facePaintPath, paintDoc.Map() ) &&
		VmfIO::LoadMap( facePaintPath, reloadedFace ) && reloadedFace.SolidById( frontId ) &&
		reloadedFace.SolidById( frontId )->sides[faceHit.faceIndex].texture == QLatin1String( "dev/dev_measuregeneric01" ) &&
		paintDoc.Undo() && paintDoc.Map().SolidById( frontId )->sides[faceHit.faceIndex].texture == oldFace &&
		paintDoc.Redo() && paintDoc.Map().SolidById( frontId )->sides[faceHit.faceIndex].texture == QLatin1String( "dev/dev_measuregeneric01" );
	ctx.Step( "3d-face-material", facePaintOk, QStringLiteral( "nearest face, occlusion, VMF roundtrip and undo" ) );
	// The material tool's face selection is session-only and separate from
	// object selection. Exercise actual 3D click, Ctrl toggle and Enter events.
	{
		CHammerDocument doc;
		const double aMin[3] = { 32,-16,84 }, aMax[3] = { 64,16,116 };
		const double bMin[3] = { 160,-16,84 }, bMax[3] = { 192,16,116 };
		const int a = doc.CreateBrush( aMin, aMax ), b = doc.CreateBrush( bMin, bMax );
		EntityLite owner; owner.id = doc.Map().AllocId(); owner.classname = QStringLiteral( "func_detail" );
		doc.Map().entities.append( owner ); doc.Map().SolidById( b )->ownerEntityId = owner.id;
		doc.SetTool( TOOL_TEXTURE ); doc.SetActiveMaterial( QStringLiteral( "dev/face_multi" ) );
		CQtViewport3D view; view.resize( 400, 300 ); view.setDocument( &doc );
		const double eyeA[3] = { 0,0,100 }, eyeB[3] = { 128,0,100 }, angles[3] = { 0,0,0 };
		view.setCameraPositionAngles( eyeA, angles );
		auto click = [&]( Qt::MouseButton button, Qt::KeyboardModifiers modifiers ) {
			QMouseEvent down( QEvent::MouseButtonPress, QPointF( 200,150 ), button, button, modifiers );
			QMouseEvent up( QEvent::MouseButtonRelease, QPointF( 200,150 ), button, Qt::NoButton, modifiers );
			QCoreApplication::sendEvent( &view, &down ); QCoreApplication::sendEvent( &view, &up );
		};
		QImage unselected( view.size(), QImage::Format_ARGB32_Premultiplied ); unselected.fill( Qt::transparent ); view.render( &unselected );
		click( Qt::LeftButton, Qt::NoModifier );
		const QSet<int> firstFaces = view.selectedFaces().value( a );
		QImage selected( view.size(), QImage::Format_ARGB32_Premultiplied ); selected.fill( Qt::transparent ); view.render( &selected );
		bool ok = firstFaces.size() == 1 && !view.selectedFaces().contains( b ) && unselected.pixel( 200,150 ) != selected.pixel( 200,150 );
		QMouseEvent orbitDown( QEvent::MouseButtonPress, QPointF( 200,150 ), Qt::LeftButton, Qt::LeftButton, Qt::NoModifier );
		QMouseEvent orbitMove( QEvent::MouseMove, QPointF( 230,150 ), Qt::NoButton, Qt::LeftButton, Qt::NoModifier );
		QMouseEvent orbitUp( QEvent::MouseButtonRelease, QPointF( 230,150 ), Qt::LeftButton, Qt::NoButton, Qt::NoModifier );
		QCoreApplication::sendEvent( &view, &orbitDown ); QCoreApplication::sendEvent( &view, &orbitMove ); QCoreApplication::sendEvent( &view, &orbitUp );
		ok = ok && view.selectedFaces().value( a ) == firstFaces;
		const int faceA = firstFaces.isEmpty() ? -1 : *firstFaces.cbegin();
		QString originalA;
		for ( const SideLite &side : doc.Map().SolidById( a )->sides ) if ( side.id == faceA ) originalA = side.texture;
		view.setCameraPositionAngles( eyeB, angles ); click( Qt::LeftButton, Qt::ControlModifier );
		const QSet<int> secondFaces = view.selectedFaces().value( b );
		const int faceB = secondFaces.isEmpty() ? -1 : *secondFaces.cbegin();
		ok = ok && view.selectedFaces().value( a ).contains( faceA ) && secondFaces.size() == 1;
		click( Qt::LeftButton, Qt::ControlModifier );
		ok = ok && !view.selectedFaces().contains( b ) && view.selectedFaces().contains( a );
		click( Qt::LeftButton, Qt::ControlModifier );
		QMap<int, QSet<int>> invalid = view.selectedFaces(); invalid[b].insert( 99999999 );
		ok = ok && !doc.ApplyMaterialToFaces( invalid ) && view.selectedFaces().size() == 2;
		QKeyEvent apply( QEvent::KeyPress, Qt::Key_Return, Qt::NoModifier ); QCoreApplication::sendEvent( &view, &apply );
		auto textureOf = [&]( int solidId, int id ) {
			const SolidLite *solid = doc.Map().SolidById( solidId );
			if ( solid ) for ( const SideLite &side : solid->sides ) if ( side.id == id ) return side.texture;
			return QString();
		};
		ok = ok && view.selectedFaces().isEmpty() && textureOf( a, faceA ) == QLatin1String( "dev/face_multi" ) &&
			textureOf( b, faceB ) == QLatin1String( "dev/face_multi" ) && doc.Undo() && textureOf( a, faceA ) == originalA;
		view.setCameraPositionAngles( eyeB, angles ); click( Qt::LeftButton, Qt::NoModifier );
		ViewFilter filter = doc.GetViewFilter(); filter.quickHiddenIds.insert( owner.id ); doc.SetViewFilter( filter );
		ok = ok && view.selectedFaces().isEmpty();
		filter.quickHiddenIds.clear(); doc.SetViewFilter( filter ); click( Qt::LeftButton, Qt::NoModifier );
		QKeyEvent escape( QEvent::KeyPress, Qt::Key_Escape, Qt::NoModifier ); QCoreApplication::sendEvent( &view, &escape );
		ok = ok && view.selectedFaces().isEmpty(); click( Qt::LeftButton, Qt::NoModifier );
		doc.SetTool( TOOL_SELECT ); ok = ok && view.selectedFaces().isEmpty();
		doc.SetTool( TOOL_TEXTURE ); click( Qt::LeftButton, Qt::NoModifier );
		ok = ok && !view.selectedFaces().isEmpty() && doc.NewDocument() && view.selectedFaces().isEmpty();
		ctx.Step( "3d-face-selection", ok, QStringLiteral( "3D click, occlusion, Ctrl toggle, material apply, undo, highlight and hidden guard" ) );
	}
	// Face-property editing also accepts explicitly picked 3D faces, independent
	// of object selection. A stale/hidden/ambiguous face rejects the whole edit.
	{
		CHammerDocument doc;
		const double loA[3] = { 0,0,0 }, hiA[3] = { 64,64,64 };
		const double loB[3] = { 128,0,0 }, hiB[3] = { 192,64,64 };
		const int a = doc.CreateBrush( loA, hiA ), b = doc.CreateBrush( loB, hiB );
		EntityLite owner; owner.id = doc.Map().AllocId(); owner.classname = QStringLiteral( "func_detail" );
		doc.Map().entities.append( owner ); doc.Map().SolidById( b )->ownerEntityId = owner.id;
		doc.SelectObjects( {} );
		const SideLite originalA = doc.Map().SolidById( a )->sides[0];
		const SideLite originalB = doc.Map().SolidById( b )->sides[0];
		const QString untouchedA = doc.Map().SolidById( a )->sides[1].texture;
		SideLite editedA = originalA, editedB = originalB;
		editedA.texture = QStringLiteral( "brick/face_edit_a" ); editedA.uaxis[3] += 8;
		editedB.texture = QStringLiteral( "brick/face_edit_b" ); editedB.vscale = 0.5f;
		QMap<int, QList<SideLite>> edits { { a, { editedA } }, { b, { editedB } } };
		QMap<int, QList<SideLite>> bad = edits; bad[b][0].uscale = 0;
		bool ok = !doc.SetVisibleFaceProperties( bad ) && doc.Map().SolidById( a )->sides[0].texture == originalA.texture &&
			doc.SetVisibleFaceProperties( edits ) && doc.Map().SolidById( a )->sides[0].texture == editedA.texture &&
			doc.Map().SolidById( b )->sides[0].texture == editedB.texture &&
			doc.Map().SolidById( a )->sides[1].texture == untouchedA && doc.Map().SolidById( b )->ownerEntityId == owner.id &&
			doc.Undo() && doc.Map().SolidById( a )->sides[0].texture == originalA.texture &&
			doc.Map().SolidById( b )->sides[0].texture == originalB.texture && doc.Redo();
		ViewFilter hidden = doc.GetViewFilter(); hidden.quickHiddenIds.insert( owner.id ); doc.SetViewFilter( hidden );
		ok = ok && !doc.SetVisibleFaceProperties( edits );
		hidden.quickHiddenIds.clear(); doc.SetViewFilter( hidden );
		bad = edits; bad[b][0].id = 99999999;
		ok = ok && !doc.SetVisibleFaceProperties( bad );
		const int savedId = doc.Map().SolidById( b )->sides[1].id;
		doc.Map().SolidById( b )->sides[1].id = originalA.id;
		ok = ok && !doc.SetVisibleFaceProperties( edits );
		doc.Map().SolidById( b )->sides[1].id = savedId;
		ctx.Step( "3d-selected-face-properties", ok, QStringLiteral( "picked world/entity faces, atomic validation, visibility, IDs and undo" ) );
	}
	RunViewFilterSelfTest( ctx );
	RunIoHardeningSelfTest( ctx );
	{
		QTemporaryDir temp;
		QFile fgd( temp.filePath( QStringLiteral( "brush.fgd" ) ) );
		bool ok = temp.isValid() && fgd.open( QIODevice::WriteOnly );
		if ( ok ) { fgd.write( "@SolidClass = func_test [ targetname(string) : \"Name\" : \"\" ]\n" ); fgd.close(); }
		CHammerDocument doc;
		ok = ok && doc.LoadFgd( fgd.fileName() );
		const double lo[3] = { 128, 128, 0 }, hi[3] = { 192, 192, 64 };
		const int first = doc.CreateBrush( lo, hi ), second = doc.CreateBrush( lo, hi );
		doc.SelectObjects( QSet<int>() << first << second );
		ok = ok && doc.TieSelectionToEntity( QStringLiteral( "func_test" ) );
		const int owner = doc.SelectedId();
		ok = ok && doc.Map().EntityById( owner ) && !doc.Map().EntityById( owner )->hasExplicitOrigin &&
			doc.Map().SolidById( first )->ownerEntityId == owner && doc.Map().SolidById( second )->ownerEntityId == owner;
		const QString brushPath = temp.filePath( QStringLiteral( "brush-origin.vmf" ) );
		MapDocLite loadedBrush;
		ok = ok && VmfIO::SaveMap( brushPath, doc.Map() ) && VmfIO::LoadMap( brushPath, loadedBrush ) &&
			loadedBrush.EntityById( owner ) && !loadedBrush.EntityById( owner )->hasExplicitOrigin;
		const double explicitOrigin[3] = { 0, 0, 0 };
		ok = ok && doc.SetSelectedEntityOrigin( explicitOrigin ) && VmfIO::SaveMap( brushPath, doc.Map() ) &&
			VmfIO::LoadMap( brushPath, loadedBrush ) && loadedBrush.EntityById( owner ) && loadedBrush.EntityById( owner )->hasExplicitOrigin && doc.Undo();
		doc.SelectObject( first );
		const double delta[3] = { 16, 0, 0 };
		const double fineDelta[3] = { 3, 0, 0 };
		ok = ok && doc.SelectedId() == owner && doc.MoveSelection( delta ) && doc.Map().SolidById( first )->mins[0] == 144 &&
			doc.MoveSelection( fineDelta ) && doc.SnapSelectionToGrid( false ) && doc.Map().SolidById( first )->mins[0] == 144;
		const double selectedOrigin[3] = { 16, 0, 0 };
		const double scale[3] = { 2, 1, 1 };
		ok = ok && doc.SetSelectedEntityOrigin( selectedOrigin ) && doc.ScaleSelectedBoxes( scale ) &&
			doc.Map().SolidById( first )->mins[0] == 112 && doc.Map().EntityById( owner )->origin[0] == -144 && doc.Undo() &&
			doc.RotateSelectedBoxes90( false, 2 ) && doc.Map().EntityById( owner )->origin[0] == 336 && doc.Undo() &&
			doc.FlipSelectedBoxes( 0 ) && doc.Map().EntityById( owner )->origin[0] == 336 && doc.Undo() &&
			doc.SetActiveMaterial( QStringLiteral( "dev/brush_test" ) ) && doc.ApplyMaterialToSelection() &&
			doc.Map().SolidById( second )->sides[0].texture == QLatin1String( "dev/brush_test" ) &&
			doc.CopySelection() && doc.PasteSelection() >= 0;
		const int copyOwner = doc.SelectedId();
		int copies = 0;
		for ( const SolidLite &solid : doc.Map().solids ) if ( solid.ownerEntityId == copyOwner ) ++copies;
		ok = ok && copyOwner != owner && copies == 2 && doc.DeleteSelection() && !doc.Map().EntityById( copyOwner ) && doc.Undo();
		doc.SelectObject( owner );
		ok = ok && doc.MoveSelectionToWorld() && !doc.Map().EntityById( owner ) && doc.Map().SolidById( first )->ownerEntityId == 0 &&
			doc.Undo() && doc.Map().EntityById( owner ) && doc.Map().SolidById( first )->ownerEntityId == owner;
		ctx.Step( "brush-entity-document", ok, QStringLiteral( "tie, selection, move, copy ownership, delete, untie and undo" ) );
	}
	{
		QTemporaryDir temp;
		QFile fgd( temp.filePath( QStringLiteral( "visgroup-tie.fgd" ) ) );
		bool ok = temp.isValid() && fgd.open( QIODevice::WriteOnly );
		if ( ok ) { fgd.write( "@SolidClass = func_test [ ]\n" ); fgd.close(); }
		CHammerDocument doc;
		ok = ok && doc.LoadFgd( fgd.fileName() );
		const double lo[3] = { 0, 0, 0 }, hi[3] = { 64, 64, 64 };
		const int brush = doc.CreateBrush( lo, hi );
		doc.SelectObject( brush ); doc.SetDirty( false );
		const QList<QPair<QString, QString>> protectedKeys = {
			qMakePair( QStringLiteral( "visgroupid" ), QStringLiteral( "11" ) ),
			qMakePair( QStringLiteral( "colorvisgroupid" ), QStringLiteral( "11" ) ),
			qMakePair( QStringLiteral( "visgroupshown" ), QStringLiteral( "0" ) ),
			qMakePair( QStringLiteral( "visgroupautoshown" ), QStringLiteral( "0" ) ) };
		for ( const auto &property : protectedKeys )
		{
			SolidLite *solid = doc.Map().SolidById( brush );
			solid->editorProperties = { property }; solid->hasEditor = true;
			ok = ok && !doc.TieSelectionToEntity( QStringLiteral( "func_test" ) ) && !doc.IsDirty() &&
				doc.Map().SolidById( brush )->ownerEntityId == 0 && doc.Map().SolidById( brush )->editorProperties == QList<QPair<QString, QString>>{ property };
		}
		doc.Map().SolidById( brush )->editorProperties.clear();
		doc.Map().SolidById( brush )->hiddenWrapper = true;
		ok = ok && !doc.TieSelectionToEntity( QStringLiteral( "func_test" ) ) && !doc.IsDirty();
		doc.Map().SolidById( brush )->hiddenWrapper = false;
		doc.Map().SolidById( brush )->editorProperties = { qMakePair( QStringLiteral( "visgroupshown" ), QStringLiteral( "1" ) ) };
		ok = ok && doc.TieSelectionToEntity( QStringLiteral( "func_test" ) );
		const int owner = doc.SelectedId(); doc.SetDirty( false );
		doc.Map().EntityById( owner )->editorProperties = { protectedKeys.first() };
		ok = ok && !doc.MoveSelectionToWorld() && !doc.IsDirty() && doc.Map().EntityById( owner ) &&
			doc.Map().SolidById( brush )->ownerEntityId == owner;
		doc.Map().EntityById( owner )->editorProperties.clear();
		doc.Map().SolidById( brush )->editorProperties = { protectedKeys.first() };
		ok = ok && !doc.MoveSelectionToWorld() && !doc.IsDirty() && doc.Map().EntityById( owner ) &&
			doc.Map().SolidById( brush )->ownerEntityId == owner;
		ctx.Step( "brush-entity-visgroup-guard", ok, QStringLiteral( "tie/untie reject hidden and group metadata without mutation; default shown accepted" ) );
	}
	{
		QTemporaryDir temp;
		CHammerDocument doc; doc.NewDocument();
		const double lo[3] = { 0, 0, 0 }, hi[3] = { 64, 64, 64 }, step[3] = { 16, 0, 0 };
		const int brush = doc.CreateBrush( lo, hi );
		doc.Map().SolidById( brush )->editorProperties.append( qMakePair( QStringLiteral( "visgroupid" ), QStringLiteral( "1" ) ) );
		doc.Map().SolidById( brush )->hasEditor = true;
		doc.SelectObject( brush );
		const QString path = temp.filePath( QStringLiteral( "clipboard-source.vmf" ) );
		bool ok = temp.isValid() && doc.CopySelection() && doc.SaveToFile( path ) &&
			!doc.ClipboardHasForeignVisgroupState() && doc.PasteSelection() >= 0;
		ok = ok && doc.LoadFromFile( path ) && doc.ClipboardHasForeignVisgroupState();
		const int beforeId = doc.Map().PeekNextId(), beforeSolids = doc.Map().solids.size();
		ok = ok && doc.PasteSelection() < 0 && doc.PasteSpecial( 2, step ) < 0 &&
			doc.Map().PeekNextId() == beforeId && doc.Map().solids.size() == beforeSolids && !doc.IsDirty();
		ok = ok && doc.NewDocument() && doc.ClipboardHasForeignVisgroupState() &&
			doc.PasteSelection() < 0 && doc.PasteSpecial( 2, step ) < 0 && doc.Map().solids.isEmpty() && !doc.IsDirty();
		const int plain = doc.CreateBrush( lo, hi ); doc.SelectObject( plain );
		ok = ok && doc.CopySelection() && doc.NewDocument() && !doc.ClipboardHasForeignVisgroupState() && doc.PasteSelection() >= 0;
		ctx.Step( "foreign-visgroup-clipboard", ok, QStringLiteral( "same-map save preserves paste; reopen/new reject group metadata atomically; plain cross-map paste works" ) );
	}
	{
		QTemporaryDir temp;
		QFile fgd( temp.filePath( QStringLiteral( "center-origin.fgd" ) ) );
		bool ok = temp.isValid() && fgd.open( QIODevice::WriteOnly );
		if ( ok ) { fgd.write( "@SolidClass = func_test [ ]\n" ); fgd.close(); }
		CHammerDocument doc;
		ok = ok && doc.LoadFgd( fgd.fileName() );
		const double lo[3] = { 128, 0, 0 }, hi[3] = { 192, 64, 64 };
		const int brush = doc.CreateBrush( lo, hi );
		doc.SelectObject( brush );
		ok = ok && doc.TieSelectionToEntity( QStringLiteral( "func_test" ) );
		const int owner = doc.SelectedId();
		// Implicit origins stay absent; centering must not create a new key.
		ok = ok && !doc.CenterSelectedBrushEntityOrigins() && !doc.Map().EntityById( owner )->hasExplicitOrigin;
		const double origin[3] = { 8, 16, 32 };
		ok = ok && doc.SetSelectedEntityOrigin( origin );
		const SolidLite original = *doc.Map().SolidById( brush );
		double center[3];
		for ( int axis = 0; axis < 3; ++axis ) center[axis] = original.mins[axis] / 2 + original.maxs[axis] / 2;
		ok = ok && doc.CenterSelectedBrushEntityOrigins();
		for ( int axis = 0; axis < 3; ++axis )
			ok = ok && doc.Map().EntityById( owner )->origin[axis] == center[axis] &&
				doc.Map().SolidById( brush )->mins[axis] == original.mins[axis] && doc.Map().SolidById( brush )->maxs[axis] == original.maxs[axis];
		const QString path = temp.filePath( QStringLiteral( "centered.vmf" ) );
		MapDocLite restored;
		ok = ok && VmfIO::SaveMap( path, doc.Map() ) && VmfIO::LoadMap( path, restored ) && restored.EntityById( owner );
		for ( int axis = 0; ok && axis < 3; ++axis ) ok = restored.EntityById( owner )->origin[axis] == center[axis];
		ok = ok && doc.Undo();
		for ( int axis = 0; axis < 3; ++axis ) ok = ok && doc.Map().EntityById( owner )->origin[axis] == origin[axis];
		ok = ok && doc.Redo() && !doc.CenterSelectedBrushEntityOrigins();
		ctx.Step( "center-brush-entity-origins", ok, QStringLiteral( "explicit origin only, unchanged geometry, VMF roundtrip and undo/redo" ) );
	}
	{
		QTemporaryDir temp;
		QFile fgd( temp.filePath( QStringLiteral( "geometry.fgd" ) ) );
		bool ok = temp.isValid() && fgd.open( QIODevice::WriteOnly );
		if ( ok ) { fgd.write( "@SolidClass = func_test [ ]\n" ); fgd.close(); }
		CHammerDocument doc;
		ok = ok && doc.LoadFgd( fgd.fileName() );
		const double lo[3] = { 0, 0, 0 }, hi[3] = { 128, 128, 128 };
		const int original = doc.CreateBrush( lo, hi );
		doc.SelectObject( original );
		ok = ok && doc.TieSelectionToEntity( QStringLiteral( "func_test" ) );
		const int owner = doc.SelectedId();
		ok = ok && doc.ClipSelectedBoxes( 0, 64, 0 ) && doc.SelectedIds() == ( QSet<int>() << owner );
		int children = 0;
		for ( const SolidLite &solid : doc.Map().solids ) if ( solid.ownerEntityId == owner ) ++children;
		ok = ok && children == 2 && doc.Undo() && doc.Map().SolidById( original ) &&
			doc.Map().SolidById( original )->ownerEntityId == owner && doc.Redo();
		const QString path = temp.filePath( QStringLiteral( "clipped.vmf" ) );
		MapDocLite restored;
		ok = ok && VmfIO::SaveMap( path, doc.Map() ) && VmfIO::LoadMap( path, restored );
		children = 0;
		for ( const SolidLite &solid : restored.solids ) if ( solid.ownerEntityId == owner ) ++children;
		ok = ok && children == 2 && doc.Undo() && doc.HollowSelection( 8 ) && doc.SelectedIds() == ( QSet<int>() << owner );
		children = 0;
		for ( const SolidLite &solid : doc.Map().solids ) if ( solid.ownerEntityId == owner ) ++children;
		ok = ok && children == 6 && doc.Undo() && doc.Map().SolidById( original ) && doc.Redo();
		ctx.Step( "brush-entity-clip-hollow", ok, QStringLiteral( "clip and hollow preserve ownership, selection, VMF and undo" ) );
	}
	{
		QTemporaryDir temp;
		QFile fgd( temp.filePath( QStringLiteral( "mixed_geometry.fgd" ) ) );
		bool ok = temp.isValid() && fgd.open( QIODevice::WriteOnly );
		if ( ok ) { fgd.write( "@SolidClass = func_test [ ]\n" ); fgd.close(); }
		CHammerDocument doc;
		ok = ok && doc.LoadFgd( fgd.fileName() );
		const double lo1[3] = { 0, 0, 0 }, hi1[3] = { 128, 64, 64 };
		const double lo2[3] = { 0, 128, 0 }, hi2[3] = { 128, 192, 64 };
		const double low[3] = { 32, 256, 0 }, hiw[3] = { 96, 320, 64 };
		const int a = doc.CreateBrush( lo1, hi1 ), b = doc.CreateBrush( lo2, hi2 );
		doc.SelectObjects( QSet<int>() << a << b );
		ok = ok && doc.TieSelectionToEntity( QStringLiteral( "func_test" ) );
		const int owner = doc.SelectedId();
		const int world = doc.CreateBrush( low, hiw );
		doc.SelectObjects( QSet<int>() << owner << world );
		const QString beforePath = temp.filePath( QStringLiteral( "before.vmf" ) );
		const QString afterPath = temp.filePath( QStringLiteral( "after.vmf" ) );
		auto savedBytes = [&]( const QString &path ) {
			if ( !VmfIO::SaveMap( path, doc.Map() ) ) return QByteArray();
			QFile file( path );
			return file.open( QIODevice::ReadOnly ) ? file.readAll() : QByteArray();
		};
		const QByteArray before = savedBytes( beforePath );
		ok = ok && !before.isEmpty() && !doc.ClipSelectedBoxes( 0, 120, 0 ) && savedBytes( afterPath ) == before;
		EntityLite point{}; point.id = doc.Map().AllocId(); point.classname = QStringLiteral( "info_target" );
		doc.Map().entities.append( point );
		doc.SelectObjects( QSet<int>() << owner << world << point.id );
		const QByteArray withPoint = savedBytes( beforePath );
		ok = ok && !doc.ClipSelectedBoxes( 0, 64, 0 ) && !doc.HollowSelection( 8 ) &&
			!withPoint.isEmpty() && savedBytes( afterPath ) == withPoint;
		doc.Map().RemoveById( point.id );
		doc.SelectObjects( QSet<int>() << owner << world );
		ok = ok && doc.ClipSelectedBoxes( 0, 64, 0 );
		int owned = 0, worldCount = 0;
		for ( const SolidLite &s : doc.Map().solids ) { if ( s.ownerEntityId == owner ) ++owned; else if ( !s.ownerEntityId ) ++worldCount; }
		ok = ok && owned == 4 && worldCount == 2 && doc.SelectedIds().contains( owner ) && doc.Undo() &&
			doc.Map().solids.size() == 3 && doc.Redo() && doc.Map().solids.size() == 6 && doc.Undo();
		ok = ok && doc.HollowSelection( -8 );
		owned = worldCount = 0;
		for ( const SolidLite &s : doc.Map().solids ) { if ( s.ownerEntityId == owner ) ++owned; else if ( !s.ownerEntityId ) ++worldCount; }
		ok = ok && owned == 12 && worldCount == 6 && doc.Undo() && doc.Map().solids.size() == 3 &&
			doc.Redo() && doc.Map().solids.size() == 18;
		ctx.Step( "brush-entity-mixed-geometry", ok, QStringLiteral( "mixed ownership, atomic rejection, outward hollow and undo/redo" ) );
	}
	// A partly hidden brush entity remains selectable through a visible child.
	// Geometry operations must never silently modify its unseen children.
	{
		QTemporaryDir temp;
		QFile fgd( temp.filePath( QStringLiteral( "hidden_geometry.fgd" ) ) );
		bool ok = temp.isValid() && fgd.open( QIODevice::WriteOnly );
		if ( ok ) { fgd.write( "@SolidClass = func_test [ ]\n" ); fgd.close(); }
		CHammerDocument doc;
		ok = ok && doc.LoadFgd( fgd.fileName() );
		const double lo1[3] = { 1, 1, 1 }, hi1[3] = { 129, 65, 65 };
		const double lo2[3] = { 1, 129, 1 }, hi2[3] = { 129, 193, 65 };
		const double low[3] = { 1, 257, 1 }, hiw[3] = { 129, 321, 65 };
		const int a = doc.CreateBrush( lo1, hi1 ), b = doc.CreateBrush( lo2, hi2 );
		doc.SelectObjects( QSet<int>{ a, b } );
		ok = ok && doc.TieSelectionToEntity( QStringLiteral( "func_test" ) );
		const int owner = doc.SelectedId();
		doc.Map().EntityById( owner )->hasExplicitOrigin = true;
		const int world = doc.CreateBrush( low, hiw );
		doc.SelectObjects( QSet<int>{ owner, world } );
		const QString base = temp.filePath( QStringLiteral( "hidden_before.vmf" ) );
		const QString after = temp.filePath( QStringLiteral( "hidden_after.vmf" ) );
		auto mapBytes = [&]( const QString &path ) {
			if ( !VmfIO::SaveMap( path, doc.Map() ) ) return QByteArray();
			QFile file( path ); return file.open( QIODevice::ReadOnly ) ? file.readAll() : QByteArray();
		};
		ok = ok && doc.SaveToFile( base );
		const QByteArray before = mapBytes( base );
		ViewFilter filter = doc.GetViewFilter(); filter.quickHiddenIds.insert( b ); doc.SetViewFilter( filter );
		const double delta[3] = { 16, 0, 0 }, factors[3] = { 2, 1, 1 };
		const double plane[3][3] = { {64,0,0}, {64,1,0}, {64,0,1} };
		doc.BeginEdit();
		ok = ok && doc.SelectedIds().contains( owner ) && !doc.MoveSelection( delta ) && !doc.MoveSelection( delta );
		doc.EndEdit();
		ok = ok && !doc.SnapSelectionToGrid( false ) && !doc.SnapSelectionToGrid( true ) &&
			!doc.ScaleSelectedBoxes( factors ) && !doc.RotateSelectedBoxes90( false, 2 ) && !doc.FlipSelectedBoxes( 0 ) &&
			!doc.ClipSelectedBoxes( 0, 64, 0 ) && !doc.ClipSelectedByPlane( plane, 0 ) && !doc.HollowSelection( 8 ) &&
			!doc.IsDirty() && !before.isEmpty() && mapBytes( after ) == before;
		doc.SelectObject( owner );
		const double newOrigin[3] = { 10, 20, 30 };
		ok = ok && !doc.SetSelectedEntityOrigin( newOrigin ) && !doc.CenterSelectedBrushEntityOrigins() &&
			!doc.IsDirty() && mapBytes( after ) == before;
		// Saved visgroup flags impose the same restriction, independently of Quick Hide.
		filter.quickHiddenIds.clear(); doc.SetViewFilter( filter );
		doc.Map().SolidById( b )->hasEditor = true;
		doc.Map().SolidById( b )->editorProperties.append( qMakePair( QStringLiteral( "visgroupshown" ), QStringLiteral( "0" ) ) );
		doc.SelectObject( owner );
		const QByteArray flagged = mapBytes( base );
		ok = ok && doc.SelectedIds().contains( owner ) && !doc.MoveSelection( delta ) && !doc.ScaleSelectedBoxes( factors ) &&
			!flagged.isEmpty() && mapBytes( after ) == flagged;
		// Explicitly showing the hidden group makes the entire entity editable.
		filter.showVisgroupHidden = true; doc.SetViewFilter( filter );
		ok = ok && doc.MoveSelection( delta ) && doc.Undo() && mapBytes( after ) == flagged;
		ctx.Step( "brush-entity-hidden-geometry-guard", ok,
			QStringLiteral( "hidden child rejects mixed geometry, movement and origin atomically; visible operation undoes" ) );
	}
	// Removing a leaf removes only its own membership, in one undo step.
	{
		CHammerDocument doc; QTemporaryDir dir;
		const double lo[3] = { 0, 0, 0 }, hi[3] = { 64, 64, 64 };
		const int brush = doc.CreateBrush( lo, hi );
		const int root = doc.CreateVisgroup( QStringLiteral( "Root" ) );
		const int leaf = doc.CreateVisgroup( QStringLiteral( "Leaf" ), root );
		doc.SelectObject( brush );
		bool ok = dir.isValid() && doc.SetSelectionVisgroupMembership( root, true ) && doc.SetSelectionVisgroupMembership( leaf, true );
		auto *solid = doc.Map().SolidById( brush );
		solid->editorProperties.append( qMakePair( QStringLiteral( "visgroupid" ), QStringLiteral( "999" ) ) );
		const auto original = solid->editorProperties;
		const QString before = dir.filePath( QStringLiteral( "before.vmf" ) ), after = dir.filePath( QStringLiteral( "after.vmf" ) );
		QString error; auto bytes = []( const QString &path ) { QFile f( path ); return f.open( QIODevice::ReadOnly ) ? f.readAll() : QByteArray(); };
		ok = ok && VmfIO::SaveMap( before, doc.Map(), &error ) && !doc.DeleteLeafVisgroup( root ) &&
			VmfIO::SaveMap( after, doc.Map(), &error ) && bytes( before ) == bytes( after );
		solid->editorProperties.append( qMakePair( QStringLiteral( "colorvisgroupid" ), QString::number( leaf ) ) );
		const auto colour = solid->editorProperties;
		ok = ok && !doc.DeleteLeafVisgroup( leaf ) && solid->editorProperties == colour;
		solid->editorProperties.removeLast(); solid->hiddenWrapper = true;
		ok = ok && !doc.DeleteLeafVisgroup( leaf ) && solid->hiddenWrapper && solid->editorProperties == original;
		solid->hiddenWrapper = false;
		// The VMF parser accepts multiple ID keys in one definition and uses
		// the last key for duplicate checking. An earlier reference to this
		// leaf still makes deletion ambiguous and must not be discarded.
		VisGroupLite ambiguous;
		ambiguous.properties = { qMakePair( QStringLiteral( "name" ), QStringLiteral( "Ambiguous" ) ),
			qMakePair( QStringLiteral( "visgroupid" ), QString::number( leaf ) ),
			qMakePair( QStringLiteral( "visgroupid" ), QStringLiteral( "1234" ) ) };
		doc.Map().visgroups.append( ambiguous );
		MapDocLite ambiguousReloaded;
		ok = ok && VmfIO::SaveMap( before, doc.Map(), &error ) && VmfIO::LoadMap( before, ambiguousReloaded, &error ) &&
			ambiguousReloaded.visgroups.size() == 2 && ambiguousReloaded.visgroups[1].properties == ambiguous.properties &&
			!doc.DeleteLeafVisgroup( leaf ) && VmfIO::SaveMap( after, doc.Map(), &error ) && bytes( before ) == bytes( after );
		doc.Map().visgroups.removeLast();
		ok = ok && doc.DeleteLeafVisgroup( leaf ) && doc.Map().visgroups[0].children.isEmpty() &&
			!doc.Map().SolidById( brush )->editorProperties.contains( qMakePair( QStringLiteral( "visgroupid" ), QString::number( leaf ) ) ) &&
			doc.Map().SolidById( brush )->editorProperties.contains( qMakePair( QStringLiteral( "visgroupid" ), QStringLiteral( "999" ) ) ) &&
			doc.Undo() && doc.Map().visgroups[0].children.size() == 1 && doc.Map().SolidById( brush )->editorProperties == original &&
			doc.Redo() && doc.Map().visgroups[0].children.isEmpty();
		ctx.Step( "visgroup-delete-leaf", ok, QStringLiteral( "nested leaf, membership, colour/hidden guards and undo: %1" ).arg( error ) );
	}
	// Mark members follows Hammer's direct group membership rather than nesting.
	// Selection is view state; it cannot dirty or modify the saved map.
	{
		CHammerDocument doc; QTemporaryDir dir;
		const double lo[3] = { 0, 0, 0 }, hi[3] = { 64, 64, 64 };
		const double lo2[3] = { 128, 0, 0 }, hi2[3] = { 192, 64, 64 };
		const double lo3[3] = { 256, 0, 0 }, hi3[3] = { 320, 64, 64 };
		const int a = doc.CreateBrush( lo, hi ), b = doc.CreateBrush( lo2, hi2 ), c = doc.CreateBrush( lo3, hi3 );
		EntityLite owner; owner.id = doc.Map().AllocId(); owner.classname = QStringLiteral( "func_detail" );
		owner.hasExplicitOrigin = false; const int ownerId = owner.id; doc.Map().entities.append( owner );
		doc.Map().SolidById( c )->ownerEntityId = ownerId;
		const int root = doc.CreateVisgroup( QStringLiteral( "Root" ) );
		const int child = doc.CreateVisgroup( QStringLiteral( "Child" ), root );
		doc.SelectObject( a ); bool ok = root > 0 && child > 0 && doc.SetSelectionVisgroupMembership( root, true );
		doc.SelectObject( b ); ok = ok && doc.SetSelectionVisgroupMembership( child, true );
		// Preserve a valid original VMF where a child solid has membership: its
		// visual owner must be selected, never the uneditable child solid ID.
		doc.Map().SolidById( c )->editorProperties.append( qMakePair( QStringLiteral( "visgroupid" ), QString::number( root ) ) );
		doc.Map().SolidById( c )->hasEditor = true;
		const QString path = dir.filePath( QStringLiteral( "mark-members.vmf" ) );
		ok = ok && dir.isValid() && doc.SaveToFile( path ) && !doc.IsDirty();
		doc.SelectObject( b );
		ok = ok && doc.SelectVisgroupMembers( root ) && doc.SelectedIds() == ( QSet<int>{ a, ownerId } ) && !doc.IsDirty();
		ok = ok && doc.SelectVisgroupMembers( child ) && doc.SelectedIds() == ( QSet<int>{ b } ) && !doc.IsDirty();
		ViewFilter filter = doc.GetViewFilter(); filter.quickHiddenIds.insert( a ); filter.quickHiddenIds.insert( c ); doc.SetViewFilter( filter );
		ok = ok && !doc.SelectVisgroupMembers( root ) && doc.SelectedIds() == ( QSet<int>{ b } ) && !doc.IsDirty();
		filter.quickHiddenIds.clear(); filter.visgroupForceHiddenIds.insert( ownerId ); doc.SetViewFilter( filter );
		ok = ok && doc.SelectVisgroupMembers( root ) && doc.SelectedIds() == ( QSet<int>{ a } );
		VisGroupLite ambiguous;
		ambiguous.properties = { qMakePair( QStringLiteral( "visgroupid" ), QString::number( root ) ),
			qMakePair( QStringLiteral( "visgroupid" ), QStringLiteral( "9999" ) ) };
		doc.Map().visgroups.append( ambiguous );
		ok = ok && !doc.SelectVisgroupMembers( root ) && doc.SelectedIds() == ( QSet<int>{ a } ) && !doc.IsDirty();
		CVisgroupDialog dialog( &doc );
		ok = ok && dialog.findChild<QPushButton *>( QStringLiteral( "visgroupSelectMembers" ) );
		ctx.Step( "visgroup-select-members", ok, QStringLiteral( "direct members, owner mapping, visibility, ambiguity and unchanged document" ) );
	}
	// Manual visgroups preserve unrelated/unknown assignments and undo as edits.
	{
		CHammerDocument doc;
		const double lo[3] = { 0, 0, 0 }, hi[3] = { 64, 64, 64 };
		const int brush = doc.CreateBrush( lo, hi );
		SolidLite *solid = doc.Map().SolidById( brush );
		solid->hasEditor = true;
		solid->editorProperties = { qMakePair( QStringLiteral( "visgroupid" ), QStringLiteral( "1" ) ),
			qMakePair( QStringLiteral( "color" ), QStringLiteral( "12 34 56" ) ) };
		const int root = doc.CreateVisgroup( QStringLiteral( "Walls" ) );
		const int child = doc.CreateVisgroup( QStringLiteral( "Upper" ), root );
		bool ok = root == 2 && child == 3 && doc.Map().visgroups.size() == 1 &&
			doc.Map().visgroups[0].children.size() == 1 && doc.RenameVisgroup( child, QStringLiteral( "Roof" ) );
		doc.SelectObject( brush );
		ok = ok && doc.SetSelectionVisgroupMembership( child, true );
		auto references = [&]() { return doc.Map().SolidById( brush )->editorProperties; };
		ok = ok && references().contains( qMakePair( QStringLiteral( "visgroupid" ), QStringLiteral( "1" ) ) ) &&
			references().contains( qMakePair( QStringLiteral( "visgroupid" ), QStringLiteral( "3" ) ) ) &&
			doc.Undo() && !references().contains( qMakePair( QStringLiteral( "visgroupid" ), QStringLiteral( "3" ) ) ) &&
			doc.Redo() && references().contains( qMakePair( QStringLiteral( "visgroupid" ), QStringLiteral( "3" ) ) );
		QTemporaryDir dir; const QString path = dir.filePath( QStringLiteral( "groups.vmf" ) ); MapDocLite loaded; QString error;
		ok = ok && dir.isValid() && VmfIO::SaveMap( path, doc.Map(), &error ) && VmfIO::LoadMap( path, loaded, &error ) &&
			loaded.visgroups.size() == 1 && loaded.visgroups[0].children.size() == 1 &&
			loaded.SolidById( brush ) && loaded.SolidById( brush )->editorProperties == references();
		ok = ok && doc.SetSelectionVisgroupMembership( child, false ) &&
			!references().contains( qMakePair( QStringLiteral( "visgroupid" ), QStringLiteral( "3" ) ) ) &&
			references().contains( qMakePair( QStringLiteral( "visgroupid" ), QStringLiteral( "1" ) ) ) && doc.Undo();
		// Same-map duplication keeps both unknown and known memberships; a
		// subsequent undo removes only the duplicate, not the source metadata.
		doc.SelectObject( brush );
		ok = ok && doc.CopySelection() && doc.PasteSelection() >= 0;
		const int copied = doc.SelectedId();
		const SolidLite *copy = doc.Map().SolidById( copied );
		ok = ok && copied != brush && copy && copy->editorProperties == references() && doc.Undo() &&
			!doc.Map().SolidById( copied ) && references().contains( qMakePair( QStringLiteral( "visgroupid" ), QStringLiteral( "3" ) ) );
		doc.SelectObject( brush );
		doc.Map().SolidById( brush )->editorProperties.append( qMakePair( QStringLiteral( "colorvisgroupid" ), QStringLiteral( "3" ) ) );
		const auto beforeColorRemoval = references();
		ok = ok && !doc.SetSelectionVisgroupMembership( child, false ) && references() == beforeColorRemoval;
		// An unrelated hidden wrapper survives group definition changes and VMF
		// roundtrip; its metadata cannot be edited via the selection operation.
		doc.Map().SolidById( brush )->hiddenWrapper = true;
		const int unrelated = doc.CreateVisgroup( QStringLiteral( "Other" ) );
		MapDocLite hiddenReloaded;
		ok = ok && unrelated > 0 && !doc.SetSelectionVisgroupMembership( root, true ) &&
			VmfIO::SaveMap( path, doc.Map(), &error ) && VmfIO::LoadMap( path, hiddenReloaded, &error ) &&
			hiddenReloaded.SolidById( brush ) && hiddenReloaded.SolidById( brush )->hiddenWrapper &&
			hiddenReloaded.SolidById( brush )->editorProperties == beforeColorRemoval &&
			doc.CreateVisgroup( QStringLiteral( "bad\"name" ) ) == -1;
		ctx.Step( "visgroup-management", ok, QStringLiteral( "nested create, rename, membership, unknown references, undo and VMF roundtrip: %1" ).arg( error ) );
	}
	// A group action recursively updates members; for overlapping groups the
	// last action wins, as in CMapDoc::VisGroups_ShowVisGroup. Session overrides
	// must never change the VMF or accidentally bypass Quick Hide.
	{
		CHammerDocument doc; QTemporaryDir dir;
		const double lo[3] = { 0, 0, 0 }, hi[3] = { 64, 64, 64 };
		const double lo2[3] = { 128, 0, 0 }, hi2[3] = { 192, 64, 64 };
		const double lo3[3] = { 256, 0, 0 }, hi3[3] = { 320, 64, 64 };
		const int a = doc.CreateBrush( lo, hi ), b = doc.CreateBrush( lo2, hi2 ), childSolid = doc.CreateBrush( lo3, hi3 );
		EntityLite ent; ent.id = doc.Map().AllocId(); ent.classname = QStringLiteral( "func_detail" ); ent.hasExplicitOrigin = false;
		const int owner = ent.id; doc.Map().entities.append( ent ); doc.Map().SolidById( childSolid )->ownerEntityId = owner;
		const int root = doc.CreateVisgroup( QStringLiteral( "Root" ) );
		const int child = doc.CreateVisgroup( QStringLiteral( "Child" ), root );
		const int other = doc.CreateVisgroup( QStringLiteral( "Other" ) );
		doc.SelectObject( a ); bool ok = doc.SetSelectionVisgroupMembership( root, true );
		doc.SelectObject( b ); ok = ok && doc.SetSelectionVisgroupMembership( child, true ) && doc.SetSelectionVisgroupMembership( other, true );
		doc.SelectObject( owner ); ok = ok && doc.SetSelectionVisgroupMembership( child, true );
		doc.Map().SolidById( childSolid )->hiddenWrapper = true;
		const QString before = dir.filePath( QStringLiteral( "before.vmf" ) ), after = dir.filePath( QStringLiteral( "after.vmf" ) );
		ok = ok && dir.isValid() && doc.SaveToFile( before );
		auto bytes = []( const QString &path ) { QFile file( path ); return file.open( QIODevice::ReadOnly ) ? file.readAll() : QByteArray(); };
		const QByteArray original = bytes( before );
		doc.SelectObject( b ); ok = ok && doc.IsSelected( b ) && doc.SetVisgroupVisible( root, false ) && !doc.IsSelected( b ) &&
			!doc.GetViewFilter().SolidVisible( *doc.Map().SolidById( a ) ) && !doc.GetViewFilter().SolidVisible( *doc.Map().SolidById( b ) ) &&
			!doc.GetViewFilter().EntityVisible( *doc.Map().EntityById( owner ) ) && !doc.IsDirty();
		doc.SelectObject( b ); ok = ok && !doc.IsSelected( b );
		ok = ok && doc.SetVisgroupVisible( other, true ) && doc.GetViewFilter().SolidVisible( *doc.Map().SolidById( b ) ) &&
			!doc.GetViewFilter().SolidVisible( *doc.Map().SolidById( a ) );
		ok = ok && doc.SetVisgroupVisible( child, true ) && doc.GetViewFilter().EntityVisible( *doc.Map().EntityById( owner ) ) &&
			doc.GetViewFilter().SolidVisible( *doc.Map().SolidById( childSolid ) ) && doc.Map().SolidById( childSolid )->hiddenWrapper;
		ViewFilter filter = doc.GetViewFilter(); filter.quickHiddenIds.insert( b ); doc.SetViewFilter( filter );
		ok = ok && doc.SetVisgroupVisible( other, true ) && !doc.GetViewFilter().SolidVisible( *doc.Map().SolidById( b ) );
		filter = doc.GetViewFilter(); filter.showVisgroupHidden = true; doc.SetViewFilter( filter );
		ok = ok && doc.GetViewFilter().SolidVisible( *doc.Map().SolidById( a ) ) && !doc.GetViewFilter().SolidVisible( *doc.Map().SolidById( b ) ) &&
			VmfIO::SaveMap( after, doc.Map() ) && !original.isEmpty() && bytes( after ) == original && !doc.IsDirty();
		CVisgroupDialog dialog( &doc );
		ok = ok && dialog.findChild<QPushButton *>( QStringLiteral( "visgroupShow" ) ) && dialog.findChild<QPushButton *>( QStringLiteral( "visgroupHide" ) );
		ok = ok && doc.LoadFromFile( before ) && doc.GetViewFilter().visgroupForceShownIds.isEmpty() &&
			doc.GetViewFilter().visgroupForceHiddenIds.isEmpty() && doc.GetViewFilter().quickHiddenIds.isEmpty() &&
			doc.Map().SolidById( childSolid )->hiddenWrapper;
		const int later = doc.CreateBrush( lo3, hi3 ); doc.SelectObject( later );
		ok = ok && later > 0 && doc.SetSelectionVisgroupMembership( other, true ) && doc.SetVisgroupVisible( other, false ) &&
			doc.GetViewFilter().visgroupForceHiddenIds.contains( later ) && doc.Undo() && doc.Undo() &&
			!doc.Map().SolidById( later ) && !doc.GetViewFilter().visgroupForceHiddenIds.contains( later );
		ctx.Step( "visgroup-session-visibility", ok, QStringLiteral( "nested groups, overlapping members, brush entity, Quick Hide and unchanged VMF" ) );
	}
	// Structural validation and VMF round trips for original-Hammer grids.
	{
		using namespace HammerDisplacementInspect;
		auto fixture = []( int power ) {
			Chunk root; root.name = QStringLiteral( "dispinfo" );
			root.keys = { { QStringLiteral( "power" ), QString::number( power ) },
				{ QStringLiteral( "startposition" ), QStringLiteral( "[0 0 0]" ) },
				{ QStringLiteral( "flags" ), QStringLiteral( "0" ) },
				{ QStringLiteral( "elevation" ), QStringLiteral( "0" ) },
				{ QStringLiteral( "subdiv" ), QStringLiteral( "0" ) } };
			const int n = ( 1 << power ) + 1;
			for ( const QString &name : { QStringLiteral( "normals" ), QStringLiteral( "distances" ),
				QStringLiteral( "offsets" ), QStringLiteral( "offset_normals" ),
				QStringLiteral( "alphas" ), QStringLiteral( "triangle_tags" ) } )
			{
				Chunk child; child.name = name;
				const bool triangle = name == QLatin1String( "triangle_tags" );
				const bool vector = name == QLatin1String( "normals" ) || name == QLatin1String( "offsets" ) || name == QLatin1String( "offset_normals" );
				const int rows = triangle ? n - 1 : n, columns = triangle ? 2 * ( n - 1 ) : vector ? 3 * n : n;
				QStringList values; for ( int i = 0; i < columns; ++i ) values.append( QStringLiteral( "0" ) );
				for ( int i = 0; i < rows; ++i ) child.keys.append( qMakePair( QStringLiteral( "row%1" ).arg( i ), values.join( QLatin1Char( ' ' ) ) ) );
				root.children.append( child );
			}
			Chunk allowed; allowed.name = QStringLiteral( "allowed_verts" );
			allowed.keys.append( qMakePair( QStringLiteral( "10" ), QStringLiteral( "-1 -1 -1 -1 -1 -1 -1 -1 -1 -1" ) ) );
			root.children.append( allowed ); return root;
		};
		QString error;
		bool ok = true;
		for ( int power = 2; power <= 4; ++power ) ok = InspectCanonical( fixture( power ), &error ) && ok;
		Chunk malformed = fixture( 2 ); malformed.children[0].keys.removeLast();
		ok = ok && !InspectCanonical( malformed, &error ) && error.contains( QStringLiteral( "row count" ) );
		malformed = fixture( 2 ); malformed.children[1].keys[0].second = QStringLiteral( "nan 0 0 0 0" );
		ok = ok && !InspectCanonical( malformed, &error );
		malformed = fixture( 2 ); malformed.children[5].keys[0].second = QStringLiteral( "0 0 0 0 0 0 0 65536" );
		ok = ok && !InspectCanonical( malformed, &error ) && error.contains( QStringLiteral( "triangle tag" ) );
		malformed = fixture( 2 ); malformed.children[0].keys[1].first = QStringLiteral( "row0" );
		ok = ok && !InspectCanonical( malformed, &error );
		malformed = fixture( 2 ); malformed.children.append( malformed.children[0] );
		ok = ok && !InspectCanonical( malformed, &error );
		malformed = fixture( 2 ); malformed.children[0].name = QStringLiteral( "unknown" );
		ok = ok && !InspectCanonical( malformed, &error );
		malformed = fixture( 2 ); malformed.keys[0].second = QStringLiteral( "5" );
		ok = ok && !InspectCanonical( malformed, &error );
		ctx.Step( "displacement-inspector", ok, QStringLiteral( "canonical powers 2-4, matrix shapes, finite values and malformed rejection" ) );
		for ( int power = 2; power <= 4; ++power )
		{
			QTemporaryDir dir;
			const QString path = dir.filePath( QStringLiteral( "terrain.vmf" ) );
			MapDocLite source, loaded;
			const double lower[3] = { 0, 0, 0 }, upper[3] = { 64, 64, 64 };
			const int solidId = CreateBoxSolid( source, lower, upper );
			SolidLite *solid = source.SolidById( solidId );
			Chunk disp = fixture( power );
			disp.keys.append( { QStringLiteral( "mintess" ), QStringLiteral( "0" ) } );
			disp.keys.append( { QStringLiteral( "smooth" ), QStringLiteral( "12.5" ) } );
			SideLite &side = solid->sides.first();
			side.displacement = disp;
			const double delta[3] = { 16, -32, 64 };
			TranslateSolid( *solid, delta );
			disp.keys[1].second = QStringLiteral( "[16 -32 64]" );
			bool roundtrip = dir.isValid() && VmfIO::SaveMap( path, source, &error ) && VmfIO::LoadMap( path, loaded, &error );
			const SolidLite *restored = loaded.SolidById( solidId );
			roundtrip = roundtrip && restored && restored->sides.first().displacement.has_value();
			if ( roundtrip )
			{
				const auto &actual = *restored->sides.first().displacement;
				roundtrip = actual.name == disp.name && actual.keys == disp.keys && actual.children.size() == disp.children.size();
				for ( int i = 0; roundtrip && i < disp.children.size(); ++i )
					roundtrip = actual.children[i].name == disp.children[i].name && actual.children[i].keys == disp.children[i].keys;
			}
			side.displacement->children.append( side.displacement->children.first() );
			MapDocLite preserved;
			roundtrip = roundtrip && !VmfIO::SaveMap( path, source, &error ) && !error.isEmpty() &&
				VmfIO::LoadMap( path, preserved, &error ) &&
				preserved.SolidById( solidId ) && preserved.SolidById( solidId )->sides.first().displacement.has_value();
			ctx.Step( "displacement-vmf-roundtrip", roundtrip, QStringLiteral( "power %1, translated start, metadata and atomic invalid-save guard" ).arg( power ) );
		}
	}
	// A separate VMF diagnostic must never load or modify the active document.
	{
		QTemporaryDir dir;
		const QString path = dir.filePath( QStringLiteral( "inspect.vmf" ) );
		auto write = [&]( const QByteArray &data ) {
			QFile file( path ); return file.open( QIODevice::WriteOnly | QIODevice::Truncate ) && file.write( data ) == data.size();
		};
		auto read = [&]() { QFile file( path ); return file.open( QIODevice::ReadOnly ) ? file.readAll() : QByteArray(); };
		QByteArray normals, distances;
		for ( int row = 0; row < 5; ++row )
		{
			normals += "\"row" + QByteArray::number( row ) + "\" \"0 0 1 0 0 1 0 0 1 0 0 1 0 0 1\"\n";
			distances += "\"row" + QByteArray::number( row ) + "\" \"0 0 0 0 0\"\n";
		}
		const QByteArray info = "dispinfo { \"power\" \"2\" \"startposition\" \"[0 0 0]\" normals { " + normals + " } distances { " + distances + " } }";
		const QByteArray map = "world { \"id\" \"1\" solid { \"id\" \"2\" side { \"id\" \"3\" " + info + " } } }";
		VmfIO::DisplacementReport report;
		QString error;
		CHammerDocument active;
		const double lo[3] = { 0, 0, 0 }, hi[3] = { 64, 64, 64 };
		const int activeId = active.CreateBrush( lo, hi ); active.SelectObject( activeId );
		const bool initialDirty = active.IsDirty(); const QString initialFile = active.GetFileName();
		bool ok = dir.isValid() && activeId > 0 && write( map ) && VmfIO::InspectDisplacements( path, report, &error ) &&
			report.count == 1 && report.nonCanonical == 0 && read() == map && active.IsSelected( activeId ) &&
			active.IsDirty() == initialDirty && active.GetFileName() == initialFile;
		ok = ok && write( "world { \"id\" \"1\" }" ) && VmfIO::InspectDisplacements( path, report, &error ) && report.count == 0;
		const QByteArray duplicate = "world { solid { \"id\" \"2\" side { \"id\" \"3\" " + info + info + " } } }";
		ok = ok && write( duplicate ) && VmfIO::InspectDisplacements( path, report, &error ) && report.count == 2 && report.nonCanonical == 2 && read() == duplicate;
		const QByteArray nonstandard = "world { solid { \"id\" \"2\" side { \"id\" \"3\" dispinfo { \"power\" \"5\" } } } }";
		ok = ok && write( nonstandard ) && VmfIO::InspectDisplacements( path, report, &error ) && report.count == 1 && report.nonCanonical == 1;
		ok = ok && write( "world { \"id\" \"unterminated" ) && !VmfIO::InspectDisplacements( path, report, &error ) && !error.isEmpty();
		ok = ok && write( "world { solid {" ) && !VmfIO::InspectDisplacements( path, report, &error ) && !error.isEmpty();
		// Duplicate terrain blocks must not replace the active map.
		ok = ok && write( duplicate ) && !active.LoadFromFile( path ) && active.IsSelected( activeId ) && read() == duplicate;
		ctx.Step( "displacement-vmf-diagnostic", ok, QStringLiteral( "read-only file diagnostic; canonical, absent, noncanonical, malformed and active document preservation" ) );
	}
	{
		CHammerDocument terrain;
		const double mins[3] = { 0, 0, -32 }, maxs[3] = { 128, 128, 0 };
		const int solidId = terrain.CreateBrush( mins, maxs );
		const auto solid = [&terrain, solidId] { return static_cast<const CHammerDocument &>( terrain ).Map().SolidById( solidId ); };
		const auto side = [&solid] { return solid() ? &solid()->sides.first() : nullptr; };
		bool ok = solidId > 0 && side();
		if ( ok )
		{
			const int faceId = side()->id;
			const QMap<int, QSet<int>> selection{ { solidId, { faceId } } };
			const QMap<int, QSet<int>> invalidSelection{ { solidId, { faceId, -1 } } };
			ok = !terrain.CreateDisplacements( invalidSelection, 2 ) && !side()->displacement &&
				!terrain.CreateDisplacements( selection, 5 ) && terrain.CreateDisplacements( selection, 2 ) &&
				side()->displacement && !terrain.CreateDisplacements( selection, 2 );
			QList<double> distances;
			ok = ok && HammerDisplacementEdit::ReadDistances( *side()->displacement, distances ) &&
				distances.size() == 25 && distances[12] == 0;
			if ( ok )
			{
				distances[12] = 32;
				QList<double> invalid = distances;
				invalid[13] = std::numeric_limits<double>::infinity();
				const double factors[3] = { 2, 2, 2 };
				ok = !terrain.SetDisplacementDistances( solidId, faceId, invalid ) &&
					terrain.SetDisplacementDistances( solidId, faceId, distances ) &&
					!terrain.ScaleSelectedBoxes( factors ) &&
					!terrain.RotateSelectedBoxes90( false, 2 ) && !terrain.FlipSelectedBoxes( 0 ) &&
					!terrain.ClipSelectedBoxes( 0, 64, 0 ) && !terrain.HollowSelection( 8 );
				QList<double> actual;
				ok = ok && HammerDisplacementEdit::ReadDistances( *side()->displacement, actual ) && actual == distances &&
					!terrain.RemoveDisplacements( invalidSelection ) && terrain.RemoveDisplacements( selection ) && !side()->displacement &&
					terrain.Undo() && side()->displacement &&
					HammerDisplacementEdit::ReadDistances( *side()->displacement, actual ) && actual == distances &&
					terrain.Undo() && side()->displacement &&
					HammerDisplacementEdit::ReadDistances( *side()->displacement, actual ) && actual[12] == 0 &&
					terrain.Redo() && side()->displacement &&
					HammerDisplacementEdit::ReadDistances( *side()->displacement, actual ) && actual == distances;
			}
		}
		ctx.Step( "displacement-edit", ok, QStringLiteral( "create, distance edits, destruction, atomic rejection and undo/redo" ) );
	}
	ctx.Step( "displacement-mesh", TestHammerDisplacementMesh(), QStringLiteral( "mesh construction, deformation and picking" ) );
	{
		CHammerDocument doc;
		CQtViewport3D view;
		view.setDocument( &doc );
		view.resize( 400, 300 );
		const double mins[3] = { 64, -32, 68 }, maxs[3] = { 96, 32, 132 };
		const double eye[3] = { 0, 0, 100 }, angles[3] = { 0, 0, 0 };
		const int id = doc.CreateBrush( mins, maxs );
		doc.SetTool( TOOL_TEXTURE );
		view.setCameraPositionAngles( eye, angles );
		const QPointF center( 200, 150 );
		const auto button = [&]( QEvent::Type type, Qt::MouseButton mouseButton ) {
			QMouseEvent event( type, center, mouseButton,
				type == QEvent::MouseButtonRelease ? Qt::NoButton : mouseButton, Qt::NoModifier );
			QCoreApplication::sendEvent( &view, &event );
		};
		const auto click = [&]( Qt::MouseButton mouseButton ) {
			button( QEvent::MouseButtonPress, mouseButton );
			button( QEvent::MouseButtonRelease, mouseButton );
		};
		click( Qt::LeftButton );
		const auto faces = view.selectedFaces();
		bool ok = faces.value( id ).size() == 1 && doc.CreateDisplacements( faces, 2 );
		if ( ok )
		{
			const int faceId = *faces.value( id ).cbegin();
			const auto distances = [&] {
				QList<double> result;
				const SolidLite *solid = doc.Map().SolidById( id );
				if ( solid ) for ( const SideLite &side : solid->sides )
					if ( side.id == faceId && side.displacement )
						HammerDisplacementEdit::ReadDistances( *side.displacement, result );
				return result;
			};
			click( Qt::LeftButton );
			view.setDisplacementPaint( true, 0, 12, 8, true );
			click( Qt::LeftButton );
			const auto raised = distances();
			ok = raised.size() == 25 && raised[12] == 8.0 && view.selectedFaces() == faces;
			click( Qt::RightButton );
			const auto lowered = distances();
			ok = ok && lowered.size() == 25 && lowered[12] == 0.0;
			button( QEvent::MouseButtonPress, Qt::LeftButton );
			QKeyEvent escape( QEvent::KeyPress, Qt::Key_Escape, Qt::NoModifier );
			QCoreApplication::sendEvent( &view, &escape );
			button( QEvent::MouseButtonRelease, Qt::LeftButton );
			ok = ok && distances() == lowered && doc.Undo() && distances() == raised &&
				doc.Redo() && distances() == lowered;
		}
		ctx.Step( "displacement-paint-gesture", ok, QStringLiteral( "selected 3D face, raise/lower, cancellation and undo/redo" ) );
	}
	ctx.Step( "displacement-sew", TestHammerDisplacementSew(), QStringLiteral( "reversed shared edge, mixed power, planar T-junction, selection guard and undo/redo" ) );
	{
		CMainWindow terrainWindow( nullptr, false );
		CHammerDocument *doc = terrainWindow.findChild<CHammerDocument *>();
		CQtViewport3D *view = terrainWindow.findChild<CQtViewport3D *>( QStringLiteral( "Viewport3D" ) );
		QAction *paint = terrainWindow.findChild<QAction *>( QStringLiteral( "paintDisplacementGeometry" ) );
		QAction *sew = terrainWindow.findChild<QAction *>( QStringLiteral( "sewDisplacements" ) );
		bool ok = doc && view && paint && sew && !paint->text().isEmpty() && !sew->text().isEmpty();
		if ( ok )
		{
			const double mins[3] = { 32, -16, 84 }, maxs[3] = { 64, 16, 116 };
			const double eye[3] = { 0, 0, 100 }, angles[3] = { 0, 0, 0 };
			const int brushId = doc->CreateBrush( mins, maxs );
			doc->SetTool( TOOL_TEXTURE );
			view->resize( 400, 300 );
			view->setCameraPositionAngles( eye, angles );
			const QPointF center( view->width() * 0.5, view->height() * 0.5 );
			const auto click = [&] {
				QMouseEvent down( QEvent::MouseButtonPress, center, Qt::LeftButton, Qt::LeftButton, Qt::NoModifier );
				QMouseEvent up( QEvent::MouseButtonRelease, center, Qt::LeftButton, Qt::NoButton, Qt::NoModifier );
				QCoreApplication::sendEvent( view, &down );
				QCoreApplication::sendEvent( view, &up );
			};
			click();
			const auto faces = view->selectedFaces();
			ok = faces.value( brushId ).size() == 1 && doc->CreateDisplacements( faces, 2 );
			if ( ok )
			{
				click();
				ok = view->selectedFaces().value( brushId ).size() == 1;
				if ( ok ) paint->trigger();
				QDialog *panel = terrainWindow.findChild<QDialog *>( QStringLiteral( "displacementGeometryDialog" ) );
				auto *effects = panel ? panel->findChild<QComboBox *>( QStringLiteral( "displacementPaintEffect" ) ) : nullptr;
				ok = panel && !panel->isModal() && !panel->isHidden() && effects && effects->count() == 3;
				if ( ok )
				{
					effects->setCurrentIndex( 2 );
					doc->SetTool( TOOL_SELECT );
					ok = panel->isHidden();
				}
			}
		}
		ctx.Step( "displacement-command-surface", ok,
			QStringLiteral( "3D face selection opens a modeless paint panel; tool changes close it" ) );
	}
	//--- Summary --------------------------------------------------------------------
	const bool bAllPassed = ( ctx.nFailed == 0 );
	ctx.Step( "summary", bAllPassed,
		QStringLiteral( "%1 passed, %2 failed -> %3" )
			.arg( ctx.nPassed )
			.arg( ctx.nFailed )
			.arg( bAllPassed ? QLatin1String( "OK" ) : QLatin1String( "FAILED" ) ) );

	return bAllPassed ? 0 : 1;
}
