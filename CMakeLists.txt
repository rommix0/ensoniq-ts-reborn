cmake_minimum_required(VERSION 3.20)
project(ens_ts CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

if(MSVC)
  add_compile_options(/W4)
else()
  add_compile_options(-Wall -Wextra)
endif()

set(ENS_M68K_TESTS_DIR "${CMAKE_SOURCE_DIR}/testdata/680x0/68000/m68t" CACHE PATH
  "Converted Tom Harte 68000 tests (.m68t, see docs/m68k.md); m68k_suite skips when absent")
set(ENS_ROM_DIR "${CMAKE_SOURCE_DIR}/roms" CACHE PATH "TS ROM directory (roms/, see roms/README.txt)")

set(CMAKE_RUNTIME_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR})  # build/ens_tests

enable_testing()

# Header-only helpers shared by every chip core: #include "common/bits.h"
add_library(ens_common INTERFACE)
target_include_directories(ens_common INTERFACE ${CMAKE_CURRENT_SOURCE_DIR}/src)

add_library(ens_esp STATIC src/esp/esp_alu.cpp src/esp/esp_mac.cpp src/esp/esp_agen.cpp
  src/esp/esp_isa.cpp src/esp/esp.cpp)
target_link_libraries(ens_esp PUBLIC ens_common)

# Motorola 68000 core (copied from KS-32 Reborn).
add_library(ens_m68k STATIC src/m68k/m68k_decode.cpp src/m68k/m68k_cpu.cpp src/m68k/m68k_ea.cpp
  src/m68k/m68k_ops_data.cpp src/m68k/m68k_alu.cpp src/m68k/m68k_ops_arith.cpp
  src/m68k/m68k_ops_logic.cpp src/m68k/m68k_ops_flow.cpp src/m68k/m68k_dis.cpp)
target_link_libraries(ens_m68k PUBLIC ens_common)

# ES5506 OTTO (Plan 2): the TS's wave chip.
add_library(ens_otto STATIC src/otto/otto.cpp)
target_link_libraries(ens_otto PUBLIC ens_common)

# MC68681 DUART: the TS's serial and timer chip.
add_library(ens_duart STATIC src/duart/duart.cpp)
target_link_libraries(ens_duart PUBLIC ens_common)

# TS machine (Plan 3): ROM set, wave memory; the machine itself joins in later tasks.
add_library(ens_ts STATIC src/ts/roms.cpp src/ts/board.cpp)
target_link_libraries(ens_ts PUBLIC ens_otto ens_esp ens_m68k ens_duart)

# Tom Harte's per-instruction tests (.m68t reader and runner); the data itself is not in the repo.
add_library(ens_m68k_harte STATIC src/m68k/harte_format.cpp)
target_link_libraries(ens_m68k_harte PUBLIC ens_m68k)

add_subdirectory(tests)

# Command-line tools (file I/O allowed here, not in src/).
add_executable(esp_dis tools/esp_dis/main.cpp)
target_link_libraries(esp_dis PRIVATE ens_esp ens_common)
target_compile_definitions(esp_dis PRIVATE _CRT_SECURE_NO_WARNINGS)  # fopen on MSVC (C4996)

# 68000 per-instruction suite runner (tools/m68k_harte/convert_harte.py makes the .m68t files).
add_executable(m68k_harte tools/m68k_harte/main.cpp)
target_link_libraries(m68k_harte PRIVATE ens_m68k_harte)
target_compile_definitions(m68k_harte PRIVATE _CRT_SECURE_NO_WARNINGS)
add_test(NAME m68k_suite COMMAND m68k_harte ${ENS_M68K_TESTS_DIR})
