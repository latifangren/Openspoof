# Phase 4 — Native hooks (Dobby)

**Implementation status (v0.2.0):** code complete to the extent
possible without an NDK-equipped build host and a physical device.
Dobby is vendored at `third_party/dobby`; `src/hooks.cpp` is
syntax-checked against the original Dobby header; the WebUI exposes
a `native_hooks` editor with a risk gate. Remaining: build
`libdobby.a` via CMake + NDK, then run the device checklist in
[ROADMAP.md](ROADMAP.md) § 5.

**Goal:** enable the COPG-PRO-equivalent feature set on the **native
layer** (no Java method hooking) with a single hook engine
integration.

For the contract, non-goals, and where this fits in the overall
phases, see [SPEC.md](SPEC.md). For the per-phase status of F1–F4
and PRO, see [ROADMAP.md](ROADMAP.md).

## 1. Engine choice: Dobby

- **Dobby** — <https://github.com/jmpews/Dobby> — multi-arch
  inline-hook framework (ARM / ARM64 / x86_64), widely used in
  Zygisk modules and game modding, stable API:
  `DobbyHook(addr, replace, &orig)`.
- Considered alternatives: ShadowHook (ByteDance, Android-optimized),
  v7878 (used by COPG — repo URL unverified, rejected).
- Dobby is linked as a **static lib** (`libdobby.a`) into
  `zygisk/*.so` — no extra `.so` shows up in `maps`.

## 2. Coverage

| ID    | Feature                                       | Technique                                                    |
|-------|-----------------------------------------------|--------------------------------------------------------------|
| F4a   | Per-app fake uptime                           | Hook `clock_gettime`; shift `CLOCK_BOOTTIME` / `CLOCK_MONOTONIC` by `uptime_offset_sec`. |
| F4b   | Per-app `uname` spoof                         | Hook `uname()`; overwrite `machine` / `sysname`.             |
| F4c   | Per-app native property spoof                 | Hook `__system_property_get`; return config value for registered keys, forward the rest. |
| F4d   | `getprop` consistency                         | Free from F4c: child processes (`Runtime.exec("getprop")`) are forks of the app process, so they inherit the hooked memory. |
| F4e   | GPU spoof (OpenGL ES / EGL)                   | Hook `glGetString`, `eglQueryString`, `eglGetProcAddress`.   |
| F4f   | Vulkan physical-device properties             | Hook `vkGetPhysicalDeviceProperties`; patch `deviceName` and `vendorID` at the spec-defined offsets. |

**Explicitly NOT in Phase 4** (needs ART / Java method hooking —
planned for Phase 5): Android ID, IMEI / TelephonyManager, WebView
UA, sensor list, GPS / Fused, SIM. Some of these are partially
covered by Phase B/C via `__system_property_get` and direct JNI;
see [ROADMAP.md](ROADMAP.md) for the per-phase status.

## 3. Architecture

```
preAppSpecialize (still root, before app code)
  └─ package match? ── yes ──▶ InstallNativeHooks(profile.nativeHooks)
                               ├─ clock_gettime             → fake_clock_gettime     (F4a)
                               ├─ uname                     → fake_uname             (F4b)
                               ├─ __system_property_get     → fake_property_get      (F4c)
                               ├─ __system_property_read_callback → fake_read_callback (F4c, modern Android)
                               ├─ ioctl (SIOCGIFHWADDR)     → fake_ioctl             (Phase B)
                               ├─ glGetString               → fake_glGetString       (F4e)
                               ├─ eglQueryString            → fake_eglQueryString    (F4e)
                               ├─ eglGetProcAddress         → fake_eglGetProcAddress (F4e)
                               ├─ vkGetPhysicalDeviceProperties → fake_vk_…           (F4f)
                               └─ vkGetInstanceProcAddr     → fake_vk_instance…      (F4f)
                            no ──▶ no-op (safe even without Dobby)
```

- **Per-app gating:** hooks are installed only for packages listed
  in `config.json`. Other packages → `DLCLOSE_MODULE_LIBRARY`,
  same as Phase 1 (zero trace).
- **Without Dobby:** `InstallNativeHooks()` is a no-op + log. The
  build still succeeds; Phase 1–3 features are unaffected.
- **F4c hot path:** `__system_property_get` is called very often.
  The implementation does a single `map.find(name)` and forwards
  to the original on miss. Keep the spoofed key list small.

## 4. SELinux

Inline hooks need anonymous RWX pages (Dobby trampolines) and
`mprotect` on `.text`. The template in `module/sepolicy.rule`
(following the COPG pattern) is:

```
allow untrusted_app untrusted_app process execmem
```

This is a **template** — validate on your device by running
`adb shell dmesg | grep 'avc:' | grep denied` after launching a
configured app, then tighten the domain to what your ROM actually
reports. Without a working rule, `DobbyHook` fails gracefully
(logged, not fatal). On Android 11+ with strict enforcing, you may
also need a broader `appdomain` allowance or apply it at boot via
`magiskpolicy --live` from `module/service.sh`.

## 5. Config schema

```json
"native_hooks": {
  "uptime_offset_sec": 172800,
  "uname": { "machine": "aarch64", "sysname": "Linux" },
  "props": { "ro.product.model": "NX769J", "ro.product.brand": "nubia" },
  "gpu": { "vendor": "Qualcomm", "renderer": "Adreno (TM) 750" },
  "id": { "android_id": "...", "mac_address": "...", "gaid": "..." },
  "telephony": { "imei": "...", "sim_operator": "310260" },
  "drm": { "security_level": "L1" },
  "location": { "latitude": "37.7749", "longitude": "-122.4194" }
}
```

Every field is optional; an empty or absent object → no related
hook is installed.

## 6. Build & vendoring

```bash
git clone https://github.com/jmpews/Dobby third_party/dobby
# build Dobby per its docs (CMake + NDK) to produce libdobby.a
./scripts/build.sh   # auto-adds -DHAVE_DOBBY when third_party/dobby/include exists
```

`zygisk.hpp` is fetched at build time from
`topjohnwu/zygisk-module-sample` (canonical, Magisk guarantees
backward compat).

## 7. Testing (rooted device)

1. `adb logcat -s OpenSpoof` → `hooked clock_gettime/uname/__system_property_get`.
2. F4a: the target app reads uptime (e.g. `SystemClock.elapsedRealtime()`)
   shifted by the offset; other apps read normal values.
3. F4c: in the target app, reading a prop via
   `__system_property_get` (NdkMedia or a small test app) returns
   the configured value.
4. F4d: from the target app, `Runtime.exec("getprop ro.product.model")`
   returns the spoofed value (not the real one).
5. `adb shell dmesg | grep avc` → no denial related to the hook.

## 8. Risks — read first

Inline hooks leave artefacts that **can be detected**: modified
function prologues, anonymous RWX trampolines, `mprotect` on
`.text`. This raises the detection profile vs. Phase 1 (which is
zero-trace for unconfigured packages). Following the COPG pattern,
treat Phase 4 features as **resident / use-at-your-own-risk**, and
do not enable them for games with competitive anti-cheat.
