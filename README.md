# OpenSpoof

Per-app device, CPU, and identity spoofing for rooted Android
(Magisk / KernelSU / APatch). Open source, free, no license server.

OpenSpoof lets a configured app believe it is running on a different
device (brand, model, fingerprint, GPU, IMEI, ...). Unconfigured apps
and system processes are not touched. Relaunching the app picks up
config changes; no reboot required.

## Highlights

- `android.os.Build` and `Build.VERSION` field spoofing via JNI
  (no inline-hook engine needed).
- Per-process `/proc/cpuinfo` via mount-namespace bind-mount.
- On-device WebUI editor (KernelSU / APatch WebUI bridge).
- Native hooks for GPU (OpenGL / EGL / Vulkan), system properties,
  uname, uptime, MAC, telephony, DRM, and location.

## Quick start

1. Build:

   ```bash
   export ANDROID_NDK_HOME=/path/to/ndk
   ./scripts/build.sh 0.2.0
   # -> openspoof-v0.2.0.zip
   ```

2. Flash `openspoof-v0.2.0.zip` in Magisk / KernelSU / APatch and reboot.

3. Open the WebUI, add a package, pick a profile, save.

4. Relaunch the app. Check `adb logcat -s OpenSpoof` for confirmation.

## Documentation

The full doc map is in **[docs/INDEX.md](docs/INDEX.md)**. Start there.

- [docs/SPEC.md](docs/SPEC.md) — contract, architecture, scope, non-goals.
- [docs/ROADMAP.md](docs/ROADMAP.md) — phases, status, gap analysis.
- [docs/PHASE4.md](docs/PHASE4.md) — native hook engine (Dobby) details.
- [docs/CHANGELOG.md](docs/CHANGELOG.md) — release notes.

## Honest scope

OpenSpoof is a device-spoofing tool, not an anti-cheat bypass. TEE
attestation and Widevine L1 are not addressable from user space.
Anti-cheat that does kernel-level or server-side clustering will
detect inconsistencies regardless of what user-space hooks report.
See [docs/SPEC.md](docs/SPEC.md) § Non-goals for the full list.

## Status

Phase 1–4 code is implemented and passes syntax-check. The module
has not been validated end-to-end on a physical rooted device.
See [docs/ROADMAP.md](docs/ROADMAP.md) for the per-phase checklist
and the remaining work.
