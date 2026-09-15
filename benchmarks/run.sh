#!/bin/sh
# run.sh - runs the arms of the comparison on macOS, Linux and the BSDs.
# run.ps1 is the Windows one, and the two are deliberately the same shape:
# the same flags, the same columns, the same medians.
#
# The frame counters come from each app's own stdout. Memory and CPU do not:
# they are sampled from outside, over the whole process tree, because Electron
# is five processes and a JVM's idea of its own memory is not the operating
# system's.
#
# Its memory figures are not run.ps1's. That one reads private bytes; this one
# reads resident set size, which counts a page two processes share once for
# each of them. Electron and the JVM are where that shows. Compare a run of
# this against a run of this.
#
# Nothing is built here. Each arm points at an artifact that its own toolchain
# produces, and an arm whose artifact is missing is skipped with the command
# that would make it.
#
#     ./run.sh                                   # all arms, 6 s, 60 fps, 10 runs
#     ./run.sh --targets electron,kmp --runs 3
#     ./run.sh --csv bars.csv
set -eu

here=$(CDPATH= cd -- "$(dirname -- "$0")" && pwd)
root=$(dirname -- "$here")
uname_s=$(uname -s)

# Reaktor appears three times on purpose: the default CPU rasterizer, and the
# same binary on the GPU driver the other arms draw on. SDL's name for that
# driver is the platform's.
case "$uname_s" in
    Darwin) targets="reaktor-software reaktor-metal reaktor-opengl shaft flutter electron kmp" ;;
    *)      targets="reaktor-software reaktor-opengl shaft flutter electron kmp" ;;
esac

seconds=6
fps=60
boxes=64
runs=10
csv=""

while [ $# -gt 0 ]; do
    case "$1" in
        --help|-h) sed -n '2,22p' "$0" | cut -c 3-; exit 0 ;;
        --targets|--seconds|--fps|--boxes|--runs|--csv) ;;
        *) echo "run.sh: unknown argument: $1 (try --help)" >&2; exit 2 ;;
    esac
    flag=$1
    shift
    [ $# -gt 0 ] || { echo "run.sh: $flag needs a value" >&2; exit 2; }
    case "$flag" in
        --targets) targets=$(echo "$1" | tr ',' ' ') ;;
        --seconds) seconds=$1 ;;
        --fps)     fps=$1 ;;
        --boxes)   boxes=$1 ;;
        --runs)    runs=$1 ;;
        --csv)     csv=$1 ;;
    esac
    shift
done

cores=$(getconf _NPROCESSORS_ONLN 2>/dev/null || sysctl -n hw.ncpu 2>/dev/null || echo 1)

# ---------------------------------------------------------------------------
# The arms. Each one sets arm_exe, arm_args, arm_dir and arm_make.

arm_spec() {
    arm_args=""
    case "$1" in
    reaktor-software)
        arm_exe="$root/build/bench"; arm_args="--renderer software"
        arm_dir="$root"; arm_make="./build.sh --target bench" ;;
    reaktor-metal)
        arm_exe="$root/build/bench"; arm_args="--renderer metal"
        arm_dir="$root"; arm_make="./build.sh --target bench" ;;
    reaktor-opengl)
        arm_exe="$root/build/bench"; arm_args="--renderer opengl"
        arm_dir="$root"; arm_make="./build.sh --target bench" ;;
    shaft)
        arm_exe="$here/shaft-bench/.build/release/ShaftBench"
        arm_dir="$here/shaft-bench"
        arm_make="swift build -c release   (in benchmarks/shaft-bench)" ;;
    flutter)
        arm_dir="$here/flutter-bench"
        case "$uname_s" in
        Darwin)
            arm_exe="$arm_dir/build/macos/Build/Products/Release/reaktor_bench.app/Contents/MacOS/reaktor_bench"
            arm_make="flutter create --platforms=macos . then flutter build macos --release   (in benchmarks/flutter-bench; setup.ps1 has no POSIX twin, so the window size wants its patch by hand)" ;;
        *)
            arm_exe="$arm_dir/build/linux/x64/release/bundle/reaktor_bench"
            arm_make="flutter create --platforms=linux . then flutter build linux --release   (in benchmarks/flutter-bench)" ;;
        esac ;;
    electron)
        arm_dir="$here/electron-bench"; arm_args="."
        case "$uname_s" in
        Darwin) arm_exe="$arm_dir/node_modules/electron/dist/Electron.app/Contents/MacOS/Electron" ;;
        *)      arm_exe="$arm_dir/node_modules/electron/dist/electron" ;;
        esac
        arm_make="npm install   (in benchmarks/electron-bench)" ;;
    kmp)
        arm_dir="$here/kmp-bench"
        case "$uname_s" in
        Darwin) arm_exe="$arm_dir/build/compose/binaries/main/app/reaktor-bench-kmp.app/Contents/MacOS/reaktor-bench-kmp" ;;
        *)      arm_exe="$arm_dir/build/compose/binaries/main/app/reaktor-bench-kmp/bin/reaktor-bench-kmp" ;;
        esac
        arm_make="gradle createDistributable   (in benchmarks/kmp-bench)" ;;
    *) echo "run.sh: unknown target: $1" >&2; exit 2 ;;
    esac
}

