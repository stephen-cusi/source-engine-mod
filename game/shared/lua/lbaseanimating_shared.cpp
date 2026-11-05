//=============================================================================//
//
// Purpose: CBaseAnimating bindings, ported from Experiment: Source
//          (src/game/shared/lbaseanimating_shared.cpp).
//
//          This is a partial port.  Upstream's file holds 78 bindings and 59 of
//          them already exist in HL2SB's per-realm animating bindings
//          (game/client/lua/lc_baseanimating.cpp, game/server/lua/lbaseanimating.cpp),
//          which are the more complete set (136 methods).  Only the 14 below are
//          new, and they are the ones ported here -- copying the whole file would
//          have overwritten 59 working bindings with their upstream equivalents for
//          no gain.  They are added to the same CBaseAnimating metatable by
//          luaopen_CBaseAnimating_shared().
//
//          Not ported, because HL2SB's C_BaseAnimating/CBaseAnimating has no such
//          API (Experiment: Source added it, and it is a real feature rather than a
//          binding -- GMod's Entity:SetMaterial / GetMaterials):
//            * SetMaterialOverride / GetMaterialOverride
//            * SetSubMaterialOverride / GetSubMaterialOverride
//          and GetRagdollOwner, which needs their ragdoll's GetRagdollPlayer().
//
//=============================================================================//

#include "cbase.h"
#include "luamanager.h"
#include "luasrclib.h"
#include "mathlib/lvector.h"
#include "lbaseentity_shared.h"
#ifdef CLIENT_DLL
#include "lc_baseanimating.h"
#else
#include "lbaseanimating.h"
#endif
#include <istudiorender.h>
#include <materialsystem/limaterial.h>

// memdbgon must be the last include file in a .cpp file!!!
#include "tier0/memdbgon.h"

LUA_REGISTRATION_INIT( CBaseAnimating )

// HL2SB (2026-10-16) GMod compat: the tolerant self-resolution used by the
// bindings below.  GMod keeps ONE entity method table, so any of these methods
// can be reached with a NON-animating self: a trace result (worldspawn, a
// brush entity) resolves them through the metatable chain installed in
// lua/includes/extensions/gmod_globals.lua (setmetatable on the CBaseEntity
// metatable), and the reference bindings guard on GetBaseAnimating() instead
// of raising.  Proven against GMod win64:
//   client.dll 0x180112D40 GetBodyGroups  -> empty table when no animating
//   client.dll 0x1801133A0 GetBoneCount   -> pushes 0
//   client.dll 0x180128B60 SkinCount      -> returns no values
//   server.dll 0x18018D2C0 GetBodyGroups  -> FUN_18018CFA0, empty table
//   server.dll 0x18018D900 GetBoneCount   -> pushes 0
//   server.dll 0x1801A34A0 SkinCount      -> pushes 0 / no values
// The strict luaL_checkanimating raised "CBaseAnimating expected, got NULL
// entity" for those valid-but-not-animating entities, and that raise is what
// aborted the C menu's property build on worldspawn (the bodygroups filter).
static lua_CBaseAnimating *HL2SB_AnimatingArg (lua_State *L, int narg) {
    lua_CBaseEntity *pEntity = lua_toentity(L, narg);
    return (pEntity != NULL) ? pEntity->GetBaseAnimating() : NULL;
}

LUA_BINDING_BEGIN( CBaseAnimating, GetBoneCount, "class", "Get the amount of bones this entity has in its model." )
{
    lua_CBaseAnimating *pAnimating = HL2SB_AnimatingArg( L, 1 );

    // GMod answer for a non-animating self or one without a studio model: 0,
    // not an error and not nil -- scripts compare the result
    // (properties/bone_manipulate.lua:14 `if ( bonecount <= 1 )`).
    if ( pAnimating == NULL )
    {
        lua_pushinteger( L, 0 );
        return 1;
    }

    CStudioHdr *pstudiohdr = pAnimating->GetModelPtr();

    if ( !pstudiohdr )
    {
        lua_pushinteger( L, 0 );
        return 1;
    }

    lua_pushinteger( L, pstudiohdr->numbones() );

    return 1;
}
LUA_BINDING_END( "integer", "The amount of bones" )

