#include "cbase.h"
#include "luamanager.h"
#include "luasrclib.h"
#include "lbaseflex_shared.h"
#include "lbaseentity_shared.h"
#include "lbaseplayer_shared.h"
#include "mathlib/lvector.h"
#include "lvphysics_interface.h"

// memdbgon must be the last include file in a .cpp file!!!
#include "tier0/memdbgon.h"
#include <model_types.h>

#ifdef CLIENT_DLL
static CClientSideEntityManager s_ClientSideEntityManager;
CClientSideEntityManager *g_pClientSideEntityManager = &s_ClientSideEntityManager;

CClientSideEntityManager::CClientSideEntityManager()
{
}

CClientSideEntityManager::~CClientSideEntityManager()
{
}

lua_CBaseFlex *CClientSideEntityManager::CreateClientSideEntity( const char *pszModelName, RenderGroup_t renderGroup )
{
    lua_CBaseFlex *pClientSideEntity = new lua_CBaseFlex();

    // HL2SB: upstream asks IModelInfo for a clientside model index
    // (GetModelClientSideIndex, an Experiment addition).  The closest thing the
    // shared IModelInfo has is GetModelIndex, and the studiomodel check below turns
    // a stale precache hit into a reload -- which is the behaviour that is wanted.
    int nModelIndex = modelinfo->GetModelIndex( pszModelName );

    model_t *pModel = nullptr;

    // First try get the existing model
    if ( nModelIndex != -1 )
    {
        pModel = ( model_t * )modelinfo->GetModel( nModelIndex );

        // After a map change, the pointer to the model may give an invalid studio model
        // in that case, we need to force the model to be loaded again
        if ( modelinfo->GetStudiomodel( pModel ) == nullptr )
        {
            pModel = nullptr;
        }
    }

    if ( pModel == nullptr )
    {
        // Force loading of the model into memory immediately
        pModel = ( model_t * )engine->LoadModel( pszModelName, true );
        modelinfo->RegisterDynamicModel( pszModelName, true );
    }

    InitClientEntity( pClientSideEntity, pModel, renderGroup );

    if ( pClientSideEntity->InitializeAsClientEntity( pszModelName, renderGroup ) == false )
    {
        pClientSideEntity->Remove();
        DevWarning( "Failed to initialize clientside entity for model %s\n", pszModelName );
        return NULL;
    }

    return pClientSideEntity;
}

void CClientSideEntityManager::InitClientEntity( lua_CBaseFlex *pClientSideEntity, const model_t *pModel, RenderGroup_t renderGroup )
{
    pClientSideEntity->SetModelPointer( pModel );

    // HL2SB: upstream stores the render group in C_BaseEntity::m_RenderGroup.  HL2SB
    // has no such member -- GetRenderGroup() derives it from the render mode and the
    // model's translucency -- and the group is not lost anyway: it is passed straight
    // into InitializeAsClientEntity() below, which hands it to AddToLeafSystem().
    pClientSideEntity->m_nBody = 0;
    pClientSideEntity->m_nSkin = 0;
    pClientSideEntity->SetRenderMode( kRenderNormal );
    pClientSideEntity->m_nRenderFX = kRenderFxNone;
    pClientSideEntity->Interp_SetupMappings( pClientSideEntity->GetVarMapping() );
    pClientSideEntity->SetAbsOrigin( vec3_origin );
}
#endif

/*
** access functions (stack -> C)
*/

LUA_API lua_CBaseFlex *lua_tobaseflex( lua_State *L, int idx )
{
    CBaseHandle *hEntity =
        dynamic_cast< CBaseHandle * >( ( CBaseHandle * )lua_touserdata( L, idx ) );
    if ( hEntity == NULL )
        return NULL;
    IHandleEntity *pEntity = hEntity->Get();
    return dynamic_cast< lua_CBaseFlex * >( pEntity );
}

/*
** push functions (C -> stack)
**
** HL2SB: upstream relies on CBaseFlex::PushLuaInstanceSafe(), which takes the
** metatable from GetMetatableName().  The type is known statically here, so the
** flex metatable is set directly -- that is what keeps the flex methods below
** reachable, since the shared entity push would pick the CBaseAnimating metatable.
*/
LUA_API void lua_pushbaseflex( lua_State *L, lua_CBaseFlex *pEntity )
{
    CBaseHandle *hEntity = ( CBaseHandle * )lua_newuserdata( L, sizeof( CBaseHandle ) );
    hEntity->Set( pEntity );
    LUA_SAFE_SET_METATABLE( L, LUA_CBASEFLEXLIBNAME );
}

