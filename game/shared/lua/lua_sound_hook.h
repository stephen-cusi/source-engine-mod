#ifndef HL2SB_LUA_SOUND_HOOK_H
#define HL2SB_LUA_SOUND_HOOK_H

#include "tier1/utlstring.h"
#include "mathlib/vector.h"

class CBaseEntity;

struct HL2SB_SoundHookData
{
	CBaseEntity *entity;
	CUtlString soundName;
	CUtlString originalSoundName;
	int channel;
	int soundLevel;
	int flags;
	int pitch;
	int dsp;
	float volume;
	float soundTime;
	bool hasPosition;
	Vector position;
};

// False means the hook suppressed the sound.
bool HL2SB_LuaEntityEmitSound( HL2SB_SoundHookData &sound );

#endif
