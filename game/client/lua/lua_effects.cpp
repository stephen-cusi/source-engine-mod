#include "cbase.h"
#include "c_baseanimating.h"
#include "client_thinklist.h"
#include "clientleafsystem.h"
#include "effect_dispatch_data.h"
#include "luamanager.h"
#include "luasrclib.h"
#include "leffect_dispatch_data.h"
#include "lbaseentity_shared.h"
#include "vphysics_interface.h"
#include "view.h"
#include "lua_effects.h"
#include <map>

#include "tier0/memdbgon.h"

static bool s_bLuaDOFMode = false;
static int s_nLuaEffects = 0;
static int s_nLuaEffectDepth = 0;
static std::map<unsigned int, int> s_ParentPhysicsNumbers;

bool HL2SB_LuaDOFMode()
{
	return s_bLuaDOFMode;
}

void HL2SB_ResetLuaDOFMode()
{
	s_bLuaDOFMode = false;
}

int HL2SB_LuaSetDOFMode( lua_State *state )
{
	s_bLuaDOFMode = lua_toboolean( state, 1 ) != 0;
	C_BaseEntityIterator it;
	C_BaseEntity *entity;
	while ( ( entity = it.Next() ) != NULL )
	{
		if ( entity->GetRenderHandle() != INVALID_CLIENT_RENDER_HANDLE )
			ClientLeafSystem()->SetRenderGroup( entity->GetRenderHandle(), entity->GetRenderGroup() );
	}
	return 0;
}

void HL2SB_SetClientParentPhysicsNumber( C_BaseEntity *entity, int number )
{
	if ( !entity || entity->IsMarkedForDeletion() ) return;
	const unsigned int handle = entity->GetRefEHandle().ToInt();
	if ( number < 0 ) s_ParentPhysicsNumbers.erase( handle );
	else s_ParentPhysicsNumbers[handle] = number;
	entity->InvalidatePhysicsRecursive( POSITION_CHANGED | ANGLES_CHANGED );
}

void HL2SB_ClearClientParentPhysicsNumber( C_BaseEntity *entity )
{
	if ( entity ) s_ParentPhysicsNumbers.erase( entity->GetRefEHandle().ToInt() );
}

bool HL2SB_GetClientParentPhysicsTransform( C_BaseEntity *entity, matrix3x4_t &transform )
{
	if ( !entity ) return false;
	std::map<unsigned int, int>::const_iterator entry = s_ParentPhysicsNumbers.find( entity->GetRefEHandle().ToInt() );
	if ( entry == s_ParentPhysicsNumbers.end() ) return false;
	C_BaseEntity *parent = entity->GetMoveParent();
	if ( !parent ) return false;
	IPhysicsObject *objects[VPHYSICS_MAX_OBJECT_LIST_COUNT];
	const int count = parent->VPhysicsGetObjectList( objects, ARRAYSIZE( objects ) );
	IPhysicsObject *physics = entry->second < count ? objects[entry->second] : NULL;
	if ( !physics ) return false;
	Vector position;
	QAngle angles;
	physics->GetPosition( &position, &angles );
	AngleMatrix( angles, position, transform );
	return true;
}

class CLuaNativeEffect : public C_BaseAnimating
{
public:
	CLuaNativeEffect() : m_nRenderRef( LUA_NOREF ), m_nThinkRef( LUA_NOREF ), m_nInitialModelIndex( -1 ), m_bReady( false ), m_bRendering( false ) { ++s_nLuaEffects; }
	virtual ~CLuaNativeEffect()
	{
		if ( L && m_nRenderRef >= 0 ) lua_unref( L, m_nRenderRef );
		if ( L && m_nThinkRef >= 0 ) lua_unref( L, m_nThinkRef );
		--s_nLuaEffects;
	}

	virtual bool ShouldDraw()
	{
		return m_bReady && !IsMarkedForDeletion() &&
			!IsEffectActive( EF_NODRAW ) && GetRenderMode() != kRenderNone;
	}

