Ensoniq TS-10 / TS-12 ROMs. CRC32 in brackets.

os/    OS EPROMs, 68000-class CPU, 16-bit bus: h = even bytes (D8-15), l = odd bytes (D0-7).
       Board sockets U5/U6 are 27C4001 (512 KB). 256 KB images come from 27C020 parts, which ignore
       A18 in that socket, so they mirror into the upper half.
       ts10_120 h/l   [3ae66382 / 047a0aaa]
       ts10_202 h/l   [509f1c46 / cacedb2c]  own dump. The old MAME-style ts10.zip had h and l swapped.
       ts10_305 h/l   [77d547e7 / 3df39031]
       ts10_310 h/l   [cc04aa4f / 51df8987]  MAME default
       ts12_305 h/l   [7968ca06 / 30b4bece]  own dump
       ts12_310 h/l   [99823433 / 7e64a659]  MAME default (was ts12-v310-hig/low)
waves/ Sample ROMs, MAME byte order and CRCs. The raw chip dumps of u45/u46 were the same data
       byte-swapped per word (CRC 42afc61d / 495f86d6) and were dropped as duplicates.
       901401_u41 [f98f2085] 901301_u43 [ce864565] 901402_u45 [9dfc4dda] 901302_u46 [ba5a9a95]
misc/  esqvfd_font_vfx.bin: MAME's VFD font, not a real-hardware ROM.
