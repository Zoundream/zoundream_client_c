#!/bin/bash

# Fetches the X11 development headers needed to compile the vendored GLFW, without
# requiring root: the Ubuntu/Debian -dev packages are downloaded with apt-get download
# and extracted into gui/.deps-sysroot (git-ignored).
#
# This is NOT needed if the headers are already installed system-wide
# (e.g. after `sudo apt install xorg-dev`): build.sh checks for those first.
#
# Only headers are taken from these packages; at runtime GLFW loads the X11
# libraries that are already part of every Linux desktop.

set -e
cd "$(dirname "$0")"

if [ -f /usr/include/X11/Xlib.h ]; then
    echo "System X11 headers found in /usr/include - nothing to do."
    exit 0
fi

PACKAGES="libx11-dev x11proto-dev libxrandr-dev libxrender-dev libxinerama-dev libxcursor-dev libxfixes-dev libxi-dev libxext-dev"
SYSROOT="$PWD/.deps-sysroot"

mkdir -p "$SYSROOT/downloads"
cd "$SYSROOT/downloads"
apt-get download $PACKAGES

for deb in *.deb; do
    dpkg-deb -x "$deb" "$SYSROOT"
done

echo "X11 headers extracted to $SYSROOT/usr/include"