	virtual bool IsTransparent() { return true; }
	virtual RenderGroup_t GetRenderGroup() { return RENDER_GROUP_TRANSLUCENT_ENTITY; }
	virtual void GetRenderBounds( Vector &mins, Vector &maxs )
	{
		if ( HasScriptedRenderBounds() || GetModelIndex() != m_nInitialModelIndex )
		{
			BaseClass::GetRenderBounds( mins, maxs );
			return;
		}
		mins = CollisionProp()->OBBMins();
		maxs = CollisionProp()->OBBMaxs();
		if ( mins == maxs )
		{
			mins.Init( -32, -32, -32 );
			maxs.Init( 32, 32, 32 );
		}
	}

	void SetInitialModelIndex() { m_nInitialModelIndex = GetModelIndex(); }

	virtual int DrawModel( int flags )
	{
		if ( m_bRendering ) return BaseClass::DrawModel( flags );
		if ( !m_bReady || IsMarkedForDeletion() || !L ) return 0;
		const int top = lua_gettop( L );
		if ( !PushStoredCallback( m_nRenderRef ) )
		{
			lua_settop( L, top );
			return BaseClass::DrawModel( flags );
		}
		lua_pushentity( L, this );
		m_bRendering = true;
		const int result = luasrc_pcall( L, 1, 0, 0 );
		m_bRendering = false;
		lua_settop( L, top );
		return result == 0 ? 1 : 0;
	}

	virtual void ClientThink()
	{
		if ( IsMarkedForDeletion() ) return;
		if ( !L ) { QueueRemoval(); return; }
		if ( s_ParentPhysicsNumbers.find( GetRefEHandle().ToInt() ) != s_ParentPhysicsNumbers.end() )
			InvalidatePhysicsRecursive( POSITION_CHANGED | ANGLES_CHANGED );
		const int top = lua_gettop( L );
		if ( !PushStoredCallback( m_nThinkRef ) )
		{
			lua_settop( L, top );
			QueueRemoval();
			return;
		}
		lua_pushentity( L, this );
		const bool ok = luasrc_pcall( L, 1, 1, 0 ) == 0;
		const bool alive = ok && lua_toboolean( L, -1 ) != 0;
		lua_settop( L, top );
		if ( !alive ) QueueRemoval();
		else if ( !IsMarkedForDeletion() && !m_bReady )
		{
			m_bReady = true;
			if ( GetRenderHandle() != INVALID_CLIENT_RENDER_HANDLE )
				ClientLeafSystem()->RenderableChanged( GetRenderHandle() );
		}
	}

	bool PushCallback( const char *name )
	{
		if ( !L || !lua_isrefvalid( L, m_nTableReference ) ) return false;
		lua_getref( L, m_nTableReference );
		if ( !lua_istable( L, -1 ) ) { lua_pop( L, 1 ); return false; }
		lua_pushstring( L, name );
		lua_rawget( L, -2 );
		lua_remove( L, -2 );
		if ( lua_isfunction( L, -1 ) ) return true;
		lua_pop( L, 1 );
		return false;
	}

	bool PushStoredCallback( int reference )
	{
		if ( !L || reference < 0 ) return false;
		lua_getref( L, reference );
		if ( lua_isfunction( L, -1 ) ) return true;
		lua_pop( L, 1 );
		return false;
	}

	void CacheCallbacks()
	{
		if ( PushCallback( "Render" ) ) m_nRenderRef = luaL_ref( L, LUA_REGISTRYINDEX );
		if ( PushCallback( "Think" ) ) m_nThinkRef = luaL_ref( L, LUA_REGISTRYINDEX );
	}

	void QueueRemoval()
	{
		if ( !IsMarkedForDeletion() )
			ClientThinkList()->AddToDeleteList( GetClientHandle() );
	}

private:
	int m_nRenderRef;
	int m_nThinkRef;
	int m_nInitialModelIndex;
	bool m_bReady;
	bool m_bRendering;
};

bool HL2SB_RequestLuaEffectRemoval( C_BaseEntity *entity )
{
	CLuaNativeEffect *effect = dynamic_cast<CLuaNativeEffect *>( entity );
	if ( !effect ) return false;
	if ( IsCurrentViewAccessAllowed() )
		luasrc_LuaWarnMsgF( "Entity:Remove cannot be called while rendering\n" );
	else
		effect->QueueRemoval();
	return true;
}

