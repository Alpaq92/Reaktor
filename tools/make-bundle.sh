#!/bin/sh
# make-bundle.sh - assemble build/reaktor.app around the plain binary.
#
#   make-bundle.sh BUNDLE BINARY INFOPLIST SOURCE_ROOT
#
# A CMake POST_BUILD step on macOS. The CLI binary and the Finder bundle both
# survive and share one copy of the executable. Rebuilt from scratch each time.
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

# Icon: rasterise branding/reaktor-icon.png (1024×1024) into the six sizes
# macOS wants and pack them with iconutil. Done here rather than checked in
# because .icns is a container of PNGs and the PNG is what actually changes;
# iconutil is in every Xcode CommandLineTools install so it is already on
# any machine that can build this. Silent on missing tools - a bundle
# without an icon still launches, it just shows the generic app diamond.
if command -v iconutil >/dev/null 2>&1 && command -v sips >/dev/null 2>&1 \
   && [ -f "$root/branding/reaktor-icon.png" ]; then
    iconset=$(mktemp -d "${TMPDIR:-/tmp}/reaktor-iconset.XXXXXX")/reaktor.iconset
    mkdir -p "$iconset"
    # sips writes to stdout on success but very verbosely; drop it.
    for pair in "16 icon_16x16" "32 icon_16x16@2x" \
                "32 icon_32x32" "64 icon_32x32@2x" \
                "128 icon_128x128" "256 icon_128x128@2x" \
                "256 icon_256x256" "512 icon_256x256@2x" \
                "512 icon_512x512" "1024 icon_512x512@2x"
    do
        px=${pair% *}; name=${pair#* }
        sips -z "$px" "$px" "$root/branding/reaktor-icon.png" \
             --out "$iconset/$name.png" >/dev/null 2>&1
    done
    iconutil -c icns "$iconset" -o "$bundle/Contents/Resources/reaktor.icns"
    rm -rf "$(dirname "$iconset")"
fi

# Assets the code opens by string literal - the same three families the WASM
# preload picks up: stylesheets, fonts, the brand SVG, and every ionicon
# referenced from src/. The layout inside Resources mirrors the source tree
# so nothing about the paths in the code has to change.

copy() {
    src=$1
    rel=$2
    mkdir -p "$bundle/Contents/Resources/$(dirname "$rel")"
    cp "$src" "$bundle/Contents/Resources/$rel"
}

# Stylesheets and fonts and the brand mark.
for f in \
    third_party/tinycss/src/variables-dark.css \
    third_party/tinycss/src/variables-light.css \
    third_party/tinycss/src/core.css \
    assets/fonts/Aileron-Regular.otf \
    assets/fonts/Aileron-Bold.otf \
    branding/reaktor-icon.svg
do
    if [ -f "$root/$f" ]; then
        copy "$root/$f" "$f"
    fi
done

# Ionicons: find every "-outline" name and every bare-quoted lowercase name in
# src/. This is the same two-pattern strategy the WASM build uses - see the
# comment in CMakeLists.txt around WASM_ICONS - so the bundle carries exactly
# the set of glyphs the app can ask for.
mkdir -p "$bundle/Contents/Resources/third_party/ionicons/src/svg"
{
    grep -ohE '[a-z0-9]+(-[a-z0-9]+)*-outline' "$root/src/"*.c "$root/src/"*.h 2>/dev/null
    grep -ohE '"[a-z0-9]+(-[a-z0-9]+)*"' "$root/src/"*.c "$root/src/"*.h 2>/dev/null \
        | tr -d '"'
} | sort -u | while read -r name; do
    src="$root/third_party/ionicons/src/svg/$name.svg"
    if [ -f "$src" ]; then
        cp "$src" "$bundle/Contents/Resources/third_party/ionicons/src/svg/"
    fi
done

# A bundle is not itself the sentinel - the walk-up logic still respects
# .reaktor-root when the binary is run outside the bundle, and refuses to
# recurse into Contents/. But when reaktor_root() detects it is inside a
# bundle it uses Contents/Resources directly (see src/util.c), which is why
# there is no marker file to write here.
echo "assembled: $bundle"
