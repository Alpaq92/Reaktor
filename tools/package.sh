#!/bin/sh
set -eu

build=$1
dest=$2
ext=${3-}
root=$(CDPATH= cd -- "$(dirname -- "$0")/.." && pwd)

rm -rf "$dest"
mkdir -p "$dest/licenses"
for p in showcase notepad simple bench; do
    cp "$build/$p$ext" "$dest/"
done
if [ -d "$build/reaktor.app" ]; then
    cp -R "$build/reaktor.app" "$dest/"
fi
: >"$dest/.reaktor-root"
sh "$root/tools/assets.sh" "$root" "$dest"
cp "$build/licenses/"*.txt "$dest/licenses/"
cp "$root/external/simplecss/LICENSE" "$dest/licenses/simplecss.txt"
cp "$root/docs/NOTICE.md" "$dest/"
echo "packaged: $dest"
