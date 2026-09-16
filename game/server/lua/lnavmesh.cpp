//========== HL2SB - GMod compat ==========--
//
// Purpose: GMod's navmesh Lua surface - `navmesh.*`, `CNavArea:*` and `Path()`.
//
//   NextBot scripts navigate with two things:
//
//     navmesh.GetNearestNavArea / GetNavArea / Find / area:GetHidingSpots ...
//     Path( "Follow" ) : SetMinLookAheadDistance / SetGoalTolerance / Compute /
//                        IsValid / Update / Draw / GetAge / GetLength
//
//   base_nextbot's whole movement is the second one - MoveToPos() builds a
//   Path( "Follow" ), computes it toward the goal and then Update()s it every
//   frame - which is why SCP-096 cannot chase anything until this file exists.
//
//   Everything here is a thin binding over the nav mesh this fork already
//   compiles (game/server/nav_mesh.vpc, USE_NAV_MESH) and the NextBot path
//   follower from game/server/nextbot.vpc.  ⚠️ GMod uses the BASE nav mesh
//   (subversion 0, TheNavMesh) and so does nav_mesh.vpc; the CS:GO bot mesh
//   (CSNavMesh, subversion 1) is a different class and is not involved.
//
//   Deviations, all deliberate and documented at the point they happen:
//     * Path( ... ) understands "Follow" only.  That is the only type GMod's
//       base_nextbot ever builds (sv_nextbot.lua:214 and :303), and "Chase"
//       needs a leader entity the Lua API never hands over.
//     * navmesh.Find() filters the areas inside the radius by the step up/down
//       the caller asked for, which is what the name means for base_nextbot's
//       FindSpots(); it is not the engine's own (editor-only) area search.
//===========================================================================//

#include "cbase.h"
#include "lnavmesh.h"
#include "luamanager.h"
#include "luasrclib.h"
#include "lbaseentity_shared.h"
#include "mathlib/lvector.h"

#include "nav_mesh.h"
#include "nav_area.h"
#include "nav_pathfind.h"
#include "NextBotInterface.h"
#include "Path/NextBotPathFollow.h"

#include "tier0/memdbgon.h"

#ifdef LUA_SDK

//-----------------------------------------------------------------------------
// The cost functor the path builder needs.
//
// Path::Compute() is a template over a functor that answers "how expensive is
// it to move from fromArea into area" (game/server/NextBot/Path/NextBotPath.h:156
// and :273).  The engine's own bots hand it their own class (CSimpleBot uses
// CSimpleBotPathCost, simple_bot.h:45); this is the generic version, and it asks
// the BOT's locomotion the same questions GMod's pathing does - a bot that
// cannot traverse an area (too steep, wrong type) never gets a path through it.
//-----------------------------------------------------------------------------
class CLuaPathCost : public IPathCost
{
public:
	CLuaPathCost( INextBot *me )
	{
		m_me = me;
	}

	virtual float operator()( CNavArea *area, CNavArea *fromArea, const CNavLadder *ladder, const CFuncElevator *elevator, float length ) const
	{
		if ( fromArea == NULL )
		{
			// first area in the path: no cost
			return 0.0f;
		}

		ILocomotion *pMover = m_me ? m_me->GetLocomotionInterface() : NULL;

		if ( pMover && !pMover->IsAreaTraversable( area ) )
		{
			return -1.0f;				// our locomotor says we cannot move here
		}

		float dist;

		if ( ladder )
		{
			dist = ladder->m_length;
		}
		else if ( length > 0.0f )
		{
			dist = length;
		}
		else
		{
			dist = ( area->GetCenter() - fromArea->GetCenter() ).Length();
		}

		float cost = dist + fromArea->GetCostSoFar();

		if ( pMover )
		{
			const float deltaZ = fromArea->ComputeAdjacentConnectionHeightChange( area );

			if ( deltaZ >= pMover->GetStepHeight() )
			{
				if ( deltaZ >= pMover->GetMaxJumpHeight() )
				{
					return -1.0f;		// too high to reach
				}

				// jumping is slower than walking
				cost += 5.0f * dist;
			}
			else if ( deltaZ < -pMover->GetDeathDropHeight() )
			{
				return -1.0f;			// too far to drop
			}
		}

		return cost;
	}

	INextBot *m_me;
};

//-----------------------------------------------------------------------------
// CNavArea - the "CNavArea" userdata.
//-----------------------------------------------------------------------------
static CNavArea *CheckNavArea( lua_State *L )
{
	CNavArea **ppArea = (CNavArea **)luaL_checkudata( L, 1, "CNavArea" );
	return ppArea ? *ppArea : NULL;
}

// The same, for an area handed in as an argument (nil is a legal answer).
static CNavArea *CheckNavAreaAt( lua_State *L, int nArg )
{
	if ( lua_isnoneornil( L, nArg ) )
		return NULL;

	CNavArea **ppArea = (CNavArea **)luaL_testudata( L, nArg, "CNavArea" );
	return ppArea ? *ppArea : NULL;
}

