//========= Copyright Valve Corporation, All rights reserved. ============//
//
// Purpose: Normal HUD mode
//
// $Workfile:     $
// $Date:         $
// $NoKeywords: $
//=============================================================================//


#include "cbase.h"
#include "clientmode_shared.h"
#include "iinput.h"
#include "view_shared.h"
#include "iviewrender.h"
#include "hud_basechat.h"
#include "weapon_selection.h"
#include <vgui/IVGui.h>
#include <vgui/Cursor.h>
#include <vgui/IPanel.h>
#include <vgui/IInput.h>
#include "engine/IEngineSound.h"
#include <KeyValues.h>
#include <vgui_controls/AnimationController.h>
#include "vgui_int.h"
#include "hud_macros.h"
#include "hltvcamera.h"
#include "particlemgr.h"
#include "c_vguiscreen.h"
#include "c_team.h"
#include "c_rumble.h"
#include "fmtstr.h"
#include "achievementmgr.h"
#include "c_playerresource.h"
#include "cam_thirdperson.h"
#include <vgui/ILocalize.h>
#include "hud_vote.h"
#include "ienginevgui.h"
#include "sourcevr/isourcevirtualreality.h"
#if defined( _X360 )
#include "xbox/xbox_console.h"
#endif

#if defined( REPLAY_ENABLED )
#include "replay/replaycamera.h"
#include "replay/ireplaysystem.h"
#include "replay/iclientreplaycontext.h"
#include "replay/ireplaymanager.h"
#include "replay/replay.h"
#include "replay/ienginereplay.h"
#include "replay/vgui/replayreminderpanel.h"
#include "replay/vgui/replaymessagepanel.h"
#include "econ/econ_controls.h"
#include "econ/confirm_dialog.h"
extern IClientReplayContext *g_pClientReplayContext;
extern ConVar replay_rendersetting_renderglow;
#endif

#include "luamanager.h"
#include "lbaseentity_shared.h"
#include "lbaseplayer_shared.h"

#if defined USES_ECON_ITEMS
#include "econ_item_view.h"
#endif

#if defined( TF_CLIENT_DLL )
#include "c_tf_player.h"
#include "econ_item_description.h"
#endif

// memdbgon must be the last include file in a .cpp file!!!
#include "tier0/memdbgon.h"

#define ACHIEVEMENT_ANNOUNCEMENT_MIN_TIME 10

class CHudWeaponSelection;
class CHudChat;
class CHudVote;

static vgui::HContext s_hVGuiContext = DEFAULT_VGUI_CONTEXT;

ConVar cl_drawhud( "cl_drawhud", "1", FCVAR_CHEAT, "Enable the rendering of the hud" );
ConVar hud_takesshots( "hud_takesshots", "0", FCVAR_CLIENTDLL | FCVAR_ARCHIVE, "Auto-save a scoreboard screenshot at the end of a map." );
ConVar hud_freezecamhide( "hud_freezecamhide", "0", FCVAR_CLIENTDLL | FCVAR_ARCHIVE, "Hide the HUD during freeze-cam" );
ConVar cl_show_num_particle_systems( "cl_show_num_particle_systems", "0", FCVAR_CLIENTDLL, "Display the number of active particle systems." );

extern ConVar v_viewmodel_fov;
extern ConVar voice_modenable;

extern bool IsInCommentaryMode( void );

#ifdef VOICE_VOX_ENABLE
void VoxCallback( IConVar *var, const char *oldString, float oldFloat )
{
	if ( engine && engine->IsConnected() )
	{
		ConVarRef voice_vox( var->GetName() );
		if ( voice_vox.GetBool() && voice_modenable.GetBool() )
		{
			engine->ClientCmd_Unrestricted( "voicerecord_toggle on\n" );
		}
		else
		{
			engine->ClientCmd_Unrestricted( "voicerecord_toggle off\n" );
		}
	}
}
ConVar voice_vox( "voice_vox", "0", FCVAR_ARCHIVE, "Voice chat uses a vox-style always on", true, 0, true, 1, VoxCallback );

// --------------------------------------------------------------------------------- //
// CVoxManager.
// --------------------------------------------------------------------------------- //
class CVoxManager : public CAutoGameSystem
{
public:
	CVoxManager() : CAutoGameSystem( "VoxManager" )
	{
	}

	virtual void LevelInitPostEntity( void )
	{
		if ( voice_vox.GetBool() && voice_modenable.GetBool() )
		{
			engine->ClientCmd_Unrestricted( "voicerecord_toggle on\n" );
		}
	}

	virtual void LevelShutdownPreEntity( void )
	{
		if ( voice_vox.GetBool() )
		{
			engine->ClientCmd_Unrestricted( "voicerecord_toggle off\n" );
		}
	}
};

static CVoxManager s_VoxManager;
// --------------------------------------------------------------------------------- //
#endif // VOICE_VOX_ENABLE

CON_COMMAND( hud_reloadscheme, "Reloads hud layout and animation scripts." )
{
	ClientModeShared *mode = ( ClientModeShared * )GetClientModeNormal();
	if ( !mode )
		return;

	mode->ReloadScheme();
}

#ifdef _DEBUG
CON_COMMAND_F( crash, "Crash the client. Optional parameter -- type of crash:\n 0: read from NULL\n 1: write to NULL\n 2: DmCrashDump() (xbox360 only)", FCVAR_CHEAT )
{
	int crashtype = 0;
	int dummy;
	if ( args.ArgC() > 1 )
	{
		 crashtype = Q_atoi( args[1] );
	}
	switch (crashtype)
	{
		case 0:
			dummy = *((int *) NULL);
			Msg("Crashed! %d\n", dummy); // keeps dummy from optimizing out
			break;
		case 1:
			*((int *)NULL) = 42;
			break;
#if defined( _X360 )
		case 2:
			XBX_CrashDump(false);
			break;
#endif
		default:
			Msg("Unknown variety of crash. You have now failed to crash. I hope you're happy.\n");
			break;
	}
}
#endif // _DEBUG

static void __MsgFunc_Rumble( bf_read &msg )
{
	unsigned char waveformIndex;
	unsigned char rumbleData;
	unsigned char rumbleFlags;

	waveformIndex = msg.ReadByte();
	rumbleData = msg.ReadByte();
	rumbleFlags = msg.ReadByte();

	RumbleEffect( waveformIndex, rumbleData, rumbleFlags );
}

static void __MsgFunc_VGUIMenu( bf_read &msg )
{
	char panelname[2048]; 
	
	msg.ReadString( panelname, sizeof(panelname) );

	bool  bShow = msg.ReadByte()!=0;
	
	IViewPortPanel *viewport = gViewPortInterface->FindPanelByName( panelname );

	if ( !viewport )
	{
		// DevMsg("VGUIMenu: couldn't find panel '%s'.\n", panelname );
		return;
	}

	int count = msg.ReadByte();

	if ( count > 0 )
	{
		KeyValues *keys = new KeyValues("data");
		//Msg( "MsgFunc_VGUIMenu:\n" );

		for ( int i=0; i<count; i++)
		{
			char name[255];
			char data[255];

			msg.ReadString( name, sizeof(name) );
			msg.ReadString( data, sizeof(data) );
			//Msg( "  %s <- '%s'\n", name, data );

			keys->SetString( name, data );
		}

		// !KLUDGE! Whitelist of URL protocols formats for MOTD
		if (
			!V_stricmp( panelname, PANEL_INFO ) // MOTD
			&& keys->GetInt( "type", 0 ) == 2 // URL message type
		) {
			const char *pszURL = keys->GetString( "msg", "" );
			if ( Q_strncmp( pszURL, "http://", 7 ) != 0 && Q_strncmp( pszURL, "https://", 8 ) != 0 && Q_stricmp( pszURL, "about:blank" ) != 0 )
			{
				Warning( "Blocking MOTD URL '%s'; must begin with 'http://' or 'https://' or be about:blank\n", pszURL );
				keys->deleteThis();
				return;
			}
		}

		viewport->SetData( keys );

		keys->deleteThis();
	}

	// is the server telling us to show the scoreboard (at the end of a map)?
	if ( Q_stricmp( panelname, "scores" ) == 0 )
	{
		if ( hud_takesshots.GetBool() == true )
		{
			gHUD.SetScreenShotTime( gpGlobals->curtime + 1.0 ); // take a screenshot in 1 second
		}
	}

	// is the server trying to show an MOTD panel? Check that it's allowed right now.
	ClientModeShared *mode = ( ClientModeShared * )GetClientModeNormal();
	if ( Q_stricmp( panelname, PANEL_INFO ) == 0 && mode )
	{
		if ( !mode->IsInfoPanelAllowed() )
		{
			return;
		}
		else
		{
			mode->InfoPanelDisplayed();
		}
	}

	gViewPortInterface->ShowPanel( viewport, bShow );
}

