#!/sbin/sh
# OpenSpoof uninstall — nothing device-wide was ever changed, so just clean up.
rm -rf /data/adb/modules/openspoof 2>/dev/null || true
