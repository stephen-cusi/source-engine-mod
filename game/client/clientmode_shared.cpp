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

// HL2SB: GMod-style vehicle third person.
//
// GMod never uses the engine's third-person camera for vehicles. The vehicle owns the
// flag - Vehicle:GetThirdPersonMode / SetThirdPersonMode, toggled from the usercmd's
// IN_DUCK by GM:VehicleMove (gamemodes/base/gamemode/init.lua) - and the camera is built
// from it in GM:CalcVehicleView (gamemodes/base/gamemode/cl_init.lua) /
// CalcView_ThirdPerson (lua/drive/drive_base.lua): distance from the vehicle's render
// bounds, a trace filtered to ignore props/vehicles so the car cannot block its own
// camera, and view.drawviewer to render the driver.
//
// Applied in OverrideView, on top of the final CViewSetup, for two reasons: pSetup is
// the last word on the view (the engine's own third-person code cannot undo it), and the
// engine's third-person path is unusable for vehicles here - the desired camera offset is
// never set anywhere in this tree except CInput::CAM_ToFirstPerson(), which zeroes it
// (in_camera.cpp:699), so that offset is always zero.
ConVar hl2sb_veh_thirdperson( "hl2sb_veh_thirdperson", "0", FCVAR_ARCHIVE,
							  "GMod-style third person camera while inside a vehicle." );
// 0 keeps GMod's own framing: the camera distance IS the vehicle's render-bounds radius,
// recomputed every frame from the vehicle being ridden (GMod:
// gamemodes/base/gamemode/cl_init.lua:320 `local radius = (mn - mx):Length()`), so a
// chair gets a close camera and a jeep a far one with no magic number.  Anything else is
// an absolute distance in units - that is what the mouse wheel and hl2sb_veh_zoom write,
// and it is deliberately NOT archived so a stale saved value cannot pin the camera.
ConVar hl2sb_veh_thirdperson_dist( "hl2sb_veh_thirdperson_dist", "0", 0,
								   "Distance of the vehicle third person camera. 0 = GMod's own framing "
								   "(the vehicle's render-bounds radius); non-zero = that many units." );
// GMod itself has no vertical offset: the camera sits at eye height, which is already
// above the vehicle.  Kept as a convar so the old value is still reachable.
ConVar hl2sb_veh_thirdperson_up( "hl2sb_veh_thirdperson_up", "0", 0,
								 "Height added on top of the GMod-style vehicle third person camera (GMod uses 0)." );
ConVar hl2sb_veh_thirdperson_debug( "hl2sb_veh_thirdperson_debug", "0", FCVAR_ARCHIVE,
								    "Print the vehicle third person camera trace results." );

static bool HL2SB_VehicleThirdPersonActive( C_BasePlayer *pPlayer )
{
	return pPlayer != NULL && hl2sb_veh_thirdperson.GetBool() && pPlayer->GetVehicle() != NULL;
}

static void HL2SB_VehThirdPersonToggle_f( void )
{
	hl2sb_veh_thirdperson.SetValue( hl2sb_veh_thirdperson.GetBool() ? 0 : 1 );
	Msg( "[HL2SB] vehicle third person = %d (distance %.0f)\n",
		 hl2sb_veh_thirdperson.GetBool() ? 1 : 0, hl2sb_veh_thirdperson_dist.GetFloat() );
}

static ConCommand hl2sb_veh_thirdperson_toggle( "hl2sb_veh_thirdperson_toggle",
											    HL2SB_VehThirdPersonToggle_f,
											    "Toggle the GMod-style vehicle third person camera." );

// HL2SB: the distance the vehicle third person camera really uses.
//
// GMod (GM:CalcVehicleView, gamemodes/base/gamemode/cl_init.lua:320):
//     local mn, mx = Vehicle:GetRenderBounds()
//     local radius = ( mn - mx ):Length()
// so the camera clears whatever the vehicle is BY THE VEHICLE'S OWN SIZE - a chair gets a
// close camera and a jeep a far one, with no magic number anywhere.
//
// hl2sb_veh_thirdperson_dist == 0 keeps exactly that.  Anything else is an absolute
// distance in units, which is what the mouse wheel / hl2sb_veh_zoom write (they READ this
// function first, so the first notch continues from the automatic distance instead of
// jumping to it).
#define HL2SB_VEH3RD_DEFAULT_DIST	480.0f
#define HL2SB_VEH3RD_MIN_DIST		80.0f
#define HL2SB_VEH3RD_MAX_DIST		2000.0f
// One mouse wheel notch / one `hl2sb_veh_zoom 40`.
#define HL2SB_VEH3RD_WHEEL_STEP		40.0f

