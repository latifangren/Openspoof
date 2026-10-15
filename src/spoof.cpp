// OpenSpoof — per-app device/CPU/identity spoofer (Zygisk module).
//
// Phase 1: android.os.Build static fields via JNI in preAppSpecialize.
//          No inline-hook engine needed — Build fields are public statics.
// Phase 2: per-process /proc/cpuinfo via mount-namespace bind-mount.
//          preAppSpecialize runs while the process is still root.
// Phase 4: Native hooks via Dobby (__system_property_get, uname, uptime, GPU).
// Phase B: Hardware Identifiers (Android ID, Telephony GSM properties, MAC address).
// Phase C: DRM (MediaDrm.getPropertyString via RegisterNatives) and Geolocation
//          hooks (cell LAC/CID, GPS via config in Phase D).
// Phase 5: ART hooks (FLAG_SECURE, locale) via JNI RegisterNatives.
//
// Version policy and the 17-field Build coverage were adopted from
// COPG-VD v5.2.1 (Apache License 2.0). See docs/REFERENCE-COPG.md.
//
// Build with the NDK: see scripts/build.sh
// zygisk.hpp is fetched at build time from topjohnwu/zygisk-module-sample.

#include <dlfcn.h>
#include <fcntl.h>
#include <fstream>
#include <jni.h>
#include <sched.h>
#include <sys/mount.h>
#include <unistd.h>

#include <algorithm>
#include <cctype>
#include <cstdlib>
#include <string>

#include <android/log.h>

#include "zygisk.hpp"
#include "config.h"
#include "hooks.h"

#define LOGD(...) __android_log_print(ANDROID_LOG_INFO, "OpenSpoof", __VA_ARGS__)
#define MODULE_DIR "/data/adb/modules/openspoof"

using zygisk::Api;
using zygisk::AppSpecializeArgs;

