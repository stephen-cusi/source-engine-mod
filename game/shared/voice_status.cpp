//========= Copyright Valve Corporation, All rights reserved. ============//
//
// Purpose: 
//
// $NoKeywords: $
//=============================================================================//
#include "cbase.h"
#include "basetypes.h"
#include "hud.h"
#include <string.h>
#include <stdio.h>
#include "voice_status.h"
#include "r_efx.h"
#include <vgui_controls/TextImage.h>
#include <vgui/MouseCode.h>
#include "cdll_client_int.h"
#include "hud_macros.h"
#include "c_playerresource.h"
#include "cliententitylist.h"
#include "c_baseplayer.h"
#include "materialsystem/imesh.h"
#include "view.h"
#include "convar.h"
#include <vgui_controls/Controls.h>
#include <vgui/IScheme.h>
#include <vgui/ISurface.h>
#include "vgui_bitmapimage.h"
#include "materialsystem/imaterial.h"
#include "tier0/dbg.h"
#include "cdll_int.h"
#include <vgui/IPanel.h>
// HL2SB: PlayerStartVoice / PlayerEndVoice gamemode hooks (client only file).
#include "luamanager.h"
#include "lbaseentity_shared.h"

// memdbgon must be the last include file in a .cpp file!!!
#include "tier0/memdbgon.h"

using namespace vgui;


extern int cam_thirdperson;


#define VOICE_MODEL_INTERVAL		0.3
#define SQUELCHOSCILLATE_PER_SECOND	2.0f

ConVar voice_modenable( "voice_modenable", "1", FCVAR_ARCHIVE | FCVAR_CLIENTCMD_CAN_EXECUTE, "Enable/disable voice in this mod." );
ConVar voice_clientdebug( "voice_clientdebug", "0" );

// ---------------------------------------------------------------------- //
// The voice manager for the client.
// ---------------------------------------------------------------------- //
static CVoiceStatus *g_VoiceStatus = NULL;

CVoiceStatus* GetClientVoiceMgr()
{
	if ( !g_VoiceStatus )
	{
		ClientVoiceMgr_Init();
	}

	return g_VoiceStatus;
}

void ClientVoiceMgr_Init()
{
	if ( g_VoiceStatus )
		return;

	g_VoiceStatus = new CVoiceStatus();
}

void ClientVoiceMgr_Shutdown()
{
	delete g_VoiceStatus;
	g_VoiceStatus = NULL;
}

// ---------------------------------------------------------------------- //
// CVoiceStatus.
// ---------------------------------------------------------------------- //

static CVoiceStatus *g_pInternalVoiceStatus = NULL;

void __MsgFunc_VoiceMask(bf_read &msg)
{
	if(g_pInternalVoiceStatus)
		g_pInternalVoiceStatus->HandleVoiceMaskMsg(msg);
}

void __MsgFunc_RequestState(bf_read &msg)
{
	if(g_pInternalVoiceStatus)
		g_pInternalVoiceStatus->HandleReqStateMsg(msg);
}


// ---------------------------------------------------------------------- //
// CVoiceStatus.
// ---------------------------------------------------------------------- //

CVoiceStatus::CVoiceStatus()
{
	m_nControlSize = 0;
	m_bBanMgrInitialized = false;
	m_LastUpdateServerState = 0;

	m_bTalking = m_bServerAcked = false;

#ifdef VOICE_VOX_ENABLE
	m_bAboveThresholdTimer.Invalidate();
#endif // VOICE_VOX_ENABLE

	m_bServerModEnable = -1;

	m_pHeadLabelMaterial = NULL;

	m_bHeadLabelsDisabled = false;

	// HL2SB: loudness table starts empty (invalid) so voice HUD readers fall
	// back to the speaking endpoints until the engine delivers samples.
	for ( int i = 0; i < VOICE_MAX_PLAYERS; ++i )
	{
		m_flVoiceLoudness[i] = 0.0f;
		m_bVoiceLoudnessValid[i] = false;
	}
}


CVoiceStatus::~CVoiceStatus()
{
	if ( m_pHeadLabelMaterial )
	{
		m_pHeadLabelMaterial->DecrementReferenceCount();
	}

	g_pInternalVoiceStatus = NULL;			

	const char *pGameDir = engine->GetGameDirectory();
	if( pGameDir )
	{
		if(m_bBanMgrInitialized)
		{
			m_BanMgr.SaveState( pGameDir );
		}
	}
}

