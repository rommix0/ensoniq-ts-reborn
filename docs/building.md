# Building TS Reborn, step by step

This guide starts from nothing and ends with `TS Reborn.exe` and the `TS Reborn.vst3` plugin in a folder you can
use. No programming is needed: you install two free tools, download the code, and run one command. Windows is
first; Linux and macOS are at the end.

## Windows

### 1. Install Visual Studio with C++

1. Download **Visual Studio Community** (free) from <https://visualstudio.microsoft.com/downloads/>. Visual
   Studio 2022 is tested. A newer one should also work.
2. Run the installer. On the **Workloads** tab, tick **Desktop development with C++**. Leave the parts it
   selects by itself as they are (they include "C++ CMake tools for Windows", which this needs).
3. Click **Install** and wait for it to finish (several GB). Restart if it asks.

If you already have Visual Studio, open **Visual Studio Installer** from the Start menu, click **Modify**, and
check that **Desktop development with C++** is ticked.

### 2. Install Git

Download **Git for Windows** from <https://git-scm.com/download/win> and install it. The default answer to every
question in the installer is fine. The build uses Git to download JUCE, the library the app's window and plugin
are made with.

### 3. Download the code

1. Open the Start menu, type **Developer PowerShell**, and open **Developer PowerShell for VS 2022** (or the one
   for your version). Use this window for every step below. A plain PowerShell window may not find CMake.
2. Make a short folder for it and download the code (this also downloads the six ROM files the emulator needs):

   ```powershell
   mkdir C:\src
   cd C:\src
   git clone https://github.com/rommix0/ensoniq-ts-reborn.git
   cd ensoniq-ts-reborn
   ```

   A short path like `C:\src` matters: Windows builds can fail in deep folders, because some file paths
   become too long.

### 4. Build

Still in the same window, in the `ensoniq-ts-reborn` folder, run:

```powershell
powershell -ExecutionPolicy Bypass -File tools\dist.ps1
```

- The first run downloads JUCE (about 150 MB) and then compiles. It takes about 5 to 15 minutes, and prints a
  lot of text. Warnings in that text are normal.
- It is finished when the last line reads `dist: C:\src\ensoniq-ts-reborn\dist\TS Reborn-1.0.0`.

### 5. Run it

Open `C:\src\ensoniq-ts-reborn\dist\TS Reborn-1.0.0` in Explorer.

- **The Standalone app:** double-click `TS Reborn.exe`. On the settings page (the gear on the panel) pick your
  audio output and your MIDI keyboard.
- **The VST3 plugin:** copy the whole `TS Reborn.vst3` folder into `C:\Program Files\Common Files\VST3`, then
  rescan plugins in your DAW. It carries its own ROMs, so it works from there.

The whole `TS Reborn-1.0.0` folder can be moved anywhere. The program finds the ROMs in its `roms` folder.

### Getting a newer version later

In Developer PowerShell:

```powershell
cd C:\src\ensoniq-ts-reborn
git pull
powershell -ExecutionPolicy Bypass -File tools\dist.ps1
```

Later builds are faster: JUCE is already downloaded and only changed files are compiled.

### If something goes wrong

| What you see | What to do |
|---|---|
| `cmake : The term 'cmake' is not recognized` | You are in a plain PowerShell window. Open **Developer PowerShell for VS** (step 3). If that also fails, add "C++ CMake tools for Windows" in the Visual Studio Installer. |
| `could not find any instance of Visual Studio` | The C++ workload is missing. Add **Desktop development with C++** (step 1). |
| `git` is not recognized, or the configure stops with an error naming git | Install Git (step 2), then close and reopen Developer PowerShell. |
| `running scripts is disabled on this system` | Type the whole command from step 4, including `powershell -ExecutionPolicy Bypass -File`. |
| `ROM missing: ...` | The `roms` folder is incomplete. Download the code again with `git clone` (step 3). |
| A build that worked before fails after `git pull` | Delete the `build-dist` folder inside `ensoniq-ts-reborn` and run step 4 again. |
| Errors about paths that are too long | Move the folder to a short path such as `C:\src` (step 3), or delete `build-dist` and build again from there. |

### Why "Open Folder" in Visual Studio is not enough

Visual Studio can open the `ensoniq-ts-reborn` folder and build it, but that builds only the emulator core and
its tests (`ens_tests.exe`, `tsemu.exe`), not the app. The app needs the CMake option `ENS_BUILD_APP=ON`, which
`tools\dist.ps1` sets for you. The commands for developers are in [tsapp.md](tsapp.md#building) and
[AGENTS.md](../AGENTS.md#building).

## Linux (Ubuntu 24.04 or similar)

```bash
sudo apt install build-essential cmake ninja-build git pkg-config libasound2-dev libfreetype-dev \
  libfontconfig1-dev libx11-dev libxcomposite-dev libxcursor-dev libxext-dev libxinerama-dev libxrandr-dev \
  libxrender-dev libgl1-mesa-dev
git clone https://github.com/rommix0/ensoniq-ts-reborn.git
cd ensoniq-ts-reborn
tools/dist.sh
```

The result is `dist/TS Reborn-1.0.0-linux`. Run `TS Reborn` there, or copy `TS Reborn.vst3` to `~/.vst3`.

## macOS

Install Xcode's command-line tools (`xcode-select --install` in Terminal) and CMake (from <https://cmake.org> or
`brew install cmake`). Then:

```bash
git clone https://github.com/rommix0/ensoniq-ts-reborn.git
cd ensoniq-ts-reborn
cmake -S . -B build-mac-app -G "Unix Makefiles" -DCMAKE_BUILD_TYPE=Release -DENS_BUILD_APP=ON
cmake --build build-mac-app --target TSReborn_Standalone
```

The app is `build-mac-app/app/TSReborn_artefacts/Release/Standalone/TS Reborn.app`. On its first start, click the gear on the
panel, then **Choose folder...**, and pick the `roms` folder of the download.