namespace {

// --- Phase 1: Build fields ---------------------------------------------------

void setBuildString(JNIEnv* env, jclass cls, const char* name, const std::string& value) {
    if (value.empty()) return;
    jfieldID fid = env->GetStaticFieldID(cls, name, "Ljava/lang/String;");
    if (fid == nullptr) {
        LOGD("Build.%s not present on this ROM, skipped", name);
        env->ExceptionClear();
        return;
    }
    jstring jval = env->NewStringUTF(value.c_str());
    env->SetStaticObjectField(cls, fid, jval);
    env->DeleteLocalRef(jval);
}

// --- Build version group policy ----------------------------------
//
// Read the ROM's true Android version from /system/build.prop.
// We cannot trust the system property (this module overwrites
// it), but /system/build.prop on the read-only mount survives.
// RomVersion stays default-initialised if the file cannot be
// read, which makes Rom policy a no-op.
struct RomVersion {
    std::string release;
    std::string codename;
    int sdk = 0;
};

static RomVersion readRomVersionFromBuildProp() {
    RomVersion rom;
    std::ifstream file("/system/build.prop");
    if (!file.is_open()) return rom;
    std::string line;
    while (std::getline(file, line)) {
        auto take = [&line](const char* key, std::string& out) {
            std::string needle = std::string(key) + "=";
            if (line.rfind(needle, 0) == 0) {
                std::string val = line.substr(needle.size());
                while (!val.empty() && (val.back() == '\r' || val.back() == '\n' ||
                                         val.back() == ' ' || val.back() == '\t')) {
                    val.pop_back();
                }
                out = val;
            }
        };
        take("ro.build.version.release", rom.release);
        take("ro.build.version.codename", rom.codename);
        std::string sdk;
        take("ro.build.version.sdk", sdk);
        if (!sdk.empty()) {
            try { rom.sdk = std::stoi(sdk); } catch (...) { rom.sdk = 0; }
        }
    }
    return rom;
}

// Apply version policy to a candidate (release, sdkInt) pair.
// Returns true if the candidate passes the policy, false to drop it.
static bool versionAllowed(const VersionPolicy policy,
                            const RomVersion& rom,
                            const std::string& cfgRelease,
                            int cfgSdk) {
    if (policy == VersionPolicy::Force) return true;
    if (policy == VersionPolicy::Never) return false;
    // Rom: only what does not exceed the ROM. Lowering the SDK is
    // safe; raising is what triggers API-not-found crashes.
    if (cfgSdk > 0 && rom.sdk > 0 && cfgSdk > rom.sdk) return false;
    if (!cfgRelease.empty() && !rom.release.empty() && cfgRelease != rom.release) {
        // Compare the leading major number so a config of "11"
        // on a 14 ROM is rejected while "13" on a 14 ROM is
        // also rejected but "13" on a 13 ROM passes.
        auto leading = [](const std::string& s) {
            std::string n;
            for (char c : s) {
                if (std::isdigit((unsigned char)c)) n.push_back(c);
                else break;
            }
            return n.empty() ? 0 : std::stoi(n);
        };
        int cfgN = 0, romN = 0;
        try { cfgN = leading(cfgRelease); romN = leading(rom.release); } catch (...) {}
        if (cfgN > romN) return false;
    }
    return true;
}

// AOSP rule: RELEASE_OR_CODENAME = "REL" == CODENAME ? RELEASE : CODENAME.
// Copying a literal CODENAME into it publishes "REL" in the version
// slot, which is a combination no real device reports.
static std::string releaseOrCodename(const std::string& codename,
                                      const std::string& release) {
    return (codename.empty() || codename == "REL") ? release : codename;
}

void spoofBuild(JNIEnv* env, const Profile& p, VersionPolicy versionPolicy) {
    // Decide version fields up front so the SDK_INT / RELEASE we
    // finally write to Build.VERSION are consistent with each
    // other and with the device-true ROM. readRomVersionFromBuildProp
    // reads /system/build.prop which is on the read-only mount, so
    // it sees the unmodified values even if some other module has
    // already poisoned the system properties.
    const RomVersion rom = readRomVersionFromBuildProp();
    const bool versionOk = versionAllowed(versionPolicy, rom, p.release, p.sdkInt);
    const std::string effectiveRelease = versionOk ? p.release : std::string();
    const int effectiveSdk = (versionOk && p.sdkInt > 0) ? p.sdkInt : 0;
    const std::string effectiveCodename = versionOk ? p.codename : std::string();

    jclass build = env->FindClass("android/os/Build");
    if (build == nullptr) {
        LOGD("android/os/Build not found");
        env->ExceptionClear();
        return;
    }
    // 17 Build fields, including the six the original OpenSpoof
    // skipped: BOOTLOADER, HOST, ODM_SKU, SKU, TAGS, TYPE, USER,
    // and Build.TIME (long). Adopted from COPG-VD v5.2.1
    // (Apache-2.0) — see docs/REFERENCE-COPG.md.
    setBuildString(env, build, "BRAND", p.brand);
    setBuildString(env, build, "DEVICE", p.device);
    setBuildString(env, build, "MANUFACTURER", p.manufacturer);
    setBuildString(env, build, "MODEL", p.model);
    setBuildString(env, build, "PRODUCT", p.product);
    setBuildString(env, build, "BOARD", p.board);
    setBuildString(env, build, "BOOTLOADER", p.bootloader);
    setBuildString(env, build, "HARDWARE", p.hardware);
    setBuildString(env, build, "FINGERPRINT", p.fingerprint);
    setBuildString(env, build, "ID", p.id);
    setBuildString(env, build, "DISPLAY", p.display);
    setBuildString(env, build, "HOST", p.host);
    setBuildString(env, build, "ODM_SKU", p.odmSku);
    setBuildString(env, build, "SKU", p.sku);
    setBuildString(env, build, "TAGS", p.tags);
    setBuildString(env, build, "TYPE", p.type);
    setBuildString(env, build, "USER", p.buildUser);
    setBuildString(env, build, "SERIAL", p.serial);

    if (p.time > 0) {
        jfieldID fid = env->GetStaticFieldID(build, "TIME", "J");
        if (fid != nullptr) {
            env->SetStaticLongField(build, fid, (jlong)p.time);
        } else {
            env->ExceptionClear();
        }
    }

    jclass version = env->FindClass("android/os/Build$VERSION");
    if (version == nullptr) {
        env->ExceptionClear();
        env->DeleteLocalRef(build);
        return;
    }
    setBuildString(env, version, "RELEASE", effectiveRelease);
    setBuildString(env, version, "INCREMENTAL", p.incremental);
    setBuildString(env, version, "CODENAME", effectiveCodename);
    setBuildString(env, version, "SECURITY_PATCH", p.securityPatch);

    // SDK_INT, SDK_INT_FULL (API 31+), and SDK string. All three
    // are derived from the same effectiveSdk so an app reading
    // any of them sees the same number.
    if (effectiveSdk > 0) {
        jfieldID fid = env->GetStaticFieldID(version, "SDK_INT", "I");
        if (fid != nullptr) {
            env->SetStaticIntField(version, fid, effectiveSdk);
        } else {
            env->ExceptionClear();
        }
        jfieldID sdkFull = env->GetStaticFieldID(version, "SDK_INT_FULL", "I");
        if (sdkFull != nullptr) {
            env->SetStaticIntField(version, sdkFull, (jint)(effectiveSdk * 100000));
        } else {
            env->ExceptionClear();
        }
        jfieldID sdkStr = env->GetStaticFieldID(version, "SDK", "Ljava/lang/String;");
        if (sdkStr != nullptr) {
            jstring js = env->NewStringUTF(std::to_string(effectiveSdk).c_str());
            if (js != nullptr) {
                env->SetStaticObjectField(version, sdkStr, js);
                env->DeleteLocalRef(js);
            }
        } else {
            env->ExceptionClear();
        }
    }

    // RELEASE_OR_CODENAME follows the AOSP rule. Set only if at
    // least one of (codename, release) passed the policy.
    if (!effectiveCodename.empty() || !effectiveRelease.empty()) {
        std::string rec = releaseOrCodename(
            effectiveCodename.empty() ? rom.codename : effectiveCodename,
            effectiveRelease.empty() ? rom.release : effectiveRelease);
        setBuildString(env, version, "RELEASE_OR_CODENAME", rec);
        setBuildString(env, version, "RELEASE_OR_PREVIEW_DISPLAY", rec);
    }

    env->DeleteLocalRef(build);
    env->DeleteLocalRef(version);
}

// --- Phase 2: /proc/cpuinfo, per-process -------------------------------------
// The app does a plain open() and reads the fake file: no hook to detect,
// no on-disk change visible to anyone else.

void spoofCpuinfo(const std::string& profileKey) {
    std::string src = std::string(MODULE_DIR) + "/cpuinfo/cpuinfo_" + profileKey;
    if (access(src.c_str(), R_OK) != 0) return;  // no cpuinfo override for this profile

    if (unshare(CLONE_NEWNS) != 0) {
        LOGD("unshare(CLONE_NEWNS) failed, cpuinfo spoof skipped");
        return;
    }
    // Keep our mounts private so nothing leaks to the rest of the system.
    mount("none", "/", nullptr, MS_PRIVATE | MS_REC, nullptr);
    if (mount(src.c_str(), "/proc/cpuinfo", nullptr, MS_BIND, nullptr) != 0) {
        LOGD("bind-mount of cpuinfo failed");
    } else {
        LOGD("cpuinfo spoofed from %s", src.c_str());
    }
}

// --- Phase B: Android ID Spoofing via Settings.Secure cache ------------------
void spoofAndroidId(JNIEnv* env, const std::string& androidId) {
    if (androidId.empty() || env == nullptr) return;
    jclass secureCls = env->FindClass("android/provider/Settings$Secure");
    if (secureCls == nullptr) {
        env->ExceptionClear();
        return;
    }
    jfieldID cacheFid = env->GetStaticFieldID(secureCls, "sNameValueCache", "Landroid/provider/Settings$NameValueCache;");
    if (cacheFid == nullptr) {
        env->ExceptionClear();
        return;
    }
    jobject cacheObj = env->GetStaticObjectField(secureCls, cacheFid);
    if (cacheObj == nullptr) {
        env->ExceptionClear();
        return;
    }
    jclass cacheCls = env->GetObjectClass(cacheObj);
    jfieldID valuesFid = env->GetFieldID(cacheCls, "mValues", "Landroid/util/ArrayMap;");
    if (valuesFid == nullptr) {
        env->ExceptionClear();
        valuesFid = env->GetFieldID(cacheCls, "mValues", "Ljava/util/Map;");
    }
    if (valuesFid != nullptr) {
        jobject mapObj = env->GetObjectField(cacheObj, valuesFid);
        if (mapObj != nullptr) {
            jclass mapCls = env->FindClass("java/util/Map");
            if (mapCls != nullptr) {
                jmethodID putMid = env->GetMethodID(mapCls, "put",
                    "(Ljava/lang/Object;Ljava/lang/Object;)Ljava/lang/Object;");
                if (putMid != nullptr) {
                    jstring k = env->NewStringUTF("android_id");
                    jstring v = env->NewStringUTF(androidId.c_str());
                    env->CallObjectMethod(mapObj, putMid, k, v);
                    env->DeleteLocalRef(k);
                    env->DeleteLocalRef(v);
                    LOGD("android_id spoofed in Settings$NameValueCache: %s", androidId.c_str());
                }
            }
        }
    }
    if (env->ExceptionCheck()) {
        env->ExceptionClear();
    }
}

// --- Phase C: DRM / Widevine Spoofing via JNI RegisterNatives ----------------
static jstring (*orig_MediaDrm_getPropertyString)(JNIEnv*, jobject, jstring) = nullptr;
static std::string g_drm_security_level;
static std::string g_drm_vendor;
static std::string g_drm_version;
static std::string g_drm_system_id;

jstring JNICALL fake_MediaDrm_getPropertyString(JNIEnv* env, jobject thiz, jstring name) {
    if (name != nullptr) {
        const char* str = env->GetStringUTFChars(name, nullptr);
        std::string prop = str ? str : "";
        if (str) env->ReleaseStringUTFChars(name, str);

        if (prop == "securityLevel" && !g_drm_security_level.empty()) {
            return env->NewStringUTF(g_drm_security_level.c_str());
        }
        if (prop == "vendor" && !g_drm_vendor.empty()) {
            return env->NewStringUTF(g_drm_vendor.c_str());
        }
        if (prop == "version" && !g_drm_version.empty()) {
            return env->NewStringUTF(g_drm_version.c_str());
        }
        if (prop == "systemId" && !g_drm_system_id.empty()) {
            return env->NewStringUTF(g_drm_system_id.c_str());
        }
    }
    if (orig_MediaDrm_getPropertyString != nullptr) {
        return orig_MediaDrm_getPropertyString(env, thiz, name);
    }
    return nullptr;
}

void installMediaDrmHook(JNIEnv* env, const DrmConfig& drm) {
    if (!drm.enabled() || env == nullptr) return;
    g_drm_security_level = drm.securityLevel;
    g_drm_vendor = drm.vendor;
    g_drm_version = drm.version;
    g_drm_system_id = drm.systemId;

    if (orig_MediaDrm_getPropertyString == nullptr) {
        void* handle = dlopen("libmedia_jni.so", RTLD_NOW);
        if (handle != nullptr) {
            orig_MediaDrm_getPropertyString = reinterpret_cast<jstring (*)(JNIEnv*, jobject, jstring)>(
                dlsym(handle, "android_media_MediaDrm_getPropertyString"));
        }
    }

    jclass cls = env->FindClass("android/media/MediaDrm");
    if (cls == nullptr) {
        env->ExceptionClear();
        return;
    }
    JNINativeMethod method = {
        const_cast<char*>("getPropertyString"),
        const_cast<char*>("(Ljava/lang/String;)Ljava/lang/String;"),
        reinterpret_cast<void*>(fake_MediaDrm_getPropertyString)
    };
    if (env->RegisterNatives(cls, &method, 1) == 0) {
        LOGD("installed MediaDrm.getPropertyString hook via RegisterNatives");
    } else {
        env->ExceptionClear();
    }
}

}  // namespace

