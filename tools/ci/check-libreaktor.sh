#!/bin/sh
# Installs libreaktor from a build into dist/NAME, links examples/consumer
# against it, checks that program draws what the built simple does, and
# leaves dist/NAME.tar.gz. Where the install has libreaktor.so or
# Reaktor.framework (a REAKTOR_SHARED build), the consumer is linked against
# that too, and must draw the same.
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
    cmake --install "$build" --prefix "dist/$name" --component libreaktor --strip </dev/null
fi
cmake -S examples/consumer -B build-consumer -G Ninja -DCMAKE_BUILD_TYPE=Release \
      "-DCMAKE_PREFIX_PATH=$PWD/dist/$name" </dev/null
cmake --build build-consumer </dev/null
runs="lib-built $build/simple
lib-linked build-consumer/simple"
if [ -f "dist/$name/lib/libreaktor.so" ] || [ -d "dist/$name/Frameworks/Reaktor.framework" ]; then
    cmake -S examples/consumer -B build-consumer-shared -G Ninja -DCMAKE_BUILD_TYPE=Release \
          "-DCMAKE_PREFIX_PATH=$PWD/dist/$name" -DCONSUMER_SHARED=ON </dev/null
    cmake --build build-consumer-shared </dev/null
    if command -v otool >/dev/null 2>&1; then
        otool -L build-consumer-shared/simple | grep -q 'Reaktor\.framework/'
    else
        readelf -d build-consumer-shared/simple | grep -q 'NEEDED.*\[libreaktor\.so\.'
    fi || { echo "the shared consumer does not link the shared libreaktor"; exit 1; }
    runs="$runs
lib-shared build-consumer-shared/simple"
fi
mkdir -p check
echo "$runs" | while read -r from; do
    set -- $from
    rc=0
    $wrap "$2" --theme light --video dummy \
        --shot "check/$1.bmp" --a11y-dump "check/$1.txt" </dev/null || rc=$?
    if [ "$rc" != 0 ]; then
        # Where it died, before giving up on it.
        if command -v lldb >/dev/null 2>&1; then
            lldb --batch -o run -o "bt all" -- "$2" --theme light --video dummy \
                --shot "check/$1.bmp" --a11y-dump "check/$1.txt" </dev/null || :
        elif command -v gdb >/dev/null 2>&1; then
            gdb -batch -ex run -ex "thread apply all bt" --args "$2" --theme light \
                --video dummy --shot "check/$1.bmp" --a11y-dump "check/$1.txt" </dev/null || :
        fi
        echo "$1: $2 exited with status $rc"; exit "$rc"
    fi
done
for e in bmp txt; do
    cmp "check/lib-built.$e" "check/lib-linked.$e"
    if [ -f "check/lib-shared.$e" ]; then cmp "check/lib-built.$e" "check/lib-shared.$e"; fi
done
tar -C dist -czf "dist/$name.tar.gz" "$name"
rm -rf "dist/$name" build-consumer build-consumer-shared
