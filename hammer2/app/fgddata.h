//========= Hammer Qt6 (hammer2/app) ==========================================
//
// Purpose: CFgdData - a thin Qt-friendly wrapper around the fgdlib GameData
//          loader. Provides the list of placeable point classes, the FGD
//          default key/values for a class and per-class bounding boxes used
//          for entity rendering and placement.
//
//=============================================================================

#pragma once

#include <QMap>
#include <QString>
#include <QStringList>
#include <QList>
#include <QPair>

class GameData;

class CFgdData
{
public:
	enum class ResourceType { None, StudioModel, Sound };
	CFgdData();
	~CFgdData();

	CFgdData( const CFgdData & ) = delete;
	CFgdData &operator=( const CFgdData & ) = delete;

	// Purpose: Loads an FGD file (follows @include recursively, like fgdlib).
	//          Returns false and fills errorOut when the file cannot be loaded.
	bool Load( const QString &fgdPath, QString *errorOut = nullptr );
	// Load in order, with later definitions replacing earlier ones as in Hammer.
	// A failure keeps the previously loaded game data intact.
	bool LoadFiles( const QStringList &fgdPaths, QString *errorOut = nullptr );

	// Purpose: Releases all parsed class data.
	void Clear();

	bool IsValid() const { return ( m_pGameData != nullptr ); }

	// Purpose: Sorted list of all point classes available for placement.
	QStringList PointClassNames() const;
	QStringList SolidClassNames() const;

	// Purpose: True when the given class name exists in the FGD.
	bool HasClass( const QString &className ) const;

	// Purpose: FGD default key/values for a class (from the class and its
	//          base classes). Empty values are skipped. Returns an empty map
	//          for unknown classes.
	QMap<QString, QString> DefaultKeyValues( const QString &className ) const;
	bool PropertyDefault( const QString &className, const QString &key, QString &value ) const;
	ResourceType PropertyResourceType( const QString &className, const QString &key ) const;
	// Internal key -> FGD display name, including keys without defaults.
	QMap<QString, QString> ClassPropertyNames( const QString &className ) const;
	QList<QPair<QString, QString> > PropertyChoices( const QString &className, const QString &key ) const;
	QList<QPair<QString, QString> > PropertyFlags( const QString &className, const QString &key ) const;
	QStringList ClassInputs( const QString &className ) const;
	QStringList ClassOutputs( const QString &className ) const;

	// Purpose: Class bounding box in world units (used for the 2D/3D entity
	//          marker size). Returns false for unknown classes; defaults to
	//          an 16x16x16 box around the origin.
	bool ClassBounds( const QString &className, double outMins[3], double outMaxs[3] ) const;

	// Purpose: Suggested placement height for a class (z center of its
	//          bounding box, or 0 when unknown).
	double DefaultPlaceZ( const QString &className ) const;

private:
	GameData *m_pGameData;
};
