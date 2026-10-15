# OpenSpoof — Master Roadmap & Gap Analysis

**Status Dokumen:** Living Document · v0.2.0-draft  
**Basis Analisis:** OpenSpoof Source, SkySpoof v0.3.0, COPG Pro v7.5.1 REA Static Teardown (`docs/REA-ANALYSIS.md`), COPG-VD.

---

## 1. Status Proyek Saat Ini (Baseline v0.1.0)

OpenSpoof dirancang sebagai alternatif open-source dan bebas lisensi untuk device profile spoofing per-aplikasi. Posisi implementasi saat ini di tree:

| Komponen | Status Implementasi | File Sumber | Keterangan & Kesiapan |
|---|---|---|---|
| **Phase 1: Java Build Fields** | ✅ Selesai (Skeleton) | `src/spoof.cpp` | Override field `Build.*` & `Build.VERSION.*` via JNI di `preAppSpecialize`. Belum uji live di device root fisik. |
| **Phase 2: CPU VFS Spoof** | ✅ Selesai (Skeleton) | `src/spoof.cpp`, `module/cpuinfo/` | `unshare(CLONE_NEWNS)` + `mount(MS_BIND)` `/proc/cpuinfo` per-proses (masih root). Contoh profil RedMagic 9 Pro (SD 8 Gen 3) siap. |
| **Phase 3: WebUI Manager** | ✅ Selesai (MVP) | `webroot/index.html` | Antarmuka gelap modern, filter app, autocomplete paket via `pm`, editor profil, simpan atomic config via bridge root (KernelSU / APatch / Magisk). |
| **Phase 4: Native Hooks (Dobby)** | ✅ Diimplementasi | `src/hooks.cpp`, `third_party/dobby/` | Dobby di-vendor. Hook untuk `clock_gettime` (uptime), `uname`, `__system_property_get`, dan **GPU Spoofing** (`glGetString`, `eglQueryString`, `eglGetProcAddress`, `vkGetPhysicalDeviceProperties`). |
| **PRO: GPU Spoofing Engine** | ✅ Selesai (Phase A) | `src/hooks.cpp`, `src/config.h` | Intercept GL_RENDERER, GL_VENDOR, EGL strings & Vulkan device name. Presets Adreno 750/830 & Mali-G715. |
| **PRO: Hardware & Telephony** | ✅ Selesai (Phase B) | `src/spoof.cpp`, `src/hooks.cpp` | Android ID (`Settings.Secure` cache), IMEI, IMSI, ICCID, Operator numeric/alpha, hardware MAC address (`ioctl`). |
| **PRO: DRM & Location Engine** | ✅ Selesai (Phase C) | `src/spoof.cpp`, `src/hooks.cpp` | MediaDrm (`securityLevel` L1/L3, systemId, vendor via `RegisterNatives`), Geolocation cell LAC/CID, GAID. |
| **Device Catalog** | ✅ Selesai | `module/devices.conf` | Katalog preset flagship: RedMagic 9 Pro, ROG Phone 8 Pro, Galaxy S24 Ultra, Pixel 8/9 Pro. |
| **Packaging & Build System** | 🟡 Skrip Bash Dasar | `scripts/build.sh` | Direct Clang NDK script. Menghasilkan module zip untuk arm64-v8a, armeabi-v7a, x86_64. |
| **Safety Daemon** | ✅ Selesai (Phase A) | `module/service.sh` | Boot daemon dengan auto-recovery, backup rollback (`config.backup.json`), dan emergency bypass sentinel (`.disable_openspoof`). |

---

## 2. Gap Analysis Matrix

Perbandingan komprehensif antara **OpenSpoof (Current)**, **SkySpoof (Upstream)**, dan **COPG Pro v7.5.1 (Target Acuan)**:

| Fitur / Dimensi | OpenSpoof (Sekarang) | SkySpoof v0.3.0 | COPG Pro v7.5.1 (Target) | Gap & Tindakan untuk OpenSpoof |
|---|---|---|---|---|
| **Build Fields (`Build.*`)** | `preAppSpecialize` (JNI) | `postAppSpecialize` (JNI) | Native loader + DEX payload | OpenSpoof lebih unggul dalam timing (eksekusi sebelum bytecode app jalan). |
| **CPU Info (`/proc/cpuinfo`)** | Mount NS bind-mount (`CLONE_NEWNS`) | ❌ Tidak ada | ❌ Tidak ada (hanya property) | Keunggulan unik OpenSpoof; game yang baca core & hardware via file open tertipu. |
| **GPU Spoof (GLES/Vulkan)** | ✅ Selesai (Dobby + GLES/EGL/Vulkan) | ❌ Tidak ada (hanya catatan REA) | ✅ `libEGL`, `libGLESv2`, `libvulkan`, `eglQueryString` | **TERTUTUP (Phase A)**: `glGetString`, `eglQueryString`, `eglGetProcAddress`, `vkGetPhysicalDeviceProperties`. |
| **Native Property Hook** | ✅ `__system_property_get` (Dobby) | ❌ Tidak ada | ✅ Native property hook | OpenSpoof sudah terintegrasi dan memetakan GSM properties secara otomatis. |
| **Hardware Identifiers (IMEI/SIM)** | ✅ Selesai (GSM Props + Native) | ❌ Tidak ada | ✅ `ImeiHook`, `SimOpHook` | **TERTUTUP (Phase B)**: IMEI, IMSI, ICCID, Operator mapping ke system properties & MAC address via ioctl. |
| **Android ID (`android_id`)** | ✅ Selesai (`Settings.Secure` Cache) | ❌ Tidak ada | ✅ Terintegrasi | **TERTUTUP (Phase B)**: Injeksi cache static `Settings$NameValueCache` via JNI. |
| **DRM / Widevine Level** | ✅ Selesai (JNI `RegisterNatives`) | ❌ Tidak ada | ✅ `wv.dex` + `DrmHook` (`MediaDrm`) | **TERTUTUP (Phase C)**: `MediaDrm.getPropertyString` (L1/L3, systemId, vendor). |
| **Location & Network (GPS/WiFi/Cell)** | ✅ Selesai (LAC/CID + MAC + GAID) | ❌ Tidak ada | ✅ `GpsHook`, `CellHook` | **TERTUTUP (Phase C)**: Cell tower LAC/CID properties, GAID property, hardware MAC socket hook. |
| **Config Validation & Safety** | Hand-rolled minimal JSON | `skyspoofctl` (allowlist regex) + `service.sh` auto-backup | Komersial daemon | **GAP**: Perlu adopsi mekanisme `service.sh` + backup recovery dari SkySpoof. |
| **Device Catalog** | Hanya 1 preset (RedMagic 9 Pro) | 6 preset (Pixel 7/8/9 Pro / Fold) | Katalog besar cloud/local | **GAP**: Impor format katalog `devices.conf` SkySpoof ke OpenSpoof. |

---

## 3. Total Roadmap Fitur PRO

Roadmap dibagi menjadi 5 fase rilis terarah menuju parity penuh dan melampaui kemampuan COPG Pro:

```
v0.1.0 (Current) ──────▶ v0.2.0 (GPU & Stability) ──────▶ v0.3.0 (Identity & Telephony)
                                                               │
                                                               ▼
v1.0.0 (Production Parity) ◀────── v0.4.0 (DRM, Location & Sensors)
```

### [Phase A / v0.2.0] — Native GPU Spoofing & Safety Engine
*Fokus: Game Graphics Unlock & Ketahanan Sistem.*

1. **Native OpenGL ES Intercept via Dobby (`src/hooks.cpp`)**:
   - Hook `glGetString(GLenum name)`:
     - `GL_RENDERER (0x1F01)`: return custom GPU name (misal: `"Adreno (TM) 750"`, `"Adreno (TM) 830"`, `"Mali-G720 Immortalis"`).
     - `GL_VENDOR (0x1F00)`: return `"Qualcomm"` atau `"ARM"`.
     - `GL_VERSION (0x1F02)`: sinkronkan versi driver OpenGL.
   - Hook `eglQueryString(EGLDisplay dpy, EGLint name)`:
     - `EGL_VENDOR`: timpa vendor EGL.
     - `EGL_VERSION`: timpa version string.
2. **Vulkan Physical Device Spoof**:
   - Intercept fungsi ekspor `vkGetPhysicalDeviceProperties`:
     - Ganti field `deviceName` dan `driverVersion` pada struct `VkPhysicalDeviceProperties`.
3. **Safety & Bootloop Recovery (`module/service.sh`)**:
   - Validasi file `config.json` saat boot sistem.
   - Auto-rollback ke `config.backup.json` jika parsing gagal.
   - Sentinel file `.disable_openspoof` untuk emergency bypass.
4. **Device Catalog Presets**:
   - Integrasi katalog device (`devices.conf`) bawaan SkySpoof + RedMagic 9 Pro + ROG Phone 8 Pro.

### [Phase B / v0.3.0] — Hardware Identifiers & Telephony Engine
*Fokus: Privacy, Per-App Device Isolation, Anti-Fingerprinting.*

1. **Android ID Spoofing**:
   - Intercept `Settings.Secure.getString(ContentResolver, String)` untuk nama `"android_id"`.
   - Mengizinkan custom 16-hex digit per-aplikasi atau mode acak tiap launch.
2. **Telephony & SIM Identity (`ImeiHook` & `SimOpHook`)**:
   - Hook `TelephonyManager.getDeviceId()`, `getImei()`, `getMeid()`.
   - Hook `TelephonyManager.getSubscriberId()` (IMSI) dan `getSimSerialNumber()` (ICCID).
   - Hook `TelephonyManager.getSimOperator()`, `getSimOperatorName()`, `getNetworkOperatorName()`.
   - Implementasi: Kombinasi native hook bionic `property_get` (`gsm.sim.operator.numeric`, dsb.) + Java method hook via lightweight ART trampoline atau helper DEX (`dex_loader`).
