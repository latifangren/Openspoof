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

## [v0.1.0] — initial skeleton

- Phase 1: JNI `Build` field spoofing.
- Phase 2: mount-namespace `/proc/cpuinfo` spoofing.
- Phase 3: on-device WebUI editor.
- Phase 4: Dobby native hooks (uptime, uname, `__system_property_get`).
- Single config file: `module/config.json`.
- Linux/macOS build script: `scripts/build.sh`.

[Unreleased]: #unreleased
[v0.1.0]: #v010--initial-skeleton
