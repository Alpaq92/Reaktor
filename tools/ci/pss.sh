#!/bin/sh
# What a shared libreaktor would save: runs showcase, notepad and simple
# together, idle, and reads each one's PSS and the clean pages of its own
# binary, which are roughly libreaktor's resident code. Ten launches, medians.
#   pss.sh [-x XVFB] [-w WRAP] [-n RUNS] [-a "APP..."] BUILD
# The estimate assumes the three apps would map one copy of the largest
# binary's code: saved = sum(binary) - max(binary). App-specific code is
# counted as library code, so the saving is an upper bound.
set -u

xvfb=""
wrap=""
runs=10
apps="showcase notepad simple"
while getopts x:w:n:a: opt; do
    case $opt in
        x) xvfb=$OPTARG ;;
        w) wrap=$OPTARG ;;
        n) runs=$OPTARG ;;
        a) apps=$OPTARG ;;
        *) echo "usage: pss.sh [-x XVFB] [-w WRAP] [-n RUNS] [-a \"APP...\"] BUILD" >&2; exit 2 ;;
    esac
done
shift $((OPTIND - 1))
build=$(cd "$1" && pwd)

mkdir -p smoke/pss
if [ -n "$xvfb" ]; then
    "$xvfb" :98 -screen 0 1280x1024x24 -nolisten tcp </dev/null >smoke/pss/xvfb.log 2>&1 &
    xvfb_pid=$!
    trap 'kill "$xvfb_pid" 2>/dev/null || :' EXIT
    n=0
    while [ ! -S /tmp/.X11-unix/X98 ]; do
        n=$((n + 1))
        if [ "$n" -gt 120 ] || ! kill -0 "$xvfb_pid" 2>/dev/null; then
            echo "Xvfb did not start"; cat smoke/pss/xvfb.log || :; exit 1
        fi
        sleep 1
    done
    DISPLAY=:98
    export DISPLAY
fi

# kB of PSS for the whole process, and of clean pages mapped from its binary.
measure() {
    pss=$(awk '/^Pss:/ { print $2 }' "/proc/$1/smaps_rollup")
    bin=$(awk -v exe="$2" '
        /^[0-9a-f]+-[0-9a-f]+ / { mine = ($NF == exe) }
        mine && /^(Shared|Private)_Clean:/ { kb += $2 }
        END { print kb + 0 }' "/proc/$1/smaps")
    echo "$pss $bin"
}

# Under a wrapper the app is not the child, so it is found by its binary.
find_app() {
    for d in /proc/[0-9]*; do
        [ "$(readlink "$d/exe" 2>/dev/null)" = "$1" ] && echo "${d#/proc/}"
    done
}

rows=smoke/pss/runs.txt
: >"$rows"
i=0
retries=0
while [ "$i" -lt "$runs" ]; do
    i=$((i + 1))
    pids=""
    missing=""
    for a in $apps; do
        $wrap "$build/$a" </dev/null >"smoke/pss/$a.log" 2>&1 &
        pids="$pids $!"
    done
    sleep 10
    row=""
    found=""
    for a in $apps; do
        pid=$(find_app "$build/$a")
        if [ -z "$pid" ]; then missing=$a; break; fi
        found="$found $pid"
        row="$row $(measure "$pid" "$build/$a")"
    done
    # An app that quit early (a stray click or key on a live desktop does it)
    # costs that run, not the measurement: say why, and launch the run again.
    if [ -n "$missing" ]; then
        echo "run $i: $missing is not running; its log:"
        cat "smoke/pss/$missing.log" 2>/dev/null
        kill $pids 2>/dev/null || :
        for p in $pids; do wait "$p"; echo "  pid $p ended with status $?"; done
        retries=$((retries + 1))
        if [ "$retries" -gt 3 ]; then echo "giving up after $retries lost runs"; exit 1; fi
        i=$((i - 1))
        continue
    fi
    echo "$row" >>"$rows"
    echo "run $i:$row"
    kill $found 2>/dev/null || :
    for p in $pids; do wait "$p" 2>/dev/null || :; done
done

median() { sort -n | awk '{ v[NR] = $1 } END { print (NR % 2) ? v[(NR + 1) / 2] : (v[NR / 2] + v[NR / 2 + 1]) / 2 }'; }
col() { awk -v c="$1" '{ print $c }' "$rows" | median; }

echo
printf '%-9s %10s %14s\n' app 'PSS kB' 'binary kB'
c=1
for a in $apps; do
    printf '%-9s %10s %14s\n' "$a" "$(col $c)" "$(col $((c + 1)))"
    c=$((c + 2))
done
awk '{ p = b = m = 0
       for (k = 1; k < NF; k += 2) { p += $k; b += $(k + 1); if ($(k + 1) > m) m = $(k + 1) }
       print p, b - m, 100 * (b - m) / p }' "$rows" >smoke/pss/saving.txt
echo
echo "summed PSS:      $(awk '{ print $1 }' smoke/pss/saving.txt | median) kB"
echo "shared saves:    $(awk '{ print $2 }' smoke/pss/saving.txt | median) kB"
echo "saving:          $(awk '{ print $3 }' smoke/pss/saving.txt | median)% (go at 20%)"
