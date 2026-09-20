//========= Copyright HL2SB, all rights reserved. ============//
//
// Purpose: GMod's standalone Lua particle system -- ParticleEmitter() /
//          CSEmitter / CLuaParticle -- on top of the stock CParticleEffect
//          machinery (particlemgr + particle_util), the same layer every
//          engine particle effect uses.
//
//          Needed by the Nuke Pack addon (2026-09-20 audit):
//
//              local emitter = ParticleEmitter( Pos )
//              local particle = emitter:Add( "particles/smokey", Pos )
//              particle:SetDieTime( 4 ); particle:SetStartAlpha( 255 ) ...
//              emitter:Finish()
//
//          Realms: client only (GMod realm; this file is only in the client
//          .vpc and the luaopen entry is under #ifdef CLIENT_DLL).
//
// $NoKeywords: $
//===========================================================================//

#include "cbase.h"
#include "particlemgr.h"
#include "particles_simple.h"
#include "particle_util.h"
#include "particledraw.h"
#include "luamanager.h"
#include "luasrclib.h"
#include "luabinding.h"
#include "mathlib/lvector.h"
#include "tier1/utlvector.h"
#include "cdll_client_int.h"   // extern IEngineTrace *enginetrace

// memdbgon must be the last include file in a .cpp file!!!
#include "tier0/memdbgon.h"

// ---------------------------------------------------------------------------
// The particle memory lives in the particle manager's pool and is recycled the
// moment the sim iterator removes it.  So a Lua particle wrapper carries two
// things: the pointer AND a per-particle serial number.  When the simulator
// removes a particle it records the serial as dead; a wrapper whose serial is
// dead goes inert (setters no-op, getters return last values), which is what
// keeps a script that holds a particle across frames from scribbling into a
// recycled pool slot.
// ---------------------------------------------------------------------------
struct CLuaParticleX : public SimpleParticle
{
	int m_iSerial;
	bool m_bRemoveRequested;   // set by CLuaParticle:Remove()
	Vector m_vecGravity;
	float m_flAirResistance;
	bool m_bCollide;
	float m_flBounce;
	bool m_bVelocityDecay;
	float m_flStartLength;
	float m_flEndLength;
	int m_nThinkRef;         // LUA_REFNIL when none
	float m_flNextThink;
};

class CLuaEmitter;
static void CLuaParticle_PushWrapper( CLuaEmitter *pEmitter, CLuaParticleX *pParticle );

static const float PARTICLE_DEFAULT_DIETIME = 0.4f;

static int s_iLuaParticleSerial = 0;

class CLuaEmitter : public CParticleEffect
{
public:
	DECLARE_CLASS_NOBASE( CLuaEmitter );

	static CSmartPtr<CLuaEmitter> Create( const Vector &origin, bool b3D )
	{
		CLuaEmitter *pRet = new CLuaEmitter( "CSEmitter" );
		pRet->SetDynamicallyAllocated( true );
		pRet->SetSortOrigin( origin );
		pRet->m_b3D = b3D;
		return pRet;
	}

	CLuaEmitter( const char *pDebugName ) : CParticleEffect( pDebugName )
	{
		// An empty emitter must stay alive until the script calls Finish();
		// after that the stock binding reaps it once the last particle dies.
		SetDontRemove( true );
		m_bFinished = false;
	}

	// GMod: "Removes the emitter, making it no longer usable from Lua. If
	// particles remain, the emitter will be removed when all particles die."
	void Finish()
	{
		m_bFinished = true;
		SetDontRemove( false );
	}

	bool IsSerialAlive( int iSerial ) const
	{
		for ( int i = 0; i < m_aDeadSerials.Count(); ++i )
		{
			if ( m_aDeadSerials[i] == iSerial )
				return false;
		}
		return true;
	}

	bool m_bFinished;
	bool m_b3D;
	CUtlVector< int > m_aDeadSerials;

