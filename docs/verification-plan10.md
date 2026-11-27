# Verification: Plan 10 (sending .syx files)

Branch `plan10-syx`, 2026-11-26. No written plan: the work item is "Sysex support for the plugin" from the user's
to-do list. Decision A136 (new).

## What changed

- **Send SysEx File...** in the disk drive's menu, and a `.syx` dropped on the panel, send the file into MIDI IN at
  the wire's pace, one message at a time with 100 ms after each, ahead of the host's bytes. While it goes the item
  reads **Stop Sending SysEx (n%)**.
- `Engine::midiWaiting()`: the MIDI-in bytes not yet read by the OS (the engine's store and DUART A's wire queue).

## Toolchains and suites

| Toolchain | `ens_tests` | `ens_rt_tests` | App checks |
|---|---|---|---|
| MinGW-w64 i686 (`build`) | 426 passed, 0 failed | 7 passed, 0 failed | n/a |
| MSVC x64 (`build-msvc`, `build-app`) | 426 passed, 0 failed | 7 passed, 0 failed | `app_tests: all passed` |
| Linux GCC x64 in WSL (`build-linux`, `build-linux-app`) | 426 passed, 0 failed | 7 passed, 0 failed | `app_tests: all passed` |

- New app check sysexFile: an Identity Request after 2000 bytes of another maker's sysex is answered; 82% has gone at
  200 ms (the first run, before `midiWaiting` counted DUART A's queue, showed 100%: the whole file was handed over at
  once); a file not starting with F0 and a second send are refused; Stop ends a send.
- No `skip:` line from `ens_tests` on MinGW.

## Reference hashes

Unchanged and identical on MinGW i686, MSVC x64 and Linux GCC: boot trace `ba7e34a4cf23d1e0`; renders
`23b6485893beb761`, `0aa23b0ebbdeeec6`, `e9d27e034e29b527`.

## A real factory file

Probe runs (not committed: the file is outside the image folder) with the user's TSD-1000.syx and SYS-EX=ON: its
first four messages (two program banks, two preset banks) queued back to back drew one NAK from the OS and lost a
bank; sent one at a time, each after the last was read, none, and both program banks loaded (U0 GET-TECHNO..., U1
AC-BS/PIANO...). That is why the sender waits after each F7.

## User to run

- With SYS-EX=ON, send a TSD or TSDEMO file from the menu and check the banks (and a demo's sequences) load; it
  takes about 90 s for a 300 KB file.
- Stop a send halfway: the OS shows a receive error or nothing, and keeps working.

## Open

- A send stopped while the audio device is stopped finishes only when the audio runs again.