//-----------------------------------------------------------------------------
// Purpose: 
//-----------------------------------------------------------------------------
ClientModeShared::ClientModeShared()
{
#ifdef LUA_SDK
	m_pScriptedViewport = NULL;
#endif
	m_pViewport = NULL;
#ifdef LUA_SDK
	m_pClientLuaPanel = NULL;
#endif
	m_pChatElement = NULL;
	m_pWeaponSelection = NULL;
	m_nRootSize[ 0 ] = m_nRootSize[ 1 ] = -1;

#if defined( REPLAY_ENABLED )
	m_pReplayReminderPanel = NULL;
	m_flReplayStartRecordTime = 0.0f;
	m_flReplayStopRecordTime = 0.0f;
#endif
}

//-----------------------------------------------------------------------------
// Purpose: 
//-----------------------------------------------------------------------------
ClientModeShared::~ClientModeShared()
{
#ifdef LUA_SDK
	// NOTE: Due to the behavior of many crashes, if you end up here from a
	// .mdmp or debug attach, you might as well ignore this call stack.
	delete m_pScriptedViewport; 
#endif
	delete m_pViewport; 
#ifdef LUA_SDK
	delete m_pClientLuaPanel; 
#endif
}

void ClientModeShared::ReloadScheme( void )
{
	m_pViewport->ReloadScheme( "resource/ClientScheme.res" );
	ClearKeyValuesCache();
}


//----------------------------------------------------------------------------
// Purpose: Let the client mode set some vgui conditions
//-----------------------------------------------------------------------------
void	ClientModeShared::ComputeVguiResConditions( KeyValues *pkvConditions ) 
{
	if ( UseVR() )
	{
		pkvConditions->FindKey( "if_vr", true );
	}
}



//-----------------------------------------------------------------------------
// Purpose: 
//-----------------------------------------------------------------------------
void ClientModeShared::Init()
{
	m_pChatElement = ( CBaseHudChat * )GET_HUDELEMENT( CHudChat );
	Assert( m_pChatElement );

	m_pWeaponSelection = ( CBaseHudWeaponSelection * )GET_HUDELEMENT( CHudWeaponSelection );
	Assert( m_pWeaponSelection );

	KeyValuesAD pConditions( "conditions" );
	ComputeVguiResConditions( pConditions );

	// Derived ClientMode class must make sure m_Viewport is instantiated
	Assert( m_pViewport );
	m_pViewport->LoadControlSettings( "scripts/HudLayout.res", NULL, NULL, pConditions );

#if defined( REPLAY_ENABLED )
 	m_pReplayReminderPanel = GET_HUDELEMENT( CReplayReminderPanel );
 	Assert( m_pReplayReminderPanel );
#endif

	ListenForGameEvent( "player_connect" );
	ListenForGameEvent( "player_disconnect" );
	ListenForGameEvent( "player_team" );
	ListenForGameEvent( "server_cvar" );
	ListenForGameEvent( "player_changename" );
	ListenForGameEvent( "teamplay_broadcast_audio" );
	ListenForGameEvent( "achievement_earned" );

#if defined( TF_CLIENT_DLL )
	ListenForGameEvent( "item_found" );
#endif 

#if defined( REPLAY_ENABLED )
	ListenForGameEvent( "replay_startrecord" );
	ListenForGameEvent( "replay_endrecord" );
	ListenForGameEvent( "replay_replaysavailable" );
	ListenForGameEvent( "replay_servererror" );
	ListenForGameEvent( "game_newmap" );
#endif

#ifndef _XBOX
	HLTVCamera()->Init();
#if defined( REPLAY_ENABLED )
	ReplayCamera()->Init();
#endif
#endif

	m_CursorNone = vgui::dc_none;

	HOOK_MESSAGE( VGUIMenu );
	HOOK_MESSAGE( Rumble );
}


void ClientModeShared::InitViewport()
{
}


void ClientModeShared::VGui_Shutdown()
{
#ifdef LUA_SDK
	delete m_pScriptedViewport;
	m_pScriptedViewport = NULL;
#endif
	delete m_pViewport;
	m_pViewport = NULL;
#ifdef LUA_SDK
	delete m_pClientLuaPanel;
	m_pClientLuaPanel = NULL;
#endif
}


//-----------------------------------------------------------------------------
// Purpose: 
//-----------------------------------------------------------------------------
void ClientModeShared::Shutdown()
{
}

//-----------------------------------------------------------------------------
// Purpose: 
// Input  : frametime - 
//			*cmd - 
//-----------------------------------------------------------------------------
bool ClientModeShared::CreateMove( float flInputSampleTime, CUserCmd *cmd )
{
	// Let the player override the view.
	C_BasePlayer *pPlayer = C_BasePlayer::GetLocalPlayer();
	if(!pPlayer)
		return true;

	// Let the player at it
	return pPlayer->CreateMove( flInputSampleTime, cmd );
}

// HL2SB (sbrust): the vehicle third person camera was re-architected to GMod's
// own shape (2026-10-04): the STATE lives on the vehicle entity as networked
// fields (CPropVehicleDriveable::m_bThirdPersonMode / m_flCameraDistance), the
// SERVER is the single writer (HL2SB_UpdateCameraState -- the C++ port of GMod's
// base-gamemode GM:VehicleMove: IN_DUCK edge flips the mode, the usercmd mouse
// wheel drives the distance multiplier), and the CAMERA is the gamemode's Lua
// GM:CalcVehicleView dispatched through GM:CalcView below.  The old client-side
// convar camera (hl2sb_veh_thirdperson / _dist / _up, the toggle/zoom commands,
// HL2SB_ApplyVehicleThirdPersonView and the KeyInput wheel consumer) is gone --
// it was the reason CTRL "printed but did not switch": three independent CTRL
// detectors (a ConCommand, a Lua Think edge, and a CreateMove edge) fought over
// one global convar.  GMod has no client-side vehicle camera path at all.
//
// Only the debug switch remains (it gates HL2SB_DebugVehicleCamera below, still
// useful while chasing the new Lua camera chain).
ConVar hl2sb_veh_thirdperson_debug( "hl2sb_veh_thirdperson_debug", "0", FCVAR_ARCHIVE,
								    "Print vehicle camera view-chain diagnostics." );

// HL2SB (sbrust): the frame's view.drawviewer latch, defined next to
// HL2SB_LuaCalcView in game/shared/baseplayer_shared.cpp.  Local externs follow
// the same convention this file already uses for the CalcView dispatch itself.
extern void HL2SB_SetLuaViewDrawViewer( bool bSet );
extern bool HL2SB_GetLuaViewDrawViewer( void );

