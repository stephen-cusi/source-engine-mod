// hl2sb_model_commands.cpp
//
// HL2SB: emptied on 2026-09-17 - the old console front end for the cfg-based
// player model system is gone with it.
//
// It used to register three client commands:
//
//     hl2sb_setmodel <configname>   resolved cfg/playermodel/<name>.cfg and fired
//                                   `cl_playermodel <path>`
//     hl2sb_listmodels              printed the cfg table
//     hl2sb_reloadmodels            re-read cfg/playermodel/
//
// The player model selector (GMod's PlayerEditor, lua/game/client/
// hl2sb_playermodel_gmod.lua) does all of that now: it lists the models
// player_manager knows about and writes cl_playermodel / cl_playerbodygroups /
// cl_playerskin / cl_playercolor directly, so the commands were only a second,
// stale way to change the same things - and the cfg they resolved against is no
// longer required either (the model list comes from scanning models/player/, see
// lua/autorun/client/hl2sb_playermodels.lua, and the engine accepts any
// models/player/ path: game/shared/hl2sb_model_scan.cpp).
//
// The file is kept (and stays in the .vpc) so nobody has to touch the build
// scripts to find out where the commands went.

#include "cbase.h"

// memdbgon must be the last include file in a .cpp file!!!
#include "tier0/memdbgon.h"
