# Builds the TS Reborn Standalone and VST3 (MSVC x64, Release, static C++ runtime) in build-dist and
# assembles "dist\TS Reborn-<version>": TS Reborn.exe, the TS Reborn.vst3 package, README.md with panel.png,
# LICENSE, the app guide, and the six ROM files the app loads, in roms\ beside the exe and again inside the VST3 package
# (Contents\Resources\roms), so the VST3 finds them wherever it is installed. DECISION A126.
# The ROMs are Ensoniq's.
#
#   powershell -ExecutionPolicy Bypass -File tools\dist.ps1 [-RomFolder <folder>] [-JuceSource <JUCE checkout>]
#
# -RomFolder defaults to the repository's roms folder. Without -JuceSource the first configure downloads JUCE
# (app\CMakeLists.txt) into build-dist\_deps.
param([string]$RomFolder, [string]$JuceSource)
$ErrorActionPreference = 'Stop'
$root = Split-Path -Parent $PSScriptRoot
Set-Location $root
if (-not $RomFolder) { $RomFolder = Join-Path $root 'roms' }

$version = [regex]::Match((Get-Content app\CMakeLists.txt -Raw), 'VERSION (\d+\.\d+\.\d+)').Groups[1].Value
if (-not $version) { throw 'No VERSION in app\CMakeLists.txt' }

# The six files RomFolder.cpp loads, with their sizes: checked before anything is built or written.
$roms = [ordered]@{
    'os\ts10_310h.bin' = 262144; 'os\ts10_310l.bin' = 524288
    'waves\901401_u41.bin' = 2097152; 'waves\901301_u43.bin' = 1048576
    'waves\901402_u45.bin' = 2097152; 'waves\901302_u46.bin' = 1048576
}
foreach ($f in $roms.Keys) {
    $src = Join-Path $RomFolder $f
    if (-not (Test-Path -LiteralPath $src -PathType Leaf)) { throw "ROM missing: $f in $RomFolder" }
    $size = (Get-Item -LiteralPath $src).Length
    if ($size -ne $roms[$f]) { throw "ROM $f is $size bytes, not $($roms[$f])" }
}

if (-not (Test-Path build-dist\CMakeCache.txt)) {
    # The newest Visual Studio with the C++ tools, named explicitly: a CMake's own default can be Ninja or an older
    # Visual Studio. 17 is Visual Studio 2022; a later one gives its own generator name ("Visual Studio 18 2026").
    $vswhere = "${env:ProgramFiles(x86)}\Microsoft Visual Studio\Installer\vswhere.exe"
    $ask = { param($p) if (Test-Path -LiteralPath $vswhere) {
        & $vswhere -latest -products * -requires Microsoft.VisualStudio.Component.VC.Tools.x86.x64 -property $p } }
    $major = "$(& $ask installationVersion)".Split('.')[0]
    $year = "$(& $ask catalog_productLineVersion)"
    if (-not $major -or -not $year) { throw 'No Visual Studio with C++ found: install its "Desktop development with C++" workload' }
    $gen = "Visual Studio $major $year"
    $cfg = @('-S', '.', '-B', 'build-dist', '-G', $gen, '-A', 'x64', '-DENS_BUILD_APP=ON',
             '-DCMAKE_MSVC_RUNTIME_LIBRARY=MultiThreaded')  # no Visual C++ redistributable needed
    if ($JuceSource) { $cfg += "-DFETCHCONTENT_SOURCE_DIR_JUCE=$JuceSource" }
    & cmake @cfg
    if ($LASTEXITCODE) { throw 'configure failed' }
}
& cmake --build build-dist --config Release --target TSReborn_Standalone TSReborn_VST3
if ($LASTEXITCODE) { throw 'build failed' }

$art = 'build-dist\app\TSReborn_artefacts\Release'
$out = "dist\TS Reborn-$version"
if (Test-Path -LiteralPath $out) { Remove-Item -LiteralPath $out -Recurse -Force }
New-Item -ItemType Directory -Force $out | Out-Null
Copy-Item -LiteralPath "$art\Standalone\TS Reborn.exe" $out
Copy-Item -LiteralPath "$art\VST3\TS Reborn.vst3" $out -Recurse
Copy-Item -LiteralPath README.md, panel.png, LICENSE $out
Copy-Item -LiteralPath docs\tsapp.md "$out\APP-GUIDE.md"
foreach ($dest in "$out\roms", "$out\TS Reborn.vst3\Contents\Resources\roms") {
    foreach ($f in $roms.Keys) {
        $dst = Join-Path $dest $f
        New-Item -ItemType Directory -Force (Split-Path $dst) | Out-Null
        Copy-Item -LiteralPath (Join-Path $RomFolder $f) $dst
    }
}
Write-Host "dist: $root\$out"
