#ifndef HL2SB_IHOLIVIDEO_H
#define HL2SB_IHOLIVIDEO_H

// A C-compatible interface for the holi module. All functions are called by
// the client thread. The module owns the encoder and copies submitted data.
#define HL2SB_HOLI_VIDEO_INTERFACE "HL2SBHoliVideo001"
struct HL2SBVideoBackend;

struct HL2SBHoliVideoAPI
{
    HL2SBVideoBackend *(*Create)(const char *path, int width, int height,
        double fps, int bitrate, int sampleRate, bool audio, const char **error);
    bool (*AppendVideoFrame)(HL2SBVideoBackend *backend,
        const unsigned char *bgra, int stride, double duration);
    bool (*AppendAudio)(HL2SBVideoBackend *backend, const short *stereo, int frames);
    bool (*SyncAudio)(HL2SBVideoBackend *backend);
    bool (*Finish)(HL2SBVideoBackend *backend);
    const char *(*Error)(const HL2SBVideoBackend *backend);
    void (*Destroy)(HL2SBVideoBackend *backend);
};

#endif