	virtual void Update( float flTimeDelta )
	{
		// While the script still owns the emitter (pre-Finish) it must not be
		// reaped even if it holds no particles at the moment.
		if ( !m_bFinished )
			return;
		CParticleEffect::Update( flTimeDelta );
	}

	virtual void SimulateParticles( CParticleSimulateIterator *pIterator )
	{
		CLuaParticleX *pParticle = (CLuaParticleX *)pIterator->GetFirst();
		float flDt = pIterator->GetTimeDelta();
		while ( pParticle )
		{
			pParticle->m_flLifetime += flDt;
			if ( pParticle->m_bRemoveRequested ||
				 pParticle->m_flLifetime >= pParticle->m_flDieTime )
			{
				if ( pParticle->m_nThinkRef != LUA_REFNIL )
					luaL_unref( L, LUA_REGISTRYINDEX, pParticle->m_nThinkRef );
				m_aDeadSerials.AddToTail( pParticle->m_iSerial );
				pIterator->RemoveParticle( pParticle );
				pParticle = (CLuaParticleX *)pIterator->GetNext();
				continue;
			}

			// CLuaParticle:Think -- SetThinkFunction schedules via SetNextThink,
			// which uses CurTime(); after a run the script must re-arm it.
			if ( pParticle->m_nThinkRef != LUA_REFNIL && L != NULL &&
				 gpGlobals->curtime >= pParticle->m_flNextThink )
			{
				pParticle->m_flNextThink = FLT_MAX;   // must SetNextThink again
				lua_rawgeti( L, LUA_REGISTRYINDEX, pParticle->m_nThinkRef );
				CLuaParticle_PushWrapper( this, pParticle );   // keeps emitter alive during the call
				luasrc_pcall( L, 1, 0, 0 );
			}

			if ( pParticle->m_flAirResistance > 0.0f )
			{
				float flScale = MAX( 0.0f, 1.0f - pParticle->m_flAirResistance * flDt );
				pParticle->m_vecVelocity *= flScale;
			}
			if ( pParticle->m_bVelocityDecay )
			{
				pParticle->m_vecVelocity *= MAX( 0.0f, 1.0f - flDt );
			}
			pParticle->m_vecVelocity += pParticle->m_vecGravity * flDt;

			Vector vDest = pParticle->m_Pos + pParticle->m_vecVelocity * flDt;
			if ( pParticle->m_bCollide && pParticle->m_vecVelocity != vec3_origin )
			{
				Ray_t ray;
				ray.Init( pParticle->m_Pos, vDest );
				trace_t tr;
				enginetrace->TraceRay( ray, MASK_SOLID, NULL, &tr );
				if ( tr.fraction < 1.0f )
				{
					vDest = tr.endpos + tr.plane.normal * 0.5f;
					// reflect around the hit normal, scaled by bounciness
					float flDot = DotProduct( pParticle->m_vecVelocity, tr.plane.normal );
					pParticle->m_vecVelocity -= ( 2.0f * flDot ) * tr.plane.normal;
					pParticle->m_vecVelocity *= pParticle->m_flBounce;
				}
			}
			pParticle->m_Pos = vDest;

			pParticle = (CLuaParticleX *)pIterator->GetNext();
		}
	}

