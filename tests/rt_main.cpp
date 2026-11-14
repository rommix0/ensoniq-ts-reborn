// ens_rt_tests: the real-time checks. Global operator new and delete are replaced here, so this
// is its own executable; they count the allocations made between allocStart() and allocStop().
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <new>

#include "ts_firmware.h"
#include "check.h"

namespace {

bool counting = false;  // single-threaded: the tests run one at a time on the main thread
size_t allocations = 0;

void allocStart() {
    allocations = 0;
    counting = true;
}

size_t allocStop() {
    counting = false;
    return allocations;
}

}  // namespace

void* operator new(std::size_t n) {
    if (counting) ++allocations;
    if (void* p = std::malloc(n ? n : 1)) return p;
    throw std::bad_alloc();
}
void* operator new[](std::size_t n) { return operator new(n); }
void operator delete(void* p) noexcept { std::free(p); }
void operator delete[](void* p) noexcept { std::free(p); }
void operator delete(void* p, std::size_t) noexcept { std::free(p); }
void operator delete[](void* p, std::size_t) noexcept { std::free(p); }
void* operator new(std::size_t n, const std::nothrow_t&) noexcept {
    if (counting) ++allocations;
    return std::malloc(n ? n : 1);
}
void* operator new[](std::size_t n, const std::nothrow_t& t) noexcept { return operator new(n, t); }
void operator delete(void* p, const std::nothrow_t&) noexcept { std::free(p); }
void operator delete[](void* p, const std::nothrow_t&) noexcept { std::free(p); }

// The aligned forms (over-aligned types), counted the same way.
namespace {
void* alignedAlloc(std::size_t n, std::align_val_t a) noexcept {
    if (counting) ++allocations;
#ifdef _WIN32
    return _aligned_malloc(n ? n : 1, static_cast<std::size_t>(a));
#else
    const std::size_t al = static_cast<std::size_t>(a);
    return std::aligned_alloc(al, (n + al - 1) / al * al);
#endif
}
void alignedFree(void* p) noexcept {
#ifdef _WIN32
    _aligned_free(p);
#else
    std::free(p);
#endif
}
}  // namespace
void* operator new(std::size_t n, std::align_val_t a) {
    if (void* p = alignedAlloc(n, a)) return p;
    throw std::bad_alloc();
}
void* operator new[](std::size_t n, std::align_val_t a) { return operator new(n, a); }
void* operator new(std::size_t n, std::align_val_t a, const std::nothrow_t&) noexcept { return alignedAlloc(n, a); }
void* operator new[](std::size_t n, std::align_val_t a, const std::nothrow_t&) noexcept { return alignedAlloc(n, a); }
void operator delete(void* p, std::align_val_t) noexcept { alignedFree(p); }
void operator delete[](void* p, std::align_val_t) noexcept { alignedFree(p); }
void operator delete(void* p, std::size_t, std::align_val_t) noexcept { alignedFree(p); }
void operator delete[](void* p, std::size_t, std::align_val_t) noexcept { alignedFree(p); }
void operator delete(void* p, std::align_val_t, const std::nothrow_t&) noexcept { alignedFree(p); }
void operator delete[](void* p, std::align_val_t, const std::nothrow_t&) noexcept { alignedFree(p); }


using namespace ens::ts;

// The replacement is in use, so a count of 0 below means something.
TEST(rt_allocation_counter_counts) {
    allocStart();
    int* volatile p = new int(1);
    delete p;
    int* volatile q = new (std::nothrow) int(2);
    delete q;
    struct alignas(64) Wide {
        char c[64];  // no padding (C4324)
    };
    Wide* volatile w = new Wide{};
    delete w;
    CHECK_EQ(allocStop(), 3u);
}

// Real time: once booted, a second of runFrame with the calls a host and the panel make every 50 ms
// (MIDI in, a button tap, a key and its pressure, the mod wheel, the patch select buttons, MIDI out
// drained, the panel copied) allocates nothing.
TEST(rt_machine_runframe_does_not_allocate) {
    auto m = loadTs();
    if (!m) {
        std::printf("  skip: ROMs not found\n");
        return;
    }
    runMs(*m, kBootMs);
    const uint8_t note[] = {0x90, 0x3C, 0x64, 0x80, 0x3C, 0x00};  // middle C on and off
    PanelState panel;
    Audio audio;
    allocStart();
    for (uint32_t f = 0; f < kWavRate; ++f) {
        if (f % (kWavRate / 20) == 0) {
            for (const uint8_t b : note) m->midiIn(b);
            m->button(Button::Up, true);
            m->button(Button::Up, false);
            m->key(60, true, 64);
            m->pressure(60, 40);
            m->key(60, false, 64);
            m->setAnalog(Analog::ModWheel, 0x100);
            m->patchSelect(0, true);
            m->patchSelect(0, false);
            for (uint8_t b = 0; m->popMidiOut(b);) {
            }
            panel = m->panel();
        }
        m->runFrame(audio);
    }
    const size_t n = allocStop();
    std::printf("  allocations: %zu\n", n);
    CHECK_EQ(n, 0u);
}
// Usage: ens_rt_tests [substring]  -- runs only tests whose name contains the substring.
int main(int argc, char** argv) {
    const char* filter = argc > 1 ? argv[1] : nullptr;
    int passed = 0, failed = 0;
    for (const auto& c : ens_test::registry()) {
        if (filter && !std::strstr(c.name, filter)) continue;
        ens_test::failures() = 0;
        std::printf("[ RUN  ] %s\n", c.name);
        c.fn();
        if (ens_test::failures() == 0) {
            ++passed;
            std::printf("[  OK  ] %s\n", c.name);
        } else {
            ++failed;
            std::printf("[ FAIL ] %s\n", c.name);
        }
    }
    std::printf("%d passed, %d failed\n", passed, failed);
    return failed == 0 ? 0 : 1;
}
