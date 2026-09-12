#include "cbase.h"
#include "const.h"
#include "luamanager.h"
#include "luasrclib.h"
#include "Multiplayer/multiplayer_animstate.h"	// HL2SB: header lives under game/shared/Multiplayer
#include "filesystem.h"
#include "ienginevgui.h"
#include "inputsystem/ButtonCode.h"
#include <activitylist.h>
#include "hl2_shareddefs.h"
#include "decals.h"
// HL2SB: the DOCK_TYPE enum below publishes vgui::Panel's DOCK values, so it
// cannot drift from the enum the dock pass switches on.
#include <vgui_controls/Panel.h>

// Quick and easy place to register some enums
LUALIB_API int luaopen_SharedEnumerations( lua_State *L )
{
    LUA_SET_ENUM_LIB_BEGIN( L, "SHAKE_COMMAND" );
    lua_pushenum( L, SHAKE_START, "START" );
    lua_pushenum( L, SHAKE_STOP, "STOP" );
    lua_pushenum( L, SHAKE_AMPLITUDE, "AMPLITUDE" );
    lua_pushenum( L, SHAKE_FREQUENCY, "FREQUENCY" );
    lua_pushenum( L, SHAKE_START_RUMBLEONLY, "START_RUMBLE_ONLY" );
    lua_pushenum( L, SHAKE_START_NORUMBLE, "START_NO_RUMBLE" );
    LUA_SET_ENUM_LIB_END( L );

    LUA_SET_ENUM_LIB_BEGIN( L, "SOUND_CHANNEL" );
    lua_pushenum( L, CHAN_REPLACE, "REPLACE" );
    lua_pushenum( L, CHAN_AUTO, "AUTO" );
    lua_pushenum( L, CHAN_WEAPON, "WEAPON" );
    lua_pushenum( L, CHAN_VOICE, "VOICE" );
    lua_pushenum( L, CHAN_ITEM, "ITEM" );
    lua_pushenum( L, CHAN_BODY, "BODY" );
    lua_pushenum( L, CHAN_STREAM, "STREAM" );
    lua_pushenum( L, CHAN_STATIC, "STATIC" );
    lua_pushenum( L, CHAN_VOICE2, "VOICE2" );
    lua_pushenum( L, CHAN_VOICE_BASE, "VOICE_BASE" );
    lua_pushenum( L, CHAN_USER_BASE, "USER_BASE" );
    LUA_SET_ENUM_LIB_END( L );

    LUA_SET_ENUM_LIB_BEGIN( L, "RENDER_MODE" );
    lua_pushenum( L, kRenderNormal, "NORMAL" );
    lua_pushenum( L, kRenderTransColor, "TRANSPARENT_COLOR" );
    lua_pushenum( L, kRenderTransTexture, "TRANSPARENT_TEXTURE" );
    lua_pushenum( L, kRenderGlow, "GLOW" );
    lua_pushenum( L, kRenderTransAlpha, "TRANSPARENT_ALPHA" );
    lua_pushenum( L, kRenderTransAdd, "TRANSPARENT_ADD" );
    lua_pushenum( L, kRenderEnvironmental, "ENVIRONMENTAL" );
    lua_pushenum( L, kRenderTransAddFrameBlend, "TRANSPARENT_ADD_FRAME_BLEND" );
    lua_pushenum( L, kRenderTransAlphaAdd, "TRANSPARENT_ALPHA_ADD" );
    lua_pushenum( L, kRenderWorldGlow, "WORLD_GLOW" );
    lua_pushenum( L, kRenderNone, "NONE" );
    lua_pushenum( L, kRenderModeCount, "COUNT" );
    LUA_SET_ENUM_LIB_END( L );

    LUA_SET_ENUM_LIB_BEGIN( L, "RENDER_EFFECTS" );
    lua_pushenum( L, kRenderFxNone, "NONE" );
    lua_pushenum( L, kRenderFxPulseSlow, "PULSE_SLOW" );
    lua_pushenum( L, kRenderFxPulseFast, "PULSE_FAST" );
    lua_pushenum( L, kRenderFxPulseSlowWide, "PULSE_SLOW_WIDE" );
    lua_pushenum( L, kRenderFxPulseFastWide, "PULSE_FAST_WIDE" );
    lua_pushenum( L, kRenderFxFadeSlow, "FADE_SLOW" );
    lua_pushenum( L, kRenderFxFadeFast, "FADE_FAST" );
    lua_pushenum( L, kRenderFxSolidSlow, "SOLID_SLOW" );
    lua_pushenum( L, kRenderFxSolidFast, "SOLID_FAST" );
    lua_pushenum( L, kRenderFxStrobeSlow, "STROBE_SLOW" );
    lua_pushenum( L, kRenderFxStrobeFast, "STROBE_FAST" );
    lua_pushenum( L, kRenderFxStrobeFaster, "STROBE_FASTER" );
    lua_pushenum( L, kRenderFxFlickerSlow, "FLICKER_SLOW" );
    lua_pushenum( L, kRenderFxFlickerFast, "FLICKER_FAST" );
    lua_pushenum( L, kRenderFxNoDissipation, "NO_DISSIPATION" );
    lua_pushenum( L, kRenderFxDistort, "DISTORT" );
    lua_pushenum( L, kRenderFxHologram, "HOLOGRAM" );
    lua_pushenum( L, kRenderFxExplode, "EXPLODE" );
    lua_pushenum( L, kRenderFxGlowShell, "GLOW_SHELL" );
    lua_pushenum( L, kRenderFxClampMinScale, "CLAMP_MIN_SCALE" );
    lua_pushenum( L, kRenderFxEnvRain, "ENVIRONMENT_RAIN" );
    lua_pushenum( L, kRenderFxEnvSnow, "ENVIRONMENT_SNOW" );
    lua_pushenum( L, kRenderFxSpotlight, "SPOTLIGHT" );
    lua_pushenum( L, kRenderFxRagdoll, "RAGDOLL" );
    lua_pushenum( L, kRenderFxPulseFastWider, "PULSE_FAST_WIDER" );
    lua_pushenum( L, kRenderFxMax, "MAX" );
    LUA_SET_ENUM_LIB_END( L );

    LUA_SET_ENUM_LIB_BEGIN( L, "COLLISION_GROUP" );
    lua_pushenum( L, COLLISION_GROUP_NONE, "NONE" );
    lua_pushenum( L, COLLISION_GROUP_DEBRIS, "DEBRIS" );
    lua_pushenum( L, COLLISION_GROUP_DEBRIS_TRIGGER, "DEBRIS_TRIGGER" );
    lua_pushenum( L, COLLISION_GROUP_INTERACTIVE_DEBRIS, "INTERACTIVE_DEBRIS" );
    lua_pushenum( L, COLLISION_GROUP_INTERACTIVE, "INTERACTIVE" );
    lua_pushenum( L, COLLISION_GROUP_PLAYER, "PLAYER" );
    lua_pushenum( L, COLLISION_GROUP_BREAKABLE_GLASS, "BREAKABLE_GLASS" );
    lua_pushenum( L, COLLISION_GROUP_VEHICLE, "VEHICLE" );
    lua_pushenum( L, COLLISION_GROUP_PLAYER_MOVEMENT, "PLAYER_MOVEMENT" );
    lua_pushenum( L, COLLISION_GROUP_NPC, "NPC" );
    lua_pushenum( L, COLLISION_GROUP_IN_VEHICLE, "IN_VEHICLE" );
    lua_pushenum( L, COLLISION_GROUP_WEAPON, "WEAPON" );
    lua_pushenum( L, COLLISION_GROUP_VEHICLE_CLIP, "VEHICLE_CLIP" );
    lua_pushenum( L, COLLISION_GROUP_PROJECTILE, "PROJECTILE" );
    lua_pushenum( L, COLLISION_GROUP_DOOR_BLOCKER, "DOOR_BLOCKER" );
    lua_pushenum( L, COLLISION_GROUP_PASSABLE_DOOR, "PASSABLE_DOOR" );
    lua_pushenum( L, COLLISION_GROUP_DISSOLVING, "DISSOLVING" );
    lua_pushenum( L, COLLISION_GROUP_PUSHAWAY, "PUSHAWAY" );
    lua_pushenum( L, COLLISION_GROUP_NPC_ACTOR, "NPC_ACTOR" );
    lua_pushenum( L, COLLISION_GROUP_NPC_SCRIPTED, "NPC_SCRIPTED" );
    lua_pushenum( L, COLLISION_GROUP_DEBRIS, "WORLD" );  // TODO: How different of debris is this really?
    lua_pushenum( L, LAST_SHARED_COLLISION_GROUP, "LAST" );
    LUA_SET_ENUM_LIB_END( L );

    LUA_SET_ENUM_LIB_BEGIN( L, "PLAYER_ANIMATION" );
    lua_pushenum( L, PLAYER_IDLE, "IDLE" );
    lua_pushenum( L, PLAYER_WALK, "WALK" );
    lua_pushenum( L, PLAYER_JUMP, "JUMP" );
    lua_pushenum( L, PLAYER_SUPERJUMP, "SUPER_JUMP" );
    lua_pushenum( L, PLAYER_DIE, "DIE" );
    lua_pushenum( L, PLAYER_ATTACK1, "ATTACK1" );
    lua_pushenum( L, PLAYER_IN_VEHICLE, "IN_VEHICLE" );
    lua_pushenum( L, PLAYER_RELOAD, "RELOAD" );
    lua_pushenum( L, PLAYER_START_AIMING, "START_AIMING" );
    lua_pushenum( L, PLAYER_LEAVE_AIMING, "LEAVE_AIMING" );
    LUA_SET_ENUM_LIB_END( L );

    LUA_SET_ENUM_LIB_BEGIN( L, "SEARCH_PATH_ADD_TO" );
    lua_pushenum( L, PATH_ADD_TO_HEAD, "HEAD" );
    lua_pushenum( L, PATH_ADD_TO_TAIL, "TAIL" );
    LUA_SET_ENUM_LIB_END( L );

    // HL2SB: KEY_VALUES_PRELOAD_TYPE is not present in this engine's SDK
    // (Experiment carries it from its TF2 SDK merge), so the table is skipped.

    LUA_SET_ENUM_LIB_BEGIN( L, "FILE_WARNING_LEVEL" );
    lua_pushenum( L, FILESYSTEM_WARNING, "WARNING" );
    lua_pushenum( L, FILESYSTEM_WARNING_QUIET, "QUIET" );
    lua_pushenum( L, FILESYSTEM_WARNING_REPORTUNCLOSED, "REPORT_UNCLOSED" );
    lua_pushenum( L, FILESYSTEM_WARNING_REPORTUSAGE, "REPORT_USAGE" );
    lua_pushenum( L, FILESYSTEM_WARNING_REPORTALLACCESSES, "REPORT_ALL_ACCESSES" );
    lua_pushenum( L, FILESYSTEM_WARNING_REPORTALLACCESSES_READ, "REPORT_ALL_ACCESSES_READ" );
    lua_pushenum( L, FILESYSTEM_WARNING_REPORTALLACCESSES_READWRITE, "REPORT_ALL_ACCESSES_READ_WRITE" );
    lua_pushenum( L, FILESYSTEM_WARNING_REPORTALLACCESSES_ASYNC, "REPORT_ALL_ACCESSES_ASYNC" );
    LUA_SET_ENUM_LIB_END( L );

    LUA_SET_ENUM_LIB_BEGIN( L, "VGUI_PANEL" );
    lua_pushenum( L, PANEL_ROOT, "ROOT" );
    lua_pushenum( L, PANEL_GAMEUIDLL, "GAMEUI_DLL" );
    lua_pushenum( L, PANEL_CLIENTDLL, "CLIENT_DLL" );
    lua_pushenum( L, PANEL_TOOLS, "TOOLS" );
    lua_pushenum( L, PANEL_INGAMESCREENS, "INGAME_SCREENS" );
    lua_pushenum( L, PANEL_GAMEDLL, "GAME_DLL" );
    lua_pushenum( L, PANEL_CLIENTDLL_TOOLS, "CLIENT_DLL_TOOLS" );
    LUA_SET_ENUM_LIB_END( L );

    //---------------------------------------------------------------------
    // HL2SB: GMod's DOCK enum -- the one GMod enum whose Lua names have no
    // prefix (NODOCK/FILL/LEFT/RIGHT/TOP/BOTTOM, values 0..5).
    //
    // This table HAS to come from the engine, because
    // lua/includes/modules/gmod_compatibility/sh_enumerations.lua only
    // *stubs* it:
    //
    //     _E.DOCK_TYPE = _E.DOCK_TYPE or {
    //         NONE = 0, FILL = 1, LEFT = 2, TOP = 3, RIGHT = 4, BOTTOM = 5,
    //     }
    //
    // TOP and RIGHT are SWAPPED in that stub, and the file then publishes the
    // globals straight out of it (sh_enumerations.lua:192-197).  So every
    // `panel:Dock( TOP )` in the ported Derma content passed 3, which the vgui2
    // dock pass reads as DOCK_RIGHT: the child was pinned to the right edge at
    // full height and consumed no vertical space at all, so the Dock( FILL )
    // sibling below it still covered the whole panel.
    //
    // Measured before this fix (client.dll of 95855e4e):
    //     p 400x300, a:Dock(TOP) a:SetTall(40), b:Dock(FILL),
    //     p:InvalidateLayout(true)
    //     -> A y=0 h=300 (x=336, w=64)   B y=0 h=300      wanted A y=0 h=40 | B y=40 h=260
    //     a DListLayout+DButton list -> 0/266 0/266 0/266, list h=266   wanted 90
    // (266 = the DFrame's own DockPadding: dframe.lua:65 is
    //  DockPadding( 5, 24 + 5, 5, 5 ), so 300 - 29 - 5.)
    //
    // Values are tied to vgui::Panel::HL2SBDockType_t (the enum
    // vgui2/vgui_controls/Panel.cpp switches on) instead of being written out
    // again, and they agree with lua/includes/extensions/gmod_isvalid.lua:100
    // (the in-tree authority, which warns when a global has the wrong value).
    //
    // The bare globals are deliberately NOT published here
    // (lua_pushenum_nobare): they are generic names other enums hand out, and
    // gmod_isvalid.lua plus sh_enumerations.lua already own that step -- which
    // is all this table has to make correct.
    //---------------------------------------------------------------------
    LUA_SET_ENUM_LIB_BEGIN( L, "DOCK_TYPE" );
    lua_pushenum_nobare( L, vgui::Panel::DOCK_NONE,   "NONE" );
    lua_pushenum_nobare( L, vgui::Panel::DOCK_FILL,   "FILL" );
    lua_pushenum_nobare( L, vgui::Panel::DOCK_LEFT,   "LEFT" );
    lua_pushenum_nobare( L, vgui::Panel::DOCK_RIGHT,  "RIGHT" );
    lua_pushenum_nobare( L, vgui::Panel::DOCK_TOP,    "TOP" );
    lua_pushenum_nobare( L, vgui::Panel::DOCK_BOTTOM, "BOTTOM" );
    LUA_SET_ENUM_LIB_END( L );

    LUA_SET_ENUM_LIB_BEGIN( L, "BLOOD_COLOR" );
    lua_pushenum( L, DONT_BLEED, "DONT_BLEED" );
    lua_pushenum( L, BLOOD_COLOR_RED, "RED" );
    lua_pushenum( L, BLOOD_COLOR_YELLOW, "YELLOW" );
    lua_pushenum( L, BLOOD_COLOR_GREEN, "GREEN" );
    lua_pushenum( L, BLOOD_COLOR_MECH, "MECH" );
#if defined( HL2_EPISODIC )
    lua_pushenum( L, BLOOD_COLOR_ANTLION, "ANTLION" );
    lua_pushenum( L, BLOOD_COLOR_ZOMBIE, "ZOMBIE" );
    lua_pushenum( L, BLOOD_COLOR_ANTLION_WORKER, "ANTLION_WORKER" );
#endif
    LUA_SET_ENUM_LIB_END( L );

    LUA_SET_ENUM_LIB_BEGIN( L, "SOUND_LEVEL" );
    lua_pushenum( L, SNDLVL_NONE, "NONE" );
    lua_pushenum( L, SNDLVL_20dB, "Db20" );
    lua_pushenum( L, SNDLVL_20dB, "RUSTLING_LEAVES" );
    lua_pushenum( L, SNDLVL_25dB, "Db25" );
    lua_pushenum( L, SNDLVL_25dB, "WHISPERING" );
    lua_pushenum( L, SNDLVL_30dB, "Db30" );
    lua_pushenum( L, SNDLVL_30dB, "LIBRARY" );
    lua_pushenum( L, SNDLVL_35dB, "Db35" );
    lua_pushenum( L, SNDLVL_40dB, "Db40" );
    lua_pushenum( L, SNDLVL_45dB, "Db45" );
    lua_pushenum( L, SNDLVL_45dB, "REFRIGERATOR" );
    lua_pushenum( L, SNDLVL_50dB, "Db50" );
    lua_pushenum( L, SNDLVL_50dB, "AVERAGE_HOME" );
    lua_pushenum( L, SNDLVL_55dB, "Db55" );
    lua_pushenum( L, SNDLVL_55dB, "NORMAL_CONVERSATION" );
    lua_pushenum( L, SNDLVL_IDLE, "IDLE" );
    lua_pushenum( L, SNDLVL_60dB, "Db60" );
    lua_pushenum( L, SNDLVL_60dB, "NORMAL_CONVERSATION" );
    lua_pushenum( L, SNDLVL_65dB, "Db65" );
    lua_pushenum( L, SNDLVL_65dB, "WASHING_MACHINE" );
    lua_pushenum( L, SNDLVL_STATIC, "STATIC" );
    lua_pushenum( L, SNDLVL_70dB, "Db70" );
    lua_pushenum( L, SNDLVL_70dB, "CAR" );
    lua_pushenum( L, SNDLVL_NORM, "NORMAL" );
    lua_pushenum( L, SNDLVL_75dB, "Db75" );
    lua_pushenum( L, SNDLVL_75dB, "BUSY_TRAFFIC" );
    lua_pushenum( L, SNDLVL_80dB, "Db80" );
    lua_pushenum( L, SNDLVL_80dB, "MINI_BIKE" );
    lua_pushenum( L, SNDLVL_TALKING, "TALKING" );
    lua_pushenum( L, SNDLVL_85dB, "Db85" );
    lua_pushenum( L, SNDLVL_85dB, "AVERAGE_FACTORY" );
    lua_pushenum( L, SNDLVL_90dB, "Db90" );
    lua_pushenum( L, SNDLVL_90dB, "SCREAMING_CHILD" );
    lua_pushenum( L, SNDLVL_95dB, "Db95" );
    lua_pushenum( L, SNDLVL_100dB, "Db100" );
    lua_pushenum( L, SNDLVL_100dB, "SUBWAY_TRAIN" );
    lua_pushenum( L, SNDLVL_105dB, "Db105" );
    lua_pushenum( L, SNDLVL_105dB, "HELICOPTER" );
    lua_pushenum( L, SNDLVL_110dB, "Db110" );
    lua_pushenum( L, SNDLVL_110dB, "SNOWMOBILE" );
    lua_pushenum( L, SNDLVL_120dB, "Db120" );
    lua_pushenum( L, SNDLVL_120dB, "AUTO_HORN" );
    lua_pushenum( L, SNDLVL_130dB, "Db130" );
    lua_pushenum( L, SNDLVL_130dB, "AIR_RAID_SIREN" );
    lua_pushenum( L, SNDLVL_GUNFIRE, "GUNFIRE" );
    lua_pushenum( L, SNDLVL_140dB, "Db140" );
    lua_pushenum( L, SNDLVL_150dB, "Db150" );
    lua_pushenum( L, SNDLVL_180dB, "Db180" );
    lua_pushenum( L, SNDLVL_180dB, "ROCKET_LAUNCHING" );
    LUA_SET_ENUM_LIB_END( L );

    LUA_SET_ENUM_LIB_BEGIN( L, "USE_TYPE" );
    lua_pushenum( L, USE_OFF, "OFF" );
    lua_pushenum( L, USE_ON, "ON" );
    lua_pushenum( L, USE_SET, "SET" );
    lua_pushenum( L, USE_TOGGLE, "TOGGLE" );
    LUA_SET_ENUM_LIB_END( L );

    LUA_SET_ENUM_LIB_BEGIN( L, "DAMAGE_TYPE" );
    lua_pushenum( L, DMG_GENERIC, "GENERIC" );
    lua_pushenum( L, DMG_CRUSH, "CRUSH" );
    lua_pushenum( L, DMG_BULLET, "BULLET" );
    lua_pushenum( L, DMG_SLASH, "SLASH" );
    lua_pushenum( L, DMG_BURN, "BURN" );
    lua_pushenum( L, DMG_VEHICLE, "VEHICLE" );
    lua_pushenum( L, DMG_FALL, "FALL" );
    lua_pushenum( L, DMG_BLAST, "BLAST" );
    lua_pushenum( L, DMG_CLUB, "CLUB" );
    lua_pushenum( L, DMG_SHOCK, "SHOCK" );
    lua_pushenum( L, DMG_SONIC, "SONIC" );
    lua_pushenum( L, DMG_ENERGYBEAM, "ENERGY_BEAM" );
    lua_pushenum( L, DMG_PREVENT_PHYSICS_FORCE, "PREVENT_PHYSICS_FORCE" );
    lua_pushenum( L, DMG_NEVERGIB, "NEVER_GIB" );
    lua_pushenum( L, DMG_ALWAYSGIB, "ALWAYS_GIB" );
    lua_pushenum( L, DMG_DROWN, "DROWN" );
    lua_pushenum( L, DMG_PARALYZE, "PARALYZE" );
    lua_pushenum( L, DMG_NERVEGAS, "NERVE_GAS" );
    lua_pushenum( L, DMG_POISON, "POISON" );
    lua_pushenum( L, DMG_RADIATION, "RADIATION" );
    lua_pushenum( L, DMG_DROWNRECOVER, "DROWN_RECOVER" );
    lua_pushenum( L, DMG_ACID, "ACID" );
    lua_pushenum( L, DMG_SLOWBURN, "SLOW_BURN" );
    lua_pushenum( L, DMG_REMOVENORAGDOLL, "REMOVE_NO_RAGDOLL" );
    lua_pushenum( L, DMG_PHYSGUN, "PHYSGUN" );
    lua_pushenum( L, DMG_PLASMA, "PLASMA" );
    lua_pushenum( L, DMG_AIRBOAT, "AIRBOAT" );
    lua_pushenum( L, DMG_DISSOLVE, "DISSOLVE" );
    lua_pushenum( L, DMG_BLAST_SURFACE, "BLAST_SURFACE" );
    lua_pushenum( L, DMG_DIRECT, "DIRECT" );
    lua_pushenum( L, DMG_BUCKSHOT, "BUCKSHOT" );
    lua_pushenum( L, DMG_LASTGENERICFLAG, "LAST_GENERIC_FLAG" );
    // HL2 SPECIFIC (hl2_shareddefs.h)
    lua_pushenum( L, DMG_SNIPER, "SNIPER" );
    lua_pushenum( L, DMG_MISSILEDEFENSE, "MISSILE_DEFENSE" );
    LUA_SET_ENUM_LIB_END( L );

    LUA_SET_ENUM_LIB_BEGIN( L, "HIT_GROUP" );
    lua_pushenum( L, HITGROUP_GENERIC, "GENERIC" );
    lua_pushenum( L, HITGROUP_HEAD, "HEAD" );
    lua_pushenum( L, HITGROUP_CHEST, "CHEST" );
    lua_pushenum( L, HITGROUP_STOMACH, "STOMACH" );
    lua_pushenum( L, HITGROUP_LEFTARM, "LEFT_ARM" );
    lua_pushenum( L, HITGROUP_RIGHTARM, "RIGHT_ARM" );
    lua_pushenum( L, HITGROUP_LEFTLEG, "LEFT_LEG" );
    lua_pushenum( L, HITGROUP_RIGHTLEG, "RIGHT_LEG" );
    lua_pushenum( L, HITGROUP_GEAR, "GEAR" );
    LUA_SET_ENUM_LIB_END( L );

    LUA_SET_ENUM_LIB_BEGIN( L, "MATERIAL_TYPE" );
    lua_pushenum( L, CHAR_TEX_ANTLION, "ANTLION" );
    lua_pushenum( L, CHAR_TEX_BLOODYFLESH, "BLOODY_FLESH" );
    lua_pushenum( L, CHAR_TEX_CONCRETE, "CONCRETE" );
    lua_pushenum( L, CHAR_TEX_DIRT, "DIRT" );
    lua_pushenum( L, CHAR_TEX_EGGSHELL, "EGGSHELL" );
    lua_pushenum( L, CHAR_TEX_FLESH, "FLESH" );
    lua_pushenum( L, CHAR_TEX_GRATE, "GRATE" );
    lua_pushenum( L, CHAR_TEX_ALIENFLESH, "ALIEN_FLESH" );
    lua_pushenum( L, CHAR_TEX_CLIP, "CLIP" );
    // HL2SB: not present in this engine -> lua_pushenum( L, CHAR_TEX_SNOW, "SNOW" );
    lua_pushenum( L, CHAR_TEX_PLASTIC, "PLASTIC" );
    lua_pushenum( L, CHAR_TEX_METAL, "METAL" );
    lua_pushenum( L, CHAR_TEX_SAND, "SAND" );
    lua_pushenum( L, CHAR_TEX_FOLIAGE, "FOLIAGE" );
    lua_pushenum( L, CHAR_TEX_COMPUTER, "COMPUTER" );
    lua_pushenum( L, CHAR_TEX_SLOSH, "SLOSH" );
    lua_pushenum( L, CHAR_TEX_TILE, "TILE" );
    // HL2SB: not present in this engine -> lua_pushenum( L, CHAR_TEX_GRASS, "GRASS" );
    lua_pushenum( L, CHAR_TEX_VENT, "VENT" );
    lua_pushenum( L, CHAR_TEX_WOOD, "WOOD" );
    // HL2SB: not present in this engine -> lua_pushenum( L, CHAR_TEX_DEFAULT, "DEFAULT" );
    lua_pushenum( L, CHAR_TEX_GLASS, "GLASS" );
    lua_pushenum( L, CHAR_TEX_WARPSHIELD, "WARP_SHIELD" );
    LUA_SET_ENUM_LIB_END( L );

    LUA_SET_ENUM_LIB_BEGIN( L, "NETWORK_VARIABLE_TYPE" );
    // HL2SB: not present in this engine -> lua_pushenum( L, TYPE_BOOLEAN, "BOOLEAN" );
    // HL2SB: not present in this engine -> lua_pushenum( L, TYPE_INTEGER, "INTEGER" );
    // HL2SB: not present in this engine -> lua_pushenum( L, TYPE_FLOAT, "FLOAT" );
    // HL2SB: not present in this engine -> lua_pushenum( L, TYPE_VECTOR, "VECTOR" );
    // HL2SB: not present in this engine -> lua_pushenum( L, TYPE_ANGLE, "ANGLE" );
    // HL2SB: not present in this engine -> lua_pushenum( L, TYPE_STRING, "STRING" );
    // HL2SB: not present in this engine -> lua_pushenum( L, TYPE_ENTITY, "ENTITY" );
    LUA_SET_ENUM_LIB_END( L );

    LUA_SET_ENUM_LIB_BEGIN( L, "HUD_PRINT" );
    lua_pushenum( L, HUD_PRINTNOTIFY, "NOTIFY" );
    lua_pushenum( L, HUD_PRINTCONSOLE, "CONSOLE" );
    lua_pushenum( L, HUD_PRINTTALK, "TALK" );
    lua_pushenum( L, HUD_PRINTCENTER, "CENTER" );
    LUA_SET_ENUM_LIB_END( L );

    LUA_SET_ENUM_LIB_BEGIN( L, "PLAYER_ANIMATION_EVENT" );
    lua_pushenum( L, PLAYERANIMEVENT_ATTACK_PRIMARY, "ATTACK_PRIMARY" );
    lua_pushenum( L, PLAYERANIMEVENT_ATTACK_SECONDARY, "ATTACK_SECONDARY" );
    lua_pushenum( L, PLAYERANIMEVENT_ATTACK_GRENADE, "ATTACK_GRENADE" );
    lua_pushenum( L, PLAYERANIMEVENT_RELOAD, "RELOAD" );
    lua_pushenum( L, PLAYERANIMEVENT_RELOAD_LOOP, "RELOAD_LOOP" );
    lua_pushenum( L, PLAYERANIMEVENT_RELOAD_END, "RELOAD_END" );
    lua_pushenum( L, PLAYERANIMEVENT_JUMP, "JUMP" );
    lua_pushenum( L, PLAYERANIMEVENT_SWIM, "SWIM" );
    lua_pushenum( L, PLAYERANIMEVENT_DIE, "DIE" );
    lua_pushenum( L, PLAYERANIMEVENT_FLINCH_CHEST, "FLINCH_CHEST" );
    lua_pushenum( L, PLAYERANIMEVENT_FLINCH_HEAD, "FLINCH_HEAD" );
    lua_pushenum( L, PLAYERANIMEVENT_FLINCH_LEFTARM, "FLINCH_LEFT_ARM" );
    lua_pushenum( L, PLAYERANIMEVENT_FLINCH_RIGHTARM, "FLINCH_RIGHT_ARM" );
    lua_pushenum( L, PLAYERANIMEVENT_FLINCH_LEFTLEG, "FLINCH_LEFT_LEG" );
    lua_pushenum( L, PLAYERANIMEVENT_FLINCH_RIGHTLEG, "FLINCH_RIGHT_LEG" );
    lua_pushenum( L, PLAYERANIMEVENT_DOUBLEJUMP, "DOUBLE_JUMP" );
    lua_pushenum( L, PLAYERANIMEVENT_CANCEL, "CANCEL" );
    lua_pushenum( L, PLAYERANIMEVENT_SPAWN, "SPAWN" );
    lua_pushenum( L, PLAYERANIMEVENT_SNAP_YAW, "SNAP_YAW" );
    lua_pushenum( L, PLAYERANIMEVENT_CUSTOM, "CUSTOM" );
    lua_pushenum( L, PLAYERANIMEVENT_CUSTOM_GESTURE, "CUSTOM_GESTURE" );
    lua_pushenum( L, PLAYERANIMEVENT_CUSTOM_SEQUENCE, "CUSTOM_SEQUENCE" );
    lua_pushenum( L, PLAYERANIMEVENT_CUSTOM_GESTURE_SEQUENCE, "CUSTOM_GESTURE_SEQUENCE" );
    lua_pushenum( L, PLAYERANIMEVENT_ATTACK_PRE, "ATTACK_PRE" );
    lua_pushenum( L, PLAYERANIMEVENT_ATTACK_POST, "ATTACK_POST" );
    lua_pushenum( L, PLAYERANIMEVENT_GRENADE1_DRAW, "GRENADE1_DRAW" );
    lua_pushenum( L, PLAYERANIMEVENT_GRENADE2_DRAW, "GRENADE2_DRAW" );
    lua_pushenum( L, PLAYERANIMEVENT_GRENADE1_THROW, "GRENADE1_THROW" );
    lua_pushenum( L, PLAYERANIMEVENT_GRENADE2_THROW, "GRENADE2_THROW" );
    lua_pushenum( L, PLAYERANIMEVENT_VOICE_COMMAND_GESTURE, "VOICE_COMMAND_GESTURE" );
    lua_pushenum( L, PLAYERANIMEVENT_DOUBLEJUMP_CROUCH, "DOUBLE_JUMP_CROUCH" );
    lua_pushenum( L, PLAYERANIMEVENT_STUN_BEGIN, "STUN_BEGIN" );
    lua_pushenum( L, PLAYERANIMEVENT_STUN_MIDDLE, "STUN_MIDDLE" );
    lua_pushenum( L, PLAYERANIMEVENT_STUN_END, "STUN_END" );
    // HL2SB: not present in this engine -> lua_pushenum( L, PLAYERANIMEVENT_PASSTIME_THROW_BEGIN, "PASSTIME_THROW_BEGIN" );
    // HL2SB: not present in this engine -> lua_pushenum( L, PLAYERANIMEVENT_PASSTIME_THROW_MIDDLE, "PASSTIME_THROW_MIDDLE" );
    // HL2SB: not present in this engine -> lua_pushenum( L, PLAYERANIMEVENT_PASSTIME_THROW_END, "PASSTIME_THROW_END" );
    // HL2SB: not present in this engine -> lua_pushenum( L, PLAYERANIMEVENT_PASSTIME_THROW_CANCEL, "PASSTIME_THROW_CANCEL" );
    lua_pushenum( L, PLAYERANIMEVENT_ATTACK_PRIMARY_SUPER, "ATTACK_PRIMARY_SUPER" );
    lua_pushenum( L, PLAYERANIMEVENT_COUNT, "COUNT" );
    LUA_SET_ENUM_LIB_END( L );

    return 0;
}