static void PushNavArea( lua_State *L, CNavArea *pArea )
{
	if ( pArea == NULL )
	{
		lua_pushnil( L );
		return;
	}

	CNavArea **ppArea = (CNavArea **)lua_newuserdata( L, sizeof( CNavArea * ) );
	*ppArea = pArea;

	luaL_getmetatable( L, "CNavArea" );
	lua_setmetatable( L, -2 );
}

static int CNavArea_GetID( lua_State *L )
{
	CNavArea *pArea = CheckNavArea( L );
	lua_pushinteger( L, pArea ? (int)pArea->GetID() : 0 );
	return 1;
}

static int CNavArea_GetCenter( lua_State *L )
{
	CNavArea *pArea = CheckNavArea( L );
	lua_pushvector( L, pArea ? pArea->GetCenter() : vec3_origin );
	return 1;
}

static int CNavArea_GetSizeX( lua_State *L )
{
	CNavArea *pArea = CheckNavArea( L );
	lua_pushnumber( L, pArea ? pArea->GetSizeX() : 0.0f );
	return 1;
}

static int CNavArea_GetSizeY( lua_State *L )
{
	CNavArea *pArea = CheckNavArea( L );
	lua_pushnumber( L, pArea ? pArea->GetSizeY() : 0.0f );
	return 1;
}

static int CNavArea_GetZ( lua_State *L )
{
	CNavArea *pArea = CheckNavArea( L );

	if ( pArea == NULL )
	{
		lua_pushnumber( L, 0.0f );
		return 1;
	}

	if ( lua_gettop( L ) >= 2 )
	{
		lua_pushnumber( L, pArea->GetZ( luaL_checkvector( L, 2 ) ) );
		return 1;
	}

	lua_pushnumber( L, pArea->GetZ( pArea->GetCenter() ) );
	return 1;
}

static int CNavArea_GetCorner( lua_State *L )
{
	CNavArea *pArea = CheckNavArea( L );
	const int nCorner = (int)luaL_optnumber( L, 2, 0 );

	if ( pArea == NULL || nCorner < 0 || nCorner >= NUM_CORNERS )
	{
		lua_pushvector( L, vec3_origin );
		return 1;
	}

	lua_pushvector( L, pArea->GetCorner( (NavCornerType)nCorner ) );
	return 1;
}

static int CNavArea_Contains( lua_State *L )
{
	CNavArea *pArea = CheckNavArea( L );
	lua_pushboolean( L, pArea ? pArea->Contains( luaL_checkvector( L, 2 ) ) : false );
	return 1;
}

static int CNavArea_GetClosestPoint( lua_State *L )
{
	CNavArea *pArea = CheckNavArea( L );

	if ( pArea == NULL )
	{
		lua_pushvector( L, luaL_checkvector( L, 2 ) );
		return 1;
	}

	Vector close;
	pArea->GetClosestPointOnArea( luaL_checkvector( L, 2 ), &close );
	lua_pushvector( L, close );
	return 1;
}

static int CNavArea_GetRandomPoint( lua_State *L )
{
	CNavArea *pArea = CheckNavArea( L );
	lua_pushvector( L, pArea ? pArea->GetRandomPoint() : vec3_origin );
	return 1;
}

static int CNavArea_IsFlat( lua_State *L )
{
	CNavArea *pArea = CheckNavArea( L );
	lua_pushboolean( L, pArea ? pArea->IsFlat() : false );
	return 1;
}

static int CNavArea_IsUnderwater( lua_State *L )
{
	CNavArea *pArea = CheckNavArea( L );
	lua_pushboolean( L, pArea ? pArea->IsUnderwater() : false );
	return 1;
}

static int CNavArea_IsCrouch( lua_State *L )
{
	CNavArea *pArea = CheckNavArea( L );
	lua_pushboolean( L, pArea ? pArea->HasAttributes( NAV_MESH_CROUCH ) : false );
	return 1;
}

static int CNavArea_IsBlocked( lua_State *L )
{
	CNavArea *pArea = CheckNavArea( L );
	lua_pushboolean( L, pArea ? pArea->IsBlocked( TEAM_ANY ) : false );
	return 1;
}

static int CNavArea_GetAttributes( lua_State *L )
{
	CNavArea *pArea = CheckNavArea( L );
	lua_pushinteger( L, pArea ? pArea->GetAttributes() : 0 );
	return 1;
}

// ⚠️ GMod's three attribute methods map onto Source's plural ones:
// CNavArea::HasAttributes( bits ) / SetAttributes( bits ) / RemoveAttributes( bits )
// (nav_area.h:296-299).  There is no single-bit setter in the engine, so
// SetAttribute() reads-modifies-writes and ClearAttribute() removes the bit.
static int CNavArea_HasAttribute( lua_State *L )
{
	CNavArea *pArea = CheckNavArea( L );
	lua_pushboolean( L, pArea ? pArea->HasAttributes( (int)luaL_checknumber( L, 2 ) ) : false );
	return 1;
}

