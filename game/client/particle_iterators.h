//========= Copyright Valve Corporation, All rights reserved. ============//
//
// Purpose: Client side CTFTeam class
//
// $NoKeywords: $
//=============================================================================//

#ifndef PARTICLE_ITERATORS_H
#define PARTICLE_ITERATORS_H
#ifdef _WIN32
#pragma once
#endif


#include "materialsystem/imesh.h"
#include "particledraw.h"


#define NUM_PARTICLES_PER_BATCH 200
#ifndef _XBOX
// HL2SB (2026-09-21): was 2048.  A single Nuke Pack detonation bursts
// hundreds of particles per think tick across six effects, and this pool cap
// hit within seconds -- every emitter:Add() after that returned NULL (the
// 02:10 build still printed exactly one
// "ParticleEmitter:Add('particles/smokey'): particle allocation failed"
// mid-explosion, after the HL2 flamelet materials had already started
// succeeding).  8192 keeps the sort buffers below trivial and matches what
// those effects actually ask for.
// HL2SB (2026-09-24): 8192 -> 16384.  ONE Nuke Pack detonation (ground
// cloud 681 + blastwave ring + per-victim disintegrates) filled the pool in
// 4 seconds (half at +1.9s, FULL at +4.5s) - every particle after that
// silently failed, killing the back half of the fireball.  4MB of memory,
// and the sort buffer (zCoords[]) stays at 64KB.
// HL2SB (2026-09-24, take 2): 16384 STILL filled in ~4s of one detonation
// (the blastwave effect's wave resolution gets truncated to 0 by the addon's
// SetGlobalInt chain, so it adds ~70 particles per TICK).  32768 = 8MB,
// sort buffer stays on its own MAX_SORT_KEYS=8192 with the write clamp.
// HL2SB (2026-09-24 take 3): 32768 also filled - blastwave self-throttles at
// 0.01s (script constant) while ground smoke lives 62s; demand = rate x life.
// 65536 = 16MB; sort stays MAX_SORT_KEYS=8192 clamped, stack cost unchanged.
#define MAX_TOTAL_PARTICLES		65536	// Max particles in the world

// HL2SB (2026-09-24): the bucket-sort key buffer is sized SEPARATELY from the
// pool.  Tying it to MAX_TOTAL_PARTICLES put a 64KB array on the stack of
// every DrawMaterialParticles call (01:14's __chkstk STACK_OVERFLOW beside a
// deep Push3DView frame), and an active-count that outpaces the cap would
// smash the adjacent mesh-builder pointer (01:27's AV WRITE).  8192 keys on
// the stack like stock, with a hard clamp on the write: particles beyond the
// buffer simply join the unsorted tail - degraded order, never a crash.
#define MAX_SORT_KEYS			8192
#else
#define MAX_TOTAL_PARTICLES		1024
#endif


//
// Iterate the particles like this:
//
// Particle *pCur = pIterator->GetFirst();
// while ( pCur )
// {
//     ... render the particle here and figure out the sort key and position
//     pCur = pIterator->GetNext( sortKey, pCur->m_Pos );
// }
//
class CParticleRenderIterator
{
friend class CParticleMgr;
friend class CParticleEffectBinding;
public:
	CParticleRenderIterator();

	// The sort key is used to sort the particles incrementally as they're rendered.
	// They only get sorted in the main rendered view (ie: not in reflections or monitors).
	// These return const because you should only modify particles during their simulation.
	const Particle* GetFirst();
	const Particle* GetNext( float sortKey );

	// Use this to render. This can return NULL, in which case you shouldn't render.
	// This being NULL is a carryover from when particles rendered and simulated together and
	// it should GO AWAY SOON!
	ParticleDraw* GetParticleDraw() const;

	// Quads the current mesh batch can still take.  TestFlushBatch() re-locks
	// the mesh every NUM_PARTICLES_PER_BATCH particles; effects that write
	// their own quads (CLuaEmitter) must stay inside this budget - after a
	// failed mesh re-lock the builder's pointers are stale and one quad too
	// many is an AV WRITE (2026-09-21 nuke explosion crash).
	int GetQuadsLeftInBatch() const { return NUM_PARTICLES_PER_BATCH - m_nParticlesInCurrentBatch; }


private:

	void TestFlushBatch();


private:
	// Set by CParticleMgr.
	CParticleEffectBinding *m_pEffectBinding;
	CEffectMaterial *m_pMaterial;
	ParticleDraw *m_pParticleDraw;
	CMeshBuilder *m_pMeshBuilder;
	IMesh *m_pMesh;
	bool m_bBucketSort;
	
	// Output after rendering.
	float m_MinZ;
	float m_MaxZ;
	float m_zCoords[MAX_SORT_KEYS];
	int m_nZCoords;
	
	Particle *m_pCur;
	bool m_bGotFirst;
	float m_flPrevZ;
	int m_nParticlesInCurrentBatch;
};


