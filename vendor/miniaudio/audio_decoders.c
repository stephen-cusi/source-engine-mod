//=============================================================================
//
// HL2SB: miniaudio decoder translation unit for the sound.PlayFile runtime.
//
// Compiles the vendored miniaudio amalgamation plus its stb_vorbis-backed
// Ogg Vorbis decoding backend. The library is used strictly in no-device
// mode: this project mixes decoded PCM into the engine paint buffer through
// the HL2SBLuaStreamMix001 interface, so no miniaudio context/device is ever
// created and playback/recording/encoding/generation APIs are compiled out.
//
// stb_vorbis must be compiled (and its header guard visible) BEFORE
// miniaudio.h so the amalgamation defines MA_HAS_VORBIS and the
// g_ma_decoding_backend_vtable_stbvorbis backend vtable. That vtable is a
// file-static symbol inside the amalgamation, invisible to other translation
// units, so this file exposes the HL2SB_ma_decoder_* wrappers below and the
// C++ audio runtime links against those instead of reaching into miniaudio
// internals. WAV / MP3 / FLAC decoders are built into the amalgamation.
//
// Licensing: miniaudio is public domain / MIT-0 (see the end of
// miniaudio.h); stb_vorbis is public domain / MIT (see the end of
// stb_vorbis.c). Both are vendored here unmodified from upstream releases.
//
//=============================================================================

#include "../stb/stb_vorbis.c"

// stb_vorbis leaks single-letter playback-position macros (L/C/R, used to
// seed its channel_position table) and never undefines them; they would
// shatter every later header that uses L as an identifier (windows.h
// bitfields included). Recover the identifiers before anything else.
#undef PLAYBACK_MONO
#undef PLAYBACK_LEFT
#undef PLAYBACK_RIGHT
#undef L
#undef C
#undef R

#define MA_NO_DEVICE_IO
#define MA_NO_ENCODING
#define MA_NO_GENERATION
#define MINIAUDIO_IMPLEMENTATION
#include "miniaudio.h"

static ma_decoding_backend_vtable *const g_pHL2SBDecoderBackends[] =
{
	&g_ma_decoding_backend_vtable_stbvorbis,
};

/*
** Initializes a decoder over custom IO (the engine file system) with the
** stb_vorbis backend registered as a custom decoding backend, requesting
** f32 output at the given channel count and rate. miniaudio converts
** everything else (container, source format, rate and channel count).
*/
#ifdef __cplusplus
extern "C"
#endif
ma_result HL2SB_ma_decoder_init( ma_decoder_read_proc onRead, ma_decoder_seek_proc onSeek,
								void *pUserData, ma_uint32 channels, ma_uint32 sampleRate,
								ma_decoder *pDecoder )
{
	ma_decoder_config config = ma_decoder_config_init( ma_format_f32, channels, sampleRate );
	config.ppCustomBackendVTables = ( ma_decoding_backend_vtable ** )g_pHL2SBDecoderBackends;
	config.customBackendCount = ( int )( sizeof( g_pHL2SBDecoderBackends ) / sizeof( g_pHL2SBDecoderBackends[ 0 ] ) );

	return ma_decoder_init( onRead, onSeek, pUserData, &config, pDecoder );
}