int CVoiceStatus::Init(
	IVoiceStatusHelper *pHelper,
	VPANEL pParentPanel)
{
	const char *pGameDir = engine->GetGameDirectory();
	if( pGameDir )
	{
		m_BanMgr.Init( pGameDir );
		m_bBanMgrInitialized = true;
	}

	Assert(!g_pInternalVoiceStatus);
	g_pInternalVoiceStatus = this;


	m_pHeadLabelMaterial = materials->FindMaterial( "voice/icntlk_pl", TEXTURE_GROUP_VGUI );
	m_pHeadLabelMaterial->IncrementReferenceCount();

	m_bInSquelchMode = false;

	m_pHelper = pHelper;
	m_pParentPanel = pParentPanel;

	HOOK_MESSAGE(VoiceMask);
	HOOK_MESSAGE(RequestState);

	return 1;
}


BitmapImage* vgui_LoadMaterial( vgui::VPANEL pParent, const char *pFilename )
{
	return new BitmapImage( pParent, pFilename );
}


void CVoiceStatus::VidInit()
{
}


void CVoiceStatus::Frame(double frametime)
{
	// check server banned players once per second
	if (gpGlobals->curtime - m_LastUpdateServerState > 1)
	{
		UpdateServerState(false);
	}
}


float g_flHeadOffset = 35;
float g_flHeadIconSize = 8;


void CVoiceStatus::SetHeadLabelOffset( float offset )
{
	g_flHeadOffset = offset;
}

float CVoiceStatus::GetHeadLabelOffset( void ) const
{
	return g_flHeadOffset;
}


void CVoiceStatus::DrawHeadLabels()
{
	if ( m_bHeadLabelsDisabled )
		return;

	if ( GameRules() && ( GameRules()->ShouldDrawHeadLabels() == false ) )
		return;

	if( !m_pHeadLabelMaterial )
		return;

	CMatRenderContextPtr pRenderContext( materials );

	for(int i=0; i < VOICE_MAX_PLAYERS; i++)
	{
		if ( !m_VoicePlayers[i] )
			continue;
		
		IClientNetworkable *pClient = cl_entitylist->GetClientEntity( i+1 );
		
		// Don't show an icon if the player is not in our PVS.
		if ( !pClient || pClient->IsDormant() )
			continue;

		C_BasePlayer *pPlayer = dynamic_cast<C_BasePlayer*>(pClient);
		if( !pPlayer )
			continue;

		// Don't show an icon for dead or spectating players (ie: invisible entities).
		if( pPlayer->IsPlayerDead() )
			continue;

		// Place it 20 units above his head.
		Vector vOrigin = pPlayer->WorldSpaceCenter();
		vOrigin.z += g_flHeadOffset;

		
		// Align it so it never points up or down.
		Vector vUp( 0, 0, 1 );
		Vector vRight = CurrentViewRight();
		if ( fabs( vRight.z ) > 0.95 )	// don't draw it edge-on
			continue;

		vRight.z = 0;
		VectorNormalize( vRight );


		float flSize = g_flHeadIconSize;

		pRenderContext->Bind( pPlayer->GetHeadLabelMaterial() );
		IMesh *pMesh = pRenderContext->GetDynamicMesh();
		CMeshBuilder meshBuilder;
		meshBuilder.Begin( pMesh, MATERIAL_QUADS, 1 );

		meshBuilder.Color3f( 1.0, 1.0, 1.0 );
		meshBuilder.TexCoord2f( 0,0,0 );
		meshBuilder.Position3fv( (vOrigin + (vRight * -flSize) + (vUp * flSize)).Base() );
		meshBuilder.AdvanceVertex();

		meshBuilder.Color3f( 1.0, 1.0, 1.0 );
		meshBuilder.TexCoord2f( 0,1,0 );
		meshBuilder.Position3fv( (vOrigin + (vRight * flSize) + (vUp * flSize)).Base() );
		meshBuilder.AdvanceVertex();

		meshBuilder.Color3f( 1.0, 1.0, 1.0 );
		meshBuilder.TexCoord2f( 0,1,1 );
		meshBuilder.Position3fv( (vOrigin + (vRight * flSize) + (vUp * -flSize)).Base() );
		meshBuilder.AdvanceVertex();

		meshBuilder.Color3f( 1.0, 1.0, 1.0 );
		meshBuilder.TexCoord2f( 0,0,1 );
		meshBuilder.Position3fv( (vOrigin + (vRight * -flSize) + (vUp * -flSize)).Base() );
		meshBuilder.AdvanceVertex();
		meshBuilder.End();
		pMesh->Draw();
	}
}


