#!/bin/sh
# pss.sh counted all of a binary's clean pages as library. This counts only
# the resident pages that hold libreaktor's and SDL's own code and data:
# builds showcase, notepad and simple with linker maps into build-map, runs
# them together, idle, and sorts each resident, file-backed page of each
# binary by the map. A page shared with an app object, a module or the C
# runtime is not counted. Ten launches, medians.
#   pss-pages.sh [-x XVFB] [-w WRAP] [-n RUNS]
set -u

xvfb=""
wrap=""
runs=10
while getopts x:w:n: opt; do
    case $opt in
        x) xvfb=$OPTARG ;;
        w) wrap=$OPTARG ;;
        n) runs=$OPTARG ;;
        *) echo "usage: pss-pages.sh [-x XVFB] [-w WRAP] [-n RUNS]" >&2; exit 2 ;;
    esac
done
apps="showcase notepad simple"
build=$PWD/build-map
out=smoke/pss-pages

mkdir -p "$build/maps" "$out"
# A directory given to -Map gets one OUTPUT.map per program (binutils 2.38).
cmake -S . -B "$build" -G Ninja -DCMAKE_BUILD_TYPE=Release \
      "-DCMAKE_EXE_LINKER_FLAGS=-Wl,-Map=$build/maps/" </dev/null >"$out/configure.log" 2>&1 ||
    { cat "$out/configure.log"; exit 1; }
for a in $apps; do
    cmake --build "$build" --target "$a" </dev/null >"$out/build-$a.log" 2>&1 ||
        { tail -50 "$out/build-$a.log"; exit 1; }
    [ -s "$build/maps/$a.map" ] || { echo "no map for $a"; ls "$build/maps"; exit 1; }
done