/*
** Open ACT_* enum library
*/
LUALIB_API int luaopen_ACTIVITY( lua_State *L )
{
    // Experiment; In order to get all ACT_* enums available in time,
    // we moved this from CWorld and C_World to here.
    ActivityList_Init();

    // Andrew; There's a big issue with including the Activity enumeration
    // library, and that issue is that it's massive. While we clean up _G by
    // placing it in it's own library and increase lookup times across nearly
    // all of our resources, it may be best that we make it a standard practice
    // for developers to look up the enumerations that they need on an
    // as-needed basis, and store them as locals in their relative files, or
    // simply use the raw value of that enumeration in scripts, which is the
    // most performance efficient option.

    // Experiment; Thanks Andrew, but for GMod compatibility we have to ignore
    // the above advice and registers all enumerations.
    // Leaving Andrew's advice here for future reference.
    LUA_SET_ENUM_LIB_BEGIN( L, "ACTIVITY" );
    ActivityList_RegisterSharedActivities();
    LUA_SET_ENUM_LIB_END( L );

    return 0;
}

/*
** Open ButtonCode_t enum library
*/
LUALIB_API int luaopen_BUTTON( lua_State *L )
{
    // Expose ButtonCode_t as enums
    LUA_SET_ENUM_LIB_BEGIN( L, "BUTTON" );

    lua_pushenum( L, BUTTON_CODE_INVALID, "BUTTON_CODE_INVALID" );
    lua_pushenum( L, BUTTON_CODE_NONE, "BUTTON_CODE_NONE" );
    lua_pushenum( L, KEY_FIRST, "KEY_FIRST" );
    lua_pushenum( L, KEY_NONE, "KEY_NONE" );

    lua_pushenum( L, KEY_0, "KEY_0" );
    lua_pushenum( L, KEY_1, "KEY_1" );
    lua_pushenum( L, KEY_2, "KEY_2" );
    lua_pushenum( L, KEY_3, "KEY_3" );
    lua_pushenum( L, KEY_4, "KEY_4" );
    lua_pushenum( L, KEY_5, "KEY_5" );
    lua_pushenum( L, KEY_6, "KEY_6" );
    lua_pushenum( L, KEY_7, "KEY_7" );
    lua_pushenum( L, KEY_8, "KEY_8" );
    lua_pushenum( L, KEY_9, "KEY_9" );

    lua_pushenum( L, KEY_A, "KEY_A" );
    lua_pushenum( L, KEY_B, "KEY_B" );
    lua_pushenum( L, KEY_C, "KEY_C" );
    lua_pushenum( L, KEY_D, "KEY_D" );
    lua_pushenum( L, KEY_E, "KEY_E" );
    lua_pushenum( L, KEY_F, "KEY_F" );
    lua_pushenum( L, KEY_G, "KEY_G" );
    lua_pushenum( L, KEY_H, "KEY_H" );
    lua_pushenum( L, KEY_I, "KEY_I" );
    lua_pushenum( L, KEY_J, "KEY_J" );
    lua_pushenum( L, KEY_K, "KEY_K" );
    lua_pushenum( L, KEY_L, "KEY_L" );
    lua_pushenum( L, KEY_M, "KEY_M" );
    lua_pushenum( L, KEY_N, "KEY_N" );
    lua_pushenum( L, KEY_O, "KEY_O" );
    lua_pushenum( L, KEY_P, "KEY_P" );
    lua_pushenum( L, KEY_Q, "KEY_Q" );
    lua_pushenum( L, KEY_R, "KEY_R" );
    lua_pushenum( L, KEY_S, "KEY_S" );
    lua_pushenum( L, KEY_T, "KEY_T" );
    lua_pushenum( L, KEY_U, "KEY_U" );
    lua_pushenum( L, KEY_V, "KEY_V" );
    lua_pushenum( L, KEY_W, "KEY_W" );
    lua_pushenum( L, KEY_X, "KEY_X" );
    lua_pushenum( L, KEY_Y, "KEY_Y" );
    lua_pushenum( L, KEY_Z, "KEY_Z" );

    lua_pushenum( L, KEY_PAD_0, "KEYPAD_0" );
    lua_pushenum( L, KEY_PAD_1, "KEYPAD_1" );
    lua_pushenum( L, KEY_PAD_2, "KEYPAD_2" );
    lua_pushenum( L, KEY_PAD_3, "KEYPAD_3" );
    lua_pushenum( L, KEY_PAD_4, "KEYPAD_4" );
    lua_pushenum( L, KEY_PAD_5, "KEYPAD_5" );
    lua_pushenum( L, KEY_PAD_6, "KEYPAD_6" );
    lua_pushenum( L, KEY_PAD_7, "KEYPAD_7" );
    lua_pushenum( L, KEY_PAD_8, "KEYPAD_8" );
    lua_pushenum( L, KEY_PAD_9, "KEYPAD_9" );

    lua_pushenum( L, KEY_PAD_DIVIDE, "KEYPAD_DIVIDE" );
    lua_pushenum( L, KEY_PAD_MULTIPLY, "KEYPAD_MULTIPLY" );
    lua_pushenum( L, KEY_PAD_MINUS, "KEYPAD_MINUS" );
    lua_pushenum( L, KEY_PAD_PLUS, "KEYPAD_PLUS" );
    lua_pushenum( L, KEY_PAD_ENTER, "KEYPAD_ENTER" );
    lua_pushenum( L, KEY_PAD_DECIMAL, "KEYPAD_DECIMAL" );

    lua_pushenum( L, KEY_LBRACKET, "KEY_BRACKET_LEFT" );
    lua_pushenum( L, KEY_RBRACKET, "KEY_BRACKET_RIGHT" );
    lua_pushenum( L, KEY_SEMICOLON, "KEY_SEMICOLON" );
    lua_pushenum( L, KEY_APOSTROPHE, "KEY_APOSTROPHE" );
    lua_pushenum( L, KEY_BACKQUOTE, "KEY_BACKQUOTE" );
    lua_pushenum( L, KEY_COMMA, "KEY_COMMA" );
    lua_pushenum( L, KEY_PERIOD, "KEY_PERIOD" );
    lua_pushenum( L, KEY_SLASH, "KEY_SLASH" );
    lua_pushenum( L, KEY_BACKSLASH, "KEY_BACKSLASH" );
    lua_pushenum( L, KEY_MINUS, "KEY_MINUS" );
    lua_pushenum( L, KEY_EQUAL, "KEY_EQUAL" );
    lua_pushenum( L, KEY_ENTER, "KEY_ENTER" );
    lua_pushenum( L, KEY_SPACE, "KEY_SPACE" );
    lua_pushenum( L, KEY_BACKSPACE, "KEY_BACKSPACE" );
    lua_pushenum( L, KEY_TAB, "KEY_TAB" );
    lua_pushenum( L, KEY_CAPSLOCK, "KEY_CAPSLOCK" );
    lua_pushenum( L, KEY_NUMLOCK, "KEY_NUM_LOCK" );
    lua_pushenum( L, KEY_ESCAPE, "KEY_ESCAPE" );
    lua_pushenum( L, KEY_SCROLLLOCK, "KEY_SCROLL_LOCK" );
    lua_pushenum( L, KEY_INSERT, "KEY_INSERT" );
    lua_pushenum( L, KEY_DELETE, "KEY_DELETE" );
    lua_pushenum( L, KEY_HOME, "KEY_HOME" );
    lua_pushenum( L, KEY_END, "KEY_END" );
    lua_pushenum( L, KEY_PAGEUP, "KEY_PAGE_UP" );
    lua_pushenum( L, KEY_PAGEDOWN, "KEY_PAGE_DOWN" );
    lua_pushenum( L, KEY_BREAK, "KEY_BREAK" );

    lua_pushenum( L, KEY_LSHIFT, "KEY_SHIFT_LEFT" );
    lua_pushenum( L, KEY_RSHIFT, "KEY_SHIFT_RIGHT" );
    lua_pushenum( L, KEY_LALT, "KEY_ALT_LEFT" );
    lua_pushenum( L, KEY_RALT, "KEY_ALT_RIGHT" );
    lua_pushenum( L, KEY_LCONTROL, "KEY_CONTROL_LEFT" );
    lua_pushenum( L, KEY_RCONTROL, "KEY_CONTROL_RIGHT" );
    lua_pushenum( L, KEY_LWIN, "KEY_WINDOWS_LEFT" );
    lua_pushenum( L, KEY_RWIN, "KEY_WINDOWS_RIGHT" );
    lua_pushenum( L, KEY_APP, "KEY_APP" );

    lua_pushenum( L, KEY_UP, "KEY_UP" );
    lua_pushenum( L, KEY_LEFT, "KEY_LEFT" );
    lua_pushenum( L, KEY_DOWN, "KEY_DOWN" );
    lua_pushenum( L, KEY_RIGHT, "KEY_RIGHT" );

    lua_pushenum( L, KEY_F1, "KEY_F1" );
    lua_pushenum( L, KEY_F2, "KEY_F2" );
    lua_pushenum( L, KEY_F3, "KEY_F3" );
    lua_pushenum( L, KEY_F4, "KEY_F4" );
    lua_pushenum( L, KEY_F5, "KEY_F5" );
    lua_pushenum( L, KEY_F6, "KEY_F6" );
    lua_pushenum( L, KEY_F7, "KEY_F7" );
    lua_pushenum( L, KEY_F8, "KEY_F8" );
    lua_pushenum( L, KEY_F9, "KEY_F9" );
    lua_pushenum( L, KEY_F10, "KEY_F10" );
    lua_pushenum( L, KEY_F11, "KEY_F11" );
    lua_pushenum( L, KEY_F12, "KEY_F12" );

    lua_pushenum( L, KEY_CAPSLOCKTOGGLE, "KEY_CAPS_LOCK_TOGGLE" );
    lua_pushenum( L, KEY_NUMLOCKTOGGLE, "KEY_NUM_LOCK_TOGGLE" );
    lua_pushenum( L, KEY_SCROLLLOCKTOGGLE, "KEY_SCROLL_LOCK_TOGGLE" );

    lua_pushenum( L, KEY_LAST, "KEY_LAST" );

    lua_pushenum( L, MOUSE_FIRST, "MOUSE_FIRST" );
    lua_pushenum( L, MOUSE_LEFT, "MOUSE_LEFT" );
    lua_pushenum( L, MOUSE_RIGHT, "MOUSE_RIGHT" );
    lua_pushenum( L, MOUSE_MIDDLE, "MOUSE_MIDDLE" );
    lua_pushenum( L, MOUSE_4, "MOUSE_4" );
    lua_pushenum( L, MOUSE_5, "MOUSE_5" );
    lua_pushenum( L, MOUSE_WHEEL_UP, "MOUSE_WHEEL_UP" );
    lua_pushenum( L, MOUSE_WHEEL_DOWN, "MOUSE_WHEEL_DOWN" );
    lua_pushenum( L, MOUSE_LAST, "MOUSE_LAST" );

    lua_pushenum( L, JOYSTICK_FIRST, "JOYSTICK_FIRST" );
    lua_pushenum( L, JOYSTICK_FIRST_BUTTON, "JOYSTICK_FIRST_BUTTON" );
    lua_pushenum( L, JOYSTICK_LAST_BUTTON, "JOYSTICK_LAST_BUTTON" );
    lua_pushenum( L, JOYSTICK_FIRST_POV_BUTTON, "JOYSTICK_FIRST_POV_BUTTON" );
    lua_pushenum( L, JOYSTICK_LAST_POV_BUTTON, "JOYSTICK_LAST_POV_BUTTON" );
    lua_pushenum( L, JOYSTICK_FIRST_AXIS_BUTTON, "JOYSTICK_FIRST_AXIS_BUTTON" );
    lua_pushenum( L, JOYSTICK_LAST_AXIS_BUTTON, "JOYSTICK_LAST_AXIS_BUTTON" );
    lua_pushenum( L, JOYSTICK_LAST, "JOYSTICK_LAST" );

#if !defined( _X360 )
    lua_pushenum( L, NOVINT_FIRST, "NOVINT_FIRST" );
    lua_pushenum( L, NOVINT_LOGO_0, "NOVINT_LOGO_0" );
    lua_pushenum( L, NOVINT_TRIANGLE_0, "NOVINT_TRIANGLE_0" );
    lua_pushenum( L, NOVINT_BOLT_0, "NOVINT_BOLT_0" );
    lua_pushenum( L, NOVINT_PLUS_0, "NOVINT_PLUS_0" );
    lua_pushenum( L, NOVINT_LOGO_1, "NOVINT_LOGO_1" );
    lua_pushenum( L, NOVINT_TRIANGLE_1, "NOVINT_TRIANGLE_1" );
    lua_pushenum( L, NOVINT_BOLT_1, "NOVINT_BOLT_1" );
    lua_pushenum( L, NOVINT_PLUS_1, "NOVINT_PLUS_1" );
    lua_pushenum( L, NOVINT_LAST, "NOVINT_LAST" );
#endif

    lua_pushenum( L, BUTTON_CODE_LAST, "BUTTON_CODE_LAST" );
    lua_pushenum( L, BUTTON_CODE_COUNT, "BUTTON_CODE_COUNT" );

    LUA_SET_ENUM_LIB_END( L );

    return 0;
}