// HL2SB: fires the GMod voice hooks ( PlayerStartVoice / PlayerEndVoice )
// with ( player, playerIndex ), the reference's argument order.  A NULL
// player pushes the NULL sentinel so a disconnected / not-yet-known speaker
// reaches Lua as an invalid entity with a valid index -- the shape the base
// voice HUD's "Unknown Player" path reads.
static void HL2SB_FireVoiceHook( bool bStarted, C_BasePlayer *pPlayer, int entindex )
{
	if ( L == NULL )
		return;

	BEGIN_LUA_CALL_HOOK( bStarted ? "PlayerStartVoice" : "PlayerEndVoice" );
	C_BaseEntity::PushLuaInstanceSafe( L, pPlayer );
	lua_pushinteger( L, entindex );
	END_LUA_CALL_HOOK( 2, 0 );
}

void CVoiceStatus::UpdateSpeakerStatus(int entindex, bool bTalking)
{
	if(!m_pParentPanel)
		return;

	if( voice_clientdebug.GetInt() )
	{
		Msg( "CVoiceStatus::UpdateSpeakerStatus: ent %d talking = %d\n", entindex, bTalking );
	}

	// Is it the local player talking?
	if( entindex == -1 )
	{
		bool bWasTalking = m_bTalking;
		m_bTalking = !!bTalking;
		if( bTalking )
		{
			// Enable voice for them automatically if they try to talk.
			engine->ClientCmd( "voice_modenable 1" );
		}

		// HL2SB: reference behavior: the client voice status update fires the
		// PlayerStartVoice / PlayerEndVoice gamemode hooks exactly once on
		// every speaking-state flip, local player included, with
		// ( player, playerIndex ).  The engine's voice code only calls this
		// function when the local recorder starts or stops, so pairing the
		// hook with the state compare keeps that once-per-flip contract.
		if( bWasTalking != m_bTalking )
		{
			C_BasePlayer *pLocal = C_BasePlayer::GetLocalPlayer();
			if( pLocal )
			{
				if ( m_bTalking )
					ClearLoudnessValid( pLocal->entindex() );
				HL2SB_FireVoiceHook( m_bTalking, pLocal, pLocal->entindex() );
			}
		}
	}
	else if( entindex == -2 )
	{
		m_bServerAcked = !!bTalking;
	}
#ifdef VOICE_VOX_ENABLE
	else if( entindex == -3 )
	{
		if ( bTalking )
		{
			const float AboveThresholdMinDuration = 0.5f;
			m_bAboveThresholdTimer.Start( AboveThresholdMinDuration );
		}
	}
#endif // VOICE_VOX_ENABLE
	else if(entindex > 0 && entindex <= VOICE_MAX_PLAYERS)
	{
		int iClient = entindex - 1;
		if(iClient < 0)
			return;

		bool bWasTalking = m_VoicePlayers[iClient] != 0;
		if(bTalking)
		{
			m_VoicePlayers[iClient] = true;
			m_VoiceEnabledPlayers[iClient] = true;
		}
		else
		{
			m_VoicePlayers[iClient] = false;
		}

		// HL2SB: same once-per-flip hook for remote speakers; the state bit
		// is updated first so the hook body reads the new answer, matching
		// the reference (its manager bit is already current when the hook
		// fires).  A speaker the client entity list does not know pushes the
		// NULL sentinel, which the base voice HUD handles as "unknown player".
		if( bWasTalking != ( bTalking != 0 ) )
		{
			if ( bTalking )
				ClearLoudnessValid( entindex );
			C_BasePlayer *pPlayer = dynamic_cast<C_BasePlayer*>( cl_entitylist->GetClientEntity( entindex ) );
			HL2SB_FireVoiceHook( bTalking != 0, pPlayer, entindex );
		}
	}
}


