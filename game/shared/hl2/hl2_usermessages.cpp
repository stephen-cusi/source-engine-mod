//========= Copyright Valve Corporation, All rights reserved. ============//
//
// Purpose: 
//
//=============================================================================//

#include "cbase.h"
#include "usermessages.h"
#include "shake.h"
#include "voice_gamemgr.h"

// NVNT include to register in haptic user messages
#include "haptics/haptic_msgs.h"

// memdbgon must be the last include file in a .cpp file!!!
#include "tier0/memdbgon.h"

void RegisterUserMessages( void )
{
	usermessages->Register( "Geiger", 1 );
	usermessages->Register( "Train", 1 );
	usermessages->Register( "HudText", -1 );
	usermessages->Register( "SayText", -1 );
	usermessages->Register( "SayText2", -1 );
	usermessages->Register( "TextMsg", -1 );
	usermessages->Register( "HudMsg", -1 );
	usermessages->Register( "ResetHUD", 1);		// called every respawn
	usermessages->Register( "GameTitle", 0 );
	usermessages->Register( "ItemPickup", -1 );
	usermessages->Register( "ShowMenu", -1 );
	usermessages->Register( "Shake", 13 );
	usermessages->Register( "Fade", 10 );
	usermessages->Register( "VGUIMenu", -1 );	// Show VGUI menu
	usermessages->Register( "Rumble", 3 );	// Send a rumble to a controller
	usermessages->Register( "Battery", 2 );
	usermessages->Register( "Damage", 18 );		// BUG: floats are sent for coords, no variable bitfields in hud & fixed size Msg
	usermessages->Register( "VoiceMask", VOICE_MAX_PLAYERS_DW*4 * 2 + 1 );
	usermessages->Register( "RequestState", 0 );
	usermessages->Register( "CloseCaption", -1 ); // Show a caption (by string id number)(duration in 10th of a second)

	usermessages->Register("SendAudio", -1);	// play radio command
	usermessages->Register("RawAudio", -1);	// play a .wav as a radio command

	usermessages->Register( "HintText", -1 );	// Displays hint text display
	usermessages->Register( "KeyHintText", -1 );	// Displays hint text display
	usermessages->Register( "SquadMemberDied", 0 );
	usermessages->Register( "AmmoDenied", 2 );
	usermessages->Register( "CreditsMsg", 1 );
	usermessages->Register( "LogoTimeMsg", 4 );
	usermessages->Register( "AchievementEvent", -1 );
	usermessages->Register( "UpdateJalopyRadar", -1 );
	usermessages->Register( "EntityPortalled", sizeof( long ) + sizeof( long ) + sizeof( Vector ) + sizeof( QAngle ) ); //something got teleported through a portal

	usermessages->Register("BarTime", -1);	// For the C4 progress bar.
	usermessages->Register("RadioText", -1);		// for radio text display

	usermessages->Register("ReloadEffect", 2);			// a player reloading..
	usermessages->Register("PlayerAnimEvent", -1);	// jumping, firing, reload, etc.

	usermessages->Register("UpdateRadar", -1);
	usermessages->Register("KillCam", -1);
	usermessages->Register("MarkAchievement", -1);

	// Voting
	usermessages->Register("CallVoteFailed", -1);
	usermessages->Register("VoteStart", -1);
	usermessages->Register("VotePass", -1);
	usermessages->Register("VoteFailed", 2);
	usermessages->Register("VoteSetup", -1);  // Initiates client-side voting UI

#ifndef _X360
	// NVNT register haptic user messages
	RegisterHapticMessages();
#endif
}