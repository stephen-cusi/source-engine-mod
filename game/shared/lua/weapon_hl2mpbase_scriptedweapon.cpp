//========= Copyright © 1996-2005, Valve Corporation, All rights reserved. ============//
//
// Purpose: 
//
//=============================================================================//

#include "cbase.h"

#if defined( CLIENT_DLL )
	#include "c_hl2mp_player.h"
#else
	#include "hl2mp_player.h"
#endif

#include "weapon_hl2mpbase_scriptedweapon.h"
#include "in_buttons.h"
#include "ammodef.h"
#include "luamanager.h"
#include "luasrclib.h"
#include "lbasecombatweapon_shared.h"
#if defined( CLIENT_DLL ) && defined( LUA_SDK )
#include "cdll_int.h"	// HL2SB: engine->ClientCmd for the NW seed request (hl2sb_nwrequest)
#include "vgui/ISurface.h"
#include "vgui_controls/Controls.h"
#endif
// HL2SB: SWEP:CalcView pushes/reads the Player, Vector and QAngle userdata.
#include "lbaseplayer_shared.h"
// HL2SB: lua_pushentity for the SWEP:Equip dispatch
#include "lbaseentity_shared.h"
#include "mathlib/lvector.h"
// HL2SB: lua_pushtrace(), for SWEP:DoImpactEffect( trace, damageType ).
#include "lgametrace.h"
// HL2SB: MDLCACHE_CRITICAL_SECTION for the direct SendWeaponAnim sequence set.
#include "datacache/imdlcache.h"

// memdbgon must be the last include file in a .cpp file!!!
extern ConVar hl2sb_dev_msgs;

#include "tier0/memdbgon.h"

IMPLEMENT_NETWORKCLASS_ALIASED( HL2MPScriptedWeapon, DT_HL2MPScriptedWeapon )

BEGIN_NETWORK_TABLE( CHL2MPScriptedWeapon, DT_HL2MPScriptedWeapon )
#ifdef CLIENT_DLL
	RecvPropString( RECVINFO( m_iScriptedClassname ) ),
	RecvPropString( RECVINFO( m_szHoldType ) ),
#else
	SendPropString( SENDINFO( m_iScriptedClassname ) ),
	SendPropString( SENDINFO( m_szHoldType ) ),
#endif
END_NETWORK_TABLE()

BEGIN_PREDICTION_DATA( CHL2MPScriptedWeapon )
END_PREDICTION_DATA()

//=========================================================
//	>> CHLSelectFireScriptedWeapon
//=========================================================
BEGIN_DATADESC( CHL2MPScriptedWeapon )
END_DATADESC()
#ifdef CLIENT_DLL
extern ConVar v_viewmodel_fov;
#endif

ConVar sv_defaultdeployspeed( "sv_defaultdeployspeed", "4",
	FCVAR_REPLICATED, "Default deploy speed for weapons",
	true, 0.1f, true, 10.0f );

// LINK_ENTITY_TO_CLASS( weapon_hl2mpbase_scriptedweapon, CHL2MPScriptedWeapon );
// PRECACHE_WEAPON_REGISTER( weapon_hl2mpbase_scriptedweapon );

// These functions replace the macros above for runtime registration of
// scripted weapons.
#ifdef CLIENT_DLL
static C_BaseEntity *CCHL2MPScriptedWeaponFactory( void )
{
	return static_cast< C_BaseEntity * >( new CHL2MPScriptedWeapon );
};
#endif

#ifndef CLIENT_DLL
static CUtlDict< CEntityFactory<CHL2MPScriptedWeapon>*, unsigned short > m_WeaponFactoryDatabase;
#endif

void RegisterScriptedWeapon( const char *className )
{
#ifdef CLIENT_DLL
	if ( GetClassMap().FindFactory( className ) )
	{
		return;
	}

	{
		static int s_nClientRegistered = 0;
		if ( hl2sb_dev_msgs.GetBool() )
		luasrc_LuaInfoMsgF( "[HL2SB wp] REGISTER client #%d '%s'\n", ++s_nClientRegistered, className );
	}

	GetClassMap().Add( className, "CHL2MPScriptedWeapon", sizeof( CHL2MPScriptedWeapon ),
		&CCHL2MPScriptedWeaponFactory, true );
#else
	if ( EntityFactoryDictionary()->FindFactory( className ) )
	{
		return;
	}

	{
		static int s_nServerRegistered = 0;
		if ( hl2sb_dev_msgs.GetBool() )
		luasrc_LuaInfoMsgF( "[HL2SB wp] REGISTER server #%d '%s'\n", ++s_nServerRegistered, className );
	}

	unsigned short lookup = m_WeaponFactoryDatabase.Find( className );
	if ( lookup != m_WeaponFactoryDatabase.InvalidIndex() )
	{
		return;
	}

	// Andrew; This fixes months worth of pain and anguish.
	CEntityFactory<CHL2MPScriptedWeapon> *pFactory = new CEntityFactory<CHL2MPScriptedWeapon>( className );

	lookup = m_WeaponFactoryDatabase.Insert( className, pFactory );
	Assert( lookup != m_WeaponFactoryDatabase.InvalidIndex() );
#endif
	// BUGBUG: When attempting to precache weapons registered during runtime,
	// they don't appear as valid registered entities.
	// static CPrecacheRegister precache_weapon_(&CPrecacheRegister::PrecacheFn_Other, className);
}

void ResetWeaponFactoryDatabase( void )
{
#ifdef CLIENT_DLL
#ifdef LUA_SDK
	GetClassMap().RemoveAllScripted();
#endif
#else
	for ( int i=m_WeaponFactoryDatabase.First(); i != m_WeaponFactoryDatabase.InvalidIndex(); i=m_WeaponFactoryDatabase.Next( i ) )
	{
		delete m_WeaponFactoryDatabase[ i ];
	}
	m_WeaponFactoryDatabase.RemoveAll();
#endif
}


// acttable_t CHL2MPScriptedWeapon::m_acttable[] = 
// {
// 	{ ACT_MP_STAND_IDLE,				ACT_HL2MP_IDLE_PISTOL,					false },
// 	{ ACT_MP_CROUCH_IDLE,				ACT_HL2MP_IDLE_CROUCH_PISTOL,			false },
// 
// 	{ ACT_MP_RUN,						ACT_HL2MP_RUN_PISTOL,					false },
// 	{ ACT_MP_CROUCHWALK,				ACT_HL2MP_WALK_CROUCH_PISTOL,			false },
// 
// 	{ ACT_MP_ATTACK_STAND_PRIMARYFIRE,	ACT_HL2MP_GESTURE_RANGE_ATTACK_PISTOL,	false },
// 	{ ACT_MP_ATTACK_CROUCH_PRIMARYFIRE,	ACT_HL2MP_GESTURE_RANGE_ATTACK_PISTOL,	false },
// 
// 	{ ACT_MP_RELOAD_STAND,				ACT_HL2MP_GESTURE_RELOAD_PISTOL,		false },
// 	{ ACT_MP_RELOAD_CROUCH,				ACT_HL2MP_GESTURE_RELOAD_PISTOL,		false },
// 
// 	{ ACT_MP_JUMP,						ACT_HL2MP_JUMP_PISTOL,					false },
// };

// IMPLEMENT_ACTTABLE( CHL2MPScriptedWeapon );

// HL2SB (2026-10-01): guarded SWEP-field push. The raw three-line
// getref/getfield/remove pattern was an unprotected "attempt to index a nil
// value" whenever m_nTableReference was -2 (never bound) or -1 (weapons.Get
// returned nil) -- which the client hits the moment binding is deferred to the
// networked scripted classname instead of the (possibly wrong) netclass name.
// Always leaves exactly one value on the stack: the field, or nil.
static void lua_pushweaponfield( lua_State *L, int iRef, const char *pszField )
{
	if ( L == NULL || iRef < 0 )
	{
		if ( L != NULL )
			lua_pushnil( L );
		return;
	}
	lua_getref( L, iRef );
	if ( !lua_istable( L, -1 ) )
	{
		lua_pop( L, 1 );
		lua_pushnil( L );
		return;
	}
	lua_getfield( L, -1, pszField );
	lua_remove( L, -2 );
}

// These functions serve as skeletons for the our weapons' actions to be
// implemented in Lua.
acttable_t *CHL2MPScriptedWeapon::ActivityList( void ) {
#ifdef LUA_SDK
	lua_pushweaponfield( L, m_nTableReference, "m_acttable" );
	if ( lua_istable( L, -1 ) )
	{
		for( int i = 0 ; i < LUA_MAX_WEAPON_ACTIVITIES ; i++ )
		{
			lua_pushinteger( L, i );
			lua_gettable( L, -2 );
			if ( lua_istable( L, -1 ) )
			{
				m_acttable[i].baseAct = ACT_INVALID;
				lua_pushinteger( L, 1 );
				lua_gettable( L, -2 );
				if ( lua_isnumber( L, -1 ) )
					m_acttable[i].baseAct = lua_tointeger( L, -1 );
				lua_pop( L, 1 );

				m_acttable[i].weaponAct = ACT_INVALID;
				lua_pushinteger( L, 2 );
				lua_gettable( L, -2 );
				if ( lua_isnumber( L, -1 ) )
					m_acttable[i].weaponAct = lua_tointeger( L, -1 );
				lua_pop( L, 1 );

				m_acttable[i].required = false;
				lua_pushinteger( L, 3 );
				lua_gettable( L, -2 );
				if ( lua_isboolean( L, -1 ) )
					m_acttable[i].required = (bool)lua_toboolean( L, -1 );
				lua_pop( L, 1 );
			}
			lua_pop( L, 1 );
		}
	}
	lua_pop( L, 1 );
#endif
	return m_acttable;
}
int CHL2MPScriptedWeapon::ActivityListCount( void ) { return LUA_MAX_WEAPON_ACTIVITIES; }

//-----------------------------------------------------------------------------
// Purpose: 
//-----------------------------------------------------------------------------
CHL2MPScriptedWeapon::CHL2MPScriptedWeapon( void )
{
	m_szHoldType.GetForModify()[0] = '\0';
	m_szAppliedHoldType[0] = '\0';
	m_bUpdatingHoldType = false;
	m_pLuaWeaponInfo = dynamic_cast< CHL2MPSWeaponInfo* >( CreateWeaponInfo() );

#ifdef LUA_SDK
	// HL2SB: this MUST be initialised.  InitScriptedWeapon() is what normally
	// assigns it, but GetMaxClip1() / GetWeight() / ... are reachable before
	// that -- CBaseCombatWeapon::Weapon_EquipAmmoOnly() calls
	// UsesClipsForAmmo1() -> GetMaxClip1() as soon as a player bumps a dropped
	// weapon.  Leaving the member uninitialised made lua_getref() read a random
	// registry slot, which held a number, so lua_getfield raised
	// "attempt to index a number value" OUTSIDE any protected call -- and with
	// no panic function that aborted the whole server via __fastfail.
	m_nTableReference = LUA_NOREF;
#endif
}

//-----------------------------------------------------------------------------
// Purpose: 
//-----------------------------------------------------------------------------
CHL2MPScriptedWeapon::~CHL2MPScriptedWeapon( void )
{
	delete m_pLuaWeaponInfo;
	// Andrew; This is actually done in CBaseEntity. I'm doing it here because
	// this is the class that initialized the reference.
#ifdef LUA_SDK
	// Only unref a reference that was actually taken: with an uninitialised (or
	// already-freed) value this would free a random registry slot.
	//
	// HL2SB: m_nTableReference is inherited from CBaseEntity and ~CBaseEntity
	// calls lua_unref() on it again after this destructor returns.  Unref'ing
	// twice corrupts the registry free list -- luaL_unref() writes t[ref] = t[0]
	// (a NUMBER) into the slot and re-points the free list at it, so a later
	// luaL_ref() hands the same slot to a second live weapon and lua_getref()
	// then reads a number, which raises "attempt to index a number value" from
	// an unprotected context and aborts the process.  Clearing the member makes
	// the base-class unref a no-op (luaL_unref ignores negative refs), and the
	// L != NULL test covers shutdown, where L has already been cleared.
	if ( L != NULL && m_nTableReference >= 0 )
		lua_unref( L, m_nTableReference );

	m_nTableReference = LUA_NOREF;
#endif
}

extern const char *pWeaponSoundCategories[ NUM_SHOOT_SOUND_TYPES ];

#ifdef CLIENT_DLL
extern ConVar hud_fastswitch;
#endif

	// GMod stores weapon data nested under Primary/Secondary. ALWAYS leaves exactly one value on the
// Lua stack (the caller must lua_pop it once), so the over/underflow bugs in
// the old inline getref/getfield/remove sequences are impossible here.
static int lua_getweaponfield ( lua_State *L, int ref, const char *tblKey, const char *subKey, const char *flatKey )
{
	// HL2SB (2026-10-01): ref < 0 (unbound entity) used to push nil and the
	// very next raw lua_getfield() threw UNPROTECTED from C++ -- this helper
	// backs GetMaxClip1/GetDefaultClip*/ammo lookups, which Precache/Equip/
	// the HUD all call on a weapon the client has not bound yet.
	if ( L == NULL || ref < 0 )
	{
		if ( L != NULL )
			lua_pushnil( L );
		return 1;
	}

	lua_getref( L, ref );                    // [T]
	lua_getfield( L, -1, tblKey );           // [T, tbl]
	if ( lua_istable( L, -1 ) )
	{
		lua_getfield( L, -1, subKey );       // [T, tbl, sub]
		lua_remove( L, -2 );                 // [T, sub]
		if ( !lua_isnil( L, -1 ) )
		{
			lua_remove( L, -2 );             // [sub]
			return 1;
		}
		lua_pop( L, 1 );                     // [T]  (sub was nil)
	}
	else
	{
		lua_pop( L, 1 );                     // [T]  (tbl not a table)
	}
	lua_getfield( L, -1, flatKey );          // [T, flat]
	lua_remove( L, -2 );                     // [flat]
	return 1;
}

// Same dual lookup, as a bool / int.  Used by the engine-side GMod weapon loop
// (SWEP.Primary.Automatic and the clip seeding) so it does not have to care
// whether the script or the loader produced the nested or the flat spelling.
static bool lua_getweaponbool ( lua_State *L, int ref, const char *tblKey, const char *subKey, const char *flatKey, bool bDefault )
{
	lua_getweaponfield( L, ref, tblKey, subKey, flatKey );
	bool bResult = lua_isnil( L, -1 ) ? bDefault : ( lua_toboolean( L, -1 ) != 0 );
	lua_pop( L, 1 );
	return bResult;
}

