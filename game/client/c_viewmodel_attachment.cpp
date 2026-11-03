//========= Copyright (c) All rights reserved. ============//
//
// Purpose: GMod material proxies for player colours.  The legacy
//          viewmodel-attachment c_hands renderer that used to live in this
//          file was deleted 2026-09-27 (GMod's gmod_hands entity drawn by
//          GM:PostDrawViewModel owns the first-person arms now); what remains
//          is the PlayerColor / PlayerWeaponColor proxy pair + factory that
//          the c_arms and GMod weapon sheets bind.

#include "cbase.h"
#include "c_baseviewmodel.h"
#include "hands_model_mapping.h"
#include "bone_setup.h"
#include "model_types.h"
#include "cliententitylist.h"
#include "gamestringpool.h"
#include "materialsystem/imaterialproxy.h"
#include "materialsystem/imaterialproxyfactory.h"
#include "materialsystem/imaterial.h"
#include "materialsystem/imaterialvar.h"
#include "c_baseplayer.h"
#include "iclientrenderable.h"	// proxy bindable -> entity (CResultProxy::BindArgToEntity inline)
// HL2SB: LUA_NOREF / the entity's Lua table reference, for the PlayerColor proxy's
// "is this a Lua-created clientside model?" test.
#include "luamanager.h"
#include "mathlib/lvector.h"
// HL2SB (2026-10-07): lua_pushmaterial for the Lua matproxy bridge below.
#include "lua/materialsystem/limaterial.h"
#include "tier0/memdbgon.h"

// GMod "PlayerColor"?-style player sleeve color. GMod tints the c_arms sleeves
// with each player's own colour (player:GetPlayerColor), defaulting to a teal
// (62,88,106)/255. Here the local player's sleeve colour is a client convar.
ConVar hl2sb_player_color( "hl2sb_player_color", "0.243 0.345 0.416", FCVAR_ARCHIVE,
	"c_arms sleeve tint (PlayerColor proxy). GMod default teal 62/88/106. Format: 'r g b'" );

// HL2SB_SetWeaponColor lives in the shared player bindings (game/shared/lua/
// lbaseplayer_shared.cpp); the server pushes colour-table entries through this
// console command for both colour kinds.
void HL2SB_SetWeaponColor( int iUserID, const Vector &v );

//-----------------------------------------------------------------------------
// GMod-style per-player colour table entry. GMod networks m_PlayerColor /
// m_WeaponColor on the player data table; the fork's server instead broadcasts
//   hl2sb_setplayercolor <userid> <0=player|1=weapon> <r> <g> <b>
// (normalized floats, HDR >1 allowed exactly like the reference members) for
// every Player:SetPlayerColor / SetWeaponColor write and replays the whole
// table on ClientActive, so every client's colour maps carry every player.
// An optional trailing alpha argument from the old 0-255 format is accepted
// and ignored - the reference colour is a Vector with no alpha.
//-----------------------------------------------------------------------------
static void CC_HL2SB_SetPlayerColor( const CCommand &args )
{
	if ( args.ArgC() < 6 )
		return;
	int iUserID = atoi( args[1] );
	int iWeapon = atoi( args[2] );
	Vector v( atof( args[3] ), atof( args[4] ), atof( args[5] ) );

	if ( iWeapon )
		HL2SB_SetWeaponColor( iUserID, v );
	else
		HL2SB_SetPlayerColor( iUserID, v );
}
static ConCommand hl2sb_setplayercolor( "hl2sb_setplayercolor", CC_HL2SB_SetPlayerColor,
	"Server-pushed per-player colour table entry. Usage: hl2sb_setplayercolor <userid> <0=player|1=weapon> <r> <g> <b>" );

