// Hand-over of 2048-byte CD sectors between the audio side (the CD target, which must never allocate, lock or
// block) and whoever owns the image (another thread in the app, or the test/tool loop between frames).
//
// Audio side: ask() replaces any outstanding ask; answered() says whether the reply to the CURRENT ask is in;
// data() is valid only after answered() returned true, and the caller copies the sector out before the next ask().
// Reader side: pending() reads the current ask, reply() answers it.
//
// Why it is safe without a lock:
//  - The ask is a seqlock: askSeq_ is odd while ask() writes medium/lba, so pending() never sees a torn pair.
//  - The reader writes buf_ only for an ask that is still current and not yet answered, then publishes the
//    ask's sequence number with release. The audio side reads buf_ only after it sees (acquire) its own current
//    sequence published, and the reader never writes buf_ again for that sequence (a second reply is ignored),
//    so buf_ is never written while it may be read. A reply whose ask was replaced meanwhile publishes the old
//    sequence, which answered() does not match: a stale reply is never served.
//  - One reader at a time (a thread, or the test loop) is assumed.
#pragma once
#include <atomic>
#include <cstdint>
#include <cstring>

namespace ens::ts {

class SectorPort {
public:
    static constexpr int kBytes = 2048;

    // audio side
    void ask(uint32_t medium, uint32_t lba) {
        uint32_t s = askSeq_.load(std::memory_order_relaxed);
        askSeq_.store(s + 1, std::memory_order_relaxed);             // odd: writing
        std::atomic_thread_fence(std::memory_order_release);
        medium_.store(medium, std::memory_order_relaxed);
        lba_.store(lba, std::memory_order_relaxed);
        askSeq_.store(s + 2, std::memory_order_release);             // even, never 0 after the first ask
    }
    bool answered(uint32_t medium, uint32_t lba, bool& ok) const {
        uint32_t s = askSeq_.load(std::memory_order_relaxed);
        if (medium_.load(std::memory_order_relaxed) != medium || lba_.load(std::memory_order_relaxed) != lba)
            return false;
        if (answerSeq_.load(std::memory_order_acquire) != s) return false;
        ok = ok_.load(std::memory_order_relaxed);
        return true;
    }
    const uint8_t* data() const { return buf_; }
    uint32_t asks() const { return askSeq_.load(std::memory_order_relaxed) >> 1; }  // asks made so far

    // reader side
    bool pending(uint32_t& medium, uint32_t& lba) const {
        uint32_t s1 = askSeq_.load(std::memory_order_acquire);
        if (s1 == 0 || (s1 & 1)) return false;
        uint32_t m = medium_.load(std::memory_order_relaxed), l = lba_.load(std::memory_order_relaxed);
        std::atomic_thread_fence(std::memory_order_acquire);
        if (askSeq_.load(std::memory_order_relaxed) != s1) return false;
        if (answerSeq_.load(std::memory_order_relaxed) == s1) return false;
        medium = m;
        lba = l;
        return true;
    }
    void reply(uint32_t medium, uint32_t lba, const uint8_t* data, bool ok) {
        uint32_t s = askSeq_.load(std::memory_order_acquire);
        if (s == 0 || (s & 1) || answerSeq_.load(std::memory_order_relaxed) == s) return;
        if (medium_.load(std::memory_order_relaxed) != medium || lba_.load(std::memory_order_relaxed) != lba) return;
        if (ok && data) std::memcpy(buf_, data, kBytes);
        ok_.store(ok, std::memory_order_relaxed);
        answerSeq_.store(s, std::memory_order_release);
    }

private:
    std::atomic<uint32_t> askSeq_{0};
    std::atomic<uint32_t> medium_{0}, lba_{0};
    std::atomic<uint32_t> answerSeq_{0xFFFFFFFFu};  // odd, so it never equals a valid (even) ask sequence
    std::atomic<bool> ok_{false};
    uint8_t buf_[kBytes] = {};
};

}  // namespace ens::ts
