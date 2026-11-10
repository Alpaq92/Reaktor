#!/bin/sh
# Builds twice, link-time optimization off and on, and compares the two:
# tests, shots and dumps, sizes, and bench frame times taken in turn.
#   lto-check.sh [-x XVFB] [-w WRAP] [-r RUNS] [CMAKE_ARGS...]
set -u

xvfb=""
wrap=""
runs=10
while getopts x:w:r: opt; do
    case $opt in
        x) xvfb=$OPTARG ;;
        w) wrap=$OPTARG ;;
        r) runs=$OPTARG ;;
        *) echo "usage: lto-check.sh [-x XVFB] [-w WRAP] [-r RUNS] [CMAKE_ARGS...]" >&2; exit 2 ;;
    esac
done
shift $((OPTIND - 1))

tests=$(cat tools/ci/tests.txt)
for v in OFF ON; do
    cmake -S . -B "build-lto-$v" -G Ninja -DCMAKE_BUILD_TYPE=Release -DREAKTOR_LTO=$v "$@" </dev/null >"lto-$v.cmake.log" 2>&1 || { cat "lto-$v.cmake.log"; exit 1; }
    grep -i "LTO" "lto-$v.cmake.log" || :
    cmake --build "build-lto-$v" --target showcase notepad simple bench $tests </dev/null >"lto-$v.build.log" 2>&1 || { tail -50 "lto-$v.build.log"; exit 1; }
    echo "$v: $(grep -ci 'warning' "lto-$v.build.log") warning lines, $(grep -c -e '-flto' "build-lto-$v/build.ninja") -flto lines in build.ninja"
    grep -i 'warning' "lto-$v.build.log" | sort | uniq -c | sort -rn | head -20
done

if [ -n "$xvfb" ]; then
    "$xvfb" :96 -screen 0 1280x1024x24 -nolisten tcp </dev/null >lto-xvfb.log 2>&1 &
    xvfb_pid=$!
    trap 'kill "$xvfb_pid" 2>/dev/null || :' EXIT
    n=0
    while [ ! -S /tmp/.X11-unix/X96 ]; do
        n=$((n + 1))
        if [ "$n" -gt 120 ] || ! kill -0 "$xvfb_pid" 2>/dev/null; then
            echo "Xvfb did not start"; cat lto-xvfb.log || :; exit 1
        fi
        sleep 1
    done
    DISPLAY=:96
    export DISPLAY
fi

fail=0
for v in OFF ON; do
    for t in $tests; do
        a=; [ "$t" = gibustest ] && [ -n "${DBUS_SESSION_BUS_ADDRESS:-}" ] && a=--require-bus
        $wrap "./build-lto-$v/$t" $a </dev/null >"lto-$v-$t.log" 2>&1 || { echo "$v $t: failed"; tail -20 "lto-$v-$t.log"; fail=1; }
    done
done

rm -rf lto-shots
mkdir lto-shots
same=0
for t in 0 1 2 3 4 5 6 7 8 9; do
    for th in light dark; do
        for v in OFF ON; do
            $wrap "./build-lto-$v/showcase" --tab $t --theme $th --shot "lto-shots/$v-$t-$th.bmp" --a11y-dump "lto-shots/$v-$t-$th.txt" </dev/null >/dev/null 2>&1
            grep -v volatile "lto-shots/$v-$t-$th.txt" | grep -v '^#' >"lto-shots/$v-$t-$th.dump"
        done
        if [ -s "lto-shots/OFF-$t-$th.bmp" ] && cmp -s "lto-shots/OFF-$t-$th.bmp" "lto-shots/ON-$t-$th.bmp" &&
            cmp -s "lto-shots/OFF-$t-$th.dump" "lto-shots/ON-$t-$th.dump"; then
            same=$((same + 1))
        else
            echo "showcase tab $t $th: differs"; fail=1
        fi
    done
done
for s in notepad simple; do
    for v in OFF ON; do
        $wrap "./build-lto-$v/$s" --theme light --shot "lto-shots/$v-$s.bmp" </dev/null >/dev/null 2>&1
    done
    if [ -s "lto-shots/OFF-$s.bmp" ] && cmp -s "lto-shots/OFF-$s.bmp" "lto-shots/ON-$s.bmp"; then same=$((same + 1)); else echo "$s: differs"; fail=1; fi
done
echo "shots and dumps the same: $same of 22"

for p in showcase bench; do
    echo "$p bytes: off $(wc -c <"build-lto-OFF/$p") on $(wc -c <"build-lto-ON/$p")"
done

med() { sort -n | awk '{a[NR] = $1} END {print (NR % 2) ? a[(NR + 1) / 2] : (a[NR / 2] + a[NR / 2 + 1]) / 2}'; }
for boxes in 64 512; do
    : >"lto-bench-$boxes.txt"
    i=0
    while [ "$i" -lt "$runs" ]; do
        order="OFF ON"; [ $((i % 2)) -eq 1 ] && order="ON OFF"
        for v in $order; do
            $wrap "./build-lto-$v/bench" --renderer software --no-vsync --bench-seconds 5 --boxes $boxes </dev/null 2>/dev/null |
                awk -v v=$v -v i=$i '/^ms_render/ {r = $2} /^ms_per_frame/ {f = $2} END {print i, v, r, f}' >>"lto-bench-$boxes.txt"
        done
        i=$((i + 1))
    done
    for v in OFF ON; do
        echo "bench $boxes boxes, software, $v: render $(awk -v v=$v '$2 == v {print $3}' "lto-bench-$boxes.txt" | med) ms, frame $(awk -v v=$v '$2 == v {print $4}' "lto-bench-$boxes.txt" | med) ms"
    done
    echo "runs where on rendered faster: $(awk '{r[$1, $2] = $3} END {n = 0; for (k in r) {split(k, p, SUBSEP); if (p[2] == "ON" && r[p[1], "ON"] < r[p[1], "OFF"]) n++} print n}' "lto-bench-$boxes.txt") of $runs"
done
exit "$fail"
