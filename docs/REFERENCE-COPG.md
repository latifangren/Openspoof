# COPG Reference Audit

This document records what the OpenSpoof maintainer has personally
observed by extracting `COPG-v7.5.1.zip` and inspecting it with file
inventory and ASCII string scans only. It complements the public
`latifangren/SkySpoof/docs/REA-ANALYSIS.md` REA, which used REA/Ghidra
on the same release. This file intentionally stops short of full
binary disassembly, decryption, or runtime tracing.

## Provenance

- **Artifact:** `COPG-v7.5.1.zip` from the public COPG GitHub release
  (downloaded by the maintainer for personal use).
- **Method:** SHA-256 hash check, file inventory, ASCII string scan
  via PowerShell regex (no `strings(1)` available in the sandbox).
  No RE/decompile, no patch, no license bypass, no runtime test.
- **Authorization:** the maintainer (latifangren) is the author of
  OpenSpoof and has the right to inspect this artifact for design
  comparison purposes.

## SHA-256 cross-check vs REA

The four primary artifacts analyzed by the public REA all hash-match
the binaries in `COPG-v7.5.1.zip`, confirming the inspected release
is the same one the REA covered:

| Artifact | SHA-256 (lower) | Match REA? |
|---|---|---|
| `zygisk/arm64-v8a.so` | `0a85a837ce61a779a2281ae9102c3e62a4cc5da804e42676693f96ff7498595d` | yes |
| `copg_lic_arm64` | `86387f8152ab5aa76eb770ad5be6553d54c453fb4d82523b194912255c451c6e` | yes |
| `wv.dex` | `4c7431592b426a0eabc3ed0d818a48173352daa2367ade64fc47972a8eb66e5b` | yes |
| `userprobe.dex` | `8de7ba4d571998af5417e3c67c70e85668245db2a324d69cc9f1c47d187416d3` | yes |

## File inventory beyond the REA

`COPG-v7.5.1.zip` contains a richer set of binaries than the REA
discussed. The REA focused on the four primary artifacts above. The
following files exist in the same zip and were not analyzed in the
public REA — they are listed here for completeness.

| Path | Size (bytes) | Notes |
|---|---|---|
| `zygisk/arm64-v8a.so` | 10,457,376 | Main Zygisk module (REA target) |
| `zygisk/armeabi-v7a.so` | 1,355,828 | 32-bit Zygisk fallback |
| `controller_arm64` | 153,012 | Background controller daemon (arm64) |
| `controller_armv7` | n/a | Background controller daemon (armv7) |
| `copg_lic_arm64` | ~40 KB | UPX-compressed licensing helper (REA target) |
| `copg_lic_armv7` | n/a | UPX-compressed licensing helper (armv7) |
| `micshare/libmicshare_arm64.so` | 14,432 | Concurrent Mic shim (arm64) |
| `micshare/libmicshare_armv7.so` | n/a | Concurrent Mic shim (armv7) |
| `micshare/mapinject_arm64` | n/a | Memory-map injector (arm64) |
| `micshare/mapinject_armv7` | n/a | Memory-map injector (armv7) |
| `proxy/copg_proxy.sh` | n/a | TPROXY setup script |
| `proxy/tproxy.sh` | n/a | iptables/nftables helper |
| `proxy/manifest.json` | n/a | Proxy route configuration |
| `CPU/cpuinfo_*` | various | 11 CPU sample files (manifest) |
| `CPU/manifest.json` | n/a | CPU profile manifest |
| `GPU/manifest.json` | n/a | GPU profile manifest (Adreno 830/840) |
| `SIM/manifest.json` | n/a | SIM/carrier profile manifest (140 carriers) |
| `UA/manifest.json` | n/a | User-Agent profile manifest |
| `common/repo.yaml` | n/a | Repository metadata |
| `webroot/` | 30+ files | Multi-page WebUI: index.html, 13 CSS, 25 JS, 9 langs |
| `wv.dex` | n/a | Plaintext Widevine probe DEX (REA target) |
| `userprobe.dex` | n/a | Plaintext user/package probe DEX (REA target) |
| `COPG.json`, `config.json`, `global.json`, `list.json` | n/a | Configuration |
| `customize.sh`, `service.sh`, `uninstall.sh`, `sepolicy.rule`, `module.prop` | n/a | Magisk plumbing |

## Sentinel-file pattern (new observation)

The arm64 Zygisk object references a family of files under
`/data/system/` used as **state sentinels and boot guards** for the
advanced spoofs. OpenSpoof may want to study this approach; it is
similar in spirit to the OpenSpoof `.disable_openspoof` sentinel but
more granular.