LUA_BINDING_BEGIN( CBaseAnimating, GetModelName, "class", "Get the model path of the entity" )
{
    lua_CBaseAnimating *pAnimating = LUA_BINDING_ARGUMENT( luaL_checkanimating, 1, "entity" );

    CStudioHdr *pstudiohdr = pAnimating->GetModelPtr();

    if ( !pstudiohdr )
    {
        lua_pushstring( L, "models/error.mdl" );
    }
    else
    {
        lua_pushstring( L, pstudiohdr->pszName() );
    }

    return 1;
}
LUA_BINDING_END( "string", "The model name" )

// HL2SB (2026-10-16) GMod compat: tolerant self, like the family above.
// GMod's GetNumBodyGroups (client.dll 0x1801199B0, server.dll 0x180193E90)
// pushes 0 when the self is not an animating entity; it never raises.
LUA_BINDING_BEGIN( CBaseAnimating, GetBodyGroupsCount, "class", "Get the number of bodygroups" )
{
    lua_CBaseAnimating *pAnimating = HL2SB_AnimatingArg( L, 1 );

    lua_pushinteger( L, ( pAnimating != NULL ) ? pAnimating->GetNumBodyGroups() : 0 );

    return 1;
}
LUA_BINDING_END( "integer", "The number of bodygroups this model has" )

LUA_BINDING_BEGIN( CBaseAnimating, InvalidateModelCache, "class", "Invalidate the model cache." )
{
    lua_CBaseAnimating *pAnimating = LUA_BINDING_ARGUMENT( luaL_checkanimating, 1, "entity" );

    pAnimating->InvalidateMdlCache();

    return 0;
}
LUA_BINDING_END()


LUA_BINDING_BEGIN( CBaseAnimating, SetBodyGroups, "class", "Set the bodygroup values by the bodygroup string. Each hexadecimal character represents the bodygroup at its index, e.g: 0a00001 sets bodygroup 1 to 10(a) and bodygroup 6 to 1, the rest are set to 0" )
{
    lua_CBaseAnimating *pAnimating = LUA_BINDING_ARGUMENT( luaL_checkanimating, 1, "entity" );

    const char *pBodyGroups = LUA_BINDING_ARGUMENT( luaL_checkstring, 2, "bodyGroupString" );
    int iNumGroups = Q_strlen( pBodyGroups );

    if ( iNumGroups <= 0 )
        return 0;

    int nMaxGroups = pAnimating->GetNumBodyGroups();

    for ( int iGroup = 0; iGroup < iNumGroups && iGroup < nMaxGroups; ++iGroup )
    {
        char c = pBodyGroups[iGroup];
        int iValue = 0;

        if ( c >= '0' && c <= '9' )
        {
            iValue = c - '0';  // 0 - 9
        }
        else if ( c >= 'a' && c <= 'z' )
        {
            iValue = c - 'a' + 10;  // 10 - 35
        }

        pAnimating->SetBodygroup( iGroup, iValue );
    }

    return 0;
}
LUA_BINDING_END()

LUA_BINDING_BEGIN( CBaseAnimating, SetBodygroup, "class", "Sets one bodygroup's value on the entity's model. GMod Entity:SetBodygroup( bodygroup, value )." )
{
    // HL2SB (2026-10-16) GMod compat: tolerant self.  GMod's SetBodygroup
    // (client.dll 0x180122B40) returns with no effect for a non-animating
    // self; the property receiver (lua/autorun/properties/bodygroups.lua:124)
    // writes through this method after its Filter passed, so an entity that
    // lost its animating class between open and click must not raise.
    lua_CBaseAnimating *pAnimating = HL2SB_AnimatingArg( L, 1 );

    if ( pAnimating == NULL )
        return 0;

    pAnimating->SetBodygroup( LUA_BINDING_ARGUMENT( luaL_checkint, 2, "bodyGroup" ),
                              LUA_BINDING_ARGUMENT( luaL_checkint, 3, "value" ) );

    return 0;
}
LUA_BINDING_END()