// HL2SB: shared debug for the vehicle camera view chain.  Call sites: the Lua
// CalcView readback ("luacalc", game/shared/baseplayer_shared.cpp),
// SetUpViews ("final", view.cpp) and KeyInput wheel events ("wheel").  The
// GMod-style camera itself lives in the gamemode Lua (GM:CalcVehicleView) with
// its state networked on the vehicle (see above).
void HL2SB_DebugVehicleCamera( const char *pszWhere, const Vector &vecOrigin, const QAngle &angView,
							   const char *pszDetail )
{
	if ( !hl2sb_veh_thirdperson_debug.GetBool() )
	{
		return;
	}

	// Call sites today: SetUpViews ("final", view.cpp) and the wheel/key debug
	// events ("wheel") -- the calc-side site ("calc") moved out with the C++
	// camera when it became the gamemode's Lua GM:CalcVehicleView (2026-10-04);
	// the Lua CalcView readback also reports ("luacalc", baseplayer_shared.cpp).
	struct HL2SB_VehCamDebugCache_t
	{
		const char *pszWhere;
		float flNextPrint;
	};
	static HL2SB_VehCamDebugCache_t s_Cache[8];
	static int s_nCache = 0;

	// Find (or add) this call site's slot.
	int iSlot = -1;
	for ( int i = 0; i < s_nCache; ++i )
	{
		if ( s_Cache[i].pszWhere == pszWhere )
		{
			iSlot = i;
			break;
		}
	}

	if ( iSlot < 0 )
	{
		if ( s_nCache >= (int)ARRAYSIZE( s_Cache ) )
		{
			return;		// never happens; do not grow state we cannot own
		}

		iSlot = s_nCache++;
		s_Cache[iSlot].pszWhere = pszWhere;
		s_Cache[iSlot].flNextPrint = 0.0f;
	}

	if ( gpGlobals->curtime < s_Cache[iSlot].flNextPrint )
	{
		return;
	}

	s_Cache[iSlot].flNextPrint = gpGlobals->curtime + 1.0f;

	if ( pszDetail && pszDetail[0] )
	{
		Msg( "[HL2SB veh3rd/%s] origin=(%.1f %.1f %.1f) pitch=%.1f yaw=%.1f | %s\n", pszWhere,
			 vecOrigin.x, vecOrigin.y, vecOrigin.z, angView[ PITCH ], angView[ YAW ], pszDetail );
	}
	else
	{
		Msg( "[HL2SB veh3rd/%s] origin=(%.1f %.1f %.1f) pitch=%.1f yaw=%.1f\n", pszWhere,
			 vecOrigin.x, vecOrigin.y, vecOrigin.z, angView[ PITCH ], angView[ YAW ] );
	}
}

//-----------------------------------------------------------------------------
// Purpose: 
// Input  : *pSetup - 
//-----------------------------------------------------------------------------
void ClientModeShared::OverrideView( CViewSetup *pSetup )
{
	QAngle camAngles;

	// HL2SB (sbrust): every view pass starts fresh -- view.drawviewer from the
	// frame's GM:CalcView dispatch is latched below (HL2SB_LuaCalcView) and
	// consumed by C_BasePlayer::ShouldDrawLocalPlayer / LocalPlayerInFirstPersonView.
	HL2SB_SetLuaViewDrawViewer( false );

	// Let the player override the view.
	C_BasePlayer *pPlayer = C_BasePlayer::GetLocalPlayer();
	if(!pPlayer)
		return;

	pPlayer->OverrideView( pSetup );

	if( ::input->CAM_IsThirdPerson() )
	{
		Vector cam_ofs = g_ThirdPersonManager.GetCameraOffsetAngles();
		Vector cam_ofs_distance = g_ThirdPersonManager.GetFinalCameraOffset();

		cam_ofs_distance *= g_ThirdPersonManager.GetDistanceFraction();

		camAngles[ PITCH ] = cam_ofs[ PITCH ];
		camAngles[ YAW ] = cam_ofs[ YAW ];
		camAngles[ ROLL ] = 0;

		Vector camForward, camRight, camUp;
		

		if ( g_ThirdPersonManager.IsOverridingThirdPerson() == false )
		{
			engine->GetViewAngles( camAngles );
		}
			
		// get the forward vector
		AngleVectors( camAngles, &camForward, &camRight, &camUp );
	
		VectorMA( pSetup->origin, -cam_ofs_distance[0], camForward, pSetup->origin );
		VectorMA( pSetup->origin, cam_ofs_distance[1], camRight, pSetup->origin );
		VectorMA( pSetup->origin, cam_ofs_distance[2], camUp, pSetup->origin );

		// Override angles from third person camera
		VectorCopy( camAngles, pSetup->angles );
	}
	else if (::input->CAM_IsOrthographic())
	{
		pSetup->m_bOrtho = true;
		float w, h;
		::input->CAM_OrthographicSize( w, h );
		w *= 0.5f;
		h *= 0.5f;
		pSetup->m_OrthoLeft   = -w;
		pSetup->m_OrthoTop    = -h;
		pSetup->m_OrthoRight  = w;
		pSetup->m_OrthoBottom = h;
	}

	// HL2SB (sbrust): the only vehicle camera now is the gamemode's Lua
	// GM:CalcVehicleView, dispatched by HL2SB_LuaCalcView below -- GMod's single
	// hook-id-4 call site sits in this same stage of their SetUpViews flow, and
	// nothing writes the view afterwards (the Lua-SDK-era CViewRender::MP_PostSimulate
	// re-pin stage was removed 2026-10-04).  The forced 38-degree-down pitch that
	// started all of this is fixed at its root in the pod itself, with GMod's own
	// `limitview 0` key -- see C_PropVehiclePrisonerPod::UpdateViewAngles in
	// game/client/hl2/c_vehicle_prisoner_pod.cpp.  The old C++ convar camera
	// (HL2SB_ApplyVehicleThirdPersonView) applied here was deleted with the
	// re-architecture; the Lua camera replaces it exactly.

	// HL2SB (2026-10-04) GMod parity: GM:CalcView dispatched from OverrideView,
	// against the FULLY assembled view (vehicle eye included) -- GMod's single
	// hook-id-4 call site sits in this same stage of their SetUpViews flow, and
	// nothing writes the view afterwards.  The
	// returned CamData (origin/angles/fov/znear/zfar) is applied into pSetup by
	// HL2SB_LuaCalcView; in-place edits of its pushed origin/angles vectors
	// reach the engine because the gamemode's GM:CalcView forwards them into
	// the returned table by reference (First Person Body's vehicle eye snap).
	// znear/zfar are written back too -- the seat handed in real clip planes.
	C_BasePlayer *pCalcViewPlayer = C_BasePlayer::GetLocalPlayer();
	if ( pCalcViewPlayer != NULL )
	{
#ifdef LUA_SDK
		extern void HL2SB_LuaCalcView( CBasePlayer *pPlayer, Vector &eyeOrigin, QAngle &eyeAngles,
									   float &fov, float flZNear, float flZFar,
									   float *pOutNear, float *pOutFar );
		HL2SB_LuaCalcView( pCalcViewPlayer, pSetup->origin, pSetup->angles, pSetup->fov,
						   pSetup->zNear, pSetup->zFar, &pSetup->zNear, &pSetup->zFar );
#endif
	}
}

//-----------------------------------------------------------------------------
// Purpose: 
//-----------------------------------------------------------------------------
bool ClientModeShared::ShouldDrawEntity(C_BaseEntity *pEnt)
{
#ifdef LUA_SDK
	BEGIN_LUA_CALL_HOOK( "ShouldDrawEntity" );
		lua_pushentity( L, pEnt );
	END_LUA_CALL_HOOK( 1, 1 );

	RETURN_LUA_BOOLEAN();
#endif

	return true;
}

//-----------------------------------------------------------------------------
// Purpose: 
//-----------------------------------------------------------------------------
bool ClientModeShared::ShouldDrawParticles( )
{
#ifdef LUA_SDK
	BEGIN_LUA_CALL_HOOK( "ShouldDrawParticles" );
	END_LUA_CALL_HOOK( 0, 1 );

	RETURN_LUA_BOOLEAN();
#endif

#ifdef TF_CLIENT_DLL
	C_TFPlayer *pTFPlayer = C_TFPlayer::GetLocalTFPlayer();
	if ( pTFPlayer && !pTFPlayer->ShouldPlayerDrawParticles() )
		return false;
#endif // TF_CLIENT_DLL

	return true;
}

//-----------------------------------------------------------------------------
// Purpose: Allow weapons to override mouse input (for binoculars)
//-----------------------------------------------------------------------------
void ClientModeShared::OverrideMouseInput( float *x, float *y )
{
	C_BaseCombatWeapon *pWeapon = GetActiveWeapon();
	if ( pWeapon )
	{
		pWeapon->OverrideMouseInput( x, y );
	}
}

#ifdef ARGG
//-----------------------------------------------------------------------------
// Purpose: Allow weapons to override mouse input to view angles (for orbiting)
//-----------------------------------------------------------------------------
// adnan
// control the mouse input in the grav gun through this
bool ClientModeShared::OverrideViewAngles( void )
{
	C_BaseCombatWeapon *pWeapon = GetActiveWeapon();
	if ( pWeapon )
	{
		// adnan
		return pWeapon->OverrideViewAngles();
	}

	return false;
}
// end adnan
#endif

