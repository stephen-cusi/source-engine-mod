#include "hl2sb_video_backend.h"

#if defined(HL2SB_VIDEO_FFMPEG)
extern "C" {
#include <libavcodec/avcodec.h>
#include <libavformat/avformat.h>
#include <libavutil/audio_fifo.h>
#include <libavutil/channel_layout.h>
#include <libavutil/error.h>
#include <libavutil/opt.h>
#include <libswresample/swresample.h>
#include <libswscale/swscale.h>
}
#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <new>

struct HL2SBVideoBackend
{
    AVFormatContext *format = NULL;
    AVCodecContext *video = NULL;
    AVCodecContext *audio = NULL;
    AVStream *videoStream = NULL;
    AVStream *audioStream = NULL;
    AVFrame *videoFrame = NULL;
    AVAudioFifo *fifo = NULL;
    SwrContext *resampler = NULL;
    SwsContext *scaler = NULL;
    int width = 0, height = 0, sampleRate = 0;
    int64_t audioPts = 0;
    int64_t videoFrames = 0;
    double fps = 0;
    double nextVideoTime = 0;
    bool header = false, finished = false, failed = false;
    char error[256] = {};
};

static bool VideoError(HL2SBVideoBackend *b, const char *context, int code)
{
    char detail[AV_ERROR_MAX_STRING_SIZE] = {};
    if (code < 0) av_strerror(code, detail, sizeof(detail));
    snprintf(b->error, sizeof(b->error), "%s%s%s", context, code < 0 ? ": " : "", detail);
    b->failed = true;
    return false;
}

static bool WritePackets(HL2SBVideoBackend *b, AVCodecContext *codec, AVStream *stream)
{
    AVPacket *packet = av_packet_alloc();
    if (!packet) return VideoError(b, "Could not allocate packet", AVERROR(ENOMEM));
    int ret;
    while ((ret = avcodec_receive_packet(codec, packet)) >= 0)
    {
        av_packet_rescale_ts(packet, codec->time_base, stream->time_base);
        packet->stream_index = stream->index;
        ret = av_interleaved_write_frame(b->format, packet);
        av_packet_unref(packet);
        if (ret < 0) { av_packet_free(&packet); return VideoError(b, "Could not write packet", ret); }
    }
    av_packet_free(&packet);
    if (ret != AVERROR(EAGAIN) && ret != AVERROR_EOF)
        return VideoError(b, "Could not receive encoded packet", ret);
    return true;
}

static bool EncodeFrame(HL2SBVideoBackend *b, AVCodecContext *codec, AVStream *stream, AVFrame *frame)
{
    int ret = avcodec_send_frame(codec, frame);
    if (ret < 0) return VideoError(b, "Could not send frame", ret);
    return WritePackets(b, codec, stream);
}

static bool EncodeAudio(HL2SBVideoBackend *b, bool final)
{
    const int frameSize = b->audio->frame_size > 0 ? b->audio->frame_size : 1024;
    while (av_audio_fifo_size(b->fifo) >= frameSize || (final && av_audio_fifo_size(b->fifo) > 0))
    {
        const int available = av_audio_fifo_size(b->fifo);
        const int samples = std::min(available, frameSize);
        AVFrame *frame = av_frame_alloc();
        if (!frame) return VideoError(b, "Could not allocate audio frame", AVERROR(ENOMEM));
        frame->nb_samples = samples;
        frame->format = b->audio->sample_fmt;
        frame->sample_rate = b->audio->sample_rate;
        int ret = av_channel_layout_copy(&frame->ch_layout, &b->audio->ch_layout);
        if (ret >= 0) ret = av_frame_get_buffer(frame, 0);
        if (ret < 0) { av_frame_free(&frame); return VideoError(b, "Could not allocate audio buffer", ret); }
        ret = av_audio_fifo_read(b->fifo, reinterpret_cast<void **>(frame->data), samples);
        if (ret != samples) { av_frame_free(&frame); return VideoError(b, "Could not read audio samples", AVERROR(EIO)); }
        frame->pts = b->audioPts;
        b->audioPts += samples;
        bool ok = EncodeFrame(b, b->audio, b->audioStream, frame);
        av_frame_free(&frame);
        if (!ok) return false;
    }
    return true;
}