static int CNavArea_SetAttribute( lua_State *L )
{
	CNavArea *pArea = CheckNavArea( L );

	if ( pArea )
		pArea->SetAttributes( pArea->GetAttributes() | (int)luaL_checknumber( L, 2 ) );

	return 0;
}

static int CNavArea_ClearAttribute( lua_State *L )
{
	CNavArea *pArea = CheckNavArea( L );

	if ( pArea )
		pArea->RemoveAttributes( (int)luaL_checknumber( L, 2 ) );

	return 0;
}

//-----------------------------------------------------------------------------
// The hiding spots, which is the one area query base_nextbot itself uses
// (FindSpots -> area:GetHidingSpots(), sv_nextbot.lua:227).
//-----------------------------------------------------------------------------
struct CLuaHidingSpotCollector
{
	CUtlVector< Vector > m_spots;

	bool operator()( CNavArea *area )
	{
		if ( area == NULL )
			return true;

		const HidingSpotVector *pSpots = area->GetHidingSpots();

		if ( pSpots )
		{
			FOR_EACH_VEC( *pSpots, i )
			{
				const HidingSpot *pSpot = ( *pSpots )[ i ];

				if ( pSpot )
					m_spots.AddToTail( pSpot->GetPosition() );
			}
		}

		return true;
	}
};

static int CNavArea_GetHidingSpots( lua_State *L )
{
	CNavArea *pArea = CheckNavArea( L );

	lua_newtable( L );

	if ( pArea == NULL )
		return 1;

	const HidingSpotVector *pSpots = pArea->GetHidingSpots();

	if ( pSpots == NULL )
		return 1;

	int n = 0;
	FOR_EACH_VEC( *pSpots, i )
	{
		const HidingSpot *pSpot = ( *pSpots )[ i ];

		if ( pSpot )
		{
			lua_pushvector( L, pSpot->GetPosition() );
			lua_rawseti( L, -2, ++n );
		}
	}

	return 1;
}

//-----------------------------------------------------------------------------
// The neighbours.  GMod hands back a flat table of areas
// (CNavArea:GetAdjacentAreas()), so the four directions are walked here.
//-----------------------------------------------------------------------------
struct CLuaAdjacentCollector
{
	CUtlVector< CNavArea * > m_areas;

	bool Add( const NavConnectVector *pConnections )
	{
		if ( pConnections == NULL )
			return false;

		FOR_EACH_VEC( *pConnections, i )
		{
			CNavArea *pArea = ( *pConnections )[ i ].area;

			if ( pArea && !m_areas.HasElement( pArea ) )
				m_areas.AddToTail( pArea );
		}

		return true;
	}
};

static int CNavArea_GetAdjacentAreas( lua_State *L )
{
	CNavArea *pArea = CheckNavArea( L );

	if ( pArea == NULL )
	{
		lua_newtable( L );
		return 1;
	}

	CLuaAdjacentCollector collector;

	for ( int dir = 0; dir < NUM_DIRECTIONS; ++dir )
		collector.Add( pArea->GetAdjacentAreas( (NavDirType)dir ) );

	lua_newtable( L );

	for ( int i = 0; i < collector.m_areas.Count(); ++i )
	{
		PushNavArea( L, collector.m_areas[i] );
		lua_rawseti( L, -2, i + 1 );
	}

	return 1;
}

static int CNavArea_GetAdjacentAreaCount( lua_State *L )
{
	CNavArea *pArea = CheckNavArea( L );

	if ( pArea == NULL )
	{
		lua_pushinteger( L, 0 );
		return 1;
	}

	int nCount = 0;

	for ( int dir = 0; dir < NUM_DIRECTIONS; ++dir )
	{
		const NavConnectVector *pConnections = pArea->GetAdjacentAreas( (NavDirType)dir );

		if ( pConnections )
			nCount += pConnections->Count();
	}

	lua_pushinteger( L, nCount );
	return 1;
}

static int CNavArea_GetAdjacentArea( lua_State *L )
{
	CNavArea *pArea = CheckNavArea( L );

	if ( pArea == NULL )
	{
		lua_pushnil( L );
		return 1;
	}

	const int dir = (int)luaL_checknumber( L, 2 );
	const int index = (int)luaL_optnumber( L, 3, 0 );

	if ( dir < 0 || dir >= NUM_DIRECTIONS || index < 0 )
	{
		lua_pushnil( L );
		return 1;
	}

	PushNavArea( L, pArea->GetAdjacentArea( (NavDirType)dir, index ) );
	return 1;
}

static int CNavArea_ComputeAdjacentConnectionHeightChange( lua_State *L )
{
	CNavArea *pArea = CheckNavArea( L );
	CNavArea *pOther = CheckNavAreaAt( L, 2 );

	lua_pushnumber( L, ( pArea && pOther ) ? pArea->ComputeAdjacentConnectionHeightChange( pOther ) : 0.0f );
	return 1;
}