//-----------------------------------------------------------------------------
// Purpose: 
//-----------------------------------------------------------------------------
bool ClientModeShared::ShouldDrawViewModel()
{
#ifdef LUA_SDK
	BEGIN_LUA_CALL_HOOK( "ShouldDrawViewModel" );
	END_LUA_CALL_HOOK( 0, 1 );

	RETURN_LUA_BOOLEAN();
#endif

	return true;
}

bool ClientModeShared::ShouldDrawDetailObjects( )
{
#ifdef LUA_SDK
	BEGIN_LUA_CALL_HOOK( "ShouldDrawDetailObjects" );
	END_LUA_CALL_HOOK( 0, 1 );

	RETURN_LUA_BOOLEAN();
#endif

	return true;
}


//-----------------------------------------------------------------------------
// Purpose: Returns true if VR mode should black out everything outside the HUD.
//			This is used for things like sniper scopes and full screen UI
//-----------------------------------------------------------------------------
bool ClientModeShared::ShouldBlackoutAroundHUD()
{
	return enginevgui->IsGameUIVisible();
}


//-----------------------------------------------------------------------------
// Purpose: Allows the client mode to override mouse control stuff in sourcevr
//-----------------------------------------------------------------------------
HeadtrackMovementMode_t ClientModeShared::ShouldOverrideHeadtrackControl() 
{
	return HMM_NOOVERRIDE;
}


//-----------------------------------------------------------------------------
// Purpose: 
// Output : Returns true on success, false on failure.
//-----------------------------------------------------------------------------
bool ClientModeShared::ShouldDrawCrosshair( void )
{
	return true;
}

//-----------------------------------------------------------------------------
// Purpose: Don't draw the current view entity if we are using the fake viewmodel instead
//-----------------------------------------------------------------------------
bool ClientModeShared::ShouldDrawLocalPlayer( C_BasePlayer *pPlayer )
{
#ifdef LUA_SDK
	BEGIN_LUA_CALL_HOOK( "ShouldDrawLocalPlayer" );
		lua_pushplayer( L, pPlayer );
	END_LUA_CALL_HOOK( 1, 1 );

	RETURN_LUA_BOOLEAN();
#endif

	if ( ( pPlayer->index == render->GetViewEntity() ) && !C_BasePlayer::ShouldDrawLocalPlayer() )
		return false;

	return true;
}


//-----------------------------------------------------------------------------
// Purpose: The mode can choose to not draw fog
//-----------------------------------------------------------------------------
bool ClientModeShared::ShouldDrawFog( void )
{
#ifdef LUA_SDK
	BEGIN_LUA_CALL_HOOK( "ShouldDrawFog" );
	END_LUA_CALL_HOOK( 0, 1 );

	RETURN_LUA_BOOLEAN();
#endif

	return true;
}

//-----------------------------------------------------------------------------
// Purpose: 
//-----------------------------------------------------------------------------
void ClientModeShared::AdjustEngineViewport( int& x, int& y, int& width, int& height )
{
#ifdef LUA_SDK
	BEGIN_LUA_CALL_HOOK( "AdjustEngineViewport" );
		lua_pushinteger( L, x );
		lua_pushinteger( L, y );
		lua_pushinteger( L, width );
		lua_pushinteger( L, height );
	END_LUA_CALL_HOOK( 4, 4 );

	if ( lua_isnumber( L, -4 ) )
		x = luaL_checkint( L, -4 );
	if ( lua_isnumber( L, -3 ) )
		y = luaL_checkint( L, -3 );
	if ( lua_isnumber( L, -2 ) )
		width = luaL_checkint( L, -2 );
	if ( lua_isnumber( L, -1 ) )
		height = luaL_checkint( L, -1 );

	lua_pop( L, 4 );
#endif
}

//-----------------------------------------------------------------------------
// Purpose:
//-----------------------------------------------------------------------------
void ClientModeShared::PreRender( CViewSetup *pSetup )
{
	// HL2SB: GM:PreRender is deliberately NOT dispatched yet - its GMod
	// contract (return true cancels the whole frame render) needs a check in
	// the frame owner, not in this void method.  Deferred to the render-cancel
	// round.
}

//-----------------------------------------------------------------------------
// Purpose:
//-----------------------------------------------------------------------------
void ClientModeShared::PostRender()
{
#if defined( LUA_SDK )
	// HL2SB (2026-10-08): GM:PostRender() - GMod raises it once the frame has
	// rendered (their PreRender/PostRender pair brackets the frame; the cancel
	// half is the one still unwired here).
	{
		extern bool g_bRenderingReflection;
		if ( L != NULL && !g_bRenderingReflection && engine->IsInGame() )
		{
			BEGIN_LUA_CALL_HOOK( "PostRender" );
			END_LUA_CALL_HOOK( 0, 0 );
		}
	}
#endif

	// Let the particle manager simulate things that haven't been simulated.
	ParticleMgr()->PostRender();
}

void ClientModeShared::PostRenderVGui()
{
#if defined( LUA_SDK )
	// HL2SB (2026-10-08): GM:PostRenderVGUI() - fired after the VGUI paint
	// pass, still inside the 2D context (the main view calls this from its
	// Push2DView block).  GMod fires it unconditionally each frame; the guard
	// keeps menu-realm noise out, same as every other 2D pump here.
	{
		extern bool g_bRenderingReflection;
		if ( L != NULL && !g_bRenderingReflection && engine->IsInGame() )
		{
			BEGIN_LUA_CALL_HOOK( "PostRenderVGUI" );
			END_LUA_CALL_HOOK( 0, 0 );
		}
	}
#endif
}

//-----------------------------------------------------------------------------
// Purpose: 
//-----------------------------------------------------------------------------
void ClientModeShared::Update()
{
#if defined( REPLAY_ENABLED )
	UpdateReplayMessages();
#endif

	if ( m_pViewport->IsVisible() != cl_drawhud.GetBool() )
	{
		m_pViewport->SetVisible( cl_drawhud.GetBool() );
	}

	UpdateRumbleEffects();

	if ( cl_show_num_particle_systems.GetBool() )
	{
		int nCount = 0;

		for ( int i = 0; i < g_pParticleSystemMgr->GetParticleSystemCount(); i++ )
		{
			const char *pParticleSystemName = g_pParticleSystemMgr->GetParticleSystemNameFromIndex(i);
			CParticleSystemDefinition *pParticleSystem = g_pParticleSystemMgr->FindParticleSystem( pParticleSystemName );
			if ( !pParticleSystem )
				continue;

			for ( CParticleCollection *pCurCollection = pParticleSystem->FirstCollection();
				  pCurCollection != NULL;
				  pCurCollection = pCurCollection->GetNextCollectionUsingSameDef() )
			{
				++nCount;
			}
		}

		engine->Con_NPrintf( 0, "# Active particle systems: %i", nCount );
	}

#if defined( LUA_SDK )
	// HL2SB GMod compat (2026-09-24): fire "Think" and "Tick" once per client
	// frame from the engine loop.  GMod fires GM:Think per frame and GM:Tick
	// per tick from its client frame driver; until now the ONLY "Think"
	// dispatch was CHL2MPRules::Think on the SERVER, and the content side had
	// to bridge Think through HudViewportPaint (hl2sb_notification.lua - now
	// removed).  First Person Body parks 6 hooks on Think and its slow
	// processing on Tick.  Update() only runs in-game (g_pClientMode exists
	// once a mode is active), which is exactly the GMod realm behaviour.
	if ( L != NULL && engine->IsInGame() )
	{
		BEGIN_LUA_CALL_HOOK( "Think" );
		END_LUA_CALL_HOOK( 0, 0 );
		BEGIN_LUA_CALL_HOOK( "Tick" );
		END_LUA_CALL_HOOK( 0, 0 );
	}

	// HL2SB (2026-09-25): the engine timer library (game/shared/lua/ltimer.cpp)
	// and the rate-limited client->server net transport (lnet.cpp) pump here.
	// They used to hang off CScriptedHudViewport::Paint, which only runs when a
	// scripted HUD viewport control exists - client timers and queued net
	// commands stalled on any client UI without one.  Not gated on IsInGame on
	// purpose: connected-but-paused still ticks, like GMod.
	if ( L != NULL )
	{
		LUA_API void HL2SB_TimerTick( void );
		HL2SB_TimerTick();

		LUA_API void HL2SB_NetCmdPump( void );
		HL2SB_NetCmdPump();
	}
#endif
}