LUALIB_API lua_CBaseFlex *luaL_checkbaseflex( lua_State *L, int narg )
{
    lua_CBaseFlex *d = lua_tobaseflex( L, narg );

    if ( d == NULL ) /* avoid extra test when d is not 0 */
        luaL_argerror( L, narg, "CBaseFlex expected, got NULL entity" );

    return d;
}

LUA_REGISTRATION_INIT( CBaseFlex )

LUA_BINDING_BEGIN( CBaseFlex, GetFlexIdByName, "class", "Get flex ID by name." )
{
    lua_CBaseFlex *pEntity = LUA_BINDING_ARGUMENT( luaL_checkbaseflex, 1, "entity" );
    const char *szFlexName = LUA_BINDING_ARGUMENT( luaL_checkstring, 2, "flexName" );

    LocalFlexController_t iFlexController = pEntity->FindFlexController( szFlexName );

    lua_pushinteger( L, iFlexController );
    return 1;
}
LUA_BINDING_END( "integer", "Flex ID." )

LUA_BINDING_BEGIN( CBaseFlex, GetFlexWeight, "class", "Get flex weight." )
{
    lua_CBaseFlex *pEntity = LUA_BINDING_ARGUMENT( luaL_checkbaseflex, 1, "entity" );
    LocalFlexController_t iFlexController = ( LocalFlexController_t )( int )LUA_BINDING_ARGUMENT( luaL_checknumber, 2, "flexId" );

    lua_pushnumber( L, pEntity->GetFlexWeight( iFlexController ) );
    return 1;
}
LUA_BINDING_END( "number", "Flex weight." )

LUA_BINDING_BEGIN( CBaseFlex, SetFlexWeight, "class", "Set flex weight." )
{
    lua_CBaseFlex *pEntity = LUA_BINDING_ARGUMENT( luaL_checkbaseflex, 1, "entity" );
    LocalFlexController_t iFlexController = ( LocalFlexController_t )( int )LUA_BINDING_ARGUMENT( luaL_checknumber, 2, "flexId" );
    float flWeight = LUA_BINDING_ARGUMENT( luaL_checknumber, 3, "weight" );

    pEntity->SetFlexWeight( iFlexController, flWeight );
    return 0;
}
LUA_BINDING_END()

LUA_BINDING_BEGIN( CBaseFlex, HasFlexManipulator, "class", "Check if the entity has flex manipulator." )
{
    lua_CBaseFlex *pEntity = LUA_BINDING_ARGUMENT( luaL_checkbaseflex, 1, "entity" );

    for ( LocalFlexController_t i = LocalFlexController_t( 0 ); i < pEntity->GetNumFlexControllers(); i++ )
    {
        if ( pEntity->GetFlexWeight( i ) != 0.0f )
        {
            lua_pushboolean( L, true );
            return 1;
        }
    }

    lua_pushboolean( L, false );
    return 1;
}
LUA_BINDING_END( "boolean", "True if the entity has flex manipulator." )

// Experiment; We only let CBaseEntity determine equality, which should be fine since they're pointers to the same entity. Disabled:
// LUA_BINDING_BEGIN( CBaseFlex, __eq, "class", "Equality operator." )
//{
//    lua_CBaseFlex *pEntityA = LUA_BINDING_ARGUMENT( luaL_checkbaseflex, 1, "entity" );
//    lua_CBaseFlex *pEntityB = LUA_BINDING_ARGUMENT( luaL_checkbaseflex, 2, "other" );
//
//    lua_pushboolean( L, pEntityA == pEntityB );
//    return 1;
//}
// LUA_BINDING_END( "boolean", "True if the entities are equal." )
//
// LUA_BINDING_BEGIN( CBaseFlex, __tostring, "class", "To string operator." )
//{
//    lua_CBaseFlex *pEntity = LUA_BINDING_ARGUMENT( lua_tobaseflex, 1, "entity" );
//
//    if ( pEntity == NULL )
//        lua_pushstring( L, "NULL" );
//    else
//        lua_pushfstring( L, "CBaseFlex: %d \"%s\"", pEntity->entindex(), pEntity->GetClassname() );
//
//    return 1;
//}
// LUA_BINDING_END( "string", "String representation of the entity." )

LUA_REGISTRATION_INIT( Entities )