HL2SBVideoBackend *HL2SBVideoBackend_Create(const char *path, int width, int height,
    double fps, int bitrate, int sampleRate, bool audio, const char **error)
{
    static const char *generic = "Could not create video encoder";
    if (error) *error = generic;
    if (!path || !path[0] || width < 2 || height < 2 || width > 8192 || height > 8192 ||
        (width & 1) || (height & 1) || !std::isfinite(fps) || fps < 1 || fps > 240 ||
        bitrate < 1 || (audio && (sampleRate < 8000 || sampleRate > 192000)))
    {
        if (error) *error = "Invalid video or audio settings";
        return NULL;
    }
    HL2SBVideoBackend *b = new(std::nothrow) HL2SBVideoBackend;
    if (!b) return NULL;
    b->width = width; b->height = height; b->sampleRate = sampleRate; b->fps = fps;
    int ret = avformat_alloc_output_context2(&b->format, NULL, NULL, path);
    if (ret < 0 || !b->format) goto fail;
    {
        const bool webm = strstr(b->format->oformat->name, "webm") != NULL;
        const bool ogg = strcmp(b->format->oformat->name, "ogg") == 0;
        if (!webm && !ogg) { if (error) *error = "Only WebM and Ogg are supported"; goto fail; }
        const AVCodec *videoCodec = avcodec_find_encoder_by_name(webm ? "libvpx" : "libtheora");
        const AVCodec *audioCodec = audio ? avcodec_find_encoder_by_name("libvorbis") : NULL;
        if (!videoCodec || (audio && !audioCodec)) { if (error) *error = "Required video or Vorbis encoder is unavailable"; goto fail; }
        b->videoStream = avformat_new_stream(b->format, NULL);
        b->video = avcodec_alloc_context3(videoCodec);
        if (!b->videoStream || !b->video) goto fail;
        b->video->width = width;
        b->video->height = height;
        b->video->pix_fmt = AV_PIX_FMT_YUV420P;
        b->video->time_base = av_inv_q(av_d2q(fps, 100000));
        b->video->framerate = av_d2q(fps, 100000);
        b->video->bit_rate = bitrate;
        b->video->gop_size = std::max(1, (int)(fps * 2));
        if (b->format->oformat->flags & AVFMT_GLOBALHEADER) b->video->flags |= AV_CODEC_FLAG_GLOBAL_HEADER;
        if (webm) { av_opt_set(b->video->priv_data, "deadline", "realtime", 0); av_opt_set(b->video->priv_data, "cpu-used", "5", 0); }
        if ((ret = avcodec_open2(b->video, videoCodec, NULL)) < 0) goto fail;
        b->videoStream->time_base = b->video->time_base;
        if ((ret = avcodec_parameters_from_context(b->videoStream->codecpar, b->video)) < 0) goto fail;
        b->videoFrame = av_frame_alloc();
        if (!b->videoFrame) goto fail;
        b->videoFrame->format = b->video->pix_fmt; b->videoFrame->width = width; b->videoFrame->height = height;
        if ((ret = av_frame_get_buffer(b->videoFrame, 32)) < 0) goto fail;
        b->scaler = sws_getContext(width, height, AV_PIX_FMT_BGRA, width, height, AV_PIX_FMT_YUV420P, SWS_BILINEAR, NULL, NULL, NULL);
        if (!b->scaler) goto fail;
        if (audio)
        {
            b->audioStream = avformat_new_stream(b->format, NULL);
            b->audio = avcodec_alloc_context3(audioCodec);
            if (!b->audioStream || !b->audio) goto fail;
            b->audio->sample_rate = sampleRate;
            b->audio->sample_fmt = AV_SAMPLE_FMT_FLTP;
            b->audio->time_base = AVRational{1, sampleRate};
            b->audio->bit_rate = 128000;
            av_channel_layout_default(&b->audio->ch_layout, 2);
            if (b->format->oformat->flags & AVFMT_GLOBALHEADER) b->audio->flags |= AV_CODEC_FLAG_GLOBAL_HEADER;
            if ((ret = avcodec_open2(b->audio, audioCodec, NULL)) < 0) goto fail;
            b->audioStream->time_base = b->audio->time_base;
            if ((ret = avcodec_parameters_from_context(b->audioStream->codecpar, b->audio)) < 0) goto fail;
            AVChannelLayout inputLayout = AV_CHANNEL_LAYOUT_STEREO;
            ret = swr_alloc_set_opts2(&b->resampler, &b->audio->ch_layout, b->audio->sample_fmt, sampleRate,
                &inputLayout, AV_SAMPLE_FMT_S16, sampleRate, 0, NULL);
            if (ret < 0 || (ret = swr_init(b->resampler)) < 0) goto fail;
            b->fifo = av_audio_fifo_alloc(b->audio->sample_fmt, 2, 1024);
            if (!b->fifo) goto fail;
        }
        if (!(b->format->oformat->flags & AVFMT_NOFILE) && (ret = avio_open(&b->format->pb, path, AVIO_FLAG_WRITE)) < 0) goto fail;
        if ((ret = avformat_write_header(b->format, NULL)) < 0) goto fail;
        b->header = true;
        if (error) *error = NULL;
        return b;
    }
fail:
    HL2SBVideoBackend_Destroy(b);
    return NULL;
}

