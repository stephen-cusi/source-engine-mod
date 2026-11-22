#pragma once

#include "document.h"

// State edited by the Grid Settings dialog. Applying goes through the same
// document setters that View > Grid Size, Show Grid and Snap to Grid use, so
// the menu check marks follow via CHammerDocument::stateChanged.
struct GridSettingsState
{
	int size = 16;
	bool visible = true;
	bool snap = true;
};

// The document model and View > Grid Size only use these power-of-two sizes.
inline bool IsValidGridSize( int size )
{
	return size >= 1 && size <= 512 && ( size & ( size - 1 ) ) == 0;
}

inline GridSettingsState CurrentGridSettings( const CHammerDocument &document )
{
	GridSettingsState state;
	state.size = document.GridSize();
	state.visible = document.GridVisible();
	state.snap = document.SnapEnabled();
	return state;
}

// Returns false and changes nothing when the size is not supported.
inline bool ApplyGridSettings( CHammerDocument &document, const GridSettingsState &state )
{
	if ( !IsValidGridSize( state.size ) ) return false;
	document.SetGridSize( state.size );
	document.SetGridVisible( state.visible );
	document.SetSnapEnabled( state.snap );
	return true;
}
