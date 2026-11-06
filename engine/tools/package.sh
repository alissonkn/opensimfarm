#!/bin/bash
# OpenSimFarm - an open re-implementation of SimFarm. Copyright (c) 2026 OpenSimFarm developers.
# Licensed under the GNU General Public License version 3 (see LICENSE.txt).
#
# package.sh [windows] [rpm] [deb]  (none = all three): builds the release packages into engine/dist/
#   windows  OpenSimFarm-VERSION-windows-x64-setup.exe (NSIS installer) and OpenSimFarm-VERSION-windows-x64.zip
#   rpm      opensimfarm-VERSION-1.x86_64.rpm
#   deb      opensimfarm_VERSION-1_amd64.deb
# None of them holds anything of the original game: the game finds the player's own SimFarm folder (installer page,
# or its own search and folder dialog at the first start).
#
# Needs (full instructions: README.md "Building the installers"):
#   all      cmake, python3 with Pillow, Zig from pip (python3 -m pip install --user ziglang)
#   Windows  SDL2 MinGW files ($SDL2_MINGW: the x86_64-w64-mingw32 folder of the official SDL2-devel-2.x-mingw.tar.gz),
#            NSIS 3 (makensis on the PATH, or $NSIS_ROOT holding usr/bin/makensis and usr/share/nsis), 7z for the zip
#   Linux    SDL2 headers (the system's SDL2 development package, or $SDL2_ROOT), rpmbuild for the rpm, ar for the deb
# Build folders: build-win/ and build-pkg/ ($BUILD_WIN, $BUILD_PKG); configured with packaging/toolchains/ when new.
set -euo pipefail
ENGINE=$(cd "$(dirname "$0")/.." && pwd)
ROOT=$(cd "$ENGINE/.." && pwd)
VERSION=$(sed -n 's/^project(OpenSimFarm VERSION \([0-9.]*\).*/\1/p' "$ENGINE/CMakeLists.txt")
DIST=$ENGINE/dist
WORK=$DIST/.work
SDL2_MINGW=${SDL2_MINGW:-$HOME/tools/sdl2-mingw/SDL2-2.30.9/x86_64-w64-mingw32}
NSIS_ROOT=${NSIS_ROOT:-$ROOT/work/tools/nsis/root}
SDL2_ROOT=${SDL2_ROOT:-$HOME/tools/sdl2/usr}
BUILD_WIN=${BUILD_WIN:-$ENGINE/build-win}
BUILD_PKG=${BUILD_PKG:-$ENGINE/build-pkg}
TOOLCHAINS=$ENGINE/packaging/toolchains
MAINTAINER=${MAINTAINER:-"$(git -C "$ROOT" config user.name) <$(git -C "$ROOT" config user.email)>"}
HOMEPAGE=https://github.com/alissonkn/opensimfarm
SUMMARY="Open re-implementation of SimFarm (plays with your own copy of the game)"

