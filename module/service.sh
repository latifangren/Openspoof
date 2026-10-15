#!/system/bin/sh
# OpenSpoof boot-time safety & recovery daemon
MODDIR=${0%/*}
CONFIG=$MODDIR/config.json
BACKUP=$MODDIR/config.backup.json
TEMPLATE=$MODDIR/config.default.json
DISABLE=$MODDIR/.disable_openspoof

# 1. Emergency disable sentinel
if [ -f "$DISABLE" ]; then
  log -t OpenSpoof "Emergency disable sentinel present (.disable_openspoof) — leaving module inactive"
  exit 0
fi

# 2. Ensure config.json exists
if [ ! -f "$CONFIG" ]; then
  if [ -f "$TEMPLATE" ]; then
    cp "$TEMPLATE" "$CONFIG" 2>/dev/null || true
    chmod 0600 "$CONFIG" 2>/dev/null || true
    log -t OpenSpoof "Initialized default config.json from template"
  fi
fi

# 3. Simple JSON sanity validation
is_valid_json() {
  local file="$1"
  [ -s "$file" ] || return 1
  # Check basic braces balance and mandatory keys
  grep -q '"profiles"' "$file" && grep -q '"apps"' "$file"
}

if ! is_valid_json "$CONFIG"; then
  log -t OpenSpoof "WARNING: config.json corrupted or empty!"
  if is_valid_json "$BACKUP"; then
    cp "$BACKUP" "$CONFIG"
    chmod 0600 "$CONFIG" 2>/dev/null || true
    log -t OpenSpoof "Successfully recovered config.json from backup"
  elif [ -f "$TEMPLATE" ]; then
    cp "$TEMPLATE" "$CONFIG"
    chmod 0600 "$CONFIG" 2>/dev/null || true
    log -t OpenSpoof "Restored default config.json from template"
  fi
else
  # Config is healthy -> update backup
  cp "$CONFIG" "$BACKUP" 2>/dev/null || true
  chmod 0600 "$BACKUP" 2>/dev/null || true
fi

chmod 0600 "$CONFIG" 2>/dev/null || true
