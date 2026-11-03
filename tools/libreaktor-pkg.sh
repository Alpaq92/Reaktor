#!/bin/sh
# Builds libreaktor and packs it for this system: .deb, .rpm, or a FreeBSD,
# OpenBSD or NetBSD package; a .tar.gz anywhere else.
set -eu

usage() {
    cat <<'EOF'
usage: libreaktor-pkg.sh [-i] [-s] [-b BUILD] [-p PREFIX] [-o DIR] [-f FORMAT] [-m MAINTAINER] [-a ARCH]
  -i  install the package once it is built (through sudo or doas)
  -s  build libreaktor.so too, and split the package in three (Linux and the BSDs)
  -b  pack from this configured build instead of building one
  -p  where the files go (/opt/reaktor on Linux, /usr/local/reaktor on the BSDs)
  -o  where the package is written (dist)
  -f  deb, rpm, freebsd, openbsd, netbsd or tar (picked from the system)
  -m  the maintainer the package names
  -a  the .deb's architecture, for a cross build (dpkg's own otherwise)
EOF
}

root=$(CDPATH= cd -- "$(dirname -- "$0")/.." && pwd)
os=$(uname -s)
install=0
prefix=""
out="$root/dist"
format=""
maintainer="Reaktor <reaktor@invalid>"
build=""
debarch=""
shared=OFF

while getopts isb:p:o:f:m:a:h opt; do
    case $opt in
        i) install=1 ;;
        s) shared=ON ;;
        b) build=$OPTARG ;;
        p) prefix=$OPTARG ;;
        o) out=$OPTARG ;;
        f) format=$OPTARG ;;
        m) maintainer=$OPTARG ;;
        a) debarch=$OPTARG ;;
        h) usage; exit 0 ;;
        *) usage >&2; exit 2 ;;
    esac
done

if [ -z "$format" ]; then
    case $os in
        FreeBSD) format=freebsd ;;
        OpenBSD) format=openbsd ;;
        NetBSD)  format=netbsd ;;
        *) if command -v dpkg-deb >/dev/null 2>&1; then format=deb
           elif command -v rpmbuild >/dev/null 2>&1; then format=rpm
           else format=tar; fi ;;
    esac
fi
if [ -z "$prefix" ]; then
    case $os in
        *BSD) prefix=/usr/local/reaktor ;;
        *)    prefix=/opt/reaktor ;;
    esac
fi
case $prefix in /*) ;; *) echo "libreaktor-pkg.sh: -p needs an absolute path" >&2; exit 2 ;; esac

version=$(sed -n 's/^project(reaktor VERSION \([0-9.]*\).*/\1/p' "$root/CMakeLists.txt")
summary="Reaktor, a C99 GUI library on Nuklear styled by CSS (static library and headers)"
comment="C99 GUI library on Nuklear, styled by CSS"
www="https://github.com/Alpaq92/Reaktor"
work="$root/build-pkg"
[ -f "$root/external/SDL/CMakeLists.txt" ] || {
    echo "libreaktor-pkg.sh: submodules missing - run: git submodule update --init --recursive" >&2
    exit 1; }
stage="$work/stage"
mkdir -p "$out"
out=$(CDPATH= cd -- "$out" && pwd)

ncpu=$(nproc 2>/dev/null || sysctl -n hw.ncpu 2>/dev/null || echo 2)
if [ -z "$build" ]; then
    build="$work/build"
    if command -v ninja >/dev/null 2>&1; then gen="-G Ninja"; else gen=""; fi
    # shellcheck disable=SC2086
    cmake -S "$root" -B "$build" $gen -DCMAKE_BUILD_TYPE=Release \
          -DREAKTOR_SAMPLES=OFF -DREAKTOR_TESTS=OFF -DREAKTOR_SHARED=$shared
fi
cmake --build "$build" --parallel "$ncpu" --target libreaktor
rm -rf "$stage"
# --strip reaches the shared libraries only; the archives are installed as files.
DESTDIR="$stage" cmake --install "$build" --component libreaktor --prefix "$prefix" --strip

listing() {
    (cd "$1$prefix" &&
     find . -type d ! -name . | sed 's|^\./||; s|$|/|' | sort
     find . ! -type d | sed 's|^\./||' | sort)
}