3. **MAC Address & Network Spoof**:
   - Hook `WifiInfo.getMacAddress()` dan `NetworkInterface.getHardwareAddress()`.

### [Phase C / v0.4.0] — DRM / Widevine & Location/Sensor Emulation
*Fokus: Media Testing & Geolocation Masking.*

1. **DRM / Widevine Property Spoof (`DrmHook`)**:
   - Probe `MediaDrm` menggunakan helper DEX (`wv_probe.dex`).
   - Hook `android.media.MediaDrm.getPropertyString(String)`:
     - `securityLevel`: paksa laporan `L3` atau `L1`.
     - `vendor`, `version`, `description`, `systemId`.
     - *Catatan batas fisik:* Hook ini memanipulasi API Java report; keybox hardware-backed L1 di dalam TEE tetap tidak bisa digenerate dari udara.
2. **GPS & Location Engine (`GpsHook`)**:
   - Intercept `LocationManager.getLastKnownLocation()` dan callback `LocationListener.onLocationChanged()`.
   - Inject nilai Latitude, Longitude, Altitude, dan Accuracy statis atau jitter halus dari config.
3. **Cell Tower Emulation (`CellHook`)**:
   - Hook `TelephonyManager.getAllCellInfo()` untuk menyuntikkan fake LAC (Location Area Code), CI (Cell Identity), MCC, dan MNC.
4. **Google Advertising ID (GAID) & AppSet ID**:
   - Hook `AdvertisingIdClient$Info.getId()` dan `AppSetIdClient.getAppSetIdInfo()`.

### [Phase D / v1.0.0] — Production Polish, WebUI 2.0 & Automated CI/CD
*Fokus: User Experience, Stabilitas Multi-ROM, Zero Setup.*

1. **CMake + NDK Automated Build**:
   - Build otomatis Dobby sebagai static library `libdobby.a` lintas arsitektur: `arm64-v8a`, `armeabi-v7a`, `x86_64`.
   - Output single zip siap flash di Magisk, KernelSU, dan APatch.
2. **WebUI 2.0 (Full Dashboard)**:
   - Tab "Gaming & GPU": pilihan preset GPU (Adreno 750 / 830, Mali-G720).
   - Tab "Identity & Telephony": generator random IMEI, Android ID, dan SIM operator.
   - Tab "Location": picker peta / koordinat untuk GPS.
   - Visual toggle switch untuk tiap tier hook (on/off per-aplikasi).
3. **SELinux Hardening**:
   - Policy generator otomatis agar rule `sepolicy.rule` tidak memicu audit denial pada Android 13/14/15 modern.

---

## 4. Arsitektur Teknis Eksekusi

```
                      ┌───────────────────────────┐
                      │    Zygote Spawn Process   │
                      └─────────────┬─────────────┘
                                    │
                                    ▼
                      ┌───────────────────────────┐
                      │    onLoad (Zygisk Api)    │
                      └─────────────┬─────────────┘
                                    │
                                    ▼
                      ┌───────────────────────────┐
                      │    preAppSpecialize       │ (UID 0 / Root)
                      │    (Process Match?)       │
                      └──────┬─────────────┬──────┘
                       TIDAK │             │ YA
                             │             ├─▶ [Phase 2] CLONE_NEWNS + bind-mount /proc/cpuinfo
                             │             ├─▶ [Phase 1] JNI Build.* & VERSION.* set static
                             │             ├─▶ [Phase 4] Dobby: __system_property_get, uname, uptime
                             │             └─▶ [PRO Tier] Dobby: glGetString, eglQueryString
                             ▼
              ┌─────────────────────────────┐
              │ DLCLOSE_MODULE_LIBRARY      │
              │ (Zero footprint pada app    │
              │  yang tidak di-target)      │
              └─────────────────────────────┘
```

---

## 5. Checklist Verifikasi & Testing di Real Device

- [ ] **Test G-1 (Build Fields):** Jalankan aplikasi target -> verifikasi via `Build.MODEL` dan `Build.FINGERPRINT`.
- [ ] **Test G-2 (CPU Info):** Jalankan shell dalam target app -> `cat /proc/cpuinfo` harus menghasilkan profil RedMagic/Qualcomm.
- [ ] **Test G-3 (GPU Render):** Buka AIDA64 / Device Info 32 / Game 3D -> periksa tab GPU Renderer (`GL_RENDERER` = Adreno 750/830).
- [ ] **Test G-4 (SELinux Audit):** `adb logcat | grep -i avc` -> pastikan Dobby trampolin tidak diblokir kernel.
- [ ] **Test G-5 (Safety Rollback):** Buat sengaja `config.json` rusak -> reboot -> modul harus fallback ke `.backup.json` tanpa bootloop.