static int lua_getweaponint ( lua_State *L, int ref, const char *tblKey, const char *subKey, const char *flatKey, int nDefault )
{
	lua_getweaponfield( L, ref, tblKey, subKey, flatKey );
	int nResult = lua_isnumber( L, -1 ) ? (int)lua_tointeger( L, -1 ) : nDefault;
	lua_pop( L, 1 );
	return nResult;
}

static float lua_getweaponfloat ( lua_State *L, int ref, const char *tblKey, const char *subKey, const char *flatKey, float flDefault )
{
	lua_getweaponfield( L, ref, tblKey, subKey, flatKey );
	float flResult = lua_isnumber( L, -1 ) ? (float)lua_tonumber( L, -1 ) : flDefault;
	lua_pop( L, 1 );
	return flResult;
}

void CHL2MPScriptedWeapon::InitScriptedWeapon( void )
{
#if defined ( LUA_SDK )
#ifndef CLIENT_DLL
	// Let the instance reinitialize itself for the client.
	if ( m_pLuaWeaponInfo->bParsedScript )
		return;
#endif

	char className[ MAX_WEAPON_STRING ];
#if defined ( CLIENT_DLL )
	// HL2SB (2026-10-01): bind ONLY to the networked scripted class name.
	// Falling back to GetClassname() here bound the weapon to the WRONG SWEP
	// whenever the client's netclass index resolved to a different scripted
	// name than the server's (measured: a freshly spawned weapon_fists created
	// client-side as 'weapon_nyangun', ran THAT SWEP's Initialize/data pass,
	// and the first equip ended with no viewmodel -- only a weapon switch
	// re-ran the equip machinery over the corrected table). The networked
	// string IS the truth (server: InitScriptedWeapon line above sets it);
	// OnDataChanged retries once it arrives.
	if ( !m_iScriptedClassname.Get() || !m_iScriptedClassname.Get()[0] )
	{
		if ( hl2sb_dev_msgs.GetBool() ) luasrc_LuaInfoMsgF( "[HL2SB wp] CLIENT InitScriptedWeapon DEFERRED (no scripted name yet) ent=%d baseclass='%s'\n",
			entindex(), BaseClass::GetClassname() );
		return;
	}
	Q_strncpy( className, m_iScriptedClassname.Get(), sizeof( className ) );
#else
	Q_strncpy( m_iScriptedClassname.GetForModify(), GetClassname(), sizeof( className ) );
 	Q_strncpy( className, GetClassname(), sizeof( className ) );
#endif
 	Q_strlower( className );
	// Andrew; This redundancy is pretty annoying.
	// Classname
	Q_strncpy( m_pLuaWeaponInfo->szClassName, className, MAX_WEAPON_STRING );
	SetClassname( className );

	const int iTopBeforeWeapon = lua_gettop( L );
	const int iExistingReference = m_nTableReference;
	bool bReusedInstance = false;
	lua_getglobal( L, "weapons" );
	if ( lua_istable( L, -1 ) )
	{
		lua_getfield( L, -1, "Get" );
		if ( lua_isfunction( L, -1 ) )
		{
			lua_remove( L, -2 );
			lua_pushstring( L, className );
			if ( iExistingReference >= 0 && lua_isrefvalid( L, iExistingReference ) )
			{
				lua_getref( L, iExistingReference );
				if ( lua_istable( L, -1 ) )
					bReusedInstance = true;
				else
					lua_pop( L, 1 );
			}
			luasrc_pcall( L, bReusedInstance ? 2 : 1, 1, 0 );
		}
		else
		{
			lua_pop( L, 2 );
		}
	}
	else
	{
		lua_pop( L, 1 );
	}

	// HL2SB (2026-10-01): every path above must leave exactly one value (the
	// weapons.Get result, which is nil when the SWEP is not in this realm's
	// module). The old code then ran luaL_ref() directly over that stack: a
	// nil result became LUA_REFNIL (-1) and every later raw field read threw
	// unprotected from C++, and the "module missing" branches left nothing to
	// ref at all (referencing whatever stale value sat underneath). Unbound
	// entities now keep LUA_NOREF: the guarded getters fall back to the C++
	// defaults and the client's OnDataChanged retries the bind next update.
	if ( lua_gettop( L ) == iTopBeforeWeapon )
		lua_pushnil( L );
	if ( !lua_istable( L, -1 ) )
	{
		lua_pop( L, 1 );
		if ( hl2sb_dev_msgs.GetBool() ) luasrc_LuaInfoMsgF( "[HL2SB wp] InitScriptedWeapon NO TABLE from weapons.Get('%s') ent=%d\n",
			className, entindex() );
		return;
	}
	if ( bReusedInstance )
	{
		lua_getref( L, iExistingReference );
		if ( !lua_rawequal( L, -1, -2 ) )
		{
			const int iSource = lua_absindex( L, -2 );
			const int iInstance = lua_absindex( L, -1 );
			lua_pushnil( L );
			while ( lua_next( L, iSource ) != 0 )
			{
				lua_pushvalue( L, -2 );
				lua_rawget( L, iInstance );
				const bool bMissing = lua_isnil( L, -1 );
				lua_pop( L, 1 );
				if ( bMissing )
				{
					lua_pushvalue( L, -2 );
					lua_pushvalue( L, -2 );
					lua_rawset( L, iInstance );
				}
				lua_pop( L, 1 );
			}
			lua_remove( L, iSource );
		}
		else
		{
			lua_pop( L, 1 );
		}
	}

	// Legacy self aliases belong to the instance before its setup callbacks.
	lua_pushweapon( L, this );
	lua_setfield( L, -2, "Weapon" );
	lua_pushweapon( L, this );
	lua_setfield( L, -2, "Entity" );

	if ( bReusedInstance )
	{
		lua_pop( L, 1 );
	}
	else
	{
		if ( iExistingReference >= 0 && lua_isrefvalid( L, iExistingReference ) )
			lua_unref( L, iExistingReference );
		m_nTableReference = luaL_ref( L, LUA_REGISTRYINDEX );
	}
	// A fresh Lua instance needs its own activity translation table.
	m_szAppliedHoldType[0] = '\0';
#ifndef CLIENT_DLL
	m_pLuaWeaponInfo->bParsedScript = true;
#endif
	// Printable name
	lua_pushweaponfield( L, m_nTableReference, "PrintName" );
	if ( lua_isstring( L, -1 ) )
	{
		Q_strncpy( m_pLuaWeaponInfo->szPrintName, lua_tostring( L, -1 ), MAX_WEAPON_STRING );
	}
	else
	{
		Q_strncpy( m_pLuaWeaponInfo->szPrintName, WEAPON_PRINTNAME_MISSING, MAX_WEAPON_STRING );
	}
	lua_pop( L, 1 );
	// View model & world model.
	lua_pushweaponfield( L, m_nTableReference, "ViewModel" );
	if ( lua_isstring( L, -1 ) )
	{
		Q_strncpy( m_pLuaWeaponInfo->szViewModel, lua_tostring( L, -1 ), MAX_WEAPON_STRING );
	}
	lua_pop( L, 1 );
	lua_pushweaponfield( L, m_nTableReference, "WorldModel" );
	if ( lua_isstring( L, -1 ) )
	{
		Q_strncpy( m_pLuaWeaponInfo->szWorldModel, lua_tostring( L, -1 ), MAX_WEAPON_STRING );
	}
	lua_pop( L, 1 );
	lua_pushweaponfield( L, m_nTableReference, "anim_prefix" );
	if ( lua_isstring( L, -1 ) )
	{
		Q_strncpy( m_pLuaWeaponInfo->szAnimationPrefix, lua_tostring( L, -1 ), MAX_WEAPON_PREFIX );
	}
	lua_pop( L, 1 );
	lua_pushweaponfield( L, m_nTableReference, "Slot" );
	if ( lua_isnumber( L, -1 ) )
	{
		m_pLuaWeaponInfo->iSlot = lua_tonumber( L, -1 );
	}
	else
	{
		m_pLuaWeaponInfo->iSlot = 0;
	}
	lua_pop( L, 1 );
	lua_pushweaponfield( L, m_nTableReference, "SlotPos" );
	if ( lua_isnumber( L, -1 ) )
	{
		m_pLuaWeaponInfo->iPosition = lua_tonumber( L, -1 );
	}
	else
	{
		m_pLuaWeaponInfo->iPosition = 0;
	}
	lua_pop( L, 1 );
	lua_getweaponfield( L, m_nTableReference, "Primary", "ClipSize", "" );
	if ( lua_isnumber( L, -1 ) )
	{
		m_pLuaWeaponInfo->iMaxClip1 = lua_tonumber( L, -1 );					// Max primary clips gun can hold (assume they don't use clips by default)
	}
	else
	{
		m_pLuaWeaponInfo->iMaxClip1 = WEAPON_NOCLIP;
	}
	lua_pop( L, 1 );
	lua_getweaponfield( L, m_nTableReference, "Secondary", "ClipSize", "" );
	if ( lua_isnumber( L, -1 ) )
	{
		m_pLuaWeaponInfo->iMaxClip2 = lua_tonumber( L, -1 );					// Max secondary clips gun can hold (assume they don't use clips by default)
	}
	else
	{
		m_pLuaWeaponInfo->iMaxClip2 = WEAPON_NOCLIP;
	}
	lua_pop( L, 1 );
	lua_getweaponfield( L, m_nTableReference, "Primary", "DefaultClip", "" );
	if ( lua_isnumber( L, -1 ) )
	{
		m_pLuaWeaponInfo->iDefaultClip1 = lua_tonumber( L, -1 );		// amount of primary ammo placed in the primary clip when it's picked up
	}
	else
	{
		m_pLuaWeaponInfo->iDefaultClip1 = m_pLuaWeaponInfo->iMaxClip1;
	}
	lua_pop( L, 1 );
	lua_getweaponfield( L, m_nTableReference, "Secondary", "DefaultClip", "" );
	if ( lua_isnumber( L, -1 ) )
	{
		m_pLuaWeaponInfo->iDefaultClip2 = lua_tonumber( L, -1 );		// amount of secondary ammo placed in the secondary clip when it's picked up
	}
	else
	{
		m_pLuaWeaponInfo->iDefaultClip2 = m_pLuaWeaponInfo->iMaxClip2;
	}
	lua_pop( L, 1 );
	lua_pushweaponfield( L, m_nTableReference, "Weight" );
	if ( lua_isnumber( L, -1 ) )
	{
		m_pLuaWeaponInfo->iWeight = lua_tonumber( L, -1 );
	}
	else
	{
		m_pLuaWeaponInfo->iWeight = 0;
	}
	lua_pop( L, 1 );

	// Scripted weapons inherit the global deploy speed unless they specify one.
	lua_pushweaponfield( L, m_nTableReference, "DeploySpeed" );
	m_flDeploySpeed = MAX( 0.1f, lua_isnumber( L, -1 )
		? (float)lua_tonumber( L, -1 ) : sv_defaultdeployspeed.GetFloat() );
	lua_pop( L, 1 );

	lua_pushweaponfield( L, m_nTableReference, "rumble" );
	if ( lua_isnumber( L, -1 ) )
	{
		m_pLuaWeaponInfo->iWeight = lua_tonumber( L, -1 );
	}
	else
	{
		m_pLuaWeaponInfo->iWeight = -1;
	}
	lua_pop( L, 1 );
	
	lua_pushweaponfield( L, m_nTableReference, "showusagehint" );
	if ( lua_isnumber( L, -1 ) )
	{
		m_pLuaWeaponInfo->bShowUsageHint = (int)lua_tointeger( L, -1 ) != 0 ? true : false;
	}
	else
	{
		m_pLuaWeaponInfo->bShowUsageHint = false;
	}
	lua_pop( L, 1 );
	lua_getref( L, m_nTableReference );
	// HL2SB GMod SWEP compat: SWEP.AutoSwitchTo (GMod's name, usually a
	// boolean) wins; the flat HL2SB autoswitchto key is the fallback.
	lua_getfield( L, -1, "AutoSwitchTo" );
	lua_remove( L, -2 );
	if ( lua_isnil( L, -1 ) )
	{
		lua_pop( L, 1 );
		lua_pushboolean( L, true );
	}
	if ( ( lua_isboolean( L, -1 ) && !lua_toboolean( L, -1 ) )
	     || ( lua_isnumber( L, -1 ) && (int)lua_tointeger( L, -1 ) == 0 ) )
	{
		m_pLuaWeaponInfo->bAutoSwitchTo = false;
	}
	else
	{
		m_pLuaWeaponInfo->bAutoSwitchTo = true;
	}
	lua_pop( L, 1 );
	lua_getref( L, m_nTableReference );
	lua_getfield( L, -1, "AutoSwitchFrom" );
	lua_remove( L, -2 );
	if ( lua_isnil( L, -1 ) )
	{
		lua_pop( L, 1 );
		lua_pushboolean( L, true );
	}
	if ( ( lua_isboolean( L, -1 ) && !lua_toboolean( L, -1 ) )
	     || ( lua_isnumber( L, -1 ) && (int)lua_tointeger( L, -1 ) == 0 ) )
	{
		m_pLuaWeaponInfo->bAutoSwitchFrom = false;
	}
	else
	{
		m_pLuaWeaponInfo->bAutoSwitchFrom = true;
	}
	lua_pop( L, 1 );
	lua_pushweaponfield( L, m_nTableReference, "BuiltRightHanded" );
	if ( lua_isnumber( L, -1 ) )
	{
		m_pLuaWeaponInfo->m_bBuiltRightHanded = (int)lua_tointeger( L, -1 ) != 0 ? true : false;
	}
	else
	{
		m_pLuaWeaponInfo->m_bBuiltRightHanded = true;
	}
	lua_pop( L, 1 );
	lua_pushweaponfield( L, m_nTableReference, "AllowFlipping" );
	if ( lua_isnumber( L, -1 ) )
	{
		m_pLuaWeaponInfo->m_bAllowFlipping = (int)lua_tointeger( L, -1 ) != 0 ? true : false;
	}
	else
	{
		m_pLuaWeaponInfo->m_bAllowFlipping = true;
	}
	lua_pop( L, 1 );

	// GMod SWEP compat: SWEP.ViewModelFlip marks a viewmodel authored mirrored
	// (left-handed).  GMod's engine flips such models so they render
	// right-handed; the stock "The Ultimate Admin Gun" sets ViewModelFlip = true
	// and ships a left-handed v_ model, which is why the pistol came up
	// left-handed.  HL2SB's equivalent switch is
	// C_BaseViewModel::ShouldFlipViewModel(), which compares
	// FileWeaponInfo_t::m_bBuiltRightHanded against cl_righthand, so map the GMod
	// field onto it.  GMod SWEPs never set BuiltRightHanded/AllowFlipping, so
	// ViewModelFlip wins whenever the script provides it.
	lua_pushweaponfield( L, m_nTableReference, "ViewModelFlip" );
	if ( lua_isboolean( L, -1 ) || lua_isnumber( L, -1 ) )
	{
		bool bFlippedModel = lua_isboolean( L, -1 ) ? ( lua_toboolean( L, -1 ) != 0 )
													: ( lua_tointeger( L, -1 ) != 0 );
		m_pLuaWeaponInfo->m_bBuiltRightHanded = !bFlippedModel;
		m_pLuaWeaponInfo->m_bAllowFlipping = true;
	}
	lua_pop( L, 1 );

	lua_getweaponfield( L, m_nTableReference, "Primary", "MeleeWeapon", "" );
	if ( lua_isnumber( L, -1 ) )
	{
		m_pLuaWeaponInfo->m_bMeleeWeapon = (int)lua_tointeger( L, -1 ) != 0 ? true : false;
	}
	else
	{
		m_pLuaWeaponInfo->m_bMeleeWeapon = false;
	}
	lua_pop( L, 1 );

	// Primary ammo used.
	lua_getweaponfield( L, m_nTableReference, "Primary", "Ammo", "" );
	if ( lua_isstring( L, -1 ) )
	{
		const char *pAmmo = lua_tostring( L, -1 );
		if ( strcmp("None", pAmmo) == 0 )
			Q_strncpy( m_pLuaWeaponInfo->szAmmo1, "", sizeof( m_pLuaWeaponInfo->szAmmo1 ) );
		else
			Q_strncpy( m_pLuaWeaponInfo->szAmmo1, pAmmo, sizeof( m_pLuaWeaponInfo->szAmmo1 )  );
		m_pLuaWeaponInfo->iAmmoType = GetAmmoDef()->Index( m_pLuaWeaponInfo->szAmmo1 );
	}
	lua_pop( L, 1 );
	
	// Secondary ammo used
	lua_getweaponfield( L, m_nTableReference, "Secondary", "Ammo", "" );
	if ( lua_isstring( L, -1 ) )
	{
		const char *pAmmo = lua_tostring( L, -1 );
		if ( strcmp("None", pAmmo) == 0)
			Q_strncpy( m_pLuaWeaponInfo->szAmmo2, "", sizeof( m_pLuaWeaponInfo->szAmmo2 ) );
		else
			Q_strncpy( m_pLuaWeaponInfo->szAmmo2, pAmmo, sizeof( m_pLuaWeaponInfo->szAmmo2 )  );
		m_pLuaWeaponInfo->iAmmo2Type = GetAmmoDef()->Index( m_pLuaWeaponInfo->szAmmo2 );
	}
	lua_pop( L, 1 );

	// Now read the weapon sounds
	memset( m_pLuaWeaponInfo->aShootSounds, 0, sizeof( m_pLuaWeaponInfo->aShootSounds ) );
	lua_pushweaponfield( L, m_nTableReference, "SoundData" );
	if ( lua_istable( L, -1 ) )
	{
		for ( int i = EMPTY; i < NUM_SHOOT_SOUND_TYPES; i++ )
		{
			lua_getfield( L, -1, pWeaponSoundCategories[i] );
			if ( lua_isstring( L, -1 ) )
			{
				const char *soundname = lua_tostring( L, -1 );
				if ( soundname && soundname[0] )
				{
					Q_strncpy( m_pLuaWeaponInfo->aShootSounds[i], soundname, MAX_WEAPON_STRING );
				}
			}
			lua_pop( L, 1 );
		}
	}
	lua_pop( L, 1 );

	lua_getweaponfield( L, m_nTableReference, "Primary", "Damage", "" );
	if ( lua_isnumber( L, -1 ) )
	{
		m_pLuaWeaponInfo->m_iPlayerDamage = (int)lua_tointeger( L, -1 );
	}
	lua_pop( L, 1 );

	extern void HL2SB_InstallScriptDataTables(lua_State *, CBaseEntity *);
	HL2SB_InstallScriptDataTables(L,this);

	BEGIN_LUA_CALL_WEAPON_METHOD( "Initialize" );
	END_LUA_CALL_WEAPON_METHOD( 0, 0 );
	UpdateHoldType();
#endif
}

