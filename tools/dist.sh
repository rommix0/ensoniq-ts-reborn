#!/usr/bin/env bash
# Linux counterpart of tools/dist.ps1: builds the TS Reborn Standalone and VST3 (GCC x64, Release, static
# libstdc++ and libgcc) in build-dist-linux and assembles "dist/TS Reborn-<version>-linux": TS Reborn, the
# TS Reborn.vst3 package, README.md with panel.png, LICENSE, the app guide, and the eight ROM files (the app
# loads six of them per model), in roms/ beside the program and again inside the VST3 package
# (Contents/Resources/roms). DECISION A126.
# The ROMs are Ensoniq's.
#
#   tools/dist.sh [--rom-folder <folder>] [--juce-source <JUCE checkout>]
#
# --rom-folder defaults to the repository's roms folder. Without --juce-source the first configure downloads
# JUCE (app/CMakeLists.txt) into build-dist-linux/_deps.
set -euo pipefail
cd "$(dirname "$0")/.."
root=$PWD

rom_folder=$root/roms
juce_source=
while [ $# -gt 0 ]; do
    case $1 in
        --rom-folder) rom_folder=${2:?--rom-folder needs a folder}; shift 2 ;;
        --juce-source) juce_source=${2:?--juce-source needs a folder}; shift 2 ;;
        *) echo "unknown argument: $1" >&2; exit 1 ;;
    esac
done

version=$(grep -oP 'VERSION \K\d+\.\d+\.\d+' app/CMakeLists.txt | head -1)
[ -n "$version" ] || { echo 'No VERSION in app/CMakeLists.txt' >&2; exit 1; }

# The eight files (the six RomFolder.cpp loads, plus the TS-12 OS pair), with their sizes: checked before anything is built or written.
roms=(os/ts10_310h.bin:262144 os/ts10_310l.bin:524288
      os/ts12_310h.bin:262144 os/ts12_310l.bin:524288
      waves/901401_u41.bin:2097152 waves/901301_u43.bin:1048576
      waves/901402_u45.bin:2097152 waves/901302_u46.bin:1048576)
for entry in "${roms[@]}"; do
    f=${entry%%:*} want=${entry##*:}
    [ -f "$rom_folder/$f" ] || { echo "ROM missing: $f in $rom_folder" >&2; exit 1; }
    size=$(stat -c %s "$rom_folder/$f")
    [ "$size" = "$want" ] || { echo "ROM $f is $size bytes, not $want" >&2; exit 1; }
done

if [ ! -f build-dist-linux/CMakeCache.txt ]; then
    cfg=(-S . -B build-dist-linux -G Ninja -DCMAKE_BUILD_TYPE=Release -DENS_BUILD_APP=ON
         "-DCMAKE_EXE_LINKER_FLAGS=-static-libstdc++ -static-libgcc"     # runs without this GCC's libstdc++
         "-DCMAKE_MODULE_LINKER_FLAGS=-static-libstdc++ -static-libgcc")
    if [ -n "$juce_source" ]; then cfg+=("-DFETCHCONTENT_SOURCE_DIR_JUCE=$juce_source"); fi
    cmake "${cfg[@]}"
fi
cmake --build build-dist-linux --target TSReborn_Standalone TSReborn_VST3

art=build-dist-linux/app/TSReborn_artefacts/Release
out="dist/TS Reborn-$version-linux"
rm -rf "$out"
mkdir -p "$out"
cp "$art/Standalone/TS Reborn" "$out/"
cp -r "$art/VST3/TS Reborn.vst3" "$out/"
cp README.md panel.png LICENSE "$out/"
cp docs/tsapp.md "$out/APP-GUIDE.md"
for dest in "$out/roms" "$out/TS Reborn.vst3/Contents/Resources/roms"; do
    for entry in "${roms[@]}"; do
        f=${entry%%:*}
        mkdir -p "$(dirname "$dest/$f")"
        cp "$rom_folder/$f" "$dest/$f"
    done
done
echo "dist: $root/$out"
