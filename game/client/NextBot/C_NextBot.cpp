// C_NextBot.cpp
// Client-side implementation of Next generation bot system
// Author: Michael Booth, April 2005
//========= Copyright Valve Corporation, All rights reserved. ============//

#include "cbase.h"
#include "C_NextBot.h"
#include "debugoverlay_shared.h"
#include <bitbuf.h>
#include "viewrender.h"

#ifdef LUA_SDK
// HL2SB: the Lua state and the entity push helper, for ENTITY:RenderOverride.
#include "luamanager.h"
#include "lc_baseanimating.h"
#endif

// memdbgon must be the last include file in a .cpp file!!!
#include "tier0/memdbgon.h"

#undef NextBot

ConVar NextBotShadowDist( "nb_shadow_dist", "400" );

//-----------------------------------------------------------------------------
IMPLEMENT_CLIENTCLASS_DT( C_NextBotCombatCharacter, DT_NextBot, NextBotCombatCharacter )
	// HL2SB GMod compat: the Lua classname (mirrors SendPropString in
	// game/server/NextBot/NextBot.cpp).  Without it a Lua nextbot's client half can
	// never find its script - see the note in NextBot.h.
	RecvPropString( RECVINFO( m_iScriptedClassname ) ),
END_RECV_TABLE()


//-----------------------------------------------------------------------------
C_NextBotCombatCharacter::C_NextBotCombatCharacter()
{
	// Left4Dead have surfaces too steep for IK to work properly
	m_EntClientFlags |= ENTCLIENTFLAG_DONTUSEIK;

	m_shadowType = SHADOWS_SIMPLE;
	m_forcedShadowType = SHADOWS_NONE;
	m_bForceShadowType = false;

	m_bLuaInitialized = false;
	m_bInLuaDraw = false;
	m_bLuaRenderGroupRead = false;
	m_nLuaRenderGroup = -1;

	// HL2SB: probe.  If this never prints while a Lua nextbot is alive on the
	// server, the client never built this class for it at all -- and then
	// GetRenderGroup()/DrawModel() are unreachable by construction, which looks
	// exactly like "the bot runs but is invisible".
	static int s_nCtorReports = 0;

	if ( s_nCtorReports < 40 )
	{
		++s_nCtorReports;
		luasrc_LuaWarnMsgF( "[HL2SB] CLIENT C_NextBotCombatCharacter constructed: this=%p entindex=%d",
			(void *)this, entindex() );
	}

	TheClientNextBots().Register( this );
}


//-----------------------------------------------------------------------------
C_NextBotCombatCharacter::~C_NextBotCombatCharacter()
{
	TheClientNextBots().UnRegister( this );
}


//-----------------------------------------------------------------------------
void C_NextBotCombatCharacter::Spawn( void )
{
	BaseClass::Spawn();
}


//-----------------------------------------------------------------------------
void C_NextBotCombatCharacter::UpdateClientSideAnimation()
{
	if (IsDormant())
	{
		return;
	}

	BaseClass::UpdateClientSideAnimation();
}


