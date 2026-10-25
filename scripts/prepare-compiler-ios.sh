#!/usr/bin/env bash
set -euo pipefail

# Optional compiler runtime: prebuilt iOS LLVM 14 frameworks from a-Shell's
# open-source toolchain, plus a matching small wasi-libc sysroot.
# The frameworks are copied into the .app during IPA packaging, not linked into
# the shell core. They must be signed along with the .app when sideloading.
cd "$(dirname "$0")/.."
DEST="$PWD/vendor/compiler-ios"
mkdir -p "$DEST/frameworks" "$DEST/archives"

download_framework() {
    local name="$1" owner="$2" repo="$3" version="$4"
    local zip="$DEST/archives/${name}.xcframework.zip"
    local unpack="$DEST/archives/${name}-unpacked"
    local final="$DEST/frameworks/${name}.framework"
    if [ -d "$final" ]; then return 0; fi
    curl --fail --location --retry 3 --silent --show-error \
        "https://github.com/$owner/$repo/releases/download/$version/${name}.xcframework.zip" -o "$zip"
    rm -rf "$unpack"
    mkdir -p "$unpack"
    unzip -q "$zip" -d "$unpack"
    local source="$unpack/${name}.xcframework/ios-arm64/${name}.framework"
    if [ ! -d "$source" ]; then
        echo "Missing iOS arm64 framework: $source" >&2
        find "$unpack" -maxdepth 3 -type d | head -30
        exit 1
    fi
    cp -R "$source" "$final"
    rm -rf "$unpack" "$zip"
}

download_framework clang holzschu llvm-project 14.0.0
download_framework libLLVM holzschu llvm-project 14.0.0
download_framework lld holzschu llvm-project 14.0.0
download_framework ios_system holzschu ios_system v3.0.5

# WASI SDK 20 matches the older LLVM 14 feature level much better than 34.
# This is an asset of the WebAssembly wasi-sdk release, not an executable.
SYSROOT="$DEST/wasi-sysroot"
if [ ! -f "$SYSROOT/include/stdio.h" ]; then
    curl --fail --location --retry 3 --silent --show-error \
        "https://github.com/WebAssembly/wasi-sdk/releases/download/wasi-sdk-20/wasi-sysroot-20.0.tar.gz" \
        -o "$DEST/archives/wasi-sysroot.tar.gz"
    mkdir -p "$SYSROOT"
    tar -xzf "$DEST/archives/wasi-sysroot.tar.gz" -C "$SYSROOT" --strip-components=1
    rm -f "$DEST/archives/wasi-sysroot.tar.gz"
    if [ ! -f "$SYSROOT/include/stdio.h" ]; then
        echo "WASI sysroot did not extract include/stdio.h" >&2
        find "$SYSROOT" -maxdepth 3 -name stdio.h -print
        exit 1
    fi
fi
# Clang's standard builtins (stdarg.h, stddef.h, etc.) are not part of
# wasi-libc's sysroot. Bundle host compiler-resource headers for C programs.
# This header compatibility needs physical-device validation with Clang 14.
BUILTINS="$DEST/clang-include"
if [ ! -f "$BUILTINS/stdarg.h" ]; then
    HOST_RESOURCES="$(xcrun clang -print-resource-dir)"
    test -f "$HOST_RESOURCES/include/stdarg.h"
    mkdir -p "$BUILTINS"
    cp -R "$HOST_RESOURCES/include/." "$BUILTINS/"
fi

echo "iOS Clang/LLD and wasm32-wasi sysroot ready."
