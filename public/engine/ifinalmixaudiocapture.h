#pragma once

#include "appframework/IAppSystem.h"

#define HL2SB_FINAL_MIX_CAPTURE_VERSION "HL2SBFinalMixCapture001"

// A single recorder owns this bounded queue. Read returns interleaved signed
// 16-bit stereo frames. The mixer never waits for the consumer.
class IFinalMixAudioCapture
{
public:
	virtual bool Start() = 0;
	virtual void Stop() = 0;
	virtual int Read( short *samples, int maxFrames ) = 0;
	virtual int SampleRate() = 0;
	virtual unsigned int DroppedFrames() = 0;
};