#ifdef CLIENT_DLL
static void HL2SB_EnsureInitialWeaponDeploy( CHL2MPScriptedWeapon *pWeapon )
{
#ifdef LUA_SDK
	if ( L == NULL || pWeapon->m_nTableReference < 0 )
		return;

	CBasePlayer *pOwner = ToBasePlayer( pWeapon->GetOwner() );
	if ( !pOwner || pOwner != CBasePlayer::GetLocalPlayer()
		|| pOwner->GetActiveWeapon() != pWeapon || !pOwner->IsAlive()
		|| !pOwner->GetViewModel( pWeapon->m_nViewModelIndex, false ) )
		return;

	lua_getref( L, pWeapon->m_nTableReference );
	if ( !lua_istable( L, -1 ) )
	{
		lua_pop( L, 1 );
		return;
	}
	lua_pushliteral( L, "__hl2sb_client_deploy_attempted" );
	lua_rawget( L, -2 );
	const bool bAttempted = lua_toboolean( L, -1 ) != 0;
	lua_pop( L, 2 );
	if ( !bAttempted )
		pWeapon->Deploy();
#endif
}

void CHL2MPScriptedWeapon::Simulate()
{
	CHandle<CHL2MPScriptedWeapon> guard = this;
	BaseClass::Simulate();
	if ( guard.Get() != this || L == NULL || m_nTableReference < 0 )
		return;
	UpdateHoldType();
	CBasePlayer *pOwner = ToBasePlayer( GetOwner() );
	if ( !pOwner || !pOwner->IsAlive() || pOwner->GetActiveWeapon() != this )
		return;
	if ( pOwner == CBasePlayer::GetLocalPlayer() && !pOwner->GetViewModel( m_nViewModelIndex, false ) )
		return;

	HL2SB_EnsureInitialWeaponDeploy( this );
	if ( guard.Get() != this )
		return;
	lua_getref( L, m_nTableReference );
	if ( !lua_istable( L, -1 ) )
	{
		lua_pop( L, 1 );
		return;
	}
	lua_pushliteral( L, "__hl2sb_client_think_frame" );
	lua_rawget( L, -2 );
	const bool bAlreadyThought = lua_isnumber( L, -1 ) && lua_tointeger( L, -1 ) == gpGlobals->framecount;
	lua_pop( L, 1 );
	if ( bAlreadyThought )
	{
		lua_pop( L, 1 );
		return;
	}
	lua_pushliteral( L, "__hl2sb_client_think_frame" );
	lua_pushinteger( L, gpGlobals->framecount );
	lua_rawset( L, -3 );
	lua_pop( L, 1 );
	BEGIN_LUA_CALL_WEAPON_METHOD( "Think" );
	END_LUA_CALL_WEAPON_METHOD( 0, 0 );
}

void CHL2MPScriptedWeapon::OnDataChanged( DataUpdateType_t updateType )
{
	BaseClass::OnDataChanged( updateType );

	// HL2SB (2026-10-01): bind exactly once, and only from the networked
	// scripted class name (the base netclass index can resolve to a DIFFERENT
	// scripted weapon on the client -- see the InitScriptedWeapon guard). If
	// the string has not arrived by CREATED, retry on the next update.
	// ref semantics: -2 never bound, -1 weapons.Get returned nil, >=0 bound.
	if ( !m_pLuaWeaponInfo->bParsedScript
	  && m_iScriptedClassname.Get() && m_iScriptedClassname.Get()[0] )
	{
		if ( hl2sb_dev_msgs.GetBool() ) luasrc_LuaInfoMsgF( "[HL2SB wp] CLIENT weapon bind ent=%d update=%d baseclass='%s' scripted='%s' refBefore=%d\n",
			entindex(), (int)updateType, BaseClass::GetClassname(), m_iScriptedClassname.Get(), (int)m_nTableReference );

		SetClassname( m_iScriptedClassname.Get() );
		InitScriptedWeapon();

		// HL2SB: only mark the info parsed when the bind actually took -- if
		// weapons.Get had no entry for this realm yet, retry on the next update
		// (bParsedScript is what keeps this block from running again).
		if ( m_nTableReference >= 0 )
		{
			m_pLuaWeaponInfo->bParsedScript = true;

#ifdef LUA_SDK
			BEGIN_LUA_CALL_WEAPON_METHOD( "Precache" );
			END_LUA_CALL_WEAPON_METHOD( 0, 0 );
#endif

			if ( hl2sb_dev_msgs.GetBool() ) luasrc_LuaInfoMsgF( "[HL2SB wp] CLIENT weapon bound ent=%d refAfter=%d vmidx=%d\n",
				entindex(), (int)m_nTableReference, (int)m_iViewModelIndex );
		}
	}

	// A server-confirmed switch also reaches clients that did not predict it.
	// Notify Lua without holstering the replacement weapon's shared viewmodel.
	// The on-target state is also active; use the owner's selected weapon.
	CBasePlayer *pOwner = ToBasePlayer( GetOwner() );
	if ( L != NULL && m_nTableReference >= 0 && pOwner != NULL &&
		pOwner == CBasePlayer::GetLocalPlayer() && pOwner->GetActiveWeapon() != this )
	{
		lua_getref( L, m_nTableReference );
		if ( lua_istable( L, -1 ) )
		{
			lua_pushliteral( L, "__hl2sb_client_deploy_attempted" );
			lua_rawget( L, -2 );
			const bool bDeployed = lua_toboolean( L, -1 ) != 0;
			lua_pop( L, 1 );
			if ( bDeployed )
			{
				lua_pushliteral( L, "__hl2sb_client_deploy_attempted" );
				lua_pushboolean( L, false );
				lua_rawset( L, -3 );
				lua_pop( L, 1 );
				BEGIN_LUA_CALL_WEAPON_METHOD( "Holster" );
					lua_pushweapon( L, pOwner->GetActiveWeapon() );
				END_LUA_CALL_WEAPON_METHOD( 1, 0 );
			}
			else
				lua_pop( L, 1 );
		}
		else
			lua_pop( L, 1 );
	}

	// Network activation can precede the client's scripted weapon binding.
	HL2SB_EnsureInitialWeaponDeploy( this );
}

const char *CHL2MPScriptedWeapon::GetScriptedClassname( void )
{
	if ( m_iScriptedClassname.Get() )
		return m_iScriptedClassname.Get();
	return BaseClass::GetClassname();
}
#endif

void CHL2MPScriptedWeapon::Precache( void )
{
	BaseClass::Precache();

	InitScriptedWeapon();

	// Get the ammo indexes for the ammo's specified in the data file
	// GMod: Primary.Ammo = "none" means "this weapon uses no ammo" -- skip
	// silently instead of logging an undefined-ammo error every spawn.
	if ( GetWpnData().szAmmo1[0] && Q_stricmp( GetWpnData().szAmmo1, "none" ) )
	{
		m_iPrimaryAmmoType = GetAmmoDef()->Index( GetWpnData().szAmmo1 );
		if (m_iPrimaryAmmoType == -1)
		{
			Msg("ERROR: Weapon (%s) using undefined primary ammo type (%s)\n",GetClassname(), GetWpnData().szAmmo1);
		}
	}
	if ( GetWpnData().szAmmo2[0] && Q_stricmp( GetWpnData().szAmmo2, "none" ) )
	{
		m_iSecondaryAmmoType = GetAmmoDef()->Index( GetWpnData().szAmmo2 );
		if (m_iSecondaryAmmoType == -1)
		{
			Msg("ERROR: Weapon (%s) using undefined secondary ammo type (%s)\n",GetClassname(),GetWpnData().szAmmo2);
		}
	}

	// Precache models (preload to avoid hitch)
	m_iViewModelIndex = 0;
	m_iWorldModelIndex = 0;
	if ( GetViewModel() && GetViewModel()[0] )
	{
		m_iViewModelIndex = CBaseEntity::PrecacheModel( GetViewModel() );
	}
	if ( GetWorldModel() && GetWorldModel()[0] )
	{
		m_iWorldModelIndex = CBaseEntity::PrecacheModel( GetWorldModel() );
	}

	// Precache sounds, too
	for ( int i = 0; i < NUM_SHOOT_SOUND_TYPES; ++i )
	{
		const char *shootsound = GetShootSound( i );
		if ( shootsound && shootsound[0] )
		{
			CBaseEntity::PrecacheScriptSound( shootsound );
		}
	}

#if defined ( LUA_SDK ) && !defined( CLIENT_DLL )
	BEGIN_LUA_CALL_WEAPON_METHOD( "Precache" );
	END_LUA_CALL_WEAPON_METHOD( 0, 0 );

	// HL2SB (2026-10-01) probe: server-side bind outcome (this is the ONLY
	// server bind site). Remove after the fists diagnosis.
	if ( hl2sb_dev_msgs.GetBool() ) luasrc_LuaInfoMsgF( "[HL2SB wp] SERVER weapon precache ent=%d class='%s' ref=%d vm='%s' wm='%s'\n",
		entindex(), GetClassname(), (int)m_nTableReference, GetViewModel(), GetWorldModel() );
#endif
}


//-----------------------------------------------------------------------------
// Purpose: Get my data in the file weapon info array
//-----------------------------------------------------------------------------
const FileWeaponInfo_t &CHL2MPScriptedWeapon::GetWpnData( void ) const
{
	return *m_pLuaWeaponInfo;
}

const char *CHL2MPScriptedWeapon::GetViewModel( int ) const
{
#if defined ( LUA_SDK )
	lua_pushweaponfield( L, m_nTableReference, "ViewModel" );

	RETURN_LUA_STRING();
#endif

	return BaseClass::GetViewModel();
}

