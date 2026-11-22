//========= Copyright Valve Corporation, All rights reserved. ============//
//
// Purpose: Module-level active-document notification provider, the MFC-free
//			equivalent of CMapDoc::GetActiveMapDoc(). The host application
//			sets the active document's IDocNotify adapter here.
//
// $NoKeywords: $
//
//=============================================================================//

#include "hc_stdafx.h"
#include "IDocNotify.h"

// memdbgon must be the last include file in a .cpp file!!!
#include <tier0/memdbgon.h>

static IDocNotify *s_pActiveDocNotify = NULL;

IDocNotify *hc_GetActiveDocNotify(void)
{
	return s_pActiveDocNotify;
}


void hc_SetActiveDocNotify(IDocNotify *pNotify)
{
	s_pActiveDocNotify = pNotify;
}
