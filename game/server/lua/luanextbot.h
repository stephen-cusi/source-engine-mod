//========== HL2SB - GMod compat ==========--
//
// Purpose: the Lua half of the NextBot system - GMod's `ENT.Type = "nextbot"`.
//
//   In Garry's Mod an addon nextbot is a plain Lua SENT that says
//
//       ENT.Base = "base_nextbot"     -- a gamemode Lua base (not engine code)
//       ENT.Type = "nextbot"
//
//   and the ENGINE recognises the `nextbot` type, attaching a real INextBot
//   (locomotion, event responders, navigation) to the scripted entity.  This
//   file is that recognition, written against the NextBot stack this fork now
//   compiles (game/server/NextBot, see game/server/nextbot.vpc) instead of
//   GMod's own C++.
//
//   What base_nextbot needs from us, all of it verified against the real
//   GMod file (gamemodes/base/entities/entities/base_nextbot/sv_nextbot.lua):
//
//     * ENT:BehaveStart()   - kick off the behaviour coroutine (ours: the
//                             intention's Reset()).
//     * ENT:BehaveUpdate(f) - resume that coroutine every frame (the
//                             intention's Update()).
//     * ENT:BodyUpdate()    - per-frame animation, called after the bot update.
//     * ENT:On{LeaveGround,LandOnGround,Stuck,UnStuck,Contact,Ignite,
//              NavAreaChanged,OtherKilled,EntitySight,EntitySightLost}
//                          - the INextBotEventResponder set.  Those are exactly
//                            the events Source dispatches into a bot's
//                            components, so the intention forwards them.
//     * ENT:OnTakeDamage / OnInjured / OnKilled / HandleAnimEvent / Precache
//                          - the entity-level ones, dispatched here.
//     * self.loco           - ILocomotion, i.e. GMod's CLuaLocomotion
//                             (SetDesiredSpeed / FaceTowards / IsStuck ...).
//                             Pushed as the "CLuaLocomotion" userdata below.
//     * self:BodyMoveXY()   - move_x/move_y pose from the ground speed.
//     * self:BoundingRadius()
//     * Path("Follow")      - the path follower a bot drives itself with; see
//                             lnavmesh.cpp.
//
//   GMod's ENT:Initialize() is dispatched exactly where CBaseScripted does it
//   (basescripted.cpp:348) - the addon's whole state setup lives there, so it
//   has to run after the script table has been taken.
//===========================================================================//

#ifndef LUA_NEXTBOT_H
#define LUA_NEXTBOT_H

#ifdef LUA_SDK

#include "NextBot.h"
#include "NextBotIntentionInterface.h"
#include "NextBotGroundLocomotion.h"

class CLuaNextBot;

//-----------------------------------------------------------------------------
// The intention.
//
// GMod keeps a nextbot's behaviour in a LUA COROUTINE (base_nextbot's
// RunBehaviour), so there is no Behavior<> object and no action tree here -
// the two overrides below are the whole "AI": Reset() starts the coroutine and
// Update() resumes it.  Everything else is event forwarding.
//
// INextBotComponent's constructor registers `this` with the bot, which is why
// the engine calls Update() on it every frame and why the event responders
// reach the functions below.
//-----------------------------------------------------------------------------
class CLuaNextBotIntention : public IIntention
{
public:
	CLuaNextBotIntention( INextBot *bot );

	virtual void Reset( void );
	virtual void Update( void );

	// INextBotEventResponder: GMod's ENT:On* callbacks.
	virtual void OnLeaveGround( CBaseEntity *entity );
	virtual void OnLandOnGround( CBaseEntity *entity );
	virtual void OnStuck( void );
	virtual void OnUnStuck( void );
	virtual void OnOtherKilled( CBaseCombatCharacter *victim, const CTakeDamageInfo &info );
	virtual void OnContact( CBaseEntity *other, CGameTrace *result = NULL );
	virtual void OnIgnite( void );
	virtual void OnNavAreaChanged( CNavArea *enteredArea, CNavArea *leftArea );
	virtual void OnEntitySight( CBaseEntity *subject );
	virtual void OnEntitySightLost( CBaseEntity *subject );

private:
	CHandle< CLuaNextBot > m_me;
};

//-----------------------------------------------------------------------------
// One Lua nextbot: the scripted entity class GMod's `Type = "nextbot"` maps to.
//
// Deliberately WITHOUT DECLARE_SERVERCLASS(), exactly like this fork's own
// CSimpleBot (game/server/NextBot/simple_bot.h): the entity is then networked
// as its base, NextBotCombatCharacter, which is the class the client already
// knows (C_NextBotCombatCharacter + DT_NextBot, game/client/NextBot/C_NextBot).
// Giving it its own server class would need a matching client class for a
// spawn name the client has no idea about.
//-----------------------------------------------------------------------------
class CLuaNextBot : public NextBotCombatCharacter
{
public:
	DECLARE_CLASS( CLuaNextBot, NextBotCombatCharacter );
	DECLARE_DATADESC();