	virtual void RenderParticles( CParticleRenderIterator *pIterator )
	{
		const CLuaParticleX *pParticle = (const CLuaParticleX *)pIterator->GetFirst();
		while ( pParticle )
		{
			float flT = 0.0f;
			if ( pParticle->m_flDieTime > 0.001f )
				flT = clamp( pParticle->m_flLifetime / pParticle->m_flDieTime, 0.0f, 1.0f );

			float flSize = pParticle->m_uchStartSize +
				( (int)pParticle->m_uchEndSize - (int)pParticle->m_uchStartSize ) * flT;
			float flAlpha = ( (int)pParticle->m_uchStartAlpha +
				( (int)pParticle->m_uchEndAlpha - (int)pParticle->m_uchStartAlpha ) * flT ) / 255.0f;
			Vector vColor( pParticle->m_uchColor[0] / 255.0f,
						   pParticle->m_uchColor[1] / 255.0f,
						   pParticle->m_uchColor[2] / 255.0f );

			float flRoll = pParticle->m_flRoll + pParticle->m_flRollDelta * pParticle->m_flLifetime;

			Vector vView;
			TransformParticle( ParticleMgr()->GetModelView(), pParticle->m_Pos, vView );
			float flSortKey = vView.z;

			if ( m_b3D )
			{
				// 3D (non-billboarded): quad in the world XY plane, engine-style.
				RenderParticle_ColorSizeAngle( pIterator->GetParticleDraw(), pParticle->m_Pos,
											   vColor, flAlpha, flSize, flRoll );
			}
			else
			{
				// 2D: screen-facing billboard.  The modelview matrix rotates
				// world->view, so its transposed rotation rows give the camera
				// right/up axes in world space.
				const VMatrix &matView = ParticleMgr()->GetModelView();
				Vector vRight( matView[0][0], matView[1][0], matView[2][0] );
				Vector vUp( matView[0][1], matView[1][1], matView[2][1] );

				// roll rotates the billboard in screen plane
				float sa, ca;
				SinCos( flRoll, &sa, &ca );
				Vector vAxisRight = vRight * ca + vUp * sa;
				Vector vAxisUp    = vUp * ca - vRight * sa;

				ParticleDraw *pDraw = pIterator->GetParticleDraw();
				if ( flAlpha >= 0.001f && pDraw->GetMeshBuilder() )
				{
					CMeshBuilder *pBuilder = pDraw->GetMeshBuilder();
					unsigned char ubColor[4];
					ubColor[0] = (unsigned char)RoundFloatToInt( vColor.x * 254.9f );
					ubColor[1] = (unsigned char)RoundFloatToInt( vColor.y * 254.9f );
					ubColor[2] = (unsigned char)RoundFloatToInt( vColor.z * 254.9f );
					ubColor[3] = (unsigned char)RoundFloatToInt( flAlpha * 254.9f );

					Vector vCorner;
					vCorner = pParticle->m_Pos - vAxisRight * flSize + vAxisUp * flSize;
					pBuilder->Position3fv( &vCorner.x );
					pBuilder->Color4ubv( ubColor );
					pBuilder->TexCoord2f( 0, pDraw->m_pSubTexture->m_tCoordMins[0], pDraw->m_pSubTexture->m_tCoordMaxs[1] );
					pBuilder->AdvanceVertex();

					vCorner = pParticle->m_Pos - vAxisRight * flSize - vAxisUp * flSize;
					pBuilder->Position3fv( &vCorner.x );
					pBuilder->Color4ubv( ubColor );
					pBuilder->TexCoord2f( 0, pDraw->m_pSubTexture->m_tCoordMins[0], pDraw->m_pSubTexture->m_tCoordMins[1] );
					pBuilder->AdvanceVertex();

					vCorner = pParticle->m_Pos + vAxisRight * flSize - vAxisUp * flSize;
					pBuilder->Position3fv( &vCorner.x );
					pBuilder->Color4ubv( ubColor );
					pBuilder->TexCoord2f( 0, pDraw->m_pSubTexture->m_tCoordMaxs[0], pDraw->m_pSubTexture->m_tCoordMins[1] );
					pBuilder->AdvanceVertex();

					vCorner = pParticle->m_Pos + vAxisRight * flSize + vAxisUp * flSize;
					pBuilder->Position3fv( &vCorner.x );
					pBuilder->Color4ubv( ubColor );
					pBuilder->TexCoord2f( 0, pDraw->m_pSubTexture->m_tCoordMaxs[0], pDraw->m_pSubTexture->m_tCoordMaxs[1] );
					pBuilder->AdvanceVertex();
				}
			}

			pParticle = (const CLuaParticleX *)pIterator->GetNext( flSortKey );
		}
	}

private:
	friend struct CLuaParticleX;
};

