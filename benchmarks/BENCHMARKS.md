# Benchmarks

Five UI stacks draw the same animation, and one script measures each from
outside its process. The results are the chart in
[`docs/PERFORMANCE.md`](../docs/PERFORMANCE.md).

| Arm | Directory | Built with |
| --- | --- | --- |
| Reaktor | `samples/bench/` | `build.ps1` / `build.sh` |
| Electron | `electron-bench/` | npm |
| Flutter | `flutter-bench/` | Flutter SDK |
| Compose Multiplatform | `kmp-bench/` | Gradle |
| Shaft | `shaft-bench/` | SwiftPM, macOS only — [below](#shaft) |

## Running it

```powershell
./benchmarks/run.ps1                        # every arm, 6 s, 60 fps, 10 runs
./benchmarks/run.ps1 -Targets electron,kmp -Runs 3
./benchmarks/run.ps1 -Csv bars.csv
```

`run.ps1` also takes `-Seconds`, `-Fps` and `-Boxes`; `run.sh` is the same for
macOS, Linux and the BSDs (`--targets`, `--runs`, `--seconds`, `--fps`,
`--boxes`, `--csv`). Nothing is built for you: a missing arm is skipped, with
the command that builds it.

Reaktor runs three times — `reaktor-software`, its default, and
`reaktor-d3d11` and `reaktor-opengl`, the same binary on SDL's GPU drivers
(`run.sh` adds `reaktor-metal` on macOS). The others draw on the GPU, so those
two are the like-for-like comparison.

## What every arm draws

- An **800×600** drawing area, not counting the window frame.
- A **#f7f7f7** background, Reaktor's light theme.
- **64 quads**, each spinning about its center. For box `i` in a `w`×`h` area,
  `t` seconds after the first frame:

  ```
  a  = t * (1 + 0.05 * i)
  cx = (i % 8) * (w / 8) + w / 16
  cy = (i / 8) * (h / 8) + h / 16
  r  = h / 24
  vertex k = (cx + r * cos(a + k * pi/2), cy + r * sin(a + k * pi/2))
  fill     = rgb(60 + (i * 3) % 190, 140, 220)
  ```

  The divisions are integer divisions, as in `bench.c`, which every arm was
  written from.
- Antialiasing at each framework's default, on in all of them.

Every arm takes `--bench-seconds N`, `--fps F` (`0` uncapped) and `--boxes N`,
and prints `key value` lines as it quits. `size` must read `800x600` for a run
to count, and `fps` shows whether the rate held. Each framework times a
different part of the frame, so only the script's numbers compare.

## How it measures

Every 100 ms the script samples the whole process tree: the peak of its summed
memory, and the processor time charged to it — so Electron's five processes
and a JVM stand beside one C executable. CPU comes out as `CpuCore`, a percent
of one core, and `CpuMachine`, over every logical processor; each figure is
the median of all runs.

- **Keep runs at 6 seconds.** Startup counts, and some arms pay far more for
  it: at 3 seconds Electron reads 45.5% of a core instead of 37.5%.
- **The scripts read memory differently.** `run.ps1` uses private bytes,
  `run.sh` resident set size, which counts shared pages against every process
  and inflates Electron and the JVM. Compare `run.sh` numbers only with each
  other; without `perl` it also times runs to the whole second.

## Building the arms

Each arm uses its framework's own toolchain; none is vendored here.

### Electron

```bash
cd benchmarks/electron-bench
npm install
```

Node 20.19+ or 22+; older Node fails with `ERR_REQUIRE_ESM`. `--software`
turns hardware acceleration off.

### Flutter

```powershell
cd benchmarks/flutter-bench
./setup.ps1
flutter build windows --release
```

Only the Dart is checked in. `setup.ps1` generates the Windows runner and
patches in the title and an 800×600 client area, by regex over generated code
— a new Flutter template can break it, and the `size` line will show it.

### Compose Multiplatform

```bash
cd benchmarks/kmp-bench
gradle createDistributable
```

A full JDK 17+ and Gradle 8.7+; Android Studio's bundled JBR has no
`jpackage`. Run the packaged app, not `gradle run`, or the daemon is measured
too. The JVM gets no heap flags.

### Shaft

```bash
cd benchmarks/shaft-bench
swift build -c release
```

It does not link on Windows: SwiftSDL3 0.1.6 never compiles its joystick
sources there, leaving `SDL_SetJoystickVirtualButtonInner` undefined, and one
Swift package cannot add sources to another. Until that changes it runs on
macOS only and stays out of the chart. To get as far as that link on Windows,
build from `vcvars64.bat` with `GIT_CONFIG_COUNT=1`,
`GIT_CONFIG_KEY_0=core.symlinks` and `GIT_CONFIG_VALUE_0=false`. The `-bench`
suffix keeps SwiftPM, which names a package after its directory, from
colliding with Shaft itself.

## What still is not equal

- **The rasterizer.** Reaktor draws on the CPU by default, the others on the
  GPU. Its GPU arms narrow the gap without closing it: SDL's OpenGL renderer is
  not Skia.
- **Frame pacing.** At `--fps 60` every arm waits out its frame, then asks for
  exactly one more. Skipping vsyncs that are not due looks simpler and drifts:
  to 41.6 fps in Flutter and 42.7 in Compose, which is why every arm prints
  `fps`.
