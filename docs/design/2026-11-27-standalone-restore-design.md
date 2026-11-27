# The Standalone keeps its sampled sounds: design

2026-11-27. Issue #9, item 1: "make the emulator remember and keep previously loaded samples in memory when it
starts up". The user's decisions (2026-11-27): approach A, and the Standalone saves settings that are reloaded at
startup; A121's rule (the Standalone restores no state) no longer holds; a Power cycle button for a clean power-on
(2026-11-27, approved with it).

## Intent

On a real TS-10, sample RAM is lost at power-off. The battery memory keeps the sounds' names, disks and bank
places, and the OS asks "AUTO-LOAD ALL SAMPLED-SOUNDS" at power-on. The VST3 already brings the sounds back
from the project: it records the disk and CD blocks the OS read (A129) and, at a restore, serves them as stand-in
media while the engine answers *YES* (A130, A131). The Standalone records nothing and starts with the controls at
their defaults (A121). The user wants the Standalone to come back as it was left: the sampled sounds loaded, the
sound and the controls as they were.

## Goals

- The Standalone records the media the OS reads, as Project mode does (A129), and keeps them with its state.
- At start it restores that state: the controls, the sound (`select`), and the sampled sounds through the stand-ins,
  with no prompt, as the VST3 does.
- The image files can move or be gone: the record holds the blocks, not the paths.

## Non-goals

- Remembering which disk or CD was in the drive (approach B). The drives start empty, as now.
- Saving sample RAM itself (the OS marks sampled sounds not loaded at every power-on, A124).
- Any change to the VST3's behaviour.

## Anti-goals

- The memory file loses its role: the Standalone's RAM still comes from `ts-memory.bin` / `ts12-memory.bin`
  (A116, A137), saved every 60 s and on close; the state carries no `<RAM>`.
- A state of one model restoring media into the other.
- A broken or missing medium stopping the start: it is skipped as A131 skips one (*SKIP*), the boot goes on.

## Approaches

- **A (chosen by the user).** The Standalone records and restores like the VST3: the record goes into the state
  JUCE's Standalone wrapper already saves on close and reloads at start (`filterState` in `TS Reborn.settings`,
  `StandalonePluginHolder::savePluginState` / `reloadPluginState`).
- B. Remember the image paths and put the media back in the drives at start; the player answers the OS's
  auto-load. Breaks when files move; a sound from a second CD needs a swap by hand.

## The design (A)

- **Recording.** `TsProcessor::capture` and the CD drive's record run in File mode too. The record restarts at each
  restore, as in Project mode, so it holds what the auto-load read plus new reads (A129's trimming).
- **Saving.** `getStateInformation` in File mode writes what it writes now (`model`, `select`, the parameters) plus
  `<Media>` (the record, or `restored_` while a restore is due or running). No `<RAM>`.
- **Restoring.** `setStateInformation` in File mode no longer returns at once. It replaces the parameters and the
  select as in Project mode. With `<Media>`, and the state's `model` the running model, the machine restarts from
  its memory file with the stand-ins (`restored_`, `restoreWanted_`, `restart(Ram::Start, true)`): the RAM is the
  running machine's once booted (a state loaded from JUCE's Options menu mid-session), else the start image the
  constructor read from the memory file. A state of the other model restores the controls only. A state with no
  media keeps the A117 select path.
- **The prompt.** The engine answers the auto-load (`setAnswerAutoLoad`) in File mode only when it has stand-ins
  (A130's *YES*). Without them the Standalone still asks, as the TS does (A124).
- **When it is saved.** On close only, by JUCE's wrapper (A120's window saves it before quitting). After a crash
  the memory file (saved every 60 s) may name sounds the state has no blocks for: the OS asks, as now.
- **Size.** The record grows as Project mode's does, about 1.35 times the bytes read, and it sits base64-encoded
  again in `TS Reborn.settings` (about 1.8 times in all). A full 8 MB of sampled sounds makes that file about
  15 MB, written on close.

- **Power cycle.** A button on the settings page, beside Erase memory, in both modes, after a confirm: the machine
  restarts as a TS switched off and on. The memory is kept (the running RAM once booted, else the start image),
  sample RAM is empty, no stand-ins are given, so the OS asks the auto-load question (the VST3's engine answers
  *NO*, A124). The record and the disk's read marks are cleared, so the next start, or the next project save, has
  no media until new sampled sounds are loaded. The disk and CD stay in their drives. Three levels: nothing (come
  back as left), Power cycle (a clean power-on), Erase memory (factory fresh).

## Decisions to update

- A138 (new): the Standalone's restore and Power cycle.
- A121: superseded. The Standalone restores its state from JUCE's wrapper at start.
- A129: Project and File mode record; File mode's state has `<Media>` and no `<RAM>`.
- A124/A130: the File-mode answer only with stand-ins.
- Checks to change: `standalone_state_before_boot` (now restores), `standalone_has_no_media` (now records).
  New: a File processor that loaded FLUTE 1 (ED-001), its state given to a new File processor on the same memory
  file, restores FLUTE 1 with no prompt and no image in the drive; a TS-12 state's media are not restored into a
  TS-10 Standalone. Power cycle after FLUTE 1 is loaded: the OS asks the auto-load question in File mode, the record
  is empty, and a state taken then has no `<Media>`.

## User to run

- Load a sampled sound in the Standalone, close it, move the disk image away, start it again: the sound plays,
  no prompt.
