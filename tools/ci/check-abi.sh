#!/bin/sh
# What libreaktor.so's ABI is beyond its symbols: the layout of every struct
# an app sees (Reaktor's own, and Nuklear's through reaktor/nuklear.h, with
# nk_window and nk_input, which apps reach through the context) and the
# value of every constant reaktor/nuklear.h renames. gdb reads them from the
# debug types of tools/abi/abi.c, so nothing is copied out of Nuklear. The
# result must match tools/abi/libreaktor.so.SOVERSION-ARCH.abi; a change means
# a new SOVERSION in CMakeLists.txt and a new table, written with -u.
#   check-abi.sh [-u] [-c CC]
set -eu

update=0
cc=${CC:-cc}
while getopts uc: opt; do
    case $opt in
        u) update=1 ;;
        c) cc=$OPTARG ;;
        *) echo "usage: check-abi.sh [-u] [-c CC]" >&2; exit 2 ;;
    esac
done

so=$(sed -n 's/.*SOVERSION \([0-9][0-9]*\).*/\1/p' CMakeLists.txt | head -n 1)
[ -n "$so" ] || { echo "no SOVERSION in CMakeLists.txt"; exit 1; }
arch=$(uname -m)
table="tools/abi/libreaktor.so.$so-$arch.abi"
work=$(mktemp -d)
trap 'rm -rf "$work"' EXIT

"$cc" -g -O0 -fno-eliminate-unused-debug-types -Iinclude -Iexternal/nuklear \
      -Iexternal/nukanim -Iexternal/SDL/include -c tools/abi/abi.c -o "$work/abi.o"

{
    sed -n 's/^typedef \(struct\|union\|enum\) \(reaktor_[a-z0-9_]*\).*/\1 \2/p' include/reaktor/*.h |
        sort -u | while read -r kind name; do
            if [ "$kind" = enum ]; then echo "ptype enum $name"; else echo "ptype /o $name"; fi
        done
    for n in window input; do echo "ptype /o struct nk_$n"; done
    sed -n 's/^#define reaktor_[a-z0-9_]* *nk_\([a-z0-9_]*\)$/\1/p' include/reaktor/nuklear.h |
        while read -r n; do
            printf 'echo nk_%s:\\n\nptype /o struct nk_%s\nptype nk_%s\n' "$n" "$n" "$n"
        done
    sed -n 's/^#define \(REAKTOR_[A-Z0-9_]*\) *\(NK_[A-Z0-9_]*\)$/\1 \2/p' include/reaktor/nuklear.h |
        while read -r r n; do printf 'echo %s = \noutput/d %s\necho \\n\n' "$r" "$n"; done
} >"$work/cmds.gdb"
# One -ex each: a command file would stop at the first type that is not a
# struct (nk_bool, say), and an -ex that fails does not stop the rest.
set --
while IFS= read -r c; do set -- "$@" -ex "$c"; done <"$work/cmds.gdb"
gdb -batch -nx "$@" "$work/abi.o" >"$work/abi.txt" 2>&1 || :

# Kept where CI uploads it, so a table can be taken from a machine like CI's:
# gdb's layout of the output differs a little between versions.
mkdir -p smoke/abi
cp "$work/abi.txt" "smoke/abi/$(basename "$table")"

if [ "$update" = 1 ]; then
    cp "$work/abi.txt" "$table"
    echo "wrote $table"
elif [ ! -f "$table" ]; then
    echo "no $table: write it with -u"; exit 1
elif ! diff -u "$table" "$work/abi.txt"; then
    echo "libreaktor's ABI changed: bump SOVERSION in CMakeLists.txt and run check-abi.sh -u"
    exit 1
else
    echo "ABI matches $table"
fi
