param([switch]$Serve)

$extra = @($args)
$ErrorActionPreference = "Stop"
$root   = $PSScriptRoot
$outDir = Join-Path $root "build-wasm"

$emsdk = if ($env:EMSDK) { $env:EMSDK } else { "$env:USERPROFILE\emsdk" }
$emcc  = Join-Path $emsdk "upstream\emscripten\emcc.py"
$toolchain = Join-Path $emsdk "upstream\emscripten\cmake\Modules\Platform\Emscripten.cmake"
if (-not (Test-Path $toolchain)) {
    throw "emsdk not found at $emsdk - clone https://github.com/emscripten-core/emsdk, run 'emsdk install latest' then 'emsdk activate latest', or set EMSDK"
}

$vswhere = "${env:ProgramFiles(x86)}\Microsoft Visual Studio\Installer\vswhere.exe"
if (-not (Test-Path $vswhere)) { throw "vswhere.exe not found - install Visual Studio Build Tools" }
$vsPath = & $vswhere -latest -products * -property installationPath
$cmake  = Join-Path $vsPath "Common7\IDE\CommonExtensions\Microsoft\CMake\CMake\bin\cmake.exe"
$ninja  = Join-Path $vsPath "Common7\IDE\CommonExtensions\Microsoft\CMake\Ninja\ninja.exe"
foreach ($t in @($cmake, $ninja)) {
    if (-not (Test-Path $t)) { throw "missing build tool: $t" }
}

if (-not (Test-Path (Join-Path $root "external\SDL\CMakeLists.txt"))) {
    throw "submodules missing - run: git submodule update --init --recursive"
}

$prev = $ErrorActionPreference
$ErrorActionPreference = 'Continue'

& $cmake -S $root -B $outDir -G Ninja `
    "-DCMAKE_MAKE_PROGRAM=$ninja" `
    "-DCMAKE_TOOLCHAIN_FILE=$toolchain" `
    "-DCMAKE_BUILD_TYPE=Release" @extra 2>&1 | ForEach-Object { "$_" }
$rc = $LASTEXITCODE
if ($rc -eq 0) {
    & $cmake --build $outDir --parallel 2>&1 | ForEach-Object { "$_" }
    $rc = $LASTEXITCODE
}
$ErrorActionPreference = $prev
if ($rc -ne 0) { throw "wasm build failed ($rc)" }

Write-Host ""
Write-Host "built: $outDir\showcase.html (plus simple.html and notepad.html)"
Write-Host "a file:// page cannot fetch the .wasm, so serve it:"
Write-Host "  python -m http.server -d build-wasm 8000"

if ($Serve) {
    & (Join-Path $emsdk "python\3.13.3_64bit\python.exe") -m http.server -d $outDir 8000
}
