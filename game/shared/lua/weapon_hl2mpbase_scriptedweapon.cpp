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
#endif
// HL2SB: SWEP:CalcView pushes/reads the Player, Vector and QAngle userdata.
#include "lbaseplayer_shared.h"
// HL2SB: lua_pushentity for the SWEP:Equip dispatch
#include "lbaseentity_shared.h"
#include "mathlib/lvector.h"
// HL2SB: lua_pushtrace(), for SWEP:DoImpactEffect( trace, damageType ).
#include "lgametrace.h"

// memdbgon must be the last include file in a .cpp file!!!
#include "tier0/memdbgon.h"

IMPLEMENT_NETWORKCLASS_ALIASED( HL2MPScriptedWeapon, DT_HL2MPScriptedWeapon )

BEGIN_NETWORK_TABLE( CHL2MPScriptedWeapon, DT_HL2MPScriptedWeapon )
#ifdef CLIENT_DLL
	RecvPropString( RECVINFO( m_iScriptedClassname ) ),
#else
	SendPropString( SENDINFO( m_iScriptedClassname ) ),
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

	GetClassMap().Add( className, "CHL2MPScriptedWeapon", sizeof( CHL2MPScriptedWeapon ),
		&CCHL2MPScriptedWeaponFactory, true );
#else
	if ( EntityFactoryDictionary()->FindFactory( className ) )
	{
		return;
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

// These functions serve as skeletons for the our weapons' actions to be
// implemented in Lua.
acttable_t *CHL2MPScriptedWeapon::ActivityList( void ) {
#ifdef LUA_SDK
	lua_getref( L, m_nTableReference );
	lua_getfield( L, -1, "m_acttable" );
	lua_remove( L, -2 );
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

// HL2SB GMod SWEP compat: read a weapon data field that GMod stores nested
// (Primary.SubKey / Secondary.SubKey) but HL2SB keeps flat.  Prefers the nested
// value; falls back to the flat key.  ALWAYS leaves exactly one value on the
// Lua stack (the caller must lua_pop it once), so the over/underflow bugs in
// the old inline getref/getfield/remove sequences are impossible here.
static int lua_getweaponfield ( lua_State *L, int ref, const char *tblKey, const char *subKey, const char *flatKey )
{
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
	if ( m_nTableReference != LUA_NOREF )
		return;
#endif

	char className[ MAX_WEAPON_STRING ];
#if defined ( CLIENT_DLL )
	if ( strlen( GetScriptedClassname() ) > 0 )
		Q_strncpy( className, GetScriptedClassname(), sizeof( className ) );
	else
		Q_strncpy( className, GetClassname(), sizeof( className ) );
#else
	Q_strncpy( m_iScriptedClassname.GetForModify(), GetClassname(), sizeof( className ) );
 	Q_strncpy( className, GetClassname(), sizeof( className ) );
#endif
 	Q_strlower( className );
	// Andrew; This redundancy is pretty annoying.
	// Classname
	Q_strncpy( m_pLuaWeaponInfo->szClassName, className, MAX_WEAPON_STRING );
	SetClassname( className );

	lua_getglobal( L, "weapon" );
	if ( lua_istable( L, -1 ) )
	{
		lua_getfield( L, -1, "get" );
		if ( lua_isfunction( L, -1 ) )
		{
			lua_remove( L, -2 );
			lua_pushstring( L, className );
			luasrc_pcall( L, 1, 1, 0 );
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

	// HL2SB GMod SWEP compat: GMod's engine calls SWEP:SetupDataTables() while it
	// sets a weapon up, and that is where SWEP:NetworkVar() declares the
	// per-instance accessors the script uses (weapon_medkit declares
	// LastAmmoRegen, for one).  Skipping the call left every such method nil, so
	// the weapon's Think() threw "attempt to call a nil value (method
	// 'GetLastAmmoRegen')" once per frame, forever.
	if ( lua_istable( L, -1 ) )
	{
		lua_getfield( L, -1, "SetupDataTables" );
		if ( lua_isfunction( L, -1 ) )
		{
			lua_pushvalue( L, -2 );		// self: the weapon's Lua table
			luasrc_pcall( L, 1, 0, 0 );
		}
		else
		{
			lua_pop( L, 1 );
		}
	}

	m_nTableReference = luaL_ref( L, LUA_REGISTRYINDEX );
#ifndef CLIENT_DLL
	m_pLuaWeaponInfo->bParsedScript = true;
#endif
	// Printable name
	lua_getref( L, m_nTableReference );
	lua_getfield( L, -1, "PrintName" );
	lua_remove( L, -2 );
	if ( !lua_isstring( L, -1 ) || lua_tostring( L, -1 )[0] == '\0' )
	{
		// HL2SB GMod SWEP compat: fall back to the flat HL2SB printname key.
		lua_pop( L, 1 );
		lua_getref( L, m_nTableReference );
		lua_getfield( L, -1, "printname" );
		lua_remove( L, -2 );
	}
	if ( lua_isstring( L, -1 ) )
	{
		Q_strncpy( m_pLuaWeaponInfo->szPrintName, lua_tostring( L, -1 ), MAX_WEAPON_STRING );
	}
	else
	{
		Q_strncpy( m_pLuaWeaponInfo->szPrintName, WEAPON_PRINTNAME_MISSING, MAX_WEAPON_STRING );
	}
	lua_pop( L, 1 );
	// View model & world model.  HL2SB GMod SWEP compat: GMod SWEPs set the
	// capitalised ViewModel/WorldModel and inherit the flat lowercase keys from
	// weapon_hl2mpbase_scriptedweapon (v_357/etc).  Prefer the GMod-style key so
	// the SWEP's own model wins; fall back to the flat key for HL2SB-era scripts.
	lua_getref( L, m_nTableReference );
	lua_getfield( L, -1, "ViewModel" );
	lua_remove( L, -2 );
	if ( !lua_isstring( L, -1 ) || lua_tostring( L, -1 )[0] == '\0' )
	{
		lua_pop( L, 1 );
		lua_getref( L, m_nTableReference );
		lua_getfield( L, -1, "viewmodel" );
		lua_remove( L, -2 );
	}
	if ( lua_isstring( L, -1 ) )
	{
		Q_strncpy( m_pLuaWeaponInfo->szViewModel, lua_tostring( L, -1 ), MAX_WEAPON_STRING );
	}
	lua_pop( L, 1 );
	lua_getref( L, m_nTableReference );
	lua_getfield( L, -1, "WorldModel" );
	lua_remove( L, -2 );
	if ( !lua_isstring( L, -1 ) || lua_tostring( L, -1 )[0] == '\0' )
	{
		lua_pop( L, 1 );
		lua_getref( L, m_nTableReference );
		lua_getfield( L, -1, "playermodel" );
		lua_remove( L, -2 );
	}
	if ( lua_isstring( L, -1 ) )
	{
		Q_strncpy( m_pLuaWeaponInfo->szWorldModel, lua_tostring( L, -1 ), MAX_WEAPON_STRING );
	}
	lua_pop( L, 1 );
	lua_getref( L, m_nTableReference );
	lua_getfield( L, -1, "anim_prefix" );
	lua_remove( L, -2 );
	if ( lua_isstring( L, -1 ) )
	{
		Q_strncpy( m_pLuaWeaponInfo->szAnimationPrefix, lua_tostring( L, -1 ), MAX_WEAPON_PREFIX );
	}
	lua_pop( L, 1 );
	lua_getref( L, m_nTableReference );
	lua_getfield( L, -1, "Slot" );
	lua_remove( L, -2 );
	if ( !lua_isnumber( L, -1 ) )
	{
		// HL2SB GMod SWEP compat: fall back to the flat HL2SB bucket key.
		lua_pop( L, 1 );
		lua_getref( L, m_nTableReference );
		lua_getfield( L, -1, "bucket" );
		lua_remove( L, -2 );
	}
	if ( lua_isnumber( L, -1 ) )
	{
		m_pLuaWeaponInfo->iSlot = lua_tonumber( L, -1 );
	}
	else
	{
		m_pLuaWeaponInfo->iSlot = 0;
	}
	lua_pop( L, 1 );
	lua_getref( L, m_nTableReference );
	lua_getfield( L, -1, "SlotPos" );
	lua_remove( L, -2 );
	if ( !lua_isnumber( L, -1 ) )
	{
		// HL2SB GMod SWEP compat: fall back to the flat HL2SB bucket_position.
		lua_pop( L, 1 );
		lua_getref( L, m_nTableReference );
		lua_getfield( L, -1, "bucket_position" );
		lua_remove( L, -2 );
	}
	if ( lua_isnumber( L, -1 ) )
	{
		m_pLuaWeaponInfo->iPosition = lua_tonumber( L, -1 );
	}
	else
	{
		m_pLuaWeaponInfo->iPosition = 0;
	}
	lua_pop( L, 1 );

	// Use the console (X360) buckets if hud_fastswitch is set to 2.
#ifdef CLIENT_DLL
	if ( hud_fastswitch.GetInt() == 2 )
#else
	if ( IsX360() )
#endif
	{
		lua_getref( L, m_nTableReference );
		lua_getfield( L, -1, "bucket_360" );
		lua_remove( L, -2 );
		if ( lua_isnumber( L, -1 ) )
		{
			m_pLuaWeaponInfo->iSlot = lua_tonumber( L, -1 );
		}
		lua_pop( L, 1 );
		lua_getref( L, m_nTableReference );
		lua_getfield( L, -1, "bucket_position_360" );
		lua_remove( L, -2 );
		if ( lua_isnumber( L, -1 ) )
		{
			m_pLuaWeaponInfo->iPosition = lua_tonumber( L, -1 );
		}
		lua_pop( L, 1 );
	}
	lua_getweaponfield( L, m_nTableReference, "Primary", "ClipSize", "clip_size" );
	if ( lua_isnumber( L, -1 ) )
	{
		m_pLuaWeaponInfo->iMaxClip1 = lua_tonumber( L, -1 );					// Max primary clips gun can hold (assume they don't use clips by default)
	}
	else
	{
		m_pLuaWeaponInfo->iMaxClip1 = WEAPON_NOCLIP;
	}
	lua_pop( L, 1 );
	lua_getweaponfield( L, m_nTableReference, "Secondary", "ClipSize", "clip2_size" );
	if ( lua_isnumber( L, -1 ) )
	{
		m_pLuaWeaponInfo->iMaxClip2 = lua_tonumber( L, -1 );					// Max secondary clips gun can hold (assume they don't use clips by default)
	}
	else
	{
		m_pLuaWeaponInfo->iMaxClip2 = WEAPON_NOCLIP;
	}
	lua_pop( L, 1 );
	lua_getweaponfield( L, m_nTableReference, "Primary", "DefaultClip", "default_clip" );
	if ( lua_isnumber( L, -1 ) )
	{
		m_pLuaWeaponInfo->iDefaultClip1 = lua_tonumber( L, -1 );		// amount of primary ammo placed in the primary clip when it's picked up
	}
	else
	{
		m_pLuaWeaponInfo->iDefaultClip1 = m_pLuaWeaponInfo->iMaxClip1;
	}
	lua_pop( L, 1 );
	lua_getweaponfield( L, m_nTableReference, "Secondary", "DefaultClip", "default_clip2" );
	if ( lua_isnumber( L, -1 ) )
	{
		m_pLuaWeaponInfo->iDefaultClip2 = lua_tonumber( L, -1 );		// amount of secondary ammo placed in the secondary clip when it's picked up
	}
	else
	{
		m_pLuaWeaponInfo->iDefaultClip2 = m_pLuaWeaponInfo->iMaxClip2;
	}
	lua_pop( L, 1 );
	lua_getref( L, m_nTableReference );
	lua_getfield( L, -1, "Weight" );
	lua_remove( L, -2 );
	if ( !lua_isnumber( L, -1 ) )
	{
		// HL2SB GMod SWEP compat: fall back to the flat HL2SB weight key.
		lua_pop( L, 1 );
		lua_getref( L, m_nTableReference );
		lua_getfield( L, -1, "weight" );
		lua_remove( L, -2 );
	}
	if ( lua_isnumber( L, -1 ) )
	{
		m_pLuaWeaponInfo->iWeight = lua_tonumber( L, -1 );
	}
	else
	{
		m_pLuaWeaponInfo->iWeight = 0;
	}
	lua_pop( L, 1 );

	lua_getref( L, m_nTableReference );
	lua_getfield( L, -1, "rumble" );
	lua_remove( L, -2 );
	if ( lua_isnumber( L, -1 ) )
	{
		m_pLuaWeaponInfo->iWeight = lua_tonumber( L, -1 );
	}
	else
	{
		m_pLuaWeaponInfo->iWeight = -1;
	}
	lua_pop( L, 1 );
	
	lua_getref( L, m_nTableReference );
	lua_getfield( L, -1, "showusagehint" );
	lua_remove( L, -2 );
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
		lua_getref( L, m_nTableReference );
		lua_getfield( L, -1, "autoswitchto" );
		lua_remove( L, -2 );
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
	// Same contract as AutoSwitchTo above.
	lua_getfield( L, -1, "AutoSwitchFrom" );
	lua_remove( L, -2 );
	if ( lua_isnil( L, -1 ) )
	{
		lua_pop( L, 1 );
		lua_getref( L, m_nTableReference );
		lua_getfield( L, -1, "autoswitchfrom" );
		lua_remove( L, -2 );
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
	lua_getref( L, m_nTableReference );
	lua_getfield( L, -1, "BuiltRightHanded" );
	lua_remove( L, -2 );
	if ( lua_isnumber( L, -1 ) )
	{
		m_pLuaWeaponInfo->m_bBuiltRightHanded = (int)lua_tointeger( L, -1 ) != 0 ? true : false;
	}
	else
	{
		m_pLuaWeaponInfo->m_bBuiltRightHanded = true;
	}
	lua_pop( L, 1 );
	lua_getref( L, m_nTableReference );
	lua_getfield( L, -1, "AllowFlipping" );
	lua_remove( L, -2 );
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
	lua_getref( L, m_nTableReference );
	lua_getfield( L, -1, "ViewModelFlip" );
	lua_remove( L, -2 );
	if ( lua_isboolean( L, -1 ) || lua_isnumber( L, -1 ) )
	{
		bool bFlippedModel = lua_isboolean( L, -1 ) ? ( lua_toboolean( L, -1 ) != 0 )
													: ( lua_tointeger( L, -1 ) != 0 );
		m_pLuaWeaponInfo->m_bBuiltRightHanded = !bFlippedModel;
		m_pLuaWeaponInfo->m_bAllowFlipping = true;
	}
	lua_pop( L, 1 );

	lua_getweaponfield( L, m_nTableReference, "Primary", "MeleeWeapon", "MeleeWeapon" );
	if ( lua_isnumber( L, -1 ) )
	{
		m_pLuaWeaponInfo->m_bMeleeWeapon = (int)lua_tointeger( L, -1 ) != 0 ? true : false;
	}
	else
	{
		m_pLuaWeaponInfo->m_bMeleeWeapon = false;
	}
	lua_pop( L, 1 );

	// Primary ammo used.  GMod SWEPs use Primary.Ammo; helper falls back flat.
	lua_getweaponfield( L, m_nTableReference, "Primary", "Ammo", "primary_ammo" );
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
	lua_getweaponfield( L, m_nTableReference, "Secondary", "Ammo", "secondary_ammo" );
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
	lua_getref( L, m_nTableReference );
	lua_getfield( L, -1, "SoundData" );
	lua_remove( L, -2 );
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

	lua_getweaponfield( L, m_nTableReference, "Primary", "Damage", "damage" );
	if ( lua_isnumber( L, -1 ) )
	{
		m_pLuaWeaponInfo->m_iPlayerDamage = (int)lua_tointeger( L, -1 );
	}
	lua_pop( L, 1 );

	// HL2SB GMod SWEP compat: GMod's engine calls SWEP:SetupDataTables() while
	// it sets a scripted weapon up, and that is where SWEP:NetworkVar()
	// declares the per-instance accessors the SWEP uses (gmod_camera declares
	// Zoom/Roll there and calls SetZoom()/GetZoom() from Reload, Tick, Equip,
	// CalcView and AdjustMouseSensitivity -- without this call every one of
	// those hooks throws on its first Zoom access).  HL2SB_EntityNetworkVar is
	// the same accessor factory the scripted-entity path installs
	// (basescripted.cpp); the accessors store on the weapon's own Lua table.
	lua_getref( L, m_nTableReference );
	if ( lua_istable( L, -1 ) )
	{
		const int iWeaponTable = lua_gettop( L );

		lua_getglobal( L, "HL2SB_EntityNetworkVar" );
		if ( lua_isfunction( L, -1 ) )
		{
			lua_setfield( L, -2, "NetworkVar" );
		}
		else
		{
			lua_pop( L, 1 );
		}

		lua_getfield( L, -1, "SetupDataTables" );
		if ( lua_isfunction( L, -1 ) )
		{
			lua_pushvalue( L, -2 );		// self: the weapon's Lua table
			luasrc_pcall( L, 1, 0, 0 );
		}
		else
		{
			lua_pop( L, 1 );
		}

		// HL2SB: record the declared NW names (the __hl2sb_nw_* storage fields
		// the factory above created) as a list on the weapon's table.  The Lua
		// seed bridge in weapon_base/shared.lua (hl2sb_nwrequest / hl2sb_nwseed)
		// uses the list to copy the server's initial values to the predicted
		// client, whose storage otherwise starts empty (gmod_camera's Zoom).
		lua_newtable( L );
		const int iNames = lua_gettop( L );
		int iName = 1;

		lua_pushnil( L );
		while ( lua_next( L, iWeaponTable ) != 0 )
		{
			if ( lua_type( L, -2 ) == LUA_TSTRING )
			{
				const char *pszKey = lua_tostring( L, -2 );
				if ( Q_strnicmp( pszKey, "__hl2sb_nw_", 11 ) == 0 )
				{
					lua_pushstring( L, pszKey + 11 );
					lua_rawseti( L, iNames, iName++ );
				}
			}
			lua_pop( L, 1 );
		}

		lua_setfield( L, iWeaponTable, "__hl2sb_nw_names" );

#ifdef CLIENT_DLL
		// Ask the server realm for the initial NW values now that the names
		// are known (weapon_base's hl2sb_nwrequest handler answers with
		// hl2sb_nwseed, written back onto this weapon's storage).
		char szNWRequest[ 64 ];
		Q_snprintf( szNWRequest, sizeof( szNWRequest ), "hl2sb_nwrequest %d", entindex() );
		engine->ClientCmd( szNWRequest );
		luasrc_LuaInfoMsgF( "[HL2SB] nwseed request sent (create): ent=%d\n", entindex() );
#endif
	}
	lua_pop( L, 1 );

	BEGIN_LUA_CALL_WEAPON_METHOD( "Initialize" );
	END_LUA_CALL_WEAPON_METHOD( 0, 0 );
#endif
}

#ifdef CLIENT_DLL
void CHL2MPScriptedWeapon::OnDataChanged( DataUpdateType_t updateType )
{
	BaseClass::OnDataChanged( updateType );

	if ( updateType == DATA_UPDATE_CREATED )
	{
		if ( m_iScriptedClassname.Get() && !m_pLuaWeaponInfo->bParsedScript )
		{
			m_pLuaWeaponInfo->bParsedScript = true;
			SetClassname( m_iScriptedClassname.Get() );
			InitScriptedWeapon();

#ifdef LUA_SDK
			BEGIN_LUA_CALL_WEAPON_METHOD( "Precache" );
			END_LUA_CALL_WEAPON_METHOD( 0, 0 );
#endif
		}
	}
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
	lua_getref( L, m_nTableReference );
	lua_getfield( L, -1, "ViewModel" );
	lua_remove( L, -2 );

	RETURN_LUA_STRING();
#endif

	return BaseClass::GetViewModel();
}

const char *CHL2MPScriptedWeapon::GetWorldModel( void ) const
{
#if defined ( LUA_SDK )
	lua_getref( L, m_nTableReference );
	lua_getfield( L, -1, "WorldModel" );
	lua_remove( L, -2 );

	RETURN_LUA_STRING();
#endif

	return BaseClass::GetWorldModel();
}

const char *CHL2MPScriptedWeapon::GetAnimPrefix( void ) const
{
#if defined ( LUA_SDK )
	lua_getref( L, m_nTableReference );
	lua_getfield( L, -1, "anim_prefix" );
	lua_remove( L, -2 );

	RETURN_LUA_STRING();
#endif

	return BaseClass::GetAnimPrefix();
}

bool CHL2MPScriptedWeapon::UseHands( void ) const
{
#if defined ( LUA_SDK )
	lua_getref( L, m_nTableReference );
	lua_getfield( L, -1, "UseHands" );
	lua_remove( L, -2 );

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
// to the paper "weapons/swep" icon.  (The older WepSelectIcon spelling as a
// surface.GetTextureID() *number* is still not a name, and is still ignored.)
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
	lua_getref( L, m_nTableReference );
	lua_getfield( L, -1, "PrintName" );
	lua_remove( L, -2 );

	RETURN_LUA_STRING();
#endif

	return BaseClass::GetPrintName();
}

int CHL2MPScriptedWeapon::GetMaxClip1( void ) const
{
#if defined ( LUA_SDK )
	lua_getref( L, m_nTableReference );
	lua_getfield( L, -1, "Primary.ClipSize" );
	lua_remove( L, -2 );

	RETURN_LUA_INTEGER();
#endif

	return BaseClass::GetMaxClip1();
}

int CHL2MPScriptedWeapon::GetMaxClip2( void ) const
{
#if defined ( LUA_SDK )
	lua_getref( L, m_nTableReference );
	lua_getfield( L, -1, "Secondary.ClipSize" );
	lua_remove( L, -2 );

	RETURN_LUA_INTEGER();
#endif

	return BaseClass::GetMaxClip2();
}

int CHL2MPScriptedWeapon::GetDefaultClip1( void ) const
{
#if defined ( LUA_SDK )
	lua_getref( L, m_nTableReference );
	lua_getfield( L, -1, "Primary.DefaultClip" );
	lua_remove( L, -2 );

	RETURN_LUA_INTEGER();
#endif

	return BaseClass::GetDefaultClip1();
}

int CHL2MPScriptedWeapon::GetDefaultClip2( void ) const
{
#if defined ( LUA_SDK )
	lua_getref( L, m_nTableReference );
	lua_getfield( L, -1, "Secondary.DefaultClip" );
	lua_remove( L, -2 );

	RETURN_LUA_INTEGER();
#endif

	return BaseClass::GetDefaultClip2();
}


bool CHL2MPScriptedWeapon::IsMeleeWeapon() const
{
#if defined ( LUA_SDK )
	lua_getref( L, m_nTableReference );
	lua_getfield( L, -1, "MeleeWeapon" );
	lua_remove( L, -2 );

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
	lua_getref(L, m_nTableReference );
	lua_getfield( L, -1, "DrawAmmo");
	lua_remove(L, -2);

	RETURN_LUA_BOOLEAN();
#endif
}

int CHL2MPScriptedWeapon::GetWeight( void ) const
{
#if defined ( LUA_SDK )
	lua_getref( L, m_nTableReference );
	lua_getfield( L, -1, "Weight" );
	lua_remove( L, -2 );

	RETURN_LUA_INTEGER();
#endif

	return BaseClass::GetWeight();
}

bool CHL2MPScriptedWeapon::AllowsAutoSwitchTo( void ) const
{
#if defined ( LUA_SDK )
	lua_getref( L, m_nTableReference );
	lua_getfield( L, -1, "AutoSwitchTo" );
	lua_remove( L, -2 );

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
	lua_getref( L, m_nTableReference );
	lua_getfield( L, -1, "ViewModelFOV");
	lua_remove(L, -2 );

	RETURN_LUA_INTEGER();
#endif
	//return BaseClass:GetFOV();
}
#endif

bool CHL2MPScriptedWeapon::AllowsAutoSwitchFrom( void ) const
{
#if defined ( LUA_SDK )
	lua_getref( L, m_nTableReference );
	lua_getfield( L, -1, "AutoSwitchFrom" );
	lua_remove( L, -2 );

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
	lua_getref( L, m_nTableReference );
	lua_getfield( L, -1, "Spawnable");
	lua_remove( L, -2 );

	RETURN_LUA_BOOLEAN();
#endif
}

int CHL2MPScriptedWeapon::GetWeaponFlags( void ) const
{
#if defined ( LUA_SDK )
	lua_getref( L, m_nTableReference );
	lua_getfield( L, -1, "item_flags" );
	lua_remove( L, -2 );
	RETURN_LUA_INTEGER();
#endif

	return BaseClass::GetWeaponFlags();
}

int CHL2MPScriptedWeapon::GetSlot( void ) const
{
#if defined ( LUA_SDK )
	lua_getref( L, m_nTableReference );
	lua_getfield( L, -1, "Slot" );
	lua_remove( L, -2 );

	RETURN_LUA_INTEGER();
#endif

	return BaseClass::GetSlot();
}

int CHL2MPScriptedWeapon::GetPosition( void ) const
{
#if defined ( LUA_SDK )
	lua_getref( L, m_nTableReference );
	lua_getfield( L, -1, "SlotPos" );
	lua_remove( L, -2 );

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
//-----------------------------------------------------------------------------
// HL2SB GMod compat: GMod keeps self.Owner / self.Weapon on the weapon's Lua
// table for every SWEP method call.  Old addons reach for self.Owner directly
// (the minecraft SWEP: self.Owner:GetEyeTrace(), self.Owner:ConCommand(...)),
// so the fields have to exist.  Refreshed on Deploy, which covers every owner
// change (a weapon that changed hands redeploys first).
//-----------------------------------------------------------------------------
void HL2SB_WeaponUpdateLuaOwnerFields( CHL2MPScriptedWeapon *pWeapon )
{
#if defined ( LUA_SDK )
	if ( L == NULL || !lua_isrefvalid( L, pWeapon->m_nTableReference ) )
		return;

	lua_getref( L, pWeapon->m_nTableReference );
	if ( !lua_istable( L, -1 ) )
	{
		lua_pop( L, 1 );
		return;
	}

	lua_pushweapon( L, pWeapon );
	lua_setfield( L, -2, "Weapon" );

	CBasePlayer *pPlayer = ToBasePlayer( pWeapon->GetOwner() );
	if ( pPlayer != NULL )
		lua_pushplayer( L, pPlayer );
	else
		lua_pushnil( L );
	lua_setfield( L, -2, "Owner" );

	lua_pop( L, 1 );
#endif
}

bool CHL2MPScriptedWeapon::Deploy( void )
{
#if defined ( LUA_SDK )
	// HL2SB: refresh self.Owner / self.Weapon BEFORE the script sees the deploy.
	HL2SB_WeaponUpdateLuaOwnerFields( this );

	// GMod: SWEP:Deploy() returning true is the NORMAL case (weapon_base
	// returns true) and does NOT mean "skip the engine default" - only an
	// explicit false cancels the deploy. The engine's DefaultDeploy() has to
	// run: it sets the viewmodel, plays the draw activity, fires
	// WeaponSound(DEPLOY), unhides the weapon and arms m_flNextPrimaryAttack.
	BEGIN_LUA_CALL_WEAPON_METHOD( "Deploy" );
	END_LUA_CALL_WEAPON_METHOD( 0, 1 );

	RETURN_LUA_VETO();
#endif

	return BaseClass::Deploy();
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
	HL2SB_WeaponUpdateLuaOwnerFields( this );

	BEGIN_LUA_CALL_WEAPON_METHOD( "Equip" );
		if ( pOwner != NULL )
			lua_pushentity( L, pOwner );
	END_LUA_CALL_WEAPON_METHOD( pOwner != NULL ? 1 : 0, 0 );
#endif
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
#ifdef CLIENT_DLL
	// HL2SB TEMPORARY diagnostic: the FOV the view actually receives from the
	// camera's TranslateFOV.  First 10 calls after it starts returning values
	// different from the input.
	static float s_flLastDiagFOV = -1.0f;
#endif
	BEGIN_LUA_CALL_WEAPON_METHOD( "TranslateFOV" );
	lua_pushnumber( L, flFOV );
	END_LUA_CALL_WEAPON_METHOD( 1, 1 );
#ifdef CLIENT_DLL
	{
		float flNew = flFOV;
		if ( lua_gettop( L ) > 0 && lua_isnumber( L, -1 ) )
			flNew = ( float )lua_tonumber( L, -1 );
		if ( flNew != s_flLastDiagFOV )
		{
			s_flLastDiagFOV = flNew;
			luasrc_LuaInfoMsgF( "[HL2SB] TranslateFOV: in=%.1f out=%.1f\n", flFOV, flNew );
		}
	}
#endif

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
		if ( lua_isuserdata( L, -2 ) && luaL_checkudata( L, -2, "QAngle" ) )
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
// Purpose: GMod semantics: every SendWeaponAnim restarts the viewmodel
//          animation, so automatic fire shows a kick on every shot. Stock
//          SetIdealActivity early-outs while the same activity is already the
//          ideal one, which leaves scripted viewmodels frozen on the first
//          frame of the shot animation. Invalidating the cached ideal before
//          delegating forces the restart on every call.
//
// HL2SB: the stock chain resolves the activity with SelectWeightedSequence() on
// the WEAPON ENTITY's own model, and only then hands the resulting sequence
// index to the viewmodel entity (CBaseCombatWeapon::SetIdealActivity ->
// SendViewModelAnim).  CBaseCombatWeapon::Equip() and SetActivity() point that
// own model at the VIEWMODEL while a player holds the weapon, which is how
// Valve's content is meant to be resolved: v_smg1.mdl carries the full ACT_VM_*
// list (w_smg1.mdl only carries ACT_VM_IDLE and its range-attack activity).
//
// ⚠️ Measured on 2026-09-13 (the one-shot line below): for a scripted weapon the
// own model IS the viewmodel -- weapon_medkit reports
// weaponModel='models/weapons/c_medkit.mdl' resolves=1 -- so an earlier theory
// that GMod's activity-less world models were breaking this was WRONG.  What the
// log did show is that resolution can still fail for a normal reason:
//
//   SendWeaponAnim 'weapon_flechettegun' activity=172: weaponModel='c_smg1.mdl' resolves=0
//
// models/weapons/c_smg1.mdl simply has no ACT_VM_HOLSTER, so the stock path
// returns false and nothing is animated.
//
// So this fallback is a safety net, not the medkit's fix: when the weapon's own
// model cannot resolve the activity, try the viewmodel entity's own sequence list
// before giving up.  It costs nothing when the stock path works (it is not
// reached), and it turns a silent no-op into either an animation or a log line.
//-----------------------------------------------------------------------------
bool CHL2MPScriptedWeapon::SendWeaponAnim( int iActivity )
{
	m_IdealActivity = ACT_INVALID;
	m_nIdealSequence = -1;

	CBasePlayer *pOwner = ToBasePlayer( GetOwner() );
	CBaseViewModel *pViewModel = ( pOwner != NULL ) ? pOwner->GetViewModel( m_nViewModelIndex, false ) : NULL;

	const bool bOnWeaponModel =
		( SelectWeightedSequence( (Activity)iActivity ) != ACTIVITY_NOT_AVAILABLE );

	// GetScriptedClassname() is client-only, and the client sets its classname
	// from the scripted name, so GetClassname() names the SWEP on both realms.
	char szKey[ 192 ];
	Q_snprintf( szKey, sizeof( szKey ), "weaponanim:%s:%d", GetClassname(), iActivity );

	HL2SB_WarnOnce( szKey,
		"SendWeaponAnim '%s' activity=%d: weaponModel='%s' resolves=%d viewmodel='%s'\n",
		GetClassname(), iActivity, STRING( GetModelName() ), bOnWeaponModel ? 1 : 0,
		( pViewModel != NULL ) ? STRING( pViewModel->GetModelName() ) : "<none>" );

	if ( bOnWeaponModel )
		return BaseClass::SendWeaponAnim( iActivity );

	if ( pViewModel == NULL )
		return false;

	// Make sure the viewmodel carries this weapon's model before asking it to
	// resolve an activity (SendViewModelAnim would do this, but we need the
	// sequence now).
	SetViewModel();

	const int nSequence = pViewModel->SelectWeightedSequence( (Activity)iActivity );

	if ( nSequence == ACTIVITY_NOT_AVAILABLE )
		return false;

	// Keep the weapon's cached ideal in step, so MaintainIdealActivity() and
	// IsViewModelSequenceFinished() agree with what the viewmodel is playing.
	m_IdealActivity = (Activity)iActivity;
	m_nIdealSequence = nSequence;

	SendViewModelAnim( nSequence );

	SetWeaponIdleTime( gpGlobals->curtime + SequenceDuration() );

	return true;
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
#endif

	return BaseClass::Holster( pSwitchingTo );
}

//-----------------------------------------------------------------------------
// Purpose: 
//-----------------------------------------------------------------------------
void CHL2MPScriptedWeapon::ItemPostFrame( void )
{
#if defined ( LUA_SDK )
	// GMod calls SWEP:Think() every frame while the weapon is active.
	BEGIN_LUA_CALL_WEAPON_METHOD( "Think" );
	END_LUA_CALL_WEAPON_METHOD( 0, 0 );

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

#ifdef CLIENT_DLL
	// HL2SB TEMPORARY diagnostic: prove the camera's Tick runs and what its
	// script sees.  Remove once the zoom is confirmed.
	static int s_nCameraTickDiag = 0;
	if ( s_nCameraTickDiag < 12 )
	{
		++s_nCameraTickDiag;
		C_BasePlayer *pLocal = C_BasePlayer::GetLocalPlayer();
		C_BaseCombatWeapon *pWpn = pLocal ? pLocal->GetActiveWeapon() : NULL;
		luasrc_LuaInfoMsgF( "[HL2SB] weapon Tick #%d dispatched classname='%s' active='%s' isscripted=%d\n",
			s_nCameraTickDiag, GetClassname(),
			( pWpn != NULL ) ? pWpn->GetClassname() : "(none)",
			( pWpn != NULL && pWpn->IsScripted() ) ? 1 : 0 );
	}
#endif

#ifdef CLIENT_DLL
	// HL2SB: make sure the NW seed request goes out even if the copy sent from
	// the OnDataChanged/creation context was dropped.  On the first predicted
	// frame of a weapon that declared NW vars without a seed yet, ask again and
	// mark it (weapon_base's hl2sb_nwrequest handler answers).
	if ( m_nTableReference != LUA_NOREF )
	{
		lua_getref( L, m_nTableReference );
		lua_getfield( L, -1, "__hl2sb_nw_names" );
		const bool bHasNames = lua_istable( L, -1 ) != 0;
		lua_pop( L, 1 );

		bool bRequested = false;
		if ( bHasNames )
		{
			lua_getref( L, m_nTableReference );
			lua_getfield( L, -1, "__hl2sb_nw_requested" );
			bRequested = lua_toboolean( L, -1 ) != 0;
			lua_pop( L, 1 );
		}

		if ( bHasNames && !bRequested )
		{
			lua_getref( L, m_nTableReference );
			lua_pushboolean( L, true );
			lua_setfield( L, -2, "__hl2sb_nw_requested" );
			lua_pop( L, 1 );

			char szNWRequest[ 64 ];
			Q_snprintf( szNWRequest, sizeof( szNWRequest ), "hl2sb_nwrequest %d", entindex() );
			engine->ClientCmd( szNWRequest );
			luasrc_LuaInfoMsgF( "[HL2SB] nwseed request sent (first frame): ent=%d\n", entindex() );
		}
	}
#endif

	CBasePlayer *pOwner = ToBasePlayer( GetOwner() );

	if ( pOwner != NULL && m_nTableReference >= 0 )
	{
		const float flTime = gpGlobals->curtime;
		const int nButtons = pOwner->m_nButtons;
		const int nPressed = pOwner->m_afButtonPressed;

		// A Lua SWEP may implement SWEP:ItemPostFrame() itself; returning false
		// means "Lua owns the fire buttons" (the GMod base port did that).
		BEGIN_LUA_CALL_WEAPON_METHOD( "ItemPostFrame" );
		END_LUA_CALL_WEAPON_METHOD( 0, 1 );

		if ( lua_gettop( L ) > 0 && lua_isboolean( L, -1 ) && !lua_toboolean( L, -1 ) )
		{
			lua_pop( L, 1 );
			WeaponIdle();
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
			const int nClipSize1 = lua_getweaponint( L, m_nTableReference, "Primary", "ClipSize", "Primary.ClipSize", -1 );
			if ( nClipSize1 != -1 )
			{
				// Same write as the SetClip1 Lua binding.
				m_iClip1.GetForModify() = lua_getweaponint( L, m_nTableReference, "Primary", "DefaultClip", "Primary.DefaultClip", nClipSize1 );
			}

			const int nClipSize2 = lua_getweaponint( L, m_nTableReference, "Secondary", "ClipSize", "Secondary.ClipSize", -1 );
			if ( nClipSize2 != -1 )
			{
				m_iClip2.GetForModify() = lua_getweaponint( L, m_nTableReference, "Secondary", "DefaultClip", "Secondary.DefaultClip", nClipSize2 );
			}
		}

		const bool bPrimaryAutomatic = lua_getweaponbool( L, m_nTableReference, "Primary", "Automatic", "Primary.Automatic", false );
		const bool bSecondaryAutomatic = lua_getweaponbool( L, m_nTableReference, "Secondary", "Automatic", "Secondary.Automatic", false );

		const bool bPrimaryWants = ( nButtons & IN_ATTACK ) != 0 &&
								   ( bPrimaryAutomatic || ( nPressed & IN_ATTACK ) != 0 );
		const bool bSecondaryWants = ( nButtons & IN_ATTACK2 ) != 0 &&
									 ( bSecondaryAutomatic || ( nPressed & IN_ATTACK2 ) != 0 );

		// Secondary first, the way the GMod base orders it.
		if ( bSecondaryWants && flTime >= m_flNextSecondaryAttack )
		{
			BEGIN_LUA_CALL_WEAPON_METHOD( "SecondaryAttack" );
			END_LUA_CALL_WEAPON_METHOD( 0, 0 );

			if ( m_flNextSecondaryAttack <= flTime )
			{
				// HL2SB GMod compat: SWEP.Secondary.Delay is the GMod engine's
				// fire-rate contract -- a SWEP that only declares the Delay
				// field (and never calls SetNextSecondaryFire) still fires at
				// that rate.  Only applies when the SWEP did not set the time
				// itself; a manual SetNextSecondaryFire inside the hook wins.
				m_flNextSecondaryAttack = flTime + MAX( 0.0f,
					lua_getweaponfloat( L, m_nTableReference, "Secondary", "Delay", "Secondary.Delay", 0.05f ) );
			}
		}
		else if ( bPrimaryWants && flTime >= m_flNextPrimaryAttack )
		{
			BEGIN_LUA_CALL_WEAPON_METHOD( "PrimaryAttack" );
			END_LUA_CALL_WEAPON_METHOD( 0, 0 );

			// The SWEP is expected to call SetNextPrimaryFire(); this stops a
			// script that forgets from firing once per frame.  Same Delay
			// contract as the secondary above: SWEP.Primary.Delay applies
			// unless the hook set the time itself.
			if ( m_flNextPrimaryAttack <= flTime )
			{
				m_flNextPrimaryAttack = flTime + MAX( 0.0f,
					lua_getweaponfloat( L, m_nTableReference, "Primary", "Delay", "Primary.Delay", 0.05f ) );
			}
		}
		else if ( ( nPressed & IN_RELOAD ) != 0 )
		{
			BEGIN_LUA_CALL_WEAPON_METHOD( "Reload" );
			END_LUA_CALL_WEAPON_METHOD( 0, 0 );
		}

		// The engine-side upkeep the HL2 base provided (idle / viewmodel
		// animation), and deliberately none of its ammo or empty-click handling.
		WeaponIdle();
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
#if defined ( LUA_SDK )
	BEGIN_LUA_CALL_WEAPON_METHOD( "ItemBusyFrame" );
	END_LUA_CALL_WEAPON_METHOD( 0, 1 );

	RETURN_LUA_NONE();
#endif

	BaseClass::ItemBusyFrame();
}

#ifndef CLIENT_DLL
int CHL2MPScriptedWeapon::CapabilitiesGet( void )
{
#if defined ( LUA_SDK )
	BEGIN_LUA_CALL_WEAPON_METHOD( "CapabilitiesGet" );
	END_LUA_CALL_WEAPON_METHOD( 0, 1 );

	RETURN_LUA_INTEGER();
#endif

	return BaseClass::CapabilitiesGet();
}
#else
int CHL2MPScriptedWeapon::DrawModel( int flags )
{
#if defined ( LUA_SDK )
	BEGIN_LUA_CALL_WEAPON_METHOD( "DrawModel" );
		lua_pushinteger( L, flags );
	END_LUA_CALL_WEAPON_METHOD( 1, 1 );

	RETURN_LUA_INTEGER();
#endif

	return BaseClass::DrawModel( flags );
}
#endif