```
/data/system/copg_hb                  (heartbeat, system-server live)
/data/system/copg_ss_hb               (heartbeat, system-server layer)
/data/adb/.copgts                     (timestamp/state)
/data/adb/COPG/served.jsonl           (append-only event log)
/data/adb/COPG/license/license.bin     (encrypted license blob)
/data/adb/modules/COPG/wv.dex          (Widevine probe, plaintext)
/data/system/copg_account.txt         (Google Account spoof state)
/data/system/copg_account_bootguard    (auto-disable after N boot crashes)
/data/system/copg_corepatch.txt       (Install Patches state — signature bypass)
/data/system/copg_corepatch_bootguard
/data/system/copg_flagsecure.txt       (Disable FLAG_SECURE state)
/data/system/copg_fs_bootguard         (No Storage Restrict boot guard)
/data/system/copg_lang.txt             (per-app language state)
/data/system/copg_lang_bootguard
/data/system/copg_pairip.txt           (PairIP bypass state)
/data/system/copg_playsrc.txt          (Fake Play install source state)
/data/system/copg_playsrc_bootguard
/data/system/copg_micshare             (Concurrent Mic socket path)
```

Each pair (`<feature>.txt` + `<feature>_bootguard`) implements the
"auto-recovery for system-level hooks" pattern described in
`changelog.md` v6.6.0: a feature writes its state file when armed;
on the next boot, the module checks whether the previous boot
crashed while the feature was on. After a threshold of crashes the
feature auto-disables to break the boot loop. OpenSpoof's
`module/service.sh` has a simpler version of this for the whole
module (`.disable_openspoof`).

## Class names visible in arm32 (new observations)

The 32-bit `zygisk/armeabi-v7a.so` exposes class references that the
64-bit build did not surface in the strings sampled here. The most
interesting for gap analysis:

```
com.copg.rr.RrHook          -- Refresh Rate hook
com.copg.vpnapp.VpnAppHook  -- Hide VPN (in-app aggressive mode)
COPGModule                  -- module class name
COPG-src-v1                  -- internal version string
COPG-wrap                    -- obfuscation marker
__loader_dlopen              -- custom dlopen wrapper (anti-detection)
__loader_dlclose
__loader_android_dlopen_ext
__dl__Z8__dlopenPKciPKv     -- mangled __dl_dlopen symbol
```

The dlopen wrapper symbols suggest COPG uses an inline `dlopen`
patch to hide the names of the libraries it loads at runtime — an
anti-detection technique.

## Library set used by `micshare/libmicshare_arm64.so`

```
libaudioflinger             -- Android audio HAL server (lowest level)
libaudiopolicy              -- audio policy routing
libaudiopolicymanager       -- policy manager service
libaudiopolicyservice       -- policy decision service
libaudiopolicycomponents    -- component registration
```

This confirms the changelog v7.5.1 description of the **Concurrent
Mic** fix: COPG's micshare component injects into `libaudioflinger`
to register a second audio client alongside the first, so two apps
can hold the microphone simultaneously. It is a low-level audio
HAL hook, not an ART method hook.

## What this audit did not do

- No decompilation of the UPX-compressed `copg_lic_*` binaries
  (would require unpacking first; license-circumvention territory).
- No decompilation of `controller_arm*` (also UPX-compressed and
  contains license state).
- No RE of the encrypted paid DEX that the loader fetches from
  `license/license.bin` (the "InMemoryDexClassLoader" string seen
  in the REA).
- No runtime test of any feature on a real device.
- No attempt to load or run any of the binaries.

These omissions are deliberate: they keep the audit in the same
evidence-led posture as the public REA, and they avoid touching
the licensing layer (which is the part of COPG that is
deliberately closed-source and revenue-bearing).

## Implications for OpenSpoof design

Concrete ideas this audit suggests, listed in rough order of effort:

1. **Per-feature sentinel files** instead of a single global one.
   Lets a feature fail-closed independently, so one bad config
   cannot take the whole module down. The OpenSpoof safety daemon
   would need only a small extension to support this.
2. **Boot-guard counter**: count consecutive boot failures with a
   feature armed, and auto-disable after a threshold. Currently
   OpenSpoof's `service.sh` only honors an explicit
   `.disable_openspoof` file. A boot-guard would be a defensive
   addition for the more invasive Phase 4 features.
3. **`__dl_dlopen` wrapper** as a Phase 5 idea. OpenSpoof's
   `src/hooks.cpp` currently uses ordinary `dlopen`/`dlsym`. Wrapping
   those in a custom hook would reduce detection surface in apps
   that scan the dynamic linker.