const char *CHL2MPScriptedWeapon::GetWorldModel( void ) const
{
#if defined ( LUA_SDK )
	lua_pushweaponfield( L, m_nTableReference, "WorldModel" );

	RETURN_LUA_STRING();
#endif

	return BaseClass::GetWorldModel();
}

const char *CHL2MPScriptedWeapon::GetAnimPrefix( void ) const
{
#if defined ( LUA_SDK )
	lua_pushweaponfield( L, m_nTableReference, "anim_prefix" );

	RETURN_LUA_STRING();
#endif

	return BaseClass::GetAnimPrefix();
}

bool CHL2MPScriptedWeapon::UseHands( void ) const
{
#if defined ( LUA_SDK )
	lua_pushweaponfield( L, m_nTableReference, "UseHands" );

	if ( lua_isboolean( L, -1 ) )
	{
		bool bUseHands = lua_toboolean( L, -1 ) != 0;
		lua_pop( L, 1 );
		return bUseHands;
	}
	lua_pop( L, 1 );
#endif

	return false;
}

//-----------------------------------------------------------------------------
// HL2SB GMod SWEP compat: the weapon selection HUD takes the icon straight off
// the SWEP.  GMod spells it IconOverride (material path) or WepSelectIcon, and
// the two spellings are used for two DIFFERENT things:
//
//   * a STRING is a material path ("nyan/selection.png");
//   * a Material() object is what weapon_nyangun sets:
//
//         SWEP.WepSelectIcon = Material( "nyan/selection.png" )
//
//     which on this fork is the Lua proxy table from
//     lua/includes/extensions/gmod_surface.lua; it keeps its path in __path and
//     mat:GetName() returns the same string.
//
// The old version returned lua_tostring() of a value whose stack slot it had
// already popped -- a pointer the GC could collect before the HUD copied it --
// and it ignored every non-string, so the Material() form silently fell through
// to the paper "weapons/swep" icon.  The classic WepSelectIcon spelling as a
// surface.GetTextureID() numbers are resolved through the native texture dictionary.
// ("killicon/ak47_beast").
//-----------------------------------------------------------------------------
const char *CHL2MPScriptedWeapon::GetWepSelectIcon( void ) const
{
#if defined ( LUA_SDK )
	if ( L == NULL || m_nTableReference < 0 )
		return NULL;

	static const char *s_pIconKeys[] = { "IconOverride", "WepSelectIcon" };

	// The caller (HL2SB_DrawWeaponSelectIcon in hud_weaponselection.cpp) copies
	// this immediately, so one file-static buffer is enough -- and it keeps the
	// string alive past the lua_pop() below, which the old code did not.
	static char s_szWepSelectIcon[MAX_PATH];

	for ( int i = 0; i < ARRAYSIZE( s_pIconKeys ); ++i )
	{
		lua_getref( L, m_nTableReference );
		if ( !lua_istable( L, -1 ) )
		{
			lua_pop( L, 1 );
			return NULL;
		}

		lua_getfield( L, -1, s_pIconKeys[i] );
		lua_remove( L, -2 );

		bool bHaveIcon = false;

		if ( lua_type( L, -1 ) == LUA_TSTRING )
		{
			Q_strncpy( s_szWepSelectIcon, lua_tostring( L, -1 ), sizeof( s_szWepSelectIcon ) );
			bHaveIcon = true;
		}
		else if ( lua_type( L, -1 ) == LUA_TNUMBER )
		{
#ifdef CLIENT_DLL
			const int texture = (int)lua_tonumber( L, -1 );
			bHaveIcon = vgui::surface()->DrawGetTextureFile( texture, s_szWepSelectIcon, sizeof( s_szWepSelectIcon ) );
#endif
		}
		else if ( lua_istable( L, -1 ) )
		{
			lua_getfield( L, -1, "__path" );
			if ( lua_type( L, -1 ) == LUA_TSTRING )
			{
				Q_strncpy( s_szWepSelectIcon, lua_tostring( L, -1 ), sizeof( s_szWepSelectIcon ) );
				bHaveIcon = true;
			}
			lua_pop( L, 1 );
		}
		else if ( luaL_testudata( L, -1, LUA_MATERIALLIBNAME ) != NULL )
		{
			// The engine's Material() (litexture.cpp HL2SB_Material) returns an
			// IMaterial userdata, not the old proxy table.  Call its GetName().
			lua_getfield( L, -1, "GetName" );
			if ( lua_isfunction( L, -1 ) )
			{
				lua_pushvalue( L, -2 );		// self
				if ( lua_pcall( L, 1, 1, 0 ) == 0 && lua_type( L, -1 ) == LUA_TSTRING )
				{
					Q_strncpy( s_szWepSelectIcon, lua_tostring( L, -1 ), sizeof( s_szWepSelectIcon ) );
					bHaveIcon = true;
				}
			}
			lua_pop( L, 1 );
		}

		lua_pop( L, 1 );

		if ( bHaveIcon && s_szWepSelectIcon[0] )
			return s_szWepSelectIcon;
	}
#endif

	return NULL;
}

const char *CHL2MPScriptedWeapon::GetPrintName( void ) const
{
#if defined ( LUA_SDK )
	lua_pushweaponfield( L, m_nTableReference, "PrintName" );
	if ( lua_isstring( L, -1 ) && !Q_stricmp( lua_tostring( L, -1 ), "#GMOD_Fists" ) )
	{
		lua_pop( L, 1 );
		return "#weapon_fists";
	}

	RETURN_LUA_STRING();
#endif

	return BaseClass::GetPrintName();
}

int CHL2MPScriptedWeapon::GetMaxClip1( void ) const
{
#if defined ( LUA_SDK )
	lua_pushweaponfield( L, m_nTableReference, "Primary.ClipSize" );

	RETURN_LUA_INTEGER();
#endif

	return BaseClass::GetMaxClip1();
}

int CHL2MPScriptedWeapon::GetMaxClip2( void ) const
{
#if defined ( LUA_SDK )
	lua_pushweaponfield( L, m_nTableReference, "Secondary.ClipSize" );

	RETURN_LUA_INTEGER();
#endif

	return BaseClass::GetMaxClip2();
}

int CHL2MPScriptedWeapon::GetDefaultClip1( void ) const
{
#if defined ( LUA_SDK )
	lua_pushweaponfield( L, m_nTableReference, "Primary.DefaultClip" );

	RETURN_LUA_INTEGER();
#endif

	return BaseClass::GetDefaultClip1();
}

int CHL2MPScriptedWeapon::GetDefaultClip2( void ) const
{
#if defined ( LUA_SDK )
	lua_pushweaponfield( L, m_nTableReference, "Secondary.DefaultClip" );

	RETURN_LUA_INTEGER();
#endif

	return BaseClass::GetDefaultClip2();
}


bool CHL2MPScriptedWeapon::IsMeleeWeapon() const
{
#if defined ( LUA_SDK )
	lua_pushweaponfield( L, m_nTableReference, "MeleeWeapon" );

	if ( lua_gettop( L ) > 0 )
	{
		if ( lua_isnumber( L, -1 ) )
		{
			int res = ( (int)lua_tointeger( L, -1 ) != 0 ) ? true : false;
			lua_pop(L, 1);
			return res;
		}
		else
			lua_pop(L, 1);
	}
#endif

	return BaseClass::IsMeleeWeapon();
}

bool CHL2MPScriptedWeapon::DrawAmmo() const
{
#if defined (LUA_SDK)
	// HL2SB (2026-10-01): the old body had NO return on two paths - an unbound
	// table (lua_getref on LUA_NOREF) and a SWEP that does not spell out the
	// field. RETURN_LUA_BOOLEAN() pops and falls through when the value is not
	// a boolean, so the function ended at its closing brace: the return value
	// was whatever the last called routine left in AL. For the weapon_medkit
	// (GMod default DrawAmmo = true - the field is simply absent) the answer
	// came out false and CHudAmmo::UpdatePlayerAmmo hid the whole ammo block,
	// which is why the medkit's charge number never showed.
	// GMod's documented default is TRUE; only an explicit false hides.
	if ( m_nTableReference < 0 || L == NULL )
		return true;

	lua_getref(L, m_nTableReference );
	if ( !lua_istable( L, -1 ) )
	{
		lua_pop( L, 1 );
		return true;
	}
	lua_getfield( L, -1, "DrawAmmo");
	lua_remove(L, -2);

	if ( lua_isboolean( L, -1 ) )
	{
		bool bDraw = lua_toboolean( L, -1 ) != 0;
		lua_pop( L, 1 );
		return bDraw;
	}
	lua_pop( L, 1 );
	return true;
#else
	return true;
#endif
}

int CHL2MPScriptedWeapon::GetWeight( void ) const
{
#if defined ( LUA_SDK )
	lua_pushweaponfield( L, m_nTableReference, "Weight" );

	RETURN_LUA_INTEGER();
#endif

	return BaseClass::GetWeight();
}

bool CHL2MPScriptedWeapon::AllowsAutoSwitchTo( void ) const
{
#if defined ( LUA_SDK )
	lua_pushweaponfield( L, m_nTableReference, "AutoSwitchTo" );

	if ( lua_gettop( L ) > 0 )
	{
		if ( lua_isnumber( L, -1 ) )
		{
			int res = ( (int)lua_tointeger( L, -1 ) != 0 ) ? true : false;
			lua_pop(L, 1);
			return res;
		}
		else
			lua_pop(L, 1);
	}
#endif

	return BaseClass::AllowsAutoSwitchTo();
}

#ifdef CLIENT_DLL
int CHL2MPScriptedWeapon::GetFOV( void ) const
{
#if defined (LUA_SDK)
	lua_pushweaponfield( L, m_nTableReference, "ViewModelFOV" );

	RETURN_LUA_INTEGER();
#endif
	//return BaseClass:GetFOV();
}
#endif

bool CHL2MPScriptedWeapon::AllowsAutoSwitchFrom( void ) const
{
#if defined ( LUA_SDK )
	lua_pushweaponfield( L, m_nTableReference, "AutoSwitchFrom" );

	if ( lua_gettop( L ) > 0 )
	{
		if ( lua_isnumber( L, -1 ) )
		{
			int res = ( (int)lua_tointeger( L, -1 ) != 0 ) ? true : false;
			lua_pop(L, 1);
			return res;
		}
		else
			lua_pop(L, 1);
	}
#endif

	return BaseClass::AllowsAutoSwitchFrom();
}

bool CHL2MPScriptedWeapon::IsSpawnable( void ) const
{
#ifdef LUA_SDK
	// HL2SB (2026-10-01): the old body had the same no-return-at-the-end
	// undefined value as DrawAmmo() (field missing -> garbage). GMod's C++
	// "Spawnable" key defaults to false when absent.
	lua_pushweaponfield( L, m_nTableReference, "Spawnable" );

	if ( lua_isboolean( L, -1 ) )
	{
		bool bSpawnable = lua_toboolean( L, -1 ) != 0;
		lua_pop( L, 1 );
		return bSpawnable;
	}
	lua_pop( L, 1 );
	return false;
#else
	return false;
#endif
}

int CHL2MPScriptedWeapon::GetWeaponFlags( void ) const
{
#if defined ( LUA_SDK )
	lua_pushweaponfield( L, m_nTableReference, "item_flags" );
	RETURN_LUA_INTEGER();
#endif

	return BaseClass::GetWeaponFlags();
}

int CHL2MPScriptedWeapon::GetSlot( void ) const
{
#if defined ( LUA_SDK )
	lua_pushweaponfield( L, m_nTableReference, "Slot" );

	RETURN_LUA_INTEGER();
#endif

	return BaseClass::GetSlot();
}

int CHL2MPScriptedWeapon::GetPosition( void ) const
{
#if defined ( LUA_SDK )
	lua_pushweaponfield( L, m_nTableReference, "SlotPos" );

	RETURN_LUA_INTEGER();
#endif

	return BaseClass::GetPosition();
}

const Vector &CHL2MPScriptedWeapon::GetBulletSpread( void )
{
	static Vector cone = VECTOR_CONE_3DEGREES;
	return cone;
}

//-----------------------------------------------------------------------------
// Purpose: 
//
//
//-----------------------------------------------------------------------------
void CHL2MPScriptedWeapon::PrimaryAttack( void )
{
#if defined ( LUA_SDK )
	BEGIN_LUA_CALL_WEAPON_METHOD( "PrimaryAttack" );
	END_LUA_CALL_WEAPON_METHOD( 0, 0 );
#endif

	// HL2SB (2026-10-01) probe: was the fire button ever routed into the Lua
	// SWEP on the first equip? -2/-1 ref = the script never ran. Remove after
	// the fists diagnosis.
	if ( hl2sb_dev_msgs.GetBool() ) luasrc_LuaInfoMsgF( "[HL2SB wp] %s PrimaryAttack ent=%d class='%s' ref=%d\n",
#ifdef CLIENT_DLL
		"CLIENT",
#else
		"SERVER",
#endif
		entindex(), GetClassname(), (int)m_nTableReference );
}

void CHL2MPScriptedWeapon::SecondaryAttack( void )
{
#if defined ( LUA_SDK )
	BEGIN_LUA_CALL_WEAPON_METHOD( "SecondaryAttack" );
	END_LUA_CALL_WEAPON_METHOD( 0, 0 );
#endif
}

//-----------------------------------------------------------------------------
// Purpose: 
// Input  : &info - 
//-----------------------------------------------------------------------------
void CHL2MPScriptedWeapon::FireBullets( const FireBulletsInfo_t &info )
{
	m_flLastShootTime = gpGlobals->curtime;
	if(CBasePlayer *pPlayer = ToBasePlayer ( GetOwner() ) )
	{
		pPlayer->FireBullets(info);
	}
}

//-----------------------------------------------------------------------------
// Purpose: GMod calls SWEP:DoImpactEffect( trace, damageType ) for every bullet
//          impact, and a `true` return vetoes the engine's own impact effect.
//
// HL2SB: nothing ever dispatched this, so a scripted weapon's impacts silently
// fell through to the stock HL2 impact and the SWEP's impact effect -- which for
// weapon_nyangun is the entire visible result of shooting something
// (util.Effect( "rb655_nyan_bounce" )) -- never ran at all, with no error.
//-----------------------------------------------------------------------------
void CHL2MPScriptedWeapon::DoImpactEffect( trace_t &tr, int nDamageType )
{
#if defined ( LUA_SDK )
	BEGIN_LUA_CALL_WEAPON_METHOD( "DoImpactEffect" );
		lua_pushtrace( L, tr );
		lua_pushinteger( L, nDamageType );
	END_LUA_CALL_WEAPON_METHOD( 2, 1 );

	// The END macro guarantees exactly one result (or nil) on top of the stack.
	if ( lua_isboolean( L, -1 ) )
	{
		const bool bHandled = lua_toboolean( L, -1 ) != 0;
		lua_pop( L, 1 );
		if ( bHandled )
			return;
	}
	else
	{
		lua_pop( L, 1 );
	}
#endif

	BaseClass::DoImpactEffect( tr, nDamageType );
}

