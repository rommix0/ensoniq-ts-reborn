# Adding a test file is a one-line change: append it to ENS_TEST_SOURCES.
set(ENS_TEST_SOURCES
  main.cpp
  test_bits.cpp
  test_esp_alu.cpp
  test_esp_mac.cpp
  test_esp_agen.cpp
  test_esp_core.cpp
  test_esp_host.cpp
  test_esp_isa.cpp
  test_esp_programs.cpp
  test_m68k_cpu.cpp
  test_m68k_harte_format.cpp
  test_m68k_alu.cpp
  test_m68k_irq.cpp
  test_m68k_dis.cpp
  test_duart.cpp
  test_otto_regs.cpp
  test_otto_addr.cpp
  test_otto_signal.cpp
  test_otto_mix.cpp
  test_otto_pot.cpp
  test_ts_roms.cpp
  test_ts_board.cpp
  test_ts_machine.cpp
  test_ts_firmware.cpp
  test_ts_panel.cpp
  test_ts_disk.cpp
  test_ts_fdc.cpp
  test_ts_floppy.cpp
  test_ts_cd_image.cpp
  test_ts_cd_target.cpp
  test_ts_wd33c93.cpp
  test_ts_scsi.cpp
  test_app_engine.cpp
  test_app_panel_layout.cpp
  test_app_media.cpp
)

add_executable(ens_tests ${ENS_TEST_SOURCES})
target_link_libraries(ens_tests PRIVATE ens_tools_common ens_app ens_ts ens_otto ens_esp ens_m68k_harte ens_m68k ens_duart ens_common)
target_compile_definitions(ens_tests PRIVATE ENS_TEST_DATA_DIR="${CMAKE_CURRENT_SOURCE_DIR}/data"
  ENS_ROM_DIR="${ENS_ROM_DIR}" ENS_DISK_DIR="${ENS_DISK_DIR}" _CRT_SECURE_NO_WARNINGS)
add_test(NAME ens_tests COMMAND ens_tests)

# Real-time checks: replaces global operator new/delete to count allocations, so it is its own
# executable with its own main.
add_executable(ens_rt_tests rt_main.cpp)
target_link_libraries(ens_rt_tests PRIVATE ens_tools_common ens_app ens_ts ens_otto ens_esp ens_m68k ens_duart ens_common)
target_compile_definitions(ens_rt_tests PRIVATE ENS_ROM_DIR="${ENS_ROM_DIR}" ENS_DISK_DIR="${ENS_DISK_DIR}")
add_test(NAME ens_rt_tests COMMAND ens_rt_tests)
