#!/bin/bash
# OpenSpoof build + package script.
# Needs: Android NDK (ANDROID_NDK_HOME), zip, curl.
# Usage: ./scripts/build.sh [version]   (default 0.1.0)
set -euo pipefail

REPO="$(cd "$(dirname "$0")/.." && pwd)"
cd "$REPO"
VERSION="${1:-0.1.0}"

ANDROID_NDK_HOME="${ANDROID_NDK_HOME:-${ANDROID_NDK_ROOT:-}}"
: "${ANDROID_NDK_HOME:?set ANDROID_NDK_HOME or ANDROID_NDK_ROOT to your NDK path}"

# Detect host OS toolchain
OS="$(uname -s 2>/dev/null | tr '[:upper:]' '[:lower:]' || echo 'linux')"
case "$OS" in
  linux*)               HOST_TAG="linux-x86_64" ;;
  darwin*)              HOST_TAG="darwin-x86_64" ;;
  msys*|mingw*|cygwin*) HOST_TAG="windows-x86_64" ;;
  *)                    HOST_TAG="linux-x86_64" ;;
esac

TOOL="$ANDROID_NDK_HOME/toolchains/llvm/prebuilt/$HOST_TAG/bin"
[ -d "$TOOL" ] || { echo "NDK toolchain not found at $TOOL"; exit 1; }

# zygisk.hpp is canonical upstream; Magisk guarantees backward compat.
if [ ! -f src/zygisk.hpp ]; then
  echo "fetching zygisk.hpp from topjohnwu/zygisk-module-sample..."
  curl -sL -o src/zygisk.hpp \
    https://raw.githubusercontent.com/topjohnwu/zygisk-module-sample/master/module/jni/zygisk.hpp
fi

API=21

# Phase 4: Dobby (optional).
DOBBY_FLAGS=""
if [ -d "third_party/dobby/include" ]; then
  DOBBY_FLAGS="-DHAVE_DOBBY -Ithird_party/dobby/include"
  echo "Dobby headers found: Phase 4 & PRO native hooks enabled"
fi

get_dobby_lib() { # $1 abi
  for p in "third_party/dobby/build/$1/libdobby.a" \
           "third_party/dobby/build/libdobby_$1.a" \
           "third_party/dobby/build/libdobby.a"; do
    if [ -f "$p" ]; then
      echo "$p"
      return
    fi
  done
}

build_abi() { # $1 triple-prefix, $2 zygisk dir name
  # Windows NDK ships cross-compilers as .exe (e.g. aarch64-linux-android21-clang++.exe).
  # Native (Linux/macOS) NDK has no extension. There is no `.cmd` shim for cross targets.
  local exe=""
  if [ "$HOST_TAG" = "windows-x86_64" ]; then exe=".exe"; fi
  local cxx="$TOOL/$1-linux-android$API-clang++$exe"
  if [ "$1" = "armv7a" ]; then cxx="$TOOL/armv7a-linux-androideabi$API-clang++$exe"; fi
  local strip="$TOOL/llvm-strip$exe"

  local dobby_lib="$(get_dobby_lib "$2")"

  if [ ! -x "$cxx" ] && [ ! -f "$cxx" ]; then
    echo "compiler not found: $cxx"; return 1
  fi

  echo "building $2 (dobby: ${dobby_lib:-none})..."
  mkdir -p module/zygisk
  "$cxx" -std=c++17 -shared -fPIC -O2 -ffunction-sections -fdata-sections \
    -Wl,--gc-sections $DOBBY_FLAGS -I src \
    src/spoof.cpp src/hooks.cpp src/f5_art_hooks.cpp \
    ${dobby_lib:+$dobby_lib} -llog -o "module/zygisk/$2.so"
  "$strip" --strip-unneeded "module/zygisk/$2.so"
}
build_abi aarch64 arm64-v8a
build_abi armv7a armeabi-v7a
build_abi x86_64 x86_64

# Sync webroot to module package
mkdir -p module/webroot
cp -f webroot/index.html module/webroot/index.html

chmod 0755 module/customize.sh module/service.sh module/uninstall.sh \
  module/META-INF/com/google/android/update-binary
chmod 0644 module/webroot/index.html module/devices.conf module/config.json 2>/dev/null || true

sed -i "s/^version=.*/version=v$VERSION/" module/module.prop

ZIP="openspoof-v$VERSION.zip"
rm -f "$ZIP"
(cd module && zip -r -q -X "../$ZIP" . -x '.*')
echo "packaged -> $ZIP ($(du -h "$ZIP" | cut -f1))"