//-----------------------------------------------------------------------------
// Purpose: GMod's source of a bullet tracer's effect name.
//
// HL2SB: a SWEP passes its tracer effect as bullet.TracerName
// (weapon_nyangun: bullet.TracerName = "rb655_nyan_tracer"), but the engine's
// tracer path does not read the bullet table at all -- CBaseEntity::MakeTracer()
// (baseentity_shared.cpp:2257) and the server's TE_HL2MPFireBullets handler
// (c_te_hl2mp_shotgun_shot.cpp:101) both ask the WEAPON for GetTracerType().
// So CBaseEntity_FireBullets() (lbaseentity_shared.cpp) copies bullet.TracerName
// into this weapon's own Lua table as Primary.TracerName, and this returns it.
//
// Both realms get their own copy of that field from their own predicted
// FireBullets, which is what makes the client's TE path work too: the server's
// TE_HL2MPFireBullets arrives on a client whose weapon already learned the name.
//-----------------------------------------------------------------------------
const char *CHL2MPScriptedWeapon::GetTracerType( void )
{
#if defined ( LUA_SDK )
	static char s_szTracerName[ 64 ];

	if ( L != NULL && m_nTableReference >= 0 )
	{
		static const char *s_pTracerField[ 2 ] = { "Primary", "Secondary" };

		lua_getref( L, m_nTableReference );                 // [tbl]
		if ( lua_istable( L, -1 ) )
		{
			for ( int i = 0; i < 2; ++i )
			{
				lua_getfield( L, -1, s_pTracerField[ i ] ); // [tbl, sub]
				if ( lua_istable( L, -1 ) )
				{
					lua_getfield( L, -1, "TracerName" );    // [tbl, sub, name]
					if ( lua_type( L, -1 ) == LUA_TSTRING )
					{
						Q_strncpy( s_szTracerName, lua_tostring( L, -1 ), sizeof( s_szTracerName ) );
						lua_pop( L, 3 );                    // []
						return s_szTracerName;
					}
					lua_pop( L, 1 );                        // [tbl, sub]
				}
				lua_pop( L, 1 );                            // [tbl]
			}
		}
		lua_pop( L, 1 );                                    // []
	}
#endif

	/*
	** HL2SB diagnostic (AGENTS.md 9.7): this weapon has no bullet.TracerName
	** published on THIS realm, so every tracer it fires falls back to the stock
	** "Tracer" effect here.  The name is published by the Lua FireBullets()
	** binding (lbaseentity_shared.cpp) in the realm that runs it; the client only
	** runs it when the shot is predicted there, which is exactly how the Nyan
	** Gun lost its rainbow tracer while its impacts kept working.  One line per
	** class per DLL load.
	*/
	{
		char szKey[128];
		Q_snprintf( szKey, sizeof( szKey ), "tracer-name-missing:%s", GetClassname() );
#ifdef CLIENT_DLL
		HL2SB_WarnOnce( szKey, "GetTracerType: client has no TracerName for '%s'\n", GetClassname() );
#else
		HL2SB_WarnOnce( szKey, "GetTracerType: server has no TracerName for '%s'\n", GetClassname() );
#endif
	}

	return BaseClass::GetTracerType();
}

//-----------------------------------------------------------------------------
// HL2SB GMod compat (wiki): WEAPON:TranslateActivity( act ) -> act -- the
// per-weapon activity translation, layered in FRONT of the engine's own
// m_acttable walk.  weapon_base's sh_anim.lua answers from the same
// m_acttable and returns ACT_INVALID (-1) for "no mapping", which falls
// through to the engine walk -- the same answer twice for base weapons, and
// a script's own translation wins for custom ones.
//-----------------------------------------------------------------------------
void CHL2MPScriptedWeapon::SetNativeHoldType( const char *name )
{
	if ( name && name[0] )
		Q_strncpy( m_szHoldType.GetForModify(), name, 80 );
}

const char *CHL2MPScriptedWeapon::GetNativeHoldType() const
{
	return m_szHoldType.Get();
}

void CHL2MPScriptedWeapon::UpdateHoldType()
{
#if defined( LUA_SDK )
	if ( L == NULL || m_nTableReference < 0 || m_bUpdatingHoldType ||
		!m_szHoldType.Get()[0] || !Q_strcmp( m_szAppliedHoldType, m_szHoldType.Get() ) ) return;
	const int top = lua_gettop(L);
	lua_getref(L,m_nTableReference);
	if (!lua_istable(L,-1)) { lua_settop(L,top); return; }
	luasrc_PushScriptField(L,-1,"SetWeaponHoldType");
	lua_remove(L,-2);
	if (!lua_isfunction(L,-1)) { lua_settop(L,top); return; }
	m_bUpdatingHoldType = true;
	char applied[80];
	Q_strncpy(applied,m_szHoldType.Get(),sizeof(applied));
	lua_pushweapon(L,this);
	lua_pushstring(L,applied);
	const bool succeeded = luasrc_pcall(L,2,0,0) == 0;
	lua_settop(L,top);
	if (succeeded) Q_strncpy(m_szAppliedHoldType,applied,sizeof(m_szAppliedHoldType));
	m_bUpdatingHoldType = false;
#endif
}

Activity CHL2MPScriptedWeapon::ActivityOverride( Activity baseAct, bool *pRequired )
{
	UpdateHoldType();
#if defined ( LUA_SDK )
	BEGIN_LUA_CALL_WEAPON_METHOD( "TranslateActivity" );
		lua_pushinteger( L, (int)baseAct );
	END_LUA_CALL_WEAPON_METHOD( 1, 1 );

	if ( lua_gettop( L ) > 0 )
	{
		if ( lua_isnumber( L, -1 ) )
		{
			const int nAct = (int)lua_tointeger( L, -1 );
			lua_pop( L, 1 );
			if ( nAct > ACT_INVALID )
				return (Activity)nAct;
		}
		else
		{
			lua_pop( L, 1 );
		}
	}
#endif
	return BaseClass::ActivityOverride( baseAct, pRequired );
}

//-----------------------------------------------------------------------------
// HL2SB GMod compat (wiki): WEAPON:OnRestore() -- "Called when the weapon
// entity is reloaded from a Source Engine save (not the Sandbox saves or
// dupes) or on a changelevel."  The engine state is restored first so the
// script reads post-restore values.
//-----------------------------------------------------------------------------
void CHL2MPScriptedWeapon::OnRestore( void )
{
	BaseClass::OnRestore();

#if defined ( LUA_SDK )
	BEGIN_LUA_CALL_WEAPON_METHOD( "OnRestore" );
	END_LUA_CALL_WEAPON_METHOD( 0, 0 );
#endif
}

bool CHL2MPScriptedWeapon::Reload( void )
{
#if defined ( LUA_SDK )
	BEGIN_LUA_CALL_WEAPON_METHOD( "Reload" );
	END_LUA_CALL_WEAPON_METHOD( 0, 1 );

	RETURN_LUA_BOOLEAN();
#endif
	return BaseClass::Reload();
}

//-----------------------------------------------------------------------------
// Purpose: 
// Output : Returns true on success, false on failure.
//-----------------------------------------------------------------------------
// Legacy self references resolve to the weapon; ownership remains a native read.
static void HL2SB_WeaponUpdateLuaSelfAliases( CHL2MPScriptedWeapon *pWeapon )
{
#if defined ( LUA_SDK )
	if ( L == NULL || !lua_isrefvalid( L, pWeapon->m_nTableReference ) )
		return;
	lua_getref( L, pWeapon->m_nTableReference );
	if ( lua_istable( L, -1 ) )
	{
		lua_pushweapon( L, pWeapon );
		lua_setfield( L, -2, "Weapon" );
		lua_pushweapon( L, pWeapon );
		lua_setfield( L, -2, "Entity" );
	}
	lua_pop( L, 1 );
#endif
}

//-----------------------------------------------------------------------------
// HL2SB GMod compat (wiki): WEAPON:GetTracerOrigin() -> Vector -- "override
// where the tracer comes from".  Consumed once per shot by CBaseEntity::
// FireBullets (baseentity_shared.cpp).  On the client only the local
// player's prediction draws its own tracers, so only that shooter is asked;
// everyone else's tracers arrive through the HL2MP TE, which carries the
// server's answer.
//-----------------------------------------------------------------------------
bool HL2SB_GetWeaponTracerOrigin( CBaseEntity *pShooter, Vector &vecOut )
{
#if defined ( LUA_SDK )
	if ( pShooter == NULL || L == NULL )
		return false;

	CBaseCombatWeapon *pWeapon = NULL;
#ifdef CLIENT_DLL
	C_BasePlayer *pLocal = C_BasePlayer::GetLocalPlayer();
	if ( pLocal != NULL && pShooter == pLocal )
		pWeapon = pLocal->GetActiveWeapon();
#else
	if ( CBaseCombatCharacter *pCharacter = pShooter->MyCombatCharacterPointer() )
		pWeapon = pCharacter->GetActiveWeapon();
#endif
	if ( pWeapon == NULL || !pWeapon->IsScripted() )
		return false;

	CHL2MPScriptedWeapon *pScripted = static_cast< CHL2MPScriptedWeapon * >( pWeapon );
	if ( pScripted->m_nTableReference < 0 || !lua_isrefvalid( L, pScripted->m_nTableReference ) )
		return false;

	lua_getref( L, pScripted->m_nTableReference );			// [T]
	if ( !lua_istable( L, -1 ) )
	{
		lua_pop( L, 1 );
		return false;
	}

	luasrc_PushScriptField( L, -1, "GetTracerOrigin" );		// [T, f]
	if ( !lua_isfunction( L, -1 ) )
	{
		lua_pop( L, 2 );									// []
		return false;
	}

	lua_pushweapon( L, pScripted );							// [T, f, self]
	lua_remove( L, -3 );									// [f, self]

	if ( luasrc_pcall( L, 1, 1, 0 ) != 0 )
	{
		// error path leaves the nil placeholder (2026-10-04)
		lua_pop( L, 1 );
		return false;
	}

	bool bAnswered = false;
	if ( lua_isuserdata( L, -1 ) && luaL_checkudata( L, -1, "Vector" ) )
	{
		vecOut = luaL_checkvector( L, -1 );
		bAnswered = true;
	}
	lua_pop( L, 1 );
	return bAnswered;
#else
	return false;
#endif
}

#ifdef CLIENT_DLL
//-----------------------------------------------------------------------------
// HL2SB GMod compat: WEAPON:FireAnimationEvent( pos, ang, event, options,
// source ) - wiki (https://wiki.facepunch.com/gmod/WEAPON:FireAnimationEvent):
// "Called before executing an animation event, such as a muzzle flash...", the
// return value is "Return true to disable the effect", clientside handles the
// 5000-range and other events, and arg 5 (source) is the viewmodel on the
// client.
//
// cf_beast paints its FIRST-PERSON CS muzzle flash from event 21 (its hook
// calls util.Effect("CS_MuzzleFlash") and returns true).  This engine never
// dispatched the hook at all, so the SWEP's effect never ran - the log of a
// live session shows 137 shots and ZERO "DispatchEffect 'CS_MuzzleFlash'" -
// and the viewmodel's own default muzzle event drew the wrong sprite instead
// (the stray blue streak the user reported).  Called from
// C_BaseAnimating::FireEvent (viewmodel scope; the local world weapon's copy
// of the event stays suppressed by the existing HL2SB block in the
// AE_MUZZLEFLASH case, so first person gets exactly one flash).
//
// Returns true when the script disabled the event.
//-----------------------------------------------------------------------------
bool HL2SB_ViewmodelFireAnimationEvent( C_BaseCombatWeapon *pWpn, const Vector &pos, const QAngle &ang, int event, const char *options )
{
	if ( L == NULL || pWpn == NULL )
		return false;

	CHL2MPScriptedWeapon *pScripted = dynamic_cast< CHL2MPScriptedWeapon * >( pWpn );
	if ( pScripted == NULL || !lua_isrefvalid( L, pScripted->m_nTableReference ) )
		return false;


	lua_getref( L, pScripted->m_nTableReference );
	if ( !lua_istable( L, -1 ) )
	{
		lua_pop( L, 1 );
		return false;
	}

	// Protected read: the weapon table answers through __index (base chain),
	// and lua_getfield from C is NOT protected - an error raised there skips
	// every pcall and goes straight to atpanic (process abort).
	luasrc_PushScriptField( L, -1, "FireAnimationEvent" );
	if ( !lua_isfunction( L, -1 ) )
	{
		lua_pop( L, 2 );	// nil + table
		return false;
	}

	lua_pushweapon( L, pWpn );
	lua_pushvector( L, pos );
	lua_pushangle( L, ang );
	lua_pushinteger( L, event );
	if ( options && options[0] )
		lua_pushstring( L, options );
	else
		lua_pushnil( L );
	// wiki arg 5: source entity - the viewmodel on the client
	C_BasePlayer *pLocal = C_BasePlayer::GetLocalPlayer();
	if ( pLocal && pLocal->GetViewModel() )
		lua_pushentity( L, pLocal->GetViewModel() );
	else
		lua_pushnil( L );

	if ( luasrc_pcall( L, 6, 1, 0 ) != 0 )
	{
		// Error path leaves [weapon table][nil placeholder] (2026-10-04);
		// popping both matches the success path's pop of (result + table).
		lua_pop( L, 2 );
		return false;
	}
	// A non-boolean result (nil = "no opinion") lets the default event run.
	bool bDisabled = ( lua_isboolean( L, -1 ) && lua_toboolean( L, -1 ) != 0 );
	lua_pop( L, 2 );				// result + weapon table
	return bDisabled;
}
#endif

