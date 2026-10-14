#!/bin/bash
# OpenSpoof build + package script.
# Needs: Android NDK (ANDROID_NDK_HOME), zip, curl.
# Usage: ./scripts/build.sh [version]   (default 0.1.0)
set -euo pipefail

REPO="$(cd "$(dirname "$0")/.." && pwd)"
cd "$REPO"
VERSION="${1:-0.1.0}"

: "${ANDROID_NDK_HOME:?set ANDROID_NDK_HOME to your NDK path}"
TOOL="$ANDROID_NDK_HOME/toolchains/llvm/prebuilt/linux-x86_64/bin"
[ -d "$TOOL" ] || { echo "NDK toolchain not found at $TOOL"; exit 1; }

# zygisk.hpp is canonical upstream; Magisk guarantees backward compat.
if [ ! -f src/zygisk.hpp ]; then
  echo "fetching zygisk.hpp from topjohnwu/zygisk-module-sample..."
  curl -sL -o src/zygisk.hpp \
    https://raw.githubusercontent.com/topjohnwu/zygisk-module-sample/master/module/jni/zygisk.hpp
fi

API=21

# Phase 4: Dobby (optional). Vendor with:
#   git clone https://github.com/jmpews/Dobby third_party/dobby
# then build Dobby per its docs (CMake + NDK) to produce libdobby.a
DOBBY_FLAGS=""; DOBBY_LIB=""
if [ -d "third_party/dobby/include" ]; then
  DOBBY_FLAGS="-DHAVE_DOBBY -Ithird_party/dobby/include"
  DOBBY_LIB="$(ls third_party/dobby/build/libdobby.a 2>/dev/null || true)"
  echo "Dobby found: Phase 4 native hooks enabled"
fi

build_abi() { # $1 triple-prefix, $2 zygisk dir name
  local cxx="$TOOL/$1-linux-android$API-clang++"
  [ "$1" = "armv7a" ] && cxx="$TOOL/armv7a-linux-androideabi$API-clang++"
  echo "building $2..."
  mkdir -p module/zygisk
  "$cxx" -std=c++17 -shared -fPIC -O2 -ffunction-sections -fdata-sections \
    -Wl,--gc-sections $DOBBY_FLAGS -I src src/spoof.cpp src/hooks.cpp \
    ${DOBBY_LIB:+$DOBBY_LIB} -llog -o "module/zygisk/$2.so"
  "$TOOL/llvm-strip" --strip-unneeded "module/zygisk/$2.so"
}
build_abi aarch64 arm64-v8a
build_abi armv7a armeabi-v7a
build_abi x86_64 x86_64

chmod 0755 module/customize.sh module/uninstall.sh \
  module/META-INF/com/google/android/update-binary

sed -i "s/^version=.*/version=v$VERSION/" module/module.prop

ZIP="openspoof-v$VERSION.zip"
rm -f "$ZIP"
(cd module && zip -r -q -X "../$ZIP" . -x '.*')
echo "packaged -> $ZIP ($(du -h "$ZIP" | cut -f1))"