void CVoiceStatus::UpdateServerState(bool bForce)
{
	// Can't do anything when we're not in a level.
	if( !g_bLevelInitialized )
	{
		if( voice_clientdebug.GetInt() )
		{
			Msg( "CVoiceStatus::UpdateServerState: g_bLevelInitialized\n" );
		}

		return;
	}
	
	int bCVarModEnable = !!voice_modenable.GetInt();
	if(bForce || m_bServerModEnable != bCVarModEnable)
	{
		m_bServerModEnable = bCVarModEnable;

		char str[256];
		Q_snprintf(str, sizeof(str), "VModEnable %d", m_bServerModEnable);
		engine->ServerCmd(str);

		if( voice_clientdebug.GetInt() )
		{
			Msg( "CVoiceStatus::UpdateServerState: Sending '%s'\n", str );
		}
	}

	char str[2048];
	Q_strncpy(str,"vban",sizeof(str));
	bool bChange = false;

	for(unsigned long dw=0; dw < VOICE_MAX_PLAYERS_DW; dw++)
	{	
		unsigned long serverBanMask = 0;
		unsigned long banMask = 0;
		for(unsigned long i=0; i < 32; i++)
		{
			int playerIndex = ( dw * 32 + i );
			if ( playerIndex >= MAX_PLAYERS )
				break;

			player_info_t pi;

			if ( !engine->GetPlayerInfo( i+1, &pi ) )
				continue;

			if ( m_BanMgr.GetPlayerBan( pi.guid ) )
			{
				banMask |= 1 << i;
			}

			if ( m_ServerBannedPlayers[playerIndex] )
			{
				serverBanMask |= 1 << i;
			}
		}

		if ( serverBanMask != banMask )
		{
			bChange = true;
		}

		// Ok, the server needs to be updated.
		char numStr[512];
		Q_snprintf(numStr, sizeof(numStr), " %lx", banMask);
		Q_strncat(str, numStr, sizeof(str), COPY_ALL_CHARACTERS);
	}

	if(bChange || bForce)
	{
		if( voice_clientdebug.GetInt() )
		{
			Msg( "CVoiceStatus::UpdateServerState: Sending '%s'\n", str );
		}

		engine->ServerCmd( str, false );	// Tell the server..
	}
	else
	{
		if( voice_clientdebug.GetInt() )
		{
			Msg( "CVoiceStatus::UpdateServerState: no change\n" );
		}
	}
	
	m_LastUpdateServerState = gpGlobals->curtime;
}

void CVoiceStatus::HandleVoiceMaskMsg(bf_read &msg)
{
	unsigned int dw;
	for(dw=0; dw < VOICE_MAX_PLAYERS_DW; dw++)
	{
		m_AudiblePlayers.SetDWord(dw, (unsigned long)msg.ReadLong());
		m_ServerBannedPlayers.SetDWord(dw, (unsigned long)msg.ReadLong());

		if( voice_clientdebug.GetInt())
		{
			Msg("CVoiceStatus::HandleVoiceMaskMsg\n");
			Msg("    - m_AudiblePlayers[%d] = %u\n", dw, m_AudiblePlayers.GetDWord(dw));
			Msg("    - m_ServerBannedPlayers[%d] = %u\n", dw, m_ServerBannedPlayers.GetDWord(dw));
		}
	}

	m_bServerModEnable = msg.ReadByte();
}

void CVoiceStatus::HandleReqStateMsg(bf_read &msg)
{
	if(voice_clientdebug.GetInt())
	{
		Msg("CVoiceStatus::HandleReqStateMsg\n");
	}

	UpdateServerState(true);	
}

void CVoiceStatus::StartSquelchMode()
{
	if(m_bInSquelchMode)
		return;

	m_bInSquelchMode = true;
	m_pHelper->UpdateCursorState();
}

void CVoiceStatus::StopSquelchMode()
{
	m_bInSquelchMode = false;
	m_pHelper->UpdateCursorState();
}

bool CVoiceStatus::IsInSquelchMode()
{
	return m_bInSquelchMode;
}

void SetOrUpdateBounds( 
	vgui::Panel *pPanel, 
	int left, int top, int wide, int tall, 
	bool bOnlyUpdateBounds, int &topCoord, int &bottomCoord )
{
	if ( bOnlyUpdateBounds )
	{
		if ( top < topCoord )
			topCoord = top;

		if ( (top+tall) >= bottomCoord )
			bottomCoord = top+tall;
	}
	else
	{
		pPanel->SetBounds( left, top, wide, tall );
	}
}

//-----------------------------------------------------------------------------
// Purpose: returns true if the target client has been banned
// Input  : playerID - 
// Output : Returns true on success, false on failure.
//-----------------------------------------------------------------------------
bool CVoiceStatus::IsPlayerBlocked(int iPlayer)
{
	player_info_t pi;

	if ( !engine->GetPlayerInfo( iPlayer, &pi ) )
		return false;

	return m_BanMgr.GetPlayerBan( pi.guid );
}

//-----------------------------------------------------------------------------
// Purpose: returns true if the player can't hear the other client due to game rules (eg. the other team)
// Input  : playerID - 
// Output : Returns true on success, false on failure.
//-----------------------------------------------------------------------------
bool CVoiceStatus::IsPlayerAudible(int iPlayer)
{
	return !!m_AudiblePlayers[iPlayer-1];
}


