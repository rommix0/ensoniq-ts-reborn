# TS Reborn SCSI and CD-ROM (Plan 7)

Approved by the user on 2026-11-15 (after the builder check). Built on branch `plan7-scsi`; checked in
`docs/verification-plan7.md`. Every section below is done except the "User to run" item under Checks.

Calls made in this design, not by the user (each can be overturned): the SP-4 always fitted in the app and
off in the test machine; Standalone and VST3 alike leave the CD out of the state; selection, command and
status answer at once and the Timeout Period is not modelled; floppy blocks kept as first read; a missing
`.cue` FILE path falls back to the `.cue`'s folder; with no matching saved source the app presses *SKIP*.

## Intent

The real TS-10 could take the SP-4 SCSI kit and read sampled sounds from Ensoniq CD-ROMs and other SCSI
storage (Musician's Manual, Section 13, "SCSI Option (read-only)"). The user has 16 Ensoniq CD-ROM images
and wants to load sounds from them in TS Reborn.

The user's second aim is the VST3 project. Sample RAM is lost at every power-on, and the OS reloads what
was loaded only by asking "AUTO-LOAD ALL SAMPLED-SOUNDS" and reading the disks again. Today the VST3 answers
*NO* (A124), so a project reopens with every sampled sound gone, whether it came from a floppy or a CD. The
user's idea (2026-11-15): a project keeps only what the loaded sounds need, never whole disk or CD images.

The user's decisions (2026-11-15):

- Projects keep sounds loaded from the floppy and from SCSI.
- One CD-ROM drive at SCSI ID 4 (Ensoniq's rule for its CD-ROMs, Musician's Manual Section 13), reached from
  a "CD-ROM" submenu of the floppy drive's menu.
- The CD drive reads the image on demand from another thread; the image is never loaded whole.
- A project carries every block the OS read since the instance started or was last restored, browsing
  included. It is trimmed to the loaded sounds only when the project is reopened (the auto-load reads just
  what it needs). No size cap.

## What the firmware does (found 2026-11-15)

From the OS v3.10 disassembly, the WD33C93B datasheet (`datasheets/WD_WD33C93B.pdf`) and the service manual:

- **The chip.** A WD33C93 on the SP-4 board (4090020601, 34-pin ribbon to J9). I/O1 n = 1: 0x240001 is the
  address port (write) and the Auxiliary Status register (read); 0x240003 is the register port. The OS uses
  indirect addressing: every access at 0x240003 steps the address register, except at the Command (0x18)
  and Data (0x19) registers (datasheet 3.1.2).
- **No interrupt, no DMA.** Interrupt levels 2 and 4-7 all go to the fatal-exception path (`c00d62`, code
  0x8B), so the chip's INTRQ cannot reach the CPU. The OS polls Auxiliary Status: bit 7 INT, bit 6 LCI, bit 0
  DBR. Control register = 0x00 (polled I/O). Every data byte goes through the Data register.
- **Boot.** Probe (`c0f820`): register 2 written with 0x00, 0x55, 0xAA, 0xFF and read back, the address
  rewritten before each read. On a match (`c0f7d4`): Own ID = 3, command Reset (0x00), wait 15 ms, poll INT,
  read SCSI Status (0x17) to clear it; `$12b6` bit 3 set, which shows SCSI on the STORAGE page. Today the
  block reads 0 (T12) and the probe fails at 0x55.
- **Commands.** Each SCSI command (`c1563e`): Control 0 and Timeout 0x0D, the CDB into registers 3.., Target
  LUN 0, Synchronous 0, the 24-bit Transfer Count, Destination ID (`$f5` - 1), Source ID 0, then
  Select-and-Transfer without ATN (0x09). Data moves byte by byte while DBR is set; the OS checks INT before
  each byte and stops reading when it is set. CDBs used: TEST UNIT READY (00), REQUEST SENSE (03, 14 bytes),
  INQUIRY (12, 0x24 bytes), MODE SELECT(6) (15), MODE SENSE(6) (1A), READ CAPACITY (25), READ(10) (28),
  WRITE(10) (2A, present but not on any TS path found).
- **Finish.** The OS expects interrupt 0x16 (Select-and-Transfer done) with Command Phase 0x60 and the
  target's status byte in register 0x0F, then, after it reads SCSI Status, a second interrupt 0x85 when the
  bus goes free (datasheet 4.6.1). 0x42 (selection timeout) means no device at that ID (shown INVALID). A
  status byte 2 (CHECK CONDITION) is followed by REQUEST SENSE; sense key 6 with ASC 0x28 is handled as a
  media change (`c154cc`).
- **Mount** (`c1548c`): ID 3 refused (the TS's own ID); bus reset; TEST UNIT READY; INQUIRY. Type 0 (disk):
  READ CAPACITY, block length must be 512. Type 5 (CD-ROM): MODE SELECT(6) setting 512-byte blocks
  (block descriptor: density 0, 0 blocks, length 0x200). Then the directory is read with the floppy's code
  (`c0e372`).
- **One block path.** All block reads go through `c0f43e`: `$f5` = 0 is the floppy, otherwise SCSI ID
  `$f5` - 1 (`c15a22`, READ(10) of 512-byte blocks, up to 5 tries). The Ensoniq file system code is shared;
  the emulated drive only serves blocks.

### Auto-load check (floppy)

A throwaway test (2026-11-15, removed) loaded ED-001's FLUTE 1 into S8-0: the load read 314 of the disk's
1600 blocks (the sound's 308 plus directory and file table). A power-on from that battery RAM with ED-001 in,
answered *YES*, read the same 314 blocks and no other. A disk holding only those 314 blocks (the rest zero)
brought FLUTE 1 back and C-E-G played sample-for-sample the same as from the full disk. *YES* is Soft3
(row 0, columns 35-39); *NO * is Soft6.

## Goals

- **SCSI in the machine.** A WD33C93 core and a CD-ROM target at ID 4 in `src/ts`, fitted by a `Machine`
  option (default off, like `sqx70`). The app always fits it.
- **CD images.** `.iso` (2048-byte sectors) and `.cue`/`.bin` with one MODE1/2352 data track (all 16 of the
  user's CDs), read on demand by a non-audio thread. Read-only: the image file is never written.
- **The CD drive in the app.** A "CD-ROM (SCSI ID 4)" submenu in the floppy drive's menu with Insert CD
  Image... and Eject CD; the inserted image's name shown in that submenu. A `.iso` or `.cue` file dropped on
  the panel goes to the CD drive (other files to the floppy, as today).
- **Projects keep loaded sounds (VST3).** Each drive notes the blocks the OS reads; a Project state stores
  them per source; on restore the app puts stand-in media holding only those blocks where the OS looks, and
  answers the auto-load prompts.
- **Tools.** `tsemu` takes `--cd <image>` (and fits SCSI with it).

## Non-goals

- No hard-drive or MO images, no SCSI IDs other than 4, no settings-page SCSI section.
- No writing to SCSI. WRITE(10) gets CHECK CONDITION, DATA PROTECT.
- No audio CD playback. A CD image with no data track is refused at insert.
- No change to the Standalone's state rules (A121): the Standalone still asks auto-load, and *YES* works when
  the right floppy and CD are in.
- Neither the floppy nor the CD in the drive becomes part of the state (A119 stays).
- No whole-machine snapshot (CPU and chips saved). Saving sample RAM itself would not help: the OS rebuilds
  its sample bookkeeping at every power-on.
- No firmware change. The reference hashes stay as they are (the default machine has no SCSI).

## Anti-goals

- The audio thread never waits on a file and never allocates (AGENTS.md; `ens_rt_tests` stays at 0).
- A project never holds a whole disk or CD image.
- The user's image files are never modified.

## Approaches

How the CD's blocks reach the emulated drive:

1. **Read on demand (chosen by the user).** The drive asks a reader thread for a block and reports busy (DBR
   clear, no interrupt) until the data arrives, as a real drive seeks. Memory stays small for any CD size and
   any number of plugin instances; insert is instant; the project's saved blocks become one more block
   source. Costs a "not ready yet" state in the chip and a hand-over between threads.
2. **Whole image in memory**, as the floppy. Simplest code, but 120-690 MB per CD and per instance, and a
   second or more per insert.
3. **Memory-mapped file.** Rejected: the first touch of a page blocks the audio thread on the disk.

How projects keep sounds:

1. **Blocks the OS read, replayed through its own auto-load (chosen).** Proven on the floppy above.
2. **Sample RAM saved and restored.** Rejected: the OS treats sample RAM as lost at power-on and asks to
   reload anyway.
3. **A full machine snapshot.** Rejected for now: every core would need saving and loading, far beyond this
   plan.

## The machine (`src/ts`)

- **WD33C93 core.** Registers 0x00-0x1A as the datasheet's map; indirect addressing with the step rule above;
  Auxiliary Status read at the address port. Commands needed: Reset (0x00; status 0x00 with no advanced
  features, as Own ID = 3 gives) and Select-and-Transfer (0x08, 0x09). Others: "invalid command" (0x40)
  unless the firmware is found to use them. LCI when a command is written while INT is set (datasheet 3.1.1).
- **Select-and-Transfer** runs as a small state machine driven by the target: selection (no device at the
  ID: 0x42, Command Phase 0x00), command, data in or out (DBR per byte; the Transfer Count counts down), status
  (byte into 0x0F), command complete (Command Phase 0x60, interrupt 0x16), then bus free (interrupt 0x85 once
  SCSI Status has been read). Reading SCSI Status clears INT.
- **Timing.** Selection, command and status complete at once. The data phase waits only when the target has
  no data yet (a block still on its way from the reader thread). The Timeout Period register is not
  modelled; a missing ID answers at once. Recorded as decisions.
- **CD-ROM target (ID 4).** INQUIRY: type 5, removable, vendor and product strings of a drive Ensoniq listed
  (service manual p. 62: Toshiba or Sony mechanisms; the builder picks one and records it). TEST UNIT READY:
  GOOD with a disc; NOT READY (2/0x3A) with none. After a disc change one UNIT ATTENTION (6/0x28). MODE
  SELECT(6) sets the block length (512 or 2048 accepted; the CD starts at 2048, as real drives). MODE
  SENSE(6) answers the block descriptor. READ CAPACITY in the current block length. READ(10) in the current
  block length. Data the target does not have is padded to the Transfer Count. WRITE(10): DATA PROTECT.
- **Block source.** The target asks for 2048-byte sectors through a fixed hand-over (request slot and reply
  slot, no allocation, no lock on the audio side), filled by whoever owns the image. A test source can answer
  in the same call, so runs are deterministic. No file I/O under `src/` (AGENTS.md).
- **Recording.** Each drive notes the blocks the OS reads: the floppy per sector it reads, the CD per sector
  served. On the audio side this is a preallocated bitmap per inserted medium (a CD of 690 MB is about 340 000
  sectors, 42 KB of bits); the bytes are gathered off the audio thread (below).
- `Machine::create(..., scsi = false)`; `tsemu --cd` and the app pass true. T12 stays for the machine
  without the board.

## The app (`app/`)

- **CD image reader.** Opens `.iso` (size a multiple of 2048) or `.cue` (parses FILE, one TRACK 01
  MODE1/2352, INDEX 01 00:00:00) and reads 2048-byte sectors from the right offsets (2352-byte sector: 16
  header bytes, then 2048 data). A FILE path that does not exist falls back to that file name in the `.cue`'s
  own folder, matched without case (CDR-13's `.cue` names `E:\ENSONIQ-CDR-13-PERCUSSION-INVISION\DLM-CDR13.BIN`).
  Other cue layouts are refused with a message naming what is supported. Reads happen on its own thread.
- **The CD drive** (beside `DiskDrive`): insert, eject, the current file's name, the reader thread. Insert
  and eject reach the engine through the same single-slot hand-over style as the floppy (A118).
- **Menu and drop.** The submenu and the drop rule above. The panel is unchanged (the real TS-10 has only the
  rear SCSI connector).
- **What a Project state stores.** For each source the OS loaded from since the instance started (or since
  the last restore): its kind (floppy DD or HD, or CD at ID 4), its label as the OS sees it (the disk or
  volume name in the ID block), and the recorded blocks with their bytes. The bytes are the ones the OS read,
  kept from the first capture on: a later write by the OS to the same block does not replace them. CD bytes
  are kept by the reader thread as it serves them. Floppy bytes are captured off the audio thread: on the
  first tick (30 Hz) after the drive records new blocks, the app asks for a copy of the disk (A118) and keeps
  the new blocks from it, and again when the disk leaves the drive. A write to a just-read block in the few
  frames before that copy is the one way a saved block can differ from what was read (builder's call to
  close it if it is cheap). Stored as a child of the existing `TSState` element, next to `<RAM>`.
- **Restore.** The engine answers the power-on question *YES* (Soft3) instead of *NO* when the state holds
  sources, and *NO * as today when it holds none. Before the boot it puts the first floppy source in the drive
  and the first CD source in the CD drive, as stand-ins built from the saved blocks (unsaved blocks read as
  zero). When the OS asks for another disk or volume by name, the engine swaps in the stand-in with that label
  and presses *OK*; when no saved source matches, *SKIP*. The manual says the OS remembers each sound by
  disk or volume name, path and SCSI ID, and asks for a named SCSI volume by name (Section 14, "About the
  Auto-Load Feature", prompts 3 and 4), so two CDs at ID 4 are told apart by their prompts. The plan checks
  this early with two CDs; if the OS turns out not to ask, a project keeps the CD source loaded from last and
  the decision entry says so. Once the auto-load ends, the stand-ins come out and
  the drives go back to what they held. The record then starts again from what the auto-load read, so it
  sheds blocks of sounds no longer loaded.
- **Standalone.** It records nothing and stores nothing (A121); the CD drive and the prompts work as on the
  real instrument.

## Decisions to record

- T entries (from T25): the SCSI block and the WD33C93 core (fitted, address and data ports, the step rule),
  no INTRQ wiring, the Select-and-Transfer timing, the CD target's answers, the block hand-over.
- A entries (from A127): the CD image formats, the CD drive and its menu, what a Project state stores, the
  auto-load answers on restore (an update to A124), the stand-ins.
- T12 updated: it holds for a machine without the board.

## Checks

- **Unit tests, no ROMs:** the WD33C93 register map and step rule; the probe's write and read-back; Reset
  status; Select-and-Transfer with a test target: GOOD with data in, CHECK CONDITION then REQUEST SENSE, a
  missing ID (0x42), the 0x16 then 0x85 order; DBR held clear while a block is pending; the cue parser (good
  and refused layouts) and sector offsets on small synthetic files.
- **Firmware tests (need the ROMs and `ENS_DISK_DIR`):** with SCSI fitted the STORAGE page shows SCSI; with
  one of the user's CDs at ID 4 the SCSI STATUS page shows the volume name, the Load page lists the root, and
  a sampled sound loads into S8-0 and plays from sample RAM (as `ts_loads_eps_instrument`); with no disc,
  INVALID or STANDBY as the manual describes.
- **Auto-load replay:** for the floppy (the check above, kept as a test) and for a CD sound: a power-on from
  the RAM with stand-ins built from the recorded blocks restores the sound, with audio identical to the full
  medium.
- **App checks:** a Project state round trip restores a sound loaded from a floppy and one from a CD, with no
  image file present at restore; the state holds blocks only of media the OS read; after a restore and a new
  save it holds only the blocks the auto-load read; the Standalone state has none; insert and eject through the menu's functions; a dropped `.cue` goes to the CD drive.
- **Real time:** `ens_rt_tests` with a CD inserted and a load running: 0 allocations.
- All suites on MinGW, MSVC and Linux; reference hashes unchanged.
- User to run: insert a CD in the Standalone, browse and load; save a VST3 project with sounds from a floppy
  and a CD, reopen it with the images moved away.

## Docs

`docs/ts-hardware-interface.md` (the SCSI section), `docs/tsapp.md` (the CD drive, what projects keep),
`AGENTS.md` (`tsemu --cd`, the CD tests and `ENS_DISK_DIR`), `README.md` (status), `docs/decisions.md`,
`docs/verification-plan7.md`.

## Left to the builder

- The exact display text of the OS's insert-disk prompts and the soft buttons under *OK* and *SKIP*; find
  them by running the firmware (the strings are built from pieces near 0xc20a61).
- Where the label sits in the ID block, and what the OS reads to name a volume; how an unnamed source is
  matched (by the file name the prompt shows).
- The INQUIRY strings, within the rule above.
- The size of the reader thread's cache and its read-ahead.
- The exact XML layout of the saved sources and its encoding.
- Whether MODE SENSE page 0x2E (asked only of a drive named "I325VM") needs anything.