/*
** Open engine flags FL_* and EFL_ enum library (see m_fFlags and m_iEFlags)
*/
LUALIB_API int luaopen_FL( lua_State *L )
{
    LUA_SET_ENUM_LIB_BEGIN( L, LUA_ENGINEFLAGSENUMLIBNAME );
    lua_pushenum( L, FL_ONGROUND, "ON_GROUND" );
    lua_pushenum( L, FL_DUCKING, "DUCKING" );
    lua_pushenum( L, FL_ANIMDUCKING, "DUCKING_ANIMATION" );
    lua_pushenum( L, FL_WATERJUMP, "WATER_JUMP" );
    lua_pushenum( L, FL_ONTRAIN, "ON_TRAIN" );
    lua_pushenum( L, FL_INRAIN, "IN_RAIN" );
    lua_pushenum( L, FL_FROZEN, "FROZEN" );
    lua_pushenum( L, FL_ATCONTROLS, "AT_CONTROLS" );
    lua_pushenum( L, FL_CLIENT, "CLIENT" );
    lua_pushenum( L, FL_FAKECLIENT, "FAKE_CLIENT" );
    lua_pushenum( L, FL_INWATER, "IN_WATER" );
    lua_pushenum( L, FL_FLY, "FLY" );
    lua_pushenum( L, FL_SWIM, "SWIM" );
    lua_pushenum( L, FL_CONVEYOR, "CONVEYOR" );
    lua_pushenum( L, FL_NPC, "NPC" );
    lua_pushenum( L, FL_GODMODE, "GOD_MODE" );
    lua_pushenum( L, FL_NOTARGET, "NO_TARGET" );
    lua_pushenum( L, FL_AIMTARGET, "AIM_TARGET" );
    lua_pushenum( L, FL_PARTIALGROUND, "PARTIAL_GROUND" );
    lua_pushenum( L, FL_STATICPROP, "STATIC_PROP" );
    lua_pushenum( L, FL_GRAPHED, "GRAPHED" );
    lua_pushenum( L, FL_GRENADE, "GRENADE" );
    lua_pushenum( L, FL_STEPMOVEMENT, "STEP_MOVEMENT" );
    lua_pushenum( L, FL_DONTTOUCH, "DONT_TOUCH" );
    lua_pushenum( L, FL_BASEVELOCITY, "BASE_VELOCITY" );
    lua_pushenum( L, FL_WORLDBRUSH, "WORLD_BRUSH" );
    lua_pushenum( L, FL_OBJECT, "OBJECT" );
    lua_pushenum( L, FL_KILLME, "KILL_ME" );
    lua_pushenum( L, FL_ONFIRE, "ON_FIRE" );
    lua_pushenum( L, FL_DISSOLVING, "DISSOLVING" );
    lua_pushenum( L, FL_TRANSRAGDOLL, "TRANSFORMING_TO_RAGDOLL" );
    lua_pushenum( L, FL_UNBLOCKABLE_BY_PLAYER, "UNBLOCKABLE_BY_PLAYER" );
    LUA_SET_ENUM_LIB_END( L );

    LUA_SET_ENUM_LIB_BEGIN( L, "ENTITY_FLAG" );
    lua_pushenum( L, EFL_KILLME, "KILL_ME" );
    lua_pushenum( L, EFL_DORMANT, "DORMANT" );
    lua_pushenum( L, EFL_NOCLIP_ACTIVE, "NOCLIP_ACTIVE" );
    lua_pushenum( L, EFL_SETTING_UP_BONES, "SETTING_UP_BONES" );
    lua_pushenum( L, EFL_KEEP_ON_RECREATE_ENTITIES, "KEEP_ON_RECREATE_ENTITIES" );
    lua_pushenum( L, EFL_HAS_PLAYER_CHILD, "HAS_PLAYER_CHILD" );
    lua_pushenum( L, EFL_DIRTY_SHADOWUPDATE, "DIRTY_SHADOW_UPDATE" );
    lua_pushenum( L, EFL_NOTIFY, "NOTIFY" );
    lua_pushenum( L, EFL_FORCE_CHECK_TRANSMIT, "FORCE_CHECK_TRANSMIT" );
    lua_pushenum( L, EFL_BOT_FROZEN, "BOT_FROZEN" );
    lua_pushenum( L, EFL_SERVER_ONLY, "SERVER_ONLY" );
    lua_pushenum( L, EFL_NO_AUTO_EDICT_ATTACH, "NO_AUTO_EDICT_ATTACH" );
    lua_pushenum( L, EFL_DIRTY_ABSTRANSFORM, "DIRTY_ABSOLUTE_TRANSFORM" );
    lua_pushenum( L, EFL_DIRTY_ABSVELOCITY, "DIRTY_ABSOLUTE_VELOCITY" );
    lua_pushenum( L, EFL_DIRTY_ABSANGVELOCITY, "DIRTY_ABSOLUTE_ANGULAR_VELOCITY" );
    lua_pushenum( L, EFL_DIRTY_SURROUNDING_COLLISION_BOUNDS, "DIRTY_SURROUNDING_COLLISION_BOUNDS" );
    lua_pushenum( L, EFL_DIRTY_SPATIAL_PARTITION, "DIRTY_SPATIAL_PARTITION" );
    lua_pushenum( L, EFL_IN_SKYBOX, "IN_SKYBOX" );
    lua_pushenum( L, EFL_USE_PARTITION_WHEN_NOT_SOLID, "USE_PARTITION_WHEN_NOT_SOLID" );
    lua_pushenum( L, EFL_TOUCHING_FLUID, "TOUCHING_FLUID" );
    lua_pushenum( L, EFL_IS_BEING_LIFTED_BY_BARNACLE, "IS_BEING_LIFTED_BY_BARNACLE" );
    lua_pushenum( L, EFL_NO_ROTORWASH_PUSH, "NO_ROTORWASH_PUSH" );
    lua_pushenum( L, EFL_NO_THINK_FUNCTION, "NO_THINK_FUNCTION" );
    lua_pushenum( L, EFL_NO_GAME_PHYSICS_SIMULATION, "NO_GAME_PHYSICS_SIMULATION" );
    lua_pushenum( L, EFL_CHECK_UNTOUCH, "CHECK_UNTOUCH" );
    lua_pushenum( L, EFL_DONTBLOCKLOS, "DONT_BLOCK_LINE_OF_SIGHT" );
    lua_pushenum( L, EFL_DONTWALKON, "DONT_WALK_ON" );
    lua_pushenum( L, EFL_NO_DISSOLVE, "NO_DISSOLVE" );
    lua_pushenum( L, EFL_NO_MEGAPHYSCANNON_RAGDOLL, "NO_MEGA_PHYSCANNON_RAGDOLL" );
    lua_pushenum( L, EFL_NO_WATER_VELOCITY_CHANGE, "NO_WATER_VELOCITY_CHANGE" );
    lua_pushenum( L, EFL_NO_PHYSCANNON_INTERACTION, "NO_PHYSCANNON_INTERACTION" );
    lua_pushenum( L, EFL_NO_DAMAGE_FORCES, "NO_DAMAGE_FORCES" );
    LUA_SET_ENUM_LIB_END( L );

    return 0;
}