4. **Audio HAL hooks for the Concurrent Mic use case.** This is
   out of scope for OpenSpoof's stated goals (per-app device
   spoofing, not audio routing) but is the kind of feature a future
   "voice chat while recording" use case would need.
5. **License server is the missing piece COPG has and OpenSpoof
   deliberately does not.** This is by design and not a gap.

## Adoption from COPG-VD v5.2.1 (Apache-2.0)

Unlike the COPG v7.5.1 audit above, COPG-VD v5.2.1 is licensed
**Apache License 2.0** and ships its **source code in the public
repository** (no proprietary DEX payload, no license server).
That makes direct adoption possible and intended: the maintainer
can copy design and code fragments with attribution.

What was adopted (and where it lives in OpenSpoof):

| Design | Source | OpenSpoof file | Notes |
|---|---|---|---|
| `VersionPolicy { Never, Rom, Force }` enum + clamp logic | [spoof_module.cpp:29-105](https://github.com/VD171/COPG-VD/blob/main/zygisk/spoof_module.cpp) | [src/config.h](src/config.h) `VersionPolicy`, [src/spoof.cpp](src/spoof.cpp) `versionAllowed()` | The guard against raising `SDK_INT` above the ROM's true level — without this, a config typo can softloop the device. |
| 17-field Build coverage (BRAND..SERIAL plus BOOTLOADER, HOST, ODM_SKU, SKU, TAGS, TYPE, USER) + Build.TIME (long) | [spoof_module.cpp:130-180](https://github.com/VD171/COPG-VD/blob/main/zygisk/spoof_module.cpp) | [src/spoof.cpp](src/spoof.cpp) `spoofBuild()` | The original OpenSpoof only set 11 of 17 Build fields. The added six close a real detection vector. |
| `releaseOrCodename` AOSP rule (RELEASE_OR_CODENAME = "REL" == CODENAME ? RELEASE : CODENAME) | [spoof_module.cpp:107-112](https://github.com/VD171/COPG-VD/blob/main/zygisk/spoof_module.cpp) | [src/spoof.cpp](src/spoof.cpp) `releaseOrCodename()` | Prevents a fingerprint that publishes "REL" in the version slot, which no real device reports. |
| Defensive `getFieldOrNull` pattern (clear exception on missing field) | [spoof_module.cpp:130-134](https://github.com/VD171/COPG-VD/blob/main/zygisk/spoof_module.cpp) | Already used in OpenSpoof; consistent application | The original OpenSpoof also checks exceptions, but not for every field. |
| `readRomVersionFromBuildProp()` (read /system/build.prop instead of system property) | [spoof_module.cpp:75-94](https://github.com/VD171/COPG-VD/blob/main/zygisk/spoof_module.cpp) | [src/spoof.cpp](src/spoof.cpp) `readRomVersionFromBuildProp()` | Survives a poisoned system property because /system/build.prop is on the read-only mount. |

What was **not** adopted and why:

- **nlohmann/json single-header** (979 KB). Apache-2.0 / MIT
  compatible, but OpenSpoof's hand-rolled parser is 300 lines and
  does not need a full JSON library. Worth swapping only if the
  config schema grows significantly.
- **`__cxa_atexit` polyfill** (`atexit.cpp`, AOSP 2020 copyright,
  BSD-3-Clause). The host NDK already provides it. Only useful
  for the older AOSP builds that the polyfill targets.
- **i18n / 21 languages** (140 KB of JSON). Out of scope for the
  current commit but a worthwhile follow-up.
- **`CMakeLists.txt`** (645 bytes). Could replace the direct
  `clang++` invocation in `scripts/build.sh`, but the current
  script is simpler and works.
- **The `service.sh` / `fingerprint-update.sh` patterns** (57 KB
  combined). COPG-VD's service.sh is device-wide (one device for
  all apps); OpenSpoof is per-app, so the patterns do not transfer
  directly.

Apache 2.0 attribution requirement (per License §4): the copyright
notice and a copy of the License are present in this file (in
the audit table above) and in [docs/CHANGELOG.md](CHANGELOG.md).
The "Statement of Changes" lives in the same files. Modifications
to the adopted code are described in the commit messages of the
relevant commits; the `CHANGELOG.md` entry for this adoption
points to the commits explicitly.

## Companion documents

- [SPEC.md](SPEC.md) — OpenSpoof contract and architecture.
- [ROADMAP.md](ROADMAP.md) — feature status and gap matrix vs COPG.
- The public REA at
  `https://raw.githubusercontent.com/latifangren/SkySpoof/refs/heads/main/docs/REA-ANALYSIS.md`
  is the prior, more thorough analysis this audit leans on. Its
  evidence levels ("confirmed static evidence", "static indication",
  "project claim", "not established") are used as-is here.
