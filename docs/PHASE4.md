# Fase 4 — Native Hooks (Dobby)

**Status implementasi (2026-10-15):** selesai dikerjakan sejauh bisa tanpa NDK/device.
Dobby di-vendor di `third_party/dobby` (shallow clone); `src/hooks.cpp` lolos
compile-check lawan **header Dobby asli**; WebUI punya editor `native_hooks`
dengan risk gate ("use at your own risk"); contoh config terisi. Sisa: build
`libdobby.a` via CMake+NDK di mesin build, lalu uji di device root (§7).

**Tujuan:** membuka fitur sekelas PRO COPG yang bekerja di **lapisan native**
(tanpa Java method hooking) — dengan satu integrasi hook engine.

## 1. Pilihan engine: Dobby

- **Dobby** — https://github.com/jmpews/Dobby — inline-hook framework
  multi-arch (ARM/ARM64/x86_64), dipakai luas di modul Zygisk/game-modding,
  API stabil: `DobbyHook(addr, replace, &orig)`.
- Alternatif yang dipertimbangkan: ShadowHook (ByteDance, optimasi Android),
  v7878 (dipakai COPG — URL repo tidak terverifikasi, jadi tidak dipilih).
- Dobby dialokasikan sebagai **static lib** (`libdobby.a`) dan di-link ke
  `zygisk/*.so` — tidak ada `.so` tambahan yang terlihat di `maps`.

## 2. Cakupan Fase 4

| ID | Fitur | Teknik | Tingkat |
|---|---|---|---|
| F4a | **Fake uptime** per-app | Hook `clock_gettime`; geser `CLOCK_BOOTTIME`/`CLOCK_MONOTONIC` sebesar `uptime_offset_sec` | mudah |
| F4b | **uname spoof** per-app | Hook `uname()`; timpa `machine`/`sysname` | mudah |
| F4c | **Native prop spoof** per-app | Hook `__system_property_get`; kembalikan nilai config untuk key terdaftar, teruskan sisanya ke fungsi asli | sedang |
| F4d | **getprop consistency** | *Gratis dari F4c:* anak proses (`Runtime.exec("getprop")`) adalah fork dari proses aplikasi → mewarisi memori yang sudah di-hook → `getprop` baca nilai spoof yang sama | — (verifikasi) |

**Eksplisit bukan Fase 4** (butuh ART/Java method hooking — direncanakan Fase 5):
Android ID, IMEI/TelephonyManager, WebView UA, sensor list, GPS/Fused, SIM.
Pilihan Fase 5: integrasi Pine/LSPlant-style ART hook, atau modul pendamping LSPosed.

## 3. Arsitektur

```
preAppSpecialize (masih root, sebelum kode aplikasi)
  └─ paket cocok? ── ya ──▶ InstallNativeHooks(profile.nativeHooks)
                               ├─ clock_gettime → fake_clock_gettime   (F4a)
                               ├─ uname         → fake_uname           (F4b)
                               └─ __system_property_get → fake_...    (F4c)
                            tidak ──▶ no-op (tanpa Dobby pun aman)
```

- **Per-app gating:** hook hanya dipasang untuk paket yang terdaftar di config.
  Paket lain → `DLCLOSE_MODULE_LIBRARY` seperti Fase 1 (zero trace).
- **Tanpa Dobby:** `InstallNativeHooks()` jadi no-op + log — build tetap jalan,
  fitur Fase 1–3 tidak terpengaruh.
- **F4c hot path:** `__system_property_get` dipanggil sangat sering; implementasi
  hanya `map.find(name)` dan langsung teruskan ke fungsi asli bila tidak cocok.
  Key yang di-spoof sebaiknya sedikit (daftar di config).

## 4. SELinux

Inline hook butuh halaman anonim RWX (trampolin Dobby) dan `mprotect` di `.text`.
Template di `module/sepolicy.rule` (dari pola COPG):

```
allow untrusted_app untrusted_app process execmem
```

Ini **template** — validasi di device: `adb shell dmesg | grep avc` setelah
menjalankan aplikasi target, lalu ketatkan domain sesuai ROM. Tanpa rule yang
benar, `DobbyHook` gagal diam-diam (di-log, bukan crash).

## 5. Skema config

```json
"native_hooks": {
  "uptime_offset_sec": 172800,
  "uname": { "machine": "aarch64", "sysname": "Linux" },
  "props": { "ro.product.model": "NX769J", "ro.product.brand": "nubia" }
}
```

Semua field opsional; objek boleh kosong/absen → hook terkait tidak dipasang.

## 6. Build & vendoring

```bash
git clone https://github.com/jmpews/Dobby third_party/dobby
# build Dobby sesuai docs-nya (CMake + NDK) hingga menghasilkan libdobby.a
./scripts/build.sh   # otomatis -DHAVE_DOBBY bila third_party/dobby/include ada
```

## 7. Testing (device root)

1. `adb logcat -s OpenSpoof` → `hooked clock_gettime/uname/__system_property_get`.
2. F4a: aplikasi target baca uptime (mis. `SystemClock.elapsedRealtime()`)
   bergeser sebesar offset; aplikasi lain normal.
3. F4c: di aplikasi target, baca prop via `__system_property_get`
   (NdkMedia atau test app) = nilai config.
4. F4d: dari aplikasi target, `Runtime.exec("getprop ro.product.model")`
   → nilai spoof (bukan nilai asli).
5. `adb shell dmesg | grep avc` → tidak ada denial terkait hook.

## 8. Risiko — dibaca dulu

Inline hook meninggalkan artefak yang **bisa dideteksi**: prolog fungsi yang
dimodifikasi, trampolin RWX anonim, `mprotect` di `.text`. Ini menaikkan profil
deteksi dibanding Fase 1 (yang zero-trace untuk paket tak terdaftar).
Ikuti pola COPG: tandai fitur Fase 4 sebagai **resident / use-at-your-own-risk**,
jangan dipakai untuk game dengan anti-cheat kompetitif.
