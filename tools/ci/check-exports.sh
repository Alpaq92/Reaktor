#!/bin/sh
# What libreaktor.so exports against what it should. tools/reaktor.map names
# Reaktor's API, the non-public calls the modules and tests make, and globs
# for Nuklear's and nukanim's APIs. A REAKTOR_SHARED build must export exactly
# those; every public declaration in include/reaktor/ must be exported or come
# from libreaktor-modules.a; every name the map exports must be declared there
# or sit under a "Not public" comment, and those must still be called.
#   check-exports.sh BUILD
set -eu

build=$1
so="$build/lib/libreaktor.so"
mods="$build/lib/libreaktor-modules.a"
work=$(mktemp -d)
trap 'rm -rf "$work"' EXIT
fail=0

nm -D --defined-only "$so" | awk '$2 != "A" { print $3 }' | sed 's/@.*//' | sort -u >"$work/exported"
# Explicit names, and those under a "Not public" comment.
awk '/^ *#/ { hidden = ($0 ~ /Not public/); next }
     /;/ { n = split($0, w, /[ ;]+/)
           for (k = 1; k <= n; k++) if (w[k] != "" && w[k] !~ /[*:{}]/) print w[k] (hidden ? " hidden" : "") }' \
    tools/reaktor.map >"$work/map"
awk '{ print $1 }' "$work/map" | sort -u >"$work/named"
awk '$2 == "hidden" { print $1 }' "$work/map" | sort -u >"$work/hidden"
grep -h -o -E '\breaktor_[a-z0-9_]+ *\(' include/reaktor/*.h | sed 's/ *($//' | sort -u >"$work/public"
nm --defined-only "$mods" 2>/dev/null | awk '$2 ~ /[TDBR]/ { print $3 }' | sort -u >"$work/modules"
{
    nm -u "$mods" 2>/dev/null | awk '{ print $2 }'
    for f in "$build"/*; do
        [ -f "$f" ] && [ -x "$f" ] && nm -D --undefined-only "$f" 2>/dev/null | awk '{ print $2 }'
    done
} | sed 's/@.*//' | sort -u >"$work/called"

report() {
    if [ -s "$work/$1" ]; then
        echo "$2:"; sed 's/^/  /' "$work/$1"; fail=1
    fi
}
comm -23 "$work/named" "$work/exported" >"$work/stale"
report stale "named in tools/reaktor.map but not exported (gone, or a typo)"
grep -v -E '^(nk_|nka_)' "$work/exported" | comm -23 - "$work/named" >"$work/extra"
report extra "exported but not named in tools/reaktor.map"
comm -23 "$work/public" "$work/exported" | comm -23 - "$work/modules" >"$work/missing"
report missing "declared in include/reaktor/ but neither exported nor in the modules"
grep '^reaktor_' "$work/named" | comm -23 - "$work/public" | comm -23 - "$work/hidden" >"$work/leak"
report leak "exported, not declared in include/reaktor/, and not under a Not public comment"
comm -23 "$work/hidden" "$work/called" >"$work/unused"
report unused "under a Not public comment but no module or test calls them"

[ "$fail" = 0 ] && echo "exports match tools/reaktor.map ($(wc -l <"$work/exported") symbols)"
exit "$fail"