// ---------------------------------------------------------------------------
// Lua wrappers.  One userdata layout for both: the wrapper holds the emitter
// via CSmartPtr (so the effect outlives the script's reference) and, for
// particles, the serial used for the liveness check.
// ---------------------------------------------------------------------------
struct LuaParticleUD
{
	CSmartPtr<CLuaEmitter> m_pEmitter;
	int m_iSerial;
	CLuaParticleX *m_pParticle;
};

static int LuaEmitter_gc( lua_State *Lstate )
{
	LuaParticleUD *pUD = (LuaParticleUD *)lua_touserdata( Lstate, 1 );
	if ( pUD )
		pUD->~LuaParticleUD();
	return 0;
}

static LuaParticleUD *LuaEmitter_checkudata( lua_State *Lstate, int iArg )
{
	LuaParticleUD *pUD = (LuaParticleUD *)luaL_checkudata( Lstate, iArg, "CSEmitter" );
	if ( pUD == NULL )
		luaL_argerror( Lstate, iArg, "CSEmitter expected" );
	return pUD;
}

static CLuaParticleX *LuaParticle_checkalive( lua_State *Lstate, int iArg, LuaParticleUD **ppOut = NULL )
{
	LuaParticleUD *pUD = (LuaParticleUD *)luaL_checkudata( Lstate, iArg, "CLuaParticle" );
	if ( pUD == NULL )
		luaL_argerror( Lstate, iArg, "CLuaParticle expected" );
	if ( ppOut )
		*ppOut = pUD;
	if ( pUD->m_pEmitter.GetObject() == NULL || !pUD->m_pEmitter->IsSerialAlive( pUD->m_iSerial ) )
		return NULL;   // dead particle: getters return what they remember,
					   // setters no-op (below all guard this)
	return pUD->m_pParticle;
}

// Push a fresh wrapper for a live particle (used by Add() and by Think calls).
static void CLuaParticle_PushWrapper( CLuaEmitter *pEmitter, CLuaParticleX *pParticle )
{
	LuaParticleUD *pUD = (LuaParticleUD *)lua_newuserdata( L, sizeof( LuaParticleUD ) );
	new ( &pUD->m_pEmitter ) CSmartPtr<CLuaEmitter>( pEmitter );
	pUD->m_iSerial = pParticle->m_iSerial;
	pUD->m_pParticle = pParticle;
	luaL_getmetatable( L, "CLuaParticle" );
	lua_setmetatable( L, -2 );
}

LUA_REGISTRATION_INIT( CSEmitterReg );
LUA_REGISTRATION_INIT( CLuaParticleReg );

// ---------------------------------------------------------------------------
// CSEmitter methods
// ---------------------------------------------------------------------------