/*
** Open OBS_MODE_* enum library
*/
LUALIB_API int luaopen_OBS_MODE( lua_State *L )
{
    LUA_SET_ENUM_LIB_BEGIN( L, LUA_OBSMODELIBNAME );
    lua_pushenum( L, OBS_MODE_NONE, "NONE" );
    lua_pushenum( L, OBS_MODE_DEATHCAM, "DEATHCAM" );
    lua_pushenum( L, OBS_MODE_FREEZECAM, "FREEZECAM" );
    lua_pushenum( L, OBS_MODE_FIXED, "FIXED" );
    lua_pushenum( L, OBS_MODE_IN_EYE, "IN_EYE" );
    lua_pushenum( L, OBS_MODE_CHASE, "CHASE" );
    // HL2SB: not present in this engine -> lua_pushenum( L, OBS_MODE_POI, "POI" );
    lua_pushenum( L, OBS_MODE_ROAMING, "ROAMING" );
    LUA_SET_ENUM_LIB_END( L );
    return 0;
}

/*
** Open GESTURE_SLOT enum library
*/
LUALIB_API int luaopen_GESTURE_SLOT( lua_State *L )
{
    LUA_SET_ENUM_LIB_BEGIN( L, LUA_GESTURESLOTLIBNAME );
    lua_pushenum( L, GESTURE_SLOT_ATTACK_AND_RELOAD, "ATTACK_AND_RELOAD" );
    lua_pushenum( L, GESTURE_SLOT_GRENADE, "GRENADE" );
    lua_pushenum( L, GESTURE_SLOT_JUMP, "JUMP" );
    lua_pushenum( L, GESTURE_SLOT_SWIM, "SWIM" );
    lua_pushenum( L, GESTURE_SLOT_FLINCH, "FLINCH" );
    lua_pushenum( L, GESTURE_SLOT_VCD, "VCD" );
    lua_pushenum( L, GESTURE_SLOT_CUSTOM, "CUSTOM" );
    lua_pushenum( L, GESTURE_SLOT_COUNT, "COUNT" );
    lua_pushenum( L, GESTURE_SLOT_INVALID, "INVALID" );
    LUA_SET_ENUM_LIB_END( L );
    return 0;
}

