//========= Copyright HL2SB, all rights reserved. ============//
//
// Purpose: HL2SB: the client half of GMod's .pcf engine-particle Lua surface.
//          lnewparticle.cpp owns the CNewParticleEffect metatable plus the
//          CreateParticleSystem / CreateParticleSystemNoEntity globals; the
//          shared GMod globals ParticleEffect / ParticleEffectAttach (GMod
//          realm: Shared) live in game/shared/lua/lparticle_system.cpp and
//          call into these helpers on the client.
//
//          Deliberately a new header instead of an entry in luasrclib.h:
//          waf has no header dependency propagation (AGENTS.md section 2),
//          so touching a shared header would force a full-tree rebuild for a
//          surface only two .cpp files need.
//
// $NoKeywords: $
//===========================================================================//

#ifdef CLIENT_DLL

#ifndef HL2SB_LNEWPARTICLE_H
#define HL2SB_LNEWPARTICLE_H

struct lua_State;  // pulled in for real by luamanager.h before this header
class CNewParticleEffect;
class CBaseEntity;
class Vector;
class QAngle;

// Push the Lua wrapper for a CNewParticleEffect (the "CNewParticleEffect"
// metatable).  NULL hands back nil, exactly like GMod.
void HL2SB_PushNewParticleEffect( lua_State *L, CNewParticleEffect *pEffect );

// The client half of Global.ParticleEffect: create locally, no TE involved.
// pParent == NULL spawns a free-standing system at vecPos with CP0/CP1 set to
// it and CP0 oriented by angOrient (GMod: "the angles only take effect when
// the entity argument is provided" -- same shape here, just parented).
CNewParticleEffect *HL2SB_ClientCreateParticleEffect( const char *pszName,
	const Vector &vecPos, const QAngle &angOrient, CBaseEntity *pParent );

// The client half of Global.ParticleEffectAttach.
CNewParticleEffect *HL2SB_ClientAttachParticleEffect( const char *pszName,
	int iAttachType, CBaseEntity *pEntity, int iAttachmentPoint );

#endif // HL2SB_LNEWPARTICLE_H

#endif // CLIENT_DLL
