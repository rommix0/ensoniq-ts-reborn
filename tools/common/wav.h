#pragma once
// 16-bit PCM RIFF/WAVE writer (bytes only; the caller writes the file). Copied from KS-32 Reborn.
#include <cstdint>
#include <vector>

// 44-byte header followed by the samples, little-endian. `interleaved` holds frames * channels samples.
std::vector<uint8_t> makeWav16(const std::vector<int16_t>& interleaved, int channels, uint32_t rate);
