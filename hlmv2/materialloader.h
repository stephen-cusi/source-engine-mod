//========= Half-Life Model Viewer 2 (Qt6 port of utils/hlmv) ================
//
// Purpose: Cross-platform material lookup for the standalone viewer.
//
//          The original HLMV resolves studio materials through the engine's
//          materialsystem (IMaterial / FindMaterial, see utils/hlmv/matsyswin.cpp).
//          That stack requires the D3D9 shader API, a filesystem bootstrap and
//          the whole engine to be loaded, which is Windows-only and is the very
//          reason hlmv2 uses its own GL renderer.
//
//          This module replaces just the *lookup* part with plain Qt I/O so the
//          viewer runs on win32/linux/darwin:
//
//            - loose files: <gameroot>/materials/... (Qt file I/O)
//            - VPK v1/v2 archives: <searchroot>/*_dir.vpk  (self-contained
//              tree parser; entries may live in sibling _NNN.vpk payload files)
//            - VMT: tiny key/value reader for "$basetexture"
//            - VTF 7.x: mip-0 decoder for DXT1/3/5 and the common uncompressed
//              image formats, producing a QImage (no D3D/GPU dependency)
//
//          Search roots are derived from the model path: the game directory
//          (the parent of "models/"), its parent, and any sibling directory
//          that looks like a game directory (holds a gameinfo.txt or VPKs).
//          That covers standalone mods that mount HL2's hl2_textures_*.vpk.
//
//          Nothing here touches the filesystem outside of reading. No engine
//          module, no platform headers, no registry, no D3D.
//
//=============================================================================

#ifndef HLMV2_MATERIALLOADER_H
#define HLMV2_MATERIALLOADER_H

#include <QByteArray>
#include <QHash>
#include <QImage>
#include <QString>
#include <QStringList>
#include <QVector3D>

//-----------------------------------------------------------------------------
// Purpose: Result of inspecting a VMT for engine-driven colouring.
//-----------------------------------------------------------------------------
struct VmtColorInfo_t
{
	bool bTintable = false;
	QVector3D defaultTint{ 1.0f, 1.0f, 1.0f };
};

// Inspects VMT text for GMod's PlayerColor proxy / "$color" / "$color2".
VmtColorInfo_t InspectVmtColor( const QByteArray &vmt );

//-----------------------------------------------------------------------------
// Purpose: Resolves studio texture names to decoded QImages.
//-----------------------------------------------------------------------------
class CMaterialLoader
{
public:
	CMaterialLoader();

	// Builds the search roots + VPK index for a model file. Call once per model.
	void SetModelPath( const QString &mdlPath );

	// Number of indexed VPK entries (for diagnostics / self-test).
	int GetIndexedFileCount() const { return m_Index.count(); }
	const QStringList &GetSearchRoots() const { return m_lstRoots; }

	// Resolves one mdl texture name (e.g. "apc01blue") through its cdtexture
	// search paths (e.g. "models\\combine_apc\\") into a decoded QImage.
	// Returns false when no VMT/VTF was found or the format is unsupported;
	// sStatus always contains a human-readable resolution trail.
	//
	// pTintable reports whether the material's colour is engine driven (GMod's
	// PlayerColor proxy / "$color" / "$color2"), which is how HL2/GMod colour
	// player arms and weapons. pDefaultTint receives the colour the engine
	// would use before the player picks one.
	bool LoadTextureImage( const QString &texName, const QStringList &cdPaths,
		QImage &outImage, QString &sStatus, bool *pTintable = nullptr,
		QVector3D *pDefaultTint = nullptr );

	// Reads a game-relative path (e.g. "materials/models/...vmt") from loose
	// files or VPK archives. Public so the self-test can exercise the archive
	// reader directly.
	bool ReadGameFile( const QString &relPath, QByteArray &outData );

private:
	struct VpkEntry_t
	{
		QString dirVpk;      // absolute path of the *_dir.vpk
		int nArchive = 0;    // 0x7fff = data embedded in the dir vpk
		quint32 nOffset = 0;
		quint32 nLength = 0;
		QByteArray preload;  // bytes already stored in the tree
		quint32 nDataBase = 0; // file offset of the dir vpk's data section
	};

	void IndexVpk( const QString &dirVpk );
	bool ReadVpkEntry( const VpkEntry_t &entry, QByteArray &outData );
	static QString NormalizePath( const QString &path );

	QStringList m_lstRoots;
	QHash<QString, VpkEntry_t> m_Index;   // lowercase relative path -> entry
};

//-----------------------------------------------------------------------------
// Purpose: Decodes a VTF 7.x blob's largest mip into a QImage.
//          Public for the self-test; returns false with sError on unsupported
//          formats. Pure byte manipulation, no GL context required.
//-----------------------------------------------------------------------------
bool DecodeVtfToImage( const QByteArray &vtf, QImage &outImage, QString &sError );

#endif // HLMV2_MATERIALLOADER_H