/*
** Open MOVETYPE_* enum library
*/
LUALIB_API int luaopen_MOVETYPE( lua_State *L )
{
    LUA_SET_ENUM_LIB_BEGIN( L, LUA_MOVETYPELIBNAME );
    lua_pushenum( L, MOVETYPE_NONE, "NONE" );
    lua_pushenum( L, MOVETYPE_ISOMETRIC, "ISOMETRIC" );
    lua_pushenum( L, MOVETYPE_WALK, "WALK" );
    lua_pushenum( L, MOVETYPE_STEP, "STEP" );
    lua_pushenum( L, MOVETYPE_FLY, "FLY" );
    lua_pushenum( L, MOVETYPE_FLYGRAVITY, "FLY_GRAVITY" );
    lua_pushenum( L, MOVETYPE_VPHYSICS, "VPHYSICS" );
    lua_pushenum( L, MOVETYPE_PUSH, "PUSH" );
    lua_pushenum( L, MOVETYPE_NOCLIP, "NOCLIP" );
    lua_pushenum( L, MOVETYPE_LADDER, "LADDER" );
    lua_pushenum( L, MOVETYPE_OBSERVER, "OBSERVER" );
    lua_pushenum( L, MOVETYPE_CUSTOM, "CUSTOM" );
    lua_pushenum( L, MOVETYPE_LAST, "LAST" );
    LUA_SET_ENUM_LIB_END( L );
    return 0;
}

