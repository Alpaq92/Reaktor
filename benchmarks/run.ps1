# Runs the arms of the comparison and measures them all the same way.
#
# The frame counters come from each app's own stdout. Memory and CPU do not:
# they are sampled from outside, over the whole process tree, because Electron
# is five processes and a JVM's idea of its own memory is not the operating
# system's. One sampler for all five is the only way the bars mean anything
# against each other.
#
# Nothing is built here. Each arm points at an artifact that its own toolchain
# produces, and an arm whose artifact is missing is skipped with the command
# that would make it.
param(
    [string[]] $Targets = @("reaktor-software", "reaktor-d3d11",
                            "reaktor-opengl", "shaft", "flutter", "electron",
                            "kmp"),
    [double]   $Seconds = 6,
    [double]   $Fps = 60,
    [int]      $Boxes = 64,
    [int]      $Runs = 10,
    [string]   $Csv = ""
)

$ErrorActionPreference = "Stop"
$here = $PSScriptRoot
$root = Split-Path $here -Parent
$cores = [Environment]::ProcessorCount

function Get-Arm([string]$name) {
    switch ($name) {
        "reaktor-software" {
            @{ exe  = Join-Path $root "build\bench.exe"
               args = @("--renderer", "software")
               dir  = $root
               make = "./build.ps1 bench" }
        }
        "reaktor-d3d11" {
            @{ exe  = Join-Path $root "build\bench.exe"
               args = @("--renderer", "direct3d11")
               dir  = $root
               make = "./build.ps1 bench" }
        }
        "reaktor-opengl" {
            @{ exe  = Join-Path $root "build\bench.exe"
               args = @("--renderer", "opengl")
               dir  = $root
               make = "./build.ps1 bench" }
        }
        "shaft" {
            @{ exe  = Join-Path $here "shaft-bench\.build\release\ShaftBench.exe"
               args = @()
               dir  = Join-Path $here "shaft-bench"
               make = "swift build -c release   (in benchmarks/shaft-bench; macOS - see the README)" }
        }
        "flutter" {
            @{ exe  = Join-Path $here "flutter-bench\build\windows\x64\runner\Release\reaktor_bench.exe"
               args = @()
               dir  = Join-Path $here "flutter-bench"
               make = "./setup.ps1 then flutter build windows --release   (in benchmarks/flutter-bench)" }
        }
        "electron" {
            @{ exe  = Join-Path $here "electron-bench\node_modules\electron\dist\electron.exe"
               args = @(".")
               dir  = Join-Path $here "electron-bench"
               make = "npm install   (in benchmarks/electron-bench)" }
        }
        "kmp" {
            @{ exe  = Join-Path $here "kmp-bench\build\compose\binaries\main\app\reaktor-bench-kmp\reaktor-bench-kmp.exe"
               args = @()
               dir  = Join-Path $here "kmp-bench"
               make = "gradle createDistributable   (in benchmarks/kmp-bench)" }
        }
        default { throw "unknown target: $name" }
    }
}

# Every descendant of a process, so Electron's renderer and GPU children are
# counted and a launcher's children are not left out.
function Get-ProcessTree([int]$rootId) {
    $byParent = @{}
    foreach ($p in Get-CimInstance Win32_Process -Property ProcessId, ParentProcessId) {
        $parent = [int]$p.ParentProcessId
        if (-not $byParent.ContainsKey($parent)) { $byParent[$parent] = @() }
        $byParent[$parent] += [int]$p.ProcessId
    }

    $seen = @{}
    $stack = New-Object System.Collections.Stack
    $stack.Push($rootId)
    while ($stack.Count -gt 0) {
        $id = $stack.Pop()
        if ($seen.ContainsKey($id)) { continue }
        $seen[$id] = $true
        if ($byParent.ContainsKey($id)) {
            foreach ($child in $byParent[$id]) { $stack.Push($child) }
        }
    }
    return @($seen.Keys)
}

