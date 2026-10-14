// OpenSpoof — minimal per-app device/CPU spoofer (Zygisk module).
//
// Phase 1: android.os.Build static fields via JNI in preAppSpecialize.
//          No inline-hook engine needed — Build fields are public statics.
// Phase 2: per-process /proc/cpuinfo via mount-namespace bind-mount.
//          preAppSpecialize runs while the process is still root.
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
        spoofCpuinfo(it->second);  // Phase 2 (no-op when no cpuinfo file for profile)
        spoofBuild(env_, *p);      // Phase 1
        openspoof::InstallNativeHooks(p->nativeHooks);  // Phase 4 (no-op without Dobby)
        LOGD("spoofed %s as profile '%s'", process.c_str(), it->second.c_str());
    }

private:
    Api* api_ = nullptr;
    JNIEnv* env_ = nullptr;
};

REGISTER_ZYGISK_MODULE(OpenSpoof)
