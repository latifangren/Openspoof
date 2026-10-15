#!/bin/bash
# Test for module/sepolicy_helper.sh
#
# Verifies the helper behaves correctly under the three situations it can
# encounter on a real device, by stubbing the policy tools it shells out to:
#   1. Magisk present  -> rule applied via magiskpolicy.
#   2. KernelSU present -> rule applied via `ksu MagiskPolicy`.
#   3. APatch present   -> rule applied via supolicy.
#   4. None present     -> helper logs a warning, exits 0, no rule applied.
#   5. magiskpolicy rejects the rule (returns non-zero) -> helper logs and
#      continues, no crash.
#   6. Tool selection order: if multiple tools are present, Magisk wins.
#
# Each test sources the helper in a subshell with PATH/BIN_DIR overridden,
# so the real helper is exercised end-to-end (not a re-implementation).

set -u
PASS=0
FAIL=0

# Resolve repo root from this script's location.
TESTS_DIR="$(cd "$(dirname "$0")" && pwd)"
REPO_DIR="$(cd "$TESTS_DIR/.." && pwd)"
HELPER="$REPO_DIR/module/sepolicy_helper.sh"

if [ ! -f "$HELPER" ]; then
  echo "FATAL: helper not found at $HELPER" >&2
  exit 2
fi

# Build an isolated environment under $TMPDIR for each scenario.
make_fake_path() {
  local bin="$1"
  mkdir -p "$bin"
  printf '%s\n' "$bin" >"$TMPDIR/empty_path"
  echo "$bin"
}

# Stub policy tool. Args: <bin_dir> <name> [exit_code]
make_tool() {
  local bin="$1" name="$2" rc="${3:-0}"
  cat >"$bin/$name" <<EOF
#!/bin/sh
# stub $name
echo "\$@" >> "$LOGFILE"
exit $rc
EOF
  chmod +x "$bin/$name"
}

# Source the helper with a chosen BIN_DIR first on PATH. We cannot modify
# `command -v` resolution inside the helper, but `have_tool` checks
# both `command -v` and a hardcoded `/sbin/<tool>` absolute path. We avoid
# the absolute-path branch by ensuring `/sbin/magiskpolicy`,
# `/sbin/supolicy` are NOT present in the sandbox.
#
# The helper defines helper-internal functions (have_tool, apply_rule, log_se).
# Sourcing it runs the policy application. We redirect logs to LOGFILE.
run_helper() {
  local bin="$1"
  (
    # Override PATH so `command -v` finds our stub first.
    PATH="$bin:$PATH"
    # Set log destination via env var the helper could read; the helper
    # currently uses `log -t` (Android-only) and falls back to /dev/kmsg.
    # Capture fallback output by overriding /dev/kmsg in a writable file.
    rm -f "$LOGFILE"
    # Stub /dev/kmsg by binding an fd: simpler — set IFS empty and let
    # the helper's `>/dev/kmsg 2>&1` go to a real /dev/kmsg on Linux.
    # In a non-Android sandbox, that write fails silently (helper
    # already handles that with `|| true`).
    . "$HELPER"
  )
  return $?
}

assert_log_contains() {
  if grep -qF "$1" "$LOGFILE" 2>/dev/null; then
    PASS=$((PASS+1))
    echo "PASS: $2"
  else
    FAIL=$((FAIL+1))
    echo "FAIL: $2 (expected '$1' in log; got: $(cat "$LOGFILE" 2>/dev/null))"
  fi
}

assert_log_not_contains() {
  if ! grep -qF "$1" "$LOGFILE" 2>/dev/null; then
    PASS=$((PASS+1))
    echo "PASS: $2"
  else
    FAIL=$((FAIL+1))
    echo "FAIL: $2 (forbidden '$1' was in log)"
  fi
}

assert_file_not_empty() {
  if [ -s "$LOGFILE" ]; then
    PASS=$((PASS+1))
    echo "PASS: $2"
  else
    FAIL=$((FAIL+1))
    echo "FAIL: $2 (log empty)"
  fi
}