/*
** Open MOVECOLLIDE_* enum library
*/
LUALIB_API int luaopen_MOVECOLLIDE( lua_State *L )
{
    LUA_SET_ENUM_LIB_BEGIN( L, LUA_MOVECOLLIDELIBNAME );
    lua_pushenum( L, MOVECOLLIDE_DEFAULT, "DEFAULT" );
    lua_pushenum( L, MOVECOLLIDE_FLY_BOUNCE, "FLY_BOUNCE" );
    lua_pushenum( L, MOVECOLLIDE_FLY_CUSTOM, "FLY_CUSTOM" );
    lua_pushenum( L, MOVECOLLIDE_FLY_SLIDE, "FLY_SLIDE" );
    lua_pushenum( L, MOVECOLLIDE_COUNT, "COUNT" );
    LUA_SET_ENUM_LIB_END( L );
    return 0;
}

/*
** Open SOLID_* enum library
*/
LUALIB_API int luaopen_SOLID( lua_State *L )
{
    LUA_SET_ENUM_LIB_BEGIN( L, LUA_SOLIDLIBNAME );
    lua_pushenum( L, SOLID_NONE, "NONE" );
    lua_pushenum( L, SOLID_BSP, "BSP" );
    lua_pushenum( L, SOLID_BBOX, "AABB" );
    lua_pushenum( L, SOLID_OBB, "OBB" );
    lua_pushenum( L, SOLID_OBB_YAW, "OBB_YAW" );
    lua_pushenum( L, SOLID_CUSTOM, "CUSTOM" );
    lua_pushenum( L, SOLID_VPHYSICS, "VPHYSICS" );
    lua_pushenum( L, SOLID_LAST, "LAST" );
    LUA_SET_ENUM_LIB_END( L );
    return 0;
}