//-----------------------------------------------------------------------------
// navmesh.GetNavAreasInRadius / GetAllNavAreas / Find all collect areas, so they
// share one collector and one "push a table of areas" tail.
//-----------------------------------------------------------------------------
struct CLuaNavAreaCollector
{
	CLuaNavAreaCollector( float flStepUp, float flStepDown, CNavArea *pFrom )
	{
		m_flStepUp = flStepUp;
		m_flStepDown = flStepDown;
		m_pFrom = pFrom;
	}

	bool operator()( CNavArea *area )
	{
		if ( area == NULL )
			return true;

		// navmesh.Find( pos, radius, stepdown, stepup ): keep only what the
		// caller can actually step into / drop onto from the area we start on.
		if ( m_pFrom && ( m_flStepUp > 0.0f || m_flStepDown > 0.0f ) )
		{
			const float flDelta = m_pFrom->ComputeAdjacentConnectionHeightChange( area );

			if ( flDelta > 0.0f && m_flStepUp > 0.0f && flDelta > m_flStepUp )
				return true;

			if ( flDelta < 0.0f && m_flStepDown > 0.0f && -flDelta > m_flStepDown )
				return true;
		}

		m_areas.AddToTail( area );
		return true;
	}

	CUtlVector< CNavArea * > m_areas;
	float m_flStepUp;
	float m_flStepDown;
	CNavArea *m_pFrom;
};

static void PushNavAreaTable( lua_State *L, const CUtlVector< CNavArea * > &areas )
{
	lua_newtable( L );

	for ( int i = 0; i < areas.Count(); ++i )
	{
		PushNavArea( L, areas[i] );
		lua_rawseti( L, -2, i + 1 );
	}
}

#define LUA_NAVMESH_CHECK_LOADED() \
	if ( TheNavMesh == NULL || !TheNavMesh->IsLoaded() ) { lua_pushnil( L ); return 1; }

static int navmesh_GetNavArea( lua_State *L )
{
	LUA_NAVMESH_CHECK_LOADED();

	const Vector &pos = luaL_checkvector( L, 1 );
	const float flBeneath = (float)luaL_optnumber( L, 2, 120.0f );

	PushNavArea( L, TheNavMesh->GetNavArea( pos, flBeneath ) );
	return 1;
}

static int navmesh_GetNearestNavArea( lua_State *L )
{
	LUA_NAVMESH_CHECK_LOADED();

	const Vector &pos = luaL_checkvector( L, 1 );
	const float flMaxDist = (float)luaL_optnumber( L, 2, 10000.0f );
	const bool bCheckLOS = lua_toboolean( L, 3 ) != 0;
	const bool bCheckGround = lua_gettop( L ) >= 4 ? ( lua_toboolean( L, 4 ) != 0 ) : true;
	const int nTeam = (int)luaL_optnumber( L, 5, TEAM_ANY );

	PushNavArea( L, TheNavMesh->GetNearestNavArea( pos, false, flMaxDist, bCheckLOS, bCheckGround, nTeam ) );
	return 1;
}

static int navmesh_GetNavAreaByID( lua_State *L )
{
	LUA_NAVMESH_CHECK_LOADED();

	PushNavArea( L, TheNavMesh->GetNavAreaByID( (unsigned int)luaL_checknumber( L, 1 ) ) );
	return 1;
}

static int navmesh_GetNavAreaCount( lua_State *L )
{
	lua_pushinteger( L, ( TheNavMesh && TheNavMesh->IsLoaded() ) ? (int)TheNavMesh->GetNavAreaCount() : 0 );
	return 1;
}

static int navmesh_GetNavAreasInRadius( lua_State *L )
{
	LUA_NAVMESH_CHECK_LOADED();

	const Vector &pos = luaL_checkvector( L, 1 );
	const float flRadius = (float)luaL_checknumber( L, 2 );

	CLuaNavAreaCollector collector( 0.0f, 0.0f, NULL );
	TheNavMesh->ForAllAreasInRadius( collector, pos, flRadius );

	PushNavAreaTable( L, collector.m_areas );
	return 1;
}

static int navmesh_GetAllNavAreas( lua_State *L )
{
	LUA_NAVMESH_CHECK_LOADED();

	CLuaNavAreaCollector collector( 0.0f, 0.0f, NULL );
	TheNavMesh->ForAllAreas( collector );

	PushNavAreaTable( L, collector.m_areas );
	return 1;
}

//-----------------------------------------------------------------------------
// navmesh.Find( pos, radius, stepdown, stepup ) - base_nextbot's FindSpots uses
// it to gather candidate areas around a position (sv_nextbot.lua:217).
//-----------------------------------------------------------------------------
static int navmesh_Find( lua_State *L )
{
	LUA_NAVMESH_CHECK_LOADED();

	const Vector &pos = luaL_checkvector( L, 1 );
	const float flRadius = (float)luaL_optnumber( L, 2, 1000.0f );
	const float flStepDown = (float)luaL_optnumber( L, 3, 20.0f );
	const float flStepUp = (float)luaL_optnumber( L, 4, 20.0f );

	CNavArea *pFrom = TheNavMesh->GetNearestNavArea( pos, false, flRadius );

	CLuaNavAreaCollector collector( flStepUp, flStepDown, pFrom );
	TheNavMesh->ForAllAreasInRadius( collector, pos, flRadius );

	PushNavAreaTable( L, collector.m_areas );
	return 1;
}