class OpenSpoof : public zygisk::ModuleBase {
public:
    void onLoad(Api* api, JNIEnv* env) override {
        api_ = api;
        env_ = env;
        LOGD("OpenSpoof module loaded via Zygisk in process (PID %d)", getpid());
    }

    void preAppSpecialize(AppSpecializeArgs* args) override {
        const char* raw = env_->GetStringUTFChars(args->nice_name, nullptr);
        std::string process = raw ? raw : "";
        if (raw) env_->ReleaseStringUTFChars(args->nice_name, raw);

        LOGD("preAppSpecialize: process='%s'", process.c_str());

        Config cfg;
        int mod_dfd = api_->getModuleDir();
        if (mod_dfd >= 0) {
            int fd = openat(mod_dfd, "config.json", O_RDONLY | O_CLOEXEC);
            if (fd >= 0) {
                std::string content;
                char buf[1024];
                ssize_t n;
                while ((n = read(fd, buf, sizeof(buf))) > 0) {
                    content.append(buf, n);
                }
                close(fd);
                cfg = LoadConfigFromString(content);
            }
        }
        if (cfg.profiles.empty() && cfg.apps.empty()) {
            cfg = LoadConfig(MODULE_DIR "/config.json");
        }

        if (cfg.profiles.empty() && cfg.apps.empty()) {
            LOGD("config.json missing, empty or unreadable; unloading from %s", process.c_str());
            api_->setOption(zygisk::Option::DLCLOSE_MODULE_LIBRARY);
            return;
        }

        auto it = cfg.apps.find(process);
        if (it == cfg.apps.end()) {
            it = cfg.apps.find("*");
            if (it != cfg.apps.end()) {
                // Safety guard: never spoof critical system processes in global mode
                if (process == "system_server" ||
                    process == "com.android.systemui" ||
                    process == "com.android.phone" ||
                    process == "com.google.android.gms" ||
                    process == "com.google.android.gms.unstable" ||
                    process.rfind("com.android.providers.", 0) == 0) {
                    api_->setOption(zygisk::Option::DLCLOSE_MODULE_LIBRARY);
                    return;
                }
            }
        }
        if (it == cfg.apps.end() || it->second.empty() || it->second == "none") {
            // Not configured or explicitly excluded: unload ourselves — zero trace.
            api_->setOption(zygisk::Option::DLCLOSE_MODULE_LIBRARY);
            return;
        }
        const Profile* p = cfg.findProfile(it->second);
        if (p == nullptr) {
            LOGD("profile '%s' not found for '%s', unloading", it->second.c_str(), process.c_str());
            api_->setOption(zygisk::Option::DLCLOSE_MODULE_LIBRARY);
            return;
        }
        androidId_ = p->nativeHooks.id.androidId;
        drm_ = p->nativeHooks.drm;
        disableFlagSecure_ = p->nativeHooks.disableFlagSecure;
        languageTag_ = p->nativeHooks.languageTag;

        spoofCpuinfo(it->second);  // Phase 2 (no-op when no cpuinfo file for profile)
        spoofBuild(env_, *p, cfg.versionPolicy);  // Phase 1 + version policy
        spoofAndroidId(env_, androidId_); // Phase B (early attempt)
        installMediaDrmHook(env_, drm_);  // Phase C (early attempt)
        openspoof::InstallNativeHooks(p->nativeHooks, p);  // Phase 4 & PRO + native system props
        LOGD("SUCCESS: spoofed '%s' as profile '%s' (Model: %s, Brand: %s)",
             process.c_str(), it->second.c_str(), p->model.c_str(), p->brand.c_str());
    }

    void postAppSpecialize(const AppSpecializeArgs*) override {
        if (!androidId_.empty()) {
            spoofAndroidId(env_, androidId_); // Phase B (post-init attempt)
        }
        if (drm_.enabled()) {
            installMediaDrmHook(env_, drm_);  // Phase C (post-init attempt)
        }
        // Phase 5: ART-side hooks run after the app's classes are
        // loaded. FLAG_SECURE is the most reliable here because
        // android.view.Window is a system class that Zygote has
        // already resolved by postAppSpecialize. Locale is
        // similar: java.util.Locale is in the bootclasspath.
        if (disableFlagSecure_) {
            openspoof::InstallFlagSecureHookF5(env_);
        }
        if (!languageTag_.empty()) {
            openspoof::ApplyLocaleOverrideF5(env_, languageTag_);
        }
    }

private:
    Api* api_ = nullptr;
    JNIEnv* env_ = nullptr;
    std::string androidId_;
    DrmConfig drm_;
    bool disableFlagSecure_ = false;
    std::string languageTag_;
};

REGISTER_ZYGISK_MODULE(OpenSpoof)
