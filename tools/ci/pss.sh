#!/bin/sh
# What a shared libreaktor would save: runs showcase, notepad and simple
# together, idle, and reads each one's PSS and the clean pages of its own
# binary, which are roughly libreaktor's resident code. Ten launches, medians.
#   pss.sh [-x XVFB] [-w WRAP] [-n RUNS] BUILD
# The estimate assumes the three apps would map one copy of the largest
# binary's code: saved = sum(binary) - max(binary). App-specific code is
# counted as library code, so the saving is an upper bound.
set -u

xvfb=""
wrap=""
runs=10
while getopts x:w:n: opt; do
    case $opt in
        x) xvfb=$OPTARG ;;
        w) wrap=$OPTARG ;;
        n) runs=$OPTARG ;;
        *) echo "usage: pss.sh [-x XVFB] [-w WRAP] [-n RUNS] BUILD" >&2; exit 2 ;;
    esac
done
shift $((OPTIND - 1))
build=$(cd "$1" && pwd)
apps="showcase notepad simple"

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

rows=smoke/pss/runs.txt
: >"$rows"
i=0
while [ "$i" -lt "$runs" ]; do
    i=$((i + 1))
    pids=""
    for a in $apps; do
        rm -f "smoke/pss/$a.log"
        $wrap "$build/$a" --log "smoke/pss/$a.log" </dev/null >/dev/null 2>&1 &
        pids="$pids $!"
    done
    for a in $apps; do
        n=0
        while [ ! -s "smoke/pss/$a.log" ] && [ "$n" -lt 60 ]; do n=$((n + 1)); sleep 1; done
    done
    sleep 5
    row=""
    found=""
    for a in $apps; do
        # Under a wrapper the app is not the child, so find it by its binary.
        pid=""
        for d in /proc/[0-9]*; do
            [ "$(readlink "$d/exe" 2>/dev/null)" = "$build/$a" ] && pid=${d#/proc/}
        done
        if [ -z "$pid" ]; then echo "run $i: $a is not running"; cat "smoke/pss/$a.log" 2>/dev/null; exit 1; fi
        found="$found $pid"
        row="$row $(measure "$pid" "$build/$a")"
    done
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
awk '{ p = $1 + $3 + $5; b = $2 + $4 + $6; m = $2
       if ($4 > m) m = $4; if ($6 > m) m = $6
       print p, b - m, 100 * (b - m) / p }' "$rows" >smoke/pss/saving.txt
echo
echo "summed PSS:      $(awk '{ print $1 }' smoke/pss/saving.txt | median) kB"
echo "shared saves:    $(awk '{ print $2 }' smoke/pss/saving.txt | median) kB"
echo "saving:          $(awk '{ print $3 }' smoke/pss/saving.txt | median)% (go at 20%)"
