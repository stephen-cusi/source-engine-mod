//========== HL2SB ===========//
//
// Purpose: Server-side undo stack for entities created by the spawnmenu
//          console commands (ent_create / prop_physics_create).
//
//          The stack itself is Garry's Mod's own Lua module
//          (lua/includes/modules/undo.lua), which is the single source of
//          truth for undo.  This header only declares the bridge the spawn
//          commands use to register a freshly created entity with it, so
//          scripts and the engine share ONE stack.
//
//          The previous version kept a second, private C++ stack here and
//          also registered the `hl2sb_undo` / `hl2sb_undoclear` console
//          commands.  Because the Lua module registers the same command names,
//          one silently shadowed the other and the command could end up
//          draining an empty stack -- that is gone now; undo lives entirely in
//          Lua with GMod's names (`undo`, `gmod_undo`, `gmod_undonum`).
//
//===========================================================================//

#ifndef HL2SB_UNDO_H
#define HL2SB_UNDO_H

class CBasePlayer;
class CBaseEntity;

//-----------------------------------------------------------------------------
// Register a freshly spawned entity as one undoable action for the given
// player.  Called by CC_Ent_Create / CC_Prop_Physics_Create after the entity
// is spawned.  Implemented by calling undo.Create / AddEntity / SetPlayer /
// Finish in Lua.  pOwner may be NULL (console), in which case nothing is
// recorded -- GMod's undo.Finish() drops ownerless actions too.
//-----------------------------------------------------------------------------
void HL2SB_UndoRecord( CBasePlayer *pOwner, CBaseEntity *pEnt, const char *pszLabel );
// 2-arg overload: pre-existing translation units (props.cpp, baseentity.cpp)
// were compiled against it -- keep the symbol alive instead of forcing a full
// rebuild for a label parameter.
void HL2SB_UndoRecord( CBasePlayer *pOwner, CBaseEntity *pEnt );

#endif // HL2SB_UNDO_H
