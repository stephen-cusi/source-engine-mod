#include "engine/iluaaudiodecoder.h"
#include "../vendor/miniaudio/miniaudio.h"
#include <new>

extern "C" ma_result HL2SB_ma_decoder_init( ma_decoder_read_proc, ma_decoder_seek_proc,
	void *, ma_uint32, ma_uint32, ma_decoder * );

struct AudioDecoder
{
	ma_decoder decoder;
	void *user;
	HL2SBAudioReadFn read;
	HL2SBAudioSeekFn seek;
};

static ma_result DecoderRead( ma_decoder *decoder, void *buffer, size_t bytes, size_t *read )
{
	AudioDecoder *instance = static_cast<AudioDecoder *>( decoder->pUserData );
	size_t count = instance->read( instance->user, buffer, bytes );
	if ( read ) *read = count;
	return count ? MA_SUCCESS : MA_AT_END;
}

static ma_result DecoderSeek( ma_decoder *decoder, ma_int64 offset, ma_seek_origin origin )
{
	AudioDecoder *instance = static_cast<AudioDecoder *>( decoder->pUserData );
	return instance->seek( instance->user, offset, origin == ma_seek_origin_current ) ? MA_SUCCESS : MA_ERROR;
}

class CLuaAudioDecoder : public ILuaAudioDecoder
{
public:
	void *Open( void *user, HL2SBAudioReadFn read, HL2SBAudioSeekFn seek,
		unsigned int channels, unsigned int sampleRate, HL2SBAudioDecodeResult *result )
	{
		AudioDecoder *instance = new ( std::nothrow ) AudioDecoder;
		if ( !instance )
		{
			*result = HL2SB_AUDIO_DECODE_MEMORY;
			return NULL;
		}
		instance->user = user;
		instance->read = read;
		instance->seek = seek;
		ma_result status = HL2SB_ma_decoder_init( DecoderRead, DecoderSeek, instance, channels, sampleRate, &instance->decoder );
		if ( status != MA_SUCCESS )
		{
			delete instance;
			*result = status == MA_OUT_OF_MEMORY ? HL2SB_AUDIO_DECODE_MEMORY : HL2SB_AUDIO_DECODE_ERROR;
			return NULL;
		}
		*result = HL2SB_AUDIO_DECODE_OK;
		return instance;
	}

	void Close( void *decoder )
	{
		AudioDecoder *instance = static_cast<AudioDecoder *>( decoder );
		if ( instance ) { ma_decoder_uninit( &instance->decoder ); delete instance; }
	}

	HL2SBAudioDecodeResult Read( void *decoder, float *output, uint64_t frames, uint64_t *read )
	{
		ma_uint64 count = 0;
		ma_result status = ma_decoder_read_pcm_frames( &static_cast<AudioDecoder *>( decoder )->decoder, output, frames, &count );
		*read = count;
		return status == MA_SUCCESS ? HL2SB_AUDIO_DECODE_OK : status == MA_AT_END ? HL2SB_AUDIO_DECODE_END : HL2SB_AUDIO_DECODE_ERROR;
	}

	bool Seek( void *decoder, uint64_t frame )
	{
		return ma_decoder_seek_to_pcm_frame( &static_cast<AudioDecoder *>( decoder )->decoder, frame ) == MA_SUCCESS;
	}

	bool Length( void *decoder, uint64_t *frames )
	{
		ma_uint64 count = 0;
		bool ok = ma_decoder_get_length_in_pcm_frames( &static_cast<AudioDecoder *>( decoder )->decoder, &count ) == MA_SUCCESS;
		*frames = count;
		return ok;
	}
};

static CLuaAudioDecoder s_LuaAudioDecoder;
EXPOSE_SINGLE_INTERFACE_GLOBALVAR( CLuaAudioDecoder, ILuaAudioDecoder,
	HL2SB_AUDIO_DECODER_INTERFACE_VERSION, s_LuaAudioDecoder );
