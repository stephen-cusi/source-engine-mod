#pragma once

// Keep the document's double precision when the original float model cannot
// represent a selection or its center without a change.
bool TryHammerCoreBoundsCenter( const double mins[3], const double maxs[3], double center[3] );
bool TryHammerCoreRegionMatch( const double mins[2], const double maxs[2], const double regionMins[2], const double regionMaxs[2], bool insideOnly, bool &matches );
bool RunHammerCoreBoundsSmokeTest();