// CSEmitter:Add( material, position ) -> CLuaParticle
LUA_BINDING_BEGIN( CSEmitterReg, Add, "method", "Creates a new CLuaParticle with the given material and position.", "client" )
{
	LuaParticleUD *pUD = LuaEmitter_checkudata( L, 1 );
	CLuaEmitter *pEmitter = pUD->m_pEmitter.GetObject();
	if ( pEmitter == NULL || pEmitter->m_bFinished )
		return 0;   // GMod: unusable after Finish()

	const char *pszMaterial = luaL_checkstring( L, 2 );
	Vector vecPos = luaL_checkvector( L, 3 );

	PMaterialHandle hMaterial = ParticleMgr()->GetPMaterial( pszMaterial );

	CLuaParticleX *pParticle = (CLuaParticleX *)pEmitter->AddParticle( sizeof( CLuaParticleX ), hMaterial, vecPos );
	if ( pParticle == NULL )
		return 0;

	pParticle->m_iSerial = ++s_iLuaParticleSerial;
	pParticle->m_bRemoveRequested = false;
	pParticle->m_Pos = vecPos;
	pParticle->m_vecVelocity.Init();
	pParticle->m_flRoll = 0.0f;
	pParticle->m_flRollDelta = 0.0f;
	pParticle->m_flLifetime = 0.0f;
	pParticle->m_flDieTime = PARTICLE_DEFAULT_DIETIME;
	pParticle->m_uchColor[0] = pParticle->m_uchColor[1] = pParticle->m_uchColor[2] = 255;
	pParticle->m_uchStartAlpha = 255;
	pParticle->m_uchEndAlpha = 0;
	pParticle->m_uchStartSize = 1;
	pParticle->m_uchEndSize = 1;
	pParticle->m_iFlags = 0;
	pParticle->m_vecGravity.Init();
	pParticle->m_flAirResistance = 0.0f;
	pParticle->m_bCollide = false;
	pParticle->m_flBounce = 1.0f;
	pParticle->m_bVelocityDecay = false;
	pParticle->m_flStartLength = 0.0f;
	pParticle->m_flEndLength = 0.0f;
	pParticle->m_nThinkRef = LUA_REFNIL;
	pParticle->m_flNextThink = 0.0f;

	CLuaParticle_PushWrapper( pEmitter, pParticle );
	return 1;
}
LUA_BINDING_END( "CLuaParticle", "The created particle, or nil." )

// CSEmitter:Finish()
LUA_BINDING_BEGIN( CSEmitterReg, Finish, "method", "Removes the emitter, making it no longer usable from Lua. If particles remain, the emitter will be removed when all particles die.", "client" )
{
	LuaParticleUD *pUD = LuaEmitter_checkudata( L, 1 );
	if ( pUD->m_pEmitter.GetObject() != NULL )
		pUD->m_pEmitter->Finish();
	return 0;
}
LUA_BINDING_END()

// ---------------------------------------------------------------------------
// CLuaParticle methods.  Dead particles: getters return the remembered value,
// setters are a no-op -- a script that keeps a particle across frames must not
// crash when the particle's lifetime runs out underneath it.
// ---------------------------------------------------------------------------

#define LUA_PARTICLE_SETTER_BEGIN( name )                                       \
	LUA_BINDING_BEGIN( CLuaParticleReg, name, "method", "", "client" )          \
	{                                                                            \
		CLuaParticleX *pParticle = LuaParticle_checkalive( L, 1 );               \
		if ( pParticle )                                                         \
		{

#define LUA_PARTICLE_SETTER_END( nret ) \
		}                        \
		return nret;             \
	}                            \
	LUA_BINDING_END()

LUA_PARTICLE_SETTER_BEGIN( SetStartAlpha )
	pParticle->m_uchStartAlpha = (unsigned char)clamp( (int)luaL_checknumber( L, 2 ), 0, 255 );
LUA_PARTICLE_SETTER_END( 0 )

LUA_PARTICLE_SETTER_BEGIN( GetStartAlpha )
	lua_pushnumber( L, pParticle->m_uchStartAlpha );
LUA_PARTICLE_SETTER_END( 1 )

LUA_PARTICLE_SETTER_BEGIN( SetEndAlpha )
	pParticle->m_uchEndAlpha = (unsigned char)clamp( (int)luaL_checknumber( L, 2 ), 0, 255 );
LUA_PARTICLE_SETTER_END( 0 )

LUA_PARTICLE_SETTER_BEGIN( GetEndAlpha )
	lua_pushnumber( L, pParticle->m_uchEndAlpha );
LUA_PARTICLE_SETTER_END( 1 )

LUA_PARTICLE_SETTER_BEGIN( SetVelocity )
	pParticle->m_vecVelocity = luaL_checkvector( L, 2 );
LUA_PARTICLE_SETTER_END( 0 )

LUA_PARTICLE_SETTER_BEGIN( SetStartSize )
	pParticle->m_uchStartSize = (unsigned char)clamp( (int)luaL_checknumber( L, 2 ), 0, 255 );
