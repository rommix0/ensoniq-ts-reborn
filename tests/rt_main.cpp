// ens_rt_tests: the real-time checks. Global operator new and delete are replaced here, so this
// is its own executable; they count the allocations made between allocStart() and allocStop().
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <new>

#include "app/engine.h"
#include "check.h"
#include "ts_firmware.h"

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

// Real time: with a disk in, the OS reading it (Storage twice), another disk swapped in and the drive emptied
// between frames allocate nothing: insertDisk only moves pointers (T23).
TEST(rt_disk_in_the_drive_does_not_allocate) {
    auto m = loadTs();
    if (!m) {
        std::printf("  skip: ROMs not found\n");
        return;
    }
    auto hd = Disk::formatted(Density::High);
    m->insertDisk(Disk::formatted(Density::Double));
    runMs(*m, kBootMs);
    std::unique_ptr<Disk> out1, out2;
    allocStart();
    tap(*m, Button::Storage);
    tap(*m, Button::Storage);
    runMs(*m, 1500);
    out1 = m->insertDisk(std::move(hd));
    runMs(*m, 500);
    out2 = m->insertDisk(nullptr);
    runMs(*m, 500);
    const size_t n = allocStop();
    std::printf("  allocations: %zu\n", n);
    CHECK_EQ(n, 0u);
    CHECK(out1 && out2);
}

// Real time: once booted, 2 s of engine blocks with what a host and the GUI do meanwhile (MIDI notes
// and a program change with offsets, button, key, analog and patch select posts, a requested sound, an
// analog from the host, snapshots, one RAM copy) allocates nothing.
TEST(rt_engine_render_does_not_allocate) {
    RomSet roms;
    if (!readTsRoms(roms)) {
        std::printf("  skip: ROMs not found\n");
        return;
    }
    std::string err;
    auto e = ens::app::Engine::create(roms, err);
    CHECK(e != nullptr);
    if (!e) return;
    constexpr int kBlock = 480;
    e->prepare(48000, kBlock);
    float ml[kBlock], mr[kBlock], al[kBlock], ar[kBlock];
    for (int b = 0; b < 540; ++b) e->render(ml, mr, al, ar, kBlock);  // 5.4 s: booted
    e->prepare(44100, 512);             // a device rate change, as prepareToPlay makes on a running machine
    e->render(ml, mr, al, ar, kBlock);  // the first block at the new rate
    using ens::app::Event;
    const uint8_t on[] = {0x90, 0x3C, 0x64}, off[] = {0x80, 0x3C, 0x40}, pc[] = {0xC0, 0x05};
    std::vector<uint8_t> ram(e->machine().ramImage().size());
    ens::app::Snapshot s;
    bool taken = false;
    uint16_t mod = 0;
    allocStart();
    for (int b = 0; b < 200; ++b) {
        if (b % 5 == 0) {
            e->midi(10, on, 3);
            e->midi(20, pc, 2);
            e->midi(400, off, 3);
            e->post({Event::Kind::Button, static_cast<uint8_t>(Button::Up), b % 10 == 0, 0});
            e->post({Event::Kind::Key, 60, b % 10 == 0, 100});
            e->post({Event::Kind::Analog, static_cast<uint8_t>(Analog::ModWheel), false, mod});
            e->post({Event::Kind::PatchSelect, 0, b % 10 == 0, 0});
            mod = static_cast<uint16_t>((mod + 0x40) & 0x3FF);
            e->setProgram(b % 10 == 0 ? 9 : 5);
            e->setAnalog(Analog::Volume, static_cast<uint16_t>(0x200 + b));
        }
        if (b == 50) e->requestRam();
        e->render(ml, mr, al, ar, kBlock);
        s = e->snapshot();
        if (b >= 50 && !taken) taken = e->takeRam(ram);
    }
    const size_t n = allocStop();
    std::printf("  allocations: %zu, frames %llu\n", n, static_cast<unsigned long long>(s.frames));
    CHECK(taken);
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