/*
** Open SOLIDFLAG_* enum library
*/
LUALIB_API int luaopen_SOLIDFLAG( lua_State *L )
{
    LUA_SET_ENUM_LIB_BEGIN( L, LUA_SOLIDFLAGLIBNAME );
    lua_pushenum( L, FSOLID_CUSTOMRAYTEST, "CUSTOMRAYTEST" );
    lua_pushenum( L, FSOLID_CUSTOMBOXTEST, "CUSTOMBOXTEST" );
    lua_pushenum( L, FSOLID_NOT_SOLID, "NOT_SOLID" );
    lua_pushenum( L, FSOLID_TRIGGER, "TRIGGER" );
    lua_pushenum( L, FSOLID_NOT_STANDABLE, "NOT_STANDABLE" );
    lua_pushenum( L, FSOLID_VOLUME_CONTENTS, "VOLUME_CONTENTS" );
    lua_pushenum( L, FSOLID_FORCE_WORLD_ALIGNED, "FORCE_WORLD_ALIGNED" );
    lua_pushenum( L, FSOLID_USE_TRIGGER_BOUNDS, "USE_TRIGGER_BOUNDS" );
    lua_pushenum( L, FSOLID_ROOT_PARENT_ALIGNED, "ROOT_PARENT_ALIGNED" );
    lua_pushenum( L, FSOLID_TRIGGER_TOUCH_DEBRIS, "TRIGGER_TOUCH_DEBRIS" );
    LUA_SET_ENUM_LIB_END( L );
    return 0;
}