LUA_PARTICLE_SETTER_END( 0 )

LUA_PARTICLE_SETTER_BEGIN( SetEndSize )
	pParticle->m_uchEndSize = (unsigned char)clamp( (int)luaL_checknumber( L, 2 ), 0, 255 );
LUA_PARTICLE_SETTER_END( 0 )

LUA_PARTICLE_SETTER_BEGIN( SetDieTime )
	// GMod wiki: "Sets the time where the particle will be removed" -- relative
	// to now, since lifetime counts up from 0 at creation.
	pParticle->m_flDieTime = MAX( 0.0f, (float)luaL_checknumber( L, 2 ) );
LUA_PARTICLE_SETTER_END( 0 )

LUA_PARTICLE_SETTER_BEGIN( GetDieTime )
	lua_pushnumber( L, pParticle->m_flDieTime );
LUA_PARTICLE_SETTER_END( 1 )

LUA_PARTICLE_SETTER_BEGIN( SetLifeTime )
	pParticle->m_flLifetime = MAX( 0.0f, (float)luaL_checknumber( L, 2 ) );
LUA_PARTICLE_SETTER_END( 0 )

LUA_PARTICLE_SETTER_BEGIN( GetLifeTime )
	lua_pushnumber( L, pParticle->m_flLifetime );
LUA_PARTICLE_SETTER_END( 1 )

LUA_PARTICLE_SETTER_BEGIN( SetColor )
	pParticle->m_uchColor[0] = (unsigned char)clamp( (int)luaL_checknumber( L, 2 ), 0, 255 );
	pParticle->m_uchColor[1] = (unsigned char)clamp( (int)luaL_checknumber( L, 3 ), 0, 255 );
	pParticle->m_uchColor[2] = (unsigned char)clamp( (int)luaL_checknumber( L, 4 ), 0, 255 );
LUA_PARTICLE_SETTER_END( 0 )

LUA_PARTICLE_SETTER_BEGIN( SetRoll )
	pParticle->m_flRoll = (float)luaL_checknumber( L, 2 );
LUA_PARTICLE_SETTER_END( 0 )

LUA_PARTICLE_SETTER_BEGIN( SetRollDelta )
	pParticle->m_flRollDelta = (float)luaL_checknumber( L, 2 );
LUA_PARTICLE_SETTER_END( 0 )

LUA_PARTICLE_SETTER_BEGIN( SetAirResistance )
	pParticle->m_flAirResistance = MAX( 0.0f, (float)luaL_checknumber( L, 2 ) );
LUA_PARTICLE_SETTER_END( 0 )

LUA_PARTICLE_SETTER_BEGIN( SetGravity )
	pParticle->m_vecGravity = luaL_checkvector( L, 2 );
LUA_PARTICLE_SETTER_END( 0 )

LUA_PARTICLE_SETTER_BEGIN( SetCollide )
	pParticle->m_bCollide = luaL_checkboolean( L, 2 ) ? true : false;
LUA_PARTICLE_SETTER_END( 0 )

LUA_PARTICLE_SETTER_BEGIN( SetBounce )
	pParticle->m_flBounce = MAX( 0.0f, (float)luaL_checknumber( L, 2 ) );
LUA_PARTICLE_SETTER_END( 0 )

LUA_PARTICLE_SETTER_BEGIN( SetStartLength )
	pParticle->m_flStartLength = MAX( 0.0f, (float)luaL_checknumber( L, 2 ) );
LUA_PARTICLE_SETTER_END( 0 )

LUA_PARTICLE_SETTER_BEGIN( SetEndLength )
	pParticle->m_flEndLength = MAX( 0.0f, (float)luaL_checknumber( L, 2 ) );
LUA_PARTICLE_SETTER_END( 0 )

// VelocityDecay( bool ) -- enables an exponential velocity decay toward zero.
LUA_PARTICLE_SETTER_BEGIN( VelocityDecay )
	pParticle->m_bVelocityDecay = luaL_checkboolean( L, 2 ) ? true : false;