static int navmesh_IsLoaded( lua_State *L )
{
	lua_pushboolean( L, TheNavMesh != NULL && TheNavMesh->IsLoaded() );
	return 1;
}

static int navmesh_IsGenerating( lua_State *L )
{
	lua_pushboolean( L, TheNavMesh != NULL && TheNavMesh->IsGenerating() );
	return 1;
}

// HL2SB GMod compat: navmesh.BeginGeneration() and navmesh.SetPlayerSpawnName().
//
// Wiki: BeginGeneration is server only and "starts the generation of a new
// navmesh ... highly resource intensive"; nextbot addons call it when a map has
// no nav mesh at all (their whole behaviour is dead without one), naming the
// entity class that counts as the player spawn first.  Both are thin wrappers
// over CNavMesh, whose generation pass already drives this fork's progress window.
static int navmesh_BeginGeneration( lua_State *L )
{
	if ( TheNavMesh != NULL )
		TheNavMesh->BeginGeneration( false );

	return 0;
}

static int navmesh_SetPlayerSpawnName( lua_State *L )
{
	const char *pszName = luaL_checkstring( L, 1 );

	if ( TheNavMesh != NULL )
		TheNavMesh->SetPlayerSpawnName( pszName );

	return 0;
}

// HL2SB GMod compat: navmesh.AddWalkableSeed( pos, dir ) / ClearWalkableSeeds().
//
// Wiki: "Add this position and normal to the list of walkable positions, used
// before map generation with navmesh.BeginGeneration"; ClearWalkableSeeds "clears
// all the walkable positions, used before calling navmesh.BeginGeneration".
// npc_verity seeds the generation from every player spawn it can find (its
// npc_verity_learn command) and clears them again when the start fails.
//
// Both are the same CNavMesh calls the nav_mark_walkable / nav_clear_walkable_marks
// console commands make (nav_mesh.cpp:2696/2709).
static int navmesh_AddWalkableSeed( lua_State *L )
{
	if ( TheNavMesh != NULL )
	{
		Vector pos = luaL_checkvector( L, 1 );
		Vector normal = luaL_checkvector( L, 2 );

		TheNavMesh->AddWalkableSeed( pos, normal );
	}

	return 0;
}

static int navmesh_ClearWalkableSeeds( lua_State *L )
{
	if ( TheNavMesh != NULL )
		TheNavMesh->ClearWalkableSeeds();

	return 0;
}

static const luaL_Reg s_LuaNavMeshFunctions[] =
{
	{ "GetNavArea",					navmesh_GetNavArea },
	{ "GetNearestNavArea",			navmesh_GetNearestNavArea },
	{ "GetNavAreaByID",				navmesh_GetNavAreaByID },
	{ "GetNavAreaCount",			navmesh_GetNavAreaCount },
	{ "GetNavAreasInRadius",		navmesh_GetNavAreasInRadius },
	{ "GetAllNavAreas",				navmesh_GetAllNavAreas },
	{ "Find",						navmesh_Find },
	{ "IsLoaded",					navmesh_IsLoaded },
	{ "IsGenerating",				navmesh_IsGenerating },
	// HL2SB GMod compat (see the definitions above).
	{ "BeginGeneration",			navmesh_BeginGeneration },
	{ "SetPlayerSpawnName",			navmesh_SetPlayerSpawnName },
	{ "AddWalkableSeed",			navmesh_AddWalkableSeed },
	{ "ClearWalkableSeeds",			navmesh_ClearWalkableSeeds },
	{ NULL,							NULL }
};

//=============================================================================
// Path( "Follow" ) - the path follower, GMod's PathFollower.
//=============================================================================
static PathFollower *CheckPathFollower( lua_State *L )
{
	PathFollower **ppPath = (PathFollower **)luaL_checkudata( L, 1, "PathFollower" );
	return ppPath ? *ppPath : NULL;
}

static INextBot *CheckBotArg( lua_State *L, int nArg )
{
	CBaseEntity *pEntity = luaL_checkentity( L, nArg );

	return pEntity ? pEntity->MyNextBotPointer() : NULL;
}

static int PathFollower_SetMinLookAheadDistance( lua_State *L )
{
	PathFollower *pPath = CheckPathFollower( L );

	if ( pPath )
		pPath->SetMinLookAheadDistance( (float)luaL_checknumber( L, 2 ) );

	return 0;
}

static int PathFollower_SetGoalTolerance( lua_State *L )
{
	PathFollower *pPath = CheckPathFollower( L );

	if ( pPath )
		pPath->SetGoalTolerance( (float)luaL_checknumber( L, 2 ) );

	return 0;
}

