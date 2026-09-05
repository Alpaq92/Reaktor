# build.ps1 - convenience wrapper around CMake on Windows.
#
# All build logic lives in CMakeLists.txt; this only locates the MSVC
# environment and the CMake/Ninja that ship inside VS Build Tools, so nothing
# has to be installed or added to PATH.
#
# On macOS, Linux and the BSDs skip this entirely:
#     cmake -S . -B build && cmake --build build --parallel
# -Target builds one CMake target instead of everything, which is what you
# want while a single library is being brought up.
param([string]$Target = "")

$ErrorActionPreference = "Stop"
$root = $PSScriptRoot
$outDir = Join-Path $root "build"

$vswhere = "${env:ProgramFiles(x86)}\Microsoft Visual Studio\Installer\vswhere.exe"
if (-not (Test-Path $vswhere)) { throw "vswhere.exe not found - install Visual Studio Build Tools" }
$vsPath = & $vswhere -latest -products * -requires Microsoft.VisualStudio.Component.VC.Tools.x86.x64 -property installationPath
if (-not $vsPath) { throw "MSVC C/C++ toolset not installed (need 'Desktop development with C++')" }

$vcvars = Join-Path $vsPath "VC\Auxiliary\Build\vcvars64.bat"
$cmake  = Join-Path $vsPath "Common7\IDE\CommonExtensions\Microsoft\CMake\CMake\bin\cmake.exe"
$ninja  = Join-Path $vsPath "Common7\IDE\CommonExtensions\Microsoft\CMake\Ninja\ninja.exe"
foreach ($t in @($vcvars, $cmake, $ninja)) {
    if (-not (Test-Path $t)) { throw "missing build tool: $t" }
}

if (-not (Test-Path (Join-Path $root "third_party\SDL\CMakeLists.txt"))) {
    throw "submodules missing - run: git submodule update --init --recursive"
}

# A temp .bat keeps vcvars and cmake in one cmd session, and avoids PowerShell
# mangling the quoting of paths that contain spaces.
$bat = Join-Path $env:TEMP "curie_build.bat"
@(
  '@echo off',
  ('call "' + $vcvars + '" >nul 2>&1'),
  ('"' + $cmake + '" -S "' + $root + '" -B "' + $outDir + '" -G Ninja ' +
   '-DCMAKE_MAKE_PROGRAM="' + $ninja + '" -DCMAKE_BUILD_TYPE=Release'),
  'if errorlevel 1 exit /b 1',
  ('"' + $cmake + '" --build "' + $outDir + '" --parallel' +
   $(if ($Target) { ' --target ' + $Target } else { '' })),
  'exit /b %ERRORLEVEL%'
) | Set-Content -Path $bat -Encoding ascii

# PowerShell 5.1 turns a native command's stderr into ErrorRecords, so a mere
# CMake *warning* would trip $ErrorActionPreference='Stop'. Exit code is truth.
$prev = $ErrorActionPreference
$ErrorActionPreference = 'Continue'
& cmd.exe /c $bat
$rc = $LASTEXITCODE
$ErrorActionPreference = $prev

if ($rc -ne 0) { throw "build failed ($rc)" }
Write-Host ""
if ($Target) { Write-Host "built target: $Target" }
else { Write-Host "built: $outDir\curie.exe" }
