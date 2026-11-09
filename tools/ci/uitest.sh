#!/bin/sh
# The scripted UI tests: tests/uitest.c, tests/skiptest.c and
# tests/damagetest.c drive their own windows through the accessibility tree and
# SDL's events, and exit 0 when every check passed.
#   uitest.sh [-w WRAP] BUILD
set -u

wrap=""
while getopts w: opt; do
    case $opt in
        w) wrap=$OPTARG ;;
        *) echo "usage: uitest.sh [-w WRAP] BUILD" >&2; exit 2 ;;
    esac
done
shift $((OPTIND - 1))
build=$1

mkdir -p smoke
status=0
for name in uitest skiptest damagetest damagetest_text; do
    rm -f "smoke/$name.log"
    $wrap "$build/$name" --log "smoke/$name.log" </dev/null >/dev/null 2>&1
    rc=$?
    grep "test:" "smoke/$name.log" 2>/dev/null || cat "smoke/$name.log" 2>/dev/null || :
    if [ "$rc" != 0 ]; then
        echo "$name: exit status $rc"
        status=$rc
    fi
done
exit "$status"
