#!/bin/sh
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

sh "$root/tools/assets.sh" "$root" "$bundle/Contents/Resources"

echo "assembled: $bundle"
