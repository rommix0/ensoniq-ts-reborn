#pragma once
// File reading for the tools (src/ stays free of file I/O).
#include <cstdint>
#include <string>
#include <vector>

// Whole file into `bytes`. On failure returns false with err = "cannot open" or "cannot read".
bool readFile(const std::string& path, std::vector<uint8_t>& bytes, std::string& err);