#ifdef CLIENT_DLL
LUA_BINDING_BEGIN( Entities, CreateClientEntity, "library", "Create a clientside entity with the specified model.", "client" )
{
    const char *pszModelName = LUA_BINDING_ARGUMENT( luaL_checkstring, 1, "model" );
    int renderGroup = ( int )LUA_BINDING_ARGUMENT_WITH_DEFAULT( luaL_optnumber, 2, RENDER_GROUP_OTHER, "renderGroup" );

    if ( pszModelName == NULL )
    {
        luaL_error( L, "ClientsideModel: Invalid model name" );
        return 0;
    }

    lua_CBaseFlex *pEntity = g_pClientSideEntityManager->CreateClientSideEntity( pszModelName, ( RenderGroup_t )renderGroup );
    lua_pushbaseflex( L, pEntity );

    return 1;
}
LUA_BINDING_END( "Entity", "Clientside model entity." )
#endif

/*
** HL2SB: a clientside model is still an animating entity, so the flex metatable
** answers from its own table first and then hands over to the CBaseAnimating
** __index, which walks on to CBaseEntity -- the same chain CBasePlayer uses.
** Upstream gets this for free from their single-instance metatable inheritance.
*/
static int CBaseFlex___index( lua_State *L )
{
    lua_getmetatable( L, 1 );
    lua_pushvalue( L, 2 );
    lua_rawget( L, -2 );
    if ( !lua_isnil( L, -1 ) )
        return 1;
    lua_pop( L, 2 );

    luaL_getmetatable( L, LUA_BASEANIMATINGLIBNAME );
    if ( lua_istable( L, -1 ) )
    {
        lua_getfield( L, -1, "__index" );
        if ( lua_isfunction( L, -1 ) )
        {
            lua_pushvalue( L, 1 );
            lua_pushvalue( L, 2 );
            lua_call( L, 2, 1 );
            return 1;
        }
    }

    lua_pushnil( L );
    return 1;
}

/*
** HL2SB: GMod entities accept arbitrary field writes, and GMod's own player model
** selector relies on it:
**
**     mdl.Entity.GetPlayerColor = function() return Vector( GetConVarString( "cl_playercolor" ) ) end
**     (garrysmod/gamemodes/sandbox/gamemode/editor_player.lua, UpdateFromConvars)
**
** The CBaseFlex metatable below only ever got an __index chain, so that assignment
** raised
**
**     hl2sb_playermodel_gmod.lua:444: attempt to index a CBaseFlex value (field 'Entity')
**
** and aborted BuildEditor() half way through: the Bodygroups sliders, the colour
** callbacks and the preview tint that follow that line never ran (2026-09-17).
**
** Delegate to CBaseAnimating's __newindex, which stores unknown fields in the entity's
** own Lua table - the same handler a scripted entity gets
** (game/client/lua/lc_baseanimating.cpp, game/server/lua/lbaseanimating.cpp).
*/
static int CBaseFlex___newindex( lua_State *L )
{
    luaL_getmetatable( L, LUA_BASEANIMATINGLIBNAME );

    if ( lua_istable( L, -1 ) )
    {
        lua_getfield( L, -1, "__newindex" );

        if ( lua_isfunction( L, -1 ) )
        {
            lua_pushvalue( L, 1 );
            lua_pushvalue( L, 2 );
            lua_pushvalue( L, 3 );
            lua_call( L, 3, 0 );
            return 0;
        }

        lua_pop( L, 1 ); /* the nil field */
    }

    lua_pop( L, 1 ); /* the metatable */

    /* Unreachable while CBaseAnimating installs the handler; keep the message the
    ** engine used to raise so the failure is still recognisable. */
    luaL_error( L, "attempt to index a CBaseFlex value" );
    return 0;
}

/*
** Open CBaseFlex object
*/
LUALIB_API int luaopen_CBaseFlex_shared( lua_State *L )
{
    LUA_PUSH_NEW_METATABLE( L, LUA_CBASEFLEXLIBNAME );

    LUA_REGISTRATION_COMMIT( CBaseFlex );

    lua_pushvalue( L, -1 );           /* push metatable */
    lua_setfield( L, -2, "__index" ); /* default __index = metatable */
    lua_pushcfunction( L, CBaseFlex___index );
    lua_setfield( L, -2, "__index" ); /* ... replaced by the chain above */
    lua_pushcfunction( L, CBaseFlex___newindex );
    lua_setfield( L, -2, "__newindex" ); /* field writes -> entity's Lua table */
    lua_pushstring( L, "entity" );
    lua_setfield( L, -2, "__type" );
    lua_pop( L, 1 );  /* drop the metatable; the entity table is the return value */

    luaL_register_entity_library( L, Entities_luaRegistry );

    return 1;
}