//-----------------------------------------------------------------------------
// This processes all input before SV Move messages are sent
//-----------------------------------------------------------------------------

void ClientModeShared::ProcessInput(bool bActive)
{
	gHUD.ProcessInput( bActive );
}

//-----------------------------------------------------------------------------
// Purpose: We've received a keypress from the engine. Return 1 if the engine is allowed to handle it.
//-----------------------------------------------------------------------------
int	ClientModeShared::KeyInput( int down, ButtonCode_t keynum, const char *pszCurrentBinding )
{
	// HL2SB (sbrust): the mouse-wheel vehicle zoom moved server-side with the
	// camera re-architecture (GMod's own shape: the wheel rides in the usercmd
	// -- CInput::KeyEvent accumulates it in in_main.cpp, and the driveable
	// vehicle's server-side camera-state writer consumes it, the C++ port of
	// GMod's GM:VehicleMove).  The old block here was the wheel's fourth local
	// consumer; the wheel keeps its stock engine behavior on foot (weapon
	// selection) and for the gravity gun (its own KeyInput).

#ifdef LUA_SDK
	if ( g_bLuaInitialized )
	{
		BEGIN_LUA_CALL_HOOK( "KeyInput" );
			lua_pushinteger( L, down );
			lua_pushinteger( L, keynum );
			lua_pushstring( L, pszCurrentBinding );
		END_LUA_CALL_HOOK( 3, 1 );

		RETURN_LUA_INTEGER();
	}
#endif

	if ( engine->Con_IsVisible() )
		return 1;

	// Should we start typing a message?
	if ( pszCurrentBinding &&
		( Q_strcmp( pszCurrentBinding, "messagemode" ) == 0 ||
		  Q_strcmp( pszCurrentBinding, "say" ) == 0 ) )
	{
		if ( down )
		{
			StartMessageMode( MM_SAY );
		}
		return 0;
	}
	else if ( pszCurrentBinding &&
				( Q_strcmp( pszCurrentBinding, "messagemode2" ) == 0 ||
				  Q_strcmp( pszCurrentBinding, "say_team" ) == 0 ) )
	{
		if ( down )
		{
			StartMessageMode( MM_SAY_TEAM );
		}
		return 0;
	}
	
	// If we're voting...
#ifdef VOTING_ENABLED
	CHudVote *pHudVote = GET_HUDELEMENT( CHudVote );
	if ( pHudVote && pHudVote->IsVisible() )
	{
		if ( !pHudVote->KeyInput( down, keynum, pszCurrentBinding ) )
		{
			return 0;
		}
	}
#endif

	C_BasePlayer *pPlayer = C_BasePlayer::GetLocalPlayer();

	// if ingame spectator mode, let spectator input intercept key event here
	if( pPlayer &&
		( pPlayer->GetObserverMode() > OBS_MODE_DEATHCAM ) &&
		!HandleSpectatorKeyInput( down, keynum, pszCurrentBinding ) )
	{
		return 0;
	}

	// Let game-specific hud elements get a crack at the key input
	if ( !HudElementKeyInput( down, keynum, pszCurrentBinding ) )
	{
		return 0;
	}

	C_BaseCombatWeapon *pWeapon = GetActiveWeapon();
	if ( pWeapon )
	{
		return pWeapon->KeyInput( down, keynum, pszCurrentBinding );
	}

	return 1;
}

//-----------------------------------------------------------------------------
// Purpose: See if spectator input occurred. Return 0 if the key is swallowed.
//-----------------------------------------------------------------------------
int ClientModeShared::HandleSpectatorKeyInput( int down, ButtonCode_t keynum, const char *pszCurrentBinding )
{
	// we are in spectator mode, open spectator menu
	if ( down && pszCurrentBinding && Q_strcmp( pszCurrentBinding, "+duck" ) == 0 )
	{
		m_pViewport->ShowPanel( PANEL_SPECMENU, true );
		return 0; // we handled it, don't handle twice or send to server
	}
	else if ( down && pszCurrentBinding && Q_strcmp( pszCurrentBinding, "+attack" ) == 0 )
	{
		engine->ClientCmd( "spec_next" );
		return 0;
	}
	else if ( down && pszCurrentBinding && Q_strcmp( pszCurrentBinding, "+attack2" ) == 0 )
	{
		engine->ClientCmd( "spec_prev" );
		return 0;
	}
	else if ( down && pszCurrentBinding && Q_strcmp( pszCurrentBinding, "+jump" ) == 0 )
	{
		engine->ClientCmd( "spec_mode" );
		return 0;
	}
	else if ( down && pszCurrentBinding && Q_strcmp( pszCurrentBinding, "+strafe" ) == 0 )
	{
		HLTVCamera()->SetAutoDirector( true );
#if defined( REPLAY_ENABLED )
		ReplayCamera()->SetAutoDirector( true );
#endif
		return 0;
	}

	return 1;
}

//-----------------------------------------------------------------------------
// Purpose: See if hud elements want key input. Return 0 if the key is swallowed
//-----------------------------------------------------------------------------
int ClientModeShared::HudElementKeyInput( int down, ButtonCode_t keynum, const char *pszCurrentBinding )
{
	if ( m_pWeaponSelection )
	{
		if ( !m_pWeaponSelection->KeyInput( down, keynum, pszCurrentBinding ) )
		{
			return 0;
		}
	}

#if defined( REPLAY_ENABLED )
	if ( m_pReplayReminderPanel )
	{
		if ( m_pReplayReminderPanel->HudElementKeyInput( down, keynum, pszCurrentBinding ) )
		{
			return 0;
		}
	}
#endif

	return 1;
}


//-----------------------------------------------------------------------------
// Purpose: 
//-----------------------------------------------------------------------------
bool ClientModeShared::DoPostScreenSpaceEffects( const CViewSetup *pSetup )
{
#if defined( REPLAY_ENABLED )
	if ( engine->IsPlayingDemo() )
	{
		if ( !replay_rendersetting_renderglow.GetBool() )
			return false;
	}
#endif 
	return true;
}

//-----------------------------------------------------------------------------
// Purpose: 
// Output : vgui::Panel
//-----------------------------------------------------------------------------
vgui::Panel *ClientModeShared::GetMessagePanel()
{
	if ( m_pChatElement && m_pChatElement->GetInputPanel() && m_pChatElement->GetInputPanel()->IsVisible() )
		return m_pChatElement->GetInputPanel();

	return NULL;
}

//-----------------------------------------------------------------------------
// Purpose: The player has started to type a message
//-----------------------------------------------------------------------------
void ClientModeShared::StartMessageMode( int iMessageModeType )
{
	// Can only show chat UI in multiplayer!!!
	if ( gpGlobals->maxClients == 1 )
	{
		return;
	}
	if ( m_pChatElement )
	{
		m_pChatElement->StartMessageMode( iMessageModeType );
	}
}

//-----------------------------------------------------------------------------
// Purpose: 
// Input  : *newmap - 
//-----------------------------------------------------------------------------
void ClientModeShared::LevelInit( const char *newmap )
{
	m_pViewport->GetAnimationController()->StartAnimationSequence("LevelInit");

	// Tell the Chat Interface
	if ( m_pChatElement )
	{
		m_pChatElement->LevelInit( newmap );
	}

	// we have to fake this event clientside, because clients connect after that
	IGameEvent *event = gameeventmanager->CreateEvent( "game_newmap" );
	if ( event )
	{
		event->SetString("mapname", newmap );
		gameeventmanager->FireEventClientSide( event );
	}

	// Create a vgui context for all of the in-game vgui panels...
	if ( s_hVGuiContext == DEFAULT_VGUI_CONTEXT )
	{
		s_hVGuiContext = vgui::ivgui()->CreateContext();
	}

	// Reset any player explosion/shock effects
	CLocalPlayerFilter filter;
	enginesound->SetPlayerDSP( filter, 0, true );
}

