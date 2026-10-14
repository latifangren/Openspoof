# OpenSpoof

Per-app device & CPU spoofing for rooted Android (Magisk / KernelSU / APatch).
Open source, free, no license server. Inspired by the architecture of COPG,
rebuilt from open components.

- **Phase 1:** spoof `android.os.Build` / `Build.VERSION` per app via JNI (no hook engine needed)
- **Phase 2:** per-process `/proc/cpuinfo` via mount-namespace bind-mount
- **Phase 3:** on-device WebUI config editor

See [SPEC.md](SPEC.md) for the full spec (in Indonesian).

## Build

```bash
export ANDROID_NDK_HOME=/path/to/ndk
./scripts/build.sh 0.1.0
# -> openspoof-v0.1.0.zip (flash in Magisk/KernelSU/APatch)
```

## Configure

Edit `/data/adb/modules/openspoof/config.json` — map package names to profiles:

```json
{ "apps": { "com.example.game": "redmagic_9_pro" } }
```

Relaunch the app to apply. Check `adb logcat -s OpenSpoof` for confirmation.