bool CHL2MPScriptedWeapon::Deploy( void )
{
#if defined ( LUA_SDK )
	// HL2SB (2026-10-01) probe: first-equip deploy reached C++? what ref state?
	// Remove after the fists diagnosis.
	if ( hl2sb_dev_msgs.GetBool() ) luasrc_LuaInfoMsgF( "[HL2SB wp] %s Deploy ent=%d class='%s' ref=%d state=%d\n",
#ifdef CLIENT_DLL
		"CLIENT",
#else
		"SERVER",
#endif
		entindex(), GetClassname(), (int)m_nTableReference, (int)m_iState );

	// Refresh legacy self references before the script sees the deploy.
	HL2SB_WeaponUpdateLuaSelfAliases( this );
	CBasePlayer *pOwner = ToBasePlayer( GetOwner() );

#ifdef CLIENT_DLL
	// Record the attempt before callbacks so a veto is not retried every frame.
	if ( L != NULL && m_nTableReference >= 0 && pOwner != NULL
		&& pOwner->GetViewModel( m_nViewModelIndex, false ) != NULL )
	{
		lua_getref( L, m_nTableReference );
		if ( lua_istable( L, -1 ) )
		{
			lua_pushliteral( L, "__hl2sb_client_deploy_attempted" );
			lua_pushboolean( L, true );
			lua_rawset( L, -3 );
		}
		lua_pop( L, 1 );
	}
#endif

	// GMod prepares the model and default draw before the script chooses its
	// own sequence, playback rate and attack times.
	BaseClass::Deploy();
	if ( pOwner != NULL )
	{
		CBaseViewModel *pViewModel = pOwner->GetViewModel( m_nViewModelIndex, false );
		if ( pViewModel != NULL )
			pViewModel->SetPlaybackRate( MAX( 0.1f, GetDeploySpeed() ) );
	}

	BEGIN_LUA_CALL_WEAPON_METHOD( "Deploy" );
	END_LUA_CALL_WEAPON_METHOD( 0, 1 );

	// Capture the script result before the return-value handler consumes it.
	if ( hl2sb_dev_msgs.GetBool() )
	{
		const int iTop = lua_gettop( L );
		const bool bIsBool = ( iTop > 0 && lua_isboolean( L, -1 ) ) != 0;
		const bool bVal = bIsBool && ( lua_toboolean( L, -1 ) != 0 );
		luasrc_LuaInfoMsgF( "[HL2SB wp] %s Deploy post-lua ent=%d class='%s' top=%d isbool=%d boolval=%d owner=%d ref=%d\n",
#ifdef CLIENT_DLL
			"CLIENT",
#else
			"SERVER",
#endif
			entindex(), GetClassname(), iTop, bIsBool ? 1 : 0, bVal ? 1 : 0,
			GetOwner() != NULL ? 1 : 0, (int)m_nTableReference );
	}

	RETURN_LUA_VETO();

	return true;
#else
	return BaseClass::Deploy();
#endif
}

//-----------------------------------------------------------------------------
// HL2SB GMod compat: SWEP:Equip( newOwner ) -- "Called when a player or NPC has
// picked the weapon up" (wiki).  gmod_camera uses it to move the networked Zoom
// to the player's fov_desired the moment the camera is picked up; without this
// dispatch the zoom only corrects itself after the first Reload.  GMod hands the
// hook the new owner, which can be a Player or an NPC -- the engine virtual
// already receives a CBaseCombatCharacter, so push it as a plain entity.
// Runs AFTER BaseClass::Equip so the owner fields the script may read are set.
//-----------------------------------------------------------------------------
void CHL2MPScriptedWeapon::Equip( CBaseCombatCharacter *pOwner )
{
	BaseClass::Equip( pOwner );

#if defined ( LUA_SDK )
	HL2SB_WeaponUpdateLuaSelfAliases( this );

	BEGIN_LUA_CALL_WEAPON_METHOD( "Equip" );
		if ( pOwner != NULL )
			lua_pushentity( L, pOwner );
	END_LUA_CALL_WEAPON_METHOD( pOwner != NULL ? 1 : 0, 0 );

	// HL2SB (2026-10-13): GM:WeaponEquip( weapon, owner ) is fired ONCE from the
	// player side now (CBasePlayer::Weapon_Equip), where GMod fires it and where
	// every weapon - scripted or not - passes exactly once.  Firing it here too
	// would double it for SWEPs, so this site was removed.
#endif
}

//-----------------------------------------------------------------------------
// Purpose: HL2SB GMod compat (2026-10-03): WEAPON:OnRemove() -- GMod dispatches
//          it right before a scripted weapon entity goes away (weapon_base
//          stubs it; addons release sounds/particles there).  UpdateOnRemove
//          is the entity-removal chain every scripted weapon walks.
//-----------------------------------------------------------------------------
void CHL2MPScriptedWeapon::UpdateOnRemove( void )
{
#if defined ( LUA_SDK )
	BEGIN_LUA_CALL_WEAPON_METHOD( "OnRemove" );
	END_LUA_CALL_WEAPON_METHOD( 0, 0 );
#endif

	BaseClass::UpdateOnRemove();
}

//-----------------------------------------------------------------------------
// Purpose: HL2SB GMod compat (2026-10-03): WEAPON:OnDrop() -- GMod dispatches
//          it when the weapon is dropped.  Server only: the base Drop body is
//          server-only too, so a client-side drop has nothing to hook.
//-----------------------------------------------------------------------------
void CHL2MPScriptedWeapon::Drop( const Vector &vecVelocity )
{
#ifndef CLIENT_DLL
#if defined ( LUA_SDK )
	BEGIN_LUA_CALL_WEAPON_METHOD( "OnDrop" );
	END_LUA_CALL_WEAPON_METHOD( 0, 0 );
#endif
#endif

	BaseClass::Drop( vecVelocity );
}

Activity CHL2MPScriptedWeapon::GetDrawActivity( void )
{
#if defined ( LUA_SDK )
	BEGIN_LUA_CALL_WEAPON_METHOD( "GetDrawActivity" );
	END_LUA_CALL_WEAPON_METHOD( 0, 1 );

	// Kind of lame, but we're required to explicitly cast
	RETURN_LUA_ACTIVITY();
#endif

	return BaseClass::GetDrawActivity();
}

//-----------------------------------------------------------------------------
// Purpose: GMod's SWEP:TranslateFOV( current_fov ) -> number.  The camera weapon
//          returns its networked Zoom here, which is what makes holding mouse 2
//          actually change the field of view.
//          A weapon that does not define the method, or returns nothing / a
//          non-number, keeps the FOV the engine computed: the fallback can never
//          be 0, which would black out the view.
//-----------------------------------------------------------------------------
float CHL2MPScriptedWeapon::TranslateFOV( float flFOV )
{
#if defined ( LUA_SDK )
	BEGIN_LUA_CALL_WEAPON_METHOD( "TranslateFOV" );
	lua_pushnumber( L, flFOV );
	END_LUA_CALL_WEAPON_METHOD( 1, 1 );

	// HL2SB: deliberately NOT RETURN_LUA_NUMBER().  A weapon's answer has to be a
	// USABLE field of view before it may replace the engine's, because the camera's
	// SWEP:TranslateFOV returns self:GetZoom() - and that NetworkVar is 0 on the
	// client until the server's value arrives (and stays 0 if the scripted-weapon
	// DT var never syncs).  FOV 0 degenerates the projection matrix: the whole
	// screen goes black with no error, no warning and no crash anywhere, which is
	// exactly what equipping the camera used to do.  Same reason a missing/nil/
	// non-number answer must never be treated as 0.
	if ( lua_gettop( L ) > 0 )
	{
		if ( lua_isnumber( L, -1 ) )
		{
			const float flNewFOV = (float)lua_tonumber( L, -1 );
			lua_pop( L, 1 );

			if ( flNewFOV >= 1.0f && flNewFOV <= 179.0f )
				return flNewFOV;
		}
		else
		{
			lua_pop( L, 1 );
		}
	}

	return flFOV;
#endif

	return flFOV;
}

//-----------------------------------------------------------------------------
// Purpose: GMod's SWEP:CalcView( ply, pos, angles, fov ) -> pos, angles, fov.
//          The camera weapon writes angles.Roll here.  Only Lua return values
//          that are actually present AND of the expected type overwrite the
//          view, so a weapon returning fewer values (or nil) cannot corrupt it.
//-----------------------------------------------------------------------------
void CHL2MPScriptedWeapon::CalcView( CBasePlayer *pPlayer, Vector &vecOrigin, QAngle &vecAngles, float &flFOV )
{
#if defined ( LUA_SDK )
	BEGIN_LUA_CALL_WEAPON_METHOD( "CalcView" );
	lua_pushplayer( L, pPlayer );
	lua_pushvector( L, vecOrigin );
	lua_pushangle( L, vecAngles );
	lua_pushnumber( L, flFOV );
	END_LUA_CALL_WEAPON_METHOD( 4, 3 );

	int nRet = lua_gettop( L );
	if ( nRet >= 3 )
	{
		if ( lua_isuserdata( L, -3 ) && luaL_checkudata( L, -3, "Vector" ) )
			vecOrigin = luaL_checkvector( L, -3 );
		if ( lua_isuserdata( L, -2 ) && luaL_checkudata( L, -2, "Angle" ) )
			vecAngles = luaL_checkangle( L, -2 );
		if ( lua_isnumber( L, -1 ) )
			flFOV = (float)lua_tonumber( L, -1 );

		lua_pop( L, 3 );
	}
	else if ( nRet > 0 )
	{
		lua_pop( L, nRet );
	}
#endif
}

//-----------------------------------------------------------------------------
// HL2SB GMod compat: SWEP:FreezeMovement() -- "hold the view still".  GMod's
// CInput asks the deployed weapon before it applies the mouse to the view
// angles; gmod_camera returns true while Mouse2 is held so the zoomed aim
// does not drift.  CInput::MouseMove (in_mouse.cpp) consults this through
// IsScripted() + static_cast, the same path TranslateFOV/CalcView use.
// No method (the weapon_base default does not define one) -> do not freeze.
//-----------------------------------------------------------------------------
bool CHL2MPScriptedWeapon::DispatchFreezeMovement( void )
{
#if defined ( LUA_SDK )
	BEGIN_LUA_CALL_WEAPON_METHOD( "FreezeMovement" );
	END_LUA_CALL_WEAPON_METHOD( 0, 1 );

	bool bFreeze = false;
	if ( lua_gettop( L ) > 0 )
	{
		bFreeze = lua_isboolean( L, -1 ) && lua_toboolean( L, -1 ) != 0;
		lua_pop( L, 1 );
	}
	return bFreeze;
#else
	return false;
#endif
}

//-----------------------------------------------------------------------------
// HL2SB GMod compat: SWEP:AdjustMouseSensitivity( defaultSensitivity, localFOV,
// defaultFOV ) -- a number SCALES the normal sensitivity (gmod_camera returns
// GetZoom()/80 so zooming in slows the aim); returning nothing leaves the
// sensitivity alone.  CInput::MouseMove applies it after ScaleMouse, before
// OverrideMouseInput.  GMod's wiki-documented signature passes three numbers;
// addons compute (localFOV / defaultFOV) ratios, so the args are never optional.
//-----------------------------------------------------------------------------
float CHL2MPScriptedWeapon::DispatchAdjustMouseSensitivity( float flDefaultSensitivity, float flLocalFOV, float flDefaultFOV )
{
#if defined ( LUA_SDK )
	BEGIN_LUA_CALL_WEAPON_METHOD( "AdjustMouseSensitivity" );
		lua_pushnumber( L, flDefaultSensitivity );
		lua_pushnumber( L, flLocalFOV );
		lua_pushnumber( L, flDefaultFOV );
	END_LUA_CALL_WEAPON_METHOD( 3, 1 );

	float flScale = 0.0f;
	if ( lua_gettop( L ) > 0 )
	{
		if ( lua_isnumber( L, -1 ) )
			flScale = ( float )lua_tonumber( L, -1 );
		lua_pop( L, 1 );
	}
	return flScale;
#else
	return 0.0f;
#endif
}

//-----------------------------------------------------------------------------
// HL2SB GMod compat: SWEP:HUDShouldDraw( name ) -- returning false vetoes that
// HUD element while the weapon is deployed (gmod_camera hides everything but
// the weapon selection and chat).  CHudElement::ShouldDraw (hud.cpp) consults
// it for the LOCAL player's active weapon.  No method -> draw as usual.
//-----------------------------------------------------------------------------
bool CHL2MPScriptedWeapon::DispatchHUDShouldDraw( const char *pszElementName )
{
#if defined ( LUA_SDK )
	BEGIN_LUA_CALL_WEAPON_METHOD( "HUDShouldDraw" );
	lua_pushstring( L, pszElementName );
	END_LUA_CALL_WEAPON_METHOD( 1, 1 );

	bool bDraw = true;
	if ( lua_gettop( L ) > 0 )
	{
		if ( lua_isboolean( L, -1 ) )
			bDraw = lua_toboolean( L, -1 ) != 0;
		lua_pop( L, 1 );
	}
	return bDraw;
#else
	return true;
#endif
}

//-----------------------------------------------------------------------------
// Resolve animation activities through the normal weapon transition path.
// Failed requests retain the previous animation state.
//-----------------------------------------------------------------------------
bool CHL2MPScriptedWeapon::SendWeaponAnim( int iActivity )
{
	return BaseClass::SendWeaponAnim( iActivity );
}

//-----------------------------------------------------------------------------
// Purpose: 
// Output : Returns true on success, false on failure.
//-----------------------------------------------------------------------------
bool CHL2MPScriptedWeapon::Holster( CBaseCombatWeapon *pSwitchingTo )
{
#if defined ( LUA_SDK )
	// Same veto-only rule as Deploy: weapon_base:Holster() returns true to say
	// "yes, allow the switch", not "skip the engine". CBaseCombatWeapon::Holster
	// cancels the reload, kills the think, plays ACT_VM_HOLSTER and hides the
	// weapon - skipping it left the weapon visible and thinking.
	BEGIN_LUA_CALL_WEAPON_METHOD( "Holster" );
		lua_pushweapon( L, pSwitchingTo );
	END_LUA_CALL_WEAPON_METHOD( 1, 1 );

	RETURN_LUA_VETO();
#ifdef CLIENT_DLL
	if ( L != NULL && m_nTableReference >= 0 )
	{
		lua_getref( L, m_nTableReference );
		if ( lua_istable( L, -1 ) )
		{
			lua_pushliteral( L, "__hl2sb_client_deploy_attempted" );
			lua_pushboolean( L, false );
			lua_rawset( L, -3 );
		}
		lua_pop( L, 1 );
	}
#endif
#endif

	return BaseClass::Holster( pSwitchingTo );
}

