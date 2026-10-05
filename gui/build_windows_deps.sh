#!/bin/bash

# Cross-compiles the one Windows dependency we cannot vendor as source: a static
# libcurl, built with the Schannel TLS backend (the native Windows one, so no
# certificate bundle needs to be shipped) and everything we do not use disabled.
# The result is installed into gui/.win-deps (git-ignored).
#
# Requires the llvm-mingw toolchain; see build-windows.sh for how it is located.

set -e
cd "$(dirname "$0")"

TOOLCHAIN="${LLVM_MINGW:-$HOME/babyt/toolchains/llvm-mingw}"
if [ ! -x "$TOOLCHAIN/bin/x86_64-w64-mingw32-gcc" ]; then
    echo "llvm-mingw toolchain not found at $TOOLCHAIN" >&2
    echo "Download a release from https://github.com/mstorsjo/llvm-mingw/releases" >&2
    echo "(the ucrt-ubuntu-*-x86_64 tarball), extract it there, or set LLVM_MINGW." >&2
    exit 1
fi
export PATH="$TOOLCHAIN/bin:$PATH"

CURL_VERSION=8.9.1
DEPS="$PWD/.win-deps"
BUILDDIR="$DEPS/curl-build"

mkdir -p "$BUILDDIR"
cd "$BUILDDIR"

if [ ! -d "curl-$CURL_VERSION" ]; then
    curl -sLO "https://curl.se/download/curl-$CURL_VERSION.tar.xz"
    tar xf "curl-$CURL_VERSION.tar.xz"
fi

cd "curl-$CURL_VERSION"

./configure --host=x86_64-w64-mingw32 --prefix="$DEPS" \
    --with-schannel --enable-static --disable-shared \
    --enable-http --disable-ftp --disable-file --disable-ldap --disable-ldaps \
    --disable-rtsp --disable-dict --disable-telnet --disable-tftp --disable-pop3 \
    --disable-imap --disable-smb --disable-smtp --disable-gopher --disable-mqtt \
    --without-libpsl --without-nghttp2 --without-zlib --without-brotli \
    --without-zstd --without-libidn2 --without-libssh2 \
    --disable-manual --disable-docs --disable-ntlm --disable-tls-srp \
    --quiet

make -j"$(nproc)" -C lib
make -C lib install
make -C include install

echo "Static libcurl for Windows installed in $DEPS (lib/libcurl.a)"