LUA_BINDING_BEGIN( CBaseAnimating, GetBodyGroupsAsString, "class", "Get the bodygroup values as a string of hexadecimal values. Each hexadecimal character represents the bodygroup at its index, e.g: 0a00001 means bodygroup 1 is 10(a) and bodygroup 6 is 1, the rest are 0" )
{
    lua_CBaseAnimating *pAnimating = LUA_BINDING_ARGUMENT( luaL_checkanimating, 1, "entity" );
    int nMaxGroups = pAnimating->GetNumBodyGroups();

    char *pBodyGroups = ( char * )_alloca( nMaxGroups + 1 );
    pBodyGroups[nMaxGroups] = 0;

    for ( int iGroup = 0; iGroup < nMaxGroups; ++iGroup )
    {
        int iValue = pAnimating->GetBodygroup( iGroup );

        if ( iValue >= 0 && iValue <= 9 )
        {
            pBodyGroups[iGroup] = '0' + iValue;
        }
        else if ( iValue >= 10 && iValue <= 35 )
        {
            pBodyGroups[iGroup] = 'a' + iValue - 10;
        }
        else
        {
            pBodyGroups[iGroup] = '0';
        }
    }

    lua_pushstring( L, pBodyGroups );

    return 1;
}
LUA_BINDING_END( "string", "The bodygroup values as a string of hexadecimal values" )

// HL2SB (2026-10-16) GMod compat: the tolerant variant of the GMod contract,
// proven against the reference binaries: when the self is not an animating
// entity or its studio model is not loaded, GetBodyGroups answers an EMPTY
// TABLE (client.dll 0x180112D40 and server.dll 0x18018D2C0/FUN_18018CFA0 both
// build the empty table on that path), never an error and never nil.  The C
// menu's bodygroups filter (lua/autorun/properties/bodygroups.lua:19) runs on
// worldspawn whenever the player right-clicks the map, and duplicator.lua:187
// relies on the same contract: with the old strict luaL_checkanimating the
// whole property menu build aborted here with "calling 'GetBodyGroups' on bad
// self (CBaseAnimating expected, got NULL entity)".
LUA_BINDING_BEGIN( CBaseAnimating, GetBodyGroups, "class", "Get the bodygroup values as a table" )
{
    lua_CBaseAnimating *pAnimating = HL2SB_AnimatingArg( L, 1 );

    if ( pAnimating == NULL )
    {
        lua_newtable( L );
        return 1;
    }

    CStudioHdr *pstudiohdr = pAnimating->GetModelPtr();

    if ( !pstudiohdr )
    {
        lua_newtable( L );
        return 1;
    }

    int nMaxGroups = pAnimating->GetNumBodyGroups();

    lua_newtable( L );

    for ( int iGroup = 0; iGroup < nMaxGroups; ++iGroup )
    {
        // TODO: Make a nice push struct function for this
        lua_newtable( L );

        lua_pushinteger( L, iGroup );
        lua_setfield( L, -2, "id" );

        lua_pushstring( L, pAnimating->GetBodygroupName( iGroup ) );
        lua_setfield( L, -2, "name" );

        int subGroupCount = pAnimating->GetBodygroupCount( iGroup );
        lua_pushinteger( L, subGroupCount );
        lua_setfield( L, -2, "num" );

        // zero indexed table of names in the smd mesh file indicating what valid subgroup values are
        lua_newtable( L );

        mstudiobodyparts_t *pbodypart = pstudiohdr->pBodypart( iGroup );

        for ( int iSubGroup = 0; iSubGroup < subGroupCount; ++iSubGroup )
        {
            mstudiomodel_t *submodel = pbodypart->pModel( iSubGroup );
            lua_pushstring( L, submodel->pszName() );
            lua_rawseti( L, -2, iSubGroup );
        }

        lua_setfield( L, -2, "submodels" );

        lua_rawseti( L, -2, iGroup );
    }

    return 1;
}
LUA_BINDING_END( "table", "The bodygroup values" )

