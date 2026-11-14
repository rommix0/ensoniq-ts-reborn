# TS Reborn, the JUCE app (Plan 4): Standalone and VST3 around ens::app::Engine.
# JUCE is fetched into the build directory (<build>/_deps), never into the source tree; configure with
# -DFETCHCONTENT_SOURCE_DIR_JUCE=<a JUCE 8.0.16-security checkout> to build without a download.
enable_language(C)  # JUCE compiles C sources; the default builds stay C++ only
include(FetchContent)
FetchContent_Declare(juce
  GIT_REPOSITORY https://github.com/juce-framework/JUCE
  GIT_TAG 8.0.16-security  # as KS-32 Reborn: the security release of 8.0.15
  GIT_SHALLOW TRUE)
FetchContent_MakeAvailable(juce)

juce_add_plugin(TSReborn
  PRODUCT_NAME "TS Reborn"
  VERSION 0.4.0
  BUNDLE_ID com.tsreborn.tsreborn
  COMPANY_NAME "TS Reborn"
  PLUGIN_MANUFACTURER_CODE Tsre
  PLUGIN_CODE Tsrb
  FORMATS Standalone VST3
  IS_SYNTH TRUE
  NEEDS_MIDI_INPUT TRUE
  NEEDS_MIDI_OUTPUT FALSE)

set(TSR_SOURCES source/Processor.cpp source/PanelEditor.cpp source/AppSettings.cpp source/RomFolder.cpp
  source/MemoryFile.cpp source/SettingsPage.cpp)

target_sources(TSReborn PRIVATE ${TSR_SOURCES})
target_compile_definitions(TSReborn PUBLIC JUCE_WEB_BROWSER=0 JUCE_USE_CURL=0 JUCE_VST3_CAN_REPLACE_VST2=0)
target_link_libraries(TSReborn
  PRIVATE ens_app juce::juce_audio_utils
  PUBLIC juce::juce_recommended_config_flags juce::juce_recommended_warning_flags)

# The checks that need JUCE but no window of their own: the ROM folder, the memory file, the
# processor's memory-file guards (dummy files only), its two buses and the built VST3 loaded through
# JUCE's VST3 host (the last two need the ROMs). The processor is compiled in as a plain class (no wrapper).
juce_add_console_app(TSReborn_AppTests PRODUCT_NAME "TSReborn_AppTests" VERSION 0.4.0)
target_sources(TSReborn_AppTests PRIVATE tests/app_tests.cpp ${TSR_SOURCES})
target_compile_definitions(TSReborn_AppTests PRIVATE JUCE_WEB_BROWSER=0 JUCE_USE_CURL=0 JUCE_PLUGINHOST_VST3=1
  JucePlugin_Name="TS Reborn" JucePlugin_Build_Standalone=0
  TSR_ROM_DIR="${ENS_ROM_DIR}"
  TSR_VST3_PATH="$<GENEX_EVAL:$<TARGET_PROPERTY:TSReborn_VST3,JUCE_PLUGIN_ARTEFACT_FILE>>")
add_dependencies(TSReborn_AppTests TSReborn_VST3)
target_link_libraries(TSReborn_AppTests
  PRIVATE ens_app juce::juce_audio_utils
  PUBLIC juce::juce_recommended_config_flags juce::juce_recommended_warning_flags)
add_test(NAME app_tests COMMAND TSReborn_AppTests)
