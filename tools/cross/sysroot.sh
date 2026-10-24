#!/bin/sh
# Gathers an arm64 sysroot on a FreeBSD, OpenBSD or NetBSD x86-64 host: the release's
# sets for headers, libraries and start files, and the packages SDL and the
# modules link, unpacked where the toolchain files expect them.
#   sysroot.sh <freebsd|openbsd|netbsd> <version> <dir>
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
    # The CDN drops a request now and then, and ftp -V says nothing when it does.
    get() {
        n=1
        until ftp -V -o "$1" "$2"; do
            [ "$n" -lt 4 ] || { echo "sysroot: could not fetch $2" >&2; return 1; }
            n=$((n + 1))
            echo "sysroot: $2 failed, try $n of 4" >&2
            sleep 10
        done
    }
    url="https://cdn.NetBSD.org/pub/NetBSD/NetBSD-$version/evbarm-aarch64/binary/sets"
    get "$tmp/SHA512" "$url/SHA512"
    for set in base comp xbase xcomp; do
        get "$tmp/$set.tar.xz" "$url/$set.tar.xz"
        want=$(awk -v f="$set.tar.xz" '$2 == "(" f ")" { print $4 }' "$tmp/SHA512")
        got=$(cksum -a SHA512 "$tmp/$set.tar.xz" | awk '{ print $4 }')
        [ "$got" = "$want" ] || { echo "$set.tar.xz: checksum mismatch" >&2; exit 1; }
        tar -xJf "$tmp/$set.tar.xz" -C "$dir"
    done
    # The host's package repository, read for the same quarterly, with the architecture swapped.
    repo=""
    if [ -f /usr/pkg/etc/pkgin/repositories.conf ]; then
        repo=$(grep -v '^#' /usr/pkg/etc/pkgin/repositories.conf | grep . | head -1 |
               sed -e 's#^ftp://ftp\.NetBSD\.org#https://cdn.NetBSD.org#' -e 's#/NetBSD/[^/]*/#/NetBSD/aarch64/#')
    fi
    [ -n "$repo" ] || repo="https://cdn.NetBSD.org/pub/pkgsrc/packages/NetBSD/aarch64/$version/All"
    repo=${repo%/}
    case "$repo" in *aarch64*) ;; *) echo "sysroot: $repo is not an aarch64 repository" >&2; exit 1 ;; esac
    echo "sysroot: packages from $repo"
    rm -rf "$tmp/pkgs"
    mkdir -p "$tmp/pkgs"
    get "$tmp/pkgs/pkg_summary.gz" "$repo/pkg_summary.gz"
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
        get "$tmp/pkgs/$f" "$repo/$f"
    done < "$tmp/pkgs/list"
    mkdir -p "$dir/var/db/pkg"
    while IFS= read -r f; do
        PKG_PATH="$tmp/pkgs" pkg_add -m aarch64 -I -P "$dir" -K "$dir/var/db/pkg" "$tmp/pkgs/$f"
    done < "$tmp/pkgs/list"
    ;;
openbsd)
    v=$(echo "$version" | tr -d .)
    url="https://cdn.openbsd.org/pub/OpenBSD/$version/arm64"
    cd "$tmp"
    for f in SHA256.sig base$v.tgz comp$v.tgz xbase$v.tgz xshare$v.tgz; do
        curl -fsSLo "$f" "$url/$f"
    done
    signify -C -p "/etc/signify/openbsd-$v-base.pub" -x SHA256.sig \
        base$v.tgz comp$v.tgz xbase$v.tgz xshare$v.tgz
    tar -xzf base$v.tgz -C "$dir" ./usr/lib ./usr/libexec/ld.so
    tar -xzf comp$v.tgz -C "$dir" ./usr/include ./usr/lib
    tar -xzf xbase$v.tgz -C "$dir" ./usr/X11R6/lib ./usr/X11R6/include
    tar -xzf xshare$v.tgz -C "$dir" ./usr/X11R6/include ./usr/X11R6/lib
    # pkg_add will not install another architecture's packages, so they are
    # unpacked by hand, their dependencies read from each +CONTENTS.
    purl="https://cdn.openbsd.org/pub/OpenBSD/$version/packages/aarch64"
    rm -rf "$tmp/pkgs"
    mkdir -p "$tmp/pkgs" "$dir/usr/local"
    cd "$tmp/pkgs"
    curl -fsSLo SHA256.sig "$purl/SHA256.sig"
    queue=""
    for stem in dbus libxkbcommon wayland wayland-protocols; do
        name=$(sed -n "s/^SHA256 (\($stem-[0-9][^)]*\)\.tgz).*/\1/p" SHA256.sig | head -1)
        [ -n "$name" ] || { echo "sysroot: no $stem package for aarch64" >&2; exit 1; }
        queue="$queue $name"
    done
    seen=" "
    while [ -n "$(echo $queue)" ]; do
        set -- $queue
        p=$1
        shift
        queue="$*"
        case "$seen" in *" $p "*) continue ;; esac
        seen="$seen$p "
        curl -fsSLo "$p.tgz" "$purl/$p.tgz"
        signify -C -p "/etc/signify/openbsd-$v-pkg.pub" -x SHA256.sig "$p.tgz"
        rm -rf meta
        mkdir meta
        tar -xzf "$p.tgz" -C meta +CONTENTS
        for d in $(sed -n 's/^@depend [^:]*:[^:]*:\(.*\)$/\1/p' meta/+CONTENTS); do
            queue="$queue $d"
        done
        tar -xzf "$p.tgz" -C "$dir/usr/local"
        echo "sysroot: $p"
    done
    rm -f "$dir"/usr/local/+*
    cd /
    ;;
*)
    echo "sysroot.sh: unknown system $os" >&2
    exit 2
    ;;
esac

# A library link to an absolute path would reach the host's own libraries.
escaped=$(find "$dir" -type l -lname '/*' -name 'lib*.so*' 2>/dev/null || true)
if [ -n "$escaped" ]; then
    echo "sysroot: links reach outside it:" >&2
    echo "$escaped" >&2
    exit 1
fi
echo "sysroot: $dir ($(du -sh "$dir" | cut -f1))"
