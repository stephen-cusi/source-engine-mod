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
LUA_BINDING_BEGIN( CBaseAnimating, GetBoneCount, "class", "Get the amount of bones this entity has in its model." )
{
    lua_CBaseAnimating *pAnimating = LUA_BINDING_ARGUMENT( luaL_checkanimating, 1, "entity" );

    CStudioHdr *pstudiohdr = pAnimating->GetModelPtr();

    if ( !pstudiohdr )
    {
        Warning( "CBaseAnimating::GetBoneCount failed: no model\n" );
        return 0;
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

LUA_BINDING_BEGIN( CBaseAnimating, GetBodyGroupsCount, "class", "Get the number of bodygroups" )
{
    lua_CBaseAnimating *pAnimating = LUA_BINDING_ARGUMENT( luaL_checkanimating, 1, "entity" );

    lua_pushinteger( L, pAnimating->GetNumBodyGroups() );

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

LUA_BINDING_BEGIN( CBaseAnimating, GetBodyGroups, "class", "Get the bodygroup values as a table" )
{
    lua_CBaseAnimating *pAnimating = LUA_BINDING_ARGUMENT( luaL_checkanimating, 1, "entity" );

    int nMaxGroups = pAnimating->GetNumBodyGroups();
    CStudioHdr *pstudiohdr = pAnimating->GetModelPtr();

    if ( !pstudiohdr )
    {
        Warning( "CBaseAnimating::GetBodyGroups failed: no model\n" );
        return 0;
    }

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
// and this fork had no way to ask for it - only SetSkin/GetSkin existed.
// A studio model's skin count is its number of skin families
// (public/studio.h: CStudioHdr::numskinfamilies(), "numskinfamilies" in the header).
// Entities without a studio model answer 1, so "<= 1 skins" reads as "no skin slider".
LUA_BINDING_BEGIN( CBaseAnimating, SkinCount, "class", "Returns the amount of skins the entity has" )
{
    lua_CBaseAnimating *pAnimating = LUA_BINDING_ARGUMENT( luaL_checkanimating, 1, "entity" );
    CStudioHdr *pStudioHdr = pAnimating->GetModelPtr();

    lua_pushinteger( L, ( pStudioHdr != NULL ) ? pStudioHdr->numskinfamilies() : 1 );

    return 1;
}
LUA_BINDING_END( "number", "The amount of skins the entity's model has" )

LUA_BINDING_BEGIN( CBaseAnimating, GetFlexBounds, "class", "Returns the min and max values for the target flex controller" )
{
    lua_CBaseAnimating *pAnimating = LUA_BINDING_ARGUMENT( luaL_checkanimating, 1, "entity" );
    int flexControllerIndex = ( int )LUA_BINDING_ARGUMENT( luaL_checknumber, 2, "flexControllerIndex" );
    LocalFlexController_t iFlexController = ( LocalFlexController_t )flexControllerIndex;
    CStudioHdr *pStudioHdr = pAnimating->GetModelPtr();

    Assert( pStudioHdr );

    mstudioflexcontroller_t *flex = pStudioHdr->pFlexcontroller( iFlexController );

    Assert( flex );

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
    lua_CBaseAnimating *pAnimating = LUA_BINDING_ARGUMENT( luaL_checkanimating, 1, "entity" );

    CStudioHdr *pStudioHdr = pAnimating->GetModelPtr();

    if ( !pStudioHdr )
    {
        lua_pushnil( L );
        return 1;
    }

    const studiohdr_t *pRenderHdr = pStudioHdr->GetRenderHdr();

    IMaterial *pMaterials[128];
    int iMaterialCount = g_pStudioRender->GetMaterialList( ( studiohdr_t * )pRenderHdr, ARRAYSIZE( pMaterials ), pMaterials );

    lua_newtable( L );

    for ( int i = 0; i < iMaterialCount; i++ )
    {
        IMaterial *pMaterial = pMaterials[i];

        if ( !pMaterial )
            continue;

        lua_pushnumber( L, i + 1 );  // 1 indexed
        lua_pushmaterial( L, pMaterial );
        lua_settable( L, -3 );
    }

    lua_pushinteger( L, iMaterialCount );
    return 2;
}
LUA_BINDING_END( "table", "The materials", "integer", "The number of materials" )

LUA_BINDING_BEGIN( CBaseAnimating, GetSubModels, "class", "Get the submodels" )
{
    lua_CBaseAnimating *pAnimating = LUA_BINDING_ARGUMENT( luaL_checkanimating, 1, "entity" );

    CStudioHdr *pStudioHdr = pAnimating->GetModelPtr();

    if ( !pStudioHdr )
    {
        lua_pushnil( L );
        return 1;
    }

    lua_newtable( L );

    for ( int i = 0; i < pStudioHdr->numbodyparts(); i++ )
    {
        mstudiobodyparts_t *pBodyPart = pStudioHdr->pBodypart( i );

        // TODO: Make a nice struct push function for this
        lua_newtable( L );

        lua_pushstring( L, pBodyPart->pszName() );
        lua_setfield( L, -2, "name" );

        lua_pushinteger( L, pBodyPart->modelindex );
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