as_root() {
    if [ "$(id -u)" -eq 0 ]; then "$@"
    elif command -v sudo >/dev/null 2>&1; then sudo "$@"
    else doas "$@"; fi
}

# A static build packs one package, libreaktor-static. A REAKTOR_SHARED build
# packs three, the development one under the name each format expects. The
# two development packages install the same headers, so each conflicts with
# the other: a Conflicts line, a manifest's conflicts, @conflict in OpenBSD's
# packing list, -C for NetBSD's pkg_create.
static=libreaktor-static
conflicts=""
case $format in
    rpm) dev=libreaktor-devel ;;
    deb) dev=libreaktor-dev ;;
    *)   dev=libreaktor ;;
esac

# pack NAME ROOT SUMMARY [DEPENDENCY...]: one package of the files under ROOT.
pack() {
    name=$1 root=$2 what=$3
    shift 3
    case $format in
    deb)
        arch=${debarch:-$(dpkg --print-architecture)}
        pkg="$out/${name}_${version}_$arch.deb"
        mkdir -p "$root/DEBIAN"
        deps=""
        for d in "$@"; do deps="${deps:+$deps, }$d (= $version)"; done
        {
            echo "Package: $name"
            echo "Version: $version"
            echo "Architecture: $arch"
            echo "Maintainer: $maintainer"
            case $name in "$dev"|"$static") echo "Section: libdevel" ;; *) echo "Section: libs" ;; esac
            echo "Priority: optional"
            echo "Homepage: $www"
            [ -z "$deps" ] || echo "Depends: $deps"
            [ -z "$conflicts" ] || echo "Conflicts: $conflicts"
            echo "Description: $what"
        } >"$root/DEBIAN/control"
        dpkg-deb --root-owner-group --build "$root" "$pkg"
        rm -rf "$root/DEBIAN"
        ;;
    rpm)
        top="$work/rpm-$name"
        rm -rf "$top"
        mkdir -p "$top/SPECS"
        {
            echo "Name: $name"
            echo "Version: $version"
            echo "Release: 1"
            echo "Summary: $what"
            echo "License: MIT"
            echo "URL: $www"
            echo "AutoReqProv: no"
            for d in "$@"; do echo "Requires: $d = $version-1"; done
            [ -z "$conflicts" ] || echo "Conflicts: $conflicts"
            echo
            echo "%description"
            echo "$what."
            echo
            echo "%install"
            echo "cp -a \"$root/.\" %{buildroot}/"
            echo
            echo "%files"
            # Split packages share directories, so each lists only its files.
            if [ "$split" = 0 ]; then echo "$prefix"
            else listing "$root" | grep -v '/$' | sed "s|^|$prefix/|"; fi
        } >"$top/SPECS/$name.spec"
        rpmbuild -bb --define "_topdir $top" --define "debug_package %{nil}" \
                 --define "__os_install_post %{nil}" "$top/SPECS/$name.spec"
        pkg=$(find "$top/RPMS" -name '*.rpm' | head -n 1)
        cp "$pkg" "$out/"
        pkg="$out/$(basename "$pkg")"
        ;;
    freebsd)
        {
            echo "name: $name"
            echo "version: \"$version\""
            echo "origin: devel/$name"
            echo "comment: \"$comment\""
            echo "desc: \"$what\""
            echo "maintainer: \"$maintainer\""
            echo "www: \"$www\""
            echo "prefix: \"$prefix\""
            [ -z "$conflicts" ] || echo "conflicts: [ \"$conflicts-*\" ]"
            if [ $# -gt 0 ]; then
                echo "deps: {"
                for d in "$@"; do echo "  $d: { origin: \"devel/$d\", version: \"$version\" }"; done
                echo "}"
            fi
        } >"$work/manifest"
        listing "$root" | grep -v '/$' >"$work/plist"
        pkg create -M "$work/manifest" -p "$work/plist" -r "$root" -o "$out"
        pkg="$out/$name-$version.pkg"
        ;;
    openbsd)
        {
            [ -z "$conflicts" ] || echo "@conflict $conflicts-*"
            listing "$root"
        } >"$work/plist"
        pkg="$out/$name-$version.tgz"
        deps=""
        for d in "$@"; do deps="$deps -P devel/$d:$d-$version:$d-$version"; done
        # shellcheck disable=SC2086
        pkg_create -A "$(arch -s)" -B "$root" -p "$prefix" -f "$work/plist" \
                   -D COMMENT="$comment" -D FULLPKGPATH="devel/$name" \
                   -D HOMEPAGE="$www" -D MAINTAINER="$maintainer" \
                   -d "-$what." $deps "$pkg"
        ;;
    netbsd)
        listing "$root" | grep -v '/$' >"$work/plist"
        {
            echo "MACHINE_ARCH=$(uname -p)"
            echo "OPSYS=NetBSD"
            echo "OS_VERSION=$(uname -r)"
            echo "PKGTOOLS_VERSION=$(pkg_info -V)"
        } >"$work/build-info"
        pkg="$out/$name-$version.tgz"
        deps=""
        for d in "$@"; do deps="$deps -P $d>=$version"; done
        [ -z "$conflicts" ] || deps="$deps -C $conflicts-[0-9]*"
        # Unquoted for the splitting, with globbing off for the -C pattern.
        # shellcheck disable=SC2086
        set -f
        pkg_create -B "$work/build-info" -c "-$comment" -d "-$what." \
                   -f "$work/plist" -I "$prefix" -p "$root$prefix" $deps "$pkg"
        set +f
        ;;
    tar)
        pkg="$out/$name-$version-$(uname -s | tr 'A-Z' 'a-z')-$(uname -m).tar.gz"
        tar -C "$root" -czf "$pkg" .
        ;;
    *)
        echo "libreaktor-pkg.sh: unknown format: $format" >&2
        exit 2
        ;;
    esac
    echo "packed: $pkg"
    packed="$packed $pkg"
}

