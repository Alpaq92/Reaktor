#!/bin/sh
# make-bundle.sh - assemble build/reaktor.app around the plain binary.
#
#   make-bundle.sh BUNDLE BINARY INFOPLIST SOURCE_ROOT
#
# A CMake POST_BUILD step on macOS. The CLI binary and the Finder bundle both
set -eu

bundle=$1
binary=$2
infoplist=$3
root=$4

rm -rf "$bundle"
mkdir -p "$bundle/Contents/MacOS"
mkdir -p "$bundle/Contents/Resources"

cp "$binary"    "$bundle/Contents/MacOS/reaktor"
cp "$infoplist" "$bundle/Contents/Info.plist"

if command -v iconutil >/dev/null 2>&1 && command -v sips >/dev/null 2>&1 \
   && [ -f "$root/assets/icons/reaktor-icon.png" ]; then
    iconset=$(mktemp -d "${TMPDIR:-/tmp}/reaktor-iconset.XXXXXX")/reaktor.iconset
    mkdir -p "$iconset"
    for pair in "16 icon_16x16" "32 icon_16x16@2x" \
                "32 icon_32x32" "64 icon_32x32@2x" \
                "128 icon_128x128" "256 icon_128x128@2x" \
                "256 icon_256x256" "512 icon_256x256@2x" \
                "512 icon_512x512" "1024 icon_512x512@2x"
    do
        px=${pair% *}; name=${pair#* }
        sips -z "$px" "$px" "$root/assets/icons/reaktor-icon.png" \
             --out "$iconset/$name.png" >/dev/null 2>&1
    done
    iconutil -c icns "$iconset" -o "$bundle/Contents/Resources/reaktor.icns"
    rm -rf "$(dirname "$iconset")"
fi

copy() {
    src=$1
    rel=$2
    mkdir -p "$bundle/Contents/Resources/$(dirname "$rel")"
    cp "$src" "$bundle/Contents/Resources/$rel"
}

for f in \
    external/tinycss/src/variables-dark.css \
    external/tinycss/src/variables-light.css \
    external/tinycss/src/core.css \
    assets/fonts/Aileron-Regular.otf \
    assets/fonts/Aileron-Bold.otf \
    assets/icons/reaktor-icon.svg
do
    if [ -f "$root/$f" ]; then
        copy "$root/$f" "$f"
    fi
done

# The same set CMake scans for the WASM preload; keep the two in step.
srcs=$(ls "$root/src/"*.h "$root/core/"*/*.c "$root/core/"*/*.h \
          "$root/platform/"*.c "$root/platform/"*/*.c \
          "$root/runtime/"*.c "$root/samples/"*.h \
          "$root/samples/"*/*.c "$root/samples/"*/*.h 2>/dev/null)
mkdir -p "$bundle/Contents/Resources/external/ionicons/src/svg"
{
    grep -ohE '[a-z0-9]+(-[a-z0-9]+)*-outline' $srcs 2>/dev/null
    grep -ohE '"[a-z0-9]+(-[a-z0-9]+)*"' $srcs 2>/dev/null | tr -d '"'
} | sort -u | while read -r name; do
    src="$root/external/ionicons/src/svg/$name.svg"
    if [ -f "$src" ]; then
        cp "$src" "$bundle/Contents/Resources/external/ionicons/src/svg/"
    fi
done

echo "assembled: $bundle"