static bool HL2SB_GetLuaPlayerColor( C_BaseEntity *pEntity, Vector &color )
{
	if ( !L || !pEntity || !lua_isrefvalid( L, pEntity->m_nTableReference ) )
		return false;

	lua_getref( L, pEntity->m_nTableReference );
	lua_pushliteral( L, "GetPlayerColor" );
	lua_rawget( L, -2 );
	lua_remove( L, -2 );
	if ( !lua_isfunction( L, -1 ) )
	{
		lua_pop( L, 1 );
		return false;
	}

	CBaseEntity::PushLuaInstanceSafe( L, pEntity );
	if ( luasrc_pcall( L, 1, 1, 0 ) != 0 )
	{
		lua_pop( L, 1 );
		return false;
	}
	bool valid = luaL_testudata( L, -1, "Vector" ) != NULL;
	if ( valid )
		color = luaL_checkvector( L, -1 );
	lua_pop( L, 1 );
	return valid;
}

class CPlayerColorProxy : public IMaterialProxy
{
public:
	CPlayerColorProxy( void ) : m_pColor( NULL ), m_bArmsMaterial( false )
	{
		m_flDefault[0] = 0.2f; m_flDefault[1] = 0.4f; m_flDefault[2] = 0.7f;
	}
	virtual ~CPlayerColorProxy( void ) { }

	virtual bool Init( IMaterial *pMaterial, KeyValues *pKeyValues )
	{
		bool found = false;
		const char *pszResultVar = pKeyValues->GetString( "resultVar", "$color2" );
		m_pColor = pMaterial->FindVar( pszResultVar, &found, false );

		// HL2SB: which colour this material belongs to is a property of the MATERIAL, not
		// of whoever is drawing it.  The arm/sleeve materials are the c_arms_* chain, and
		// deciding by entity class broke as soon as the player model selector opened: a
		// non-matching draw of the same material (the menu's own clientside models) wrote
		// the default white over the arms (2026-09-17).
		const char *pszMatName = pMaterial->GetName();
		m_bArmsMaterial = ( pszMatName != NULL && Q_stristr( pszMatName, "c_arms" ) != NULL );

		// Parse the "default" color (e.g. "0.2 0.4 0.7").
		const char *pszDefault = pKeyValues->GetString( "default", NULL );
		if ( pszDefault )
		{
			sscanf( pszDefault, "%f %f %f", &m_flDefault[0], &m_flDefault[1], &m_flDefault[2] );
		}

		return m_pColor != NULL;
	}