//-----------------------------------------------------------------------------
// Purpose: 
//-----------------------------------------------------------------------------
void CHL2MPScriptedWeapon::ItemPostFrame( void )
{
	UpdateHoldType();
#ifdef CLIENT_DLL
	HL2SB_EnsureInitialWeaponDeploy( this );
#endif
#if defined ( LUA_SDK )
	// GMod calls SWEP:Think() every frame while the weapon is active.
#ifndef CLIENT_DLL
	BEGIN_LUA_CALL_WEAPON_METHOD( "Think" );
	END_LUA_CALL_WEAPON_METHOD( 0, 0 );
#endif

	// ...and SWEP:Tick() every frame as well.  GMod weapons put their per-frame
	// prediction-safe work there, and weapon_fists lands its melee from it:
	//     function SWEP:Tick()
	//         local meleetime = self:GetNextMeleeAttack()
	//         if ( meleetime > 0 && CurTime() > meleetime ) then
	//             self:DealDamage()
	// Without this dispatch DealDamage() is never reached, so the fists played
	// their swing animation but never applied any damage.
	BEGIN_LUA_CALL_WEAPON_METHOD( "Tick" );
	END_LUA_CALL_WEAPON_METHOD( 0, 0 );



	CBasePlayer *pOwner = ToBasePlayer( GetOwner() );

	if ( pOwner != NULL && m_nTableReference >= 0 )
	{
		extern bool HL2SB_PlayerCanAttack( CBasePlayer *player );
		if ( !HL2SB_PlayerCanAttack( pOwner ) )
			return;

		const float flTime = gpGlobals->curtime;
		const int nButtons = pOwner->m_nButtons;
		const int nPressed = pOwner->m_afButtonPressed;

		// HL2SB GMod compat: the stock CBaseCombatWeapon::ItemPostFrame() calls
		// CheckReload() first thing every frame (basecombatweapon_shared.cpp:
		// 1689).  That call -- and only that call -- notices m_bInReload (set
		// by DefaultReload) once m_flNextPrimaryAttack has elapsed and runs
		// FinishReload(), which moves spare ammo into the clip.  This GMod
		// -style frame drives the fire buttons itself and returns before
		// BaseClass::ItemPostFrame(), so that completion step was severed: a
		// SWEP reload played the animation and set m_bInReload, but nothing
		// ever refilled the clip ("reload anim OK, still no bullets"; session
		// log: SendWeaponAnim activity=182 with zero Lua errors).  Placed
		// BEFORE the Lua ItemPostFrame dispatch so the "Lua owns the buttons"
		// early return below still completes reloads, guarded exactly like the
		// base class (ClipSize -1 weapons skip it).
		if ( UsesClipsForAmmo1() )
		{
			CheckReload();
		}

		// A Lua SWEP may implement SWEP:ItemPostFrame() itself; returning false
		// means "Lua owns the fire buttons" (the GMod base port did that).
		BEGIN_LUA_CALL_WEAPON_METHOD( "ItemPostFrame" );
		END_LUA_CALL_WEAPON_METHOD( 0, 1 );

		if ( lua_gettop( L ) > 0 && lua_isboolean( L, -1 ) && !lua_toboolean( L, -1 ) )
		{
			lua_pop( L, 1 );
			// HL2SB (2026-09-30): no C++ WeaponIdle() here anymore -- GMod
			// does not run any idle upkeep for Lua SWEPs (see the header
			// comment); the viewmodel holds the pose the script sent.
			return;
		}

		if ( lua_gettop( L ) > 0 )
		{
			lua_pop( L, 1 );
		}

		// GMod's engine drives the fire buttons from here on:
		//
		//   IN_ATTACK  (pressed, or held when SWEP.Primary.Automatic)    -> PrimaryAttack
		//   IN_ATTACK2 (pressed, or held when SWEP.Secondary.Automatic)  -> SecondaryAttack
		//   IN_RELOAD  (pressed)                                         -> Reload
		//
		// and it does NO clip or ammo bookkeeping: SWEP.Primary.ClipSize = -1
		// means "no clip", and a SWEP that wants an ammo check calls
		// CanPrimaryAttack() itself.  CBaseCombatWeapon::ItemPostFrame() does the
		// opposite - an empty clip plays the "no ammo" click - which is why every
		// GMod SWEP without its own Lua post-frame clicked empty instead of
		// firing (weapon_flechettegun: ClipSize -1, Primary.Ammo "none").
		//
		// GMod gives a weapon its clip contents from Primary/Secondary
		// DefaultClip.  The old Lua base seeded them; the flag lives on the
		// weapon's Lua table because adding a member to this class would be an
		// ABI trap (waf does not track header changes).
		//
		// HL2SB (2026-09-22): only the CLIP is seeded here.  The spare half of
		// DefaultClip (DefaultClip - ClipSize) is granted by the engine's
		// CBaseCombatCharacter::Weapon_Equip() - that single grant IS GMod's
		// behaviour (GMod AK47: 35 clip / 105 reserve = 140 - 35).  An older
		// revision of this block also called GiveAmmo() for the excess, so a
		// freshly given weapon arrived with DOUBLE the reserve (210, and 340
		// after a re-give in a test session) - user-observed, fixed here.
		bool bClipsSeeded = false;
		lua_getref( L, m_nTableReference );
		if ( lua_istable( L, -1 ) )
		{
			lua_getfield( L, -1, "_hl2sb_clips_seeded" );
			bClipsSeeded = lua_toboolean( L, -1 ) != 0;
			lua_pop( L, 1 );

			if ( !bClipsSeeded )
			{
				lua_pushboolean( L, true );
				lua_setfield( L, -2, "_hl2sb_clips_seeded" );
			}
		}
		lua_pop( L, 1 );

		if ( !bClipsSeeded )
		{
			// GMod Primary.DefaultClip = total ammo on give (clip + spare).
			// Seed magazine with min(DefaultClip, ClipSize); excess -> GiveAmmo.
			// Writing DefaultClip straight into m_iClip1 made an 11-round
			// deagle show 55 in the HUD (ClipSize=11, DefaultClip=55).
			// Clip only - clamp to ClipSize exactly like GMod's net result
			// (clip = min(DefaultClip, ClipSize)); the spare half of
			// DefaultClip is Weapon_Equip's job, not ours (see comment above
			// bClipsSeeded - granting it here doubled the reserve).
			const int nClipSize1 = lua_getweaponint( L, m_nTableReference, "Primary", "ClipSize", "", -1 );
			if ( nClipSize1 != -1 )
			{
				const int nDefault1 = lua_getweaponint( L, m_nTableReference, "Primary", "DefaultClip", "", nClipSize1 );
				const int nSeed1 = ( nClipSize1 > 0 ) ? MIN( nDefault1, nClipSize1 ) : nDefault1;
				m_iClip1.GetForModify() = nSeed1;
			}

			const int nClipSize2 = lua_getweaponint( L, m_nTableReference, "Secondary", "ClipSize", "", -1 );
			if ( nClipSize2 != -1 )
			{
				const int nDefault2 = lua_getweaponint( L, m_nTableReference, "Secondary", "DefaultClip", "", nClipSize2 );
				const int nSeed2 = ( nClipSize2 > 0 ) ? MIN( nDefault2, nClipSize2 ) : nDefault2;
				m_iClip2.GetForModify() = nSeed2;
			}
		}

		// HL2SB probe: did the GMod fire block run with attack held?
		if ( ( nButtons & ( IN_ATTACK | IN_ATTACK2 ) ) != 0 )
		{
			char szKey[ 192 ];
			Q_snprintf( szKey, sizeof( szKey ), "fireblk:%s", GetClassname() );
			HL2SB_WarnOnce( szKey, "[HL2SB wp] fire block %s class='%s' owner=%d ref=%d state=%d\n",
#ifdef CLIENT_DLL
				"CLIENT",
#else
				"SERVER",
#endif
				GetClassname(), pOwner != NULL ? 1 : 0, (int)m_nTableReference, (int)m_iState );
		}

		const bool bPrimaryAutomatic = lua_getweaponbool( L, m_nTableReference, "Primary", "Automatic", "", false );
		const bool bSecondaryAutomatic = lua_getweaponbool( L, m_nTableReference, "Secondary", "Automatic", "", false );

		const bool bPrimaryWants = ( nButtons & IN_ATTACK ) != 0 &&
								   ( bPrimaryAutomatic || ( nPressed & IN_ATTACK ) != 0 );
		const bool bSecondaryWants = ( nButtons & IN_ATTACK2 ) != 0 &&
									 ( bSecondaryAutomatic || ( nPressed & IN_ATTACK2 ) != 0 );

		// Secondary first, the way the GMod base orders it.
		//
		// HL2SB (2026-10-07): the old auto-gate that applied SWEP.Primary.Delay /
		// SWEP.Secondary.Delay when the script did not call SetNext*Fire itself
		// is GONE.  GMod's engine drives the fire buttons every tick and the
		// RATE is the script's own job (its lua base weapons apply
		// SWEP.Primary.Delay through SetNextPrimaryFire); an engine-side Delay
		// fallback never existed there.  This fork's fallback silently
		// throttled the continuous-beam SWEPs: hl1sweps' egon fires
		// PrimaryAttack every tick and paces its damage with its own
		// SetDmgTime( GetPulseInterval() ) cadence -- the auto-gate stretched
		// that to SWEP.Primary.Delay (0.75s), i.e. the reported "egon shoots
		// slowly and barely damages".  A script that wants a fire rate arms
		// the gate itself; one that does not gets GMod's every-tick behaviour.
		if ( bSecondaryWants && flTime >= m_flNextSecondaryAttack )
		{
			BEGIN_LUA_CALL_WEAPON_METHOD( "SecondaryAttack" );
			END_LUA_CALL_WEAPON_METHOD( 0, 0 );
		}
		else if ( bPrimaryWants && flTime >= m_flNextPrimaryAttack )
		{
			BEGIN_LUA_CALL_WEAPON_METHOD( "PrimaryAttack" );
			END_LUA_CALL_WEAPON_METHOD( 0, 0 );
		}
		else if ( ( nPressed & IN_RELOAD ) != 0 )
		{
			BEGIN_LUA_CALL_WEAPON_METHOD( "Reload" );
			END_LUA_CALL_WEAPON_METHOD( 0, 0 );
		}

		// HL2SB (2026-09-30): NO C++ WeaponIdle() upkeep here anymore --
		// GMod runs no idle management for Lua SWEPs (its own weapon_base
		// Think() is empty); the viewmodel holds the pose the script sent
		// and SWEPs re-idle themselves.  The old upkeep re-sent
		// ACT_VM_IDLE whenever its timer elapsed, and since every
		// SendWeaponAnim force-restarts the viewmodel (GMod semantics),
		// automatic weapons whose fire animation is shorter than their
		// fire delay got an idle restart interleaved between shots -- the
		// reported attack-animation twitch.
		return;
	}
#endif
	BaseClass::ItemPostFrame();
}

//-----------------------------------------------------------------------------
// Purpose: Called each frame by the player PostThink, if the player's not ready to attack yet
//-----------------------------------------------------------------------------
void CHL2MPScriptedWeapon::ItemBusyFrame( void )
{
#ifdef CLIENT_DLL
	HL2SB_EnsureInitialWeaponDeploy( this );
#endif
#if defined ( LUA_SDK )
	BEGIN_LUA_CALL_WEAPON_METHOD( "ItemBusyFrame" );
	END_LUA_CALL_WEAPON_METHOD( 0, 1 );

	RETURN_LUA_NONE();
#endif

	BaseClass::ItemBusyFrame();
}

#ifndef CLIENT_DLL
//-----------------------------------------------------------------------------
// HL2SB GMod compat (wiki): WEAPON:AcceptInput( inputName, activator, caller,
// data ) -> boolean "Should we suppress the default action for this input?".
// Entity I/O is server-only.  Override of the CBaseEntity virtual.
//-----------------------------------------------------------------------------
bool CHL2MPScriptedWeapon::AcceptInput( const char *szInputName, CBaseEntity *pActivator, CBaseEntity *pCaller, variant_t Value, int outputID )
{
#if defined ( LUA_SDK )
	BEGIN_LUA_CALL_WEAPON_METHOD( "AcceptInput" );
		lua_pushstring( L, szInputName );
		if ( pActivator != NULL ) lua_pushentity( L, pActivator ); else lua_pushnil( L );
		if ( pCaller != NULL ) lua_pushentity( L, pCaller ); else lua_pushnil( L );
		lua_pushstring( L, Value.String() );
	END_LUA_CALL_WEAPON_METHOD( 4, 1 );

	if ( lua_gettop( L ) > 0 )
	{
		const bool bSuppress = lua_toboolean( L, -1 ) != 0;
		lua_pop( L, 1 );
		if ( bSuppress )
			return true;
	}
#endif
	return BaseClass::AcceptInput( szInputName, pActivator, pCaller, Value, outputID );
}

//-----------------------------------------------------------------------------
// HL2SB GMod compat (wiki): WEAPON:NPCShoot_Primary( shootPos, shootDir ) /
// WEAPON:NPCShoot_Secondary( shootPos, shootDir ) -- "called internally
// during TASK_RANGE_ATTACK1 -> OnRangeAttack1".  The SDK fires an NPC's
// weapon through AE_NPC_WEAPON_FIRE -> Operator_ForceNPCFire; stock weapons
// implement that inline, the scripted one hands the shot to the Lua method
// (weapon_base's NPCShoot_* route it into PrimaryAttack/SecondaryAttack).
//-----------------------------------------------------------------------------
void CHL2MPScriptedWeapon::Operator_ForceNPCFire( CBaseCombatCharacter *pOperator, bool bSecondary )
{
#if defined ( LUA_SDK )
	if ( L != NULL && m_nTableReference >= 0 && lua_isrefvalid( L, m_nTableReference ) && pOperator != NULL )
	{
		Vector vecShootOrigin, vecShootDir;
		int iAttachment = LookupAttachment( "muzzle" );
		if ( iAttachment > 0 )
		{
			QAngle angShootDir;
			GetAttachment( iAttachment, vecShootOrigin, angShootDir );
			AngleVectors( angShootDir, &vecShootDir );
		}
		else
		{
			// no muzzle attachment: shoot from the operator's gun position
			// along its body direction
			vecShootOrigin = pOperator->Weapon_ShootPosition();
			vecShootDir = pOperator->BodyDirection3D();
		}

		BEGIN_LUA_CALL_WEAPON_METHOD( bSecondary ? "NPCShoot_Secondary" : "NPCShoot_Primary" );
			lua_pushvector( L, vecShootOrigin );
			lua_pushvector( L, vecShootDir );
		END_LUA_CALL_WEAPON_METHOD( 2, 0 );
	}
#endif

	BaseClass::Operator_ForceNPCFire( pOperator, bSecondary );
}