static float HL2SB_VehicleCameraDistance( C_BasePlayer *pPlayer )
{
	const float flConfigured = hl2sb_veh_thirdperson_dist.GetFloat();
	if ( flConfigured > 0.0f )
	{
		return clamp( flConfigured, HL2SB_VEH3RD_MIN_DIST, HL2SB_VEH3RD_MAX_DIST );
	}

	// GMod's radius, from the vehicle ENTITY.  C_BasePlayer::GetVehicleEntity() is the
	// entity the server sends in m_hVehicle (RecvPropEHandle, c_baseplayer.cpp:278); it
	// is the pod/jeep itself, so its render bounds are the vehicle's.
	C_BaseEntity *pVehicleEnt = ( pPlayer != NULL ) ? pPlayer->GetVehicleEntity() : NULL;
	if ( pVehicleEnt != NULL )
	{
		Vector vecMin, vecMax;
		pVehicleEnt->GetRenderBounds( vecMin, vecMax );

		const float flRadius = ( vecMax - vecMin ).Length();

		// A model that has not streamed in, or a degenerate bounds, must not put the
		// camera inside the vehicle.
		if ( flRadius >= 16.0f && flRadius <= HL2SB_VEH3RD_MAX_DIST )
		{
			return flRadius;
		}
	}

	return HL2SB_VEH3RD_DEFAULT_DIST;
}

// GMod zooms the vehicle camera with the mouse wheel, which is
// Vehicle:SetCameraDistance() - HL2SB's seats/vehicles expose that through the Lua API,
// and this is the engine side of it:
//   console: hl2sb_veh_zoom <delta>
//   mouse:   taken in ClientModeShared::KeyInput() (no bind needed - see there)
static float HL2SB_VehThirdPersonZoom( C_BasePlayer *pPlayer, float flDelta )
{
	const float flCurrent = HL2SB_VehicleCameraDistance( pPlayer );
	const float flNew = clamp( flCurrent + flDelta, HL2SB_VEH3RD_MIN_DIST, HL2SB_VEH3RD_MAX_DIST );

	hl2sb_veh_thirdperson_dist.SetValue( flNew );

	Msg( "[HL2SB] vehicle third person distance = %.0f\n", flNew );
	return flNew;
}

static void HL2SB_VehThirdPersonZoom_f( const CCommand &args )
{
	const int iDelta = ( args.ArgC() > 1 ) ? atoi( args.Arg( 1 ) ) : (int)HL2SB_VEH3RD_WHEEL_STEP;

	HL2SB_VehThirdPersonZoom( C_BasePlayer::GetLocalPlayer(), (float)iDelta );
}

static ConCommand hl2sb_veh_zoom( "hl2sb_veh_zoom", HL2SB_VehThirdPersonZoom_f,
							      "Zoom the GMod-style vehicle third person camera by <delta> units." );

// HL2SB: prove the mouse wheel reached ClientModeShared::KeyInput - and say what it was
// allowed to do there.
//
// This exists because "the wheel zoom does nothing" is not diagnosable from a screenshot:
// the wheel can be lost in three different places before the camera is ever consulted (the
// engine's vgui filter, a Lua `KeyInput` hook, or the third-person camera simply being
// off), and all three look identical on screen. So one line is printed for EVERY wheel
// event that gets this far, whether or not anything is done with it, and the flags name
// which of those cases it is:
//
//   [HL2SB veh3rd/wheel] down=1 MOUSE_WHEEL_UP inVehicle=1 camera=1 third=1 dist=0
//
//     inVehicle=0  the wheel got here but we are on foot - not a camera problem
//     inVehicle=1 camera=0  seated, but hl2sb_veh_thirdperson is 0 (nothing zooms by
//                  design); press Ctrl in the vehicle, or `hl2sb_veh_thirdperson 1`
//     inVehicle=1 camera=1  the wheel IS being consumed as a zoom: the following
//                  "[HL2SB] vehicle third person distance = N" line is the new distance
//
// Throttled to one line a second (and the very first few always print) so a scroll wheel
// spun in the console cannot flood the log. Behind hl2sb_veh_thirdperson_debug like the
// rest of the vehicle camera logging.
static void HL2SB_VehicleWheelDebug( bool bDown, ButtonCode_t keynum, bool bInVehicle, bool bCameraOn )
{
	if ( !hl2sb_veh_thirdperson_debug.GetBool() )
	{
		return;
	}

	static float s_flNextPrint = 0.0f;
	static int s_iSeen = 0;

	++s_iSeen;

	// Always print the first few, then at most one a second.
	if ( s_iSeen > 4 && gpGlobals->curtime < s_flNextPrint )
	{
		return;
	}

	s_flNextPrint = gpGlobals->curtime + 1.0f;

	Msg( "[HL2SB veh3rd/wheel] down=%d %s inVehicle=%d camera=%d third=%d dist=%.0f\n",
		 bDown ? 1 : 0,
		 ( keynum == MOUSE_WHEEL_UP ) ? "MOUSE_WHEEL_UP" : "MOUSE_WHEEL_DOWN",
		 bInVehicle ? 1 : 0, bCameraOn ? 1 : 0, hl2sb_veh_thirdperson.GetBool() ? 1 : 0,
		 hl2sb_veh_thirdperson_dist.GetFloat() );
}