		virtual void OnBind( void *pBindable )
	{
		if ( !m_pColor )
			return;

		Vector v;

		// HL2SB: the arm/sleeve chain always wears the WEAPON colour, and that is
		// decided by the MATERIAL - so no other entity drawing the same material can
		// knock it back to white (opening the player model selector did exactly that,
		// 2026-09-17).  Reference behavior for the weapon colour carries the same
		// always-on pulse as the PlayerWeaponColor proxy (col + col * mul, GMod's
		// lua/matproxy/player_weapon_color.lua), so the sleeves breathe with the gun.
		if ( m_bArmsMaterial )
		{
			C_BasePlayer *pLocal = C_BasePlayer::GetLocalPlayer();
			if ( pLocal != NULL )
				v = HL2SB_GetWeaponColor( pLocal->GetUserID() );
			else
				v.Init( 0.30f, 1.80f, 2.10f );

			float mul = ( 1.0f + sin( gpGlobals->curtime * 5.0f ) ) * 0.5f;
			m_pColor->SetVecValue( v.x + v.x * mul, v.y + v.y * mul, v.z + v.z * mul );
			return;
		}

		// HL2SB: reference behavior reads the colour from the entity being drawn,
		// exactly like GMod's lua/matproxy/player_color.lua: players and ragdolls
		// (through their player) tint with their own colour, anything that has no
		// colour of its own falls back to the teal default.
		C_BaseEntity *pEntity = NULL;

		if ( pBindable != NULL )
		{
			IClientRenderable *pRenderable = ( IClientRenderable * )pBindable;
			IClientUnknown *pUnknown = pRenderable->GetIClientUnknown();

			if ( pUnknown != NULL )
			{
				pEntity = pUnknown->GetBaseEntity();
			}
		}
		if ( pEntity != NULL && HL2SB_GetLuaPlayerColor( pEntity, v ) )
		{
			m_pColor->SetVecValue( v.x, v.y, v.z );
			return;
		}

		C_BasePlayer *pLocal = C_BasePlayer::GetLocalPlayer();
		C_BasePlayer *pPlayer = NULL;

		if ( pEntity != NULL )
		{
			if ( pEntity->IsPlayer() )
			{
				pPlayer = ToBasePlayer( pEntity );
			}
			else if ( dynamic_cast< C_BaseAnimating * >( pEntity ) != NULL &&
					  dynamic_cast< C_BaseAnimating * >( pEntity )->IsRagdoll() )
			{
				// Reference: a corpse is tinted with its player's colour
				// (player_color.lua's GetRagdollOwner hop).  The ragdoll keeps
				// its builder in m_hOwnerEntity.
				pPlayer = ToBasePlayer( pEntity->GetOwnerEntity() );
			}
			else if ( pLocal != NULL && pEntity->m_nTableReference != LUA_NOREF )
			{
				// A Lua-created clientside model (the player model selector's own preview:
				// lua/vgui/DModelPanel.lua's ClientsideModel, which the editor also gives a
				// GetPlayerColor field) follows the local player's colour.  The model list's
				// THUMBNAILS are entities the engine owns and never hand to Lua, so they have
				// no table and take the teal fallback - which is the reference look for
				// colourless entities (2026-09-17).
				pPlayer = pLocal;
			}
			else if ( pLocal != NULL )
			{
				// GMod-style separation, as asked for on 2026-09-17: the ARMS follow the
				// WEAPON colour while the player MODEL follows the player colour.  The
				// arms' VMTs declare "PlayerColor", so they used to move with the skin
				// mixer and the two were inseparable - the weapon mixer looked dead.  The
				// weapon sheets keep their own PlayerWeaponColor proxy.
				const char *pszClass = pEntity->GetClassname();

				if ( pszClass != NULL &&
					 ( Q_stristr( pszClass, "viewmodel" ) != NULL ||
					   Q_stristr( pszClass, "arms" ) != NULL ||
					   Q_stristr( pszClass, "hands" ) != NULL ) )
				{
					v = HL2SB_GetWeaponColor( pLocal->GetUserID() );

					float mul = ( 1.0f + sin( gpGlobals->curtime * 5.0f ) ) * 0.5f;
					m_pColor->SetVecValue( v.x + v.x * mul, v.y + v.y * mul, v.z + v.z * mul );
					return;
				}
			}
		}

		if ( pPlayer != NULL )
		{
			// The colour table is the single source of truth for BOTH realms: the
			// server fills it from the userinfo convars on every spawn
			// (player_sandbox.lua's PLAYER:Spawn, reference behavior), so a convar
			// change shows up exactly when the reference shows it - on the next
			// spawn - and self and others always agree.
			v = HL2SB_GetPlayerColor( pPlayer->GetUserID() );
		}
		else
		{
			// Reference fallback for entities with no GetPlayerColor: the teal
			// Vector(62/255, 88/255, 106/255) from player_color.lua.
			v = HL2SB_GetPlayerColor( -1 );
		}

		// Reference behavior passes the vector to the material untouched - no
		// clamping (GMod's default weapon colour is above 1 on purpose, and the
		// PlayerColor proxy forwards whatever the model wears).
		m_pColor->SetVecValue( v.x, v.y, v.z );
	}

	virtual void Release( void ) { }

	virtual IMaterial *GetMaterial( void ) { return NULL; }

private:
	IMaterialVar	*m_pColor;
	float			m_flDefault[3];
	bool			m_bArmsMaterial;
};

EXPOSE_INTERFACE( CPlayerColorProxy, IMaterialProxy, "PlayerColor" IMATERIAL_PROXY_INTERFACE_VERSION );

