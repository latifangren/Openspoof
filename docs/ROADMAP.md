# OpenSpoof — Roadmap & Gap Analysis

**Status document:** living · v0.2.0-draft

This document tracks where OpenSpoof stands versus its target parity
with COPG Pro / SkySpoof, and the remaining work for each release.

> Source of truth: see [SPEC.md](SPEC.md) for contract and architecture.
> Phase 4 native hook details: [PHASE4.md](PHASE4.md).

---

## 1. Current status (v0.2.0)

| Component                       | Code status       | Source files                          | Notes |
|---------------------------------|-------------------|---------------------------------------|-------|
| F1: Java `Build` fields         | done (syntax OK)  | `src/spoof.cpp`                       | JNI in `preAppSpecialize`; not yet validated on a rooted device. |
| F2: CPU VFS spoof               | done (syntax OK)  | `src/spoof.cpp`, `module/cpuinfo/`    | `unshare(CLONE_NEWNS)` + `mount(MS_BIND)` per-process. RedMagic 9 Pro (SD 8 Gen 3) sample shipped. |
| F3: WebUI manager               | done              | `webroot/index.html`                  | Filter, package autocomplete via `pm`, atomic save via KSU/APatch bridge. |
| F4: Native hooks (Dobby)        | done              | `src/hooks.cpp`, `third_party/dobby/` | Hooks for `clock_gettime`, `uname`, `__system_property_get`, GLES/EGL/Vulkan. Remaining: link `libdobby.a` + device test. |
| Phase A: GPU spoof              | done              | `src/hooks.cpp`, `src/config.h`       | `glGetString`, `eglQueryString`, `eglGetProcAddress`, `vkGetPhysicalDeviceProperties`. Presets Adreno 750/830, Mali-G715. |
| Phase B: Hardware & Telephony   | done              | `src/spoof.cpp`, `src/hooks.cpp`      | Android ID, IMEI, IMSI, ICCID, Operator, MAC via `ioctl`. |
| Phase C: DRM & Location         | done              | `src/spoof.cpp`, `src/hooks.cpp`      | `MediaDrm.getPropertyString` via `RegisterNatives`, cell LAC/CID, GAID. |
| Device catalog                  | done              | `module/devices.conf`                 | 5 presets: RedMagic 9 Pro, ROG Phone 8 Pro, Galaxy S24 Ultra, Pixel 8/9 Pro. |
| Packaging & build               | done (bash)       | `scripts/build.{sh,ps1,build_dobby.sh}` | Multi-ABI NDK build + Dobby static lib. |
| Safety daemon                   | done              | `module/service.sh`                   | Boot-time config validation, backup rollback, `.disable_openspoof` sentinel. |

**Tag v1.0.0 will only be cut after the verification checklist in §5
is fully green on a physical rooted device.** Until then, every "done"
above means "code is in place", not "validated in production".

## 2. Gap matrix

| Dimension                       | OpenSpoof (now)              | SkySpoof v0.3.0          | COPG Pro v7.5.1          | Gap |
|---------------------------------|------------------------------|--------------------------|--------------------------|-----|
| `Build.*` timing                | `preAppSpecialize` (JNI)     | `postAppSpecialize` (JNI)| Native loader + DEX      | OpenSpoof earlier (before app code). |
| `/proc/cpuinfo`                 | Mount-NS bind-mount          | not present              | not present (props only) | **Unique to OpenSpoof.** |
| GPU spoof (GLES / Vulkan)       | done (Dobby)                 | not present              | done                     | Closed. |
| Native property hook            | done (`__system_property_get`)| not present             | done                     | Closed. |
| Hardware IDs (IMEI / SIM)       | done (GSM props + ioctl MAC) | not present              | done (`ImeiHook`)        | Closed. |
| Android ID                      | done (`Settings.Secure` cache)| not present             | done                     | Closed. |
| DRM / Widevine level            | done (`MediaDrm` via JNI)    | not present              | done (`wv.dex` + `DrmHook`) | Closed. |
| Location / Cell / GAID          | done (cell props + GAID)     | not present              | done (`GpsHook`)         | Closed (no full GPS yet — see F5). |
| Config validation & safety      | Hand-rolled minimal + `service.sh` rollback | `skyspoofctl` allowlist regex + auto-backup | Commercial daemon | Adopted from SkySpoof: backup + sentinel. |
| Device catalog                  | 5 presets                    | 6 presets                | Large cloud catalog      | Room to grow. |

