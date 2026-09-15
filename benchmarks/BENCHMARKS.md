# Benchmarks

Five UI stacks draw the same animation, and one script measures each of them
from outside its process. The results are the chart in
[`docs/PERFORMANCE.md`](../docs/PERFORMANCE.md).

| Arm | Directory | Built with |
| --- | --- | --- |
| Reaktor | `samples/bench/` | `build.ps1` / `build.sh` |
| Electron | `electron-bench/` | npm |
| Flutter | `flutter-bench/` | Flutter SDK |
| Compose Multiplatform | `kmp-bench/` | Gradle |
| Shaft | `shaft-bench/` | SwiftPM, macOS only — [see below](#shaft) |

## Running it

```powershell
./benchmarks/run.ps1                        # every arm, 6 s, 60 fps, 10 runs
./benchmarks/run.ps1 -Targets electron,kmp -Runs 3
./benchmarks/run.ps1 -Csv bars.csv
```

`run.ps1` also takes `-Seconds`, `-Fps` and `-Boxes`. `run.sh` does the same on
macOS, Linux and the BSDs, with `--targets`, `--runs`, `--seconds`, `--fps`,
`--boxes` and `--csv`. Nothing is built for you: a missing arm is skipped, and
the script prints the command that builds it.

Reaktor runs three times: `reaktor-software`, the renderer it ships with, and
`reaktor-d3d11` and `reaktor-opengl`, the same binary on SDL's GPU drivers
(`run.sh` also offers `reaktor-metal` on macOS). The other frameworks draw on
the GPU, so those two are the like-for-like comparison.

## What every arm draws

- An **800×600** drawing area, not counting the window frame.
- A **#f7f7f7** background, Reaktor's light theme.
- **64 quads**, each spinning around its own center. For box `i`, in a `w`×`h`
  area, `t` seconds after the first frame:

  ```
  a  = t * (1 + 0.05 * i)
  cx = (i % 8) * (w / 8) + w / 16
  cy = (i / 8) * (h / 8) + h / 16
  r  = h / 24
  vertex k = (cx + r * cos(a + k * pi/2), cy + r * sin(a + k * pi/2))
  fill     = rgb(60 + (i * 3) % 190, 140, 220)
  ```

  The divisions are integer divisions, as in `bench.c`, which every other arm
  was written from.
- Antialiasing at each framework's default, which is on in all of them.

Every arm takes `--bench-seconds N`, `--fps F` (`0` for uncapped) and
`--boxes N`, and prints `key value` lines as it quits. `size` has to read
`800x600` or the run isn't comparable, and `fps` shows whether the rate held.
Some arms also print `ms_per_frame`, but each framework times a different part
of the frame, so only the script's numbers compare across arms.

## How it measures

Every 100 ms the script samples the whole process tree: the peak of its summed
memory, and the processor time charged to it. That puts Electron's five
processes and a JVM on the same footing as one C executable. CPU comes out two
ways — `CpuCore`, a percent of one core, and `CpuMachine`, the same time over
every logical processor — and each figure is the median of all runs.

- **Keep runs at 6 seconds.** Startup counts, and some arms pay far more for it:
  at 3 seconds Electron reads 45.5% of a core instead of 37.5%.
- **The two scripts read memory differently.** `run.ps1` uses private bytes;
  `run.sh` uses resident set size, which counts a shared page against every
  process using it and so inflates Electron and the JVM. Compare `run.sh`
  numbers only with each other. Without `perl` it also times runs to the whole
  second — enough to see an arm run, not to quote.

## Building the arms

Each arm uses its framework's own toolchain; none is vendored here.

### Electron

```bash
cd benchmarks/electron-bench
npm install
```

Needs Node 20.19+ or 22+; older Node fails with `ERR_REQUIRE_ESM`.
`--software` turns hardware acceleration off.

### Flutter

```powershell
cd benchmarks/flutter-bench
./setup.ps1
flutter build windows --release
```

Only the Dart is checked in. `setup.ps1` generates the Windows runner and
patches in the title and an 800×600 client area. The patches are regexes over
generated code, so a Flutter release that changes its template can break them;
the `size` line will show it.

### Compose Multiplatform

```bash
cd benchmarks/kmp-bench
gradle createDistributable
```

Needs a full JDK 17+ and Gradle 8.7+ — Android Studio's bundled JBR has no
`jpackage`. Run the packaged app, not `gradle run`, or the Gradle daemon is
measured too. The JVM gets no heap flags.

### Shaft

```bash
cd benchmarks/shaft-bench
swift build -c release
```

It doesn't link on Windows. SwiftSDL3 0.1.6 never compiles its joystick sources
there, so `SDL_SetJoystickVirtualButtonInner` is left undefined, and one Swift
package can't add sources to another. Until that changes it runs on macOS only
and stays out of the chart.

To get as far as that link on Windows, build from a developer shell
(`vcvars64.bat`), and set `GIT_CONFIG_COUNT=1`, `GIT_CONFIG_KEY_0=core.symlinks`
and `GIT_CONFIG_VALUE_0=false` so a dependency's symlink doesn't stop the
checkout. The `-bench` suffix on every directory is for SwiftPM, which names a
package after its directory: `shaft` would collide with Shaft itself.

## What still isn't equal

- **The rasterizer.** Reaktor draws on the CPU by default and the others on the
  GPU. The GPU arms narrow that gap without closing it: SDL's OpenGL renderer
  isn't Skia.
- **Frame pacing.** At `--fps 60` every arm waits out its frame, then asks for
  exactly one more. Skipping vsyncs that aren't due yet looks simpler and is
  wrong — the rate drifts to 41.6 fps in Flutter and 42.7 in Compose, which is
  why every arm prints `fps`.