LUA_PARTICLE_SETTER_END( 0 )

// CLuaParticle:SetThinkFunction( fn )
LUA_PARTICLE_SETTER_BEGIN( SetThinkFunction )
	if ( pParticle->m_nThinkRef != LUA_REFNIL )
	{
		luaL_unref( L, LUA_REGISTRYINDEX, pParticle->m_nThinkRef );
		pParticle->m_nThinkRef = LUA_REFNIL;
	}
	if ( lua_isfunction( L, 2 ) )
	{
		lua_pushvalue( L, 2 );
		pParticle->m_nThinkRef = luaL_ref( L, LUA_REGISTRYINDEX );
		pParticle->m_flNextThink = 0.0f;   // GMod: first think on the next SetNextThink
	}
LUA_PARTICLE_SETTER_END( 0 )

// CLuaParticle:SetNextThink( time ) -- absolute CurTime(), per the wiki.
LUA_PARTICLE_SETTER_BEGIN( SetNextThink )
	pParticle->m_flNextThink = (float)luaL_checknumber( L, 2 );
LUA_PARTICLE_SETTER_END( 0 )

// CLuaParticle:Remove() -- GMod lets a think function kill its particle.
LUA_PARTICLE_SETTER_BEGIN( Remove )
	// Marked dead by serial; the simulator reaps it on its next pass.
	LuaParticleUD *pUD = NULL;
	LuaParticle_checkalive( L, 1, &pUD );
	if ( pUD && pUD->m_pEmitter.GetObject() != NULL && pUD->m_pParticle )
		pUD->m_pParticle->m_bRemoveRequested = true;
LUA_PARTICLE_SETTER_END( 0 )

// ---------------------------------------------------------------------------
// Global ParticleEmitter( position, use3D )
// ---------------------------------------------------------------------------
static int LuaGlobal_ParticleEmitter( lua_State *Lstate )
{
	Vector vecPos = luaL_checkvector( Lstate, 1 );
	bool b3D = luaL_optboolean( Lstate, 2, 0 ) ? true : false;

	CSmartPtr<CLuaEmitter> pEmitter = CLuaEmitter::Create( vecPos, b3D );

	LuaParticleUD *pUD = (LuaParticleUD *)lua_newuserdata( Lstate, sizeof( LuaParticleUD ) );
	new ( &pUD->m_pEmitter ) CSmartPtr<CLuaEmitter>( pEmitter );
	pUD->m_iSerial = 0;
	pUD->m_pParticle = NULL;
	luaL_getmetatable( Lstate, "CSEmitter" );
	lua_setmetatable( Lstate, -2 );
	return 1;
}

// ---------------------------------------------------------------------------
// luaopen: registers the two metatables and the ParticleEmitter global.
// ---------------------------------------------------------------------------
LUALIB_API int luaopen_CSEmitter( lua_State *Lstate )
{
	luaL_newmetatable( Lstate, "CSEmitter" );
	LUA_REGISTRATION_COMMIT( CSEmitterReg );
	lua_pushvalue( Lstate, -1 );
	lua_setfield( Lstate, -2, "__index" );
	lua_pushcfunction( Lstate, LuaEmitter_gc );
	lua_setfield( Lstate, -2, "__gc" );
	lua_pop( Lstate, 1 );

	luaL_newmetatable( Lstate, "CLuaParticle" );
	LUA_REGISTRATION_COMMIT( CLuaParticleReg );
	lua_pushvalue( Lstate, -1 );
	lua_setfield( Lstate, -2, "__index" );
	lua_pushcfunction( Lstate, LuaEmitter_gc );
	lua_setfield( Lstate, -2, "__gc" );
	lua_pop( Lstate, 1 );

	lua_pushcfunction( Lstate, LuaGlobal_ParticleEmitter );
	lua_setglobal( Lstate, "ParticleEmitter" );
	return 0;
}
