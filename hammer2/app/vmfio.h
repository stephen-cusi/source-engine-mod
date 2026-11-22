//========= Hammer Qt6 (hammer2/app) ==========================================
//
// Purpose: VMF save/load for the MapDocLite model. The written format targets
//          the subset of VMF that vbsp consumes (utils/vbsp/map.cpp):
//            - versioninfo / world / solid / side / entity chunks
//            - side keys: "plane" with three points, "material", "uaxis" and
//              "vaxis" in the "[x y z offset] scale" form, "rotation",
//              "lightmapscale" and "smoothing_groups"
//
//=============================================================================

#pragma once

#include <QString>
#include <QStringList>

class MapDocLite;

namespace VmfIO
{
	// Diagnostic-only inspection; never loads a document or writes the file.
	// A false return means I/O, file-size, or VMF syntax failure. A successful
	// result can still contain non-canonical displacement sections.
	struct DisplacementReport
	{
		int count = 0;
		int nonCanonical = 0;
		QStringList details;
	};
	bool InspectDisplacements( const QString &path, DisplacementReport &report, QString *errorOut = nullptr );
	// Purpose: Writes the document as a Valve Map File. The document is passed
	//          non-const because face ids are allocated lazily at save time.
	// Output  : true on success; on failure a human readable reason is set.
	bool SaveMap( const QString &path, MapDocLite &map, QString *errorOut = nullptr );

	// Purpose: Reads a VMF file back into the document. Unsupported data is
	//          rejected rather than discarded; camera and cordon data are preserved.
	bool LoadMap( const QString &path, MapDocLite &map, QString *errorOut = nullptr );
} // namespace VmfIO
