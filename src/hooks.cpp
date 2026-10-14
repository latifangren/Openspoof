// OpenSpoof Phase 4 — native-layer hooks via Dobby.
//
// Hooks patch this process's memory; children forked from the app process
// (e.g. `getprop` via Runtime.exec) inherit them — that is what gives us
// getprop consistency (F4d) on top of the __system_property_get hook (F4c).
//
// Build: vendor Dobby at third_party/dobby, build libdobby.a, compile with
// -DHAVE_DOBBY -I third_party/dobby/include. Without it this file compiles
// to a safe no-op.

#include "hooks.h"

#include <android/log.h>

#define LOGD(...) __android_log_print(ANDROID_LOG_DEBUG, "OpenSpoof", __VA_ARGS__)

#ifdef HAVE_DOBBY
#include <dlfcn.h>
#include <string.h>
#include <time.h>
#include <sys/utsname.h>

#include "dobby.h"
#endif

namespace openspoof {
namespace {

#ifdef HAVE_DOBBY

// --- F4a: fake uptime ---------------------------------------------------------
static int (*orig_clock_gettime)(clockid_t, struct timespec*) = nullptr;
static long g_uptime_offset_sec = 0;

int fake_clock_gettime(clockid_t clk, struct timespec* tp) {
    int r = orig_clock_gettime(clk, tp);
    if (r == 0 && g_uptime_offset_sec != 0 &&
        (clk == CLOCK_BOOTTIME || clk == CLOCK_MONOTONIC)) {
        tp->tv_sec += g_uptime_offset_sec;
    }
    return r;
}

// --- F4b: uname -----------------------------------------------------------------
static int (*orig_uname)(struct utsname*) = nullptr;
static std::string g_uname_machine;
static std::string g_uname_sysname;

void copyField(char* dst, size_t dstSize, const std::string& src) {
    if (src.empty()) return;
    strncpy(dst, src.c_str(), dstSize - 1);
    dst[dstSize - 1] = '\0';
}

int fake_uname(struct utsname* buf) {
    int r = orig_uname(buf);
    if (r == 0) {
        copyField(buf->machine, sizeof(buf->machine), g_uname_machine);
        copyField(buf->sysname, sizeof(buf->sysname), g_uname_sysname);
    }
    return r;
}

// --- F4c: __system_property_get ---------------------------------------------------
// Hot path: single map lookup, straight through to the original otherwise.
static int (*orig_system_property_get)(const char*, char*) = nullptr;
static std::map<std::string, std::string> g_props;

int fake_system_property_get(const char* name, char* value) {
    auto it = g_props.find(name);
    if (it != g_props.end()) {
        size_t n = it->second.size();
        if (n > 91) n = 91;  // PROP_VALUE_MAX - 1
        memcpy(value, it->second.c_str(), n);
        value[n] = '\0';
        return (int)n;
    }
    return orig_system_property_get(name, value);
}

bool hookOne(const char* sym, void* replace, void** orig, const char* label) {
    void* addr = dlsym(RTLD_DEFAULT, sym);
    if (addr == nullptr) {
        LOGD("dlsym(%s) failed, skipped", sym);
        return false;
    }
    if (DobbyHook(addr, replace, orig) != 0) {
        LOGD("DobbyHook(%s) failed (sepolicy? see sepolicy.rule)", label);
        return false;
    }
    LOGD("hooked %s", label);
    return true;
}

#endif  // HAVE_DOBBY

}  // namespace

bool InstallNativeHooks(const NativeHooks& h) {
#ifdef HAVE_DOBBY
    if (!h.enabled()) return false;
    bool any = false;
    if (h.uptimeOffsetSec != 0) {
        g_uptime_offset_sec = h.uptimeOffsetSec;
        any |= hookOne("clock_gettime", (void*)fake_clock_gettime,
                       (void**)&orig_clock_gettime, "clock_gettime");
    }
    if (!h.unameMachine.empty() || !h.unameSysname.empty()) {
        g_uname_machine = h.unameMachine;
        g_uname_sysname = h.unameSysname;
        any |= hookOne("uname", (void*)fake_uname, (void**)&orig_uname, "uname");
    }
    if (!h.props.empty()) {
        g_props = h.props;
        any |= hookOne("__system_property_get", (void*)fake_system_property_get,
                       (void**)&orig_system_property_get, "__system_property_get");
    }
    return any;
#else
    (void)h;
    LOGD("Phase 4 hooks not built (Dobby missing) — skipping");
    return false;
#endif
}

}  // namespace openspoof