LUA_BINDING_BEGIN( CBaseAnimating, SetSkin, "class", "Set the skin of the entity" )
{
    lua_CBaseAnimating *pAnimating = LUA_BINDING_ARGUMENT( luaL_checkanimating, 1, "entity" );
    int iSkin = LUA_BINDING_ARGUMENT( luaL_checknumber, 2, "skin" );

    pAnimating->m_nSkin = iSkin;

    return 0;
}
LUA_BINDING_END()

// HL2SB: GMod's Entity:SkinCount (https://wiki.facepunch.com/gmod/Entity:SkinCount).
// The skin slider of the player model editor builds itself from it
// (garrysmod/gamemodes/sandbox/gamemode/editor_player.lua:205: mdl.Entity:SkinCount()),
// HL2SB (2026-10-16) GMod compat: tolerant self, like the family above.
// GMod's SkinCount (client.dll 0x180128B60) returns NO values when the self is
// not an animating entity (the reference's lua/usermessages consumers read
// that as nil), and pushes 0 when the animating entity has no usable studio
// model (the 0x180128CAF exit is lua_pushnumber(0)); "model usable" is what
// the reference tests before reading numskinfamilies at studiohdr+0xE0.
LUA_BINDING_BEGIN( CBaseAnimating, SkinCount, "class", "Returns the amount of skins the entity has" )
{
    lua_CBaseAnimating *pAnimating = HL2SB_AnimatingArg( L, 1 );

    if ( pAnimating == NULL )
        return 0;

    CStudioHdr *pStudioHdr = pAnimating->GetModelPtr();

    lua_pushinteger( L, ( pStudioHdr != NULL ) ? pStudioHdr->numskinfamilies() : 0 );

    return 1;
}
LUA_BINDING_END( "number", "The amount of skins the entity's model has" )

// HL2SB: the per-group bodygroup readers used to live in the client-side
// binding file only (game/client/lua/lc_baseanimating.cpp), so GMod code
// running on the server (gamemode SetModel hooks, duplicator, sandbox menus)
// saw GetBodygroup / GetNumBodyGroups / GetBodygroupCount / GetBodygroupName /
// FindBodygroupByName as nil.  GMod registers all of them on BOTH realms
// (server.dll and client.dll both carry the methods), so they live here where
// the registration feeds both metatables.
LUA_BINDING_BEGIN( CBaseAnimating, GetBodygroup, "class", "Get the entity's bodygroup value for the given bodygroup id" )
{
    // HL2SB (2026-10-16) GMod compat: tolerant self, like the family above.
    // GMod's GetBodygroup (client.dll 0x180112FA0, server.dll 0x18018D520)
    // returns NO values for a non-animating self; the engine call itself is
    // null-model safe (game/shared/animation.cpp: GetBodygroup returns 0).
    lua_CBaseAnimating *pAnimating = HL2SB_AnimatingArg( L, 1 );

    if ( pAnimating == NULL )
        return 0;

    int iGroup = LUA_BINDING_ARGUMENT( luaL_checkint, 2, "bodygroup" );

    lua_pushinteger( L, pAnimating->GetBodygroup( iGroup ) );

    return 1;
}
LUA_BINDING_END( "integer", "The bodygroup value" )

// HL2SB (2026-10-16) GMod compat: tolerant self, like the family above.
// GMod's GetNumBodyGroups (client.dll 0x1801199B0, server.dll 0x180193E90)
// pushes 0 for a non-animating self.
LUA_BINDING_BEGIN( CBaseAnimating, GetNumBodyGroups, "class", "Returns the number of bodygroups on the entity's model" )
{
    lua_CBaseAnimating *pAnimating = HL2SB_AnimatingArg( L, 1 );

    lua_pushinteger( L, ( pAnimating != NULL ) ? pAnimating->GetNumBodyGroups() : 0 );

    return 1;
}
LUA_BINDING_END( "integer", "The number of bodygroups" )

