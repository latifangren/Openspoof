#!/bin/bash
# OpenSpoof test runner. Runs every test_*.sh script in this directory
# and exits non-zero if any test fails. CI should call this script.
TESTS_DIR="$(cd "$(dirname "$0")" && pwd)"
REPO_DIR="$(cd "$TESTS_DIR/.." && pwd)"
FAIL=0
RUN=0

for t in "$TESTS_DIR"/test_*.sh; do
  [ -f "$t" ] || continue
  RUN=$((RUN+1))
  echo "==> $t"
  if bash "$t"; then
    echo "OK: $t"
  else
    echo "FAILED: $t"
    FAIL=$((FAIL+1))
  fi
  echo
done

# C++ tests: build with host clang then run. We deliberately
# pin the build directory to a repo-scoped path (BASE_TMP) so the
# runner works in sandboxes where /tmp is read-only. The path is
# the same as tests/test_sepolicy_helper.sh to keep the convention
# consistent.
#
# If no C++ compiler is on PATH, we skip the C++ tests with a
# clear notice rather than fail. CI on ubuntu-latest installs
# clang via the build job, so a missing compiler only happens in
# developer sandboxes.
CC="${CC:-clang++}"
if ! command -v "$CC" >/dev/null 2>&1; then
  echo "NOTE: '$CC' not on PATH; skipping C++ tests in $TESTS_DIR"
else
  CXX_BUILD_BASE="$REPO_DIR/.tmp/test_cpp_bin"
  mkdir -p "$CXX_BUILD_BASE"
  for t in "$TESTS_DIR"/test_*.cpp; do
    [ -f "$t" ] || continue
    RUN=$((RUN+1))
    echo "==> $t (C++)"
    out="$CXX_BUILD_BASE/$(basename "$t" .cpp)"
    if "$CC" -std=c++17 -Wall -I src -I tests "$t" -o "$out" 2>&1 \
         && "$out" 2>&1; then
      echo "OK: $t"
    else
      echo "FAILED: $t"
      FAIL=$((FAIL+1))
    fi
    echo
  done
fi

if [ "$RUN" -eq 0 ]; then
  echo "No test_*.sh scripts found in $TESTS_DIR" >&2
  exit 1
fi
if [ "$FAIL" -gt 0 ]; then
  echo "----"
  echo "$FAIL test file(s) failed out of $RUN"
  exit 1
fi
echo "All $RUN test file(s) passed."
exit 0
