#!/bin/sh
# What libreaktor.so exports against what it should. tools/reaktor.map names
# Reaktor's API and globs Nuklear's and nukanim's under LIBREAKTOR_0, and the
# non-public calls the modules make under LIBREAKTOR_PRIVATE_0. A
# REAKTOR_SHARED build must export exactly those, each under its version;
# every public declaration in include/reaktor/ must be exported or come from
# libreaktor-modules.a; every LIBREAKTOR_0 name must be declared there; every
# private one must still be called by the modules, and by no app's own code.
#   check-exports.sh BUILD
set -eu

build=$1
so="$build/lib/libreaktor.so"
mods="$build/lib/libreaktor-modules.a"
for f in "$so" "$mods"; do
    if [ ! -f "$f" ]; then
        echo "no $f: build the libreaktor target first" >&2; exit 1
    fi
done
work=$(mktemp -d)
trap 'rm -rf "$work"' EXIT
fail=0

nm -D --defined-only "$so" | awk '$2 != "A" { print $3 }' | sort -u >"$work/versioned"
sed 's/@.*//' "$work/versioned" | sort -u >"$work/exported"
# Explicit names, and those in the private version.
awk '/^ *#/ || /^}/ { next }
     /^LIBREAKTOR_[A-Z0-9_]* *\{/ { hidden = ($1 ~ /PRIVATE/); next }
     /;/ { n = split($0, w, /[ ;]+/)
           for (k = 1; k <= n; k++) if (w[k] != "" && w[k] !~ /[*:{}]/) print w[k] (hidden ? " hidden" : "") }' \
    tools/reaktor.map >"$work/map"
awk '{ print $1 }' "$work/map" | sort -u >"$work/named"
awk '$2 == "hidden" { print $1 }' "$work/map" | sort -u >"$work/hidden"
grep -h -o -E '\breaktor_[a-z0-9_]+ *\(' include/reaktor/*.h | sed 's/ *($//' | sort -u >"$work/public"
nm --defined-only "$mods" 2>/dev/null | awk '$2 ~ /[TDBR]/ { print $3 }' | sort -u >"$work/modules"
nm -u "$mods" 2>/dev/null | awk '{ print $2 }' | sort -u >"$work/called"
# The apps' own objects (not their modules') in every program linked to it.
for f in "$build"/*; do
    [ -f "$f" ] && [ -x "$f" ] || continue
    readelf -d "$f" 2>/dev/null | grep -q 'NEEDED.*\[libreaktor\.so\.' || continue
    find "$build/CMakeFiles/$(basename "$f").dir" \( -path '*/samples/*' -o -path '*/tests/*' \) \
         -name '*.o' -exec nm -u {} + 2>/dev/null | awk '{ print $2 }'
done | sort -u >"$work/appcalls"

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
report leak "under LIBREAKTOR_0 but not declared in include/reaktor/"
comm -23 "$work/hidden" "$work/called" >"$work/unused"
report unused "under LIBREAKTOR_PRIVATE_0 but no module calls them"
comm -12 "$work/hidden" "$work/appcalls" >"$work/reached"
report reached "private, but an app's own code calls them"
awk -F'@@?' 'NR == FNR { if ($0 !~ /^(nk_|nka_)/) { split($0, h, " "); priv[h[1]] = (h[2] == "hidden") }; next }
     NF > 1 && ($1 in priv) && ($2 != (priv[$1] ? "LIBREAKTOR_PRIVATE_0" : "LIBREAKTOR_0")) { print $0 }' \
    "$work/map" "$work/versioned" >"$work/version"
report version "exported under the wrong version"

[ "$fail" = 0 ] && echo "exports match tools/reaktor.map ($(wc -l <"$work/exported") symbols)"
exit "$fail"
