#include "cbase.h"
#include "filesystem.h"
#include "luamanager.h"
#include "luasrclib.h"
#include "materialsystem/imaterialsystem.h"
#include "engine/ifinalmixaudiocapture.h"
#include "video/iholivideo.h"
#include "tier1/utlvector.h"

#include "tier0/memdbgon.h"

static const char *kVideoWriterType = "HL2SB.IVideoWriter";
static IFinalMixAudioCapture *s_FinalMixCapture = NULL;
static CSysModule *s_HoliModule = NULL;
static HL2SBHoliVideoAPI *s_HoliVideo = NULL;

struct LuaVideoWriter
{
    HL2SBVideoBackend *backend;
    int width;
    int height;
    bool recordingSound;
    bool failed;
    bool lockFps;
    float previousFpsLimit;
    float recordingFpsLimit;
    double framesPerSecond;
    double elapsedVideoTime;
    double pendingVideoTime;
    double submittedVideoFrames;
    CUtlVector<unsigned char> *pixels;
};

static LuaVideoWriter *s_SoundWriter = NULL;
static LuaVideoWriter *s_LockFpsWriter = NULL;
static CUtlVector<LuaVideoWriter *> s_VideoWriters;

void HL2SB_VideoConnect(CreateInterfaceFn factory)
{
    s_FinalMixCapture = factory ? static_cast<IFinalMixAudioCapture *>(
        factory(HL2SB_FINAL_MIX_CAPTURE_VERSION, NULL)) : NULL;
}

static void LuaVideoDrainAudio(LuaVideoWriter *writer)
{
    if (!s_FinalMixCapture || s_SoundWriter != writer)
        return;
    short samples[4096];
    for (;;)
    {
        const int frames = s_FinalMixCapture->Read(samples, ARRAYSIZE(samples) / 2);
        if (frames <= 0)
            break;
        if (!s_HoliVideo->AppendAudio(writer->backend, samples, frames))
        {
            writer->failed = true;
            break;
        }
    }
}

static bool LuaVideoFinish(LuaVideoWriter *writer)
{
    if (!writer->backend)
        return !writer->failed;
    if (s_SoundWriter == writer && s_FinalMixCapture)
    {
        s_FinalMixCapture->Stop();
        LuaVideoDrainAudio(writer);
        const unsigned int dropped = s_FinalMixCapture->DroppedFrames();
        if (dropped)
            Warning("video.Record: dropped %u audio frames\n", dropped);
        s_SoundWriter = NULL;
    }
    writer->recordingSound = false;
    if (s_LockFpsWriter == writer)
    {
        ConVarRef fpsLimit("fps_max");
        if (fpsLimit.IsValid() && fpsLimit.GetFloat() == writer->recordingFpsLimit)
            fpsLimit.SetValue(writer->previousFpsLimit);
        s_LockFpsWriter = NULL;
        writer->lockFps = false;
    }
    const bool success = s_HoliVideo->Finish(writer->backend);
    writer->failed = writer->failed || !success;
    s_HoliVideo->Destroy(writer->backend);
    writer->backend = NULL;
    delete writer->pixels;
    writer->pixels = NULL;
    return !writer->failed;
}

void HL2SB_VideoDisconnect()
{
    for (int i = 0; i < s_VideoWriters.Count(); ++i)
        LuaVideoFinish(s_VideoWriters[i]);
    s_FinalMixCapture = NULL;
    s_HoliVideo = NULL;
    if (s_HoliModule)
        Sys_UnloadModule(s_HoliModule);
    s_HoliModule = NULL;
}

static LuaVideoWriter *CheckVideoWriter(lua_State *L)
{
    return static_cast<LuaVideoWriter *>(luaL_checkudata(L, 1, kVideoWriterType));
}

static int LuaVideoWriterFinish(lua_State *L)
{
    lua_pushboolean(L, LuaVideoFinish(CheckVideoWriter(L)));
    return 1;
}

static int LuaVideoWriterGC(lua_State *L)
{
    LuaVideoWriter *writer = CheckVideoWriter(L);
    LuaVideoFinish(writer);
    s_VideoWriters.FindAndRemove(writer);
    return 0;
}

static int LuaVideoWriterIsValid(lua_State *L)
{
    lua_pushboolean(L, CheckVideoWriter(L)->backend != NULL);
    return 1;
}

static int LuaVideoWriterWidth(lua_State *L)
{
    lua_pushinteger(L, CheckVideoWriter(L)->width);
    return 1;
}

static int LuaVideoWriterHeight(lua_State *L)
{
    lua_pushinteger(L, CheckVideoWriter(L)->height);
    return 1;
}

