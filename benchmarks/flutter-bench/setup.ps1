# Generates the platform runner for the Flutter arm and sizes its window to the
# 800x600 the other four draw into.
#
# The runner is not checked in: `flutter create` writes it, and the size lives
# in its C++ rather than in any Dart this benchmark controls. So the size is a
# patch applied after the generation, and re-running this is harmless.
param([string]$Platform = "windows")

$ErrorActionPreference = "Stop"
$here = $PSScriptRoot
$dart = Join-Path $here "lib\main.dart"

if (-not (Get-Command flutter -ErrorAction SilentlyContinue)) {
    throw "flutter is not on PATH"
}

# `flutter create` regenerates a project's scaffolding, and the benchmark is
# scaffolding-shaped: one main.dart. Keep a copy rather than find out.
$saved = Get-Content $dart -Raw

& flutter create --platforms=$Platform --project-name reaktor_bench $here
if ($LASTEXITCODE -ne 0) { throw "flutter create failed" }

if ((Get-Content $dart -Raw) -ne $saved) {
    Set-Content -Path $dart -Value $saved -NoNewline
    Write-Host "lib\main.dart was regenerated; the benchmark was put back"
}

if ($Platform -ne "windows") {
    Write-Host "runner generated. The window size lives in that platform's runner;"
    Write-Host "the size line in the app's output says what it came out as."
    exit 0
}

$main = Join-Path $here "windows\runner\main.cpp"
if (-not (Test-Path $main)) { throw "generated runner not found: $main" }

$text = Get-Content $main -Raw
if ($text -notmatch 'Win32Window::Size\s+size\([^)]*\);') {
    throw "could not find the window size in $main"
}
$sized = [regex]::Replace($text, 'Win32Window::Size\s+size\([^)]*\);',
                          'Win32Window::Size size(800, 600);')

# `flutter create` titles the window after the package, so this one comes out
# called reaktor_bench. Every other arm names the framework that is drawing -
# Reaktor, Electron, Kotlin Multiplatform - and a shot of the five is
# unreadable if one of them names the benchmark instead.
if ($sized -notmatch 'window\.Create\(L"[^"]*",') {
    throw "could not find the window title in $main"
}
$sized = [regex]::Replace($sized, 'window\.Create\(L"[^"]*",', 'window.Create(L"Flutter",')

Set-Content -Path $main -Value $sized -NoNewline

Write-Host "window sized to 800x600 and titled Flutter in windows\runner\main.cpp"

# That size is the outer window: the template hands it straight to CreateWindow,
# which leaves a client area smaller by the frame, and the client area is what
# gets drawn. Growing the rect by the frame is the whole fix, and it is done by
# patching rather than by asking, because Flutter has no API for it.
$win32 = Join-Path $here "windows\runner\win32_window.cpp"
$adjusted = $false

if (Test-Path $win32) {
    $text = Get-Content $win32 -Raw

    if ($text -match 'AdjustWindowRect') {
        $adjusted = $true
    } else {
        $grown = $text -replace 'Scale\(size\.width, scale_factor\), Scale\(size\.height, scale_factor\),',
                                'frame.right - frame.left, frame.bottom - frame.top,'
        if ($grown -ne $text) {
            $insert = @(
                '$1// Patched by benchmarks/flutter-bench/setup.ps1: the template asks'
                '$1// for an outer window of this size, and the client area is'
                '$1// what gets drawn.'
                '$1RECT frame = {0, 0, Scale(size.width, scale_factor),'
                '$1              Scale(size.height, scale_factor)};'
                '$1AdjustWindowRect(&frame, WS_OVERLAPPEDWINDOW, FALSE);'
                ''
                '$1HWND window = CreateWindow('
            ) -join "`r`n"

            $grown = $grown -replace '(?m)^(\s*)HWND window = CreateWindow\(', $insert
            Set-Content -Path $win32 -Value $grown -NoNewline
            $adjusted = $true
            Write-Host "client area grown by the window frame in windows\runner\win32_window.cpp"
        }
    }
}

if (-not $adjusted) {
    Write-Host ""
    Write-Host "warning: could not grow the client area by the window frame."
    Write-Host "The app will draw smaller than 800x600. The size line it prints"
    Write-Host "is what it actually drew at, and run.ps1 warns on it."
}

Write-Host ""
Write-Host "now: flutter build windows --release"
