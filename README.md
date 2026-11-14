# Ensoniq TS Reborn

An emulator of the Ensoniq TS-10 (1993), the Performance/Composition Synthesizer, with the TS-12 to
follow. It will run the instrument's own operating system, OS v3.10, from your ROM files on an
emulated machine: a 68000-class CPU, the ES5506 OTTO wave chip, the ES5510 ESP effects chip and the
68681 DUART. Every screen, sound and effect comes from the original firmware, unmodified, played from
a MIDI keyboard in a Standalone app or a VST3 instrument named "TS Reborn". It follows the method of
[KS-32 Reborn](https://github.com/rommix0/ensoniq-ks32-reborn).

**Status:** Plan 1: the 68000, ESP and DUART cores are seeded from KS-32 Reborn. No TS machine yet.
The design is [docs/design/2026-11-13-ts-emulator-design.md](docs/design/2026-11-13-ts-emulator-design.md).

## The ROMs

The ROMs are Ensoniq's copyrighted firmware and samples. The emulator needs six files
(see [roms/README.txt](roms/README.txt)):

| File | Size | What it is |
|---|---|---|
| `os/ts10_310h.bin` | 256 KB | OS v3.10, high byte (even addresses) |
| `os/ts10_310l.bin` | 512 KB | OS v3.10, low byte (odd addresses) |
| `waves/901401_u41.bin`, `waves/901402_u45.bin` | 2 MB each | Wave ROMs |
| `waves/901301_u43.bin`, `waves/901302_u46.bin` | 1 MB each | Wave ROMs |

## Building

Needs CMake 3.20 or later and either Visual Studio 2022 (MSVC x64) or MinGW-w64 with Ninja.

```powershell
cmake -S . -B build-msvc -G "Visual Studio 17 2022" -A x64
cmake --build build-msvc --config Release
ctest --test-dir build-msvc -C Release --output-on-failure
```

## License

Copyright (C) 2026 Anthony C. Bartman (@rommix0)

This program is free software: you can redistribute it and/or modify it under the terms of the GNU Affero General Public License as published by the Free Software Foundation, either version 3 of the license, or (at your option) any later version. It is distributed WITHOUT ANY WARRANTY; see [LICENSE](LICENSE) for the full text.

Ensoniq, TS-10 and TS-12 are trademarks of their respective owners. This project is not affiliated with or endorsed by them. The ROMs are not covered by this license.