# A REAKTOR_SHARED build splits in three, so an app can install the libraries
# alone: libreaktor-sdl3-0 (our SDL), libreaktor0 (libreaktor.so, which needs
# it) and the development package with everything else, which needs both.
split=0
[ -f "$stage$prefix/lib/libreaktor.so" ] && split=1
packed=""
if [ "$split" = 0 ]; then
    conflicts=$dev
    pack "$static" "$stage" "$summary"
else
    sdl="$work/split/sdl" lib="$work/split/lib"
    rm -rf "$work/split"
    mkdir -p "$sdl$prefix/lib" "$sdl$prefix/share/reaktor/licenses" "$lib$prefix/lib" \
             "$lib$prefix/share/reaktor"
    mv "$stage$prefix"/lib/libreaktor-sdl3.so.* "$sdl$prefix/lib/"
    mv "$stage$prefix/share/reaktor/licenses/SDL.txt" "$sdl$prefix/share/reaktor/licenses/"
    mv "$stage$prefix"/lib/libreaktor.so.* "$lib$prefix/lib/"
    mv "$stage$prefix/share/reaktor/licenses" "$stage$prefix/share/reaktor/NOTICE.md" \
       "$lib$prefix/share/reaktor/"
    pack libreaktor-sdl3-0 "$sdl" \
         "SDL 3 as Reaktor builds it, for apps linked against libreaktor.so"
    pack libreaktor0 "$lib" \
         "Reaktor, a C99 GUI library on Nuklear styled by CSS (shared library)" libreaktor-sdl3-0
    conflicts=$static
    pack "$dev" "$stage" \
         "Reaktor, a C99 GUI library on Nuklear styled by CSS (headers, static and shared libraries)" \
         libreaktor0 libreaktor-sdl3-0
fi

if [ $install -eq 1 ]; then
    # shellcheck disable=SC2086
    case $format in
        deb)     as_root dpkg -i $packed ;;
        rpm)     as_root rpm -U --replacepkgs $packed ;;
        freebsd) for p in $packed; do as_root pkg add "$p"; done ;;
        openbsd) for p in $packed; do as_root pkg_add -D unsigned "$p"; done ;;
        netbsd)  for p in $packed; do as_root pkg_add -U "$p"; done ;;
        tar)     for p in $packed; do as_root tar -C / -xzf "$p"; done ;;
    esac
fi

echo "use it: pkg-config --cflags --libs reaktor with PKG_CONFIG_PATH=$prefix/lib/pkgconfig,"
echo "        or find_package(reaktor) with CMAKE_PREFIX_PATH=$prefix"