// HL2SB (2026-10-16) GMod compat: tolerant self, like the family above.
// GMod's GetBodygroupCount (client.dll 0x180112D70) pushes 0 for a
// non-animating self.
LUA_BINDING_BEGIN( CBaseAnimating, GetBodygroupCount, "class", "Returns how many values the given bodygroup of the entity's model has" )
{
    lua_CBaseAnimating *pAnimating = HL2SB_AnimatingArg( L, 1 );

    if ( pAnimating == NULL )
    {
        lua_pushinteger( L, 0 );
        return 1;
    }

    int iGroup = LUA_BINDING_ARGUMENT( luaL_checkint, 2, "bodygroup" );

    lua_pushinteger( L, pAnimating->GetBodygroupCount( iGroup ) );

    return 1;
}
LUA_BINDING_END( "integer", "The number of values the bodygroup has" )

// HL2SB (2026-10-16) GMod compat: tolerant self, like the family above.
// GMod's GetBodygroupName (client.dll 0x180112E90) returns NO values for a
// non-animating self; an animating entity without a model answers "" (the
// engine's ::GetBodygroupName null path).
LUA_BINDING_BEGIN( CBaseAnimating, GetBodygroupName, "class", "Returns the name of the given bodygroup of the entity's model" )
{
    lua_CBaseAnimating *pAnimating = HL2SB_AnimatingArg( L, 1 );

    if ( pAnimating == NULL )
        return 0;

    int iGroup = LUA_BINDING_ARGUMENT( luaL_checkint, 2, "bodygroup" );

    lua_pushstring( L, pAnimating->GetBodygroupName( iGroup ) );

    return 1;
}
LUA_BINDING_END( "string", "The name of the bodygroup" )

// HL2SB (2026-10-16) GMod compat: tolerant self, like the family above.
// GMod's FindBodygroupByName (client.dll 0x180111520) returns NO values for a
// non-animating self; an animating entity without a model answers -1 (the
// engine's ::FindBodygroupByName null path).
LUA_BINDING_BEGIN( CBaseAnimating, FindBodygroupByName, "class", "Returns the bodygroup id with the given name on the entity's model" )
{
    lua_CBaseAnimating *pAnimating = HL2SB_AnimatingArg( L, 1 );

    if ( pAnimating == NULL )
        return 0;

    const char *pName = LUA_BINDING_ARGUMENT( luaL_checkstring, 2, "name" );

    lua_pushinteger( L, pAnimating->FindBodygroupByName( pName ) );

    return 1;
}
LUA_BINDING_END( "integer", "The bodygroup id" )

// HL2SB (2026-10-16) GMod compat: Entity:GetBoneParent( bone ) -- the studio
// bone's parent index, -1 when there is no parent or the bone is out of
// range.  The bone-manipulation widget walks parents to accumulate inherited
// scales and drags from this value
// (lua/autorun/properties/bone_manipulate.lua:166/242/226); those widget
// methods run in BOTH realms, and the property menu's server Receive chain
// re-runs the same file, so the method must exist on both metatables.
// Reference behavior (GMod win64): client.dll 0x1801138C0 and server.dll
// 0x18018DDC0 are the same shape -- a non-animating self or an out-of-range
// bone answers -1, never an error.  This used to live client-side only.
LUA_BINDING_BEGIN( CBaseAnimating, GetBoneParent, "class", "Get the parent bone index of the given bone" )
{
    lua_CBaseAnimating *pAnimating = HL2SB_AnimatingArg( L, 1 );

    if ( pAnimating == NULL )
    {
        lua_pushinteger( L, -1 );
        return 1;
    }

    int nBone = LUA_BINDING_ARGUMENT( luaL_checkint, 2, "bone" );

    CStudioHdr *pStudioHdr = pAnimating->GetModelPtr();

    if ( pStudioHdr == NULL || nBone < 0 || nBone >= pStudioHdr->numbones() )
    {
        lua_pushinteger( L, -1 );
        return 1;
    }

    lua_pushinteger( L, pStudioHdr->pBone( nBone )->parent );

    return 1;
}
LUA_BINDING_END( "integer", "The parent bone index, -1 for a root bone" )

