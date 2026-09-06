//========= Copyright Valve Corporation, All rights reserved. ============//
//
// Purpose: Client side view model implementation. Responsible for drawing
//			the view model.
//
// $NoKeywords: $
//=============================================================================//

#ifndef C_BASEVIEWMODEL_H
#define C_BASEVIEWMODEL_H
#ifdef _WIN32
#pragma once
#endif

#include "c_baseanimating.h"
#include "utlvector.h"
#include "baseviewmodel_shared.h"

#if defined( CLIENT_DLL )

// Forward declaration
class C_ViewmodelAttachment;

// Add hands attachment member to C_BaseViewModel
// This is done via a client-side extension since the base class is shared

// Add this to your c_baseviewmodel.cpp:
// CHandle<C_ViewmodelAttachment> m_hHandsAttachment;
// void UpdateHandsAttachment( void );
// void UpdateOnRemove( void );

#endif // CLIENT_DLL

#endif // C_BASEVIEWMODEL_H
