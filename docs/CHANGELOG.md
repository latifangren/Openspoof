# Changelog

All notable changes to OpenSpoof are recorded here. The format
follows [Keep a Changelog](https://keepachangelog.com/en/1.1.0/),
and the project does not yet adhere to Semantic Versioning — see
[ROADMAP.md](ROADMAP.md) for the release plan.

> **Status convention:** entries marked **Unreleased** are in the
> working tree but not in a tagged release. Each release entry
> represents the state of the repo at the corresponding tag.

## [Unreleased]

### Added
- `docs/REFERENCE-COPG.md` — maintainer's static audit of
  `COPG-v7.5.1.zip` (SHA-256 confirmed against the public REA at
  `latifangren/SkySpoof/docs/REA-ANALYSIS.md`). Documents the
  per-feature sentinel-file pattern, `libmicshare`'s
  `libaudioflinger` hook for Concurrent Mic, and the
  `__dl_dlopen` wrapper used as an anti-detection trick.
- `docs/INDEX.md` — single document map; replaces ad-hoc cross-links.
- `docs/CHANGELOG.md` — this file.
- `LICENSE` — MIT license (project was previously unlicensed despite the
  "open source" claim).
- `module/sepolicy_helper.sh` — runtime SELinux policy helper for
  Phase 4 (Dobby) hooks. Detects Magisk / KernelSU / APatch policy
  tools and applies the `execmem` allowance best-effort. Invoked from
  `module/service.sh` at boot. Closes audit K4.
- `tests/run.sh` + `tests/test_sepolicy_helper.sh` — first test
  coverage in the project; six scenarios for the SELinux helper.
  CI runs them on every push.
- Phase B / C / D implementations: Android ID, Telephony, MAC,
  `MediaDrm` level, GAID, cell LAC/CID, GPU spoofing presets.
- `module/devices.conf` — device catalog (5 presets).
- `module/service.sh` — boot-time safety daemon with backup
  rollback and `.disable_openspoof` sentinel.
- `scripts/build.ps1` — Windows NDK build script (PowerShell 5.1+).

### Changed
- `README.md` rewritten as a short pointer to `docs/INDEX.md`.
- `SPEC.md` moved from repo root to `docs/SPEC.md`; non-goals
  clarified; "in Indonesian" remark removed (the spec is in
  English since v0.2.0).
- `ROADMAP.md` moved from repo root to `docs/ROADMAP.md`; honest
  status per phase; explicit verification checklist.
- `PHASE4.md` trimmed to focus on Dobby engine details; scope
  cross-references moved to `SPEC.md` / `ROADMAP.md`.
- `src/hooks.cpp` — `fake_eglQueryString` no longer has duplicate
  `EGL_VENDOR` branch; `fake_vkGetPhysicalDeviceProperties`
  patches the spec-defined field offsets instead of a
  re-declared struct (avoids potential out-of-bounds write on
  Vulkan struct size mismatches).
- `scripts/build.sh` — Windows cross-compiler path uses `.exe`
  suffix and checks that the compiler exists before invoking.

### Fixed
- `fake_eglQueryString` returned a dead branch for `EGL_VENDOR`
  when `g_egl_vendor` was empty (K1 in the audit).
- `fake_vkGetPhysicalDeviceProperties` wrote to a local struct
  that may not match the real `VkPhysicalDeviceProperties` size
  (S3 in the audit).
- `scripts/build.sh` Windows host path used the wrong `.cmd`
  suffix for cross-compilers (K5 in the audit).

### Documentation
- Status claims across docs reconciled: every "selesai" / "done"
  in `ROADMAP.md` now explicitly means "code present", not
  "validated on device". The verification checklist in
  `ROADMAP.md` § 5 is the gate for v1.0.0.

### Added (post-REFERENCE-COPG.md)
- **CI maturity pass.** New `release.yml` workflow that publishes
  a GitHub Release on tag push, extracting notes from
  `docs/CHANGELOG.md`. `build.yml` now caches the NDK install,
  runs shellcheck on every shipped shell script, runs a
  host-clang `-fsyntax-only` pass on the C++ sources with the
  NDK sysroot include path, and runs the test runner before
  the build. `scripts/build_dobby.sh` now builds Dobby for all
  three shipped ABIs (arm64-v8a, armeabi-v7a, x86_64) instead
  of just arm64.
- **Device and CPU catalogs.** `module/devices.conf` extended
  from 5 to 18 device entries (added oneplus_12, xiaomi_14_pro,
  iqoo_12_pro, redmagic_10_pro, honor_magic7_pro,
  pixel_9_pro_xl, pixel_9, pixel_8a, pixel_7_pro,
  galaxy_s25_ultra, galaxy_s24_plus). New
  `module/cpu_profiles.conf` with 11 SoCs. New
  `module/cpuinfo/cpuinfo_sd8elite` (Snapdragon 8 Elite sample
  for the F2 bind-mount path). WebUI now iterates
  `DEVICE_PRESETS` and the new `CPU_PRESETS` map at render
  time, so adding a new entry is a JS-only edit.
- **GAP #16 — display refresh rate.** New
  `NativeHooks::refreshRateHz` (validated 30..480 Hz) plus
  the JSON parser entry `native_hooks.refresh_rate_hz`. At
  runtime the value is seeded into
  `persist.sys.display.refresh_rate` and
  `ro.surface_flinger.refresh_rate` so the per-app Display /
  SurfaceFlinger path reports the spoofed Hz.
- **GAP #10 — per-app language.** New
  `NativeHooks::languageTag` (BCP-47, validated for non-space
  and length). Property layer seeds
  `persist.sys.locale`, `ro.product.locale`, and the legacy
  `ro.product.locale.language` form. Phase 5 layer (see
  below) additionally calls `Locale.setDefault` for in-process
  readers.
- **GAP #7 — Disable FLAG_SECURE.** New
  `NativeHooks::disableFlagSecure` (boolean). Phase 5 layer
  replaces `android.view.Window.setFlags` via JNI
  `RegisterNatives`, so FLAG_SECURE on a target app's window
  is dropped at runtime. With Phase 5 the GAP moves from
  "sentinel only" to "real effect".
- **Phase 5 ART hooks.** New `src/f5_art_hooks.cpp` with two
  JNI-only helpers (no Dobby, no execmem, no SELinux work
  needed):
  `InstallFlagSecureHookF5(JNIEnv*)` and
  `ApplyLocaleOverrideF5(JNIEnv*, const std::string& tag)`.
  Both clear pending JNI exceptions and return false on
  failure (class not loaded, register failed), so a
  misconfiguration never crashes the app.
- **Tests.** New `tests/test_config_parser.cpp` (C++ unit test
  for the JSON parser: refresh_rate_hz in-range and
  out-of-range, disable_flag_secure default and explicit,
  language with and without space, combined profile with
  all three new features). New `tests/test_f5_link.cpp`
  (symbol-presence check for the two public F5 entry
  points). `tests/run.sh` now discovers and builds
  `tests/test_*.cpp` files with host clang++; it skips with
  a clear notice if the compiler is not on PATH.

### Fixed
- Build script: the multi-ABI Dobby build now produces
  `libdobby.a` for all three ABIs, fixing the
  armeabi-v7a and x86_64 link failures that would have
  surfaced on first non-arm64 packaging.

## [v0.1.0] — initial skeleton

- Phase 1: JNI `Build` field spoofing.
- Phase 2: mount-namespace `/proc/cpuinfo` spoofing.
- Phase 3: on-device WebUI editor.
- Phase 4: Dobby native hooks (uptime, uname, `__system_property_get`).
- Single config file: `module/config.json`.
- Linux/macOS build script: `scripts/build.sh`.

[Unreleased]: #unreleased
[v0.1.0]: #v010--initial-skeleton