/*
** Open LIFE_* enum library
*/
LUALIB_API int luaopen_LIFE( lua_State *L )
{
    LUA_SET_ENUM_LIB_BEGIN( L, LUA_LIFELIBNAME );
    lua_pushenum( L, LIFE_ALIVE, "ALIVE" );
    lua_pushenum( L, LIFE_DYING, "DYING" );
    lua_pushenum( L, LIFE_DEAD, "DEAD" );
    lua_pushenum( L, LIFE_RESPAWNABLE, "RESPAWNABLE" );
    lua_pushenum( L, LIFE_DISCARDBODY, "DISCARDBODY" );
    LUA_SET_ENUM_LIB_END( L );
    return 0;
}

/*
** Open EF_* enum library
*/
LUALIB_API int luaopen_EF( lua_State *L )
{
    LUA_SET_ENUM_LIB_BEGIN( L, LUA_EFLIBNAME );
    lua_pushenum( L, EF_BONEMERGE, "BONE_MERGE" );
    lua_pushenum( L, EF_BRIGHTLIGHT, "BRIGHT_LIGHT" );
    lua_pushenum( L, EF_DIMLIGHT, "DIM_LIGHT" );
    lua_pushenum( L, EF_NOINTERP, "NO_INTERPOLATE" );
    lua_pushenum( L, EF_NOSHADOW, "NO_SHADOW" );
    lua_pushenum( L, EF_NODRAW, "NO_DRAW" );
    lua_pushenum( L, EF_NORECEIVESHADOW, "NO_RECEIVE_SHADOW" );
    lua_pushenum( L, EF_BONEMERGE_FASTCULL, "BONE_MERGE_FAST_CULL" );
    lua_pushenum( L, EF_ITEM_BLINK, "ITEM_BLINK" );
    lua_pushenum( L, EF_PARENT_ANIMATES, "PARENT_ANIMATES" );
    LUA_SET_ENUM_LIB_END( L );
    return 0;
}

/*
** Open FL_EDICT_* enum library
*/
LUALIB_API int luaopen_FL_EDICT( lua_State *L )
{
    LUA_SET_ENUM_LIB_BEGIN( L, LUA_FLEDICTLIBNAME );
    lua_pushenum( L, FL_EDICT_CHANGED, "CHANGED" );
    lua_pushenum( L, FL_EDICT_FREE, "FREE" );
    lua_pushenum( L, FL_EDICT_FULL, "FULL" );
    lua_pushenum( L, FL_EDICT_FULLCHECK, "FULL_CHECK" );
    lua_pushenum( L, FL_EDICT_ALWAYS, "ALWAYS" );
    lua_pushenum( L, FL_EDICT_DONTSEND, "DONT_SEND" );
    lua_pushenum( L, FL_EDICT_PVSCHECK, "PVS_CHECK" );
    lua_pushenum( L, FL_EDICT_PENDING_DORMANT_CHECK, "PENDING_DORMANT_CHECK" );
    lua_pushenum( L, FL_EDICT_DIRTY_PVS_INFORMATION, "DIRTY_PVS_INFORMATION" );
    LUA_SET_ENUM_LIB_END( L );
    return 0;
}