# ---------------------------------------------------------------------------
# The sampler.

# A clock finer than a second. The CPU reading divides by it, and whole
# seconds over a six second run is more than a percent of error on its own.
perl=$(command -v perl 2>/dev/null || true)
[ -n "$perl" ] || echo "run.sh: no perl - wall time falls back to whole seconds" >&2

now() {
    if [ -n "$perl" ]; then
        "$perl" -MTime::HiRes=time -e 'printf "%.3f\n", time'
    else
        date +%s
    fi
}

# Every descendant of a process, so Electron's renderer and GPU children are
# counted and a launcher's children are not left out.
tree_of() {
    ps -eo pid=,ppid= | awk -v root="$1" '
        { kids[$2] = kids[$2] " " $1 }
        END {
            n = 1; q[1] = root; seen[root] = 1
            for (i = 1; i <= n; i++) {
                m = split(kids[q[i]], c, " ")
                for (j = 1; j <= m; j++)
                    if (!(c[j] in seen)) { seen[c[j]] = 1; q[++n] = c[j] }
            }
            for (i = 1; i <= n; i++) printf "%s%s", (i > 1 ? "," : ""), q[i]
            printf "\n"
        }'
}

clk_tck=$(getconf CLK_TCK 2>/dev/null || echo 100)
page_kb=$(( $(getconf PAGESIZE 2>/dev/null || echo 4096) / 1024 ))

# One reading of the tree, as "pid cpu_seconds rss_kb" lines. Linux answers
# out of /proc because its ps rounds processor time to the whole second;
# everywhere else ps carries hundredths and is the whole story.
sample_tree() {
    if [ -r /proc/self/stat ]; then
        for id in $(echo "$1" | tr ',' ' '); do
            [ -r "/proc/$id/stat" ] && [ -r "/proc/$id/statm" ] || continue
            awk -v id="$id" -v tck="$clk_tck" -v page="$page_kb" '
                FILENAME ~ /stat$/ {
                    split(substr($0, index($0, ") ") + 2), f, " ")
                    cpu = (f[12] + f[13]) / tck
                }
                FILENAME ~ /statm$/ { rss = $2 * page }
                END { printf "%s %.3f %d\n", id, cpu, rss }
            ' "/proc/$id/stat" "/proc/$id/statm" 2>/dev/null || true
        done
    else
        ps -o pid=,rss=,time= -p "$1" 2>/dev/null | awk '
            function secs(t,   a, p, n, i, s, d) {
                d = 0
                if (index(t, "-")) { split(t, a, "-"); d = a[1]; t = a[2] }
                n = split(t, p, ":"); s = 0
                for (i = 1; i <= n; i++) s = s * 60 + p[i]
                return s + d * 86400
            }
            { printf "%s %.3f %d\n", $1, secs($3), $2 }' || true
    fi
}

# The median of a column of a file, ignoring the runs that did not report it.
median_of() {
    awk -v c="$2" '$c != "-" { print $c }' "$1" | sort -n | awk '
        { a[NR] = $1 + 0 }
        END {
            if (NR == 0) { print "-"; exit }
            m = int(NR / 2)
            print (NR % 2 ? a[m + 1] : (a[m] + a[m + 1]) / 2)
        }'
}

