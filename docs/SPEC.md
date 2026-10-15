# OpenSpoof — Spec

**Status:** draft v0.2.0
**Scope:** open-source, license-free Zygisk module for **per-app
device-profile spoofing + CPU spoofing** on rooted Android. The core
value proposition of COPG, without the PRO / paid parts.

> Design notes on layering and channel ownership are partly inspired
> by [guise/docs/ARCHITECTURE.md](https://github.com/eternalcherryblossoms/guise/blob/HEAD/docs/ARCHITECTURE.md)
> (open source). Zygisk template: [topjohnwu/zygisk-module-sample](https://github.com/topjohnwu/zygisk-module-sample).

---

## 1. Goals

- Make a game / app believe it runs on a different flagship
  (brand, model, fingerprint, SDK) — unlock GPU tier / high-FPS that
  is hardware-gated on the real device.
- **Per-app:** only configured packages are touched. System and
  other apps see the real values.
- No reboot to change config: relaunch the target app.
- 100% free, open source, no license server.

## 2. Non-goals (honest up front)

- **Not** an anti-ban tool. No "anti-cheat safe" claim is made.
- TEE is not reachable: key attestation and Widevine L1 cannot be
  spoofed (keys live in the secure world).
- No IMEI / SIM / GPS / sensor spoofing in the MVP (stretch goals,
  see [ROADMAP.md](ROADMAP.md)).
- Native code that reads `__system_property_*` directly (bypassing
  Java) is not covered by Phase 1; it is covered by Phase 4 hooks
  when configured. The scope of the unhooked path is documented
  honestly in [ROADMAP.md](ROADMAP.md), not hidden.

## 3. Architecture

Three layers, partitioned by **channel ownership** (one channel =
one owner, so no two layers overwrite each other's values):

```
┌─────────────┐
│   :magisk    │  delivery only: host .so Zygisk, install/uninstall,
└──────┬──────┘  no spoof logic
       │  /data/adb/modules/openspoof/config.json  (single source of truth)
┌──────▼──────┐
│   :zygisk    │  per-PROCESS, inside the target app:
│  (native)    │   · F1: overwrite android.os.Build static fields via JNI
│              │           (preAppSpecialize)
│              │   · F2: bind-mount a fake /proc/cpuinfo via
│              │           mount-namespace (still root)
│              │   · F4: Dobby native hooks (uptime, uname, props,
│              │           GPU, MAC, telephony, DRM)
│              │   · unconfigured package -> DLCLOSE_MODULE_LIBRARY
└──────────────┘
       │
┌──────▼──────┐
│   :webui     │  Phase 3: on-device config editor (vanilla JS,
│              │  KSU/APatch WebUI bridge)
└──────────────┘
```

| Channel             | Owner      | Technique                                   |
|---------------------|------------|---------------------------------------------|
| `Build.*`, `Build.VERSION.*` | `:zygisk` (F1) | JNI `SetStaticObjectField` / `SetStaticIntField` |
| `/proc/cpuinfo`     | `:zygisk` (F2) | `unshare(CLONE_NEWNS)` + bind-mount, per-process |
| Native properties   | `:zygisk` (F4) | Dobby inline hooks                         |
| GPU strings         | `:zygisk` (F4) | Dobby hooks on `glGetString`, `eglQueryString`, `vkGetPhysicalDeviceProperties` |
| Config transport    | file JSON  | read on every `preAppSpecialize`, no daemon |

No daemon is used (unlike COPG's `controller`): for the MVP, re-reading
config on each process spawn is enough — no live state to maintain.
Relaunch the app = new config applies. A daemon would only be needed
if device-wide toggles are added later.

## 4. Phases

| Phase | Scope                                          | Status                                                |
|-------|------------------------------------------------|-------------------------------------------------------|
| F1    | JNI spoof `Build` + `Build.VERSION` per-app, config JSON, packaging, CI | code done, syntax-check OK, awaiting device validation |
| F2    | `/proc/cpuinfo` per-process via mount-NS + cpuinfo profile files | code done, awaiting device validation (`mount()` SELinux) |
| F3    | WebUI: app list, profile picker, atomic config save | code done, awaiting WebUI bridge test               |
| F4    | Native hooks (Dobby): fake uptime, uname, prop spoof, getprop consistency, GPU | code done, Dobby vendored; remaining: build `libdobby.a` + device test |
| F5 (stretch) | ART/Java method hooks (Android ID, IMEI, UA, sensor…) | design draft only |

**Final completion criterion for every phase: device-root test** — see
[ROADMAP.md](ROADMAP.md) § Verification checklist.

## 5. F1 technical detail (MVP core)

- **Hook point:** `preAppSpecialize(AppSpecializeArgs*)`. `args->nice_name`
  is the package name (for the app's main process). It runs **before**
  the app's code — `Build` fields are overwritten before anyone reads
  them.
- **No inline-hook engine needed** (Dobby / v7878 / etc). `android.os.Build`
  fields are `public static`, so plain JNI is enough:

  ```cpp
  jclass build = env->FindClass("android/os/Build");
  jfieldID fid = env->GetStaticFieldID(build, "MODEL", "Ljava/lang/String;");
  env->SetStaticObjectField(build, fid, env->NewStringUTF("NX769J"));
  ```

- Fields spoofed (all `String` except `SDK_INT`): `BRAND, DEVICE,
  MANUFACTURER, MODEL, PRODUCT, BOARD, HARDWARE, FINGERPRINT, ID,
  DISPLAY, SERIAL`, plus `Build.VERSION`: `RELEASE, INCREMENTAL,
  CODENAME, SECURITY_PATCH, SDK_INT (int)`.
- **Stealth:** unconfigured package → `api->setOption(DLCLOSE_MODULE_LIBRARY)`
  so the `.so` is unloaded; no trace in that process's `maps`.
- **Zero-dependency config parser:** minimal hand-rolled JSON
  (`src/config.h`, ~330 lines, only the subset the schema uses) —
  no nlohmann, no Abseil.

## 6. F2 technical detail

- `preAppSpecialize` runs while the process is still root (before UID
  drop) → `unshare(CLONE_NEWNS)` + `mount(MS_BIND)` are allowed here.
- Order: `unshare` → `mount("none","/",MS_PRIVATE|MS_REC)` (do not leak
  mounts to the system) → bind-mount
  `<module>/cpuinfo/cpuinfo_<profile>` to `/proc/cpuinfo`.
- The app does an ordinary `open()` → gets the fake file. **No hook
  is detectable**, no on-disk file is changed.
- SELinux note: validated on the target device. If `mount` is denied,
  fall back is documented, not silently bypassed.

## 7. Config schema (`module/config.json`)

```json
{
  "profiles": {
    "redmagic_9_pro": {
      "BRAND": "nubia", "DEVICE": "NX769J", "MANUFACTURER": "ZTE",
      "MODEL": "NX769J", "PRODUCT": "NX769J", "BOARD": "pineapple",
      "HARDWARE": "qcom",
      "FINGERPRINT": "nubia/NX769J/NX769J:14/UKQ1.230917.001/20240813.173312:user/release-keys",
      "ID": "UKQ1.230917.001", "DISPLAY": "NX769J_UNCommon_9.0.13",
      "RELEASE": "14", "SDK_INT": 34,
      "INCREMENTAL": "20240813.173312", "SECURITY_PATCH": "2024-08-01"
    }
  },
  "apps": { "com.tencent.tmgp.sgame": "redmagic_9_pro" }
}
```

One config blob read by all layers → profiles cannot "drift" between
channels. Optional `native_hooks` sub-object enables Phase 4 features
per profile (see [PHASE4.md](PHASE4.md) for the schema).

## 8. Repo layout

```
openspoof/
├── README.md
├── LICENSE
├── docs/
│   ├── INDEX.md
│   ├── SPEC.md
│   ├── ROADMAP.md
│   ├── PHASE4.md
│   └── CHANGELOG.md
├── module/                      # zip contents
│   ├── module.prop
│   ├── customize.sh
│   ├── uninstall.sh
│   ├── service.sh               # boot-time safety daemon
│   ├── config.json
│   ├── config.default.json      # template, copied to config.json on first boot
│   ├── devices.conf             # device catalog for the WebUI
│   ├── sepolicy.rule            # template SELinux allowance
│   ├── cpuinfo/                 # F2 cpuinfo_<profile> samples
│   ├── webroot/index.html
│   ├── zygisk/{arm64-v8a,armeabi-v7a,x86_64}.so
│   └── META-INF/com/google/android/{update-binary,updater-script}
├── src/
│   ├── spoof.cpp                # Zygisk module (F1+F2+F4 wiring, Phase B+C glue)
│   ├── hooks.h / hooks.cpp      # F4 native hooks (Dobby)
│   └── config.h                 # minimal JSON parser + Config/Profile/NativeHooks
├── scripts/
│   ├── build.sh                 # NDK build + package (Linux/macOS/WSL)
│   ├── build.ps1                # NDK build + package (Windows PowerShell)
│   └── build_dobby.sh           # Dobby static library
└── .github/workflows/
    ├── build.yml                # multi-ABI build on push / tag
    └── release.yml              # release on tag v*
```

`zygisk.hpp` is not vendored — it is fetched at build time from
`topjohnwu/zygisk-module-sample` (canonical, Magisk guarantees
backward compat).

## 9. Build & CI

- Local: `scripts/build.sh` requires `ANDROID_NDK_HOME` (NDK r21+).
  `aarch64-linux-android21-clang++ -std=c++17 -shared -fPIC -O2 -I src
  src/spoof.cpp src/hooks.cpp -llog -o module/zygisk/<abi>.so`.
- CI: GitHub Actions, NDK setup, build 3 ABIs, zip
  `openspoof-vX.Y.Z.zip`, upload artifact; release on tag `v*`.
- Packaging includes **META-INF** (lesson from the BFR-WEBUI-GO case:
  without it, install fails on Magisk < 29).

## 10. Testing plan (requires rooted device)

1. Flash zip in Magisk / KernelSU / APatch → reboot.
2. `adb logcat -s OpenSpoof` — confirm `spoofed <pkg> as profile <key>`
   for configured packages, and **no** log line for unconfigured ones.
3. Any free "Device Info" app (Play Store): check that Model and
   Fingerprint match the configured profile for the target package,
   and stay real for other apps.
4. (F2) Inside the target app: read `/proc/cpuinfo` (via `run-as` or a
   test app) → matches the profile; outside the app → real values.
5. Anti-regression: normal boot, no bootloop. A broken config must
   cause the module to skip, not crash — parse failure = no-op + log.

## 11. Risks & limits

- Games with strong anti-cheat (kernel-level or server-side
  clustering) can still detect inconsistencies — this is a
  spoofing tool, not an anti-cheat bypass.
- OEM ROMs and Android versions vary → some `Build` fields may not
  exist. The field list is defensive: a missing field is a log
  warning, not a crash.
- Without a physical rooted device, F1–F2 cannot be end-to-end
  validated here.
- Phase 4 native hooks leave detectable artefacts (trampolines,
  `mprotect` on `.text`). See [PHASE4.md](PHASE4.md) § Risks.
