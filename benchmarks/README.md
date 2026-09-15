# The comparison

The chart in `docs/PERFORMANCE.md` used to have five frameworks in it, and only
one of them was measured here: Reaktor's bars came from `samples/bench`, and the
other four were read off [ShaftUI/Shaft](https://github.com/ShaftUI/Shaft)'s
README, taken on an M1 Max, with no denominator named for the CPU figure.

This directory is what replaced them. Four projects that draw the same picture
the same way, and one script that measures every arm from outside.

The chart now carries what came out of it, and it is not what it replaced. The
memory ordering survived; the CPU ordering did not. Measuring Flutter over
twelve threads gives **1.4%** where that README shows 11.4%, which is an
eightfold gap in the direction that says their figure was per-core and
Reaktor's per-machine — the two plotted in one panel. On one denominator the
software rasterizer first came out the most expensive arm on CPU, which is what
a CPU rasterizer among GPU ones should be, and measuring where that went found
it was not the rasterizing: a panel background was arriving as two textured,
blended triangles where a fill would do. `docs/PERFORMANCE.md` has the numbers.
It reads 16.4% of a core now, under Flutter.

Shaft itself is not in the chart: it does not link on Windows, for a reason
that is nothing to do with this benchmark. See below.

## The picture

Every arm draws this, and nothing else:

- An **800×600** drawing area — the content, not the window frame. Each arm
  reads its real surface size and prints it, so a window that came out the
  wrong size says so instead of quietly drawing a smaller picture.
- The area cleared to **#f7f7f7**, which is `--background-body` from the light
  theme Reaktor loads.
- **64 filled quads**, each rotating about its own center. For box `i`, with
  `w` and `h` the drawing area and `t` seconds since the first frame:

  ```
  a  = t * (1 + 0.05 * i)
  cx = (i % 8) * (w / 8) + w / 16
  cy = (i / 8) * (h / 8) + h / 16
  r  = h / 24
  vertex k = (cx + r * cos(a + k * pi/2), cy + r * sin(a + k * pi/2))
  fill     = rgb(60 + (i * 3) % 190, 140, 220)
  ```

  **Those divisions are integer divisions.** `bench.c` does them in `int`, so a
  truncated grid step and a truncated radius are part of the picture; an arm
  that does them in floating point draws its boxes somewhere else.

- Antialiasing left at each framework's default, which is on for all five.

`samples/bench/bench.c` is the original, and the source every arm was
transcribed from.

## The protocol

Each arm takes the same flags, and no environment variables:

| Flag | Meaning |
| --- | --- |
| `--bench-seconds N` | run for N seconds, then print and quit |
| `--fps F` | hold the run to F frames a second, sleeping out the rest of each frame. `0` free-runs |
| `--boxes N` | how many boxes |

and prints `key value` lines on stdout before exiting:

| Key | Every arm | Meaning |
| --- | --- | --- |
| `size` | yes | the drawing area, `800x600` or the run is not comparable |
| `frames` | yes | frames actually drawn |
| `fps` | yes | the rate it held |
| `ms_per_frame` | no | what one frame cost, **by each framework's own reckoning** |
| `cpu_at_60fps` | no | `ms_per_frame` against a 16.67 ms budget |

`ms_per_frame` is not comparable across arms and the table is deliberate about
which arms report it. Reaktor measures build, render and present, because it
does all three itself — but with vsync on and `--fps 60` its present absorbs the
block, so the figure comes out at the frame period and says nothing about the
work. `--no-vsync` is how to read that one; `bench.c` says so at the top. Flutter reports build and raster from
`addTimingsCallback`. Electron can only time the script that issues the draw
calls — Chromium rasterizes and presents in other processes, and none of that
is visible from the page. Compose times its draw pass. Shaft exposes no
per-frame accounting at all.

**So the comparable numbers are the ones taken from outside**, which is what
`run.ps1` exists for.

## Measuring

```powershell
./benchmarks/run.ps1                       # all arms, 6 s, 60 fps, 10 runs
./benchmarks/run.ps1 -Targets electron,kmp -Runs 3
./benchmarks/run.ps1 -Csv bars.csv
```

```sh
./benchmarks/run.sh                        # the same, on macOS, Linux and the BSDs
./benchmarks/run.sh --targets electron,kmp --runs 3
./benchmarks/run.sh --csv bars.csv
```

For each run either script samples the **whole process tree** every 100 ms and
keeps the peak of summed memory and the total processor time each process was
charged. That is the only honest way to hold Electron — five processes — and a
JVM against a single C executable: a framework's own opinion of its memory is
its allocator's, not the system's.

It reports CPU twice, because the Shaft README's figure names no denominator
and the two readings differ by the core count:

- **`CpuCore`** — percent of one core, which is what `bench` prints.
- **`CpuMachine`** — the same time over every logical processor.

The median of `-Runs` — `--runs` — runs is the answer; a single reading
against a single reading means nothing on Windows.

**Do not shorten `-Seconds` and compare across lengths.** Startup is inside the
measurement, and the arms that pay most for it pay it once: at three seconds
rather than six, Electron reads 45.5% of a core instead of 37.5% and Compose
60.7% instead of 50.3%, while Reaktor does not move. Six seconds is the
default for that reason.

Reaktor appears three times on purpose. `reaktor-software` is the default CPU
rasterizer and the one it ships; `reaktor-d3d11` and `reaktor-opengl` are the
same binary on the two GPU drivers SDL offers here, which is where the other
arms draw. `run.sh` names that second one for the platform it is on: it offers
`reaktor-metal` on macOS and `reaktor-opengl` everywhere. Comparing like with
like means one of those two, and the spread between all three is the cost of
the default rather than a cost of the framework.

Neither script builds anything. An arm whose artifact is missing is skipped,
with the command that would produce it.

**The two samplers do not read the same memory.** `run.ps1` asks Windows for
private bytes. `run.sh` has nothing to ask: it takes resident set size, out of
`/proc` on Linux and out of `ps` everywhere else, and RSS charges a page that
two processes share to both of them. The arms that pay for that are exactly
the ones the tree walk exists for — Electron's five processes and the JVM.
So a `run.sh` figure belongs beside other `run.sh` figures and nowhere else,
and `docs/PERFORMANCE.md`'s bars are a `run.ps1` run.

Processor time is the same reading in both, to a rounding: `run.sh` reads
`utime + stime` out of `/proc/<pid>/stat` on Linux, because Linux's `ps`
rounds `TIME` to the whole second and a six second run cannot carry that.
Elsewhere `ps` reports hundredths and is taken as it comes.

Wall time wants better than a second as well, and `date` has no portable way
to give it, so `run.sh` reads the clock through `perl`'s `Time::HiRes`. Without
perl on `PATH` it says so and falls back to whole seconds, which is enough to
see an arm run and not enough to quote.

## Building the arms

Each toolchain is the framework's own, and none of them is vendored here.
Every arm's directory carries a `-bench` suffix, because one of them has to:
SwiftPM takes a root package's identity from its directory name, so a Swift
package called `shaft` collides with the `Shaft` dependency it pulls in. The
other three could have been named for the framework alone, and are not, so the
listing reads the same way down its whole length.

### Electron — `electron-bench/`

```bash
cd benchmarks/electron-bench
npm install
```

Needs **Node 20.19+ or 22+**: Electron's installer is an ES module, and older
Node cannot `require` it, which fails as `ERR_REQUIRE_ESM` during the install
rather than as anything about versions. The artifact is
`node_modules/electron/dist/electron.exe`, which `run.ps1` launches directly
rather than through `npm start`, so no shell sits in the process tree.

`--software` turns Chromium's hardware acceleration off, for a run against
Reaktor's default rasterizer rather than against `reaktor-d3d11`.

### Flutter — `flutter-bench/`

```powershell
cd benchmarks/flutter-bench
./setup.ps1
flutter build windows --release
```

The Dart is checked in; the Windows runner is not, because `flutter create`
writes it. `setup.ps1` generates it and patches the window size in
`windows/runner/main.cpp`, which is the only place a Flutter desktop app's
size is decided. It patches the window title in the same file and for the same
reason: `flutter create` names the window after the package, so this arm comes
up called `reaktor_bench` while every other one names the framework that is
drawing. `setup.ps1` retitles it **Flutter**.

That size is the **outer window**, and the client area is what gets drawn, so
`setup.ps1` also grows the rect by the window frame in `win32_window.cpp`. Both
patches are regexes against a generated file, so both can miss on a Flutter
version that reshuffles the template — which is what the `size` line in the
output and the warning from `run.ps1` are for.

### Shaft — `shaft-bench/`

```bash
cd benchmarks/shaft-bench
swift build -c release
```

The `-bench` suffix is load-bearing on this one. A root package in a directory
called `shaft` takes `shaft` as its identity and collides with the `Shaft`
dependency itself — *cyclic dependency between packages ShaftBench ->
ShaftBench*.

**It does not link on Windows.** Not for the reasons you would guess: Shaft's
Skia bundle does ship `windows-x64`, and Shaft links D3D12 and DXGI for
Windows. Everything compiles — 205 objects, Skia, and this benchmark's own
Swift. Then SwiftSDL3 0.1.6 turns out never to compile its `joystick/virtual/`,
`joystick/hidapi/` or `joystick/windows/` directories there — its Windows source
list also names `src/gpu/d3d11` and `src/render/SDL_d3dmath.c`, neither of which
exists in the SDL it vendors — so `SDL_joystick.c` references
`SDL_SetJoystickVirtualButtonInner` and nothing defines it. There is no fix from
this side: a package cannot add sources to another package's target, and
dependencies here are read as they ship.

Two other things stop it before that, and neither is Shaft's. Both have fixes,
so the build now reaches the link from an ordinary user account:

- **A developer shell.** `swift build` from a plain prompt does not reach
  dependency resolution at all — it fails compiling the manifest, with
  *lld-link: could not open 'msvcrt.lib'*. Run `vcvars64.bat` first.
- **git told not to attempt symlinks**, through the environment rather than
  through config:

  ```bat
  set GIT_CONFIG_COUNT=1
  set GIT_CONFIG_KEY_0=core.symlinks
  set GIT_CONFIG_VALUE_0=false
  ```

  git reads those above any config file, which is the point: SwiftPM appends
  `symlinks = true` to every repository it clones, after the `false` git wrote
  itself, and the last value in the file wins. The environment outranks the
  file, so SwiftPM cannot append its way past it. Developer Mode or an elevated
  shell also work and need no environment; this is the version that needs
  neither.

Two things it did need, which are fixed and are not that:

- `dxguid` and `dinput8` in this package's `linkerSettings`. SwiftSDL3 compiles
  its DirectInput haptic code on Windows without linking the library its GUIDs
  live in.
- Symlinks. One of Shaft's dependencies ships one — currently Rainbow, on its
  `AGENTS.md` — and creating it is what fails, as *unable to create symlink
  AGENTS.md: Permission denied*. A global `core.symlinks false` does not help,
  for the reason above: SwiftPM's per-repository `true` is written later and
  wins. The environment variables above are what actually settles it.

Past those two, the build compiles everything — Skia, SwiftSDL3, and this
benchmark's own Swift, 25 of 26 steps — and stops at `[26/26] Linking
ShaftBench.exe` with the undefined joystick symbols above. That is reproducible
today, on Swift 6.3.3, from a non-elevated shell.

So this arm runs on the macOS its own published figures came from, and its
numbers stay out of `docs/PERFORMANCE.md` until it runs here.

### Kotlin Multiplatform — `kmp-bench/`

```bash
cd benchmarks/kmp-bench
gradle createDistributable
```

Compose Multiplatform on the desktop target: Skia through Skiko, on a JVM.
Needs **JDK 17+ and Gradle 8.7+**; no wrapper is checked in, because a wrapper
is a binary. The JDK has to be a real one: Android Studio's bundled JBR is 17
but ships no `jpackage`, and the build stops at `checkRuntime` saying so.

No toolchain version is pinned in `build.gradle.kts`. The JDK that runs Gradle
is the one that compiles this and the one jpackage bundles, so pinning would
send Gradle hunting for an exact version on a machine that has a later one.

Run the packaged distributable rather than `gradle run`, or the Gradle daemon
becomes the app's parent process and the sampler charges the run for it.

The JVM gets no heap flags. What this arm costs out of the box is the thing
worth knowing, and a hand-sized heap is a thumb on the memory bar.

## What is still not equal

- **The machine.** These five now share a procedure, not a history. Nothing
  here makes Shaft's published M1 Max figures comparable to a run on your
  desk — it makes a fresh run of all five comparable to each other.
- **The rasterizer.** Reaktor draws on the CPU by default and the others do
  not. `reaktor-d3d11`, `reaktor-opengl` and `electron --software` are the ways
  to close that, and none of them closes it completely — SDL's GL path is not
  Skia's, and Chromium without hardware acceleration is not SDL's software
  rasterizer either.
- **The frame each one is holding to.** At `--fps 60` every arm sleeps out the
  rest of its frame, and they now sleep the same way on purpose: wait, then ask
  for exactly one frame. Reaktor sleeps with `SDL_DelayNS`, Electron with a
  timer before a `requestAnimationFrame`, Flutter with a `Timer` before a
  `scheduleFrame`, Compose with `delay` before a `withFrameNanos`.

  The other way — letting the vsyncs that are not due pass without drawing —
  reads as the obvious one and is wrong. It held **41.6 fps in Flutter and 42.7
  in Compose** instead of 60, because a repaint asked for inside a vsync
  callback lands on the next vsync and the count drifts. Both were caught by
  the `fps` line, which is the reason every arm prints one.
