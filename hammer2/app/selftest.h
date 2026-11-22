//========= Hammer Qt6 (hammer2/app) ==========================================
//
// Purpose: --selftest mode: a fully automated headless verification of the
//          Hammer Qt6 skeleton. Loads the FGD via GameConfig.txt (falling
//          back to a built-in minimal FGD), builds a small map (worldspawn,
//          two brushes, three entities), saves it as VMF, reloads it and
//          asserts consistency. Each step prints a
//          "SELFTEST: <step> PASS/FAIL <detail>" line; the process exit code
//          is 0 when everything passed and 1 otherwise.
//
//=============================================================================

#pragma once

#include <QString>

// Purpose: Runs the full self-test and writes the VMF to vmfPath.
//          Returns the process exit code (0 = success).
int RunHammerSelfTest( const QString &vmfPath );