//-----------------------------------------------------------------------------
// returns true if the player is currently speaking
//-----------------------------------------------------------------------------
bool CVoiceStatus::IsPlayerSpeaking(int iPlayerIndex)
{
	return m_VoicePlayers[iPlayerIndex-1] != 0;
}

//-----------------------------------------------------------------------------
// returns true if the local player is attempting to speak
//-----------------------------------------------------------------------------
bool CVoiceStatus::IsLocalPlayerSpeaking( void )
{
#ifdef VOICE_VOX_ENABLE
	if ( voice_vox.GetBool() )
	{
		if ( m_bAboveThresholdTimer.IsElapsed() == true )
		{
			return false;
		}
	}
#endif // VOICE_VOX_ENABLE

	return m_bTalking;
}

//-----------------------------------------------------------------------------
// HL2SB: per-player loudness storage for the voice HUD.  See the header for
// the threading contract (aligned float stores, no lock).
//-----------------------------------------------------------------------------
void CVoiceStatus::SetPlayerLoudness(int entindex, float flLoudness)
{
	if ( entindex < 1 || entindex > VOICE_MAX_PLAYERS )
		return;

	if ( flLoudness < 0.0f )
		flLoudness = 0.0f;
	else if ( flLoudness > 1.0f )
		flLoudness = 1.0f;

	m_flVoiceLoudness[entindex - 1] = flLoudness;
	m_bVoiceLoudnessValid[entindex - 1] = true;
}

float CVoiceStatus::GetPlayerLoudness(int iPlayerIndex, bool *pValid) const
{
	if ( pValid )
		*pValid = false;

	if ( iPlayerIndex < 1 || iPlayerIndex > VOICE_MAX_PLAYERS )
		return 0.0f;

	if ( pValid )
		*pValid = m_bVoiceLoudnessValid[iPlayerIndex - 1];

	return m_flVoiceLoudness[iPlayerIndex - 1];
}

void CVoiceStatus::ClearLoudnessValid(int iPlayerIndex)
{
	if ( iPlayerIndex < 1 || iPlayerIndex > VOICE_MAX_PLAYERS )
		return;

	m_bVoiceLoudnessValid[iPlayerIndex - 1] = false;
}


//-----------------------------------------------------------------------------
// Purpose: blocks/unblocks the target client from being heard
// Input  : playerID - 
// Output : Returns true on success, false on failure.
//-----------------------------------------------------------------------------
void CVoiceStatus::SetPlayerBlockedState(int iPlayer, bool blocked)
{
	if (voice_clientdebug.GetInt())
	{
		Msg( "CVoiceStatus::SetPlayerBlockedState part 1\n" );
	}

	player_info_t pi;
	if ( !engine->GetPlayerInfo( iPlayer, &pi ) )
		return;

	if (voice_clientdebug.GetInt())
	{
		Msg( "CVoiceStatus::SetPlayerBlockedState part 2\n" );
	}

	// Squelch or (try to) unsquelch this player.
	if(voice_clientdebug.GetInt())
	{
		Msg("CVoiceStatus::SetPlayerBlockedState: setting player %d ban to %d\n", iPlayer, !m_BanMgr.GetPlayerBan(pi.guid));
	}

	m_BanMgr.SetPlayerBan(pi.guid, !m_BanMgr.GetPlayerBan(pi.guid));
	UpdateServerState(false);
}

//-----------------------------------------------------------------------------
// Purpose: HL2SB - set-not-toggle mute (Lua Player:SetMuted).  Stores the
// requested state for the player's identity and resyncs the vban mask; the
// server update pass diffs the mask first, so redundant writes stay silent -
// the same shape as the reference's mute store + mask resync.
//-----------------------------------------------------------------------------
void CVoiceStatus::SetPlayerBlocked(int iPlayer, bool bBlocked)
{
	if ( iPlayer < 1 || iPlayer > VOICE_MAX_PLAYERS )
		return;

	player_info_t pi;
	if ( !engine->GetPlayerInfo( iPlayer, &pi ) )
		return;

	m_BanMgr.SetPlayerBan( pi.guid, bBlocked );
	UpdateServerState( false );
}

//-----------------------------------------------------------------------------
// Purpose: 
//-----------------------------------------------------------------------------
void CVoiceStatus::SetHeadLabelMaterial( const char *pszMaterial )
{
	if ( m_pHeadLabelMaterial )
	{
		m_pHeadLabelMaterial->DecrementReferenceCount();
		m_pHeadLabelMaterial = NULL;
	}

	m_pHeadLabelMaterial = materials->FindMaterial( pszMaterial, TEXTURE_GROUP_VGUI );
	m_pHeadLabelMaterial->IncrementReferenceCount();
}