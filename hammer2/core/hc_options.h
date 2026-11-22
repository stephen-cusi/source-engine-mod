//========= Copyright Valve Corporation, All rights reserved. ============//
//
// Purpose: MFC-free settings bridge for the hammer2/core data model. The
//			model code reads a handful of editor settings (colors, view2d
//			checkboxes, texture alignment, default texture scale) that used to
//			come from the MFC COptions object in hammer/Options.h. The host
//			application pushes current values into g_hcOptions; until then the
//			defaults below match the values COptions applies on a fresh
//			installation.
//
// $NoKeywords: $
//
//=============================================================================//

#ifndef HC_OPTIONS_H
#define HC_OPTIONS_H

#include "hc_stdafx.h"	// COLORREF

struct hc_options_t
{
	// COptionsColors subset used by the data model.
	COLORREF clrEntity;
	COLORREF clrBrush;
	COLORREF clrVertex;
	COLORREF clrSelection;
	COLORREF clrToolHandle;
	COLORREF clrToolBlock;
	COLORREF clrToolDrag;

	// COptionsView2D subset used by the data model.
	BOOL bUsegroupcolors;
	BOOL bDrawVertices;
	BOOL bSelectbyhandles;

	// COptions::GetTextureAlignment. Holds a TextureAlignment_t value
	// (TEXTURE_ALIGN_WORLD / TEXTURE_ALIGN_FACE, see mapface.h).
	int eTextureAlignment;

	// CGameConfig::GetDefaultTextureScale.
	float flDefaultTextureScale;
};

extern hc_options_t g_hcOptions;

// Host applications call this to install their current settings.
void hc_SetOptions(const hc_options_t &options);

#endif // HC_OPTIONS_H
