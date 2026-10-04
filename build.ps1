param([string]$Target = "", [ValidateSet("", "x64", "x86", "arm64")][string]$Arch = "")

$extra = @($args)
$ErrorActionPreference = "Stop"
$root = $PSScriptRoot
$hostArch = switch ([System.Runtime.InteropServices.RuntimeInformation]::OSArchitecture) {
    'Arm64' { 'arm64' }
    'X86'   { 'x86' }
    default { 'x64' }
}
if (-not $Arch) { $Arch = $hostArch }
$outDir = Join-Path $root $(if ($Arch -eq $hostArch) { "build" } else { "build-$Arch" })

$vswhere = "${env:ProgramFiles(x86)}\Microsoft Visual Studio\Installer\vswhere.exe"
if (-not (Test-Path $vswhere)) { throw "vswhere.exe not found - install Visual Studio Build Tools" }
$component = if ($Arch -eq 'arm64') { 'Microsoft.VisualStudio.Component.VC.Tools.ARM64' }
             else { 'Microsoft.VisualStudio.Component.VC.Tools.x86.x64' }
$vsPath = & $vswhere -latest -products * -requires $component -property installationPath
if (-not $vsPath) { throw "MSVC toolset for $Arch not installed (need 'Desktop development with C++')" }

$vcvars = Join-Path $vsPath "VC\Auxiliary\Build\vcvarsall.bat"
$vcarch = if ($Arch -eq $hostArch) { $Arch } else { "$($hostArch)_$Arch" }
$cmake  = Join-Path $vsPath "Common7\IDE\CommonExtensions\Microsoft\CMake\CMake\bin\cmake.exe"
$ninja  = Join-Path $vsPath "Common7\IDE\CommonExtensions\Microsoft\CMake\Ninja\ninja.exe"
if (-not (Test-Path $cmake) -and (Get-Command cmake -ErrorAction SilentlyContinue)) {
    $cmake = (Get-Command cmake).Source
}
if (-not (Test-Path $ninja) -and (Get-Command ninja -ErrorAction SilentlyContinue)) {
    $ninja = (Get-Command ninja).Source
}
foreach ($t in @($vcvars, $cmake, $ninja)) {
    if (-not (Test-Path $t)) { throw "missing build tool: $t" }
}

if (-not (Test-Path (Join-Path $root "external\SDL\CMakeLists.txt"))) {
    throw "submodules missing - run: git submodule update --init --recursive"
}

# One per run: cmd reads a batch file as it goes, and other checkouts build too.
$bat = Join-Path $env:TEMP "reaktor_build_$PID.bat"
@(
  '@echo off',
  ('call "' + $vcvars + '" ' + $vcarch + ' >nul 2>&1'),
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
try {
    & cmd.exe /c $bat
    $rc = $LASTEXITCODE
} finally {
    $ErrorActionPreference = $prev
    Remove-Item -LiteralPath $bat -ErrorAction SilentlyContinue
}

if ($rc -ne 0) { throw "build failed ($rc)" }
Write-Host ""
if ($Target) { Write-Host "built target: $Target ($Arch)" }
else { Write-Host "built: $outDir\showcase.exe (plus simple.exe and notepad.exe), $Arch" }