function Invoke-Arm($spec, [string[]]$argv) {
    $out = [IO.Path]::GetTempFileName()
    $err = [IO.Path]::GetTempFileName()

    $proc = Start-Process -FilePath $spec.exe -ArgumentList ($spec.args + $argv) -WorkingDirectory $spec.dir -PassThru -RedirectStandardOutput $out -RedirectStandardError $err

    # CPU is kept per process id rather than summed live: a child that exits
    # mid-run still spent what it spent, and the last reading it gave is it.
    $cpuMs = @{}
    $peakPrivate = 0L
    $tree = @($proc.Id)
    $treeAt = -1000
    $clock = [Diagnostics.Stopwatch]::StartNew()

    while (-not $proc.HasExited) {
        if ($clock.ElapsedMilliseconds - $treeAt -ge 500) {
            $tree = Get-ProcessTree $proc.Id
            $treeAt = $clock.ElapsedMilliseconds
        }

        $private = 0L
        foreach ($id in $tree) {
            try {
                $p = Get-Process -Id $id -ErrorAction Stop
                $cpuMs[$id] = $p.TotalProcessorTime.TotalMilliseconds
                $private += $p.PrivateMemorySize64
            } catch { }
        }
        if ($private -gt $peakPrivate) { $peakPrivate = $private }

        Start-Sleep -Milliseconds 100
    }
    $clock.Stop()

    $wallMs = $clock.Elapsed.TotalMilliseconds
    $totalCpuMs = 0.0
    foreach ($value in $cpuMs.Values) { $totalCpuMs += $value }

    $stdout = Get-Content $out -Raw
    $stderr = Get-Content $err -Raw
    Remove-Item $out, $err -Force -ErrorAction SilentlyContinue

    $keys = @{}
    foreach ($line in ($stdout -split "`r?`n")) {
        if ($line -match '^\s*([a-z_0-9]+)\s+(\S+)\s*$') { $keys[$matches[1]] = $matches[2] }
    }

    return @{
        keys        = $keys
        stderr      = $stderr
        exit        = $proc.ExitCode
        private_mb  = [math]::Round($peakPrivate / 1MB, 1)
        cpu_core    = [math]::Round(100.0 * $totalCpuMs / $wallMs, 1)
        cpu_machine = [math]::Round(100.0 * $totalCpuMs / $wallMs / $cores, 1)
        wall_s      = [math]::Round($wallMs / 1000.0, 2)
    }
}

function Get-Median([double[]]$values) {
    if ($values.Count -eq 0) { return 0 }
    $sorted = @($values | Sort-Object)
    $mid = [int][math]::Floor($sorted.Count / 2)
    if ($sorted.Count % 2 -eq 1) { return $sorted[$mid] }
    return ($sorted[$mid - 1] + $sorted[$mid]) / 2
}

$argv = @("--bench-seconds", "$Seconds", "--fps", "$Fps", "--boxes", "$Boxes")
$rows = @()

Write-Host ""
Write-Host "$Boxes boxes, $Seconds s, $Fps fps, $Runs runs, $cores logical processors"
Write-Host ""

foreach ($name in $Targets) {
    $spec = Get-Arm $name

    if (-not (Test-Path $spec.exe)) {
        Write-Host ("{0,-18} skipped - not built. {1}" -f $name, $spec.make)
        continue
    }

    $private = @(); $cpuCore = @(); $cpuMachine = @(); $frames = @(); $perFrame = @()
    $size = ""

    for ($i = 0; $i -lt $Runs; $i++) {
        $r = Invoke-Arm $spec $argv

        if ($r.exit -ne 0) {
            Write-Host ("{0,-18} exited {1}: {2}" -f $name, $r.exit, $r.stderr.Trim())
            break
        }

        $private += $r.private_mb
        $cpuCore += $r.cpu_core
        $cpuMachine += $r.cpu_machine
        if ($r.keys.ContainsKey("frames")) { $frames += [double]$r.keys["frames"] }
        if ($r.keys.ContainsKey("ms_per_frame")) { $perFrame += [double]$r.keys["ms_per_frame"] }
        if ($r.keys.ContainsKey("size")) { $size = $r.keys["size"] }

        $shown = if ($r.keys.ContainsKey("frames")) { $r.keys["frames"] } else { "?" }
        Write-Host ("  {0} run {1,2}: {2,7} MB {3,7}% core {4,6}% machine {5,6} frames" -f $name, ($i + 1), $r.private_mb, $r.cpu_core, $r.cpu_machine, $shown)
    }

    if ($private.Count -eq 0) { continue }

    $rows += [pscustomobject]@{
        Arm        = $name
        Size       = $(if ($size) { $size } else { "?" })
        MemoryMB   = Get-Median $private
        CpuCore    = Get-Median $cpuCore
        CpuMachine = Get-Median $cpuMachine
        Frames     = Get-Median $frames
        MsPerFrame = $(if ($perFrame.Count) { Get-Median $perFrame } else { $null })
        Runs       = $private.Count
    }
}

Write-Host ""
$rows | Format-Table -AutoSize

# A window that did not come out 800x600 is not drawing the same picture, and
# this is the only place that would say so.
foreach ($row in $rows) {
    if ($row.Size -ne "800x600" -and $row.Size -ne "?") {
        Write-Host ("warning: {0} drew at {1}, not 800x600" -f $row.Arm, $row.Size)
    }
}

if ($Csv) {
    $rows | Export-Csv -Path $Csv -NoTypeInformation
    Write-Host "wrote $Csv"
}
