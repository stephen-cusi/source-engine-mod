//========== HL2SB ===========//
//
// Purpose: Server-side bridge into Garry's Mod's Lua undo module.
//
//          The stack itself is Garry's Mod's own Lua module
//          (lua/includes/modules/undo.lua), which is the single source of
//          truth for undo.  GMod has NO C++ side to undo at all - its
//          lua_shared.dll exports (151 names, audited 2026-09-29) and both
//          game DLLs contain zero undo symbols; the recording lives in
//          gamemodes/sandbox/gamemode/commands.lua and the tool files.
//          In this fork the spawn menu's commands are engine ConCommands
//          (game/server/hl2sb_gm_commands.cpp), so this bridge emits
//          EXACTLY what GMod's Lua call sites emit:
//
//            props (commands.lua:302-305):
//              undo.Create( "prop_physics" )          <- the entity's class
//                undo.SetPlayer( ply )
//                undo.AddEntity( e )
//              undo.Finish( "#prop_physics (<model path>)" )
//              (no SetCustomUndoText; ragdolls/effect props same shape with
//               their own class - commands.lua:165-168, 329-332)
//
//            NPC / SENT / Vehicle (commands.lua:638-644, 876-882, 1120-1124):
//              undo.Create( "NPC" )                    <- generic category, NOT the class
//                undo.SetPlayer( ply )
//                undo.AddEntity( e )
//                undo.SetCustomUndoText( "Undone <display name>" )
//              undo.Finish( "#undo.generic.<npc|entity|vehicle> (<display name>)" )
//
//          The previous C++ stack kept a second, private stack here and also
//          registered the `hl2sb_undo` / `hl2sb_undoclear` console commands.
//          Because the Lua module registers the same command names, one
//          silently shadowed the other and the command could end up draining
//          an empty stack -- that is gone now; undo lives entirely in Lua with
//          GMod's names (`undo`, `gmod_undo`, `gmod_undonum`).
//
//===========================================================================//

#ifndef HL2SB_UNDO_H
#define HL2SB_UNDO_H

class CBasePlayer;
class CBaseEntity;

//-----------------------------------------------------------------------------
// Generic recording for the raw engine spawn commands (ent_create /
// prop_physics_create), which GMod's C++ does not record at all - this is a
// fork-side convenience.  Classifies by the entity's own class: a prop_*
// entity gets GMod's prop grammar, anything else gets the SENT grammar with
// the class name as the display name.  pOwner may be NULL (console), in
// which case nothing is recorded -- GMod's undo.Finish() drops ownerless
// actions too.
//-----------------------------------------------------------------------------
void HL2SB_UndoRecord( CBasePlayer *pOwner, CBaseEntity *pEnt );

// Labelled overload kept for the spawn-menu commands: pszLabel is GMod's
// "nice name" (the registry entry title).  Prop_* entities ignore it the way
// GMod's prop recording ignores the menu title (the model path is shown
// instead); NPC/SENT/Vehicle entities use it as the display name, falling
// back to the class name when empty.
void HL2SB_UndoRecord( CBasePlayer *pOwner, CBaseEntity *pEnt, const char *pszLabel );

//-----------------------------------------------------------------------------
// Explicit GMod-granular recording.  pszKind must be one of "NPC", "SENT",
// "Vehicle" (the undo.Category strings GMod's undo list shows and the
// "#undo.generic.*" translation keys derive from).
//-----------------------------------------------------------------------------
void HL2SB_UndoRecordProp( CBasePlayer *pOwner, CBaseEntity *pEnt );
void HL2SB_UndoRecordGeneric( CBasePlayer *pOwner, CBaseEntity *pEnt,
                              const char *pszKind, const char *pszDisplay );

#endif // HL2SB_UNDO_H