# Runs one arm once and prints
# "mem_mb cpu_core cpu_machine exit size frames ms_per_frame",
# with "-" for anything the arm did not report.
run_once() {
    : > "$tmp/samples"

    started=$(now)
    # shellcheck disable=SC2086
    ( cd "$arm_dir" && exec "$arm_exe" $arm_args --bench-seconds "$seconds" \
        --fps "$fps" --boxes "$boxes" ) > "$tmp/out" 2> "$tmp/err" &
    pid=$!

    tree="$pid"
    ticks=0
    while kill -0 "$pid" 2>/dev/null; do
        # The tree is rebuilt every fifth sample. Walking every process on the
        # machine at 10 Hz would be a measurable share of what is being
        # measured, and a child appearing half a second late costs nothing.
        if [ $(( ticks % 5 )) -eq 0 ]; then tree=$(tree_of "$pid"); fi
        ticks=$(( ticks + 1 ))
        sample_tree "$tree" | awk -v t="$ticks" '{ print t, $0 }' >> "$tmp/samples"
        sleep 0.1
    done
    status=0
    wait "$pid" || status=$?
    ended=$(now)

    # CPU is kept per process id rather than summed live: a child that exits
    # mid-run still spent what it spent, and its last reading is it. Memory is
    # the peak of the readings of a single tick, which is a moment that
    # actually happened rather than a sum of separate highs.
    set -- $(awk '
        { if ($3 > cpu[$2]) cpu[$2] = $3; rss[$1] += $4 }
        END {
            peak = 0; for (t in rss) if (rss[t] > peak) peak = rss[t]
            total = 0; for (p in cpu) total += cpu[p]
            printf "%.1f %.3f\n", peak / 1024, total
        }' "$tmp/samples")
    mem_mb=$1
    cpu_s=$2

    cpu_core=$(awk -v c="$cpu_s" -v a="$started" -v b="$ended" \
        'BEGIN { w = b - a; if (w <= 0) w = 0.001; printf "%.1f\n", 100 * c / w }')
    cpu_machine=$(awk -v c="$cpu_core" -v n="$cores" 'BEGIN { printf "%.1f\n", c / n }')

    echo "$mem_mb $cpu_core $cpu_machine $status \
$(key size) $(key frames) $(key ms_per_frame)"
}

key() {
    awk -v k="$1" '$1 == k && NF == 2 { v = $2 } END { print (v == "" ? "-" : v) }' "$tmp/out"
}

# ---------------------------------------------------------------------------

tmp=$(mktemp -d)
trap 'rm -rf "$tmp"' EXIT INT TERM
rows="$tmp/rows"
: > "$rows"

echo
echo "$boxes boxes, $seconds s, $fps fps, $runs runs, $cores logical processors, RSS not private bytes"
echo

for name in $targets; do
    arm_spec "$name"

    if [ ! -x "$arm_exe" ]; then
        printf '%-18s skipped - not built. %s\n' "$name" "$arm_make"
        continue
    fi

    per_run="$tmp/run.$name"
    : > "$per_run"
    size="?"

    i=0
    while [ "$i" -lt "$runs" ]; do
        i=$(( i + 1 ))
        set -- $(run_once)

        if [ "$4" -ne 0 ]; then
            printf '%-18s exited %s: %s\n' "$name" "$4" "$(tr '\n' ' ' < "$tmp/err")"
            break
        fi

        [ "$5" = "-" ] || size=$5
        echo "$1 $2 $3 $6 $7" >> "$per_run"
        printf '  %s run %2d: %7s MB %7s%% core %6s%% machine %6s frames\n' \
            "$name" "$i" "$1" "$2" "$3" "$6"
    done

    [ -s "$per_run" ] || continue

    # The median of the runs is the answer; a single reading against a single
    # reading means nothing.
    echo "$name $size $(median_of "$per_run" 1) $(median_of "$per_run" 2) \
$(median_of "$per_run" 3) $(median_of "$per_run" 4) $(median_of "$per_run" 5) \
$(wc -l < "$per_run" | tr -d ' ')" >> "$rows"
done

echo
printf '%-18s %-9s %9s %8s %11s %8s %11s %5s\n' \
    Arm Size MemoryMB CpuCore CpuMachine Frames MsPerFrame Runs
while read -r arm size mem core machine frames per_frame count; do
    printf '%-18s %-9s %9s %8s %11s %8s %11s %5s\n' \
        "$arm" "$size" "$mem" "$core" "$machine" "$frames" "$per_frame" "$count"
done < "$rows"

# A window that did not come out 800x600 is not drawing the same picture, and
# this is the only place that would say so.
while read -r arm size rest; do
    case "$size" in
        800x600 | \?) ;;
        *) echo "warning: $arm drew at $size, not 800x600" ;;
    esac
done < "$rows"

if [ -n "$csv" ]; then
    {
        echo "Arm,Size,MemoryMB,CpuCore,CpuMachine,Frames,MsPerFrame,Runs"
        tr -s ' ' ',' < "$rows"
    } > "$csv"
    echo "wrote $csv"
fi
