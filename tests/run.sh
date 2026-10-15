#!/bin/bash
# OpenSpoof test runner. Runs every test_*.sh script in this directory
# and exits non-zero if any test fails. CI should call this script.
set -u

TESTS_DIR="$(cd "$(dirname "$0")" && pwd)"
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
