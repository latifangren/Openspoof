#!/system/bin/sh
# OpenSpoof — runtime SELinux helper
#
# Phase 4 native hooks (Dobby) need anonymous RWX pages (trampolines) and
# mprotect on .text. Most ROMs deny this under enforcing SELinux. The static
# rule in `module/sepolicy.rule` is a *template*; on Android 11+ with
# enforcing mode, it often needs to be applied at runtime via a root policy
# tool that is already available on the device.
#
# This helper is sourced by `module/service.sh` at boot. It is best-effort:
# if no policy tool is present, the call is a no-op + log, and Phase 1–2
# (which do not need execmem) keep working.
#
# Detected policy tools (in order of preference):
#   magiskpolicy  — Magisk
#   ksu           — KernelSU (calls `ksu MagiskPolicy` if available)
#   supolicy      — APatch
#
# Required allowance for Phase 4 (Dobby inline hooks):
#   allow untrusted_app untrusted_app process execmem
# We apply the same rule to a few related app domains to be safe across
# OEMs. If a rule already exists, the policy tools silently skip it.

set -u

# logging helper: writes to logcat tag "OpenSpoof".
log_se() {
  # On Android, `log -t` writes to logcat. On other systems (test sandboxes,
  # CI) it does not exist, and /dev/kmsg may be unwritable — sending the
  # fallback there floods stderr. We deliberately drop the message in that
  # case: the test harness reads state via the rule-application return code
  # and tool invocations, not via this log line.
  log -t OpenSpoof "sepolicy: $1" 2>/dev/null || true
}

# Returns 0 if the named tool is callable from $PATH or a known absolute path.
have_tool() {
  command -v "$1" >/dev/null 2>&1 || [ -x "$2" ] 2>/dev/null
}

apply_rule() {
  # $1 = policy tool path, $2 = rule body
  # Most tools return 0 on success even when the rule already exists.
  if "$1" --live "$2" >/dev/null 2>&1; then
    log_se "applied: $2"
    return 0
  fi
  return 1
}

# Domain set: keeps the rule minimal. If your ROM rejects any of these,
# remove the failing one. Edit this list rather than duplicating the helper.
DOMAINS="untrusted_app priv_app platform_app system_app"

# Rule body for Dobby trampolines.
RULE_EXECMEM=""
for d in $DOMAINS; do
  RULE_EXECMEM="$RULE_EXECMEM allow $d $d process execmem;"
done

applied=0

# 1) Magisk
if have_tool magiskpolicy "/sbin/magiskpolicy"; then
  TOOL="magiskpolicy"
elif [ -x "/sbin/magiskpolicy" ]; then
  TOOL="/sbin/magiskpolicy"
else
  TOOL=""
fi

if [ -n "$TOOL" ]; then
  if apply_rule "$TOOL" "$RULE_EXECMEM"; then
    applied=1
  else
    log_se "$TOOL rejected rule, continuing anyway"
  fi
fi

# 2) KernelSU (preferred tool name: `ksu`, action `MagiskPolicy`)
if [ "$applied" -eq 0 ] && have_tool ksu ""; then
  if ksu MagiskPolicy --live "$RULE_EXECMEM" >/dev/null 2>&1; then
    log_se "applied via KernelSU: $RULE_EXECMEM"
    applied=1
  fi
fi

# 3) APatch
if [ "$applied" -eq 0 ] && have_tool supolicy "/sbin/supolicy"; then
  if apply_rule supolicy "$RULE_EXECMEM"; then
    applied=1
  fi
fi

if [ "$applied" -eq 0 ]; then
  log_se "no policy tool found (magiskpolicy / ksu / supolicy); Dobby may be denied on enforcing ROMs"
  log_se "if Dobby hooks fail with avc denials, apply the rule manually or use sepolicy.rule"
else
  log_se "Phase 4 SELinux policy applied"
fi