//-----------------------------------------------------------------------------
// HL2SB: "PlayerWeaponColor" material proxy - the same idea for the weapon chain.
//
// The GMod weapon sheets and the cstrike / combine arms reference it:
//
//     Proxies { PlayerWeaponColor { resultVar $color2 default 1 1 1 } }
//
// and NOTHING implemented it, so the material system answered
//
//     Error: Material "models/weapons/v_physcannon/v_superphyscannon_sheet"
//            : proxy "PlayerWeaponColor" not found!
//
// and those materials stayed untinted (that is the "weapon colour does nothing /
// the arms have no colour" report, 2026-09-17).  The colour is cl_weaponcolor, the
// convar the player model selector's weapon mixer writes.
//-----------------------------------------------------------------------------
// HL2SB (2026-09-27): the physgun colour proxy.  GMod implements this one in
// LUA (garrysmod/lua/matproxy/player_weapon_color.lua) -- there is no
// "PlayerWeaponColor" symbol in any GMod DLL; the vmts reference it and the
// lua_matproxy system resolves it.  Per-owner colour comes from
// HL2SB_GetWeaponColor (weapon_physgun.cpp / lbaseplayer_shared.cpp).
//-----------------------------------------------------------------------------
Vector HL2SB_GetWeaponColor( int iUserID );

class CPlayerWeaponColorProxy : public IMaterialProxy
{
public:
	CPlayerWeaponColorProxy( void ) : m_pColor( NULL )
	{
		m_flDefault[0] = m_flDefault[1] = m_flDefault[2] = 1.0f;
	}
	virtual ~CPlayerWeaponColorProxy( void ) { }

	virtual bool Init( IMaterial *pMaterial, KeyValues *pKeyValues )
	{
		bool found = false;
		const char *pszResultVar = pKeyValues->GetString( "resultVar", "$color2" );
		m_pColor = pMaterial->FindVar( pszResultVar, &found, false );

		const char *pszDefault = pKeyValues->GetString( "default", NULL );
		if ( pszDefault )
		{
			sscanf( pszDefault, "%f %f %f", &m_flDefault[0], &m_flDefault[1], &m_flDefault[2] );
		}

		return m_pColor != NULL;
	}

	virtual void OnBind( void *pBindable )
	{
		if ( !m_pColor )
			return;

		float r = m_flDefault[0], g = m_flDefault[1], b = m_flDefault[2];

		// HL2SB (2026-09-27): VERBATIM port of GMod's own proxy --
		// garrysmod/lua/matproxy/player_weapon_color.lua:
		//
		//   local col = owner:GetWeaponColor()
		//   local mul = ( 1 + math.sin( CurTime() * 5 ) ) * 0.5
		//   mat:SetVector( self.ResultTo, col + col * mul )
		//
		// i.e. col * (1 + mul): the tint PULSES 1x..2x ALWAYS -- idle and
		// holding are IDENTICAL (GMod has no hold boost in the proxy), and the
		// colour is NOT clamped to 0-1 (GMod's default "0.30 1.80 2.10" is
		// overbright by design).  Per-owner colour like GMod's ent:GetOwner():
		// the drawn entity is either the weapon (its owner) or the player.
		IClientRenderable *pRend = ( IClientRenderable * )pBindable;
		C_BaseEntity *pEntity = pRend ? pRend->GetIClientUnknown()->GetBaseEntity() : NULL;
		C_BasePlayer *pOwner = NULL;
		if ( pEntity != NULL )
		{
			// HL2SB (2026-09-27 fix): the FIRST-PERSON bindable is the
			// C_BaseViewModel, whose owner lives in m_hOwner (GetOwner()) --
			// m_hOwnerEntity is empty on viewmodels, so the old generic
			// GetOwnerEntity() hop resolved NULL and the proxy fell back to
			// the default (1,1,1) = the "gun turned all white" report.  Chain:
			// viewmodel -> its owner, weapon world model -> owner entity,
			// player -> itself.
			C_BaseViewModel *pVM = dynamic_cast< C_BaseViewModel * >( pEntity );
			if ( pVM != NULL )
			{
				pOwner = ToBasePlayer( pVM->GetOwner() );
			}
			else if ( pEntity->IsPlayer() )
			{
				pOwner = ToBasePlayer( pEntity );
			}
			else
			{
				pOwner = ToBasePlayer( pEntity->GetOwnerEntity() );
			}
		}

		// Colour: the per-userid table - the same source the reference's
		// networked member feeds (player_sandbox.lua fills it from the userinfo
		// convar on every spawn), so a mid-session convar change shows up when
		// the reference shows it, on the next spawn, and self and others agree.
		// If the owner still resolves nowhere, fall back to the LOCAL colour --
		// never the raw white default (that flat white was the regression).
		int iUserID = -1;
		if ( pOwner != NULL )
			iUserID = pOwner->GetUserID();
		else if ( C_BasePlayer::GetLocalPlayer() != NULL )
			iUserID = C_BasePlayer::GetLocalPlayer()->GetUserID();

		Vector v( r, g, b );
		if ( iUserID >= 0 )
			v = HL2SB_GetWeaponColor( iUserID );

		// The stock gravity gun uses a fixed color; the physgun uses its owner's.
		if ( pOwner != NULL )
		{
			C_BaseCombatWeapon *pWep = pOwner->GetActiveWeapon();
			if ( pWep != NULL && !pWep->IsScripted() && FClassnameIs( pWep, "weapon_physcannon" ) )
				v = Vector( 0.4f, 1.0f, 1.0f );
		}

		float mul = ( 1.0f + sin( gpGlobals->curtime * 5.0f ) ) * 0.5f;

		m_pColor->SetVecValue( v.x + v.x * mul, v.y + v.y * mul, v.z + v.z * mul );
	}