static int PathFollower_Compute( lua_State *L )
{
	PathFollower *pPath = CheckPathFollower( L );
	INextBot *pBot = CheckBotArg( L, 2 );

	if ( pPath == NULL || pBot == NULL )
		return 0;

	// GMod: path:Compute( ent, goalVector ).  The second argument is a Vector,
	// and an ENTITY is accepted too (aim at its origin) - CBaseEntity is tested
	// first because a Vector's Lua representation is not guaranteed to be a
	// table, so lua_type() is not the right question here.
	Vector vecGoal;
	CBaseEntity *pSubject = lua_toentity( L, 3 );

	if ( pSubject != NULL )
	{
		vecGoal = pSubject->GetAbsOrigin();
	}
	else if ( lua_isnoneornil( L, 3 ) )
	{
		return 0;
	}
	else
	{
		vecGoal = luaL_checkvector( L, 3 );
	}

	CLuaPathCost cost( pBot );
	pPath->Compute( pBot, vecGoal, cost );

	// HL2SB diagnostic: whether the script's path actually came out valid, and how
	// long it is.  "The bot does not move" with no error is a path that fails to
	// compute -- ChaseEnemy()/MoveToPos() then return "failed" silently.  Throttled
	// to one line per second per path.
	{
		static float s_flNextPathReport = 0.0f;
		float flNow = (float)gpGlobals->curtime;

		if ( flNow >= s_flNextPathReport )
		{
			s_flNextPathReport = flNow + 1.0f;

			Msg( "[HL2SB] Path: Compute from '%s' origin=(%.0f %.0f %.0f) goal=(%.0f %.0f %.0f) -> valid=%d length=%.0f\n",
				( pBot->GetEntity() != NULL ) ? pBot->GetEntity()->GetClassname() : "?",
				pBot->GetPosition().x, pBot->GetPosition().y, pBot->GetPosition().z,
				vecGoal.x, vecGoal.y, vecGoal.z,
				(int)pPath->IsValid(), pPath->GetLength() );
		}
	}

	return 0;
}

static int PathFollower_IsValid( lua_State *L )
{
	PathFollower *pPath = CheckPathFollower( L );
	lua_pushboolean( L, pPath ? pPath->IsValid() : false );
	return 1;
}

static int PathFollower_Invalidate( lua_State *L )
{
	PathFollower *pPath = CheckPathFollower( L );

	if ( pPath )
		pPath->Invalidate();

	return 0;
}

static int PathFollower_Update( lua_State *L )
{
	PathFollower *pPath = CheckPathFollower( L );
	INextBot *pBot = CheckBotArg( L, 2 );

	if ( pPath && pBot )
	{
		pPath->Update( pBot );

		// HL2SB diagnostic: the speed the locomotion is holding while the script is
		// following a path.  PathFollower::AdjustSpeed() writes it every update, so
		// a 0 here means the script never asked for a speed (or asked for 0) and the
		// bot will stand still with a perfectly valid path.
		static float s_flNextUpdateReport = 0.0f;
		float flNow = (float)gpGlobals->curtime;

		if ( flNow >= s_flNextUpdateReport )
		{
			s_flNextUpdateReport = flNow + 2.0f;

			ILocomotion *pMover = pBot->GetLocomotionInterface();

			Msg( "[HL2SB] Path: Update '%s' valid=%d desiredSpeed=%.0f actualSpeed=%.0f stuck=%d mover=%p\n",
				( pBot->GetEntity() != NULL ) ? pBot->GetEntity()->GetClassname() : "?",
				(int)pPath->IsValid(),
				pMover ? pMover->GetDesiredSpeed() : 0.0f,
				pMover ? pMover->GetVelocity().Length2D() : 0.0f,
				( pMover != NULL && pMover->IsStuck() ) ? 1 : 0,
				(void*)pMover );
		}
	}

	return 0;
}

static int PathFollower_Draw( lua_State *L )
{
	PathFollower *pPath = CheckPathFollower( L );

	if ( pPath )
		pPath->Draw();

	return 0;
}

static int PathFollower_GetAge( lua_State *L )
{
	PathFollower *pPath = CheckPathFollower( L );
	lua_pushnumber( L, pPath ? pPath->GetAge() : 0.0f );
	return 1;
}

static int PathFollower_GetLength( lua_State *L )
{
	PathFollower *pPath = CheckPathFollower( L );
	lua_pushnumber( L, pPath ? pPath->GetLength() : 0.0f );
	return 1;
}

static int PathFollower_GetStartPosition( lua_State *L )
{
	PathFollower *pPath = CheckPathFollower( L );
	lua_pushvector( L, pPath ? pPath->GetStartPosition() : vec3_origin );
	return 1;
}

static int PathFollower_GetEndPosition( lua_State *L )
{
	PathFollower *pPath = CheckPathFollower( L );
	lua_pushvector( L, pPath ? pPath->GetEndPosition() : vec3_origin );
	return 1;
}

static int PathFollower_GetCursorPosition( lua_State *L )
{
	PathFollower *pPath = CheckPathFollower( L );
	lua_pushnumber( L, pPath ? pPath->GetCursorPosition() : 0.0f );
	return 1;
}

