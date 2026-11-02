#!/bin/sh
# Installs libreaktor from a build into dist/NAME, links examples/consumer
# against it, checks that program draws what the built simple does, and
# leaves dist/NAME.tar.gz. Where the install has libreaktor.so (a
# REAKTOR_SHARED build), the consumer is linked against that too, and must
# draw the same.
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
runs="lib-built $build/simple
lib-linked build-consumer/simple"
if [ -f "dist/$name/lib/libreaktor.so" ]; then
    cmake -S examples/consumer -B build-consumer-shared -G Ninja -DCMAKE_BUILD_TYPE=Release \
          "-DCMAKE_PREFIX_PATH=$PWD/dist/$name" -DCONSUMER_SHARED=ON </dev/null
    cmake --build build-consumer-shared </dev/null
    readelf -d build-consumer-shared/simple | grep -q 'NEEDED.*\[libreaktor\.so\.' ||
        { echo "the shared consumer does not link libreaktor.so"; exit 1; }
    runs="$runs
lib-shared build-consumer-shared/simple"
fi
mkdir -p check
echo "$runs" | while read -r from; do
    set -- $from
    $wrap "$2" --theme light --video dummy \
        --shot "check/$1.bmp" --a11y-dump "check/$1.txt" </dev/null
done
for e in bmp txt; do
    cmp "check/lib-built.$e" "check/lib-linked.$e"
    if [ -f "check/lib-shared.$e" ]; then cmp "check/lib-built.$e" "check/lib-shared.$e"; fi
done
tar -C dist -czf "dist/$name.tar.gz" "$name"
rm -rf "dist/$name" build-consumer build-consumer-shared