	virtual void Release( void ) { }

	virtual IMaterial *GetMaterial( void ) { return NULL; }

private:
	IMaterialVar	*m_pColor;
	float			m_flDefault[3];
};

EXPOSE_INTERFACE( CPlayerWeaponColorProxy, IMaterialProxy, "PlayerWeaponColor" IMATERIAL_PROXY_INTERFACE_VERSION );

// HL2SB (2026-10-07): GMod's Lua matproxy bridge.  GMod addons register
// material proxies from Lua (matproxy.Add{ name=..., init=..., bind=... };
// hl1sweps ships HL1Chrome / HL1GaussGlow / SnarkEye this way) and every vmt
// naming such a proxy resolves through the Lua registry instead of failing
// with "proxy not found".  The Lua side (modules/matproxy.lua) already keeps
// ProxyList and ShouldOverrideProxy; nothing consumed them until now.

// True when the Lua matproxy registry has an entry for this proxy name.
// Answers false (fall through to the next factory) whenever Lua is not up --
// material loads that happen before the game Lua state exists keep working.
static bool HL2SB_LuaMatProxyOverride ( const char *pszName )
{
	if ( L == NULL || pszName == NULL || pszName[ 0 ] == '\0' )
		return false;
	lua_getglobal( L, "matproxy" );
	if ( !lua_istable( L, -1 ) )
	{
		lua_pop( L, 1 );
		return false;
	}
	lua_getfield( L, -1, "ShouldOverrideProxy" );
	if ( !lua_isfunction( L, -1 ) )
	{
		lua_pop( L, 2 );
		return false;
	}
	lua_pushstring( L, pszName );
	if ( luasrc_pcall( L, 1, 1, 0 ) != 0 )
	{
		lua_pop( L, 2 );	// nil placeholder + matproxy
		return false;
	}
	bool bOverride = lua_toboolean( L, -1 ) != 0;
	lua_pop( L, 2 );
	return bOverride;
}

// One material's live binding to a Lua proxy.  GMod's C++ side does NOT touch
// the registered table directly: Init pushes matproxy.Init( name, uname,
// material, values ) and OnBind pushes matproxy.Call( name, material, ent ) --
// the Lua module (modules/matproxy.lua, GMod verbatim) owns the per-material
// instance copy in ActiveList keyed by uname, which is what keeps
// "self.ResultTo = values.resultvar" written by one material's init from
// leaking into another material sharing the proxy name.  values is the proxy's
// KeyValues block (hl1sweps reads values.resultvar, a string).
class CLuaMaterialProxy : public IMaterialProxy
{
public:
	CLuaMaterialProxy( const char *pszName ) : m_pMaterial( NULL )
	{
		m_szName[ 0 ] = '\0';
		m_szUName[ 0 ] = '\0';
		if ( pszName != NULL )
			Q_strncpy( m_szName, pszName, sizeof( m_szName ) );
	}
	virtual ~CLuaMaterialProxy( void ) { }