// HL2SB GMod compat: PathFollower:GetPositionOnPath( distance ).
//
// Wiki: "Returns the vector position of distance along path".  GMod's class is
// named PathFollower and a nextbot uses it to recover from being stuck:
// npc_verity's ENT:OnStuck() walks the path cursor forward and teleports the bot to
// Path:GetPositionOnPath( newCursor ).
//
// Path::GetPosition() is the engine side of it, and it already answers
// vec3_origin for an invalid path and clamps a distance below the path start, so no
// extra guarding is needed here (NextBotPath.cpp).
static int PathFollower_GetPositionOnPath( lua_State *L )
{
	PathFollower *pPath = CheckPathFollower( L );
	float flDistance = (float)luaL_checknumber( L, 2 );

	lua_pushvector( L, pPath ? pPath->GetPosition( flDistance ) : vec3_origin );
	return 1;
}

static int PathFollower_MoveCursorToStart( lua_State *L )
{
	PathFollower *pPath = CheckPathFollower( L );

	if ( pPath )
		pPath->MoveCursorToStart();

	return 0;
}

static int PathFollower_MoveCursorToEnd( lua_State *L )
{
	PathFollower *pPath = CheckPathFollower( L );

	if ( pPath )
		pPath->MoveCursorToEnd();

	return 0;
}

static int PathFollower_MoveCursorToClosestPosition( lua_State *L )
{
	PathFollower *pPath = CheckPathFollower( L );

	if ( pPath )
		pPath->MoveCursorToClosestPosition( luaL_checkvector( L, 2 ) );

	return 0;
}

static int PathFollower_GetCurrentGoal( lua_State *L )
{
	PathFollower *pPath = CheckPathFollower( L );
	const Path::Segment *pSegment = pPath ? pPath->GetCurrentGoal() : NULL;

	if ( pSegment )
		lua_pushvector( L, pSegment->pos );
	else
		lua_pushvector( L, vec3_origin );

	return 1;
}

static int PathFollower_GetHindrance( lua_State *L )
{
	PathFollower *pPath = CheckPathFollower( L );
	lua_pushentity( L, pPath ? pPath->GetHindrance() : NULL );
	return 1;
}

// IsAtGoal() / CheckProgress() / AdjustSpeed() are PRIVATE on Source's
// PathFollower (NextBotPathFollow.h): they are the movement internals that
// Update() drives, not part of the interface, so they are not bound.

// the path owns its C++ object: free it when Lua collects the userdata
static int PathFollower_gc( lua_State *L )
{
	PathFollower **ppPath = (PathFollower **)luaL_checkudata( L, 1, "PathFollower" );

	if ( ppPath && *ppPath )
	{
		delete *ppPath;
		*ppPath = NULL;
	}

	return 0;
}

static const luaL_Reg s_LuaPathFollowerMethods[] =
{
	{ "SetMinLookAheadDistance",		PathFollower_SetMinLookAheadDistance },
	{ "SetGoalTolerance",				PathFollower_SetGoalTolerance },
	{ "Compute",						PathFollower_Compute },
	{ "IsValid",						PathFollower_IsValid },
	{ "Invalidate",						PathFollower_Invalidate },
	{ "Update",							PathFollower_Update },
	{ "Draw",							PathFollower_Draw },
	{ "GetAge",							PathFollower_GetAge },
	{ "GetLength",						PathFollower_GetLength },
	{ "GetStartPosition",				PathFollower_GetStartPosition },
	{ "GetEndPosition",					PathFollower_GetEndPosition },
	{ "GetCursorPosition",				PathFollower_GetCursorPosition },
	{ "GetPositionOnPath",				PathFollower_GetPositionOnPath },
	{ "MoveCursorToStart",				PathFollower_MoveCursorToStart },
	{ "MoveCursorToEnd",				PathFollower_MoveCursorToEnd },
	{ "MoveCursorToClosestPosition",	PathFollower_MoveCursorToClosestPosition },
	{ "GetCurrentGoal",					PathFollower_GetCurrentGoal },
	{ "GetHindrance",					PathFollower_GetHindrance },
	{ NULL,								NULL }
};

//-----------------------------------------------------------------------------
// Path( name ) - GMod's factory for the two path types.
//
// ⚠️ Only "Follow" is implemented.  base_nextbot builds nothing else
// (sv_nextbot.lua:214 MoveToPos and :303 FindSpots both say Path( "Follow" )),
// and "Chase" (Source's ChasePath) is driven by a LEADER entity its
// Update( bot, subject, cost ) requires - a piece of state the GMod Lua API
// never hands the path object.
//-----------------------------------------------------------------------------
static int lua_Path( lua_State *L )
{
	const char *pszType = luaL_checkstring( L, 1 );

	if ( Q_stricmp( pszType, "Follow" ) != 0 )
	{
		Warning( "[HL2SB] Path( \"%s\" ) is not supported: only \"Follow\" is (base_nextbot builds nothing else)\n", pszType );
		lua_pushnil( L );
		return 1;
	}

	PathFollower **ppPath = (PathFollower **)lua_newuserdata( L, sizeof( PathFollower * ) );
	*ppPath = new PathFollower();

	luaL_getmetatable( L, "PathFollower" );
	lua_setmetatable( L, -2 );
	return 1;
}

