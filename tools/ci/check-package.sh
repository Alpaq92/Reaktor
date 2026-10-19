#!/bin/sh
# Packs a build with tools/package.sh, checks the packed showcase draws what
# the built one does, and leaves dist/NAME.tar.gz.
#   check-package.sh [-w WRAP] BUILD NAME
set -eu

wrap=""
while getopts w: opt; do
    case $opt in
        w) wrap=$OPTARG ;;
        *) echo "usage: check-package.sh [-w WRAP] BUILD NAME" >&2; exit 2 ;;
    esac
done
shift $((OPTIND - 1))
build=$1
name=$2

sh tools/package.sh "$build" "dist/$name"
mkdir -p check
for from in "built $build" "packed dist/$name"; do
    set -- $from
    $wrap "$2/showcase" --tab 8 --theme light --video dummy \
        --shot "check/$1.bmp" --a11y-dump "check/$1.txt" </dev/null
done
for e in bmp txt; do
    cmp "check/built.$e" "check/packed.$e"
done
tar -C dist -czf "dist/$name.tar.gz" "$name"
rm -rf "dist/$name"
