#ifndef I_LUA_STREAM_MIX_H
#define I_LUA_STREAM_MIX_H

#include "tier1/interface.h"

#define HL2SB_LUA_STREAM_MIX_INTERFACE_VERSION "HL2SBLuaStreamMix001"

typedef void (*HL2SBLuaStreamMixCallback)( float *stereo, int frames, int sampleRate, void *user );

class ILuaStreamMix : public IBaseInterface
{
public:
	virtual bool RegisterCallback( HL2SBLuaStreamMixCallback callback, void *user ) = 0;
	virtual void UnregisterCallback( HL2SBLuaStreamMixCallback callback, void *user ) = 0;
};

#endif
