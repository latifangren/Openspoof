// OpenSpoof — minimal per-app device/CPU spoofer (Zygisk module).
//
// Phase 1: android.os.Build static fields via JNI in preAppSpecialize.
//          No inline-hook engine needed — Build fields are public statics.
// Phase 2: per-process /proc/cpuinfo via mount-namespace bind-mount.
//          preAppSpecialize runs while the process is still root.
// Phase 4: Native hooks via Dobby (__system_property_get, uname, uptime, GPU).
// Phase B: Hardware Identifiers (Android ID, Telephony GSM properties, MAC address).
//
// Build with the NDK: see scripts/build.sh
// zygisk.hpp is fetched at build time from topjohnwu/zygisk-module-sample.

#include <jni.h>
#include <sched.h>
#include <sys/mount.h>
#include <unistd.h>

#include <string>

#include <android/log.h>

#include "zygisk.hpp"
#include "config.h"
#include "hooks.h"

#define LOGD(...) __android_log_print(ANDROID_LOG_DEBUG, "OpenSpoof", __VA_ARGS__)
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

void spoofBuild(JNIEnv* env, const Profile& p) {
    jclass build = env->FindClass("android/os/Build");
    if (build == nullptr) {
        LOGD("android/os/Build not found");
        env->ExceptionClear();
        return;
    }
    setBuildString(env, build, "BRAND", p.brand);
    setBuildString(env, build, "DEVICE", p.device);
    setBuildString(env, build, "MANUFACTURER", p.manufacturer);
    setBuildString(env, build, "MODEL", p.model);
    setBuildString(env, build, "PRODUCT", p.product);
    setBuildString(env, build, "BOARD", p.board);
    setBuildString(env, build, "HARDWARE", p.hardware);
    setBuildString(env, build, "FINGERPRINT", p.fingerprint);
    setBuildString(env, build, "ID", p.id);
    setBuildString(env, build, "DISPLAY", p.display);
    setBuildString(env, build, "SERIAL", p.serial);

    jclass version = env->FindClass("android/os/Build$VERSION");
    if (version != nullptr) {
        setBuildString(env, version, "RELEASE", p.release);
        setBuildString(env, version, "INCREMENTAL", p.incremental);
        setBuildString(env, version, "CODENAME", p.codename);
        setBuildString(env, version, "SECURITY_PATCH", p.securityPatch);
        if (p.sdkInt > 0) {
            jfieldID fid = env->GetStaticFieldID(version, "SDK_INT", "I");
            if (fid != nullptr) {
                env->SetStaticIntField(version, fid, p.sdkInt);
            } else {
                env->ExceptionClear();
            }
        }
    } else {
        env->ExceptionClear();
    }
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

}  // namespace

class OpenSpoof : public zygisk::ModuleBase {
public:
    void onLoad(Api* api, JNIEnv* env) override {
        api_ = api;
        env_ = env;
    }

    void preAppSpecialize(AppSpecializeArgs* args) override {
        const char* raw = env_->GetStringUTFChars(args->nice_name, nullptr);
        std::string process = raw ? raw : "";
        if (raw) env_->ReleaseStringUTFChars(args->nice_name, raw);

        Config cfg = LoadConfig(MODULE_DIR "/config.json");
        auto it = cfg.apps.find(process);
        if (it == cfg.apps.end()) {
            // Not configured: unload ourselves — zero trace in this process.
            api_->setOption(zygisk::Option::DLCLOSE_MODULE_LIBRARY);
            return;
        }
        const Profile* p = cfg.findProfile(it->second);
        if (p == nullptr) {
            LOGD("profile '%s' not found, skipping %s", it->second.c_str(), process.c_str());
            return;
        }
        androidId_ = p->nativeHooks.id.androidId;
        spoofCpuinfo(it->second);  // Phase 2 (no-op when no cpuinfo file for profile)
        spoofBuild(env_, *p);      // Phase 1
        spoofAndroidId(env_, androidId_); // Phase B (early attempt)
        openspoof::InstallNativeHooks(p->nativeHooks);  // Phase 4 & PRO
        LOGD("spoofed %s as profile '%s'", process.c_str(), it->second.c_str());
    }

    void postAppSpecialize(const AppSpecializeArgs*) override {
        if (!androidId_.empty()) {
            spoofAndroidId(env_, androidId_); // Phase B (post-init attempt)
        }
    }

private:
    Api* api_ = nullptr;
    JNIEnv* env_ = nullptr;
    std::string androidId_;
};

REGISTER_ZYGISK_MODULE(OpenSpoof)