//--------------------------------------------------------------------------------------------------------
void C_NextBotCombatCharacter::UpdateShadowLOD( void )
{
	ShadowType_t oldShadowType = m_shadowType;

	if ( m_bForceShadowType )
	{
		m_shadowType = m_forcedShadowType;
	}
	else
	{
#ifdef NEED_SPLITSCREEN_INTEGRATION
		FOR_EACH_VALID_SPLITSCREEN_PLAYER( hh )
		{
			C_BasePlayer *pl = C_BasePlayer::GetLocalPlayer(hh);
			if ( pl )
			{
				Vector delta = GetAbsOrigin() - C_BasePlayer::GetLocalPlayer(hh)->GetAbsOrigin();
#else
		{
			if ( C_BasePlayer::GetLocalPlayer() )
			{
				Vector delta = GetAbsOrigin() - C_BasePlayer::GetLocalPlayer()->GetAbsOrigin();
#endif
				if ( delta.IsLengthLessThan( NextBotShadowDist.GetFloat() ) )
				{
					m_shadowType = SHADOWS_RENDER_TO_TEXTURE_DYNAMIC;
				}
				else
				{
					m_shadowType = SHADOWS_SIMPLE;
				}
			}
			else
			{
				m_shadowType = SHADOWS_SIMPLE;
			}
		}
	}

	if ( oldShadowType != m_shadowType )
	{
		DestroyShadow();
	}
}


//--------------------------------------------------------------------------------------------------------
// HL2SB GMod compat: the client half of a Lua nextbot's script.
//
// A client nextbot is a plain C_NextBotCombatCharacter from the network (DT_NextBot)
// and has no per-instance Lua table, so everything here reaches the script through
// scripted_ents.GetStored( classname ).t -- which only works because the server
// networks the Lua classname (m_iScriptedClassname, see NextBot.h).
//
// Dispatching ENTITY:RenderOverride() is what gives a sprite-drawn nextbot its
// entire appearance: the windgrin_npc nextbot draws itself with
// render.SetMaterial + render.DrawSprite there and never draws a model, so with
// nothing dispatching the hook the bot existed but was invisible in game.
//--------------------------------------------------------------------------------------------------------
#ifdef LUA_SDK
//--------------------------------------------------------------------------------------------------------
// HL2SB GMod compat: push the lua/entities table of this entity's script.
//
// A nextbot script's client half is reached by classname, exactly like GMod's
// (scripted_ents.GetStored( classname ).t).  On success the ENT table is the only
// thing left on the stack, on every failure path nothing is left behind.
//
// ⚠️ The WHOLE chain runs inside ONE pcall -- not just the operations that
// obviously call Lua.  The abort dump of the SCP-096 spawn is what proved the
// difference: the failure was in the plain lua_remove() calls that drop the
// temporaries at the end, not in lua_getfield and not in scripted_ents.GetStored.
// The unwind was
//
//     HL2SB_LuaPanic <- luaD_throw <- luaG_errormsg <- luaG_runerror
//       <- typeerror <- luaG_callerror <- luaD_tryfuncTM <- luaD_precall
//       <- ccall <- (callclosemethod / prepcallclosemth) <- luaF_close
//       <- lua_settop <- C_NextBotCombatCharacter::PushLuaScriptTable+0x1c5
//
// i.e. dropping values off the Lua stack ran Lua's own close-on-pop path and the
// slot it had to close held a value with no __close; THAT raised "attempt to call
// a nil value", with no Lua frame on the stack and outside any protected call, so
// it went straight to lua_atpanic.  (The dump's `[C_NextBot.cpp:192]` is those
// three lua_remove() lines.)
//
// GetRenderGroup() reaches this from ComputeFxBlend(), i.e. from the render path,
// so it has to be able to FAIL instead of killing the process.  Returning one
// value from a C closure also removes the lua_remove/lua_settop calls that blew up
// in the first place: lua_pcall truncates the stack itself.
//--------------------------------------------------------------------------------------------------------
static int C_NextBotCombatCharacter_PushScriptTableHelper( lua_State *L )
{
	// Exactly one value is returned on every path: the ENT table, or nil.
	const char *pszClassname = lua_tostring( L, 1 );

	lua_getglobal( L, "scripted_ents" );

	if ( !lua_istable( L, -1 ) )
	{
		lua_pushnil( L );
		return 1;
	}

	lua_getfield( L, -1, "GetStored" );

	if ( !lua_isfunction( L, -1 ) )
	{
		lua_pushnil( L );
		return 1;
	}

	lua_pushstring( L, pszClassname );

	if ( luasrc_pcall( L, 1, 1, 0 ) != 0 || !lua_istable( L, -1 ) )
	{
		lua_pushnil( L );
		return 1;
	}

	luasrc_PushScriptField( L, -1, "t" );

	if ( !lua_istable( L, -1 ) )
	{
		lua_pushnil( L );
		return 1;
	}

	return 1;						// the ENT table
}

bool C_NextBotCombatCharacter::PushLuaScriptTable( void )
{
	if ( L == NULL )
		return false;

	const char *pszClassname = GetClassname();

	if ( pszClassname == NULL || pszClassname[0] == '\0' )
		return false;

	lua_pushcfunction( L, C_NextBotCombatCharacter_PushScriptTableHelper );
	lua_pushstring( L, pszClassname );

	// luasrc_pcall() reports an error with its traceback and pops the message
	// itself, so a failure here leaves nothing behind -- the contract every caller
	// of this function relies on.
	if ( luasrc_pcall( L, 1, 1, 0 ) != 0 )
		return false;

	if ( !lua_istable( L, -1 ) )
	{
		lua_pop( L, 1 );
		return false;
	}

	return true;
}


//--------------------------------------------------------------------------------------------------------
// HL2SB GMod compat: is this render group drawn by ENTITY:DrawTranslucent()?
//
// The wiki splits the two hooks by render group ("ENTITY:Draw is called if and
// when the entity should be drawn opaquely, based on the Entity:GetRenderGroup of
// the entity"), so anything sorted translucently goes to DrawTranslucent.
// RENDER_GROUP_TWOPASS is drawn in both passes and a script that has only one of
// the two hooks wants the translucent one -- that is the sprite case this exists
// for.
//--------------------------------------------------------------------------------------------------------
static bool IsTranslucentRenderGroup( RenderGroup_t group )
{
	return group == RENDER_GROUP_TRANSLUCENT_ENTITY
		|| group == RENDER_GROUP_VIEW_MODEL_TRANSLUCENT
		|| group == RENDER_GROUP_TWOPASS;
}


//--------------------------------------------------------------------------------------------------------
// HL2SB: a protected  t[key]  read.
//
// A script's ENT table can carry an __index FUNCTION (that is how the entity loader
// wires ENT.Base inheritance), and lua_getfield from C is UNPROTECTED: an error in
// such a metamethod goes straight to lua_atpanic and aborts the process.  That is
// exactly how the first client-side nextbot draw killed the game ("attempt to call a
// nil value" with an empty traceback).  Every hook lookup here goes through a pcall.
//
// Leaves exactly one value on the stack; returns true when it is a function.
//--------------------------------------------------------------------------------------------------------
// HL2SB: the body moved to luasrc_PushScriptField() (luamanager.cpp), so the
// engine has ONE protected read instead of a copy per file -- and the other users
// (luamanager.h's BEGIN_LUA_CALL_* macros, CLuaNextBot::BeginLuaCall) now get the
// protection this file already needed.  Same contract as before: exactly one value
// is left on the stack, and the answer is whether it is a function.
static bool LuaNextBot_GetField( lua_State *L, int nTableIdx, const char *pszKey )
{
	return luasrc_PushScriptField( L, nTableIdx, pszKey );
}


//--------------------------------------------------------------------------------------------------------
// HL2SB GMod compat: the CLIENT half of a nextbot script needs a `loco` field.
//
// base_nextbot's client-side ENT:Initialize() opens with
//
//     self.loco:SetDeathDropHeight(600)
//
// and locomotion is SERVER state -- there is no ILocomotion on the client -- so
// LuaNextBot_PushLocomotion() (luanextbot.cpp, server-only) cannot run here.  The
// line then raised "attempt to index a userdata value (field 'loco')" and abandoned
// the rest of Initialize(), which is where npc_verity sets its render bounds and its
// render group: with no bounds a model-less sprite entity is never in the render
// list, so the bot stayed invisible even once it did reach the client.
//
// The stub answers every method with a function that returns nothing, which is what
// a client-side locomotion can honestly answer -- nothing here drives movement.
//--------------------------------------------------------------------------------------------------------
static int LuaNextBot_ClientLocoMethod( lua_State *L )
{
	lua_pushnil( L );
	return 1;
}

static int LuaNextBot_ClientLocoIndex( lua_State *L )
{
	lua_pushcfunction( L, LuaNextBot_ClientLocoMethod );
	return 1;
}

static void LuaNextBot_EnsureClientLocomotion( lua_State *L, int nTable )
{
	LuaNextBot_GetField( L, nTable, "loco" );	// leaves the current value


	if ( !lua_isnil( L, -1 ) )
	{
		lua_pop( L, 1 );
		return;									// the script brought its own
	}

	lua_pop( L, 1 );

	lua_newtable( L );							// the stub
	lua_newtable( L );							// ... with a metatable
	lua_pushcfunction( L, LuaNextBot_ClientLocoIndex );
	lua_setfield( L, -2, "__index" );
	lua_setmetatable( L, -2 );
	lua_setfield( L, nTable, "loco" );
}


//--------------------------------------------------------------------------------------------------------
// HL2SB GMod compat: Path() exists on the client too.
//
// Path() is installed by luanextbot.cpp, which is server-only, so on the client the
// global was simply absent.  That is not the harmless gap it sounds like: an
// obfuscated script (windgrin_npc's whole body is one encoded blob that runs the
// same code on both realms) calls Path( "Follow" ) from the client half as well, and
// every such call raised
//
//     attempt to call a nil value (global 'Path')
//
// A client-side PathFollower can only be a stub anyway -- the path, the nav mesh and
// the bot's ILocomotion are all server state -- so the stub answers every method with
// a no-op instead of throwing.
//--------------------------------------------------------------------------------------------------------
static int LuaNextBot_ClientPathFactory( lua_State *L )
{
	lua_newtable( L );								// the PathFollower stand-in
	lua_newtable( L );								// ... with a metatable
	lua_pushcfunction( L, LuaNextBot_ClientLocoIndex );
	lua_setfield( L, -2, "__index" );
	lua_setmetatable( L, -2 );
	return 1;
}

static void LuaNextBot_EnsureClientPathGlobal( lua_State *L )
{
	lua_getglobal( L, "Path" );

	const bool bPresent = lua_isfunction( L, -1 );

	lua_pop( L, 1 );

	if ( bPresent )
		return;

	lua_pushcfunction( L, LuaNextBot_ClientPathFactory );
	lua_setglobal( L, "Path" );
}
#endif // LUA_SDK


//--------------------------------------------------------------------------------------------------------
// HL2SB GMod compat: the client half of a Lua nextbot's script.
//
//   m_iScriptedClassname is the classname the SERVER's script is registered under
//   ("npc_verity").  The entity itself is networked as NextBotCombatCharacter, so
//   the client's own classname is the base class's; adopting the networked name is
//   what makes scripted_ents.GetStored() -- and every ent:GetClass() test in the
//   script's client half -- see the right thing.  CBaseScripted does exactly this
//   (basescripted.cpp OnDataChanged()).
//
// Then ENT:Initialize() runs, once.  GMod runs it when the client entity is created
// and scripts depend on it: npc_verity sets its render bounds and starts its music
// timer there, and since its ENT:DrawTranslucent draws a sprite with no model at
// all, those bounds are what keep the entity in the render list to begin with.
//--------------------------------------------------------------------------------------------------------
//--------------------------------------------------------------------------------------------------------
// HL2SB GMod compat: a Lua nextbot interpolates whether or not it has a model.
//
// C_BaseEntity::ShouldInterpolate() short-circuits on "index == 0 || !GetModel()",
// and every sprite nextbot is model-less by design -- npc_verity draws a quad in
// ENT:DrawTranslucent, npc_windgrinbot the same -- so the client never interpolated
// their origins.  The sprite then snapped to each networked position: the server
// updates a bot's locomotion at nb_update_frequency (0.1s = 10 Hz), and at a 650 u/s
// run speed that is roughly a 65 unit jump every step, which reads as stuttering.
//
// GMod's sprite entities move smoothly because they interpolate, so answer true
// here rather than weakening the model test for every other entity in the game.
//--------------------------------------------------------------------------------------------------------
bool C_NextBotCombatCharacter::ShouldInterpolate( void )
{
	return true;
}


void C_NextBotCombatCharacter::OnDataChanged( DataUpdateType_t updateType )
{
	BaseClass::OnDataChanged( updateType );

#ifdef LUA_SDK
	// HL2SB: probe the client entity's arrival.  If this never fires for a spawned
	// nextbot then the client never built a C_NextBotCombatCharacter for it, and
	// "DrawModel is never called" means something else is drawing it (or nothing
	// is) -- a different problem from "it is not in the render list".
	if ( updateType == DATA_UPDATE_CREATED )
	{
		static int s_nCreatedReports = 0;

		if ( s_nCreatedReports < 40 )
		{
			++s_nCreatedReports;
			luasrc_LuaWarnMsgF( "[HL2SB] CLIENT nextbot entity created: networked='%s' classname='%s' model='%s' luaInit=%d",
				m_iScriptedClassname.Get(), GetClassname(), GetModelName(), (int)m_bLuaInitialized );
		}
	}
#endif

#ifdef LUA_SDK
	if ( updateType == DATA_UPDATE_CREATED && !m_bLuaInitialized )
	{
		const char *pszName = m_iScriptedClassname.Get();

		if ( pszName != NULL && pszName[0] != '\0' )
		{
			SetClassname( pszName );
			m_bLuaInitialized = true;

			if ( PushLuaScriptTable() )
			{
				const int nTable = lua_gettop( L );

				// HL2SB: before Initialize() runs -- see
				// LuaNextBot_EnsureClientLocomotion().
				LuaNextBot_EnsureClientLocomotion( L, nTable );
				LuaNextBot_EnsureClientPathGlobal( L );

				const bool bHasInit = LuaNextBot_GetField( L, nTable, "Initialize" );

				if ( bHasInit )
				{
					lua_pushanimating( L, this );	// self
					luasrc_pcall( L, 1, 0, 0 );
				}
				else
				{
					lua_pop( L, 1 );
				}

				lua_pop( L, 1 );					// ENT table

				// HL2SB: a MODEL-LESS nextbot is drawn only from a script hook
				// (npc_verity has no ENT.Model at all -- ENT:DrawTranslucent draws it
				// with render.DrawQuadEasy).  C_BaseAnimating only puts an entity into
				// the leaf system from InitializeAsClientEntity(), which wants a model,
				// so those entities were never added: DrawModel() never ran and the bot
				// walked around perfectly while being completely invisible.
				//
				// Idempotent for everything else -- with a render handle already in
				// place, AddToLeafSystem() only updates the group.
				AddToLeafSystem();

				static int s_nInitReports = 0;

				if ( s_nInitReports < 40 )
				{
					++s_nInitReports;

					Vector vecMins, vecMaxs;
					GetRenderBounds( vecMins, vecMaxs );

					luasrc_LuaWarnMsgF( "[HL2SB] CLIENT nextbot '%s' initialised: Initialize=%d bounds=(%.0f %.0f %.0f)-(%.0f %.0f %.0f) scriptedBounds=%d group=%d renderable=%s",
						GetClassname(), (int)bHasInit,
						vecMins.x, vecMins.y, vecMins.z, vecMaxs.x, vecMaxs.y, vecMaxs.z,
						(int)HasScriptedRenderBounds(), (int)GetRenderGroup(),
						( GetRenderHandle() == INVALID_CLIENT_RENDER_HANDLE ) ? "INVALID" : "ok" );
				}
			}
		}
	}
#endif
}


//--------------------------------------------------------------------------------------------------------
// HL2SB GMod compat: Entity:GetRenderGroup() answers the script's ENT.RenderGroup
// field, which is what the two draw hooks are chosen by (see the wiki).  Reading it
// is also what moves the entity into the engine's translucent render list, so a
// sprite like npc_verity's is sorted and drawn in the right pass.
//
// Cached: the engine calls this from ComputeFxBlend() every frame.
//--------------------------------------------------------------------------------------------------------
RenderGroup_t C_NextBotCombatCharacter::GetRenderGroup( void )
{
#ifdef LUA_SDK
	// HL2SB: probe the ENTRY first.  "A sprite nextbot is invisible" has three
	// possible causes -- this function is never asked, the script table is not
	// found, or the draw hook is not there -- and without an entry line the first
	// two look identical (both print nothing at all).
	static int s_nRenderGroupReports = 0;
	const bool bReport = ( s_nRenderGroupReports < 40 );

	if ( bReport )
	{
		++s_nRenderGroupReports;
		luasrc_LuaWarnMsgF( "[HL2SB] nextbot '%s' GetRenderGroup CALLED: read=%d L=%s model='%s'",
			GetClassname(), (int)m_bLuaRenderGroupRead, ( L != NULL ) ? "set" : "NULL", GetModelName() );
	}

	if ( L != NULL && !m_bLuaRenderGroupRead )
	{
		if ( !PushLuaScriptTable() )
		{
			if ( bReport )
				luasrc_LuaWarnMsgF( "[HL2SB] nextbot '%s' GetRenderGroup: PUSH SCRIPT TABLE FAILED -> engine group",
					GetClassname() );
		}
		else
		{
			const int nTable = lua_gettop( L );

			m_bLuaRenderGroupRead = true;
			m_nLuaRenderGroup = -1;

			// Protected read: the value is not a function, only the lookup matters.
			LuaNextBot_GetField( L, nTable, "RenderGroup" );

			int nScript = -1;

			if ( lua_isnumber( L, -1 ) )
				nScript = (int)lua_tonumber( L, -1 );

			if ( nScript >= 0 && nScript < RENDER_GROUP_COUNT )
				m_nLuaRenderGroup = nScript;

			lua_pop( L, 2 );		// RenderGroup, ENT table

			// This single value decides both which draw hook is used and whether the
			// entity lands in the translucent render list.
			if ( bReport )
				luasrc_LuaWarnMsgF( "[HL2SB] nextbot '%s' GetRenderGroup: script=%d -> group=%d translucent=%d (engine base=%d)",
					GetClassname(), nScript, m_nLuaRenderGroup,
					(int)IsTranslucentRenderGroup( (RenderGroup_t)m_nLuaRenderGroup ),
					(int)BaseClass::GetRenderGroup() );
		}
	}

	if ( m_nLuaRenderGroup >= 0 )
		return (RenderGroup_t)m_nLuaRenderGroup;
#endif

	return BaseClass::GetRenderGroup();
}


//--------------------------------------------------------------------------------------------------------
// HL2SB GMod compat: the script draws the entity itself.
//
// GMod's order: ENTITY:RenderOverride() first ("Called instead of the engine
// drawing function of the entity", and it works on any entity at all), then the
// render group decides between ENTITY:Draw() and ENTITY:DrawTranslucent().  A
// nextbot always has both of those, because base_nextbot/shared.lua defines them
// (Draw -> self:DrawModel(), DrawTranslucent -> self:Draw()).
//
// Returning 1 means a script hook drew the entity and the engine's own model draw
// is skipped -- that is what those hooks mean ("instead of"), and it is what lets a
// model-less sprite entity like npc_verity draw at all.
//--------------------------------------------------------------------------------------------------------
int C_NextBotCombatCharacter::DrawModel( int flags )
{
#ifdef LUA_SDK
	// Re-entry guard: base_nextbot's ENT:Draw() calls self:DrawModel(), which lands
	// right back here and would recurse for ever.  (GMod documents exactly this:
	// Entity:DrawModel() "only draws the entity's model itself" when called inside
	// ENTITY:Draw / ENTITY:DrawTranslucent.)
	// HL2SB: "the sprite nextbot is invisible" has exactly three places it can go
	// wrong -- this hook never being called at all, the script table not being
	// found, or the chosen hook not being there -- so all three are reported, once
	// per entity, bounded.  Without this the whole path is silent and invisible
	// entities and unreached code look identical.
	// HL2SB: report per CLASSNAME, not under one global cap.  A total cap hid every
	// bot that spawned after the first few seconds of the session -- and "did the
	// bot that appeared after the level change ever draw?" is precisely what this
	// probe exists to answer.
	struct HL2SB_DrawReport_t { const char *pszClass; int nReports; };
	static HL2SB_DrawReport_t s_DrawReports[8];

	const char *pszScriptClass = GetScriptedClassname();
	HL2SB_DrawReport_t *pSlot = NULL;

	for ( int i = 0; i < ARRAYSIZE( s_DrawReports ); ++i )
	{
		if ( s_DrawReports[i].pszClass == pszScriptClass )
		{
			pSlot = &s_DrawReports[i];
			break;
		}

		if ( s_DrawReports[i].pszClass == NULL && pSlot == NULL )
			pSlot = &s_DrawReports[i];
	}

	const bool bReport = ( pSlot != NULL && pSlot->nReports < 3 );

	if ( bReport )
	{
		pSlot->pszClass = pszScriptClass;
		++pSlot->nReports;
	}

	if ( L == NULL || m_bInLuaDraw || !PushLuaScriptTable() )
	{
		if ( bReport )
		{
			luasrc_LuaWarnMsgF( "[HL2SB] nextbot '%s' DrawModel: ENGINE draw (L=%s inLuaDraw=%d scriptTable=%s) group=%d model='%s'",
				GetClassname(), ( L != NULL ) ? "set" : "NULL", (int)m_bInLuaDraw,
				( L == NULL || m_bInLuaDraw ) ? "not-tried" : "MISSING",
				(int)GetRenderGroup(), GetModelName() );
		}
	}
	else
	{
		const int nTable = lua_gettop( L );
		m_bInLuaDraw = true;

		const char *pszFunc = "RenderOverride";
		bool bFound = LuaNextBot_GetField( L, nTable, pszFunc );

		if ( !bFound )
		{
			lua_pop( L, 1 );

			pszFunc = IsTranslucentRenderGroup( GetRenderGroup() ) ? "DrawTranslucent" : "Draw";
			bFound = LuaNextBot_GetField( L, nTable, pszFunc );

			if ( !bFound )
			{
				lua_pop( L, 1 );
				pszFunc = NULL;
			}
		}

		bool bDrew = false;
		int nCallError = 0;

		if ( pszFunc != NULL )
		{
			lua_pushanimating( L, this );	// self
			lua_pushinteger( L, flags );	// STUDIO_* flags, per the wiki
			nCallError = luasrc_pcall( L, 2, 0, 0 );
			bDrew = ( nCallError == 0 );
		}

		m_bInLuaDraw = false;
		lua_pop( L, 1 );					// ENT table

		if ( bReport )
		{
			luasrc_LuaWarnMsgF( "[HL2SB] nextbot '%s' DrawModel: hook=%s found=%d callError=%d drew=%d group=%d model='%s'",
				GetClassname(), ( pszFunc != NULL ) ? pszFunc : "NONE", (int)bFound,
				nCallError, (int)bDrew, (int)GetRenderGroup(), GetModelName() );
		}

		// Only claim the script drew the entity when the hook actually ran: a
		// script that errors must leave a visible entity behind, not an invisible one.
		if ( bDrew )
			return 1;
	}
#endif

	return BaseClass::DrawModel( flags );
}

//--------------------------------------------------------------------------------------------------------
ShadowType_t C_NextBotCombatCharacter::ShadowCastType( void ) 
{
	if ( !IsVisible() )
		return SHADOWS_NONE;

	if ( m_shadowTimer.IsElapsed() )
	{
		m_shadowTimer.Start( 0.15f );
		UpdateShadowLOD();
	}

	return m_shadowType;
}


//--------------------------------------------------------------------------------------------------------
bool C_NextBotCombatCharacter::GetForcedShadowCastType( ShadowType_t* pForcedShadowType ) const
{
	if ( pForcedShadowType )
	{
		*pForcedShadowType = m_forcedShadowType;
	}
	return m_bForceShadowType;
}

//--------------------------------------------------------------------------------------------------------
/**
 * Singleton accessor.
 * By returning a reference, we guarantee construction of the 
 * instance before its first use.
 */
C_NextBotManager &TheClientNextBots( void )
{
	static C_NextBotManager manager;
	return manager;
}


//--------------------------------------------------------------------------------------------------------
C_NextBotManager::C_NextBotManager( void )
{
}


//--------------------------------------------------------------------------------------------------------
C_NextBotManager::~C_NextBotManager()
{
}


//--------------------------------------------------------------------------------------------------------
void C_NextBotManager::Register( C_NextBotCombatCharacter *bot )
{
	m_botList.AddToTail( bot );
}


//--------------------------------------------------------------------------------------------------------
void C_NextBotManager::UnRegister( C_NextBotCombatCharacter *bot )
{
	m_botList.FindAndRemove( bot );
}

//-----------------------------------------------------------------------------
// Purpose:
//-----------------------------------------------------------------------------
bool C_NextBotManager::SetupInFrustumData( void )
{
#ifdef ENABLE_AFTER_INTEGRATION
	// Done already this frame.
	if ( IsInFrustumDataValid() )
		return true;

	// Can we use the view data yet?
	if ( !FrustumCache()->IsValid() )
		return false;

	// Get the number of active bots.
	int nBotCount = m_botList.Count();

	// Reset.
	for ( int iBot = 0; iBot < nBotCount; ++iBot )
	{
		// Get the current bot.
		C_NextBotCombatCharacter *pBot = m_botList[iBot];
		if ( !pBot )
			continue;

		pBot->InitFrustumData();
	}

	FOR_EACH_VALID_SPLITSCREEN_PLAYER( iSlot )
	{
		ACTIVE_SPLITSCREEN_PLAYER_GUARD( iSlot );
		// Get the active local player.
		C_BasePlayer *pPlayer = C_BasePlayer::GetLocalPlayer();
		if ( !pPlayer )
			continue;

		for ( int iBot = 0; iBot < nBotCount; ++iBot )
		{
			// Get the current bot.
			C_NextBotCombatCharacter *pBot = m_botList[iBot];
			if ( !pBot )
				continue;

			// Are we in the view frustum?
			Vector vecMin, vecMax;
			pBot->CollisionProp()->WorldSpaceAABB( &vecMin, &vecMax );
			bool bInFrustum = !FrustumCache()->m_Frustums[iSlot].CullBox( vecMin, vecMax );
		
			if ( bInFrustum )
			{
				Vector vecSegment;
				VectorSubtract( pBot->GetAbsOrigin(), pPlayer->GetAbsOrigin(), vecSegment );
				float flDistance = vecSegment.LengthSqr();
				if ( flDistance < pBot->GetInFrustumDistanceSqr() )
				{
					pBot->SetInFrustumDistanceSqr( flDistance );
				}	

				pBot->SetInFrustum( true );
			}
		}
	}

	// Mark as setup this frame.
	m_nInFrustumFrame = gpGlobals->framecount;
#endif

	return true;
}

//--------------------------------------------------------------------------------------------------------