//-----------------------------------------------------------------------------
// Purpose: 
//-----------------------------------------------------------------------------
void ClientModeShared::LevelShutdown( void )
{
	// Reset the third person camera so we don't crash
	g_ThirdPersonManager.Init();

	if ( m_pChatElement )
	{
		m_pChatElement->LevelShutdown();
	}
	if ( s_hVGuiContext != DEFAULT_VGUI_CONTEXT )
	{
		vgui::ivgui()->DestroyContext( s_hVGuiContext );
 		s_hVGuiContext = DEFAULT_VGUI_CONTEXT;
	}

	// Reset any player explosion/shock effects
	CLocalPlayerFilter filter;
	enginesound->SetPlayerDSP( filter, 0, true );
}


void ClientModeShared::Enable()
{
	vgui::VPANEL pRoot = VGui_GetClientDLLRootPanel();;

	// Add our viewport to the root panel.
	if( pRoot != 0 )
	{
		m_pViewport->SetParent( pRoot );
#ifdef LUA_SDK
		// The scripted viewport is what drives the "HudViewportPaint" Lua hook.
		// It was never parented, sized or shown, so that hook never fired and
		// Lua HUD code had no place to draw.
		if ( m_pScriptedViewport )
			m_pScriptedViewport->SetParent( pRoot );

		// The Lua root panel is the default parent for every panel a script
		// creates with vgui.Panel / vgui.Frame / vgui.ModelPanel.  Disable()
		// unparents and hides it, but Enable() never put it back, so any Lua
		// panel created afterwards was a child of an invisible panel and never
		// rendered.
		if ( m_pClientLuaPanel )
			m_pClientLuaPanel->SetParent( pRoot );
#endif
	}

	// All hud elements should be proportional
	// This sets that flag on the viewport and all child panels

	m_pViewport->SetProportional( true );

	m_pViewport->SetCursor( m_CursorNone );

	vgui::surface()->SetCursor( m_CursorNone );

	m_pViewport->SetVisible( true );

#ifdef LUA_SDK
	if ( m_pScriptedViewport )
		m_pScriptedViewport->SetVisible( true );

	if ( m_pClientLuaPanel )
		m_pClientLuaPanel->SetVisible( true );
#endif

	if ( m_pViewport->IsKeyBoardInputEnabled() )
	{
		m_pViewport->RequestFocus();
	}

	Layout();
}


void ClientModeShared::Disable()
{
	vgui::VPANEL pRoot = VGui_GetClientDLLRootPanel();;

	// Remove our viewport from the root panel.
	if( pRoot != 0 )
	{
#ifdef LUA_SDK
		m_pScriptedViewport->SetParent( (vgui::VPANEL)NULL );
#endif
		m_pViewport->SetParent( (vgui::VPANEL)NULL );
#ifdef LUA_SDK
		m_pClientLuaPanel->SetParent( (vgui::VPANEL)NULL );
#endif
	}

#ifdef LUA_SDK
	m_pScriptedViewport->SetVisible( false );
#endif
	m_pViewport->SetVisible( false );
#ifdef LUA_SDK
	m_pClientLuaPanel->SetVisible( false );
#endif
}


void ClientModeShared::Layout()
{
	vgui::VPANEL pRoot = VGui_GetClientDLLRootPanel();
	int wide, tall;

	// Make the viewport fill the root panel.
	if( pRoot != 0 )
	{
		vgui::ipanel()->GetSize(pRoot, wide, tall);

		bool changed = wide != m_nRootSize[ 0 ] || tall != m_nRootSize[ 1 ];
		m_nRootSize[ 0 ] = wide;
		m_nRootSize[ 1 ] = tall;

#ifdef LUA_SDK
		// Keep the Lua HUD surface full-screen so "HudViewportPaint" covers the
		// whole viewport.
		if ( m_pScriptedViewport )
			m_pScriptedViewport->SetBounds( 0, 0, wide, tall );
#endif
		m_pViewport->SetBounds(0, 0, wide, tall);
#ifdef LUA_SDK
		// Full-screen so panels created by Lua are laid out against the real
		// viewport instead of a 0x0 parent.
		if ( m_pClientLuaPanel )
			m_pClientLuaPanel->SetBounds( 0, 0, wide, tall );
#endif
		if ( changed )
		{
			ReloadScheme();
		}
	}
}

float ClientModeShared::GetViewModelFOV( void )
{
	// HL2SB (2026-10-07): GMod contract -- the equipped scripted weapon's
	// SWEP.ViewModelFOV overrides the viewmodel_fov convar while it is held
	// (the hl1sweps pack pins 90 for its GoldSrc-style viewmodel pipeline).
	C_BasePlayer *pPlayer = C_BasePlayer::GetLocalPlayer();
	if ( pPlayer != NULL )
	{
		CBaseCombatWeapon *pWep = pPlayer->GetActiveWeapon();
		if ( pWep != NULL && pWep->IsScripted() )
		{
			extern float HL2SB_ScriptedViewModelFOV( CBaseCombatWeapon *pWeapon, float flDefault );
			return HL2SB_ScriptedViewModelFOV( pWep, v_viewmodel_fov.GetFloat() );
		}
	}
	return v_viewmodel_fov.GetFloat();
}

class CHudChat;

bool PlayerNameNotSetYet( const char *pszName )
{
	if ( pszName && pszName[0] )
	{
		// Don't show "unconnected" if we haven't got the players name yet
		if ( Q_strnicmp(pszName,"unconnected",11) == 0 )
			return true;
		if ( Q_strnicmp(pszName,"NULLNAME",11) == 0 )
			return true;
	}

	return false;
}