//-----------------------------------------------------------------------------
// Install: the navmesh library, the two metatables and the Path factory.
//
// Called when the first Lua nextbot is registered (a script can only want these
// from a nextbot), and only once.
//-----------------------------------------------------------------------------
void LuaNavMesh_Install( void )
{
	if ( L == NULL )
		return;

	// HL2SB: the guard has to be keyed on the STATE, not on the process.
	//
	// There is more than one lua_State -- the server and the client each own one,
	// and they are not created in a fixed order -- so a `static bool` let whoever
	// installed first win, and left every other state (and any state created
	// afterwards) without `Path()`, without the CNavArea/PathFollower metatables.
	// A nextbot's BehaveStart() then died on
	//
	//     attempt to call a nil value (global 'Path')
	//
	// leaving self.MovePath nil, which every later Think() hit again (108 times in
	// one session).  Installing twice into the same state is harmless -- the
	// globals and metatables are simply overwritten -- so the cheap, correct test
	// is whether this state already answers Path.
	lua_getglobal( L, "Path" );

	if ( lua_isfunction( L, -1 ) )
	{
		lua_pop( L, 1 );
		return;					// this state already has the surface
	}

	lua_pop( L, 1 );

	// navmesh.*
	luaL_register( L, "navmesh", s_LuaNavMeshFunctions );

	// CNavArea
	luaL_newmetatable( L, "CNavArea" );
	lua_pushvalue( L, -1 );
	lua_setfield( L, -2, "__index" );

	lua_pushcfunction( L, CNavArea_GetID );					lua_setfield( L, -2, "GetID" );
	lua_pushcfunction( L, CNavArea_GetCenter );				lua_setfield( L, -2, "GetCenter" );
	lua_pushcfunction( L, CNavArea_GetSizeX );				lua_setfield( L, -2, "GetSizeX" );
	lua_pushcfunction( L, CNavArea_GetSizeY );				lua_setfield( L, -2, "GetSizeY" );
	lua_pushcfunction( L, CNavArea_GetZ );					lua_setfield( L, -2, "GetZ" );
	lua_pushcfunction( L, CNavArea_GetCorner );				lua_setfield( L, -2, "GetCorner" );
	lua_pushcfunction( L, CNavArea_Contains );				lua_setfield( L, -2, "Contains" );
	lua_pushcfunction( L, CNavArea_GetClosestPoint );		lua_setfield( L, -2, "GetClosestPoint" );
	lua_pushcfunction( L, CNavArea_GetRandomPoint );		lua_setfield( L, -2, "GetRandomPoint" );
	lua_pushcfunction( L, CNavArea_IsFlat );				lua_setfield( L, -2, "IsFlat" );
	lua_pushcfunction( L, CNavArea_IsUnderwater );			lua_setfield( L, -2, "IsUnderwater" );
	lua_pushcfunction( L, CNavArea_IsCrouch );				lua_setfield( L, -2, "IsCrouch" );
	lua_pushcfunction( L, CNavArea_IsBlocked );				lua_setfield( L, -2, "IsBlocked" );
	lua_pushcfunction( L, CNavArea_GetAttributes );			lua_setfield( L, -2, "GetAttributes" );
	lua_pushcfunction( L, CNavArea_HasAttribute );			lua_setfield( L, -2, "HasAttribute" );
	lua_pushcfunction( L, CNavArea_SetAttribute );			lua_setfield( L, -2, "SetAttribute" );
	lua_pushcfunction( L, CNavArea_ClearAttribute );		lua_setfield( L, -2, "ClearAttribute" );
	lua_pushcfunction( L, CNavArea_GetHidingSpots );		lua_setfield( L, -2, "GetHidingSpots" );
	lua_pushcfunction( L, CNavArea_GetAdjacentAreas );		lua_setfield( L, -2, "GetAdjacentAreas" );
	lua_pushcfunction( L, CNavArea_GetAdjacentAreaCount );	lua_setfield( L, -2, "GetAdjacentAreaCount" );
	lua_pushcfunction( L, CNavArea_GetAdjacentArea );		lua_setfield( L, -2, "GetAdjacentArea" );
	lua_pushcfunction( L, CNavArea_ComputeAdjacentConnectionHeightChange );
	lua_setfield( L, -2, "ComputeAdjacentConnectionHeightChange" );

	lua_pop( L, 1 );

	// PathFollower
	luaL_newmetatable( L, "PathFollower" );
	lua_pushvalue( L, -1 );
	lua_setfield( L, -2, "__index" );

	// the path owns its C++ object: free it when Lua collects the userdata
	lua_pushcfunction( L, PathFollower_gc );
	lua_setfield( L, -2, "__gc" );

	luaL_register( L, NULL, s_LuaPathFollowerMethods );
	lua_pop( L, 1 );

	// Path( name )
	lua_pushcfunction( L, lua_Path );
	lua_setglobal( L, "Path" );

	Msg( "[HL2SB] navmesh + Path() Lua surface installed\n" );
}

#endif // LUA_SDK