// The original effects module keeps its registered definitions in an upvalue.
// Unknown or replaced implementations fall back to calling effects.Create.
static int HL2SB_LuaEffectRegistered( lua_State *state, const char *name )
{
	const int top = lua_gettop( state );
	lua_getglobal( state, "effects" );
	if ( !lua_istable( state, -1 ) ) { lua_settop( state, top ); return 0; }
	lua_getfield( state, -1, "Create" );
	if ( !lua_isfunction( state, -1 ) ) { lua_settop( state, top ); return 0; }
	for ( int i = 1; ; ++i )
	{
		const char *upvalue = lua_getupvalue( state, -1, i );
		if ( !upvalue ) break;
		if ( !Q_strcmp( upvalue, "EffectList" ) && lua_istable( state, -1 ) )
		{
			lua_pushstring( state, name );
			lua_rawget( state, -2 );
			const bool found = !lua_isnil( state, -1 );
			lua_settop( state, top );
			return found ? 1 : 0;
		}
		lua_pop( state, 1 );
	}
	lua_settop( state, top );
	return -1;
}

bool HL2SB_HasLuaEffect( const char *name )
{
	if ( !L || !name || !name[0] ) return false;
	char lower[128];
	if ( Q_strlen( name ) >= sizeof( lower ) ) return false;
	Q_strncpy( lower, name, sizeof( lower ) );
	Q_strlower( lower );
	const int registered = HL2SB_LuaEffectRegistered( L, lower );
	if ( registered >= 0 ) return registered != 0;
	const int top = lua_gettop( L );
	lua_getglobal( L, "effects" );
	lua_getfield( L, -1, "Create" );
	lua_pushstring( L, lower );
	const bool found = luasrc_pcall( L, 1, 1, 0 ) == 0 && !lua_isnil( L, -1 );
	lua_settop( L, top );
	return found;
}

bool HL2SB_CreateLuaEffect( const char *name, const CEffectData &data )
{
	if ( !L || !name || !name[0] || Q_strlen( name ) >= 128 ) return false;
	char lower[128];
	Q_strncpy( lower, name, sizeof( lower ) );
	Q_strlower( lower );
	if ( HL2SB_LuaEffectRegistered( L, lower ) == 0 ) return false;
	if ( s_nLuaEffectDepth >= 20 || s_nLuaEffects >= 2048 )
	{
		luasrc_LuaWarnMsgF( "Cannot create Lua effect '%s': effect limit reached\n", lower );
		return true;
	}

	++s_nLuaEffectDepth;
	const int top = lua_gettop( L );
	CLuaNativeEffect *effect = new CLuaNativeEffect();
	if ( modelinfo->GetModelIndex( "models/error.mdl" ) == -1 )
	{
		engine->LoadModel( "models/error.mdl", true );
		modelinfo->RegisterDynamicModel( "models/error.mdl", true );
	}
	if ( !effect->InitializeAsClientEntity( "models/error.mdl", RENDER_GROUP_TRANSLUCENT_ENTITY ) )
	{
		delete effect;
		--s_nLuaEffectDepth;
		return true;
	}
	effect->SetClassname( lower );
	effect->SetInitialModelIndex();
	effect->SetAbsOrigin( data.m_vOrigin );
	effect->SetNextClientThink( CLIENT_THINK_ALWAYS );

	lua_getglobal( L, "effects" );
	lua_getfield( L, -1, "Create" );
	lua_remove( L, -2 );
	lua_pushstring( L, lower );
	lua_pushentity( L, effect );
	const bool created = luasrc_pcall( L, 2, 1, 0 ) == 0 && !lua_isnil( L, -1 );
	lua_settop( L, top );
	if ( !created )
	{
		effect->QueueRemoval();
		--s_nLuaEffectDepth;
		return false;
	}

	// Effects publish their legacy entity reference before Init runs.
	lua_pushentity( L, effect );
	lua_pushvalue( L, -1 );
	lua_setfield( L, -2, "Entity" );
	lua_pop( L, 1 );

	if ( effect->PushCallback( "Init" ) )
	{
		lua_pushentity( L, effect );
		CEffectData copy = data;
		lua_pusheffect( L, copy );
		luasrc_pcall( L, 2, 0, 0 );
	}
	effect->CacheCallbacks();
	if ( effect->GetRenderHandle() != INVALID_CLIENT_RENDER_HANDLE )
		ClientLeafSystem()->RenderableChanged( effect->GetRenderHandle() );
	lua_settop( L, top );
	--s_nLuaEffectDepth;
	return true;
}
