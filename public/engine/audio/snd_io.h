//========= Copyright Valve Corporation, All rights reserved. ============//
//
// Purpose: 
//
// $NoKeywords: $
//=============================================================================//

#ifndef SND_IO_H
#define SND_IO_H
#pragma once

class IFileReadBinary;

extern IFileReadBinary *g_pSndIO;
float Audio_GetWaveDuration( const char *pName );
int Audio_ADPCMSampleCount( int nChannels, int nSamplesPerBlock, int nBytes );
bool Audio_IsMP3( const char *pName );
float GetMP3Duration_Helper( const char *pName );

#endif // SND_IO_H
