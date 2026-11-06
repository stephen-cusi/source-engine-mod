#include "../servo_backend.h"
#include <cassert>
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
    assert(pixels[0] == 42);
    assert(HLSBServoNavigate(v, "https://example.com/next"));
    assert(HLSBServoPump(r)); // request a screenshot, then close before completion
    assert(HLSBServoDestroyView(v));
    assert(HLSBServoPump(r)); // simulate a late error callback after close
    assert(HLSBServoDestroyView(second));
    assert(HLSBServoDestroyRuntime(r));
    r = HLSBServoCreateRuntime(argv[1]);
    assert(r && HLSBServoDestroyRuntime(r));
}
