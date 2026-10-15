#!/sbin/sh
# OpenSpoof installer — runs on Magisk, KernelSU and APatch.
# Self-contained: no util_functions.sh dependency.
# Env provided by update-binary: MODPATH, ZIPFILE, OUTFD

ui_print "- OpenSpoof v0.1.0"
ui_print "- Per-app device & CPU spoofing (open source, free)"

set_perm() {
  # $1 path, $2 uid, $3 gid, $4 mode
  chown "$2:$3" "$1" 2>/dev/null || chown "$2.$3" "$1" 2>/dev/null || true
  chmod "$4" "$1" || true
}

# config.json must survive updates: keep the user's existing one
if [ -f "/data/adb/modules/openspoof/config.json" ] && [ "$MODPATH" != "/data/adb/modules/openspoof" ]; then
  ui_print "- Keeping existing config.json"
  cp -f "/data/adb/modules/openspoof/config.json" "$MODPATH/config.json.bak"
fi

set_perm "$MODPATH/zygisk/arm64-v8a.so" 0 0 0644 2>/dev/null || true
set_perm "$MODPATH/zygisk/armeabi-v7a.so" 0 0 0644 2>/dev/null || true
set_perm "$MODPATH/zygisk/x86_64.so" 0 0 0644 2>/dev/null || true
set_perm "$MODPATH/config.json" 0 0 0644
set_perm "$MODPATH/config.default.json" 0 0 0644 2>/dev/null || true
set_perm "$MODPATH/devices.conf" 0 0 0644
set_perm "$MODPATH/webroot/index.html" 0 0 0644 2>/dev/null || true
set_perm "$MODPATH/service.sh" 0 0 0755
set_perm "$MODPATH/uninstall.sh" 0 0 0755

if [ -f "$MODPATH/config.json.bak" ]; then
  mv -f "$MODPATH/config.json.bak" "$MODPATH/config.json"
  ui_print "- Restored your config.json"
fi

ui_print "- Done. Reboot, then relaunch a configured app to apply."
ui_print "- Edit /data/adb/modules/openspoof/config.json to add apps/profiles."