void ClientModeShared::FireGameEvent( IGameEvent *event )
{
	CBaseHudChat *hudChat = (CBaseHudChat *)GET_HUDELEMENT( CHudChat );

	const char *eventname = event->GetName();

	if ( Q_strcmp( "player_connect", eventname ) == 0 )
	{
		if ( !hudChat )
			return;
		if ( PlayerNameNotSetYet(event->GetString("name")) )
			return;

		if ( !IsInCommentaryMode() )
		{
			wchar_t wszLocalized[100];
			wchar_t wszPlayerName[MAX_PLAYER_NAME_LENGTH];
			g_pVGuiLocalize->ConvertANSIToUnicode( event->GetString("name"), wszPlayerName, sizeof(wszPlayerName) );
			g_pVGuiLocalize->ConstructString( wszLocalized, sizeof( wszLocalized ), g_pVGuiLocalize->Find( "#game_player_joined_game" ), 1, wszPlayerName );

			char szLocalized[100];
			g_pVGuiLocalize->ConvertUnicodeToANSI( wszLocalized, szLocalized, sizeof(szLocalized) );

			hudChat->Printf( CHAT_FILTER_JOINLEAVE, "%s", szLocalized );
		}
	}
	else if ( Q_strcmp( "player_disconnect", eventname ) == 0 )
	{
		C_BasePlayer *pPlayer = USERID2PLAYER( event->GetInt("userid") );

		if ( !hudChat || !pPlayer )
			return;
		if ( PlayerNameNotSetYet(event->GetString("name")) )
			return;

		if ( !IsInCommentaryMode() )
		{
			wchar_t wszPlayerName[MAX_PLAYER_NAME_LENGTH];
			g_pVGuiLocalize->ConvertANSIToUnicode( pPlayer->GetPlayerName(), wszPlayerName, sizeof(wszPlayerName) );

			wchar_t wszReason[64];
			const char *pszReason = event->GetString( "reason" );
			if ( pszReason && ( pszReason[0] == '#' ) && g_pVGuiLocalize->Find( pszReason ) )
			{
				V_wcsncpy( wszReason, g_pVGuiLocalize->Find( pszReason ), sizeof( wszReason ) );
			}
			else
			{
				g_pVGuiLocalize->ConvertANSIToUnicode( pszReason, wszReason, sizeof(wszReason) );
			}

			wchar_t wszLocalized[100];
			if (IsPC())
			{
				g_pVGuiLocalize->ConstructString( wszLocalized, sizeof( wszLocalized ), g_pVGuiLocalize->Find( "#game_player_left_game" ), 2, wszPlayerName, wszReason );
			}
			else
			{
				g_pVGuiLocalize->ConstructString( wszLocalized, sizeof( wszLocalized ), g_pVGuiLocalize->Find( "#game_player_left_game" ), 1, wszPlayerName );
			}

			char szLocalized[100];
			g_pVGuiLocalize->ConvertUnicodeToANSI( wszLocalized, szLocalized, sizeof(szLocalized) );

			hudChat->Printf( CHAT_FILTER_JOINLEAVE, "%s", szLocalized );
		}
	}
	else if ( Q_strcmp( "player_team", eventname ) == 0 )
	{
		C_BasePlayer *pPlayer = USERID2PLAYER( event->GetInt("userid") );
		if ( !hudChat )
			return;

		bool bDisconnected = event->GetBool("disconnect");

		if ( bDisconnected )
			return;

		int team = event->GetInt( "team" );
		bool bAutoTeamed = event->GetInt( "autoteam", false );
		bool bSilent = event->GetInt( "silent", false );

		const char *pszName = event->GetString( "name" );
		if ( PlayerNameNotSetYet( pszName ) )
			return;

		if ( !bSilent )
		{
			wchar_t wszPlayerName[MAX_PLAYER_NAME_LENGTH];
			g_pVGuiLocalize->ConvertANSIToUnicode( pszName, wszPlayerName, sizeof(wszPlayerName) );

			wchar_t wszTeam[64];
			C_Team *pTeam = GetGlobalTeam( team );
			if ( pTeam )
			{
				g_pVGuiLocalize->ConvertANSIToUnicode( pTeam->Get_Name(), wszTeam, sizeof(wszTeam) );
			}
			else
			{
				_snwprintf ( wszTeam, sizeof( wszTeam ) / sizeof( wchar_t ), L"%d", team );
			}

			if ( !IsInCommentaryMode() )
			{
				wchar_t wszLocalized[100];
				if ( bAutoTeamed )
				{
					g_pVGuiLocalize->ConstructString( wszLocalized, sizeof( wszLocalized ), g_pVGuiLocalize->Find( "#game_player_joined_autoteam" ), 2, wszPlayerName, wszTeam );
				}
				else
				{
					g_pVGuiLocalize->ConstructString( wszLocalized, sizeof( wszLocalized ), g_pVGuiLocalize->Find( "#game_player_joined_team" ), 2, wszPlayerName, wszTeam );
				}

				char szLocalized[100];
				g_pVGuiLocalize->ConvertUnicodeToANSI( wszLocalized, szLocalized, sizeof(szLocalized) );

				hudChat->Printf( CHAT_FILTER_TEAMCHANGE, "%s", szLocalized );
			}
		}

		if ( pPlayer && pPlayer->IsLocalPlayer() )
		{
			// that's me
			pPlayer->TeamChange( team );
		}
	}
	else if ( Q_strcmp( "player_changename", eventname ) == 0 )
	{
		if ( !hudChat )
			return;

		const char *pszOldName = event->GetString("oldname");
		if ( PlayerNameNotSetYet(pszOldName) )
			return;

		wchar_t wszOldName[MAX_PLAYER_NAME_LENGTH];
		g_pVGuiLocalize->ConvertANSIToUnicode( pszOldName, wszOldName, sizeof(wszOldName) );

		wchar_t wszNewName[MAX_PLAYER_NAME_LENGTH];
		g_pVGuiLocalize->ConvertANSIToUnicode( event->GetString( "newname" ), wszNewName, sizeof(wszNewName) );

		wchar_t wszLocalized[100];
		g_pVGuiLocalize->ConstructString( wszLocalized, sizeof( wszLocalized ), g_pVGuiLocalize->Find( "#game_player_changed_name" ), 2, wszOldName, wszNewName );

		char szLocalized[100];
		g_pVGuiLocalize->ConvertUnicodeToANSI( wszLocalized, szLocalized, sizeof(szLocalized) );

		hudChat->Printf( CHAT_FILTER_NAMECHANGE, "%s", szLocalized );
	}
	else if (Q_strcmp( "teamplay_broadcast_audio", eventname ) == 0 )
	{
		int team = event->GetInt( "team" );

		bool bValidTeam = false;

		if ( (GetLocalTeam() && GetLocalTeam()->GetTeamNumber() == team) )
		{
			bValidTeam = true;
		}

		//If we're in the spectator team then we should be getting whatever messages the person I'm spectating gets.
		if ( bValidTeam == false )
		{
			CBasePlayer *pSpectatorTarget = UTIL_PlayerByIndex( GetSpectatorTarget() );

			if ( pSpectatorTarget && (GetSpectatorMode() == OBS_MODE_IN_EYE || GetSpectatorMode() == OBS_MODE_CHASE) )
			{
				if ( pSpectatorTarget->GetTeamNumber() == team )
				{
					bValidTeam = true;
				}
			}
		}

		if ( team == 0 && GetLocalTeam() )
		{
			bValidTeam = false;
		}

		if ( team == 255 )
		{
			bValidTeam = true;
		}

		if ( bValidTeam == true )
		{
			EmitSound_t et;
			et.m_pSoundName = event->GetString("sound");
			et.m_nFlags = event->GetInt("additional_flags");

			CLocalPlayerFilter filter;
			C_BaseEntity::EmitSound( filter, SOUND_FROM_LOCAL_PLAYER, et );
		}
	}
	else if ( Q_strcmp( "server_cvar", eventname ) == 0 )
	{
		if ( !IsInCommentaryMode() )
		{
			wchar_t wszCvarName[64];
			g_pVGuiLocalize->ConvertANSIToUnicode( event->GetString("cvarname"), wszCvarName, sizeof(wszCvarName) );

			wchar_t wszCvarValue[64];
			g_pVGuiLocalize->ConvertANSIToUnicode( event->GetString("cvarvalue"), wszCvarValue, sizeof(wszCvarValue) );

			wchar_t wszLocalized[256];
			g_pVGuiLocalize->ConstructString( wszLocalized, sizeof( wszLocalized ), g_pVGuiLocalize->Find( "#game_server_cvar_changed" ), 2, wszCvarName, wszCvarValue );

			char szLocalized[256];
			g_pVGuiLocalize->ConvertUnicodeToANSI( wszLocalized, szLocalized, sizeof(szLocalized) );

			hudChat->Printf( CHAT_FILTER_SERVERMSG, "%s", szLocalized );
		}
	}
	else if ( Q_strcmp( "achievement_earned", eventname ) == 0 )
	{
		int iPlayerIndex = event->GetInt( "player" );
		C_BasePlayer *pPlayer = UTIL_PlayerByIndex( iPlayerIndex );
		int iAchievement = event->GetInt( "achievement" );

		if ( !hudChat || !pPlayer )
			return;

		if ( !IsInCommentaryMode() )
		{
			CAchievementMgr *pAchievementMgr = dynamic_cast<CAchievementMgr *>( engine->GetAchievementMgr() );
			if ( !pAchievementMgr )
				return;

			IAchievement *pAchievement = pAchievementMgr->GetAchievementByID( iAchievement );
			if ( pAchievement )
			{
				if ( !pPlayer->IsDormant() && pPlayer->ShouldAnnounceAchievement() )
				{
					pPlayer->SetNextAchievementAnnounceTime( gpGlobals->curtime + ACHIEVEMENT_ANNOUNCEMENT_MIN_TIME );

					// no particle effect if the local player is the one with the achievement or the player is dead
					if ( !pPlayer->IsLocalPlayer() && pPlayer->IsAlive() ) 
					{
						//tagES using the "head" attachment won't work for CS and DoD
						pPlayer->ParticleProp()->Create( "achieved", PATTACH_POINT_FOLLOW, "head" );
					}

					pPlayer->OnAchievementAchieved( iAchievement );
				}

				if ( g_PR )
				{
					wchar_t wszPlayerName[MAX_PLAYER_NAME_LENGTH];
					g_pVGuiLocalize->ConvertANSIToUnicode( g_PR->GetPlayerName( iPlayerIndex ), wszPlayerName, sizeof( wszPlayerName ) );

					const wchar_t *pchLocalizedAchievement = ACHIEVEMENT_LOCALIZED_NAME_FROM_STR( pAchievement->GetName() );
					if ( pchLocalizedAchievement )
					{
						wchar_t wszLocalizedString[128];
						g_pVGuiLocalize->ConstructString( wszLocalizedString, sizeof( wszLocalizedString ), g_pVGuiLocalize->Find( "#Achievement_Earned" ), 2, wszPlayerName, pchLocalizedAchievement );

						char szLocalized[128];
						g_pVGuiLocalize->ConvertUnicodeToANSI( wszLocalizedString, szLocalized, sizeof( szLocalized ) );

						hudChat->ChatPrintf( iPlayerIndex, CHAT_FILTER_SERVERMSG, "%s", szLocalized );
					}
				}
			}
		}
	}
#if defined( TF_CLIENT_DLL )
	else if ( Q_strcmp( "item_found", eventname ) == 0 )
	{
		int iPlayerIndex = event->GetInt( "player" );
		entityquality_t iItemQuality = event->GetInt( "quality" );
		int iMethod = event->GetInt( "method" );
		int iItemDef = event->GetInt( "itemdef" );
		C_BasePlayer *pPlayer = UTIL_PlayerByIndex( iPlayerIndex );
		const GameItemDefinition_t *pItemDefinition = dynamic_cast<GameItemDefinition_t *>( GetItemSchema()->GetItemDefinition( iItemDef ) );

		if ( !pPlayer || !pItemDefinition )
			return;

		if ( g_PR )
		{
			wchar_t wszPlayerName[MAX_PLAYER_NAME_LENGTH];
			g_pVGuiLocalize->ConvertANSIToUnicode( g_PR->GetPlayerName( iPlayerIndex ), wszPlayerName, sizeof( wszPlayerName ) );

			if ( iMethod < 0 || iMethod >= ARRAYSIZE( g_pszItemFoundMethodStrings ) )
			{
				iMethod = 0;
			}

			const char *pszLocString = g_pszItemFoundMethodStrings[iMethod];
			if ( pszLocString )
			{
				wchar_t wszItemFound[256];
				_snwprintf( wszItemFound, ARRAYSIZE( wszItemFound ), L"%ls", g_pVGuiLocalize->Find( pszLocString ) );

				wchar_t *colorMarker = wcsstr( wszItemFound, L"::" );
				if ( colorMarker )
				{
					const char *pszQualityColorString = EconQuality_GetColorString( (EEconItemQuality)iItemQuality );
					if ( pszQualityColorString )
					{
						hudChat->SetCustomColor( pszQualityColorString );
						*(colorMarker+1) = COLOR_CUSTOM;
					}
				}

				// TODO: Update the localization strings to only have two format parameters since that's all we need.
				wchar_t wszLocalizedString[256];
				g_pVGuiLocalize->ConstructString( wszLocalizedString, sizeof( wszLocalizedString ), wszItemFound, 3, wszPlayerName, CEconItemLocalizedFullNameGenerator( GLocalizationProvider(), pItemDefinition, iItemQuality ).GetFullName(), L"" );

				char szLocalized[256];
				g_pVGuiLocalize->ConvertUnicodeToANSI( wszLocalizedString, szLocalized, sizeof( szLocalized ) );

				hudChat->ChatPrintf( iPlayerIndex, CHAT_FILTER_SERVERMSG, "%s", szLocalized );
			}
		}		
	}
#endif
#if defined( REPLAY_ENABLED )
	else if ( !V_strcmp( "replay_servererror", eventname ) )
	{
		DisplayReplayMessage( event->GetString( "error", "#Replay_DefaultServerError" ), replay_msgduration_error.GetFloat(), true, NULL, false );
	}
	else if ( !V_strcmp( "replay_startrecord", eventname ) )
	{
		m_flReplayStartRecordTime = gpGlobals->curtime;
	}
	else if ( !V_strcmp( "replay_endrecord", eventname ) )
	{
		m_flReplayStopRecordTime = gpGlobals->curtime;
	}
	else if ( !V_strcmp( "replay_replaysavailable", eventname ) )
	{
		DisplayReplayMessage( "#Replay_ReplaysAvailable", replay_msgduration_replaysavailable.GetFloat(), false, NULL, false );
	}

	else if ( !V_strcmp( "game_newmap", eventname ) )
	{
		// Make sure the instance count is reset to 0.  Sometimes the count stay in sync and we get replay messages displaying lower than they should.
		CReplayMessagePanel::RemoveAll();
	}
#endif

	else
	{
		DevMsg( 2, "Unhandled GameEvent in ClientModeShared::FireGameEvent - %s\n", event->GetName()  );
	}
}

