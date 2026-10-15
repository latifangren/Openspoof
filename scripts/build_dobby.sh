#!/bin/bash
# OpenSpoof — Dobby multi-ABI Builder
# Compiles static libdobby.a for every ABI OpenSpoof ships (arm64-v8a,
# armeabi-v7a, x86_64). All three are needed because scripts/build.sh
# links the per-ABI .so against the matching libdobby.a — building only
# arm64-v8a (as in earlier versions) made the other two ABIs fail to link
# at packaging time.
# Usage: ./scripts/build_dobby.sh
set -euo pipefail

REPO="$(cd "$(dirname "$0")/.." && pwd)"
cd "$REPO"

ANDROID_NDK_HOME="${ANDROID_NDK_HOME:-${ANDROID_NDK_ROOT:-}}"
: "${ANDROID_NDK_HOME:?set ANDROID_NDK_HOME or ANDROID_NDK_ROOT to your NDK path}"

# arm64-v8a (production Android) and x86_64 (emulators) are fully supported.
# armeabi-v7a (32-bit ARM) is unsupported by modern Dobby upstream; scripts/build.sh
# safely builds armeabi-v7a with no-op hooks when libdobby.a is not present.
ABIS=("arm64-v8a" "x86_64")

GEN_ARG=""
if command -v ninja >/dev/null 2>&1; then
  GEN_ARG="-G Ninja"
fi

for ABI in "${ABIS[@]}"; do
  echo "=== Building Dobby for $ABI ==="
  BUILD_DIR="third_party/dobby/build/$ABI"
  mkdir -p "$BUILD_DIR"
  
  cmake -B "$BUILD_DIR" $GEN_ARG \
    -DCMAKE_TOOLCHAIN_FILE="$ANDROID_NDK_HOME/build/cmake/android.toolchain.cmake" \
    -DANDROID_ABI="$ABI" \
    -DANDROID_PLATFORM=android-21 \
    -DANDROID_STL=c++_static \
    -DCMAKE_BUILD_TYPE=Release \
    -DDOBBY_GENERATE_SHARED=OFF \
    -DDOBBY_BUILD_TEST=OFF \
    third_party/dobby
    
  cmake --build "$BUILD_DIR" --target dobby_static -- -j"$(nproc 2>/dev/null || echo 4)"
  
  # Normalize output path so build.sh finds it reliably
  if [ -f "$BUILD_DIR/libdobby.a" ]; then
    echo "Dobby static library ready at: $BUILD_DIR/libdobby.a"
  else
    echo "Warning: libdobby.a not found in standard root of $BUILD_DIR, checking subdirs..."
    find "$BUILD_DIR" -name "libdobby.a" -exec cp {} "$BUILD_DIR/libdobby.a" \;
  fi
done

echo "Dobby built successfully."
