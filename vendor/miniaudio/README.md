# vendor/miniaudio

Vendored upstream miniaudio for the HL2SB `sound.PlayFile` runtime
(game/client/lua/lgmodaudio.cpp).

## Files

- `miniaudio.h` - miniaudio v0.11.22 (2025-02-24) amalgamation, unmodified
  upstream release from https://github.com/mackron/miniaudio
  (tag v0.11.22).
- `audio_decoders.c` - HL2SB-owned translation unit. Compiles the
  amalgamation with `MA_NO_DEVICE_IO` / `MA_NO_ENCODING` /
  `MA_NO_GENERATION` (decoders only, no audio device is ever opened) and
  exposes `HL2SB_ma_decoder_init`, which registers the amalgamation's
  built-in stb_vorbis-backed Ogg Vorbis backend (its vtable is static
  inside the amalgamation, hence the wrapper).
- `../stb/stb_vorbis.c` - stb_vorbis v1.14, unmodified upstream release
  from https://github.com/nothings/stb (copied from the stb repository,
  single-file C library).

## Licensing

- miniaudio: public domain or MIT-0, at the user's choice (see the license
  statement at the end of miniaudio.h).
- stb_vorbis: public domain or MIT, at the user's choice (see the license
  statement at the end of stb_vorbis.c).

Both licenses permit static vendoring without attribution requirements;
this file records provenance and version for auditing.

## Consumers

`audio/audio_decoder_module.cpp` and `audio_decoders.c` build the standalone
`miniaudio` shared library. The client loads its versioned decoder interface
at runtime; audio output remains with the engine mixer. Windows and desktop
Unix install the module alongside engine libraries in `bin`. Android uses
the platform's native-library naming and installation conventions.
