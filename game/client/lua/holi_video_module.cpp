#include "video/iholivideo.h"
#include "hl2sb_video_backend.h"

#include <condition_variable>
#include <cmath>
#include <cstring>
#include <deque>
#include <memory>
#include <mutex>
#include <new>
#include <thread>
#include <vector>

namespace {

enum class JobType { Video, Audio, Sync };
struct Job
{
    JobType type;
    std::shared_ptr<std::vector<unsigned char>> bytes;
    int stride;
    int frames;
    double duration;
};
struct AsyncWriter
{
    HL2SBVideoBackend *encoder = NULL;
    int width = 0;
    int height = 0;
    int sampleRate = 0;
    std::mutex mutex;
    std::condition_variable wake;
    std::deque<Job> queue;
    std::thread thread;
    std::shared_ptr<std::vector<unsigned char>> lastFrame;
    int queuedVideo = 0;
    int queuedAudio = 0;
    double pendingDuration = 0;
    bool closing = false;
    bool finished = false;
    bool failed = false;
    char error[256] = {};
};

static AsyncWriter *AsWriter(HL2SBVideoBackend *writer)
{
    return reinterpret_cast<AsyncWriter *>(writer);
}

static void RunEncoder(AsyncWriter *writer)
{
    for (;;)
    {
        Job job;
        {
            std::unique_lock<std::mutex> lock(writer->mutex);
            writer->wake.wait(lock, [writer] { return writer->failed || writer->closing || !writer->queue.empty(); });
            if (writer->failed)
            {
                writer->queue.clear();
                writer->queuedVideo = 0;
                writer->queuedAudio = 0;
                break;
            }
            if (writer->queue.empty()) break;
            job = std::move(writer->queue.front());
            writer->queue.pop_front();
        }
        bool ok = false;
        switch (job.type)
        {
        case JobType::Video:
            ok = HL2SBVideoBackend_AppendVideoFrame(writer->encoder, job.bytes->data(), job.stride, job.duration);
            break;
        case JobType::Audio:
            ok = HL2SBVideoBackend_AppendAudio(writer->encoder,
                reinterpret_cast<const short *>(job.bytes->data()), job.frames);
            break;
        case JobType::Sync:
            ok = HL2SBVideoBackend_SyncAudio(writer->encoder);
            break;
        }
        {
            std::lock_guard<std::mutex> lock(writer->mutex);
            if (job.type == JobType::Video) --writer->queuedVideo;
            if (job.type == JobType::Audio) writer->queuedAudio -= job.frames;
            if (!ok)
            {
                writer->failed = true;
                std::strncpy(writer->error, HL2SBVideoBackend_Error(writer->encoder), sizeof(writer->error) - 1);
                writer->queue.clear();
                writer->queuedVideo = 0;
                writer->queuedAudio = 0;
                break;
            }
        }
    }
    bool failed;
    {
        std::lock_guard<std::mutex> lock(writer->mutex);
        failed = writer->failed;
    }
    if (!failed && !HL2SBVideoBackend_Finish(writer->encoder))
    {
        std::lock_guard<std::mutex> lock(writer->mutex);
        writer->failed = true;
        std::strncpy(writer->error, HL2SBVideoBackend_Error(writer->encoder), sizeof(writer->error) - 1);
    }
}

static HL2SBVideoBackend *CreateWriter(const char *path, int width, int height,
    double fps, int bitrate, int sampleRate, bool audio, const char **error)
{
    HL2SBVideoBackend *encoder = HL2SBVideoBackend_Create(path, width, height, fps, bitrate, sampleRate, audio, error);
    if (!encoder) return NULL;
    AsyncWriter *writer = NULL;
    try { writer = new(std::nothrow) AsyncWriter; }
    catch (...) { writer = NULL; }
    if (!writer)
    {
        HL2SBVideoBackend_Destroy(encoder);
        if (error) *error = "Could not allocate video writer";
        return NULL;
    }
    writer->encoder = encoder;
    writer->width = width;
    writer->height = height;
    writer->sampleRate = sampleRate;
    try { writer->thread = std::thread(RunEncoder, writer); }
    catch (...)
    {
        HL2SBVideoBackend_Destroy(encoder);
        delete writer;
        if (error) *error = "Could not start video encoder thread";
        return NULL;
    }
    return reinterpret_cast<HL2SBVideoBackend *>(writer);
}

static bool AppendVideo(HL2SBVideoBackend *opaque, const unsigned char *bgra, int stride, double duration)
{
    AsyncWriter *writer = AsWriter(opaque);
    if (!writer || !bgra || stride < writer->width * 4 || !std::isfinite(duration) || duration <= 0 || duration > 60)
        return false;
    {
        std::lock_guard<std::mutex> lock(writer->mutex);
        if (writer->failed || writer->closing) return false;
        if (writer->queuedVideo >= 6)
        {
            if (writer->pendingDuration + duration > 2.0)
            {
                writer->failed = true;
                std::strncpy(writer->error, "Video encoder could not keep up", sizeof(writer->error) - 1);
                writer->wake.notify_one();
                return false;
            }
            writer->pendingDuration += duration;
            return true;
        }
    }
    const int rowSize = writer->width * 4;
    std::shared_ptr<std::vector<unsigned char>> pixels;
    try { pixels = std::make_shared<std::vector<unsigned char>>(static_cast<size_t>(rowSize) * writer->height); }
    catch (...) {
        std::lock_guard<std::mutex> lock(writer->mutex);
        writer->failed = true;
        std::strncpy(writer->error, "Could not allocate video frame", sizeof(writer->error) - 1);
        writer->wake.notify_one();
        return false;
    }
    for (int y = 0; y < writer->height; ++y)
        std::memcpy(pixels->data() + static_cast<size_t>(y) * rowSize,
            bgra + static_cast<size_t>(y) * stride, rowSize);
    std::lock_guard<std::mutex> lock(writer->mutex);
    if (writer->failed || writer->closing) return false;
    Job job = { JobType::Video, pixels, rowSize, 0, duration + writer->pendingDuration };
    writer->pendingDuration = 0;
    writer->lastFrame = pixels;
    writer->queue.push_back(std::move(job));
    ++writer->queuedVideo;
    writer->wake.notify_one();
    return true;
}

static bool AppendAudio(HL2SBVideoBackend *opaque, const short *stereo, int frames)
{
    AsyncWriter *writer = AsWriter(opaque);
    if (!writer || !stereo || frames <= 0 || frames > writer->sampleRate * 2) return false;
    std::lock_guard<std::mutex> lock(writer->mutex);
    if (writer->failed || writer->closing) return false;
    if (writer->queuedAudio > writer->sampleRate * 2 - frames || writer->queue.size() >= 1024)
    {
        writer->failed = true;
        std::strncpy(writer->error, "Video encoder audio queue overflow", sizeof(writer->error) - 1);
        writer->wake.notify_one();
        return false;
    }
    std::shared_ptr<std::vector<unsigned char>> bytes;
    try { bytes = std::make_shared<std::vector<unsigned char>>(static_cast<size_t>(frames) * 2 * sizeof(short)); }
    catch (...) {
        writer->failed = true;
        std::strncpy(writer->error, "Could not allocate audio frame", sizeof(writer->error) - 1);
        writer->wake.notify_one();
        return false;
    }
    std::memcpy(bytes->data(), stereo, bytes->size());
    Job job = { JobType::Audio, bytes, 0, frames, 0 };
    writer->queue.push_back(std::move(job));
    writer->queuedAudio += frames;
    writer->wake.notify_one();
    return true;
}

static bool SyncAudio(HL2SBVideoBackend *opaque)
{
    AsyncWriter *writer = AsWriter(opaque);
    if (!writer) return false;
    std::lock_guard<std::mutex> lock(writer->mutex);
    if (writer->failed || writer->closing || writer->queue.size() >= 1024) return false;
    if (writer->pendingDuration > 0 && writer->lastFrame)
    {
        Job video = { JobType::Video, writer->lastFrame, writer->width * 4, 0, writer->pendingDuration };
        writer->queue.push_back(std::move(video));
        ++writer->queuedVideo;
        writer->pendingDuration = 0;
    }
    Job job = { JobType::Sync, {}, 0, 0, 0 };
    writer->queue.push_back(std::move(job));
    writer->wake.notify_one();
    return true;
}

static bool FinishWriter(HL2SBVideoBackend *opaque)
{
    AsyncWriter *writer = AsWriter(opaque);
    if (!writer) return false;
    {
        std::lock_guard<std::mutex> lock(writer->mutex);
        if (writer->finished) return !writer->failed;
        if (!writer->failed && writer->pendingDuration > 0 && writer->lastFrame)
        {
            Job job = { JobType::Video, writer->lastFrame, writer->width * 4, 0, writer->pendingDuration };
            writer->queue.push_back(std::move(job));
            ++writer->queuedVideo;
            writer->pendingDuration = 0;
        }
        writer->closing = true;
        writer->wake.notify_one();
    }
    if (writer->thread.joinable()) writer->thread.join();
    writer->finished = true;
    return !writer->failed;
}

static const char *WriterError(const HL2SBVideoBackend *opaque)
{
    AsyncWriter *writer = AsWriter(const_cast<HL2SBVideoBackend *>(opaque));
    if (!writer) return "Invalid video writer";
    static thread_local char error[256];
    std::lock_guard<std::mutex> lock(writer->mutex);
    std::strncpy(error, writer->error[0] ? writer->error : "Video encoding failed", sizeof(error) - 1);
    error[sizeof(error) - 1] = 0;
    return error;
}

static void DestroyWriter(HL2SBVideoBackend *opaque)
{
    AsyncWriter *writer = AsWriter(opaque);
    if (!writer) return;
    FinishWriter(opaque);
    HL2SBVideoBackend_Destroy(writer->encoder);
    delete writer;
}

static void MarkQueueFailure(HL2SBVideoBackend *opaque)
{
    AsyncWriter *writer = AsWriter(opaque);
    if (!writer) return;
    std::lock_guard<std::mutex> lock(writer->mutex);
    writer->failed = true;
    std::strncpy(writer->error, "Could not allocate encoder queue", sizeof(writer->error) - 1);
    writer->wake.notify_one();
}

static bool SafeAppendVideo(HL2SBVideoBackend *writer, const unsigned char *pixels, int stride, double duration)
{
    try { return AppendVideo(writer, pixels, stride, duration); }
    catch (...) { MarkQueueFailure(writer); return false; }
}

static bool SafeAppendAudio(HL2SBVideoBackend *writer, const short *samples, int frames)
{
    try { return AppendAudio(writer, samples, frames); }
    catch (...) { MarkQueueFailure(writer); return false; }
}

static bool SafeSyncAudio(HL2SBVideoBackend *writer)
{
    try { return SyncAudio(writer); }
    catch (...) { MarkQueueFailure(writer); return false; }
}

static bool SafeFinish(HL2SBVideoBackend *writer)
{
    try { return FinishWriter(writer); }
    catch (...) { MarkQueueFailure(writer); return FinishWriter(writer); }
}

static void SafeDestroy(HL2SBVideoBackend *writer)
{
    SafeFinish(writer);
    DestroyWriter(writer);
}

static HL2SBHoliVideoAPI api = {
    CreateWriter, SafeAppendVideo, SafeAppendAudio, SafeSyncAudio, SafeFinish, WriterError, SafeDestroy
};

} // namespace

#if defined(_WIN32)
#define HOLI_EXPORT extern "C" __declspec(dllexport)
#else
#define HOLI_EXPORT extern "C" __attribute__((visibility("default")))
#endif

HOLI_EXPORT void *CreateInterface(const char *name, int *status)
{
    const bool found = name && std::strcmp(name, HL2SB_HOLI_VIDEO_INTERFACE) == 0;
    if (status) *status = found ? 0 : 1;
    return found ? &api : NULL;
}
