#!/bin/sh
# Builds libreaktor and packs it for this system: .deb, .rpm, or a FreeBSD,
# OpenBSD or NetBSD package; a .tar.gz anywhere else.
set -eu

usage() {
    cat <<'EOF'
usage: libreaktor-pkg.sh [-i] [-p PREFIX] [-o DIR] [-f FORMAT] [-m MAINTAINER]
  -i  install the package once it is built (through sudo or doas)
  -p  where the files go (/opt/reaktor on Linux, /usr/local/reaktor on the BSDs)
  -o  where the package is written (dist)
  -f  deb, rpm, freebsd, openbsd, netbsd or tar (picked from the system)
  -m  the maintainer the package names
EOF
}

root=$(CDPATH= cd -- "$(dirname -- "$0")/.." && pwd)
os=$(uname -s)
install=0
prefix=""
out="$root/dist"
format=""
maintainer="Reaktor <reaktor@invalid>"

while getopts ip:o:f:m:h opt; do
    case $opt in
        i) install=1 ;;
        p) prefix=$OPTARG ;;
        o) out=$OPTARG ;;
        f) format=$OPTARG ;;
        m) maintainer=$OPTARG ;;
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
www="https://github.com/Alpaq92/Reaktor"
work="$root/build-pkg"
[ -f "$root/external/SDL/CMakeLists.txt" ] || {
    echo "libreaktor-pkg.sh: submodules missing - run: git submodule update --init --recursive" >&2
    exit 1; }
stage="$work/stage"
mkdir -p "$out"
out=$(CDPATH= cd -- "$out" && pwd)

if command -v ninja >/dev/null 2>&1; then gen="-G Ninja"; else gen=""; fi
ncpu=$(nproc 2>/dev/null || sysctl -n hw.ncpu 2>/dev/null || echo 2)
# shellcheck disable=SC2086
cmake -S "$root" -B "$work/build" $gen -DCMAKE_BUILD_TYPE=Release \
      -DCMAKE_INSTALL_PREFIX="$prefix" -DREAKTOR_SAMPLES=OFF -DREAKTOR_TESTS=OFF
cmake --build "$work/build" --parallel "$ncpu" --target libreaktor
rm -rf "$stage"
DESTDIR="$stage" cmake --install "$work/build" --component libreaktor

listing() {
    (cd "$stage$prefix" &&
     find . -type d ! -name . | sed 's|^\./||; s|$|/|' | sort
     find . ! -type d | sed 's|^\./||' | sort)
}

as_root() {
    if [ "$(id -u)" -eq 0 ]; then "$@"
    elif command -v sudo >/dev/null 2>&1; then sudo "$@"
    else doas "$@"; fi
}

case $format in
deb)
    arch=$(dpkg --print-architecture)
    pkg="$out/libreaktor-dev_${version}_$arch.deb"
    mkdir -p "$stage/DEBIAN"
    cat >"$stage/DEBIAN/control" <<EOF
Package: libreaktor-dev
Version: $version
Architecture: $arch
Maintainer: $maintainer
Section: libdevel
Priority: optional
Homepage: $www
Description: $summary
EOF
    dpkg-deb --root-owner-group --build "$stage" "$pkg"
    rm -rf "$stage/DEBIAN"
    [ $install -eq 0 ] || as_root dpkg -i "$pkg"
    ;;
rpm)
    top="$work/rpm"
    rm -rf "$top"
    mkdir -p "$top/SPECS"
    cat >"$top/SPECS/libreaktor.spec" <<EOF
Name: libreaktor-devel
Version: $version
Release: 1
Summary: $summary
License: MIT
URL: $www
AutoReqProv: no

%description
$summary.

%install
cp -a "$stage/." %{buildroot}/

%files
$prefix
EOF
    rpmbuild -bb --define "_topdir $top" --define "debug_package %{nil}" \
             --define "__os_install_post %{nil}" "$top/SPECS/libreaktor.spec"
    pkg=$(find "$top/RPMS" -name '*.rpm' | head -n 1)
    cp "$pkg" "$out/"
    pkg="$out/$(basename "$pkg")"
    [ $install -eq 0 ] || as_root rpm -U --replacepkgs "$pkg"
    ;;
freebsd)
    cat >"$work/manifest" <<EOF
name: libreaktor
version: "$version"
origin: devel/libreaktor
comment: "$summary"
desc: "$summary"
maintainer: "$maintainer"
www: "$www"
prefix: "$prefix"
EOF
    listing | grep -v '/$' >"$work/plist"
    pkg create -M "$work/manifest" -p "$work/plist" -r "$stage" -o "$out"
    pkg="$out/libreaktor-$version.pkg"
    [ $install -eq 0 ] || as_root pkg add "$pkg"
    ;;
openbsd)
    listing >"$work/plist"
    pkg="$out/libreaktor-$version.tgz"
    pkg_create -A "$(arch -s)" -B "$stage" -p "$prefix" -f "$work/plist" \
               -D COMMENT="$summary" -D FULLPKGPATH=devel/libreaktor \
               -D HOMEPAGE="$www" -D MAINTAINER="$maintainer" \
               -d "-$summary." "$pkg"
    [ $install -eq 0 ] || as_root pkg_add -D unsigned "$pkg"
    ;;
netbsd)
    listing | grep -v '/$' >"$work/plist"
    cat >"$work/build-info" <<EOF
MACHINE_ARCH=$(uname -p)
OPSYS=NetBSD
OS_VERSION=$(uname -r)
PKGTOOLS_VERSION=$(pkg_info -V)
EOF
    pkg="$out/libreaktor-$version.tgz"
    pkg_create -B "$work/build-info" -c "-$summary" -d "-$summary." \
               -f "$work/plist" -I "$prefix" -p "$stage$prefix" "$pkg"
    [ $install -eq 0 ] || as_root pkg_add -U "$pkg"
    ;;
tar)
    pkg="$out/libreaktor-$version-$(uname -s | tr 'A-Z' 'a-z')-$(uname -m).tar.gz"
    tar -C "$stage" -czf "$pkg" .
    [ $install -eq 0 ] || as_root tar -C / -xzf "$pkg"
    ;;
*)
    echo "libreaktor-pkg.sh: unknown format: $format" >&2
    exit 2
    ;;
esac

echo "packed: $pkg"
echo "use it: pkg-config --cflags --libs reaktor with PKG_CONFIG_PATH=$prefix/lib/pkgconfig,"
echo "        or find_package(reaktor) with CMAKE_PREFIX_PATH=$prefix"