# Ensure /sbin stubs do not accidentally exist in the sandbox.
# (We do not delete real /sbin files — we just shadow with our bin via PATH.)
TEST_NUM=0
new_test() {
  TEST_NUM=$((TEST_NUM+1))
  TMPDIR_TEST="$(mktemp -d -t openspoof-sepolicy.XXXXXX)"
  LOGFILE="$TMPDIR_TEST/log"
  cd "$TMPDIR_TEST"
}

# --- Scenario 1: magiskpolicy is present and accepts the rule.
new_test
BIN="$(make_fake_path "$TMPDIR_TEST/bin")"
make_tool "$BIN" magiskpolicy 0
run_helper "$BIN"
assert_log_contains "applied: allow untrusted_app untrusted_app process execmem" "1. magiskpolicy applies execmem rule"
assert_log_contains "Phase 4 SELinux policy applied" "1. helper reports success"
# Verify the rule body actually reached the tool, including all 4 domains.
assert_log_contains "untrusted_app" "1. tool saw untrusted_app"
assert_log_contains "priv_app" "1. tool saw priv_app"
assert_log_contains "platform_app" "1. tool saw platform_app"
assert_log_contains "system_app" "1. tool saw system_app"

# --- Scenario 2: only KernelSU is present.
new_test
BIN="$(make_fake_path "$TMPDIR_TEST/bin")"
cat >"$BIN/ksu" <<'EOF'
#!/bin/sh
# stub ksu: pass-through to a fake MagiskPolicy action
if [ "$1" = "MagiskPolicy" ]; then
  shift
  echo "$@" >> "$KERNELSU_LOG"
  exit 0
fi
exit 1
EOF
chmod +x "$BIN/ksu"
export KERNELSU_LOG="$TMPDIR_TEST/ksu.log"
run_helper "$BIN"
assert_log_contains "applied via KernelSU" "2. KernelSU path applies the rule"
assert_log_contains "allow untrusted_app untrusted_app process execmem" "2. KernelSU saw the full rule"
assert_file_not_empty "2. KernelSU received the rule body"

# --- Scenario 3: only APatch's supolicy is present.
new_test
BIN="$(make_fake_path "$TMPDIR_TEST/bin")"
make_tool "$BIN" supolicy 0
run_helper "$BIN"
assert_log_contains "applied: allow untrusted_app untrusted_app process execmem" "3. supolicy applies the rule"

# --- Scenario 4: no policy tool is present.
new_test
BIN="$(make_fake_path "$TMPDIR_TEST/bin")"
run_helper "$BIN"
# Helper must log a warning, not crash, and report no policy applied.
assert_log_contains "no policy tool found" "4. helper warns when nothing is present"
assert_log_not_contains "Phase 4 SELinux policy applied" "4. helper does not claim success"

# --- Scenario 5: magiskpolicy rejects the rule (returns non-zero).
new_test
BIN="$(make_fake_path "$TMPDIR_TEST/bin")"
make_tool "$BIN" magiskpolicy 1
run_helper "$BIN"
assert_log_contains "magiskpolicy rejected rule" "5. helper logs rejection, does not crash"
assert_log_not_contains "Phase 4 SELinux policy applied" "5. helper does not claim success after rejection"

# --- Scenario 6: tool selection order — Magisk wins over KernelSU/APatch.
new_test
BIN="$(make_fake_path "$TMPDIR_TEST/bin")"
make_tool "$BIN" magiskpolicy 0
make_tool "$BIN" ksu 0     # also present
make_tool "$BIN" supolicy 0  # also present
run_helper "$BIN"
# Magisk path: log shows "applied: ..." once, NOT "via KernelSU".
N_APPLIED=$(grep -c "^applied: " "$LOGFILE" 2>/dev/null || echo 0)
if [ "$N_APPLIED" -eq 1 ]; then
  PASS=$((PASS+1)); echo "PASS: 6. exactly one policy tool invoked"
else
  FAIL=$((FAIL+1)); echo "FAIL: 6. expected 1 tool invocation, got $N_APPLIED"
fi
assert_log_not_contains "via KernelSU" "6. Magisk wins over KernelSU"

# --- Final report.
echo
echo "----"
echo "Total: $((PASS+FAIL))  Pass: $PASS  Fail: $FAIL"
if [ "$FAIL" -gt 0 ]; then exit 1; fi
exit 0
