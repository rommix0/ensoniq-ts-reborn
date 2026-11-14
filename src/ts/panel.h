#pragma once
// The TS-10 front panel and keyboard on DUART channel B: the 2 x 40 display and 16 LEDs of the
// keypad/display board (fed by the CPU's TxDB), and the keyboard microcontroller that merges the panel
// buttons with its keys and Poly-Key pressure into the CPU's RxDB. The protocol is decoded from the
// bytes OS v3.10 sends and from how it reacts to candidate bytes (docs/ts-hardware-interface.md,
// "Display and LEDs", "Keyboard link", "Buttons", "Keys and pressure"), never from ROM code.
// Pure emulation: no I/O, no statics.
#include <cstdint>
#include <string>
#include <type_traits>

#include "common/ring.h"

namespace ens::ts {

constexpr int kPanelRows = 2, kPanelCols = 40, kPanelLeds = 16;
// The 61 Poly-Key keys, C2 to C7 (MIDI notes 36-96); key code = note - kLowestKey (DECISION T17).
constexpr int kLowestKey = 36, kKeyCount = 61;

// Every front-panel button by the code the keyboard sends for it (DECISION T16). Names follow the
// Musician's Manual and the service manual's self-test chart (p11); each code was matched by the page,
// LED or MIDI message the running OS shows for it (docs/ts-hardware-interface.md, "Buttons").
enum class Button : uint8_t {
    Bank0 = 0x06, Bank1 = 0x07, Bank2 = 0x18, Bank3 = 0x19, Bank4 = 0x20,
    Bank5 = 0x21, Bank6 = 0x28, Bank7 = 0x29, Bank8 = 0x30, Bank9 = 0x31,
    Soft1 = 0x1E, Soft2 = 0x26, Soft3 = 0x2E,  // above the display, left to right
    Soft4 = 0x1F, Soft5 = 0x27, Soft6 = 0x2F,  // below the display, left to right
    SeqsSongs = 0x0E, Presets = 0x0F, Sounds = 0x10, BankSet = 0x11,
    Up = 0x16, Down = 0x17,
    MixPan = 0x36, AttackRelease = 0x3E, BrightnessTimbre = 0x02, KeyZoneVelocity = 0x37, Tuning = 0x3F,
    ControllersOnOff = 0x03, PerformanceOptions = 0x38, TrackMidi = 0x08, TrackEffects = 0x04,
    ReplaceTrackSound = 0x39, Tracks1to6 = 0x09, Tracks7to12 = 0x05,
    System = 0x0A, Storage = 0x12, MidiControl = 0x1A,
    EditSong = 0x0B, EditSequence = 0x13, EditTrack = 0x1B, Click = 0x0C, SequencerControl = 0x14,
    Locate = 0x1C, Record = 0x0D, StopContinue = 0x15, Play = 0x1D,
    Lfo = 0x22, Env1 = 0x2A, Env2 = 0x32, Env3 = 0x3A, Pitch = 0x23, PitchMods = 0x2B, Filters = 0x33,
    Output = 0x3B, Wave = 0x24, ModMixer = 0x2C, ProgramControl = 0x34, ProgramEffects = 0x3C,
    SelectVoice = 0x25, Copy = 0x2D, Write = 0x35, Compare = 0x3D,
    // Not panel buttons: the two contacts of the Foot Switch 1 jack, which the keyboard reads (service
    // manual p13) and sends like buttons. With the default FOOT-SW1-R=SUSTAIN the right one sends MIDI
    // controller 64.
    FootSwitch1Right = 0x00, FootSwitch1Left = 0x01,
};

struct ButtonName {
    Button button;
    const char* name;  // upper case, as tsemu --press takes it
};

inline constexpr ButtonName kButtons[] = {
    {Button::Bank0, "BANK0"}, {Button::Bank1, "BANK1"}, {Button::Bank2, "BANK2"}, {Button::Bank3, "BANK3"},
    {Button::Bank4, "BANK4"}, {Button::Bank5, "BANK5"}, {Button::Bank6, "BANK6"}, {Button::Bank7, "BANK7"},
    {Button::Bank8, "BANK8"}, {Button::Bank9, "BANK9"},
    {Button::Soft1, "SOFT1"}, {Button::Soft2, "SOFT2"}, {Button::Soft3, "SOFT3"},
    {Button::Soft4, "SOFT4"}, {Button::Soft5, "SOFT5"}, {Button::Soft6, "SOFT6"},
    {Button::SeqsSongs, "SEQS_SONGS"}, {Button::Presets, "PRESETS"}, {Button::Sounds, "SOUNDS"},
    {Button::BankSet, "BANKSET"}, {Button::Up, "UP"}, {Button::Down, "DOWN"},
    {Button::MixPan, "MIX_PAN"}, {Button::AttackRelease, "ATTACK_RELEASE"},
    {Button::BrightnessTimbre, "BRIGHTNESS_TIMBRE"}, {Button::KeyZoneVelocity, "KEY_ZONE_VELOCITY"},
    {Button::Tuning, "TUNING"}, {Button::ControllersOnOff, "CONTROLLERS_ON_OFF"},
    {Button::PerformanceOptions, "PERFORMANCE_OPTIONS"}, {Button::TrackMidi, "TRACK_MIDI"},
    {Button::TrackEffects, "TRACK_EFFECTS"}, {Button::ReplaceTrackSound, "REPLACE_TRACK_SOUND"},
    {Button::Tracks1to6, "TRACKS_1_6"}, {Button::Tracks7to12, "TRACKS_7_12"},
    {Button::System, "SYSTEM"}, {Button::Storage, "STORAGE"}, {Button::MidiControl, "MIDI_CONTROL"},
    {Button::EditSong, "EDIT_SONG"}, {Button::EditSequence, "EDIT_SEQUENCE"}, {Button::EditTrack, "EDIT_TRACK"},
    {Button::Click, "CLICK"}, {Button::SequencerControl, "SEQUENCER_CONTROL"}, {Button::Locate, "LOCATE"},
    {Button::Record, "RECORD"}, {Button::StopContinue, "STOP_CONTINUE"}, {Button::Play, "PLAY"},
    {Button::Lfo, "LFO"}, {Button::Env1, "ENV1"}, {Button::Env2, "ENV2"}, {Button::Env3, "ENV3"},
    {Button::Pitch, "PITCH"}, {Button::PitchMods, "PITCH_MODS"}, {Button::Filters, "FILTERS"},
    {Button::Output, "OUTPUT"}, {Button::Wave, "WAVE"}, {Button::ModMixer, "MOD_MIXER"},
    {Button::ProgramControl, "PROGRAM_CONTROL"}, {Button::ProgramEffects, "PROGRAM_EFFECTS"},
    {Button::SelectVoice, "SELECT_VOICE"}, {Button::Copy, "COPY"}, {Button::Write, "WRITE"},
    {Button::Compare, "COMPARE"}, {Button::FootSwitch1Left, "FOOTSW1_L"}, {Button::FootSwitch1Right, "FOOTSW1_R"},
};

// Plain data, so the audio thread can copy it without allocating.
struct PanelState {
    char text[kPanelRows][kPanelCols];  // character codes as sent (0x00-0x7E)
    uint64_t blink[kPanelRows];         // bit c: column c flashes (its character, or its underline if underlined)
    uint64_t underline[kPanelRows];     // bit c: column c is underlined
    uint32_t leds;                      // bit n: LED n lit; bit 16 + n: LED n flashing (DECISION T15)
    uint32_t unknown;                   // bytes outside the decoded command set
};
static_assert(std::is_trivially_copyable_v<PanelState>);

// Row 0 or 1 as a string of kPanelCols characters, for tools and tests.
inline std::string rowText(const PanelState& p, int row) { return std::string(p.text[row], kPanelCols); }

class Panel {
public:
    Panel() { reset(); }
    void reset();                // power-on: blank display, LEDs off, nothing queued
    void fromCpu(uint8_t b);     // DUART B TX -> the display board (and the keyboard's commands)
    bool popToCpu(uint8_t& b);   // keyboard -> DUART B RX; nothing while KRES is high or SACK/ is high
    void setLines(bool kres, bool dres, bool sack);  // OP4, OP5, OP7 pin levels (true = high)
    void button(Button b, bool down);
    void key(int note, bool down, int velocity);  // note 36-96; velocity = the raw strike value, clamped to 1-127
    void pressure(int note, int amount);          // Poly-Key pressure of a key, clamped to 0-127
    const PanelState& state() const { return s_; }
    std::size_t toCpuQueued() const { return toCpu_.size(); }
    uint32_t dropped() const { return dropped_; }  // events lost to a full ring since reset
    static constexpr std::size_t kToCpuSize = 256;

private:
    void clearDisplay();
    void put(uint8_t c);
    void mode(bool underline, bool blink);  // the attributes of the characters written from now on
    void cell(bool underline, bool blink);  // sets the attributes of the cell at the cursor
    void run(uint8_t cmd, uint8_t param);
    void queue(uint8_t a, uint8_t b);

    PanelState s_;
    ByteRing<kToCpuSize> toCpu_;
    uint32_t dropped_ = 0;
    uint8_t cursor_ = 0;  // 0..79: row = cursor / 40
    uint8_t saved_ = 0;   // the cursor slot of 0xF5/0xF6
    uint8_t cmd_ = 0;     // command waiting for its parameter byte (0 = none)
    bool underlineMode_ = false, blinkMode_ = false, keepAttributes_ = true;
    bool kres_ = false, sack_ = false;
};

}  // namespace ens::ts
