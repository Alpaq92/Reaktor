#!/bin/sh
# The standard shots and dumps of a build, into smoke/: simple and the
# showcase through the system's video driver, and simple through SDL's dummy.
#   shots.sh [-x XVFB] [-w WRAP] BUILD DRIVER
# -x starts that Xvfb on :99 first; -w runs each program under WRAP
# (for example "dbus-run-session -- timeout 600").
set -u

xvfb=""
wrap=""
while getopts x:w: opt; do
    case $opt in
        x) xvfb=$OPTARG ;;
        w) wrap=$OPTARG ;;
        *) echo "usage: shots.sh [-x XVFB] [-w WRAP] BUILD DRIVER" >&2; exit 2 ;;
    esac
done
shift $((OPTIND - 1))
build=$1
driver=$2

rm -rf smoke
mkdir smoke
if [ -n "$xvfb" ]; then
    "$xvfb" :99 -screen 0 1280x1024x24 -nolisten tcp </dev/null >smoke/xvfb.log 2>&1 &
    xvfb_pid=$!
    trap 'kill "$xvfb_pid" 2>/dev/null || :' EXIT
    n=0
    while [ ! -S /tmp/.X11-unix/X99 ]; do
        n=$((n + 1))
        if [ "$n" -gt 120 ] || ! kill -0 "$xvfb_pid" 2>/dev/null; then
            echo "Xvfb did not start"
            cat smoke/xvfb.log || :
            exit 1
        fi
        sleep 1
    done
    DISPLAY=:99
    export DISPLAY
fi

fail=0
shot() {
    name=$1
    drv=$2
    shift 2
    if $wrap "$@" --video "$drv" --shot "smoke/$name.bmp" --a11y-dump "smoke/$name.txt" \
            </dev/null >"smoke/$name.log" 2>&1; then
        if [ -s "smoke/$name.bmp" ] && [ -s "smoke/$name.txt" ]; then
            echo "$name: ok"
            return 0
        fi
        echo "$name: exited 0 without both a shot and a dump"
    else
        echo "$name: exit status $?"
    fi
    cat "smoke/$name.log" || :
    fail=1
}
shot simple "$driver" "$build/simple" --theme light
shot showcase "$driver" "$build/showcase" --tab 7 --theme dark
shot simple-dummy dummy "$build/simple" --theme light
exit "$fail"
