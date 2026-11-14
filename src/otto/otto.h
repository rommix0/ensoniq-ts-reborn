#pragma once
// ES5506 "OTTO" wave chip core, written from its datasheet
// (docs/reference/es5506-otto-datasheet-notes.md). Integer-only, no OS calls, no globals.
#include <cstdint>

namespace ens {

// Sound memory as the chip sees it: four banks of 2M 16-bit words. The board implements it.
struct OttoMemory {
    virtual ~OttoMemory() = default;
    virtual uint16_t read(int bank, uint32_t addr21) = 0;  // bank 0-3, 21-bit word address
};

// Pure signal-path helpers (exposed for testing). Filter values are 18-bit signed in half-LSB units.
int16_t otto_decompress(uint16_t word);                            // OT6: CMPD sample word
int32_t otto_interp(int16_t s1, int16_t s2, uint32_t f9);          // OT8
int32_t otto_lp(int32_t x, int32_t y1, uint16_t k12);              // OT9: low-pass pole
int32_t otto_hp(int32_t x, int32_t x1, int32_t y1, uint16_t k12);  // OT9: high-pass pole
int32_t otto_vol(int32_t x16, uint16_t vol);                       // OT12: one side of volume/pan
uint16_t otto_ramp(uint16_t v, uint8_t r8);                        // OT15: one envelope step

struct OttoFrame {
    int32_t ch[6][2];  // 20-bit signed, [channel][0=L,1=R]
};

class Otto {
public:
    explicit Otto(OttoMemory& mem);
    void reset();

    // Host bus, notes §1. ha = HA5..HA0: register = ha >> 2 (location / 8), byte = ha & 3, 0 = MSB.
    uint8_t read(uint8_t ha);
    void write(uint8_t ha, uint8_t data);

    bool irqAsserted() const { return irqPin_; }  // true = IRQB low
    void setPot(uint16_t v10) { pot_ = v10 & 0x3FF; }  // the POT_IN pin's value as a 10-bit count (OT16)
    int activeVoices() const { return actv_ + 1; }
    static uint32_t sampleRateHz(uint32_t clkHz, int voices) {  // notes §4: 16 CLKs per voice
        return clkHz / (16u * static_cast<uint32_t>(voices));
    }

    // Voice scan. stepVoice() runs the next voice slot (0..ACTV): fetch, signal path, envelope,
    // address update, interrupt. After slot ACTV the channel sums are latched into the frame and
    // the frame is ready; starting slot 0 of the next frame clears the ready flag.
    void stepVoice();
    bool frameReady() const { return ready_; }
    OttoFrame takeFrame();  // returns the latched frame and clears ready
    OttoFrame runFrame();   // steps to the end of a frame, then takes it
    uint32_t accum(int voice) const { return voice_[voice & 31].accum; }  // test accessor

private:
    struct Voice {
        uint32_t cr, fc, lvol, lvramp, rvol, rvramp, ecount, k2, k2ramp, k1, k1ramp;  // pages 00-1F
        uint32_t start, end, accum;                                                   // pages 20-3F
        uint32_t o4_1, o3_1, o3_2, o2_1, o2_2, o1_1;  // filter temps, 18 bits as stored
        uint32_t filtcount;                           // OT15: 3-bit slow-ramp counter
    };

    uint32_t readReg(uint8_t reg);  // the 32-bit read a byte-0 access performs (with side effects)
    void writeReg(uint8_t reg, uint32_t v);
    int32_t filterVoice(Voice& v);      // OT8-OT11: interpolate and filter; returns the 16-bit output
    void envelope(Voice& v);            // OT15
    void latchFrame();                  // OT13
    void updateAddress(Voice& v);       // OT4
    void serviceIrq(int vi, Voice& v);  // OT14
    uint32_t* voiceReg(uint8_t reg, uint32_t& mask);  // the current page's voice register, or nullptr

    OttoMemory& mem_;
    Voice voice_[32];
    uint32_t actv_, mode_, wst_, wend_, lrend_, page_, par_, irqv_;
    uint32_t chTest_[12];  // test-page CH0L..CH5R, 23 bits (OT13)
    uint8_t wlatch_[3];    // OT1: shared write latch for bytes 0-2
    uint32_t rlatch_;      // OT1: shared read latch
    bool irqPin_;
    uint32_t irqClear_;  // OT14: bit per voice, IRQV was read: clear CR.IRQ at its next slot
    uint32_t slot_;      // next voice slot
    bool ready_;
    OttoFrame frame_;
    uint16_t s1_, s2_;   // words fetched by the current slot
    int32_t sum_[6][2];  // OT13: channel sums of the frame in progress
    uint32_t pot_ = 0;   // OT16: the pin's value; survives reset()
    uint32_t potSlots_;  // OT16: slots left in the conversion in progress, 0 = idle
};

}  // namespace ens
