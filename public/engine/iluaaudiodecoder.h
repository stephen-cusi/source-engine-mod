#ifndef I_LUA_AUDIO_DECODER_H
#define I_LUA_AUDIO_DECODER_H

#include <stddef.h>
#include <stdint.h>
#include "tier1/interface.h"

#define HL2SB_AUDIO_DECODER_INTERFACE_VERSION "HL2SBAudioDecoder001"

enum HL2SBAudioDecodeResult
{
	HL2SB_AUDIO_DECODE_OK,
	HL2SB_AUDIO_DECODE_END,
	HL2SB_AUDIO_DECODE_MEMORY,
	HL2SB_AUDIO_DECODE_ERROR
};

typedef size_t (*HL2SBAudioReadFn)( void *user, void *buffer, size_t bytes );
typedef bool (*HL2SBAudioSeekFn)( void *user, int64_t offset, bool relative );

class ILuaAudioDecoder : public IBaseInterface
{
public:
	virtual void *Open( void *user, HL2SBAudioReadFn read, HL2SBAudioSeekFn seek,
		unsigned int channels, unsigned int sampleRate, HL2SBAudioDecodeResult *result ) = 0;
	virtual void Close( void *decoder ) = 0;
	virtual HL2SBAudioDecodeResult Read( void *decoder, float *output, uint64_t frames, uint64_t *read ) = 0;
	virtual bool Seek( void *decoder, uint64_t frame ) = 0;
	virtual bool Length( void *decoder, uint64_t *frames ) = 0;
};

#endif
