#!/bin/sh
# build.sh - CMake wrapper for macOS, Linux and the BSDs. build.ps1 on Windows.
#
#     ./build.sh                 # everything
#     ./build.sh --target NAME   # one target
#     ./build.sh --debug         # -DCMAKE_BUILD_TYPE=Debug
#     --                         # pass the rest to cmake
set -eu

root=$(CDPATH= cd -- "$(dirname -- "$0")" && pwd)
out="$root/build"
target=""
buildtype="Release"

while [ $# -gt 0 ]; do
    case "$1" in
        --target|-t)
            shift
            [ $# -gt 0 ] || { echo "build.sh: --target needs a name" >&2; exit 2; }
            target="$1" ;;
        --debug)   buildtype="Debug" ;;
        --release) buildtype="Release" ;;
        --help|-h) sed -n '2,7p' "$0" | cut -c 3-; exit 0 ;;
        --)        shift; break ;;
        *) echo "build.sh: unknown argument: $1 (try --help)" >&2; exit 2 ;;
    esac
    shift
done

command -v cmake >/dev/null 2>&1 || {
    echo "build.sh: cmake not found on PATH" >&2; exit 1; }

if [ ! -f "$root/external/SDL/CMakeLists.txt" ]; then
    echo "build.sh: submodules missing - run: git submodule update --init --recursive" >&2
    exit 1
fi

if command -v ninja >/dev/null 2>&1; then
    set -- -G Ninja "$@"
fi

cmake -S "$root" -B "$out" -DCMAKE_BUILD_TYPE="$buildtype" "$@"

if [ -n "$target" ]; then
    cmake --build "$out" --parallel --target "$target"
    echo
    echo "built target: $target"
else
    cmake --build "$out" --parallel
    echo
    echo "built: $out/showcase (plus simple and notepad)"
fi
