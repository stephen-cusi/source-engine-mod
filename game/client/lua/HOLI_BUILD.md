# Optional holi recording module

The client loads `holi` at runtime. Builds without its codec SDK still build
the client; recording reports that the module is unavailable.

On Windows x64, a static MSVC dynamic-CRT SDK can be built with vcpkg using
`ffmpeg[core,avcodec,avformat,swscale,swresample,theora,vorbis,vpx]` and the
`x64-windows-static-md` triplet. Set `HL2SB_HOLI_FFMPEG_ROOT` to that SDK
prefix when invoking the existing Waf build, and include `holi` in the target
list. The SDK must include the libvpx, libtheora, libvorbis and libogg headers
and static libraries. Keep the SDK outside the source tree and do not commit
its binaries. Other platforms require an appropriate static SDK and remain
subject to their own build and runtime verification.

FFmpeg and its codec libraries have their own licenses. Distributors must
provide applicable notices and satisfy the relevant source and relinking
requirements for their precise build configuration; a license text alone is
not sufficient for every static-link distribution.