	virtual bool Init( IMaterial *pMaterial, KeyValues *pKeyValues )
	{
		m_pMaterial = pMaterial;
		if ( L == NULL || m_szName[ 0 ] == '\0' || m_pMaterial == NULL )
			return true;

		// GMod: Init silently skips the Lua call when not on the main thread
		// (material instantiation can land off-main during async loads; the
		// material keeps its compiled defaults and there is no error).
		if ( !ThreadInMainThread() )
			return true;

		// ActiveList key: one entry per (material, proxy) pair.
		Q_snprintf( m_szUName, sizeof( m_szUName ), "%s_%s", m_pMaterial->GetName(), m_szName );

		lua_getglobal( L, "matproxy" );					// [mp]
		if ( !lua_istable( L, -1 ) )
		{
			lua_pop( L, 1 );
			return true;
		}
		lua_getfield( L, -1, "Init" );					// [mp][Init]
		if ( !lua_isfunction( L, -1 ) )
		{
			lua_pop( L, 2 );
			return true;
		}
		lua_pushstring( L, m_szName );
		lua_pushstring( L, m_szUName );
		lua_pushmaterial( L, m_pMaterial );

		// values = the proxy's KeyValues block as a plain table with the keys
		// lowercased (GMod lowercases; the addons read values.resultvar).
		lua_newtable( L );
		if ( pKeyValues != NULL )
		{
			for ( KeyValues *pKey = pKeyValues->GetFirstSubKey(); pKey; pKey = pKey->GetNextKey() )
			{
				char szLower[ 64 ];
				Q_strncpy( szLower, pKey->GetName(), sizeof( szLower ) );
				Q_strlower( szLower );
				switch ( pKey->GetDataType() )
				{
				case KeyValues::TYPE_INT:
					lua_pushinteger( L, pKey->GetInt() );
					break;
				case KeyValues::TYPE_FLOAT:
					lua_pushnumber( L, pKey->GetFloat() );
					break;
				default:
					lua_pushstring( L, pKey->GetString() );
					break;
				}
				lua_setfield( L, -2, szLower );
			}
		}

		if ( luasrc_pcall( L, 4, 0, 0 ) != 0 )
			lua_pop( L, 1 );							// nil placeholder
		lua_pop( L, 1 );								// matproxy
		return true;
	}

	virtual void OnBind( void *pBindable )
	{
		if ( L == NULL || m_pMaterial == NULL || m_szName[ 0 ] == '\0' )
			return;

		// GMod gates the bind on the lua_matproxy master switch.
		static ConVarRef s_lua_matproxy( "lua_matproxy" );
		if ( s_lua_matproxy.IsValid() && !s_lua_matproxy.GetBool() )
			return;

		C_BaseEntity *pEntity = NULL;
		if ( pBindable != NULL )
		{
			IClientRenderable *pRenderable = ( IClientRenderable * )pBindable;
			IClientUnknown *pUnknown = pRenderable->GetIClientUnknown();
			if ( pUnknown != NULL )
				pEntity = pUnknown->GetBaseEntity();
		}

		lua_getglobal( L, "matproxy" );					// [mp]
		if ( !lua_istable( L, -1 ) )
		{
			lua_pop( L, 1 );
			return;
		}
		lua_getfield( L, -1, "Call" );					// [mp][Call]
		if ( !lua_isfunction( L, -1 ) )
		{
			lua_pop( L, 2 );
			return;
		}
		// HL2SB (2026-10-08): GMod's matproxy.lua keys ActiveList by the
		// UNAME that Init stored ("material_proxyname"), so Call must be
		// handed the uname -- pushing the plain proxy name answered
		// ActiveList[nil] every bind and the Lua bind function silently
		// never ran (sky_paint.lua attached, resolved, painted nothing).
		lua_pushstring( L, m_szUName );
		lua_pushmaterial( L, m_pMaterial );
		if ( pEntity != NULL )
			CBaseEntity::PushLuaInstanceSafe( L, pEntity );
		else
			lua_pushnil( L );
		// luasrc_pcall was asked for ZERO results: per the 2026-10-04 error
		// convention there is NO nil placeholder to clean up on error (the
		// pcall wrapper already restored the stack to [.., matproxy]).
		// Popping here anyway ran only after the uname fix made binds
		// actually execute: one Lua error in a bind underflowed the shared
		// client Lua stack by one slot PER BIND PER MATERIAL, and the skybox
		// binds errored every frame -- the erosion ended in
		// HL2SB_LuaApiCheckFail (lua_settop underflow) inside R_DrawSkyBox.
		luasrc_pcall( L, 3, 0, 0 );
		lua_pop( L, 1 );								// matproxy
	}

