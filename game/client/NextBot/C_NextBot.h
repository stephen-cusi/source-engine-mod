// C_NextBot.h
// Next generation bot system
// Author: Michael Booth, April 2005
//========= Copyright Valve Corporation, All rights reserved. ============//

#ifndef _C_NEXT_BOT_H_
#define _C_NEXT_BOT_H_

#include "c_ai_basenpc.h"
#include "networkvar.h"

//----------------------------------------------------------------------------------------------------------------
/**
* The interface holding IBody information
*/
class IBodyClient
{
public:
	enum ActivityType 
	{ 
		MOTION_CONTROLLED_XY	= 0x0001,	// XY position and orientation of the bot is driven by the animation.
		MOTION_CONTROLLED_Z		= 0x0002,	// Z position of the bot is driven by the animation.
		ACTIVITY_UNINTERRUPTIBLE= 0x0004,	// activity can't be changed until animation finishes
		ACTIVITY_TRANSITORY		= 0x0008,	// a short animation that takes over from the underlying animation momentarily, resuming it upon completion
		ENTINDEX_PLAYBACK_RATE	= 0x0010,	// played back at different rates based on entindex
	};
};


//--------------------------------------------------------------------------------------------------------
/**
 * The client-side implementation of the NextBot
 */
class C_NextBotCombatCharacter : public C_BaseCombatCharacter
{
public:
	DECLARE_CLASS( C_NextBotCombatCharacter, C_BaseCombatCharacter );
	DECLARE_CLIENTCLASS();

	C_NextBotCombatCharacter();
	virtual ~C_NextBotCombatCharacter();

public:	
	virtual void Spawn( void );
	virtual void UpdateClientSideAnimation( void );
	virtual ShadowType_t ShadowCastType( void );
	// HL2SB GMod compat: the client half of a Lua nextbot script.  OnDataChanged()
	// adopts the networked Lua classname and runs ENT:Initialize(); DrawModel()
	// dispatches ENTITY:RenderOverride() / ENTITY:DrawTranslucent() / ENTITY:Draw();
	// GetRenderGroup() answers the script's ENT.RenderGroup field.
	virtual void OnDataChanged( DataUpdateType_t updateType );
	virtual RenderGroup_t GetRenderGroup( void );
	virtual int DrawModel( int flags );
	virtual bool IsNextBot() { return true; }

	// HL2SB GMod compat: interpolate even though a Lua nextbot has no model.
	//
	// C_BaseEntity::ShouldInterpolate() answers false for anything without a model,
	// which is every sprite nextbot (npc_verity and npc_windgrinbot both have
	// model='(null)').  Their origins were therefore never interpolated and the
	// sprite snapped to each networked position -- 10 Hz, and at a 650 u/s run
	// speed that is a ~65 unit jump per step, i.e. visibly stuttering movement.
	virtual bool ShouldInterpolate( void );

	// HL2SB: the Lua classname, networked from NextBotCombatCharacter (see the note
	// in game/server/NextBot/NextBot.h).  Mirrored by RecvPropString in the .cpp.
	CNetworkString( m_iScriptedClassname, 255 );

	const char *GetScriptedClassname( void )
	{
		const char *pszName = m_iScriptedClassname.Get();
		return ( pszName != NULL && pszName[0] != '\0' ) ? pszName : GetClassname();
	}
	void ForceShadowCastType( bool bForce, ShadowType_t forcedShadowType = SHADOWS_NONE ) { m_bForceShadowType = bForce; m_forcedShadowType = forcedShadowType; }
	bool GetForcedShadowCastType( ShadowType_t* pForcedShadowType ) const;

	// Local In View Data.
	void InitFrustumData( void )						{ m_bInFrustum = false; m_flFrustumDistanceSqr = FLT_MAX; m_nInFrustumFrame = gpGlobals->framecount; }
	bool IsInFrustumValid( void )						{ return ( m_nInFrustumFrame == gpGlobals->framecount ); }
	void SetInFrustum( bool bInFrustum )				{ m_bInFrustum = bInFrustum; }
	bool IsInFrustum( void )							{ return m_bInFrustum; }
	void SetInFrustumDistanceSqr( float flDistance )	{ m_flFrustumDistanceSqr = flDistance; }
	float GetInFrustumDistanceSqr( void )				{ return m_flFrustumDistanceSqr; }

private:
	ShadowType_t	m_shadowType;			// Are we LOD'd to simple shadows?
	CountdownTimer m_shadowTimer;	// Timer to throttle checks for shadow LOD
	ShadowType_t	m_forcedShadowType;
	bool			m_bForceShadowType;
	void UpdateShadowLOD( void );

	// HL2SB GMod compat, client half of a Lua nextbot script.
	bool m_bLuaInitialized;		// ENT:Initialize() has been dispatched
	bool m_bInLuaDraw;			// inside a Lua draw hook (self:DrawModel() re-entry guard)
	bool m_bLuaRenderGroupRead;	// ENT.RenderGroup has been read (it is a constant)
	int  m_nLuaRenderGroup;		// ... and its value, -1 when the script has none
	bool PushLuaScriptTable( void );	// leaves the script's ENT table on the stack

	// Local In View Data.
	int			m_nInFrustumFrame;
	bool		m_bInFrustum;
	float		m_flFrustumDistanceSqr;

private:
	C_NextBotCombatCharacter( const C_NextBotCombatCharacter & );				// not defined, not accessible
};

//--------------------------------------------------------------------------------------------------------
/**
 * The C_NextBotManager manager 
 */
class C_NextBotManager
{
public:
	C_NextBotManager( void );
	~C_NextBotManager();

	/**
	 * Execute functor for each NextBot in the system.
	 * If a functor returns false, stop iteration early
	 * and return false.
	 */	
	template < typename Functor >
	bool ForEachCombatCharacter( Functor &func )
	{
		for( int i=0; i < m_botList.Count(); ++i )
		{
			C_NextBotCombatCharacter *character = m_botList[i];
			if ( character->IsPlayer() )
			{
				continue;
			}

			if ( character->IsDormant() )
			{
				continue;
			}

			if ( !func( character ) )
			{
				return false;
			}
		}

		return true;
	}

	int	 GetActiveCount()						    { return m_botList.Count(); }

	bool SetupInFrustumData( void );
	bool IsInFrustumDataValid( void )				{ return ( m_nInFrustumFrame == gpGlobals->framecount ); }

private:
	friend class C_NextBotCombatCharacter;

	void Register( C_NextBotCombatCharacter *bot );
	void UnRegister( C_NextBotCombatCharacter *bot );

	CUtlVector< C_NextBotCombatCharacter * > m_botList;				///< list of all active NextBots

	int	m_nInFrustumFrame;
};

// singleton accessor
extern C_NextBotManager &TheClientNextBots( void );


#endif // _C_NEXT_BOT_H_
