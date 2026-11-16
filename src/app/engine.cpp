#include "app/engine.h"

#include <algorithm>
#include <cmath>
#include <cstring>

namespace ens::app {

namespace {
// DECISION A104: booted once the engine has run kBootMs of emulated time since power-on, counted in
// whole frames of 512 CPU clocks (A100) as the tests' msFrames counts them.
constexpr uint64_t kBootFrames = static_cast<uint64_t>(ts::kBootMs) * ts::kCpuHz / 512'000;
}  // namespace

std::unique_ptr<Engine> Engine::create(const ts::RomSet& roms, std::string& err, bool scsi) {
    auto m = ts::Machine::create(roms, err, false, scsi);
    if (!m) return nullptr;
    return std::unique_ptr<Engine>(new Engine(std::move(m)));
}

Engine::Engine(std::unique_ptr<ts::Machine> m) : m_(std::move(m)), ram_(m_->ramImage().size()) {}

Engine::~Engine() {  // message thread, audio stopped: free what is still in a slot
    delete swapIn_.load();
    delete removed_.load();
    delete scratch_.load();
}

void Engine::prepare(double outRate, int maxBlock) {
    outRate_ = outRate;
    maxBlock_ = std::max(1, maxBlock);
    rs_ = std::make_unique<Resampler>(kFrameHz, outRate, static_cast<int>(kChannels));  // DECISION A100
    // The machine is fed one frame at a time, so out_ never holds more than a piece plus the
    // frames one input frame completes: reserved here, appending never reallocates.
    const auto perFrame = static_cast<size_t>(std::ceil(outRate / kFrameHz)) + 1;
    out_.clear();
    out_.reserve(kChannels * (static_cast<size_t>(maxBlock_) + perFrame));
}

bool Engine::loadRam(const std::vector<uint8_t>& img, std::string& err) {
    if (!m_->loadRamImage(img, err)) return false;
    frames_ = 0;  // a power cycle: the boot starts again
    inParse_ = outParse_ = MidiParse{};
    booted_ = false;
    autoLoad_ = answerAutoLoad_ ? AutoLoad::Watch : AutoLoad::Off;
    requested_ = current_ = -1;
    dirty_ = false;
    channel_ = 0;
    bankSeen_ = false;
    bankMsb_ = bankLsb_ = 0;
    selectLen_ = setSelLen_ = 0;
    setSelProgram_ = -1;
    notes_ = {};
    wantSet_ = {};
    // The machine keeps its analog inputs over a power cycle (loadRamImage); a boot must see the rest
    // values (A106).
    for (size_t i = 0; i < wantAnalog_.size(); ++i) m_->setAnalog(static_cast<ts::Analog>(i), ts::kAnalogRest[i]);
    return true;
}

// DECISION A105: the engine tracks the program and bank on MIDI out and MIDI in after the boot, and
// sends the requested sound into MIDI in once, after the boot, if the machine is not already on it.
bool Engine::MidiParse::feed(uint8_t b) {
    if (b >= 0xF8) return false;  // real-time: running status stays
    if (b >= 0x80) {
        n = 0;
        sysex = b == 0xF0;
        status = b >= 0xF0 ? 0 : b;  // system messages end running status
        return false;
    }
    if (sysex || status == 0) return false;
    d[n++] = b;
    if (n < ((status & 0xE0) == 0xC0 ? 1 : 2)) return false;
    n = 0;
    return true;
}

void Engine::track(const MidiParse& p, bool fromIn) {
    const uint8_t type = p.status & 0xF0;
    if (fromIn) {  // DECISION A107: the mask follows note on/off on any channel; all notes off clears it
        auto& w = notes_[p.d[0] >> 6];
        const uint64_t bit = uint64_t{1} << (p.d[0] & 63);
        if (type == 0x90 && p.d[1] != 0) w |= bit;
        else if (type == 0x80 || type == 0x90) w &= ~bit;
        else if (type == 0xB0 && (p.d[0] == 0x7B || p.d[0] == 0x78)) notes_ = {};
    }
    if (!booted_) return;
    if (type == 0xB0 && (p.d[0] == 0x00 || p.d[0] == 0x20)) {
        (p.d[0] == 0 ? bankMsb_ : bankLsb_) = p.d[1];
        bankSeen_ = true;
    } else if (type == 0xC0) {
        // A sound the host asked for is not a change to follow (KS-32 A18). DECISION A105: nor is the same
        // program again: the OS at times answers a MIDI-in program change, the engine's own included, by
        // sending that selection on MIDI out.
        if (!sending_ && p.d[0] != current_) ++programSeq_;
        current_ = p.d[0];
        channel_ = p.status & 0x0F;
        uint8_t n = 0;
        if (bankSeen_) {
            const uint8_t bank[] = {static_cast<uint8_t>(0xB0 | channel_), 0x00, bankMsb_, 0x20, bankLsb_};
            for (const uint8_t b : bank) select_[n++] = b;
        }
        select_[n++] = p.status;  // the status byte, also when the firmware sent the data alone
        select_[n++] = p.d[0];
        selectLen_ = n;
    }
}

// The bytes come from saved state, so only `Bn 00 xx 20 yy` (a bank select; each pair may repeat the
// `Bn`) and one final `Cn pp`, all on one channel and at most 8 bytes, are taken; anything else is
// ignored and a bare `Cn pp` is sent.
void Engine::setSelect(const uint8_t* bytes, int len) {
    setSelLen_ = 0;
    setSelProgram_ = -1;
    if (len < 2 || len > static_cast<int>(setSel_.size())) return;
    if ((bytes[0] & 0xF0) != 0xB0 && (bytes[0] & 0xF0) != 0xC0) return;  // it starts with its status
    const uint8_t ch = bytes[0] & 0x0F;
    int i = 0;
    while (len - i > 2) {
        if (bytes[i] == (0xB0 | ch)) ++i;
        if (len - i <= 2 || (bytes[i] != 0x00 && bytes[i] != 0x20) || bytes[i + 1] >= 0x80) return;
        i += 2;
    }
    if (len - i != 2 || bytes[i] != (0xC0 | ch) || bytes[i + 1] >= 0x80) return;
    for (int k = 0; k < len; ++k) setSel_[static_cast<size_t>(k)] = bytes[k];
    setSelLen_ = static_cast<uint8_t>(len);
    setSelProgram_ = bytes[len - 1];
}

void Engine::setProgram(int n) {
    n = std::clamp(n, 0, 127);
    if (n != requested_) {  // DECISION A105: only a changed request is sent; panel picks stay
        requested_ = n;
        dirty_ = true;
    }
}

// A value the host parameter followed from the snapshot is taken as the request without a send, so a
// stale follow never sends a sound back (as KS-32 A18).
void Engine::adoptProgram(int n) { requested_ = std::clamp(n, 0, 127); }

// DECISION A106: an analog set by the host is held and applied at a block start once booted, so the
// boot's own reads see the rest values (A104); only the inputs the host has set are touched.
void Engine::setAnalog(ts::Analog a, uint16_t raw) {
    const auto i = static_cast<size_t>(a);
    if (i >= wantAnalog_.size()) return;
    wantAnalog_[i] = raw & 0x3FF;
    wantSet_[i] = true;
}

void Engine::sendSelect(const uint8_t* b, int n) {
    MidiParse p;  // the bytes go into the machine and through the tracker, as if sent from outside
    sending_ = true;
    for (int i = 0; i < n; ++i) {
        m_->midiIn(b[i]);
        if (p.feed(b[i])) track(p, false);
    }
    sending_ = false;
}

void Engine::blockStart() {
    bool bootEnd = false;
    if (!booted_ && frames_ >= kBootFrames) {
        booted_ = bootEnd = true;
        current_ = 0;  // DECISION A105: OS v3.10 boots, cold or warm, to U0-0 (program 0, bank 0/0)
        dirty_ = true;
    }
    if (!booted_) return;
    if (dirty_) {
        // DECISION A105: at the boot's end the restored select goes out once when it selects the request,
        // even for program 0: the boot leaves U0-0, and R2-0 differs from it only in its bank.
        const bool restore = setSelLen_ > 0 && setSelProgram_ == requested_;
        if (requested_ >= 0 && (requested_ != current_ || (bootEnd && restore))) {
            if (restore) {
                sendSelect(setSel_.data(), setSelLen_);
            } else {
                const uint8_t bare[] = {static_cast<uint8_t>(0xC0 | channel_), static_cast<uint8_t>(requested_)};
                sendSelect(bare, 2);
            }
            current_ = requested_;
            ++programsSent_;
        }
        dirty_ = false;
    }
    for (size_t i = 0; i < wantAnalog_.size(); ++i) {
        const auto a = static_cast<ts::Analog>(i);
        if (wantSet_[i] && m_->analog(a) != wantAnalog_[i]) m_->setAnalog(a, wantAnalog_[i]);
    }
}

void Engine::setAnswerAutoLoad(bool on) {
    answerAutoLoad_ = on;
    autoLoad_ = on && !booted_ ? AutoLoad::Watch : AutoLoad::Off;
}

// DECISION A124: OS v3.10 asks "AUTO-LOAD ALL SAMPLED-SOUNDS  *YES*" / "*NO *" at power-on whenever its memory
// remembers a loaded sampled sound (Musician's Manual, "About the Auto-Load Feature"). Asked for, the engine
// answers it as a player would: Soft6 (under *NO *, row 1 columns 35-39) held 100 ms. Only during the boot
// (A104), so the same question after a SAMPLE-BANKS or 60-SEQ/SONGS load is left to the player.
void Engine::answerAutoLoad() {
    static constexpr char kAsk[] = "AUTO-LOAD ALL SAMPLED-SOUNDS";
    static constexpr uint64_t kHold = 100ull * ts::kCpuHz / 512'000;
    if (autoLoad_ == AutoLoad::Watch) {
        if (booted_) {
            autoLoad_ = AutoLoad::Done;
            return;
        }
        const auto& p = m_->panel();
        if (std::memcmp(p.text[0], kAsk, sizeof kAsk - 1) != 0 || std::memcmp(&p.text[1][35], "*NO *", 5) != 0 ||
            m_->panelRoom() < 2)
            return;
        m_->button(ts::Button::Soft6, true);
        autoLoadAt_ = frames_;
        autoLoad_ = AutoLoad::Held;
    } else if (autoLoad_ == AutoLoad::Held && frames_ - autoLoadAt_ >= kHold && m_->panelRoom() >= 2) {
        m_->button(ts::Button::Soft6, false);
        autoLoad_ = AutoLoad::Done;
    }
}

ts::Machine& Engine::machine() { return *m_; }

// DECISION A102: the queue holds 256 events; they are applied at the start of the next block, in
// post order.
bool Engine::post(const Event& e) { return events_.push(e); }

Snapshot Engine::snapshot() const {
    std::lock_guard<std::mutex> lock(snapMu_);
    return snap_;
}

void Engine::requestRam() { ramWanted_.store(true, std::memory_order_release); }

bool Engine::takeRam(std::vector<uint8_t>& out) {
    if (!ramReady_.load(std::memory_order_acquire)) return false;
    out.assign(ram_.begin(), ram_.end());
    ramReady_.store(false, std::memory_order_release);
    return true;
}

// DECISION A118: each slot has one writer at a time, handed over by a release store of its flag and read
// after an acquire load of it. The audio thread only moves pointers and copies into the caller's scratch
// (Disk::copyInto, 0.8 to 1.6 MB inside render, as A117's RAM copy).
bool Engine::insertDisk(std::unique_ptr<ts::Disk> d) {
    if (swapWanted_.load(std::memory_order_acquire)) return false;
    swapIn_.store(d.release(), std::memory_order_relaxed);
    swapWanted_.store(true, std::memory_order_release);
    return true;
}

std::unique_ptr<ts::Disk> Engine::takeRemoved() {
    if (!removedFull_.load(std::memory_order_acquire)) return nullptr;
    std::unique_ptr<ts::Disk> d(removed_.exchange(nullptr, std::memory_order_relaxed));
    removedFull_.store(false, std::memory_order_release);
    return d;
}

bool Engine::requestDisk(std::unique_ptr<ts::Disk> scratch) {
    if (!scratch || copyState_.load(std::memory_order_acquire) != 0) return false;
    scratch_.store(scratch.release(), std::memory_order_relaxed);
    copyState_.store(1, std::memory_order_release);
    return true;
}

bool Engine::takeDisk(std::unique_ptr<ts::Disk>& out, bool& ok, uint32_t& swapsAtCopy) {
    if (copyState_.load(std::memory_order_acquire) != 2) return false;
    ok = copyOk_.load(std::memory_order_relaxed);
    swapsAtCopy = copySwaps_.load(std::memory_order_relaxed);
    out.reset(scratch_.exchange(nullptr, std::memory_order_relaxed));
    copyState_.store(0, std::memory_order_release);
    return true;
}

void Engine::setWriteProtect(bool p) { protect_.store(p ? 1 : 0, std::memory_order_relaxed); }

std::unique_ptr<ts::Disk> Engine::releaseDisk() {
    if (swapWanted_.load(std::memory_order_acquire)) {
        swapWanted_.store(false, std::memory_order_relaxed);
        return std::unique_ptr<ts::Disk>(swapIn_.exchange(nullptr, std::memory_order_relaxed));
    }
    return m_->insertDisk(nullptr);
}

ts::SectorPort* Engine::cdPort() { return m_->cdPort(); }

bool Engine::insertCd(uint32_t medium, uint32_t sectors) {
    if (cdWanted_.load(std::memory_order_acquire)) return false;
    cdIn_.store(uint64_t{medium} << 32 | sectors, std::memory_order_relaxed);
    cdWanted_.store(true, std::memory_order_release);
    return true;
}

std::pair<uint32_t, uint32_t> Engine::releaseCd() {
    if (cdWanted_.load(std::memory_order_acquire)) {
        cdWanted_.store(false, std::memory_order_relaxed);
        const uint64_t v = cdIn_.load(std::memory_order_relaxed);
        return {uint32_t(v >> 32), uint32_t(v)};
    }
    const ts::CdTarget* cd = m_->cd();
    return cd && cd->hasDisc() ? std::pair<uint32_t, uint32_t>{cd->medium(), cd->sectors()}
                               : std::pair<uint32_t, uint32_t>{0, 0};
}

// DECISION A101: a byte at offset o enters before machine frame floor(o * kFrameHz / outRate) of the
// block; the store holds 1024 bytes a block, and the rest are dropped and counted.
// DECISION A108: every byte goes to MIDI IN unchanged, as the jack would carry it; the OS reads pressure itself.
void Engine::midi(int offset, const uint8_t* data, int size) {
    const auto at = offset > 0 ? static_cast<uint64_t>(offset * kFrameHz / outRate_) : 0;
    for (int i = 0; i < size; ++i) {
        if (midiCount_ == kMidiStore) {
            storeDropped_ += static_cast<uint32_t>(size - i);
            return;
        }
        midi_[midiCount_++] = {at, data[i]};
    }
}

void Engine::apply(const Event& e) {
    switch (e.kind) {
        case Event::Kind::Button:
            m_->button(static_cast<ts::Button>(e.id), e.down);
            break;
        case Event::Kind::Key:
            m_->key(e.id, e.down, std::min<uint16_t>(e.value, 127));
            break;
        case Event::Kind::Analog:
            if (e.id < ts::kAnalogCount) {
                m_->setAnalog(static_cast<ts::Analog>(e.id), e.value);
                wantAnalog_[e.id] = e.value & 0x3FF;  // so a block start does not undo it (A106)
                wantSet_[e.id] = true;
            }
            break;
        case Event::Kind::PatchSelect:  // IEXP, read on the OS's next poll (T9): not on the panel link
            m_->patchSelect(e.id, e.down);
            break;
    }
}

void Engine::feedMidi(uint64_t upTo) {
    while (midiNext_ < midiCount_ && midi_[midiNext_].frame <= upTo) {
        const uint8_t b = midi_[midiNext_++].byte;
        m_->midiIn(b);
        if (inParse_.feed(b)) track(inParse_, true);
    }
}

void Engine::render(float* mainL, float* mainR, float* auxL, float* auxR, int frames) {
    float* const out[kChannels] = {mainL, mainR, auxL, auxR};
    if (!rs_) {  // not prepared
        for (float* o : out) std::fill_n(o, std::max(0, frames), 0.0f);
        return;
    }
    // DECISION A102: a button or key goes on the panel link only when it has room for both bytes,
    // so none is dropped; the rest wait for the next block, in order.
    for (;;) {
        if (!holding_ && !events_.pop(held_)) break;
        holding_ = true;
        const bool link = held_.kind == Event::Kind::Button || held_.kind == Event::Kind::Key;
        if (link && m_->panelRoom() < 2) break;
        apply(held_);
        holding_ = false;
    }
    if (autoLoad_ == AutoLoad::Watch || autoLoad_ == AutoLoad::Held) answerAutoLoad();
    // DECISION A118: a swap waits while the last removed disk is untaken, so none is lost or freed here.
    if (swapWanted_.load(std::memory_order_acquire) && !removedFull_.load(std::memory_order_acquire)) {
        std::unique_ptr<ts::Disk> old = m_->insertDisk(std::unique_ptr<ts::Disk>(swapIn_.exchange(nullptr)));
        if (old) {  // an empty drive parks nothing, so it never blocks the next swap
            removed_.store(old.release(), std::memory_order_relaxed);
            removedFull_.store(true, std::memory_order_release);
        }
        ++diskSwaps_;
        swapWanted_.store(false, std::memory_order_release);
    }
    if (cdWanted_.load(std::memory_order_acquire)) {  // A128: the CD slot, at the block start
        const uint64_t v = cdIn_.load(std::memory_order_relaxed);
        m_->insertCd(uint32_t(v >> 32), uint32_t(v));
        cdWanted_.store(false, std::memory_order_release);
    }
    // DECISION A119: after the swap, so it reaches a disk inserted in this block too.
    if (const int p = protect_.exchange(-1, std::memory_order_relaxed); p >= 0 && m_->disk())
        m_->disk()->setWriteProtected(p != 0);
    if (copyState_.load(std::memory_order_acquire) == 1) {
        const ts::Disk* cur = m_->disk();
        copyOk_.store(cur && cur->copyInto(*scratch_.load(std::memory_order_relaxed)), std::memory_order_relaxed);
        copySwaps_.store(diskSwaps_, std::memory_order_relaxed);  // after the swap above: this disk's count
        copyState_.store(2, std::memory_order_release);
    }
    blockStart();
    blockFrame_ = 0;
    for (int done = 0; done < frames;) {
        const int n = std::min(frames - done, maxBlock_);
        float* const piece[kChannels] = {mainL + done, mainR + done, auxL + done, auxR + done};
        renderPiece(piece, n);
        done += n;
    }
    feedMidi(UINT64_MAX);  // bytes due after the frames this block ran
    midiCount_ = midiNext_ = 0;
    // DECISION A2: MIDI out is drained every block and discarded, counted.
    for (uint8_t b; m_->popMidiOut(b);) {
        ++midiOut_;
        if (outParse_.feed(b)) track(outParse_, false);
    }
    // DECISION A103: published under try_lock; the block is skipped when the GUI holds the lock.
    if (snapMu_.try_lock()) {
        snap_.panel = m_->panel();
        snap_.seqLed = m_->seqLed();
        snap_.booted = frames_ >= kBootFrames;
        snap_.frames = frames_;
        snap_.midiDropped = storeDropped_ + m_->duart().rxDropped(ts::kMidiChannel);
        snap_.midiOutBytes = midiOut_;
        snap_.program = static_cast<int16_t>(current_);
        snap_.select = select_;
        snap_.selectLen = selectLen_;
        snap_.notes = notes_;
        snap_.programsSent = programsSent_;
        snap_.programSeq = programSeq_;
        snap_.diskIn = m_->disk() != nullptr;
        snap_.diskWrites = m_->disk() ? m_->disk()->writes() : 0;
        snap_.diskSwaps = diskSwaps_;
        snap_.diskReads = m_->disk() ? m_->disk()->reads() : 0;
        snap_.cdMedium = m_->cd() ? m_->cd()->medium() : 0;
        snapMu_.unlock();
    }
    // The acquire on ramReady_ pairs with takeRam's release, so its reads of ram_ end before this
    // copy; the exchange takes the request first, so one made during the copy is kept.
    if (!ramReady_.load(std::memory_order_acquire) && ramWanted_.exchange(false, std::memory_order_acquire)) {
        std::copy(m_->ramImage().begin(), m_->ramImage().end(), ram_.begin());
        ramReady_.store(true, std::memory_order_release);
    }
}

void Engine::renderPiece(float* const out[kChannels], int frames) {
    const auto want = kChannels * static_cast<size_t>(frames);
    while (out_.size() < want) {
        feedMidi(blockFrame_);
        ts::Audio a;
        m_->runFrame(a);
        ++blockFrame_;
        ++frames_;
        const int16_t s[kChannels] = {a.mainL, a.mainR, a.auxL, a.auxR};
        rs_->process(s, 1, out_);
    }
    for (size_t i = 0; i < static_cast<size_t>(frames); ++i)
        for (size_t c = 0; c < kChannels; ++c) out[c][i] = out_[kChannels * i + c];
    out_.erase(out_.begin(), out_.begin() + static_cast<std::ptrdiff_t>(want));  // the leftover stays
}

}  // namespace ens::app