want=("$@")
[ ${#want[@]} -eq 0 ] && want=(windows rpm deb)
has() { local w; for w in "${want[@]}"; do [ "$w" = "$1" ] && return 0; done; return 1; }

need() { command -v "$1" >/dev/null 2>&1 || { echo "package.sh: $1 is missing ($2)" >&2; exit 1; }; }
need cmake "install CMake"
python3 -c "import PIL" 2>/dev/null || { echo "package.sh: Pillow is missing (python3 -m pip install --user pillow)" >&2; exit 1; }
python3 -m ziglang version >/dev/null 2>&1 || { echo "package.sh: Zig is missing (python3 -m pip install --user ziglang)" >&2; exit 1; }
if has windows; then
    [ -f "$SDL2_MINGW/bin/SDL2.dll" ] || { echo "package.sh: no SDL2 MinGW files in $SDL2_MINGW (set SDL2_MINGW)" >&2; exit 1; }
    if [ -x "$NSIS_ROOT/usr/bin/makensis" ]; then
        MAKENSIS=("env" "NSISDIR=$NSIS_ROOT/usr/share/nsis" "$NSIS_ROOT/usr/bin/makensis")
    elif command -v makensis >/dev/null 2>&1; then
        MAKENSIS=(makensis)
    else
        echo "package.sh: NSIS is missing (install nsis, or set NSIS_ROOT)" >&2; exit 1
    fi
    need 7z "install p7zip or 7zip"
fi
if has rpm; then need rpmbuild "install rpm-build (Fedora) or rpm (Debian, Ubuntu)"; fi
if has deb; then need ar "install binutils"; fi
SDL2_ARG=()
if [ -d "$SDL2_ROOT" ]; then SDL2_ARG=(-DSDL2_ROOT="$SDL2_ROOT"); fi

mkdir -p "$DIST" "$WORK"
rm -rf "$WORK/icon"
python3 "$ENGINE/tools/make_icon.py" sizes "$WORK/icon"

# ---------------------------------------------------------------- Windows
if has windows; then
    echo "== Windows $VERSION"
    if [ ! -f "$BUILD_WIN/CMakeCache.txt" ]; then
        cmake -S "$ENGINE" -B "$BUILD_WIN" -DCMAKE_BUILD_TYPE=Release -DCMAKE_TOOLCHAIN_FILE="$TOOLCHAINS/windows.cmake" \
            -DSDL2_ROOT="$SDL2_MINGW" >/dev/null
    fi
    cmake --build "$BUILD_WIN" -j8 >/dev/null
    S=$WORK/win
    rm -rf "$S" && mkdir -p "$S"
    cp "$BUILD_WIN/opensimfarm.exe" "$S/"
    cp "$SDL2_MINGW/bin/SDL2.dll" "$S/"
    cp "$ROOT/README.md" "$S/README.md"
    cp "$ENGINE/LICENSE.txt" "$S/LICENSE.txt"
    cp "$ENGINE/packaging/windows/opensimfarm.ico" "$S/"
    OUT=$DIST/OpenSimFarm-$VERSION-windows-x64-setup.exe
    "${MAKENSIS[@]}" -V2 -DVERSION="$VERSION" -DSTAGE="$S" -DOUTFILE="$OUT" \
        "$ENGINE/packaging/windows/opensimfarm.nsi"
    rm -f "$DIST/OpenSimFarm-$VERSION-windows-x64.zip"
    (cd "$S" && 7z a -tzip -bso0 -bsp0 "$DIST/OpenSimFarm-$VERSION-windows-x64.zip" opensimfarm.exe SDL2.dll README.md LICENSE.txt)
    ls -la "$OUT" "$DIST/OpenSimFarm-$VERSION-windows-x64.zip"
fi

# ---------------------------------------------------------------- Linux file tree (shared by rpm and deb)
if has rpm || has deb; then
    echo "== Linux $VERSION"
    if [ ! -f "$BUILD_PKG/CMakeCache.txt" ]; then
        cmake -S "$ENGINE" -B "$BUILD_PKG" -DCMAKE_BUILD_TYPE=Release -DCMAKE_TOOLCHAIN_FILE="$TOOLCHAINS/linux-glibc235.cmake" \
            "${SDL2_ARG[@]}" -DCMAKE_SKIP_RPATH=ON >/dev/null
    fi
    cmake --build "$BUILD_PKG" -j8 >/dev/null
    T=$WORK/linux
    rm -rf "$T" && mkdir -p "$T/usr/bin" "$T/usr/share/applications" "$T/usr/share/doc/opensimfarm"
    strip -o "$T/usr/bin/opensimfarm" "$BUILD_PKG/opensimfarm"
    chmod 755 "$T/usr/bin/opensimfarm"
    install -m 644 "$ENGINE/packaging/linux/opensimfarm.desktop" "$T/usr/share/applications/"
    for n in 16 24 32 48 64 128 256; do
        install -D -m 644 "$WORK/icon/opensimfarm-$n.png" "$T/usr/share/icons/hicolor/${n}x$n/apps/opensimfarm.png"
    done
    install -m 644 "$ROOT/README.md" "$T/usr/share/doc/opensimfarm/README.md"
fi

# ---------------------------------------------------------------- rpm
if has rpm; then
    R=$WORK/rpm
    rm -rf "$R" && mkdir -p "$R/SPECS" "$R/BUILD" "$R/RPMS" "$R/SOURCES" "$R/SRPMS"
    cat > "$R/SPECS/opensimfarm.spec" <<EOF
%global debug_package %{nil}
%global __strip /bin/true
Name:           opensimfarm
Version:        $VERSION
Release:        1
Summary:        $SUMMARY
License:        GPL-3.0-or-later
URL:            $HOMEPAGE
BuildArch:      x86_64
Recommends:     (zenity or kdialog)

%description
OpenSimFarm is an open re-implementation of the farm simulation SimFarm. It contains
none of the original game's files: it plays with the files of your own copy of
SimFarm, which it finds on the first start (or you choose the folder once).

%install
cp -a "$T/." %{buildroot}/
install -D -m 644 "$ENGINE/LICENSE.txt" %{buildroot}%{_datadir}/licenses/opensimfarm/LICENSE.txt

%files
%{_bindir}/opensimfarm
%{_datadir}/applications/opensimfarm.desktop
%{_datadir}/icons/hicolor/*/apps/opensimfarm.png
%doc %{_datadir}/doc/opensimfarm/README.md
%license %{_datadir}/licenses/opensimfarm/LICENSE.txt
EOF
    rpmbuild -bb --quiet --define "_topdir $R" "$R/SPECS/opensimfarm.spec"
    cp "$R/RPMS/x86_64/opensimfarm-$VERSION-1.x86_64.rpm" "$DIST/"
    ls -la "$DIST/opensimfarm-$VERSION-1.x86_64.rpm"
fi

# ---------------------------------------------------------------- deb (built with ar and tar: no dpkg needed)
if has deb; then
    D=$WORK/deb
    rm -rf "$D" && mkdir -p "$D/data" "$D/control"
    cp -a "$T/." "$D/data/"
    {
        echo "Format: https://www.debian.org/doc/packaging-manuals/copyright-format/1.0/"
        echo "Upstream-Name: OpenSimFarm"
        echo "Source: $HOMEPAGE"
        echo
        echo "Files: *"
        echo "Copyright: 2026 OpenSimFarm developers"
        echo "License: GPL-3.0-or-later"
        echo " On Debian systems the full text is in /usr/share/common-licenses/GPL-3."
    } > "$D/data/usr/share/doc/opensimfarm/copyright"
    find "$D/data" -type d -exec chmod 755 {} +
    SIZE=$(du -sk "$D/data" | cut -f1)
    cat > "$D/control/control" <<EOF
Package: opensimfarm
Version: $VERSION-1
Architecture: amd64
Maintainer: $MAINTAINER
Installed-Size: $SIZE
Depends: libc6 (>= 2.34), libsdl2-2.0-0 (>= 2.0.18)
Recommends: zenity | kdialog
Section: games
Priority: optional
Homepage: $HOMEPAGE
Description: $SUMMARY
 OpenSimFarm is an open re-implementation of the farm simulation SimFarm. It
 contains none of the original game's files: it plays with the files of your own
 copy of SimFarm, which it finds on the first start (or you choose the folder once).
EOF
    (cd "$D/data" && find . -type f ! -path ./DEBIAN -printf '%P\n' | sort | xargs md5sum) > "$D/control/md5sums"
    echo "2.0" > "$D/debian-binary"
    tar --owner=0 --group=0 --numeric-owner -C "$D/control" -czf "$D/control.tar.gz" .
    tar --owner=0 --group=0 --numeric-owner -C "$D/data" -czf "$D/data.tar.gz" .
    OUT=$DIST/opensimfarm_$VERSION-1_amd64.deb
    rm -f "$OUT"
    (cd "$D" && ar rc "$OUT" debian-binary control.tar.gz data.tar.gz)
    ls -la "$OUT"
fi