bool HL2SBVideoBackend_AppendVideoFrame(HL2SBVideoBackend *b, const unsigned char *bgra, int stride, double duration)
{
    if (!b || b->failed || b->finished || !bgra || stride < b->width * 4 ||
        !std::isfinite(duration) || duration <= 0 || duration > 60) return false;
    const double nextTime = b->nextVideoTime + duration;
    const int64_t targetFrames = static_cast<int64_t>(floor(nextTime * b->fps + 1e-6));
    b->nextVideoTime = nextTime;
    if (targetFrames <= b->videoFrames) return true;
    int ret = av_frame_make_writable(b->videoFrame);
    if (ret < 0) return VideoError(b, "Could not prepare video frame", ret);
    const uint8_t *data[] = {bgra};
    const int lines[] = {stride};
    if (sws_scale(b->scaler, data, lines, 0, b->height, b->videoFrame->data, b->videoFrame->linesize) != b->height)
        return VideoError(b, "Could not convert video frame", 0);
    while (b->videoFrames < targetFrames)
    {
        b->videoFrame->pts = b->videoFrames++;
        if (!EncodeFrame(b, b->video, b->videoStream, b->videoFrame)) return false;
    }
    return true;
}

bool HL2SBVideoBackend_AppendAudio(HL2SBVideoBackend *b, const short *stereo, int frames)
{
    if (!b || b->failed || b->finished || !b->audio || !stereo || frames <= 0 || frames > b->sampleRate * 2) return false;
    AVFrame *converted = av_frame_alloc();
    if (!converted) return VideoError(b, "Could not allocate audio conversion", AVERROR(ENOMEM));
    converted->nb_samples = swr_get_out_samples(b->resampler, frames);
    converted->format = b->audio->sample_fmt;
    int ret = av_channel_layout_copy(&converted->ch_layout, &b->audio->ch_layout);
    if (ret >= 0) ret = av_frame_get_buffer(converted, 0);
    if (ret < 0) { av_frame_free(&converted); return VideoError(b, "Could not allocate converted audio", ret); }
    const uint8_t *input[] = {reinterpret_cast<const uint8_t *>(stereo)};
    ret = swr_convert(b->resampler, converted->data, converted->nb_samples, input, frames);
    if (ret < 0) { av_frame_free(&converted); return VideoError(b, "Could not convert audio", ret); }
    int written = av_audio_fifo_write(b->fifo, reinterpret_cast<void **>(converted->data), ret);
    av_frame_free(&converted);
    if (written != ret) return VideoError(b, "Could not queue audio", AVERROR(ENOMEM));
    return EncodeAudio(b, false);
}

bool HL2SBVideoBackend_SyncAudio(HL2SBVideoBackend *b)
{
    if (!b || !b->audio || b->failed || b->finished) return false;
    const int64_t target = static_cast<int64_t>(b->nextVideoTime * b->sampleRate);
    short silence[4096] = {};
    while (b->audioPts + av_audio_fifo_size(b->fifo) < target)
    {
        const int64_t remaining = target - b->audioPts - av_audio_fifo_size(b->fifo);
        const int frames = static_cast<int>(std::min<int64_t>(remaining, 2048));
        if (!HL2SBVideoBackend_AppendAudio(b, silence, frames)) return false;
    }
    return true;
}

bool HL2SBVideoBackend_Finish(HL2SBVideoBackend *b)
{
    if (!b || b->finished || b->failed) return false;
    if (b->audio && (!EncodeAudio(b, true) || !EncodeFrame(b, b->audio, b->audioStream, NULL))) return false;
    if (!EncodeFrame(b, b->video, b->videoStream, NULL)) return false;
    int ret = av_write_trailer(b->format);
    if (ret < 0) return VideoError(b, "Could not finalize video", ret);
    b->finished = true;
    return true;
}

const char *HL2SBVideoBackend_Error(const HL2SBVideoBackend *b)
{
    return b && b->error[0] ? b->error : "Video recording failed";
}

void HL2SBVideoBackend_Destroy(HL2SBVideoBackend *b)
{
    if (!b) return;
    av_audio_fifo_free(b->fifo);
    swr_free(&b->resampler);
    sws_freeContext(b->scaler);
    av_frame_free(&b->videoFrame);
    avcodec_free_context(&b->audio);
    avcodec_free_context(&b->video);
    if (b->format && !(b->format->oformat->flags & AVFMT_NOFILE) && b->format->pb)
        avio_closep(&b->format->pb);
    avformat_free_context(b->format);
    delete b;
}
#else
HL2SBVideoBackend *HL2SBVideoBackend_Create(const char *, int, int, double, int, int, bool, const char **error)
{
    if (error) *error = "Video encoder libraries are not available in this build";
    return nullptr;
}
bool HL2SBVideoBackend_AppendVideoFrame(HL2SBVideoBackend *, const unsigned char *, int, double) { return false; }
bool HL2SBVideoBackend_AppendAudio(HL2SBVideoBackend *, const short *, int) { return false; }
bool HL2SBVideoBackend_SyncAudio(HL2SBVideoBackend *) { return false; }
bool HL2SBVideoBackend_Finish(HL2SBVideoBackend *) { return false; }
const char *HL2SBVideoBackend_Error(const HL2SBVideoBackend *) { return "Video encoder libraries are not available in this build"; }
void HL2SBVideoBackend_Destroy(HL2SBVideoBackend *) {}
#endif