// HL2SB (2026-10-16) GMod compat: Entity:BoneHasFlag( bone, flag ) -- whether
// the studio bone carries the given BONE_USED_BY_* flag.  The bone-manipulation
// widget filters the drawable bones with it
// (lua/entities/widget_bones.lua:154 `if ( !ent:BoneHasFlag( k,
// BONE_USED_BY_VERTEX_LOD0 ) ) then continue end`), and that widget's Setup
// runs from the property's server Receive chain, so the method must exist on
// both metatables.
// Reference behavior (GMod win64 client.dll 0x18010FD20 / server.dll
// 0x18018A200): a non-animating self or an out-of-range bone returns NO values;
// otherwise it pushes (boneflags[ bone ] & flag) != 0.
LUA_BINDING_BEGIN( CBaseAnimating, BoneHasFlag, "class", "Returns whether the given bone has the given flag set" )
{
    lua_CBaseAnimating *pAnimating = HL2SB_AnimatingArg( L, 1 );

    if ( pAnimating == NULL )
        return 0;

    int nBone = LUA_BINDING_ARGUMENT( luaL_checkint, 2, "bone" );

    CStudioHdr *pStudioHdr = pAnimating->GetModelPtr();

    if ( pStudioHdr == NULL || nBone < 0 || nBone >= pStudioHdr->numbones() )
        return 0;

    int nFlag = LUA_BINDING_ARGUMENT( luaL_checkint, 3, "flag" );

    lua_pushboolean( L, ( pStudioHdr->boneFlags( nBone ) & nFlag ) != 0 );

    return 1;
}
LUA_BINDING_END( "boolean", "Whether the bone has the flag" )

// HL2SB (2026-10-16) GMod compat: Entity:BoneLength( bone ) -- the distance
// between the bone and its parent bone.  The bone-manipulation widget sizes
// every bone button from it (lua/entities/widget_bones.lua:100/166), same
// widget chain as above.
// Reference behavior (GMod win64 client.dll 0x1801100E0 / server.dll
// 0x18018A5D0): a non-animating self or an out-of-range bone returns NO
// values; a bone with no parent (parent < 1) answers 0; otherwise the
// distance between the two bone positions, computed through the same
// GetBonePosition the GetBonePosition binding uses.
LUA_BINDING_BEGIN( CBaseAnimating, BoneLength, "class", "Returns the length of the bone (distance between the bone and its parent)" )
{
    lua_CBaseAnimating *pAnimating = HL2SB_AnimatingArg( L, 1 );

    if ( pAnimating == NULL )
        return 0;

    int nBone = LUA_BINDING_ARGUMENT( luaL_checkint, 2, "bone" );

    CStudioHdr *pStudioHdr = pAnimating->GetModelPtr();

    if ( pStudioHdr == NULL || nBone < 0 || nBone >= pStudioHdr->numbones() )
        return 0;

    const int nParent = pStudioHdr->pBone( nBone )->parent;

    if ( nParent < 1 )
    {
        lua_pushinteger( L, 0 );
        return 1;
    }

    Vector vecBonePos, vecParentPos;
    QAngle angDummy;
    pAnimating->GetBonePosition( nBone, vecBonePos, angDummy );
    pAnimating->GetBonePosition( nParent, vecParentPos, angDummy );

    lua_pushnumber( L, ( vecParentPos - vecBonePos ).Length() );

    return 1;
}
LUA_BINDING_END( "number", "The length of the bone" )

LUA_BINDING_BEGIN( CBaseAnimating, GetFlexBounds, "class", "Returns the min and max values for the target flex controller" )
{
    lua_CBaseAnimating *pAnimating = LUA_BINDING_ARGUMENT( luaL_checkanimating, 1, "entity" );
    int flexControllerIndex = ( int )LUA_BINDING_ARGUMENT( luaL_checknumber, 2, "flexControllerIndex" );
    LocalFlexController_t iFlexController = ( LocalFlexController_t )flexControllerIndex;
    CStudioHdr *pStudioHdr = pAnimating->GetModelPtr();

    if (!pStudioHdr || flexControllerIndex < 0 || flexControllerIndex >= pStudioHdr->numflexcontrollers())
        return 0;
    mstudioflexcontroller_t *flex = pStudioHdr->pFlexcontroller( iFlexController );

    lua_pushnumber( L, flex->min );
    lua_pushnumber( L, flex->max );

    return 2;
}
LUA_BINDING_END( "number", "The min value", "number", "The max value" )