//-----------------------------------------------------------------------------
// HL2SB GMod compat (wiki): WEAPON:GetNPCBulletSpread( proficiency ) ->
// number of DEGREES the NPC's shots deviate from its aim vector (wiki
// default 15).  The SDK expresses the cone as sin( degrees / 2 ) per axis --
// the VECTOR_CONE_*DEGREES constants are exactly that (10deg -> 0.08716,
// 15deg -> 0.13053).
//-----------------------------------------------------------------------------
Vector CHL2MPScriptedWeapon::GetBulletSpread( WeaponProficiency_t proficiency )
{
#if defined ( LUA_SDK )
	BEGIN_LUA_CALL_WEAPON_METHOD( "GetNPCBulletSpread" );
		lua_pushinteger( L, (int)proficiency );
	END_LUA_CALL_WEAPON_METHOD( 1, 1 );

	if ( lua_gettop( L ) > 0 )
	{
		if ( lua_isnumber( L, -1 ) )
		{
			const float flDegrees = (float)lua_tonumber( L, -1 );
			lua_pop( L, 1 );
			const float flCone = sinf( DEG2RAD( flDegrees ) * 0.5f );
			return Vector( flCone, flCone, flCone );
		}
		lua_pop( L, 1 );
	}
#endif
	return BaseClass::GetBulletSpread( proficiency );
}

// NPC settings are accepted as a complete numeric group. Preserve the caller's
// Lua stack on missing methods, invalid results and protected-call failures.
static bool ReadNPCWeaponSettings( CHL2MPScriptedWeapon *weapon,
                                   const char *method, int count, float *values )
{
#if defined( LUA_SDK )
	if ( L == NULL || weapon->m_nTableReference < 0 ||
		!lua_isrefvalid( L, weapon->m_nTableReference ) ) return false;
	const int top = lua_gettop( L );
	lua_getref( L, weapon->m_nTableReference );
	if ( !lua_istable( L, -1 ) ) { lua_settop( L, top ); return false; }
	luasrc_PushScriptField( L, -1, method );
	lua_remove( L, -2 );
	if ( !lua_isfunction( L, -1 ) ) { lua_settop( L, top ); return false; }
	lua_pushweapon( L, weapon );
	if ( luasrc_pcall( L, 1, count, 0 ) != LUA_OK ) {
		lua_settop( L, top );
		return false;
	}
	bool valid = true;
	for ( int i = 0; i < count; ++i )
		if ( lua_type( L, top + i + 1 ) != LUA_TNUMBER || !isfinite( (float)lua_tonumber( L, top + i + 1 ) ) ) valid = false;
	if ( valid )
		for ( int i = 0; i < count; ++i )
			values[i] = (float)lua_tonumber( L, top + i + 1 );
	lua_settop( L, top );
	return valid;
#else
	return false;
#endif
}

float CHL2MPScriptedWeapon::GetFireRate( void )
{
	float settings[3];
	return ReadNPCWeaponSettings( this, "GetNPCBurstSettings", 3, settings )
		? settings[2] : BaseClass::GetFireRate();
}

int CHL2MPScriptedWeapon::GetMinBurst( void )
{
	float settings[3];
	return ReadNPCWeaponSettings( this, "GetNPCBurstSettings", 3, settings )
		? MAX( 1, (int)settings[0] ) : 1;
}

int CHL2MPScriptedWeapon::GetMaxBurst( void )
{
	float settings[3];
	return ReadNPCWeaponSettings( this, "GetNPCBurstSettings", 3, settings )
		? MAX( MAX( 1, (int)settings[0] ), (int)settings[1] ) : 1;
}

float CHL2MPScriptedWeapon::GetMinRestTime( void )
{
	float settings[2];
	return ReadNPCWeaponSettings( this, "GetNPCRestTimes", 2, settings )
		? MAX( 0.0f, settings[0] ) : 0.3f;
}

float CHL2MPScriptedWeapon::GetMaxRestTime( void )
{
	float settings[2];
	return ReadNPCWeaponSettings( this, "GetNPCRestTimes", 2, settings )
		? MAX( MAX( 0.0f, settings[0] ), settings[1] ) : 0.6f;
}

//-----------------------------------------------------------------------------
// HL2SB GMod compat (wiki): WEAPON:ShouldDropOnDie() -> boolean.  See the
// header for the three-valued vote contract.
//-----------------------------------------------------------------------------
int CHL2MPScriptedWeapon::DispatchShouldDropOnDieVote( void )
{
#if defined ( LUA_SDK )
	if ( L == NULL || m_nTableReference < 0 || !lua_isrefvalid( L, m_nTableReference ) )
		return -1;

	lua_getref( L, m_nTableReference );						// [T]
	if ( !lua_istable( L, -1 ) )
	{
		lua_pop( L, 1 );
		return -1;
	}

	luasrc_PushScriptField( L, -1, "ShouldDropOnDie" );		// [T, f]
	if ( !lua_isfunction( L, -1 ) )
	{
		lua_pop( L, 2 );									// []
		return -1;
	}

	lua_pushvalue( L, -2 );									// [T, f, T]
	lua_remove( L, -3 );									// [f, T]

	if ( luasrc_pcall( L, 1, 1, 0 ) != 0 )
	{
		// error path leaves the nil placeholder (2026-10-04); the table
		// went in as the argument and stays below it
		lua_pop( L, 1 );
		return -1;
	}

	int nVote = -1;
	if ( lua_isboolean( L, -1 ) )
		nVote = lua_toboolean( L, -1 ) ? 1 : 0;
	lua_pop( L, 1 );										// []
	return nVote;
#else
	return -1;
#endif
}

// HL2SB: PackDeadPlayerItems (player.cpp) consumes the vote through this
// wrapper so it does not need the scripted-weapon header.
int HL2SB_WeaponShouldDropOnDieVote( CBaseCombatWeapon *pWeapon )
{
	if ( pWeapon == NULL || !pWeapon->IsScripted() )
		return -1;
	return static_cast< CHL2MPScriptedWeapon * >( pWeapon )->DispatchShouldDropOnDieVote();
}

//-----------------------------------------------------------------------------
// Purpose: NPC capability bits.  The wiki spells the hook GetCapabilities
// (weapon_base/init.lua and real addons use it); older HL2SB content spelled
// it CapabilitiesGet -- answer whichever exists.
//-----------------------------------------------------------------------------
int CHL2MPScriptedWeapon::CapabilitiesGet( void )
{
#if defined ( LUA_SDK )
	if ( L != NULL && m_nTableReference >= 0 && lua_isrefvalid( L, m_nTableReference ) )
	{
		lua_getref( L, m_nTableReference );					// [T]
		if ( lua_istable( L, -1 ) )
		{
			luasrc_PushScriptField( L, -1, "GetCapabilities" );	// [T, f]
			if ( !lua_isfunction( L, -1 ) )
			{
				lua_pop( L, 1 );							// [T]
				luasrc_PushScriptField( L, -1, "CapabilitiesGet" );	// [T, f]
			}
			if ( lua_isfunction( L, -1 ) )
			{
				lua_pushvalue( L, -2 );						// [T, f, T]
				lua_remove( L, -3 );						// [f, T]
				if ( luasrc_pcall( L, 1, 1, 0 ) == 0 )
				{
					int nCaps = 0;
					if ( lua_isnumber( L, -1 ) )
						nCaps = (int)lua_tointeger( L, -1 );
					lua_pop( L, 1 );
					return nCaps;
				}
				// luasrc_pcall logged + popped the message; nothing left
				return BaseClass::CapabilitiesGet();
			}
			lua_pop( L, 2 );								// []
		}
		else
		{
			lua_pop( L, 1 );
		}
	}
#endif
	return BaseClass::CapabilitiesGet();
}
#else
//-----------------------------------------------------------------------------
// HL2SB GMod compat (wiki): WEAPON:DrawWeaponSelection( x, y, wide, tall,
// alpha ) -- a scripted weapon that defines the hook draws the selected
// weapon box's icon area itself (weapon_base's version bounces the
// WepSelectIcon texture and appends the info box).  Returns true when the
// hook ran so the caller skips the engine icon path.
//-----------------------------------------------------------------------------
bool CHL2MPScriptedWeapon::DispatchDrawWeaponSelection( int x, int y, int wide, int tall, int alpha )
{
#if defined ( LUA_SDK )
	if ( L == NULL || m_nTableReference < 0 || !lua_isrefvalid( L, m_nTableReference ) )
		return false;

	lua_getref( L, m_nTableReference );						// [T]
	if ( !lua_istable( L, -1 ) )
	{
		lua_pop( L, 1 );
		return false;
	}

	luasrc_PushScriptField( L, -1, "DrawWeaponSelection" );	// [T, f]
	if ( !lua_isfunction( L, -1 ) )
	{
		lua_pop( L, 2 );
		return false;
	}

	lua_pushweapon( L, this );								// [T, f, self]
	lua_pushinteger( L, x );
	lua_pushinteger( L, y );
	lua_pushinteger( L, wide );
	lua_pushinteger( L, tall );
	lua_pushinteger( L, alpha );							// [T, f, self, x, y, w, t, a]
	lua_remove( L, -8 );									// [f, self, x, y, w, t, a]

	luasrc_pcall( L, 6, 0, 0 );
	// on error luasrc_pcall logged + popped the message; zero results were
	// requested, so either way the stack is clean here
	return true;
#else
	return false;
#endif
}

//-----------------------------------------------------------------------------
// HL2SB GMod compat (wiki): WEAPON:CustomAmmoDisplay() -> table with
// Draw (boolean), PrimaryClip / PrimaryAmmo / SecondaryClip / SecondaryAmmo
// (numbers, -1 = hide the value).
//-----------------------------------------------------------------------------
bool CHL2MPScriptedWeapon::DispatchCustomAmmoDisplay( bool *pbDraw, int *pnPrimaryClip, int *pnPrimaryAmmo,
	int *pnSecondaryClip, int *pnSecondaryAmmo )
{
#if defined ( LUA_SDK )
	BEGIN_LUA_CALL_WEAPON_METHOD( "CustomAmmoDisplay" );
	END_LUA_CALL_WEAPON_METHOD( 0, 1 );

	if ( lua_gettop( L ) <= 0 || !lua_istable( L, -1 ) )
	{
		if ( lua_gettop( L ) > 0 )
			lua_pop( L, 1 );
		return false;
	}

	if ( pbDraw != NULL )
	{
		lua_getfield( L, -1, "Draw" );
		if ( lua_isboolean( L, -1 ) )
			*pbDraw = ( lua_toboolean( L, -1 ) != 0 );
		lua_pop( L, 1 );
	}

	if ( pnPrimaryClip != NULL )
	{
		lua_getfield( L, -1, "PrimaryClip" );
		if ( lua_isnumber( L, -1 ) )
			*pnPrimaryClip = (int)lua_tointeger( L, -1 );
		lua_pop( L, 1 );
	}

	if ( pnPrimaryAmmo != NULL )
	{
		lua_getfield( L, -1, "PrimaryAmmo" );
		if ( lua_isnumber( L, -1 ) )
			*pnPrimaryAmmo = (int)lua_tointeger( L, -1 );
		lua_pop( L, 1 );
	}

	if ( pnSecondaryClip != NULL )
	{
		lua_getfield( L, -1, "SecondaryClip" );
		if ( lua_isnumber( L, -1 ) )
			*pnSecondaryClip = (int)lua_tointeger( L, -1 );
		lua_pop( L, 1 );
	}

	if ( pnSecondaryAmmo != NULL )
	{
		lua_getfield( L, -1, "SecondaryAmmo" );
		if ( lua_isnumber( L, -1 ) )
			*pnSecondaryAmmo = (int)lua_tointeger( L, -1 );
		lua_pop( L, 1 );
	}

	lua_pop( L, 1 );	// the table
	return true;
#else
	return false;
#endif
}

//-----------------------------------------------------------------------------
// Returns the aiment render origin + angles
//-----------------------------------------------------------------------------
int CHL2MPScriptedWeapon::DrawModel( int flags )
{
	// Entity:DrawModel() is the native model path.  Scripted weapons dispatch
	// DrawWorldModel and DrawWorldModelTranslucent from C_BaseCombatWeapon;
	// dispatching a Lua DrawModel method here re-enters Entity:DrawModel from
	// weapon_base's default DrawWorldModel and recurses until the Lua stack is
	// exhausted.
	return BaseClass::DrawModel( flags );
}
#endif



//-----------------------------------------------------------------------------
// HL2SB (2026-10-07): GMod's viewmodel FOV contract -- SWEP.ViewModelFOV
// overrides viewmodel_fov while the weapon is equipped (hl1sweps runs its
// GoldSrc-style bob pipeline through SWEP:CalcViewModelView and pins
// ViewModelFOV to 90; without this the client kept the viewmodel_fov default
// and every HL1 weapon drew at the wrong projection).
// Client-only read helper for ClientModeShared::GetViewModelFOV.  Returns the
// weapon's ViewModelFOV script field, or flDefault when it is absent/not a
// number/<= 0.  Never raises: a broken Lua state just answers flDefault.
//-----------------------------------------------------------------------------
#ifdef CLIENT_DLL
float HL2SB_ScriptedViewModelFOV( CBaseCombatWeapon *pWeapon, float flDefault )
{
	if ( L == NULL || pWeapon == NULL || !pWeapon->IsScripted() )
		return flDefault;

	CHL2MPScriptedWeapon *pScripted = static_cast< CHL2MPScriptedWeapon * >( pWeapon );
	lua_pushweaponfield( L, pScripted->m_nTableReference, "ViewModelFOV" );
	const float flFov = lua_isnumber( L, -1 ) ? (float)lua_tonumber( L, -1 ) : 0.0f;
	lua_pop( L, 1 );
	return ( flFov > 0.0f ) ? flFov : flDefault;
}
#endif
