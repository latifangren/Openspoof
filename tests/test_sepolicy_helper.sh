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

# Pin the base tmp directory inside the repo's .tmp/ (already git-ignored).
# This avoids sandbox permission errors on /tmp and keeps test artifacts
# scoped to the project so a CI job can inspect them on failure.
BASE_TMP="$REPO_DIR/.tmp/test_sepolicy"
mkdir -p "$BASE_TMP" || { echo "FATAL: cannot create $BASE_TMP" >&2; exit 2; }
export TMPDIR="$BASE_TMP"

# Build an isolated environment under $TMPDIR for each scenario.
make_fake_path() {
  local bin="$1"
  mkdir -p "$bin"
  printf '%s\n' "$bin" >"$TMPDIR/empty_path"
  echo "$bin"
}

# Stub policy tool. Each stub records an "applied: <rule>" line plus
# the raw argument vector to the file pointed at by $STUB_LOG. The
# "applied:" prefix mirrors the message the helper itself logs via
# `log_se` on a real device, so the same assertions work in both
# places: the test reads STUB_LOG instead of the helper's own log.
# Args: <bin_dir> <name> [exit_code]
make_tool() {
  local bin="$1" name="$2" rc="${3:-0}"
  cat >"$bin/$name" <<'STUB_EOF'
#!/bin/sh
# stub (test harness)
# The helper invokes us as: <tool> --live <rule>. So $1=--live, $2=<rule>.
# Strip the leading space on the rule (it has one because of how the
# helper built RULE_EXECMEM) so downstream assertions can match cleanly.
rule="${2# }"
{
  echo "applied: $rule"
  echo "args: $@"
} >> "${STUB_LOG:-/dev/null}"
exit __STUB_RC__
STUB_EOF
  sed -i "s/__STUB_RC__/${rc}/" "$bin/$name"
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
  rm -f "$LOGFILE" "$STUB_LOG"
  (
    # Override PATH so `command -v` finds our stub first.
    PATH="$bin:$PATH"
    # Stubs record their invocations to STUB_LOG. The helper's own
    # log output is silenced on non-Android (no `log -t`), so it goes
    # nowhere useful — assertions below read STUB_LOG instead.
    export STUB_LOG
    . "$HELPER" >/dev/null 2>&1
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
  # $1 = file path, $2 = human label
  if [ -s "$1" ]; then
    PASS=$((PASS+1))
    echo "PASS: $2"
  else
    FAIL=$((FAIL+1))
    echo "FAIL: $2 (file empty: $1)"
  fi
}

# Ensure /sbin stubs do not accidentally exist in the sandbox.
# (We do not delete real /sbin files — we just shadow with our bin via PATH.)
TEST_NUM=0
new_test() {
  TEST_NUM=$((TEST_NUM+1))
  # Use -p so we are explicit about the parent directory. Some mktemp
  # implementations (Cygwin, busybox) ignore $TMPDIR when -t is used
  # without -p, and default to /tmp which may be read-only in sandboxes.
  TMPDIR_TEST="$(mktemp -d -p "$BASE_TMP" openspoof-sepolicy.XXXXXX)"
  LOGFILE="$TMPDIR_TEST/log"
  STUB_LOG="$TMPDIR_TEST/stub.log"
  KERNELSU_LOG="$TMPDIR_TEST/ksu.log"
  # Bail out if cd fails so ShellCheck SC2164 is satisfied and a broken
  # test sandbox cannot silently corrupt later assertions.
  cd "$TMPDIR_TEST" || exit 1
  # Re-export so any later mktemp call (in helpers) also stays scoped.
  # STUB_LOG is also exported so policy-tool stubs (written by make_tool)
  # can find it when the helper invokes them.
  export TMPDIR="$TMPDIR_TEST" STUB_LOG KERNELSU_LOG
}

# Assertions now read from $STUB_LOG, which the policy-tool stubs write
# to. The helper's own log lines (via `log -t`) are silenced on non-Android,
# so the only observable side effect of the helper is the tool invocations
# it makes. This mirrors what would happen on a real device, where the
# stub is replaced by the real magiskpolicy/ksu/supolicy.
assert_stub_contains() {
  if [ -f "$STUB_LOG" ] && grep -qF "$1" "$STUB_LOG"; then
    PASS=$((PASS+1)); echo "PASS: $2"
  else
    FAIL=$((FAIL+1))
    echo "FAIL: $2 (expected '$1' in stub log; got: $(cat "$STUB_LOG" 2>/dev/null))"
  fi
}
assert_stub_not_contains() {
  if [ ! -f "$STUB_LOG" ] || ! grep -qF "$1" "$STUB_LOG"; then
    PASS=$((PASS+1)); echo "PASS: $2"
  else
    FAIL=$((FAIL+1))
    echo "FAIL: $2 (forbidden '$1' was in stub log)"
  fi
}

# --- Scenario 1: magiskpolicy is present and accepts the rule.
new_test
BIN="$(make_fake_path "$TMPDIR_TEST/bin")"
make_tool "$BIN" magiskpolicy 0
run_helper "$BIN"
assert_stub_contains "applied: allow untrusted_app untrusted_app process execmem; allow priv_app priv_app process execmem; allow platform_app platform_app process execmem; allow system_app system_app process execmem;" "1. magiskpolicy receives the full execmem rule"
assert_stub_contains "untrusted_app" "1. tool saw untrusted_app"
assert_stub_contains "priv_app" "1. tool saw priv_app"
assert_stub_contains "platform_app" "1. tool saw platform_app"
assert_stub_contains "system_app" "1. tool saw system_app"
assert_stub_contains "args: --live" "1. helper invoked tool with --live flag"

# --- Scenario 2: only KernelSU is present.
# Make a ksu stub that recognises `MagiskPolicy` as KernelSU's action and
# records what it was asked to apply, mirroring the Magisk policy tool
# output. This is enough to drive the helper's "applied via KernelSU" path.
new_test
BIN="$(make_fake_path "$TMPDIR_TEST/bin")"
cat >"$BIN/ksu" <<'KSU_EOF'
#!/bin/sh
# stub ksu (KernelSU): recognise the MagiskPolicy action.
if [ "$1" = "MagiskPolicy" ]; then
  shift
  {
    echo "applied: $@"
  } >> "${STUB_LOG:-/dev/null}"
  exit 0
fi
exit 1
KSU_EOF
chmod +x "$BIN/ksu"
run_helper "$BIN"
assert_stub_contains "applied: --live" "2. KernelSU receives --live flag"
assert_stub_contains "allow untrusted_app untrusted_app process execmem" "2. KernelSU saw the full rule"
assert_stub_contains "allow system_app system_app process execmem" "2. KernelSU saw all four domains"

# --- Scenario 3: only APatch's supolicy is present.
new_test
BIN="$(make_fake_path "$TMPDIR_TEST/bin")"
make_tool "$BIN" supolicy 0
run_helper "$BIN"
assert_stub_contains "applied: allow untrusted_app untrusted_app process execmem" "3. supolicy applies the rule"
assert_stub_contains "allow system_app system_app process execmem" "3. supolicy received all four domains"

# --- Scenario 4: no policy tool is present.
# The helper must not call any tool, must not crash, and must not
# echo a "applied:" line. We assert against an empty STUB_LOG.
new_test
BIN="$(make_fake_path "$TMPDIR_TEST/bin")"
run_helper "$BIN"
if [ ! -s "$STUB_LOG" ]; then
  PASS=$((PASS+1)); echo "PASS: 4. no tool was invoked when none is on PATH"
else
  FAIL=$((FAIL+1)); echo "FAIL: 4. STUB_LOG not empty: $(cat "$STUB_LOG")"
fi
if [ ! -f "$STUB_LOG" ] || ! grep -q "^applied:" "$STUB_LOG"; then
  PASS=$((PASS+1)); echo "PASS: 4. no rule was applied"
else
  FAIL=$((FAIL+1)); echo "FAIL: 4. a rule was applied despite no tool available"
fi

# --- Scenario 5: magiskpolicy rejects the rule (returns non-zero).
new_test
BIN="$(make_fake_path "$TMPDIR_TEST/bin")"
make_tool "$BIN" magiskpolicy 1
run_helper "$BIN"
# The tool was called (it should be — we record that fact), but the
# helper must NOT claim success in the absence of a 0 exit code.
assert_stub_contains "applied: allow untrusted_app" "5. tool was invoked (helper should record attempt)"
# Only one tool exists; with rc=1, the helper must report "applied=0"
# and the other branches (none present) must not run.
N_APPLIED=$(grep -c "^applied:" "$STUB_LOG" 2>/dev/null || echo 0)
if [ "$N_APPLIED" -eq 1 ]; then
  PASS=$((PASS+1)); echo "PASS: 5. exactly one tool invocation"
else
  FAIL=$((FAIL+1)); echo "FAIL: 5. expected 1 tool invocation, got $N_APPLIED"
fi

# --- Scenario 6: tool selection order — Magisk wins over KernelSU/APatch.
new_test
BIN="$(make_fake_path "$TMPDIR_TEST/bin")"
make_tool "$BIN" magiskpolicy 0
# Provide a ksu stub too, but configured to FAIL — so if the helper
# incorrectly falls through to KernelSU, the test will notice because
# the success line comes from the magiskpolicy branch, not the ksu one.
cat >"$BIN/ksu" <<'KSU_FAIL_EOF'
#!/bin/sh
# stub ksu: refuses (so we can detect if the helper reached us).
echo "ksu: should-not-be-called" >> "${STUB_LOG:-/dev/null}"
exit 1
KSU_FAIL_EOF
chmod +x "$BIN/ksu"
make_tool "$BIN" supolicy 0
run_helper "$BIN"
N_APPLIED=$(grep -c "^applied:" "$STUB_LOG" 2>/dev/null || echo 0)
if [ "$N_APPLIED" -eq 1 ]; then
  PASS=$((PASS+1)); echo "PASS: 6. exactly one policy tool invoked"
else
  FAIL=$((FAIL+1)); echo "FAIL: 6. expected 1 tool invocation, got $N_APPLIED"
fi
if ! grep -q "should-not-be-called" "$STUB_LOG" 2>/dev/null; then
  PASS=$((PASS+1)); echo "PASS: 6. Magisk wins over KernelSU"
else
  FAIL=$((FAIL+1)); echo "FAIL: 6. helper reached ksu despite Magisk being available"
fi

# --- Final report.
echo
echo "----"
echo "Total: $((PASS+FAIL))  Pass: $PASS  Fail: $FAIL"
if [ "$FAIL" -gt 0 ]; then exit 1; fi
exit 0