// HL2SB: shared debug for the vehicle third person camera. Called twice per frame -
// once where the camera is computed and once on the final view that gets rendered - so a
// later stage overwriting the camera can be seen directly. pszDetail is the calc-side
// geometry (vehicle, distance, trace fraction) and is NULL for the final call.
//
// HL2SB: THROTTLED per call site. The old change-detection could not work here: the two
// call sites alternate (calc / final / calc / final ...) and each frame's values are
// never bit-identical, so every call looked like a change and the log was ~6 lines per
// frame at 66 tick (engine.log 2026-09-16 0:37: 191.0s-193.4s is nothing but this line).
// One print per second per call site keeps the values (eye, distance, trace fraction)
// readable while driving and cuts the volume by two orders of magnitude.
void HL2SB_DebugVehicleCamera( const char *pszWhere, const Vector &vecOrigin, const QAngle &angView,
							   const char *pszDetail )
{
	if ( !hl2sb_veh_thirdperson_debug.GetBool() )
	{
		return;
	}

	// Four call sites exist today: OverrideView ("calc"), the second calc from
	// MP_PostSimulate, SetUpViews ("final") and KeyInput ("wheel").
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

// HL2SB: GMod's GM:CalcVehicleView / CalcView_ThirdPerson, as a function of a BARE
// vehicle EYE position.
//
// GMod's own code (gamemodes/base/gamemode/cl_init.lua:305-351):
//   local radius = ( mn - mx ):Length()
//   local radius = radius + radius * Vehicle:GetCameraDistance()
//   local TargetOrigin = view.origin + ( view.angles:Forward() * -radius )
//   trace a 4x4x4 hull from view.origin to TargetOrigin, filtering props/vehicles
//   view.origin = tr.HitPos
//   if ( tr.Hit && !tr.StartSolid ) then view.origin = view.origin + tr.HitNormal * 4 end
//   view.angles is NOT touched - it stays the PLAYER's own view angles
//
// Two things follow from that and both matter here:
//
//  * The pull-back is along the FULL view direction - pitch included - and the pitch is
//    NEVER clamped. GMod does not re-aim the camera; it moves it behind the player along
//    the direction the player already looks.
//
//  * The distance is the VEHICLE's own render-bounds radius, not a constant, so the whole
//    vehicle is in frame without a magic number (see HL2SB_VehicleCameraDistance).
//
// WHY THIS IS A SHARED FUNCTION AND NOT INLINE IN OverrideView:
//
// The vehicle eye position is computed TWICE per frame in this tree - once by
// C_BasePlayer::CalcView (what OverrideView sees) and again at the very end of
// CViewRender::SetUpViews() by CViewRender::MP_PostSimulate(), which recomputes the
// vehicle view with a freshly invalidated bone cache and writes it straight back into
// m_View (view.cpp:1346). That second write happens AFTER OverrideView and therefore used
// to throw this camera away completely: the rendered view was the bare pod eye and
// nothing about the third person camera - neither this distance nor the mouse wheel that
// writes it - could be seen on screen. view.cpp now calls back into this function with the
// FRESH eye so the offset is applied exactly once, on top of the correct eye.
bool HL2SB_ApplyVehicleThirdPersonView( const Vector &vecEyeOrigin, const QAngle &angEyeAngles,
										Vector *pOutOrigin )
{
	C_BasePlayer *pPlayer = C_BasePlayer::GetLocalPlayer();
	if ( !HL2SB_VehicleThirdPersonActive( pPlayer ) )
	{
		return false;
	}

	const float flDist = HL2SB_VehicleCameraDistance( pPlayer );

	Vector vecForward;
	AngleVectors( angEyeAngles, &vecForward );

	const Vector vecTarget = vecEyeOrigin - vecForward * flDist;
	const Vector vecHull( 4.0f, 4.0f, 4.0f );

	trace_t tr;
	// GMod's filter drops props and vehicles so the camera never collides with the car it
	// rides in; MASK_SOLID_BRUSHONLY is the engine equivalent (world and brush geometry
	// only). A trace that starts solid is ignored entirely: at a driver's seat that is the
	// car's own surroundings, and collapsing the camera there is what used to leave the
	// view inside the cabin.
	UTIL_TraceHull( vecEyeOrigin, vecTarget, -vecHull, vecHull, MASK_SOLID_BRUSHONLY,
					pPlayer, COLLISION_GROUP_NONE, &tr );

	float flUsed = flDist;
	Vector vecCamera = vecTarget;

	if ( !tr.startsolid )
	{
		flUsed = flDist * tr.fraction;
		vecCamera = vecEyeOrigin - vecForward * flUsed;

		// GMod's WallOffset (4): push the camera off the wall it stopped against.
		if ( tr.fraction < 1.0f )
		{
			vecCamera += tr.plane.normal * 4.0f;
		}
	}

	vecCamera += Vector( 0.0f, 0.0f, hl2sb_veh_thirdperson_up.GetFloat() );

	// C_BaseEntity::GetClassname() hands back ONE shared static buffer on the client
	// (c_baseentity.cpp:4813), so two calls inside a single Msg() print the same name -
	// that is where the old "vehicle=player" on the client came from. Copy it out first.
	char szVehicle[128];
	C_BaseEntity *pVehicleEnt = pPlayer->GetVehicleEntity();
	Q_strncpy( szVehicle, pVehicleEnt ? pVehicleEnt->GetClassname() : "<none>", sizeof( szVehicle ) );

	char szDetail[288];
	Q_snprintf( szDetail, sizeof( szDetail ),
				"eye=(%.1f %.1f %.1f) veh=%s dist=%.0f used=%.0f frac=%.3f ss=%d up=%.0f",
				vecEyeOrigin.x, vecEyeOrigin.y, vecEyeOrigin.z, szVehicle, flDist, flUsed,
				tr.fraction, tr.startsolid ? 1 : 0, hl2sb_veh_thirdperson_up.GetFloat() );

	HL2SB_DebugVehicleCamera( "calc", vecCamera, angEyeAngles, szDetail );

	if ( pOutOrigin )
	{
		*pOutOrigin = vecCamera;
	}

	return true;
}

//-----------------------------------------------------------------------------
// Purpose: 
// Input  : *pSetup - 
//-----------------------------------------------------------------------------
void ClientModeShared::OverrideView( CViewSetup *pSetup )
{
	QAngle camAngles;

	// Let the player override the view.
	C_BasePlayer *pPlayer = C_BasePlayer::GetLocalPlayer();
	if(!pPlayer)
		return;

	pPlayer->OverrideView( pSetup );

	// HL2SB: the true eye position, before any third-person offset is applied. The
	// GMod-style vehicle camera below is built from this, never from an already offset
	// view, so the two systems cannot stack.
	const Vector vecEyeOrigin = pSetup->origin;

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

	// HL2SB: GMod's GM:CalcVehicleView / CalcView_ThirdPerson, applied last so it wins
	// over the engine's own offset.
	//
	// The forced 38-degree-down pitch that started all of this is fixed at its root in the
	// pod itself, with GMod's own `limitview 0` key - see
	// C_PropVehiclePrisonerPod::UpdateViewAngles in game/client/hl2/c_vehicle_prisoner_pod.cpp.
	//
	// NOTE: this is NOT the last write to the view. CViewRender::MP_PostSimulate() runs at
	// the end of SetUpViews() (view.cpp:832) and recomputes the vehicle eye into m_View
	// after this returns; it calls HL2SB_ApplyVehicleThirdPersonView() again with the fresh
	// eye, which is why that work lives in a function instead of here. Read the comment on
	// it before changing this.
	// vecEyeOrigin was captured before the engine's third-person block above, so the two
	// systems can never stack.
	Vector vecVehThirdPerson;
	if ( HL2SB_ApplyVehicleThirdPersonView( vecEyeOrigin, pSetup->angles, &vecVehThirdPerson ) )
	{
		pSetup->origin = vecVehThirdPerson;
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
}

//-----------------------------------------------------------------------------
// Purpose: 
//-----------------------------------------------------------------------------
void ClientModeShared::PostRender()
{
	// Let the particle manager simulate things that haven't been simulated.
	ParticleMgr()->PostRender();
}

void ClientModeShared::PostRenderVGui()
{
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
	// HL2SB: the mouse wheel zooms the vehicle third person camera, with no bind of the
	// user's own.
	//
	// GMod gets the wheel out of the usercmd (GM:VehicleMove:
	// `ply:GetCurrentCommand():GetMouseWheel()` -> Vehicle:SetCameraDistance) and HL2SB
	// has no CMoveData in Lua, so it is taken here instead, using the engine's own
	// contract for this function: "Return 1 to allow engine to process the key, otherwise,
	// act on it as needed" (CInput::KeyEvent, game/client/in_main.cpp:555).
	//
	// Returning 0 is exactly what makes "no bind" work: engine/keys.cpp:756 hands every
	// key event to the client DLL first and Key_Event() returns without dispatching the
	// key binding when the client consumes it, so the stock MWHEELUP/MWHEELDOWN binding
	// (invprev / invnext) never fires while the vehicle camera is up. Outside the vehicle
	// camera this does nothing at all and the wheel keeps switching weapons.
	//
	// IT HAS TO BE THE FIRST THING IN THIS FUNCTION. It used to sit below the Lua
	// `KeyInput` hook block, and that block ends in RETURN_LUA_INTEGER()
	// (game/shared/lua/luamanager.h:544), which returns from here as soon as a Lua hook
	// left a NUMBER on the stack. Any hook or GM:KeyInput that ever returns a number -
	// GMod's own KeyInput contract is boolean, but addons are not bound by it - would
	// therefore have made the wheel dead with no error anywhere. Nothing may sit in front
	// of this except the console guard below.
	//
	// The console keeps the wheel while it is open (that is the SDK behaviour, and the
	// `engine->Con_IsVisible()` early return further down cannot be used to enforce it
	// from up here without also moving the Lua hook behind the console - a behaviour change
	// this fix has no business making), so the guard is repeated in the condition.
	//
	// The engine's own proof that the wheel reaches this function is stock Valve code:
	// C_WeaponGravityGun::KeyInput (game/client/hl2/c_weapon_gravitygun.cpp:62) consumes
	// MOUSE_WHEEL_UP/DOWN and returns 0 on the very same path (ClientModeShared::KeyInput
	// -> pWeapon->KeyInput). The wheel is posted as IE_ButtonPressed with
	// code = MOUSE_WHEEL_UP|DOWN by CInputSystem::WindowProc (inputsystem.cpp:1446-1456)
	// and Key_Event() routes it to IN_KeyEvent -> CInput::KeyEvent -> here
	// (engine/keys.cpp:583, game/client/in_main.cpp:567).
	if ( ( keynum == MOUSE_WHEEL_UP || keynum == MOUSE_WHEEL_DOWN ) && !engine->Con_IsVisible() )
	{
		C_BasePlayer *pWheelPlayer = C_BasePlayer::GetLocalPlayer();
		const bool bInVehicle = ( pWheelPlayer != NULL && pWheelPlayer->GetVehicle() != NULL );
		const bool bCameraOn = HL2SB_VehicleThirdPersonActive( pWheelPlayer );

		// Printed for EVERY wheel event, camera or not, so the log answers "did the wheel
		// get here at all?" without the user having to change anything else.
		HL2SB_VehicleWheelDebug( down, keynum, bInVehicle, bCameraOn );

		if ( down && bCameraOn )
		{
			HL2SB_VehThirdPersonZoom( pWheelPlayer,
									  ( keynum == MOUSE_WHEEL_UP ) ? HL2SB_VEH3RD_WHEEL_STEP
																   : -HL2SB_VEH3RD_WHEEL_STEP );
			return 0;
		}
	}

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

