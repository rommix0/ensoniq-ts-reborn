# TS Floppy Drive: Design

Date: 2026-11-14
Status: approved by the user, 2026-11-14.
Parent: `docs/design/2026-11-13-ts-emulator-design.md` ("the main design"), which listed disk images as
a later stage (section 11).

How it was decided: the user chose what the drive is for (load ASR-10 and EPS sample disks, save to
disk images) and the speed (as fast as possible). Everything marked *my call* is the designer's, open
to correction.

## 1. Intent

Give TS Reborn a working floppy drive. The user opens a disk image file, the TS-10's own OS loads
EPS, EPS-16 PLUS and ASR-10 sampled sounds from it into sample RAM (Musician's Manual, "Sampled Sounds
and SIMMs" and Section 14), and anything the OS writes to the disk can be saved back to an image file.

**Why.** The TS-10's sample RAM is only useful with sample disks, and the user has a library of EPS
and ASR-10 disk images (`C:\Users\abart\Desktop\EPS-ASR10 Discs`). Saving makes the TS's own work
(sounds, presets, sequences, edited samples) keepable on disk, as on the instrument.

## 2. Goals

1. **A working uPD72069** (U20) in `src/ts/fdc.h`: the uPD765-family commands the OS sends, with data
   moved by polling in non-DMA mode, as the OS does it (below).
2. **A disk in the drive**: double density (80 tracks, 2 sides, 10 sectors of 512 bytes, 819 200
   bytes) and high density (20 sectors per track, 1 638 400 bytes), with the drive lines the OS reads
   on the DUART (index pulse IP0, disk change IP1, density IP2) and ST3 (ready, write protect, track 0,
   two sides).
3. **Disk image files**, read and written:
   - raw `.img`: exactly 819 200 or 1 638 400 bytes;
   - `.GKH`: a 58-byte header beginning "TDDFI", then the raw sectors;
   - Giebler `.EDE` (EPS) and `.EDA` (ASR-10): a 512-byte header with a bitmap of the blocks left
     out, then the stored blocks.
   The format is found from the content, not the file name.
4. **The app**: open a disk image, start a new blank disk (double or high density), save, save as,
   eject, in the Standalone and the VST3.
5. **As fast as possible** (the user's choice): the drive never makes the OS wait for a byte, a seek
   or a sector to come round. The index pulse keeps whatever timing the OS needs to see a turning
   disk.
6. **Headless checks**: `tsemu` takes a disk image, and firmware tests load a sampled sound from an
   image and format and write a blank disk.

## 3. What "passed" means

The user opens an ASR-10 factory disk and an EPS disk from their collection, loads an instrument from
each with the panel as on a TS-10, and plays it. They save something to a new blank disk, close the
app, open the image again and load it back. Supporting evidence: the firmware tests in goal 6 pass on
MinGW, MSVC and Linux GCC.

## 4. Non-goals

- `.hfe` images (raw magnetic flux). The EPS disks that are `.hfe` also have `.GKH` copies.
- Building a disk from loose `.efe` files (the 390 EPS files). A good follow-up stage.
- SCSI and the CD-ROMs.
- Real drive timing (the user chose speed).
- Waveboy effect disks or anything the TS-10 OS itself does not read.

## 5. Anti-goals

- **Patching or intercepting the OS's disk routines.** The OS runs unmodified and drives the
  emulated controller itself (AGENTS.md).
- **A disk file that changes without the user saying so.** The OS writes only to the image in memory.
- **Breaking the empty drive.** With no disk, the boot, the messages ("DISK DRIVE NOT READY") and the
  reference hashes stay as they are.

## 6. Constraints

- No file I/O, OS calls or JUCE under `src/`. The image codecs work on byte buffers and may live in
  `src/ts`; reading and writing files belongs to `app/`, `tools/` and `tests/`.
- The audio path stays real-time safe: inserting, ejecting or copying out a disk must not allocate,
  lock or wait in `Machine::runFrame` or `Engine::render`.
- The reference hashes in AGENTS.md do not change (no disk in those runs).
- Tests that need the user's disk images read them from an environment variable (as `ENS_ROM_DIR`) and
  print "skip" without them. The disk images are never committed.
- Tests never touch the user's settings, memory or disk files.

## 7. What the firmware does (evidence)

From the OS v3.10 disassembly (study allowed since 2026-11-14) and `docs/ts-hardware-interface.md`,
"Floppy controller and the empty drive" (T10):

- The controller is at 0x280001 (main status) and 0x280003 (data), polled. The interrupt line is not
  used (level 2 has no source, T5).
- Read Data is command 0x46 (MFM, no multi-track), Write Data 0x45. The OS sends the command, then
  loops on the main status register: RQM set, EXM (bit 5) set and DIO matching the direction, it moves
  one byte (`c0f576` read loop, `c0f5c6` write loop); EXM clear ends the transfer and the OS reads the
  seven result bytes. Bytes past what it wants are read and dropped. A poll counter (0x3D0900) is its
  timeout.
- Writes to 0x280001 (0x36, 0x0B, 0x4F, 0xC8, 0xE3 at boot; 0x1E and 0x0E around each access) are not
  uPD765 commands. They are probably the 72069's own registers (motor, drive select, data rate). Which
  bit selects high density is to be found.
- With the index pulse turning, the OS expects a drive that answers seeks and reads ("DISK DRIVE NOT
  RESPONDING" otherwise).

## 8. Approaches

| | Approach | Trade-off |
|---|---|---|
| **A (chosen)** | A command-level uPD72069 and a disk held as 512-byte sectors | Small and testable; reads every standard Ensoniq image. Cannot read `.hfe` or odd formats. |
| B | A flux-level drive (MFM bit streams per track) | Would read `.hfe`, but all sample disks are standard sectors; much more code and slower. |
| C | Intercept the OS's disk calls | Goes against running the firmware unmodified. Ruled out. |

There was one sensible way; the user did not have to pick.

## 9. The design

**Machine (`src/ts`).**
- `Disk`: the sectors of one disk, its density, whether each track is formatted (a blank disk is
  unformatted until the OS formats it, as the Mirage project's `DiskImage` does), a write-protect
  flag, and a "modified" flag set by any write or format.
- `Fdc`: the uPD765 command set the OS uses, on top of today's empty-drive answers: Specify,
  Recalibrate, Seek, Sense Interrupt Status, Sense Drive Status, Read ID, Read Data, Write Data,
  Format Track. Results and status bits as the uPD765 datasheet gives them; errors (no such sector,
  unformatted track, wrong density, write protected) as abnormal termination with the matching ST
  bits. Data moves in non-DMA mode, one byte per data-register access, ready on every poll.
- `Machine`: holds the drive. Insert and eject swap a disk in at a frame boundary and raise disk change
  (IP1) as a real drive does. The index pulse on IP0 runs only while a disk is in; the density line
  (IP2) follows the disk.
- Codecs: raw, GKH, EDE and EDA to and from a `Disk`, on byte buffers. Saving writes the format the
  disk was read in.

**Engine (`src/app`).** Insert, eject and "copy the current disk out" requests cross from the
message thread to the audio thread without locking (the engine's existing request pattern). The
message thread allocates the `Disk`; the audio thread only swaps pointers. *My call:* a copy-out is
taken at a frame boundary into a buffer the message thread owns.

**App (`app/`).**
- *My call:* the drive drawn on the panel is the control. Clicking it opens a menu: Open Disk
  Image..., New Blank Disk (DD) / (HD), Save Disk Image, Save Disk Image As..., Eject. A file dropped
  on the panel opens it. The slot shows the file name, with "(modified)" when the OS has written
  since the last save, or stays empty.
- *My call:* Open, New and Eject ask first when the disk has unsaved writes, and so does closing the
  Standalone. Save As picks the format from the extension typed (`.img`, `.gkh`, `.ede`/`.eda`).
- *My call:* Save replaces the file the disk came from, by writing `<file>.tmp` and then replacing
  the file (as the memory file does), so a failed save keeps the old file. A file that cannot be
  written (read-only) can only be saved with Save As. EDE/EDA saves re-encode the bitmap from the
  disk's contents, so blocks the OS wrote into are stored.
- *My call:* a "Write Protected" tick in the drive menu, off by default, sets the disk's write
  protect (ST3 WP), so the OS refuses to write, as with the tab on a real disk.
- *My call:* the disk is not remembered across a restart; the drive starts empty. In the VST3, a
  plugin removed or a project closed with unsaved writes loses them (the host gives no chance to
  ask); the slot's "(modified)" is the warning.
- *My call:* the same menu and file dialogs (JUCE `FileChooser`, async) on Windows and Linux, in the
  Standalone and the VST3. The "(modified)" mark is read from the engine's snapshot, as the display
  is.
- *My call:* the file's size decides the density for `.img`; a file of any other size is refused with
  a message naming the sizes expected.
- *My call:* opening a disk does not reset the TS; the OS sees a disk change, as when a disk is put
  in a real drive.
- *My call:* the VST3's project state does not carry the disk or its path. Sample RAM is not battery
  backed on the TS-10 either, so a reopened project needs its samples loaded again from the panel.

**Tools and tests.**
- `tsemu boot` and `tsemu render` take `--disk <image>` (and `--save-disk <file>` to write the disk
  out at the end).
- Rule tests for the controller (each command, result bytes, errors, non-DMA transfer) and the codecs
  (each format round-trips; the wrong size is refused).
- Firmware tests (skipped without the user's images): load an instrument from an ASR-10 and an EPS
  image and check the OS reports it loaded and it sounds; format a blank disk with the OS's FORMAT,
  save to it, and read the disk back. *My call:* `ENS_DISK_DIR` points at the user's
  `EPS-ASR10 Discs` folder and the tests name two files in it: the ASR-10 factory disk `AD-001.img`
  (high density, raw) and the EPS-16 PLUS factory disk `ED-001.GKH` (double density, GKH), by their
  paths under that folder. The FORMAT test needs no image.

## 10. Left to the builder

- Which 0x280001 bit or value selects high density, and what the OS does with a disk of the wrong
  density (to be found from the firmware and decided as `T` entries).
- How often the index pulse must come for "as fast as possible" and still satisfy the OS's checks,
  and how it is clocked (CPU cycles, as the DUART inputs already are), within the x2 realtime bench.
- How the `Disk` is handed over (a buffer the message thread allocates per request, or one kept
  ready), as long as the audio thread never allocates.
- What the OS needs from Read ID, Seek and Sense Interrupt Status results to accept the disk.
- The EDE/EDA header details (bitmap offset, block order, the disk-type byte), from the format notes
  in the user's `Tools` folder and the files themselves.
- Panel button sequences for the firmware tests (Musician's Manual, Section 14 and the Storage
  pages).
- Plan order: *my call* is that this becomes Plan 5 and the release folder moves to Plan 6, so the
  first release has a working drive.
