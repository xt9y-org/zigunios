#!/usr/bin/env bash
set -euo pipefail

# No JIT: wasm3 interprets imported .wasm code without executing unsigned machine code.
cd "$(dirname "$0")/.."
ROOT="$PWD"
SRC="$ROOT/.build-deps/wasm3"
BUILD="$ROOT/.build-deps/wasm3-ios-build"
WRAPPER="$ROOT/.build-deps/wasm3-wrapper"
PREFIX="$ROOT/vendor/wasm3-ios"

if [ -s "$PREFIX/lib/libm3.a" ] && [ -s "$PREFIX/include/wasm3.h" ]; then
    echo "wasm3 already prepared"
    exit 0
fi

if [ ! -d "$SRC/.git" ]; then
    mkdir -p "$(dirname "$SRC")"
    git clone --branch v0.9.0 --depth 1 https://github.com/wasm3/wasm3.git "$SRC"
fi

# Build the static library only. Upstream top-level CMake installs a
# macOS-style executable bundle, which cannot be configured for iOS.
mkdir -p "$WRAPPER"
cat > "$WRAPPER/CMakeLists.txt" <<EOF
cmake_minimum_required(VERSION 3.20)
project(ZiguniosWasm3 LANGUAGES C)
set(BUILD_WASI simple)
add_subdirectory("$SRC/source" source)
EOF

cmake -S "$WRAPPER" -B "$BUILD" -G Ninja \
    -DCMAKE_SYSTEM_NAME=iOS \
    -DCMAKE_OSX_SYSROOT=iphoneos \
    -DCMAKE_OSX_ARCHITECTURES=arm64 \
    -DCMAKE_OSX_DEPLOYMENT_TARGET=17.0 \
    -DCMAKE_TRY_COMPILE_TARGET_TYPE=STATIC_LIBRARY \
    -DCMAKE_BUILD_TYPE=Release \
    -DBUILD_NATIVE=OFF \
    -DBUILD_WASI=simple \
    -DBUILD_TESTS=OFF

cmake --build "$BUILD" --target m3 --parallel 4

mkdir -p "$PREFIX/lib" "$PREFIX/include"
cp "$BUILD/source/libm3.a" "$PREFIX/lib/libm3.a"
cp "$SRC"/source/*.h "$PREFIX/include/"
test -s "$PREFIX/lib/libm3.a"
echo "Prepared interpreter-only wasm3 for iOS ARM64."