# Page owners from the map: lib (an archive member outside the system), app
# (the program's own objects and the modules compiled into it), other.
# A page with more than one kind of owner is "mixed" and not counted.
for a in $apps; do
    awk '
        function hex(s,   v, i) {
            v = 0
            for (i = 3; i <= length(s); i++) v = v * 16 + index("0123456789abcdef", substr(s, i, 1)) - 1
            return v
        }
        /^Linker script and memory map/ { on = 1; next }
        !on { next }
        /^[ ]*\*fill\*/ { next }
        {
            n = 0
            for (i = 1; i <= NF; i++) if ($i ~ /^0x[0-9a-f]+$/) { h[++n] = $i; at = i }
            if (n < 2 || at == NF) next
            file = $(at + 1)
            if (file ~ /^\/usr\// || file ~ /^\/lib/) kind = "other"
            else if (file ~ /\.a\(/) kind = "lib"
            else if (file ~ /\.dir\//) kind = "app"
            else kind = "other"
            start = hex(h[n - 1]); size = hex(h[n])
            if (size == 0 || start == 0) next
            for (p = int(start / 4096); p <= int((start + size - 1) / 4096); p++) {
                if (!(p in own)) own[p] = kind
                else if (own[p] != kind) own[p] = "mixed"
            }
        }
        END { for (p in own) print p, own[p] }' "$build/maps/$a.map" >"$out/$a.owners"
done

if [ -n "$xvfb" ]; then
    "$xvfb" :97 -screen 0 1280x1024x24 -nolisten tcp </dev/null >"$out/xvfb.log" 2>&1 &
    xvfb_pid=$!
    trap 'kill "$xvfb_pid" 2>/dev/null || :' EXIT
    n=0
    while [ ! -S /tmp/.X11-unix/X97 ]; do
        n=$((n + 1))
        if [ "$n" -gt 120 ] || ! kill -0 "$xvfb_pid" 2>/dev/null; then
            echo "Xvfb did not start"; cat "$out/xvfb.log" || :; exit 1
        fi
        sleep 1
    done
    DISPLAY=:97
    export DISPLAY
fi

find_app() {
    for d in /proc/[0-9]*; do
        [ "$(readlink "$d/exe" 2>/dev/null)" = "$1" ] && echo "${d#/proc/}"
    done
}

# Prints the binary's resident, file-backed pages as page numbers of the
# linked image: its mappings, less the load base, read from pagemap.
pages() {
    pid=$1 exe=$2
    vaddr=$(readelf -lW "$exe" | awk '$1 == "LOAD" { print $3; exit }')
    awk -v exe="$exe" '$NF == exe { print $1, $3 }' "/proc/$pid/maps" |
    while read -r range off; do
        echo "$((0x${range%-*})) $((0x${range#*-}))"
    done >"$out/ranges"
    base=$(awk 'NR == 1 { print $1 }' "$out/ranges")
    base=$((base - (vaddr & ~4095)))
    while read -r lo hi; do
        # dd seeks; od would skip by reading the whole table below the binary.
        dd if="/proc/$pid/pagemap" bs=65536 iflag=skip_bytes,count_bytes            skip=$((lo / 4096 * 8)) count=$(((hi - lo) / 4096 * 8)) 2>/dev/null |
            od -An -v -t x8 -w8 |
            awk -v first=$(((lo - base) / 4096)) '
                { d = substr($1, 1, 1) }
                d == "a" || d == "b" || d == "e" || d == "f" { print first + NR - 1 }'
    done <"$out/ranges"
}

rows=$out/runs.txt
: >"$rows"
i=0
while [ "$i" -lt "$runs" ]; do
    i=$((i + 1))
    pids=""
    for a in $apps; do
        $wrap "$build/$a" </dev/null >"$out/$a.log" 2>&1 &
        pids="$pids $!"
    done
    sleep 10
    row=""
    found=""
    for a in $apps; do
        pid=$(find_app "$build/$a")
        if [ -z "$pid" ]; then echo "run $i: $a is not running"; cat "$out/$a.log"; exit 1; fi
        found="$found $pid"
        pss=$(awk '/^Pss:/ { print $2 }' "/proc/$pid/smaps_rollup")
        pages "$pid" "$build/$a" >"$out/$a.present"
        counts=$(awk 'NR == FNR { own[$1] = $2; next }
                      { k = ($1 in own) ? own[$1] : "other"; c[k]++ }
                      END { printf "%d %d %d %d", c["lib"] * 4, c["app"] * 4, c["mixed"] * 4, c["other"] * 4 }' \
                 "$out/$a.owners" "$out/$a.present")
        row="$row $pss $counts"
    done
    echo "$row" >>"$rows"
    echo "run $i:$row"
    kill $found 2>/dev/null || :
    for p in $pids; do wait "$p" 2>/dev/null || :; done
done

median() { sort -n | awk '{ v[NR] = $1 } END { print (NR % 2) ? v[(NR + 1) / 2] : (v[NR / 2] + v[NR / 2 + 1]) / 2 }'; }
col() { awk -v c="$1" '{ print $c }' "$rows" | median; }

echo
printf '%-9s %9s %9s %9s %9s %9s\n' app 'PSS kB' 'lib kB' 'app kB' 'mixed kB' 'other kB'
c=1
for a in $apps; do
    printf '%-9s %9s %9s %9s %9s %9s\n' "$a" "$(col $c)" "$(col $((c + 1)))" \
        "$(col $((c + 2)))" "$(col $((c + 3)))" "$(col $((c + 4)))"
    c=$((c + 5))
done
awk '{ p = $1 + $6 + $11; l = $2 + $7 + $12; m = $2
       if ($7 > m) m = $7; if ($12 > m) m = $12
       print p, l - m, 100 * (l - m) / p }' "$rows" >"$out/saving.txt"
echo
echo "summed PSS:      $(awk '{ print $1 }' "$out/saving.txt" | median) kB"
echo "shared saves:    $(awk '{ print $2 }' "$out/saving.txt" | median) kB"
echo "saving:          $(awk '{ print $3 }' "$out/saving.txt" | median)% (go at 20%)"