//
// Iterate the particles like this:
//
// Particle *pCur = pIterator->GetFirst();
// while ( pCur )
// {
//     ... simulate here.. call pIterator->RemoveParticle if you want the particle to go away
//     pCur = pIterator->GetNext();
// }
//
class CParticleSimulateIterator
{
friend class CParticleMgr;
friend class CParticleEffectBinding;
public:
	CParticleSimulateIterator();
	
	// Iterate through the particles, simulate them, and remove them if necessary.
	Particle* GetFirst();
	Particle* GetNext();
	float GetTimeDelta() const;

	void RemoveParticle( Particle *pParticle );
	void RemoveAllParticles();

private:
	CParticleEffectBinding *m_pEffectBinding;
	CEffectMaterial *m_pMaterial;
	float m_flTimeDelta;

	bool m_bGotFirst;
	Particle *m_pNextParticle;
};


// -------------------------------------------------------------------------------------------------------- //
// CParticleRenderIterator inlines
// -------------------------------------------------------------------------------------------------------- //

inline CParticleRenderIterator::CParticleRenderIterator()
{
	m_pCur = NULL;
	m_bGotFirst = false;
	m_flPrevZ = 0;
	m_nParticlesInCurrentBatch = 0;
	m_MinZ = 1e24;
	m_MaxZ = -1e24;
	m_nZCoords = 0;
}

inline const Particle* CParticleRenderIterator::GetFirst()
{
	Assert( !m_bGotFirst );
	m_bGotFirst = true;

	m_pCur = m_pMaterial->m_Particles.m_pNext;
	if ( m_pCur == &m_pMaterial->m_Particles )
		return NULL;

	m_pParticleDraw->m_pSubTexture = m_pCur->m_pSubTexture;
	return m_pCur;
}

inline void CParticleRenderIterator::TestFlushBatch()
{
	++m_nParticlesInCurrentBatch;
	if( m_nParticlesInCurrentBatch >= NUM_PARTICLES_PER_BATCH )
	{
		m_pMeshBuilder->End( false, true );
		m_pMeshBuilder->Begin( m_pMesh, MATERIAL_QUADS, NUM_PARTICLES_PER_BATCH * 4 );

		m_nParticlesInCurrentBatch = 0;
	}
}

inline const Particle* CParticleRenderIterator::GetNext( float sortKey )
{
	Assert( m_bGotFirst );
	Assert( m_pCur );

	TestFlushBatch();

	Particle *pNext = m_pCur->m_pNext;

	// Update the incremental sort.
	if( m_bBucketSort )
	{
		m_MinZ = MIN( sortKey, m_MinZ );
		m_MaxZ = MAX( sortKey, m_MaxZ );
		
		if ( m_nZCoords < MAX_SORT_KEYS )
		{
			m_zCoords[m_nZCoords] = sortKey;
			++m_nZCoords;
		}
	}
	else
	{
		// Swap with the previous particle (incremental sort)?
		if( m_pCur != m_pMaterial->m_Particles.m_pNext && m_flPrevZ > sortKey )
		{
			SwapParticles( m_pCur->m_pPrev, m_pCur );
		}
		else
		{
			m_flPrevZ = sortKey;
		}
	}

	m_pCur = pNext;
	if ( m_pCur == &m_pMaterial->m_Particles )
		return NULL;

	m_pParticleDraw->m_pSubTexture = m_pCur->m_pSubTexture;
	return m_pCur;
}

inline ParticleDraw* CParticleRenderIterator::GetParticleDraw() const
{
	return m_pParticleDraw;
}


// -------------------------------------------------------------------------------------------------------- //
// CParticleSimulateIterator inlines
// -------------------------------------------------------------------------------------------------------- //

inline CParticleSimulateIterator::CParticleSimulateIterator()
{
	m_pNextParticle = NULL;
#ifdef _DEBUG
	m_bGotFirst = false;
#endif
}

inline Particle* CParticleSimulateIterator::GetFirst()
{
#ifdef _DEBUG
	// Make sure they're either starting out fresh or that the previous guy iterated through all the particles.
	if ( m_bGotFirst )
	{
		Assert( m_pNextParticle == &m_pMaterial->m_Particles );
	}
#endif

	Particle *pRet = m_pMaterial->m_Particles.m_pNext;
	if ( pRet == &m_pMaterial->m_Particles )
		return NULL;

#ifdef _DEBUG
	m_bGotFirst = true;
#endif

	m_pNextParticle = pRet->m_pNext;
	return pRet;
}

inline Particle* CParticleSimulateIterator::GetNext()
{
	Particle *pRet = m_pNextParticle;

	if ( pRet == &m_pMaterial->m_Particles )
		return NULL;
	
	m_pNextParticle = pRet->m_pNext;
	return pRet;
}

inline void CParticleSimulateIterator::RemoveParticle( Particle *pParticle )
{
	m_pEffectBinding->RemoveParticle( pParticle );
}

inline void CParticleSimulateIterator::RemoveAllParticles()
{
	Particle *pParticle = GetFirst();
	while ( pParticle )
	{
		RemoveParticle( pParticle );
		pParticle = GetNext();
	}
}

inline float CParticleSimulateIterator::GetTimeDelta() const
{
	return m_flTimeDelta;
}


#endif // PARTICLE_ITERATORS_H