	virtual void Release( void )
	{
		m_pMaterial = NULL;
	}

	virtual IMaterial *GetMaterial( void ) { return m_pMaterial; }

private:
	char m_szName[ 64 ];
	char m_szUName[ 128 ];
	IMaterial *m_pMaterial;
};

// HL2SB: CPlayerColorProxy was only EXPOSE_INTERFACE'd, which never registers it
// with the material system, so any vmt's "PlayerColor" proxy (the GMod player
// model body / sleeve tint chain) found no handler and the colour did nothing.
// Register a proxy factory (chain-preserving) so "PlayerColor" proxies bind.
class CPlayerColorProxyFactory : public IMaterialProxyFactory
{
public:
	CPlayerColorProxyFactory() : m_pOld( NULL ), m_bRegistered( false ) { }
	virtual IMaterialProxy *CreateProxy( const char *proxyName )
	{
		if ( proxyName && !Q_stricmp( proxyName, "PlayerColor" ) )
			return new CPlayerColorProxy;
		if ( proxyName && !Q_stricmp( proxyName, "PlayerWeaponColor" ) )
			return new CPlayerWeaponColorProxy;
		// HL2SB (2026-10-07): Lua-registered proxies (matproxy.Add) resolve
		// here before the "not found" warning -- see CLuaMaterialProxy above.
		// (2026-10-08: "SkyPaint" resolves through this branch too -- the
		// painted-skybox proxy went back to GMod's Lua shape, lua/matproxy/
		// sky_paint.lua + the env_skypaint scripted entity.)
		if ( proxyName && HL2SB_LuaMatProxyOverride( proxyName ) )
			return new CLuaMaterialProxy( proxyName );
		return m_pOld ? m_pOld->CreateProxy( proxyName ) : NULL;
	}
	virtual void DeleteProxy( IMaterialProxy *pProxy )
	{
		if ( pProxy )
			pProxy->Release();
	}
	void SetOld( IMaterialProxyFactory *pOld ) { m_pOld = pOld; }
	bool m_bRegistered;
private:
	IMaterialProxyFactory *m_pOld;
};

static CPlayerColorProxyFactory g_PlayerColorProxyFactory;

// Non-static: called from lModelPanel luaopen_vgui_ModelPanel (client Lua init,
// after the material system is up and before player-model materials compile).
void RegisterPlayerColorProxyFactory()
{
	// Try every time (not gated by m_bRegistered): luaopen_vgui runs for both
	// LGameUI and the game Lua L, and the first one (GameUI) may run before the
	// game material system is ready, so a one-shot gate would skip the good one.
	IMaterialProxyFactory *pNew = &g_PlayerColorProxyFactory;
	if ( materials && materials->GetMaterialProxyFactory() != pNew )
	{
		IMaterialProxyFactory *pOld = materials->GetMaterialProxyFactory();
		g_PlayerColorProxyFactory.SetOld( pOld );
		materials->SetMaterialProxyFactory( pNew );
	}
	Msg( "[HL2SB] PlayerColorProxyFactory: materials=%s\n", materials ? "yes" : "NO" );
}

// Register as soon as the client DLL is loaded (static init).  The material
// system is up before the game DLL is loaded, so `materials` is valid here;
// the proxy factory must be set before any model material that uses a
// "PlayerColor" proxy is compiled.
struct CPlayerColorProxyFactoryReg
{
	CPlayerColorProxyFactoryReg() { RegisterPlayerColorProxyFactory(); }
} g_PlayerColorProxyFactoryReg;