static int LuaVideoWriterSetRecordSound(lua_State *L)
{
    LuaVideoWriter *writer = CheckVideoWriter(L);
    luaL_checktype(L, 2, LUA_TBOOLEAN);
    if (!writer->backend)
        return luaL_error(L, "video writer has been finished");
    const bool enabled = lua_toboolean(L, 2) != 0;
    if (enabled == writer->recordingSound)
        return 0;
    if (enabled)
    {
        if (!s_FinalMixCapture || s_SoundWriter)
            return luaL_error(L, "audio recording is unavailable");
        if (!s_HoliVideo->SyncAudio(writer->backend))
        {
            writer->failed = true;
            return luaL_error(L, "video audio synchronization failed: %s", s_HoliVideo->Error(writer->backend));
        }
        if (!s_FinalMixCapture->Start())
            return luaL_error(L, "audio recording is unavailable");
        s_SoundWriter = writer;
        writer->recordingSound = true;
    }
    else
    {
        s_FinalMixCapture->Stop();
        LuaVideoDrainAudio(writer);
        s_SoundWriter = NULL;
        writer->recordingSound = false;
    }
    return 0;
}

static int LuaVideoWriterAddFrame(lua_State *L)
{
    LuaVideoWriter *writer = CheckVideoWriter(L);
    const double frameTime = luaL_checknumber(L, 2);
    const bool downsample = lua_toboolean(L, 3) != 0;
    if (!writer->backend)
        return luaL_error(L, "video writer has been finished");
    if (!(frameTime > 0.0 && frameTime <= 10.0))
        return luaL_error(L, "invalid video frame time");

    LuaVideoDrainAudio(writer);
    if (writer->failed)
        return luaL_error(L, "video audio encoding failed: %s", s_HoliVideo->Error(writer->backend));
    writer->elapsedVideoTime += frameTime;
    writer->pendingVideoTime += frameTime;
    const double dueFrames = floor(writer->elapsedVideoTime * writer->framesPerSecond + 1e-6);
    if (dueFrames <= writer->submittedVideoFrames)
        return 0;

    CMatRenderContextPtr context(materials);
    int targetWidth = 0, targetHeight = 0;
    context->GetRenderTargetDimensions(targetWidth, targetHeight);
    if (targetWidth <= 0 || targetHeight <= 0 ||
        (!downsample && (writer->width > targetWidth || writer->height > targetHeight)))
        return luaL_error(L, "video frame exceeds the current render target");
    CUtlVector<unsigned char> &pixels = *writer->pixels;
    Q_memset(pixels.Base(), 0, pixels.Count());
    if (downsample)
    {
        Rect_t source = { 0, 0, targetWidth, targetHeight };
        Rect_t destination = { 0, 0, writer->width, writer->height };
        context->ReadPixelsAndStretch(&source, &destination, pixels.Base(), IMAGE_FORMAT_BGRA8888, writer->width * 4);
    }
    else
        context->ReadPixels(0, 0, writer->width, writer->height, pixels.Base(), IMAGE_FORMAT_BGRA8888);

    if (!s_HoliVideo->AppendVideoFrame(writer->backend, pixels.Base(), writer->width * 4, writer->pendingVideoTime))
    {
        writer->failed = true;
        return luaL_error(L, "video frame encoding failed: %s", s_HoliVideo->Error(writer->backend));
    }
    writer->submittedVideoFrames = dueFrames;
    writer->pendingVideoTime = 0.0;
    return 0;
}