	CLuaNextBot();
	virtual ~CLuaNextBot();

	virtual void Spawn( void );
	virtual void Precache( void );

	// CBaseEntity: our own think so BodyUpdate() can follow the bot update.
	virtual void Think( void );

	// CBaseEntity: GMod's ENT:OnRemove runs before the entity is deleted.  This
	// class does not inherit CBaseScripted's dispatch, so it repeats it.
	virtual void UpdateOnRemove( void );

	// INextBot
	virtual IIntention *GetIntentionInterface( void ) const			{ return m_intention; }
	virtual NextBotGroundLocomotion *GetLocomotionInterface( void ) const	{ return m_locomotor; }

	// Entity-level events the NextBot base class already funnels; GMod's ENT:*
	// callbacks hang off these.
	virtual int  OnTakeDamage_Alive( const CTakeDamageInfo &info );
	virtual void Event_Killed( const CTakeDamageInfo &info );
	virtual void HandleAnimEvent( animevent_t *event );
	virtual void Ignite( float flFlameLifetime, bool bNPCOnly = true, float flSize = 0.0f, bool bCalledByLevelDesigner = false );

	//-----------------------------------------------------------------------------
	// The Lua call protocol.
	//
	// BeginLuaCall() leaves [function][self] on the stack; the caller pushes its
	// own arguments and EndLuaCall( n ) runs it.  Same contract (and same guard)
	// as BEGIN_LUA_CALL_ENTITY_METHOD in luamanager.h:297.
	//-----------------------------------------------------------------------------
	bool BeginLuaCall( const char *pszFunc );
	void EndLuaCall( int nArgs );

	// HL2SB GMod compat: take this entity's script table, run its one-time
	// setup, and hand it the engine objects base_nextbot expects on `self`.
	bool LoadNextBotScript( void );

	// GMod NextBot:BodyMoveXY()
	void BodyMoveXY( void );

	bool HasScript( void ) const	{ return m_bScriptLoaded; }

	//-----------------------------------------------------------------------------
	// HL2SB: runtime introspection.
	//
	//   "The bot just stands there" has two causes that look identical from the
	//   outside: the engine never reaches the script, or it reaches it and the
	//   script does nothing.  The silent half is BeginLuaCall() not finding the
	//   function - that returns false, prints nothing, and costs the whole
	//   behaviour layer (that is exactly what happened to SCP-096 while
	//   entity.get() aliased base_nextbot to prop_scripted: RunBehaviour existed,
	//   BehaveStart/BehaveUpdate did not, so no coroutine was ever created).
	//
	//   hl2sb_nextbot_status prints the counters below plus which of the
	//   functions the engine looks for the script table actually answers.
	//-----------------------------------------------------------------------------
	void DumpStatus( void );

	int m_nThinkCalls;			// CLuaNextBot::Think() entries
	int m_nBehaveUpdateCalls;	// BehaveUpdate looked up AND called
	int m_nBodyUpdateCalls;		// BodyUpdate looked up AND called
	int m_nLuaErrors;			// dispatches that raised
	int m_nLuaCallMisses;		// dispatches whose function was not in the table
	int m_nLookupErrors;		// lookups that RAISED (an __index metamethod threw)
	// Movement-chain counters, for "the bot does not move": INextBot::Update
	// reaching the locomotion, and the path actually calling Approach().
	int m_nLocoUpdateCalls;
	int m_nApproachCalls;
	int m_nMoveCheckTicks;		// drives the automatic "not moving" report

	//-----------------------------------------------------------------------------
	// GMod's CLuaLocomotion tuning values that Source's locomotion has no setter
	// for.  Kept so a script that asks for them is answered truthfully instead of
	// throwing; see the note where they are set in luanextbot.cpp.
	//-----------------------------------------------------------------------------
	float m_flLuaStepHeight;
	float m_flLuaAcceleration;
	float m_flLuaDeceleration;
	// HL2SB: the other two knobs GMod's CLuaLocomotion owns (jump height and the
	// drop height that counts as fatal), honoured through the locomotion subclass.
	float m_flLuaJumpHeight;
	float m_flLuaDeathDropHeight;
	// Values the script set that this engine's locomotion has no setter for.
	float m_flLuaGravity;
	float m_flLuaMaxYawRate;

private:
	CLuaNextBotIntention	*m_intention;
	NextBotGroundLocomotion	*m_locomotor;
	bool					 m_bScriptLoaded;
};

// Registers `className` as a Lua nextbot entity: CreateEntityByName(name) then
// makes a CLuaNextBot, and the client sees it as NextBotCombatCharacter.
void RegisterLuaNextBot( const char *pszClassname );

// True when `className` was registered through RegisterLuaNextBot().
bool IsLuaNextBot( const char *pszClassname );

// Pushes the CLuaLocomotion userdata wrapping `pBot`'s locomotion interface.
void LuaNextBot_PushLocomotion( lua_State *L, CLuaNextBot *pBot );

#endif // LUA_SDK
#endif // LUA_NEXTBOT_H
