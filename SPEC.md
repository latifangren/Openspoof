# OpenSpoof — Spec & Struktur MVP

**Status:** draft v0.1.0 · 2026-10-15
**Tujuan:** modul Zygisk open-source & gratis untuk **device profile spoofing + CPU spoofing per-app**
— inti value proposition COPG, tanpa bagian PRO/berbayar.

> Desain layering & channel-ownership di bawah banyak berutang pada
> [guise/docs/ARCHITECTURE.md](https://github.com/eternalcherryblossoms/guise/blob/HEAD/docs/ARCHITECTURE.md)
> (open source). Template Zygisk: [topjohnwu/zygisk-module-sample](https://github.com/topjohnwu/zygisk-module-sample).

---

## 1. Tujuan

- Bikin game/aplikasi mengira berjalan di flagship lain (brand, model, fingerprint, SDK)
  → unlock tier grafik / high-FPS yang di-gate ke hardware tertentu.
- **Per-app**: hanya paket yang dikonfigurasi yang di-sensor; aplikasi lain & system
  tidak tersentuh.
- Tanpa reboot untuk ganti config (cukup relaunch aplikasi target).
- 100% gratis, open source, tanpa license server.

## 2. Non-tujuan (jujur di depan)

- **Bukan** anti-ban tool. Tidak ada klaim "aman anti-cheat".
- Tidak menyentuh TEE: key attestation & Widevine L1 **mustahil** di-spoof (kunci di secure world).
- Tidak ada IMEI/SIM/GPS/sensor spoof di MVP (stretch, lihat §9).
- Native code yang membaca `__system_property_*` langsung (bypass Java) tidak ter-cover
  fase 1 — didokumentasikan sebagai batasan, bukan disembunyikan.

## 3. Arsitektur

Tiga lapis, dipartisi berdasarkan **channel ownership** (satu channel = satu pemilik,
supaya tidak ada dua layer yang saling menimpa nilai):

```
┌─────────────┐
│   :magisk    │  delivery saja: host .so Zygisk, install/uninstall, tanpa logika spoof
└──────┬──────┘
       │  /data/adb/modules/openspoof/config.json  (single source of truth)
┌──────▼──────┐
│   :zygisk    │  per-PROSES, di dalam proses aplikasi target:
│  (native)    │   · F1: timpa static field android.os.Build via JNI (preAppSpecialize)
│              │   · F2: bind-mount /proc/cpuinfo palsu via mount-namespace (masih root)
│              │   · paket tak terdaftar → DLCLOSE_MODULE_LIBRARY (zero trace)
└─────────────┘
       │
┌──────▼──────┐
│   :webui     │  F3: editor config on-device (vanilla JS, KSU WebUI bridge)
└─────────────┘
```

| Channel | Pemilik | Teknik |
|---|---|---|
| `Build.*`, `Build.VERSION.*` | `:zygisk` (F1) | JNI `SetStaticObjectField` / `SetStaticIntField` |
| `/proc/cpuinfo` | `:zygisk` (F2) | `unshare(CLONE_NEWNS)` + bind-mount, per-proses |
| config transport | file JSON | dibaca tiap `preAppSpecialize`, tanpa daemon |

Kenapa tanpa daemon (tidak seperti COPG `controller`): untuk MVP, baca-ulang config
di tiap spawn proses sudah cukup — tidak ada state live yang perlu dijaga.
Relaunch aplikasi = config baru berlaku. Daemon hanya dibutuhkan kalau nanti ada
toggle device-wide.

## 4. Fase

| Fase | Cakupan | Kriteria selesai |
|---|---|---|
| **F1** | JNI spoof `Build` + `Build.VERSION` per-app, config JSON, packaging, CI | ✅ skeleton selesai (`src/spoof.cpp`, syntax-check OK) |
| **F2** | `/proc/cpuinfo` per-proses via mount-ns + profil cpuinfo | ✅ skeleton selesai (kode + `module/cpuinfo/cpuinfo_redmagic_9_pro` contoh SD 8 Gen 3) |
| **F3** | WebUI: daftar aplikasi, pilih profil, simpan config | ✅ selesai (`webroot/index.html`: filter, autocomplete paket via `pm`, tambah/hapus assignment, atomic save) |
| **F4** | Native hooks (Dobby): fake uptime, uname, prop spoof, getprop consistency | ✅ diimplementasi (Dobby di-vendor, `hooks.cpp` valid lawan header asli, editor + risk gate di WebUI); sisa: build Dobby via CMake/NDK + uji device |
| Stretch / F5 | ART/Java method hooks (Android ID, IMEI, UA, sensor…) | desain awal di `docs/PHASE4.md` §2 |
| Stretch | `Build.getSerial`, refresh-rate, dsb. | — |

Sisa untuk semua fase: **uji di device root fisik** (kriteria "selesai" tiap fase di §10).

## 5. Detail teknik F1 (inti MVP)

- **Hook point:** `preAppSpecialize(AppSpecializeArgs*)`. `args->nice_name` = nama paket
  (untuk proses utama aplikasi). Berjalan **sebelum** kode aplikasi → field `Build`
  ditimpa sebelum sempat dibaca siapa pun.
- **Tidak butuh inline-hook engine** (Dobby/v7878/dll). `android.os.Build` field-nya
  `public static`, jadi cukup JNI:
  ```cpp
  jclass build = env->FindClass("android/os/Build");
  jfieldID fid = env->GetStaticFieldID(build, "MODEL", "Ljava/lang/String;");
  env->SetStaticObjectField(build, fid, env->NewStringUTF("NX769J"));
  ```
- Field yang di-spoof (semua `String` kecuali `SDK_INT`): `BRAND, DEVICE, MANUFACTURER,
  MODEL, PRODUCT, BOARD, HARDWARE, FINGERPRINT, ID, DISPLAY, SERIAL`,
  plus `Build.VERSION`: `RELEASE, INCREMENTAL, CODENAME, SECURITY_PATCH, SDK_INT(int)`.
- **Stealth:** paket yang tidak ada di config → `api->setOption(DLCLOSE_MODULE_LIBRARY)`
  supaya `.so` di-unload, tidak ada jejak di `maps` proses tersebut.
- **Config parser tanpa dependensi:** parser JSON minimal hand-rolled (`src/config.h`,
  ~90 baris, hanya subset yang dipakai skema) — tanpa nlohmann, tanpa Abseil.

## 6. Detail teknik F2

- `preAppSpecialize` dipanggil **saat proses masih root** (sebelum UID drop) → boleh
  `unshare(CLONE_NEWNS)` + `mount(MS_BIND)` di sini.
- Urutan: `unshare` → `mount("none","/",MS_PRIVATE|MS_REC)` (jangan bocorkan mount
  ke system) → bind-mount `<module>/cpuinfo/cpuinfo_<profil>` ke `/proc/cpuinfo`.
- Aplikasi melakukan `open()` biasa → dapat file palsu; **tidak ada hook yang bisa
  dideteksi**, tidak ada file di disk yang diubah.
- Catatan SELinux: diuji di target; bila `mount` ditolak, fallback = dokumentasikan,
  bukan diakali diam-diam.

## 7. Skema config (`module/config.json`)

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

Satu blob config dibaca semua layer → profil tidak bisa "tidak sinkron" antar channel.

## 8. Struktur repo

```
openspoof/
├── SPEC.md                      # file ini
├── README.md
├── module/                      # isi zip yang di-flash
│   ├── module.prop
│   ├── customize.sh             # installer (self-contained, tanpa util_functions)
│   ├── uninstall.sh
│   ├── config.json              # contoh (profil inline + assignment + native_hooks)
│   ├── sepolicy.rule            # (F4) template allow execmem untuk inline hook
│   ├── cpuinfo/                 # (F2) cpuinfo_<profil>, cth. cpuinfo_redmagic_9_pro
│   ├── webroot/index.html       # (F3) editor config: filter, autocomplete, atomic save
│   ├── zygisk/{arm64-v8a,armeabi-v7a,x86_64}.so   # hasil build
│   └── META-INF/com/google/android/{update-binary,updater-script}
├── src/
│   ├── spoof.cpp                # modul Zygisk (F1+F2, wiring F4)
│   ├── hooks.h / hooks.cpp      # (F4) installer native hook via Dobby (no-op tanpa Dobby)
│   └── config.h                 # parser JSON minimal + struct Config/Profile/NativeHooks
├── docs/
│   └── PHASE4.md                # rancangan Fase 4 (engine, cakupan, SELinux, testing, risiko)
├── scripts/build.sh             # build NDK + packaging zip
└── .github/workflows/build.yml  # CI multi-ABI → artifact + release
```

`zygisk.hpp` tidak di-vendor — di-fetch saat build dari
`topjohnwu/zygisk-module-sample` (canonical, backward-compat dijamin Magisk).

## 9. Build & CI

- Lokal: `scripts/build.sh` butuh `ANDROID_NDK_HOME` (NDK r21+).
  `aarch64-linux-android21-clang++ -std=c++17 -shared -fPIC -O2 -I src src/spoof.cpp -llog`.
- CI: GitHub Actions, setup NDK, build 3 ABI, zip `openspoof-vX.Y.Z.zip`, upload artifact;
  release otomatis saat tag `v*`.
- Packaging memuat **META-INF** (belajar dari kasus BFR-WEBUI-GO: tanpa itu gagal
  install di Magisk < 29).

## 10. Rencana testing (butuh device root)

1. Flash zip di Magisk/KernelSU/APatch → reboot.
2. `adb logcat -s OpenSpoof` — pastikan `spoofed <pkg> as profile <key>` muncul
   untuk paket terdaftar, dan tidak muncul untuk paket lain.
3. Aplikasi "Device Info" gratisan (Play Store): cek Model/Fingerprint berubah
   sesuai profil; di aplikasi lain tetap asli.
4. (F2) Di aplikasi target: baca `/proc/cpuinfo` (via `run-as` atau test app)
   = isi profil; di luar = asli.
5. Uji anti-regresi: boot normal, tidak ada bootloop (config salah → modul skip,
   bukan crash — parser gagal = no-op + log).

## 11. Risiko & batasan

- Game dengan anti-cheat kuat (kernel-level / server-side clustering) tetap bisa
  mendeteksi inkonsistensi — ini tool spoofing, bukan bypass anti-cheat.
- OEM ROM & versi Android berbeda → field `Build` bisa beda; daftar field dibuat
  defensif (field hilang = skip + log, bukan crash).
- Tanpa device root fisik, F1–F2 tidak bisa diuji end-to-end di sini.

## 12. Estimasi / status

- F1: skeleton selesai; `spoof.cpp` lolos syntax+type check (stub JNI/Zygisk);
  butuh NDK untuk build beneran + uji device.
- F2: skeleton selesai; contoh cpuinfo SD 8 Gen 3 tersedia; butuh uji `mount()`
  di device (SELinux).
- F3: selesai fungsional (butuh KernelSU WebUI untuk bridge `ksu.exec`);
  belum diuji di WebView beneran.
- Semua fase: kriteria selesai akhir = uji di device root fisik (§10).
