#!/bin/sh
# .single_instance end to end: a second showcase hands its file to the first
# and exits 0, and the first logs the file it was given.
#   instance.sh [-w WRAP] BUILD
set -u

wrap=""
while getopts w: opt; do
    case $opt in
        w) wrap=$OPTARG ;;
        *) echo "usage: instance.sh [-w WRAP] BUILD" >&2; exit 2 ;;
    esac
done
shift $((OPTIND - 1))
build=$1

mkdir -p smoke
rm -f smoke/instance.log
$wrap "$build/showcase" --video dummy --log smoke/instance.log </dev/null >/dev/null 2>&1 &
first=$!
n=0
while [ ! -s smoke/instance.log ] && [ "$n" -lt 60 ]; do n=$((n + 1)); sleep 1; done
sleep 2

fail=0
$wrap "$build/showcase" --video dummy handed-over.txt </dev/null >smoke/instance-second.log 2>&1
rc=$?
if [ "$rc" -ne 0 ]; then echo "second launch: exit status $rc"; fail=1; fi
sleep 2
if grep -q "handed-over.txt" smoke/instance.log; then
    echo "second launch handed its file over: ok"
else
    echo "the running copy did not get the file"
    fail=1
fi
if ! kill -0 "$first" 2>/dev/null; then echo "the running copy is gone"; fail=1; fi
kill "$first" 2>/dev/null || :
wait "$first" 2>/dev/null || :
[ "$fail" = 0 ] || { cat smoke/instance.log smoke/instance-second.log 2>/dev/null || :; }
exit "$fail"
