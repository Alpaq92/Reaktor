#!/bin/sh
set -eu

root=$1
dest=$2

copy() {
    mkdir -p "$dest/$(dirname "$1")"
    cp "$root/$1" "$dest/$1"
}

for f in assets/fonts/MPLUS1p-Regular.ttf assets/fonts/MPLUS1p-Notice.txt \
         assets/fonts/Aileron-Notice.txt external/simplecss/simple.css; do
    copy "$f"
done
for f in "$root"/assets/locale/*.txt; do
    copy "assets/locale/$(basename "$f")"
done

# Same set as the WASM scan in CMakeLists.txt.
srcs=$(ls "$root/src/"*.h "$root/core/"*/*.c "$root/core/"*/*.h \
          "$root/platform/"*.c "$root/platform/"*/*.c \
          "$root/runtime/"*.c \
          "$root/samples/"*/*.c "$root/samples/"*/*.h 2>/dev/null || :)
{
    grep -ohE '[a-z0-9]+(-[a-z0-9]+)*-outline' $srcs 2>/dev/null
    grep -ohE '"[a-z0-9]+(-[a-z0-9]+)*"' $srcs 2>/dev/null | tr -d '"'
} | sort -u | while read -r name; do
    if [ -f "$root/external/ionicons/src/svg/$name.svg" ]; then
        copy "external/ionicons/src/svg/$name.svg"
    fi
done
