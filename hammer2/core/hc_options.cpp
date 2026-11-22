//========= Copyright Valve Corporation, All rights reserved. ============//
//
// Purpose: Storage + defaults for the MFC-free settings bridge. Default values
//			mirror the registry defaults COptions applies on a fresh install
//			(see hammer/options.cpp LoadColors/LoadOptions).
//
// $NoKeywords: $
//
//=============================================================================//

#include "hc_options.h"
#include "mapface.h"	// TextureAlignment_t

// memdbgon must be the last include file in a .cpp file!!!
#include <tier0/memdbgon.h>

hc_options_t g_hcOptions =
{
	RGB(220, 30, 220),	// clrEntity  ("Entity", 220, 30, 220)
	RGB(0, 0, 0),		// clrBrush   ("LineColor", 0, 0, 0)
	RGB(0, 0, 0),		// clrVertex  ("Vertex", 0, 0, 0)
	RGB(220, 0, 0),		// clrSelection ("Selection", 220, 0, 0)
	RGB(0, 0, 0),		// clrToolHandle ("HandleColor", 0, 0, 0)
	RGB(0, 0, 0),		// clrToolBlock  ("BoxColor", 0, 0, 0)
	RGB(255, 255, 0),	// clrToolDrag   ("ToolDrag", 255, 255, 0)

	TRUE,				// bUsegroupcolors ("Usegroupcolors", TRUE)
	TRUE,				// bDrawVertices ("Draw Vertices", TRUE)
	FALSE,				// bSelectbyhandles ("SelectByHandles", FALSE)

	TEXTURE_ALIGN_WORLD,	// eTextureAlignment ("Texture Alignment", TEXTURE_ALIGN_WORLD)

	1.0f,				// flDefaultTextureScale
};


void hc_SetOptions(const hc_options_t &options)
{
	g_hcOptions = options;
}