LUA_BINDING_BEGIN( CBaseAnimating, GetFlexCount, "class", "Get the number of flex controllers" )
{
    lua_CBaseAnimating *pAnimating = LUA_BINDING_ARGUMENT( luaL_checkanimating, 1, "entity" );

    lua_pushinteger( L, pAnimating->GetNumFlexControllers() );

    return 1;
}
LUA_BINDING_END( "integer", "The number of flex controllers" )

LUA_BINDING_BEGIN( CBaseAnimating, GetFlexName, "class", "Get the flex controller name by the flex controller index" )
{
    lua_CBaseAnimating *pAnimating = LUA_BINDING_ARGUMENT( luaL_checkanimating, 1, "entity" );
    int flexControllerIndex = ( int )LUA_BINDING_ARGUMENT( luaL_checknumber, 2, "flexControllerIndex" );
    LocalFlexController_t iFlexController = ( LocalFlexController_t )flexControllerIndex;

    lua_pushstring( L, pAnimating->GetFlexControllerName( iFlexController ) );

    return 1;
}
LUA_BINDING_END( "string", "The flex controller name" )





LUA_BINDING_BEGIN( CBaseAnimating, GetMaterials, "class", "Get the materials" )
{
    extern int HL2SB_EntityGetMaterials( lua_State *L );
    return HL2SB_EntityGetMaterials( L );
}
LUA_BINDING_END( "table", "The materials" )

LUA_BINDING_BEGIN( CBaseAnimating, GetSubModels, "class", "Get the submodels" )
{
    lua_CBaseEntity *pEntity = luaL_checkentity( L, 1 );
    const model_t *pModel = pEntity->GetModel();
    const studiohdr_t *pRenderHdr = pModel && modelinfo ? modelinfo->GetStudiomodel( pModel ) : NULL;
    if ( !pRenderHdr )
        return 0;

    lua_newtable( L );

    for ( int i = 0; i < pRenderHdr->numincludemodels; i++ )
    {
        const mstudiomodelgroup_t *pGroup = pRenderHdr->pModelGroup( i );
        lua_newtable( L );

        lua_pushstring( L, pGroup->pszName() );
        lua_setfield( L, -2, "name" );

        lua_pushinteger( L, i );
        lua_setfield( L, -2, "id" );

        lua_rawseti( L, -2, i + 1 );  // 1 indexed
    }

    return 1;
}
LUA_BINDING_END( "table", "The submodels" )

LUA_BINDING_BEGIN( CBaseAnimating, GetSequences, "class", "Get all sequences the model has." )
{
    lua_CBaseAnimating *pAnimating = LUA_BINDING_ARGUMENT( luaL_checkanimating, 1, "entity" );

    CStudioHdr *pStudioHdr = pAnimating->GetModelPtr();

    if ( !pStudioHdr )
    {
        DevWarning( "CBaseAnimating::GetSequences failed: no model\n" );
        lua_newtable( L );
        return 1;
    }

    lua_newtable( L );

    for ( int i = 0; i < pStudioHdr->GetNumSeq(); i++ )
    {
        mstudioseqdesc_t &seqdesc = pStudioHdr->pSeqdesc( i );

        lua_pushnumber( L, i + 1 );  // 1 indexed
        lua_pushstring( L, seqdesc.pszLabel() );
        lua_settable( L, -3 );
    }

    return 1;
}
LUA_BINDING_END( "table", "The sequences" )


/*
** Adds this file's bindings to the CBaseAnimating metatable that
** luaopen_CBaseAnimating already created (client and server each have their own
** implementation of that library, and both install the same metatable name).
*/
LUALIB_API int luaopen_CBaseAnimating_shared( lua_State *L )
{
    LUA_PUSH_METATABLE_TO_EXTEND( L, LUA_BASEANIMATINGLIBNAME );

    LUA_REGISTRATION_COMMIT( CBaseAnimating );

    return 1;
}