void ClientModeShared::UpdateReplayMessages()
{
#if defined( REPLAY_ENABLED )
	// Received a replay_startrecord event?
	if ( m_flReplayStartRecordTime != 0.0f )
	{
		DisplayReplayMessage( "#Replay_StartRecord", replay_msgduration_startrecord.GetFloat(), true, "replay\\startrecord.mp3", false );

		m_flReplayStartRecordTime = 0.0f;
		m_flReplayStopRecordTime = 0.0f;
	}

	// Received a replay_endrecord event?
	if ( m_flReplayStopRecordTime != 0.0f )
	{
		DisplayReplayMessage( "#Replay_EndRecord", replay_msgduration_stoprecord.GetFloat(), true, "replay\\stoprecord.wav", false );

		// Hide the replay reminder
		if ( m_pReplayReminderPanel )
		{
			m_pReplayReminderPanel->Hide();
		}

		m_flReplayStopRecordTime = 0.0f;
	}

	if ( !engine->IsConnected() )
	{
		ClearReplayMessageList();
	}
#endif
}

void ClientModeShared::ClearReplayMessageList()
{
#if defined( REPLAY_ENABLED )
	CReplayMessagePanel::RemoveAll();
#endif
}

void ClientModeShared::DisplayReplayMessage( const char *pLocalizeName, float flDuration, bool bUrgent,
											 const char *pSound, bool bDlg )
{
#if defined( REPLAY_ENABLED )
	// Don't display during replay playback, and don't allow more than 4 at a time
	const bool bInReplay = g_pEngineClientReplay->IsPlayingReplayDemo();
	if ( bInReplay || ( !bDlg && CReplayMessagePanel::InstanceCount() >= 4 ) )
		return;

	// Use default duration?
	if ( flDuration == -1.0f )
	{
		flDuration = replay_msgduration_misc.GetFloat();
	}

	// Display a replay message
	if ( bDlg )
	{
		if ( engine->IsInGame() )
		{
			Panel *pPanel = new CReplayMessageDlg( pLocalizeName );
			pPanel->SetVisible( true );
			pPanel->MakePopup();
			pPanel->MoveToFront();
			pPanel->SetKeyBoardInputEnabled( true );
			pPanel->SetMouseInputEnabled( true );
#if defined( TF_CLIENT_DLL )
			TFModalStack()->PushModal( pPanel );
#endif
		}
		else
		{
			ShowMessageBox( "#Replay_GenericMsgTitle", pLocalizeName, "#GameUI_OK" );
		}
	}
	else
	{
		CReplayMessagePanel *pMsgPanel = new CReplayMessagePanel( pLocalizeName, flDuration, bUrgent );
		pMsgPanel->Show();
	}

	// Play a sound if appropriate
	if ( pSound )
	{
		surface()->PlaySound( pSound );
	}
#endif
}

void ClientModeShared::DisplayReplayReminder()
{
#if defined( REPLAY_ENABLED )
	if ( m_pReplayReminderPanel && g_pReplay->IsRecording() )
	{
		// Only display the panel if we haven't already requested a replay for the given life
		CReplay *pCurLifeReplay = static_cast< CReplay * >( g_pClientReplayContext->GetReplayManager()->GetReplayForCurrentLife() );
		if ( pCurLifeReplay && !pCurLifeReplay->m_bRequestedByUser && !pCurLifeReplay->m_bSaved )
		{
			m_pReplayReminderPanel->Show();
		}
	}
#endif
}


//-----------------------------------------------------------------------------
// In-game VGUI context 
//-----------------------------------------------------------------------------
void ClientModeShared::ActivateInGameVGuiContext( vgui::Panel *pPanel )
{
	vgui::ivgui()->AssociatePanelWithContext( s_hVGuiContext, pPanel->GetVPanel() );
	vgui::ivgui()->ActivateContext( s_hVGuiContext );
}

void ClientModeShared::DeactivateInGameVGuiContext()
{
	vgui::ivgui()->ActivateContext( DEFAULT_VGUI_CONTEXT );
}

