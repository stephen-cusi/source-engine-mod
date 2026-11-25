#ifndef HL2SB_VIDEO_BACKEND_H
#define HL2SB_VIDEO_BACKEND_H

// All functions are called on the client thread. The final mix capture is
// drained to that thread before submitting signed 16-bit interleaved stereo.
struct HL2SBVideoBackend;

// The output path must be an absolute, validated path. fps is frames/second,
// bitrate is bits/second. On failure, error points to a static message.
HL2SBVideoBackend *HL2SBVideoBackend_Create( const char *path, int width, int height,
    double fps, int bitrate, int sampleRate, bool audio, const char **error );
bool HL2SBVideoBackend_AppendVideoFrame( HL2SBVideoBackend *backend,
    const unsigned char *bgra, int stride, double duration );
bool HL2SBVideoBackend_AppendAudio( HL2SBVideoBackend *backend,
    const short *stereo, int frames );
// Fill a gap before resuming the audio capture timeline with silence.
bool HL2SBVideoBackend_SyncAudio( HL2SBVideoBackend *backend );
bool HL2SBVideoBackend_Finish( HL2SBVideoBackend *backend );
const char *HL2SBVideoBackend_Error( const HL2SBVideoBackend *backend );
void HL2SBVideoBackend_Destroy( HL2SBVideoBackend *backend );

#endif
