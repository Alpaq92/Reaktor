#!/bin/sh
# Installs libreaktor from a build into dist/NAME, links examples/consumer
# against it, checks that program draws what the built simple does, and
# leaves dist/NAME.tar.gz.
#   check-libreaktor.sh [-i] [-w WRAP] BUILD NAME
# -i: dist/NAME is installed already (a cross build ships it).
set -eu

wrap=""
installed=0
while getopts iw: opt; do
    case $opt in
        i) installed=1 ;;
        w) wrap=$OPTARG ;;
        *) echo "usage: check-libreaktor.sh [-i] [-w WRAP] BUILD NAME" >&2; exit 2 ;;
    esac
done
shift $((OPTIND - 1))
build=$1
name=$2

if [ "$installed" = 0 ]; then
    cmake --build "$build" --target libreaktor </dev/null
    cmake --install "$build" --prefix "dist/$name" --component libreaktor </dev/null
fi
cmake -S examples/consumer -B build-consumer -G Ninja -DCMAKE_BUILD_TYPE=Release \
      "-DCMAKE_PREFIX_PATH=$PWD/dist/$name" </dev/null
cmake --build build-consumer </dev/null
mkdir -p check
for from in "lib-built $build/simple" "lib-linked build-consumer/simple"; do
    set -- $from
    $wrap "$2" --theme light --video dummy \
        --shot "check/$1.bmp" --a11y-dump "check/$1.txt" </dev/null
done
for e in bmp txt; do
    cmp "check/lib-built.$e" "check/lib-linked.$e"
done
tar -C dist -czf "dist/$name.tar.gz" "$name"
rm -rf "dist/$name" build-consumer