static int LuaVideoRecord(lua_State *L)
{
    luaL_checktype(L, 1, LUA_TTABLE);
    lua_getfield(L, 1, "name");
    const char *name = luaL_checkstring(L, -1);
    if (!*name || Q_strstr(name, "..") || strpbrk(name, "/\\:*?\"<>|") || Q_strlen(name) > 180)
        return luaL_error(L, "invalid video filename");
    char fileName[256];
    Q_snprintf(fileName, sizeof(fileName), "%s", name);
    lua_pop(L, 1);
    lua_getfield(L, 1, "container");
    const char *container = luaL_optstring(L, -1, "webm");
    const bool webm = Q_stricmp(container, "webm") == 0;
    const bool ogg = Q_stricmp(container, "ogg") == 0;
    lua_pop(L, 1);
    if (!webm && !ogg)
    {
        lua_pushboolean(L, 0);
        lua_pushliteral(L, "unsupported video container");
        return 2;
    }
    lua_getfield(L, 1, "video");
    const bool videoSupported = Q_stricmp(luaL_optstring(L, -1, webm ? "vp8" : "theora"),
        webm ? "vp8" : "theora") == 0;
    lua_pop(L, 1);
    lua_getfield(L, 1, "audio");
    const bool vorbis = Q_stricmp(luaL_optstring(L, -1, "vorbis"), "vorbis") == 0;
    lua_pop(L, 1);
    if (!videoSupported || !vorbis)
    {
        lua_pushboolean(L, 0);
        lua_pushliteral(L, "unsupported video or audio codec");
        return 2;
    }
    lua_getfield(L, 1, "width"); const int width = luaL_optint(L, -1, 720); lua_pop(L, 1);
    lua_getfield(L, 1, "height"); const int height = luaL_optint(L, -1, 576); lua_pop(L, 1);
    lua_getfield(L, 1, "fps"); const double fps = luaL_optnumber(L, -1, 25); lua_pop(L, 1);
    lua_getfield(L, 1, "bitrate"); const int bitrate = luaL_optint(L, -1, 4096); lua_pop(L, 1);
    lua_getfield(L, 1, "lockfps"); const bool lockfps = lua_toboolean(L, -1) != 0; lua_pop(L, 1);
    if (width < 2 || height < 2 || width > 4096 || height > 4096 || (width & 1) || (height & 1) ||
        !(fps > 0 && fps <= 120) || bitrate < 1 || bitrate > 100000)
        return luaL_error(L, "invalid video dimensions, frame rate or bitrate");
    if (lockfps)
    {
        ConVarRef fpsLimit("fps_max");
        if (s_LockFpsWriter || !fpsLimit.IsValid())
        {
            lua_pushboolean(L, 0);
            lua_pushliteral(L, "frame rate lock is unavailable");
            return 2;
        }
    }

    g_pFullFileSystem->CreateDirHierarchy("videos", "DEFAULT_WRITE_PATH");
    char path[MAX_PATH];
    const int length = Q_snprintf(path, sizeof(path), "%s/videos/%s.%s",
        engine->GetGameDirectory(), fileName, webm ? "webm" : "ogg");
    if (length < 0 || length >= sizeof(path))
        return luaL_error(L, "video path is too long");
    if (!s_HoliVideo)
    {
        s_HoliModule = Sys_LoadModule("holi");
        CreateInterfaceFn factory = s_HoliModule ? Sys_GetFactory(s_HoliModule) : NULL;
        s_HoliVideo = factory ? static_cast<HL2SBHoliVideoAPI *>(
            factory(HL2SB_HOLI_VIDEO_INTERFACE, NULL)) : NULL;
        if (!s_HoliVideo || !s_HoliVideo->Create || !s_HoliVideo->AppendVideoFrame ||
            !s_HoliVideo->AppendAudio || !s_HoliVideo->SyncAudio || !s_HoliVideo->Finish ||
            !s_HoliVideo->Error || !s_HoliVideo->Destroy)
        {
            s_HoliVideo = NULL;
            if (s_HoliModule)
                Sys_UnloadModule(s_HoliModule);
            s_HoliModule = NULL;
            lua_pushboolean(L, 0);
            lua_pushliteral(L, "video encoder module is unavailable");
            return 2;
        }
    }
    const char *error = NULL;
    HL2SBVideoBackend *backend = s_HoliVideo->Create(path, width, height, fps, bitrate * 1000,
        s_FinalMixCapture ? s_FinalMixCapture->SampleRate() : 44100, true, &error);
    if (!backend)
    {
        lua_pushboolean(L, 0);
        lua_pushstring(L, error ? error : "video encoder unavailable");
        return 2;
    }
    LuaVideoWriter *writer = static_cast<LuaVideoWriter *>(lua_newuserdata(L, sizeof(LuaVideoWriter)));
    writer->backend = backend;
    writer->width = width;
    writer->height = height;
    writer->recordingSound = false;
    writer->failed = false;
    writer->lockFps = lockfps;
    writer->previousFpsLimit = 0;
    writer->recordingFpsLimit = static_cast<float>(fps);
    writer->framesPerSecond = fps;
    writer->elapsedVideoTime = 0.0;
    writer->pendingVideoTime = 0.0;
    writer->submittedVideoFrames = 0.0;
    writer->pixels = new CUtlVector<unsigned char>;
    writer->pixels->SetCount(width * height * 4);
    if (lockfps)
    {
        ConVarRef fpsLimit("fps_max");
        writer->previousFpsLimit = fpsLimit.GetFloat();
        fpsLimit.SetValue(writer->recordingFpsLimit);
        s_LockFpsWriter = writer;
    }
    s_VideoWriters.AddToTail(writer);
    luaL_getmetatable(L, kVideoWriterType);
    lua_setmetatable(L, -2);
    return 1;
}

void HL2SB_LuaOpenVideo(lua_State *L)
{
    if (luaL_newmetatable(L, kVideoWriterType))
    {
        lua_pushvalue(L, -1); lua_setfield(L, -2, "__index");
        lua_pushcfunction(L, LuaVideoWriterGC); lua_setfield(L, -2, "__gc");
        lua_pushcfunction(L, LuaVideoWriterFinish); lua_setfield(L, -2, "Finish");
        lua_pushcfunction(L, LuaVideoWriterIsValid); lua_setfield(L, -2, "IsValid");
        lua_pushcfunction(L, LuaVideoWriterWidth); lua_setfield(L, -2, "Width");
        lua_pushcfunction(L, LuaVideoWriterHeight); lua_setfield(L, -2, "Height");
        lua_pushcfunction(L, LuaVideoWriterAddFrame); lua_setfield(L, -2, "AddFrame");
        lua_pushcfunction(L, LuaVideoWriterSetRecordSound); lua_setfield(L, -2, "SetRecordSound");
    }
    lua_pop(L, 1);
    lua_newtable(L);
    lua_pushcfunction(L, LuaVideoRecord); lua_setfield(L, -2, "Record");
    lua_setglobal(L, "video");
}
