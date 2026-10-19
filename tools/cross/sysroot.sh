#!/bin/sh
# Gathers an arm64 sysroot on a FreeBSD or NetBSD x86-64 host: the release's
# sets for headers, libraries and start files, and the packages SDL and the
# modules link, unpacked where the toolchain files expect them.
#   sysroot.sh <freebsd|netbsd> <version> <dir>
set -eu

os=$1
version=$2
dir=$3
tmp=${TMPDIR:-/tmp}/sysroot-fetch
mkdir -p "$dir" "$tmp"

case "$os" in
freebsd)
    url="https://download.freebsd.org/releases/arm64/aarch64/$version-RELEASE"
    fetch -q -o "$tmp/base.txz" "$url/base.txz"
    fetch -q -o "$tmp/MANIFEST" "$url/MANIFEST"
    want=$(awk '$1 == "base.txz" { print $2 }' "$tmp/MANIFEST")
    [ "$(sha256 -q "$tmp/base.txz")" = "$want" ] || { echo "base.txz: checksum mismatch" >&2; exit 1; }
    # Not as root: tar would restore the sets' schg flags and the tree could not be removed.
    tar -xf "$tmp/base.txz" -C "$dir" ./lib ./usr/lib ./usr/include ./usr/libdata/pkgconfig ./bin/sh
    major=${version%%.*}
    minor=${version#*.}
    pkgs="libX11 libXext libXcursor libXi libXfixes libXrandr libXrender libXtst
          libxkbcommon wayland libglvnd dbus xorgproto"
    rm -rf "$tmp/pkgs"
    # pkg's ABI override reads the arm64 catalog; the ELF note of the sysroot's sh is the fallback.
    sudo pkg -o ABI="FreeBSD:$major:aarch64" -o OSVERSION="$((major * 100000 + minor * 1000))" \
             -o IGNORE_OSVERSION=yes -o PKG_DBDIR=/var/db/pkg-aarch64 -o PKG_CACHEDIR=/var/cache/pkg-aarch64 \
             fetch -y -d -o "$tmp/pkgs" $pkgs </dev/null ||
    sudo pkg -o ABI_FILE="$dir/bin/sh" -o IGNORE_OSVERSION=yes \
             -o PKG_DBDIR=/var/db/pkg-aarch64 -o PKG_CACHEDIR=/var/cache/pkg-aarch64 \
             fetch -y -d -o "$tmp/pkgs" $pkgs </dev/null
    find "$tmp/pkgs" -name '*.pkg' | while IFS= read -r p; do
        tar -xf "$p" -C "$dir" --exclude '+*'
    done
    [ "$(find "$tmp/pkgs" -name '*.pkg' | wc -l)" -gt 0 ] || { echo "no packages were fetched" >&2; exit 1; }
    ;;
netbsd)
    url="https://cdn.NetBSD.org/pub/NetBSD/NetBSD-$version/evbarm-aarch64/binary/sets"
    ftp -V -o "$tmp/SHA512" "$url/SHA512"
    for set in base comp xbase xcomp; do
        ftp -V -o "$tmp/$set.tar.xz" "$url/$set.tar.xz"
        want=$(awk -v f="$set.tar.xz" '$2 == "(" f ")" { print $4 }' "$tmp/SHA512")
        got=$(cksum -a SHA512 "$tmp/$set.tar.xz" | awk '{ print $4 }')
        [ "$got" = "$want" ] || { echo "$set.tar.xz: checksum mismatch" >&2; exit 1; }
        tar -xJf "$tmp/$set.tar.xz" -C "$dir"
    done
    # The host's package repository, read for the same quarterly, with the architecture swapped.
    repo=""
    if [ -f /usr/pkg/etc/pkgin/repositories.conf ]; then
        repo=$(grep -v '^#' /usr/pkg/etc/pkgin/repositories.conf | grep . | head -1 | sed -e 's/\$arch/aarch64/' -e 's/\${arch}/aarch64/' -e 's/x86_64/aarch64/')
    fi
    [ -n "$repo" ] || repo="https://cdn.NetBSD.org/pub/pkgsrc/packages/NetBSD/aarch64/$version/All"
    repo=${repo%/}
    case "$repo" in *aarch64*) ;; *) echo "sysroot: $repo is not an aarch64 repository" >&2; exit 1 ;; esac
    echo "sysroot: packages from $repo"
    rm -rf "$tmp/pkgs"
    mkdir -p "$tmp/pkgs"
    ftp -V -o "$tmp/pkgs/pkg_summary.gz" "$repo/pkg_summary.gz"
    # The closure of what SDL and the modules link, resolved from the summary
    # so no directory listing is read.
    gunzip -c "$tmp/pkgs/pkg_summary.gz" | awk '
        /^PKGNAME=/  { name = substr($0, 9); sub(/-[^-]*$/, "", name) }
        /^FILE_NAME=/ { file[name] = substr($0, 11) }
        /^DEPENDS=/  { d = substr($0, 9); sub(/[<>=\[{].*$/, "", d); sub(/-[0-9][^-]*$/, "", d); sub(/-$/, "", d); deps[name] = deps[name] " " d }
        END {
            n = split("dbus libxkbcommon wayland wayland-protocols", want, " ")
            for (i = 1; i <= n; i++) queue[++q] = want[i]
            for (i = 1; i <= q; i++) {
                p = queue[i]
                if (p in seen || !(p in file)) continue
                seen[p] = 1
                print file[p]
                m = split(deps[p], ds, " ")
                for (j = 1; j <= m; j++) if (ds[j] != "") queue[++q] = ds[j]
            }
        }' > "$tmp/pkgs/list"
    while IFS= read -r f; do
        ftp -V -o "$tmp/pkgs/$f" "$repo/$f"
    done < "$tmp/pkgs/list"
    mkdir -p "$dir/var/db/pkg"
    while IFS= read -r f; do
        PKG_PATH="$tmp/pkgs" pkg_add -m aarch64 -I -P "$dir" -K "$dir/var/db/pkg" "$tmp/pkgs/$f"
    done < "$tmp/pkgs/list"
    ;;
*)
    echo "sysroot.sh: unknown system $os" >&2
    exit 2
    ;;
esac

# A symbolic link to an absolute path would reach the host's own libraries.
escaped=$(find "$dir" -type l -lname '/*' 2>/dev/null || true)
if [ -n "$escaped" ]; then
    echo "sysroot: links reach outside it:" >&2
    echo "$escaped" >&2
    exit 1
fi
echo "sysroot: $dir ($(du -sh "$dir" | cut -f1))"
