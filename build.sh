#!/bin/sh
# build.sh - convenience wrapper around CMake on macOS, Linux and the BSDs.
#
# The counterpart of build.ps1, which does the same job on Windows. All build
# logic lives in CMakeLists.txt, exactly as it does there; this only checks
# that the submodules are present and prefers Ninja when it is installed, so
# that the failure modes are a sentence rather than a page of CMake.
#
#     ./build.sh                 # everything
#     ./build.sh --target NAME   # one CMake target, which is what you want
#                                # while a single library is being brought up
#     ./build.sh --debug         # -DCMAKE_BUILD_TYPE=Debug
#
# Anything after -- is passed to the configure step untouched.
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
        --help|-h) sed -n '2,16p' "$0" | cut -c 3-; exit 0 ;;
        --)        shift; break ;;
        *) echo "build.sh: unknown argument: $1 (try --help)" >&2; exit 2 ;;
    esac
    shift
done

command -v cmake >/dev/null 2>&1 || {
    echo "build.sh: cmake not found on PATH" >&2; exit 1; }

if [ ! -f "$root/third_party/SDL/CMakeLists.txt" ]; then
    echo "build.sh: submodules missing - run: git submodule update --init --recursive" >&2
    exit 1
fi

# Ninja if it is there; otherwise CMake's default generator, which is make
# everywhere this runs. Neither is required to be installed.
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
    echo "built: $out/reaktor"
fi
