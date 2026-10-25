#!/usr/bin/env bash
set -euo pipefail

# Prepare libgit2 for a real iOS device. No binaries are checked into Git.
# Clone/fetch work on the device through SecureTransport and Apple's Security framework.
cd "$(dirname "$0")/.."
ROOT="$PWD"
SRC="$ROOT/.build-deps/libgit2"
BUILD="$ROOT/.build-deps/libgit2-ios-build"
PREFIX="$ROOT/vendor/libgit2-ios"
VERSION="v1.9.7"

if [ -f "$PREFIX/lib/libgit2.a" ] && [ -f "$PREFIX/include/git2.h" ]; then
    echo "libgit2 already prepared"
    exit 0
fi

if [ ! -d "$SRC/.git" ]; then
    mkdir -p "$(dirname "$SRC")"
    git clone --branch "$VERSION" --depth 1 https://github.com/libgit2/libgit2.git "$SRC"
fi

cmake -S "$SRC" -B "$BUILD" -G Ninja \
    -DCMAKE_SYSTEM_NAME=iOS \
    -DCMAKE_OSX_SYSROOT=iphoneos \
    -DCMAKE_OSX_ARCHITECTURES=arm64 \
    -DCMAKE_OSX_DEPLOYMENT_TARGET=17.0 \
    -DCMAKE_TRY_COMPILE_TARGET_TYPE=STATIC_LIBRARY \
    -DCMAKE_BUILD_TYPE=Release \
    -DCMAKE_INSTALL_PREFIX="$PREFIX" \
    -DBUILD_SHARED_LIBS=OFF \
    -DBUILD_TESTS=OFF \
    -DBUILD_CLI=OFF \
    -DBUILD_EXAMPLES=OFF \
    -DUSE_SSH=OFF \
    -DUSE_HTTPS=SecureTransport \
    -DREGEX_BACKEND=builtin \
    -DUSE_BUNDLED_ZLIB=ON

cmake --build "$BUILD" --parallel 4
cmake --install "$BUILD"

test -s "$PREFIX/lib/libgit2.a"
test -s "$PREFIX/include/git2.h"
echo "Prepared static libgit2 for iOS ARM64."
