#!/bin/sh
# build-wasm.sh - the same sources, linked to a web page.
#
#     ./build-wasm.sh            # build
#     ./build-wasm.sh --serve    # build, then serve it on :8000
#
# Emscripten is looked for in $EMSDK, beside emcc on PATH, in ~/emsdk, then as
# a distribution package. Output is build-wasm/reaktor.html and its .js, .wasm
# and .data, which must be served: a file:// page cannot fetch the .wasm.
set -eu

root=$(CDPATH= cd -- "$(dirname -- "$0")" && pwd)
out="$root/build-wasm"
serve=0
port=8000

while [ $# -gt 0 ]; do
    case "$1" in
        --serve|-s) serve=1 ;;
        --port|-p)
            shift
            [ $# -gt 0 ] || { echo "build-wasm.sh: --port needs a number" >&2; exit 2; }
            port="$1" ;;
        --help|-h) sed -n '2,17p' "$0" | cut -c 3-; exit 0 ;;
        --)        shift; break ;;
        *) echo "build-wasm.sh: unknown argument: $1 (try --help)" >&2; exit 2 ;;
    esac
    shift
done

# $EMSDK first, then the emsdk that owns an emcc already on PATH, then the
# default clone location. Only the toolchain file is actually needed: emcmake
# exists to set CMAKE_TOOLCHAIN_FILE and two cache variables, and passing it
# directly is the same thing without a second process.
emsdk="${EMSDK:-}"
if [ -z "$emsdk" ] && command -v emcc >/dev/null 2>&1; then
    emcc_path=$(command -v emcc)
    emsdk=$(CDPATH= cd -- "$(dirname -- "$emcc_path")/../.." && pwd)
fi
[ -n "$emsdk" ] || emsdk="$HOME/emsdk"

toolchain="$emsdk/upstream/emscripten/cmake/Modules/Platform/Emscripten.cmake"

# Nothing there means either no Emscripten at all or a packaged one: the BSD
# ports and the Linux distributions ship the same tree with no emsdk around it,
# dropped into a libdir with emcc symlinked onto PATH. Only reached once the
# emsdk layout above has come up empty, so an emsdk install takes the path it
# always did.
if [ ! -f "$toolchain" ] && command -v emcc >/dev/null 2>&1; then
    emcc_bin=$(command -v emcc)
    emcc_dir=$(dirname -- "$emcc_bin")
    # Following the symlink finds the root whatever the package called it.
    # readlink -f is absent on macOS before 12.3, so two spelled-out layouts
    # stand behind it.
    packaged=$(readlink -f "$emcc_bin" 2>/dev/null || echo "")
    if [ -n "$packaged" ]; then
        packaged=$(dirname -- "$packaged")
    fi
    for candidate in "$packaged" "$emcc_dir/../lib/emscripten" "$emcc_dir/../share/emscripten"; do
        [ -n "$candidate" ] || continue
        [ -f "$candidate/cmake/Modules/Platform/Emscripten.cmake" ] || continue
        emsdk=$(CDPATH= cd -- "$candidate" && pwd)
        toolchain="$emsdk/cmake/Modules/Platform/Emscripten.cmake"
        break
    done
fi

if [ ! -f "$toolchain" ]; then
    echo "build-wasm.sh: no Emscripten found (looked for an emsdk at $emsdk)" >&2
    echo "  clone https://github.com/emscripten-core/emsdk, run './emsdk install latest'" >&2
    echo "  then './emsdk activate latest', or set EMSDK to an existing one" >&2
    echo "  or install your system's emscripten package" >&2
    exit 1
fi

command -v cmake >/dev/null 2>&1 || {
    echo "build-wasm.sh: cmake not found on PATH" >&2; exit 1; }

if [ ! -f "$root/third_party/SDL/CMakeLists.txt" ]; then
    echo "build-wasm.sh: submodules missing - run: git submodule update --init --recursive" >&2
    exit 1
fi

# emcc needs node and the rest of the sdk on PATH, which emsdk_env.sh sets up.
# It writes its banner to stdout, so it is silenced rather than allowed to look
# like build output.
if [ -f "$emsdk/emsdk_env.sh" ]; then
    # shellcheck disable=SC1091
    . "$emsdk/emsdk_env.sh" >/dev/null 2>&1
fi

if command -v ninja >/dev/null 2>&1; then
    set -- -G Ninja "$@"
fi

cmake -S "$root" -B "$out" \
    -DCMAKE_TOOLCHAIN_FILE="$toolchain" \
    -DCMAKE_BUILD_TYPE=Release "$@"
cmake --build "$out" --parallel

echo
echo "built: $out/reaktor.html"
echo "a file:// page cannot fetch the .wasm, so serve it:"
echo "  python3 -m http.server -d $out $port"

if [ "$serve" -eq 1 ]; then
    exec python3 -m http.server -d "$out" "$port"
fi
