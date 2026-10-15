#!/bin/bash
# OpenSpoof test runner. Runs every test_*.sh script in this directory
# and exits non-zero if any test fails. CI should call this script.
TESTS_DIR="$(cd "$(dirname "$0")" && pwd)"
REPO_DIR="$(cd "$TESTS_DIR/.." && pwd)"
# Pin TMPDIR to a repo-scoped path so build tools that need
# temporary files (clang preprocessor, etc.) work in sandboxes
# where the system /tmp is read-only. On Linux CI, /tmp is
# normally writable, so we still allow the fast path. On
# Windows, the Git Bash /tmp resolves to
# C:\Users\<user>\AppData\Local\Temp which may be restricted;
# we always override there.
case "$(uname -s 2>/dev/null)" in
  MINGW*|MSYS*|CYGWIN*)
    BASE_TMP="$REPO_DIR/.tmp"
    ;;
  *)
    if [ -d "/tmp" ] && [ -w "/tmp" ]; then
      BASE_TMP="/tmp"
    else
      BASE_TMP="$REPO_DIR/.tmp"
    fi
    ;;
esac
mkdir -p "$BASE_TMP"
export TMPDIR="$BASE_TMP" TMP="$BASE_TMP" TEMP="$BASE_TMP"
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
    # When CC is a Windows executable invoked from a POSIX shell
    # (Git Bash on Windows), the Cygwin/MSYS layer does not
    # propagate TMPDIR to the native Win32 process. The MSVC
    # clang driver reads the Win32 GetTempPath API which uses
    # the uppercase TMP / TEMP variables. Fall back to
    # PowerShell when those vars are not on the Windows side.
    # Detect a Windows-hosted compiler: explicit .exe suffix,
    # or the Cygwin path without .exe but the underlying binary
    # is an .exe (Git Bash resolves "clang++" to a POSIX-style
    # path that still calls clang++.exe).
    is_windows_cc=0
    if [[ "$CC" == *.exe ]] || [[ "$CC" == *clang++* && -z "${CC##*clang++}" ]]; then
      is_windows_cc=1
    fi
    if [ $is_windows_cc -eq 1 ] && command -v powershell.exe >/dev/null 2>&1; then
      # PowerShell runs on the Windows side and expects Windows
      # paths. Convert every path the build touches via cygpath
      # so the cd, the source file, the include dirs, and the
      # output file all resolve.
      win_repo="$(cygpath -w "$REPO_DIR")"
      win_base_tmp="$(cygpath -w "$BASE_TMP")"
      win_src="$(cygpath -w "$t")"
      win_out="$(cygpath -w "$out")"
      ps_cmd="\$env:TMP='$win_base_tmp';\$env:TEMP='$win_base_tmp';cd '$win_repo';& '$CC' -std=c++17 -Wall -I src -I tests '$win_src' -o '$win_out';exit \$LASTEXITCODE"
      cmd_out=$(powershell.exe -NoProfile -Command "$ps_cmd" 2>&1)
      cmd_rc=$?
    else
      cmd_out=$("$CC" -std=c++17 -Wall -I src -I tests "$t" -o "$out" 2>&1)
      cmd_rc=$?
    fi
    if [ $cmd_rc -eq 0 ] && "$out" 2>&1; then
      echo "OK: $t"
    else
      printf '%s\n' "$cmd_out"
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
