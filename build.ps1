param([string]$Target = "")

$extra = @($args)
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

if (-not (Test-Path (Join-Path $root "external\SDL\CMakeLists.txt"))) {
    throw "submodules missing - run: git submodule update --init --recursive"
}

$bat = Join-Path $env:TEMP "reaktor_build.bat"
@(
  '@echo off',
  ('call "' + $vcvars + '" >nul 2>&1'),
  ('"' + $cmake + '" -S "' + $root + '" -B "' + $outDir + '" -G Ninja ' +
   '-DCMAKE_MAKE_PROGRAM="' + $ninja + '" -DCMAKE_BUILD_TYPE=Release' +
   (($extra | ForEach-Object { ' "' + $_ + '"' }) -join '')),
  'if errorlevel 1 exit /b 1',
  ('"' + $cmake + '" --build "' + $outDir + '" --parallel' +
   $(if ($Target) { ' --target ' + $Target } else { '' })),
  'exit /b %ERRORLEVEL%'
) | Set-Content -Path $bat -Encoding ascii

$prev = $ErrorActionPreference
$ErrorActionPreference = 'Continue'
& cmd.exe /c $bat
$rc = $LASTEXITCODE
$ErrorActionPreference = $prev

if ($rc -ne 0) { throw "build failed ($rc)" }
Write-Host ""
if ($Target) { Write-Host "built target: $Target" }
else { Write-Host "built: $outDir\showcase.exe (plus simple.exe and notepad.exe)" }