## 3. Release phases

```
v0.2.0 (current) ───▶ v0.3.0 (Identity & Telephony) ───▶ v1.0.0 (Production)
                                                          ▲
        v0.4.0 (DRM / Location / GPS) ───────────────────┘
```

### v0.2.0 — Native GPU spoofing & safety engine (current)
F1–F4 code complete, Dobby vendored, safety daemon, device catalog.

### v0.3.0 — Hardware identifiers & telephony
Android ID, Telephony GSM properties, MAC via `ioctl`. (Code present;
remaining work is device validation + tightening.)

### v0.4.0 — DRM, location, sensors
Widevine level, cell LAC/CID, GAID. (Code present; same caveat.)
Stretch: full GPS (Fused Location provider), `Build.getSerial`,
refresh rate.

### v1.0.0 — Production parity
- All of §5 verification checklist green.
- Multi-ROM tested (≥ 3 OEM ROMs).
- Documentation reviewed, `LICENSE` added, `CHANGELOG.md` kept.
- Release pipeline (`release.yml`) tested on a tag.

## 4. Technical execution

```
                      ┌───────────────────────────┐
                      │    Zygote Spawn Process   │
                      └─────────────┬─────────────┘
                                    ▼
                      ┌───────────────────────────┐
                      │    onLoad (Zygisk Api)    │
                      └─────────────┬─────────────┘
                                    ▼
                      ┌───────────────────────────┐
                      │    preAppSpecialize       │ (UID 0 / Root)
                      │    (Process Match?)       │
                      └──────┬─────────────┬──────┘
                       NO    │             │ YES
                             │             ├─▶ [F2] CLONE_NEWNS + bind-mount /proc/cpuinfo
                             │             ├─▶ [F1] JNI Build.* & VERSION.* set static
                             │             ├─▶ [F4] Dobby: __system_property_get, uname, uptime
                             │             └─▶ [PRO] Dobby: glGetString, eglQueryString, ...
                             ▼
              ┌─────────────────────────────┐
              │ DLCLOSE_MODULE_LIBRARY      │
              │ (Zero footprint on apps     │
              │  not in the target list)    │
              └─────────────────────────────┘
```

## 5. Verification checklist (real device)

These must be green before any non-alpha release.

- [ ] **V-1 (Build fields):** target app → `Build.MODEL` and
  `Build.FINGERPRINT` match profile.
- [ ] **V-2 (CPU info):** shell inside the target app → `cat /proc/cpuinfo`
  matches the profile; outside → real values.
- [ ] **V-3 (GPU render):** AIDA64 / Device Info HW / 3D game → GPU
  renderer tab shows `Adreno 750/830` or `Mali-G715` as configured.
- [ ] **V-4 (SELinux):** `adb logcat | grep -i avc` → no Dobby trampoline
  blocks on the target ROM.
- [ ] **V-5 (Safety rollback):** inject a broken `config.json` → reboot →
  module falls back to `.backup.json` (no bootloop).
- [ ] **V-6 (Stealth):** unconfigured app → `maps` does not list
  `openspoof`; `getprop` returns real values.
- [ ] **V-7 (No trace on unconfigured app):** `am force-stop` and
  re-launch → no `OpenSpoof` log lines for that package.

## 6. Known limitations (carried over)

These will not be fixed in the OpenSpoof codebase — they are scope
boundaries, not bugs. See [SPEC.md](SPEC.md) § Non-goals for the
authoritative list. Quick recap:

- TEE attestation, hardware-backed Widevine L1 — not reachable.
- Native property reads that bypass `__system_property_get` (custom
  libc hooks) — not in scope.
- Server-side anti-cheat clustering — out of scope by definition.
