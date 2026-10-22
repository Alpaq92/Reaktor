#!/bin/sh
# The scripted UI test: tests/uitest.c drives its own window through the
# accessibility tree and SDL's events, and exits 0 when every check passed.
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
rm -f smoke/uitest.log
$wrap "$build/uitest" --log smoke/uitest.log </dev/null >/dev/null 2>&1
rc=$?
grep "test:" smoke/uitest.log 2>/dev/null || cat smoke/uitest.log 2>/dev/null || :
[ "$rc" = 0 ] || echo "uitest: exit status $rc"
exit "$rc"
