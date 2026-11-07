#include "../servo_backend.h"
#include <cassert>
#include <cstring>
#include <thread>
#include <vector>

int main(int argc, char **argv) {
    assert(argc == 2);
    assert(!HLSBServoCreateRuntime("/path/does/not/exist"));
    HLSBServoRuntime *r = HLSBServoCreateRuntime(argv[1]);
    assert(r);
    assert(!HLSBServoCreateRuntime(argv[1]));
    assert(!HLSBServoCreateView(r, 0, 2));
    HLSBServoView *v = HLSBServoCreateView(r, 2, 3);
    assert(v);
    HLSBServoView *second = HLSBServoCreateView(r, 1, 1);
    assert(second);
    assert(HLSBServoSetDocumentScript(v, "window.test=true;"));
    assert(!HLSBServoSetDocumentScript(v, "\xc0\x80"));
    assert(!HLSBServoNavigate(v, "\xc0\x80"));
    assert(HLSBServoNavigate(v, "https://example.com/"));
    bool wrongThread = true;
    std::thread other([&] { wrongThread = HLSBServoPump(r); });
    other.join();
    assert(!wrongThread);
    assert(HLSBServoPump(r));
    assert(HLSBServoResize(v, 3, 2));
    assert(HLSBServoPump(r)); // stale 2x3 screenshot is discarded
    assert(HLSBServoCopyFrame(second, nullptr, 0, nullptr, nullptr, nullptr) == 4);
    assert(HLSBServoCopyFrame(v, nullptr, 0, nullptr, nullptr, nullptr) == 0);
    assert(HLSBServoPump(r));
    uint32_t width = 0, height = 0;
    uint64_t sequence = 0;
    const size_t size = HLSBServoCopyFrame(v, nullptr, 0, &width, &height, &sequence);
    assert(size == 24 && width == 3 && height == 2 && sequence == 1);
    std::vector<uint8_t> pixels(size, 0);
    assert(HLSBServoCopyFrame(v, pixels.data(), size - 1, nullptr, nullptr, nullptr) == size);
    assert(pixels[0] == 0);
    assert(HLSBServoCopyFrame(v, pixels.data(), size, nullptr, nullptr, nullptr) == size);
    assert(pixels[0] == 255 && pixels[1] == 0 && pixels[2] == 0 && pixels[3] == 128);
    assert(pixels[4] == 0 && pixels[5] == 0 && pixels[6] == 0 && pixels[7] == 0);
    assert(HLSBServoNavigate(v, "asset://garrysmod/html/menu.html"));
    uint64_t assetId = 0;
    char assetUrl[8192];
    assert(HLSBServoPopAssetEvent(v, &assetId, assetUrl, sizeof(assetUrl)));
    assert(assetId == 42 && std::strcmp(assetUrl, "asset://garrysmod/html/menu.html") == 0);
    const uint8_t body[] = {0, 1, 255};
    assert(!HLSBServoAssetReply(v, assetId, 200, "image/png", nullptr, sizeof(body)));
    assert(HLSBServoAssetReply(v, assetId, 200, "image/png", body, sizeof(body)));
    assert(!HLSBServoAssetReply(v, assetId, 200, "image/png", body, sizeof(body)));
    assert(HLSBServoNavigate(v, "asset://mapimage/gm_flatgrass"));
    assert(HLSBServoPopAssetEvent(v, &assetId, assetUrl, sizeof(assetUrl)));
    assert(std::strcmp(assetUrl, "asset://mapimage/gm_flatgrass") == 0);
    assert(HLSBServoAssetReply(v, assetId, 404, "text/plain", nullptr, 0));
    assert(HLSBServoNavigate(v, "asset://garrysmod/html/old.html"));
    assert(HLSBServoNavigate(second, "asset://garrysmod/html/other.html"));
    assert(HLSBServoNavigate(v, "asset://garrysmod/html/new.html"));
    uint64_t otherId = 0;
    char otherUrl[8192];
    assert(HLSBServoPopAssetEvent(second, &otherId, otherUrl, sizeof(otherUrl)));
    assert(std::strcmp(otherUrl, "asset://garrysmod/html/other.html") == 0);
    assert(HLSBServoAssetReply(second, otherId, 404, "text/plain", nullptr, 0));
    assert(HLSBServoPopAssetEvent(v, &assetId, assetUrl, sizeof(assetUrl)));
    assert(std::strcmp(assetUrl, "asset://garrysmod/html/new.html") == 0);
    assert(HLSBServoAssetReply(v, assetId, 404, "text/plain", nullptr, 0));
    assert(!HLSBServoPopAssetEvent(v, &assetId, assetUrl, sizeof(assetUrl)));
    assert(HLSBServoNavigate(v, "https://example.com/next"));
    assert(HLSBServoPump(r)); // request a screenshot, then close before completion
    assert(HLSBServoDestroyView(v));
    assert(HLSBServoPump(r)); // simulate a late error callback after close
    assert(HLSBServoDestroyView(second));
    assert(HLSBServoDestroyRuntime(r));
    r = HLSBServoCreateRuntime(argv[1]);
    assert(r && HLSBServoDestroyRuntime(r));
}
