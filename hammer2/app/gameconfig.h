//========= Hammer Qt6 (hammer2/app) ==========================================
//
// Purpose: GameConfig.txt handling. The file lives next to the hammer_qt
//          executable and is a simple KeyValues-style text file:
//
//          "GameConfigs"
//          {
//              "Garry's Mod"
//              {
//                  "GameData"  "fgd/garrysmod.fgd"
//              }
//          }
//
//          When the file does not exist a default one pointing at
//          ./fgd/garrysmod.fgd (relative to the executable directory) is
//          generated automatically.
//
//=============================================================================

#pragma once

#include <QString>
#include <QStringList>

//-----------------------------------------------------------------------------
// Purpose: Result of a GameConfig.txt load.
//-----------------------------------------------------------------------------
struct GameConfigInfo
{
	QString gameName;       // name of the first game entry ("Garry's Mod")
	QString fgdPath;        // absolute path to the game data (FGD) file
	QStringList fgdPaths;   // all configured FGD files, in order
	bool bCreatedDefault;   // true when GameConfig.txt was missing and was written
};

// Purpose: Loads (or creates) GameConfig.txt in exeDir and resolves the FGD
//          path to an absolute path. Returns true when a configuration could
//          be produced (even a freshly generated default one).
bool LoadGameConfig( const QString &exeDir, GameConfigInfo &outInfo, QString *errorOut = nullptr );
