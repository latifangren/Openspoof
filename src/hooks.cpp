// OpenSpoof Phase 4 & PRO Tier — native-layer hooks via Dobby.
//
// Hooks patch this process's memory; children forked from the app process
// (e.g. `getprop` via Runtime.exec) inherit them — that is what gives us
// getprop consistency on top of the __system_property_get hook.
//
// PRO Features included:
//   - F4a: fake uptime (clock_gettime)
//   - F4b: fake uname (uname)
//   - F4c: fake native properties (__system_property_get)
//   - F4e: GPU Spoofing (glGetString, eglQueryString, eglGetProcAddress, vkGetPhysicalDeviceProperties)
//   - Phase B: Telephony & SIM Identifiers (IMEI, IMSI, ICCID, Operator via __system_property_get)
//   - Phase B: Hardware MAC address spoofing via ioctl(SIOCGIFHWADDR)
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
#include <stdint.h>
#include <stdio.h>
#include <sys/utsname.h>
#include <sys/ioctl.h>
#include <net/if.h>

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

// --- F4c: __system_property_get & __system_property_read_callback --------------
// Hot path: single map lookup, straight through to the original otherwise.
static int (*orig_system_property_get)(const char*, char*) = nullptr;
static std::map<std::string, std::string> g_props;

typedef void (*prop_read_callback_fn)(void* cookie, const char* name, const char* value, uint32_t serial);
typedef void (*PFN_system_property_read_callback)(const void* pi, prop_read_callback_fn callback, void* cookie);
static PFN_system_property_read_callback orig_system_property_read_callback = nullptr;

struct PropCallbackWrapper {
    prop_read_callback_fn orig_cb;
    void* orig_cookie;
};

static void fake_prop_callback(void* cookie, const char* name, const char* value, uint32_t serial) {
    auto* wrap = reinterpret_cast<PropCallbackWrapper*>(cookie);
    if (!wrap || !wrap->orig_cb) return;
    if (name != nullptr) {
        auto it = g_props.find(name);
        if (it != g_props.end()) {
            wrap->orig_cb(wrap->orig_cookie, name, it->second.c_str(), serial);
            return;
        }
    }
    wrap->orig_cb(wrap->orig_cookie, name, value, serial);
}

void fake_system_property_read_callback(const void* pi, prop_read_callback_fn callback, void* cookie) {
    if (!orig_system_property_read_callback) return;
    if (!callback) return;
    PropCallbackWrapper wrap{callback, cookie};
    orig_system_property_read_callback(pi, fake_prop_callback, &wrap);
}

int fake_system_property_get(const char* name, char* value) {
    if (name == nullptr || value == nullptr) return 0;
    auto it = g_props.find(name);
    if (it != g_props.end()) {
        size_t n = it->second.size();
        if (n > 91) n = 91;  // PROP_VALUE_MAX - 1
        memcpy(value, it->second.c_str(), n);
        value[n] = '\0';
        return (int)n;
    }
    if (orig_system_property_get != nullptr) {
        return orig_system_property_get(name, value);
    }
    return 0;
}

// --- Phase B: MAC address spoofing via ioctl(SIOCGIFHWADDR) -------------------
static int (*orig_ioctl)(int, unsigned long, ...) = nullptr;
static std::string g_mac_address;

int fake_ioctl(int fd, unsigned long request, void* argp) {
    int r = orig_ioctl(fd, request, argp);
    if (r == 0 && request == SIOCGIFHWADDR && argp != nullptr && !g_mac_address.empty()) {
        auto* ifr = reinterpret_cast<struct ifreq*>(argp);
        unsigned int m[6];
        if (sscanf(g_mac_address.c_str(), "%x:%x:%x:%x:%x:%x",
                   &m[0], &m[1], &m[2], &m[3], &m[4], &m[5]) == 6) {
            for (int i = 0; i < 6; i++) {
                ifr->ifr_hwaddr.sa_data[i] = static_cast<char>(m[i]);
            }
        }
    }
    return r;
}

// --- F4e: GPU / OpenGL & EGL Intercept (PRO Tier) ------------------------------
#define GL_VENDOR 0x1F00
#define GL_RENDERER 0x1F01
#define GL_VERSION 0x1F02
#define EGL_VENDOR 0x3053
#define EGL_VERSION 0x3054

static const unsigned char* (*orig_glGetString)(unsigned int) = nullptr;
static const char* (*orig_eglQueryString)(void*, int) = nullptr;
static void* (*orig_eglGetProcAddress)(const char*) = nullptr;

static std::string g_gpu_vendor;
static std::string g_gpu_renderer;
static std::string g_gpu_version;
static std::string g_egl_vendor;
static std::string g_egl_version;

const unsigned char* fake_glGetString(unsigned int name) {
    if (name == GL_RENDERER && !g_gpu_renderer.empty()) {
        return reinterpret_cast<const unsigned char*>(g_gpu_renderer.c_str());
    }
    if (name == GL_VENDOR && !g_gpu_vendor.empty()) {
        return reinterpret_cast<const unsigned char*>(g_gpu_vendor.c_str());
    }
    if (name == GL_VERSION && !g_gpu_version.empty()) {
        return reinterpret_cast<const unsigned char*>(g_gpu_version.c_str());
    }
    if (orig_glGetString != nullptr) {
        return orig_glGetString(name);
    }
    return nullptr;
}

const char* fake_eglQueryString(void* dpy, int name) {
    if (name == EGL_VENDOR && !g_egl_vendor.empty()) {
        return g_egl_vendor.c_str();
    }
    if (name == EGL_VENDOR && !g_gpu_vendor.empty()) {
        return g_gpu_vendor.c_str();
    }
    if (name == EGL_VERSION && !g_egl_version.empty()) {
        return g_egl_version.c_str();
    }
    if (orig_eglQueryString != nullptr) {
        return orig_eglQueryString(dpy, name);
    }
    return nullptr;
}

void* fake_eglGetProcAddress(const char* procname) {
    if (procname != nullptr) {
        if (strcmp(procname, "glGetString") == 0) {
            return reinterpret_cast<void*>(fake_glGetString);
        }
    }
    if (orig_eglGetProcAddress != nullptr) {
        return orig_eglGetProcAddress(procname);
    }
    return nullptr;
}

// --- F4f: Vulkan Physical Device Properties (PRO Tier) ------------------------
struct FakeVkPhysicalDeviceProperties {
    uint32_t apiVersion;
    uint32_t driverVersion;
    uint32_t vendorID;
    uint32_t deviceID;
    uint32_t deviceType;
    char deviceName[256];
    uint8_t pipelineCacheUUID[16];
};

static void (*orig_vkGetPhysicalDeviceProperties)(void*, void*) = nullptr;

void fake_vkGetPhysicalDeviceProperties(void* physicalDevice, void* pProperties) {
    if (orig_vkGetPhysicalDeviceProperties != nullptr) {
        orig_vkGetPhysicalDeviceProperties(physicalDevice, pProperties);
    }
    if (pProperties != nullptr && !g_gpu_renderer.empty()) {
        auto* props = reinterpret_cast<FakeVkPhysicalDeviceProperties*>(pProperties);
        strncpy(props->deviceName, g_gpu_renderer.c_str(), sizeof(props->deviceName) - 1);
        props->deviceName[sizeof(props->deviceName) - 1] = '\0';
        if (!g_gpu_vendor.empty()) {
            if (g_gpu_vendor.find("Qualcomm") != std::string::npos) {
                props->vendorID = 0x5143; // Qualcomm PCI vendor ID
            } else if (g_gpu_vendor.find("ARM") != std::string::npos) {
                props->vendorID = 0x13B5; // ARM PCI vendor ID
            }
        }
    }
}

typedef void* (*PFN_vkGetInstanceProcAddr)(void* instance, const char* pName);
static PFN_vkGetInstanceProcAddr orig_vkGetInstanceProcAddr = nullptr;

void* fake_vkGetInstanceProcAddr(void* instance, const char* pName) {
    if (pName != nullptr) {
        if (strcmp(pName, "vkGetPhysicalDeviceProperties") == 0) {
            return (void*)fake_vkGetPhysicalDeviceProperties;
        }
    }
    if (orig_vkGetInstanceProcAddr != nullptr) {
        return orig_vkGetInstanceProcAddr(instance, pName);
    }
    return nullptr;
}

// Helper: look up symbol in process or try explicitly loading companion library
void* findSymbol(const char* libname, const char* sym) {
    void* addr = dlsym(RTLD_DEFAULT, sym);
    if (addr != nullptr) return addr;
    if (libname != nullptr) {
        void* handle = dlopen(libname, RTLD_NOW);
        if (handle != nullptr) {
            addr = dlsym(handle, sym);
        }
    }
    return addr;
}

bool hookOne(const char* libname, const char* sym, void* replace, void** orig, const char* label) {
    void* addr = findSymbol(libname, sym);
    if (addr == nullptr) {
        LOGD("symbol %s not found (lib: %s), skipped", sym, libname ? libname : "default");
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

    // F4a: uptime
    if (h.uptimeOffsetSec != 0) {
        g_uptime_offset_sec = h.uptimeOffsetSec;
        any |= hookOne(nullptr, "clock_gettime", (void*)fake_clock_gettime,
                       (void**)&orig_clock_gettime, "clock_gettime");
    }

    // F4b: uname
    if (!h.unameMachine.empty() || !h.unameSysname.empty()) {
        g_uname_machine = h.unameMachine;
        g_uname_sysname = h.unameSysname;
        any |= hookOne(nullptr, "uname", (void*)fake_uname, (void**)&orig_uname, "uname");
    }

    // Phase B: Telephony & SIM Identifiers (GSM System Properties)
    if (h.telephony.enabled()) {
        const auto& t = h.telephony;
        if (!t.simOperator.empty()) {
            g_props["gsm.sim.operator.numeric"] = t.simOperator;
            g_props["gsm.operator.numeric"] = t.simOperator;
        }
        if (!t.simOperatorName.empty()) {
            g_props["gsm.sim.operator.alpha"] = t.simOperatorName;
            g_props["gsm.operator.alpha"] = t.simOperatorName;
        }
        if (!t.simCountryIso.empty()) {
            g_props["gsm.sim.operator.iso-country"] = t.simCountryIso;
            g_props["gsm.operator.iso-country"] = t.simCountryIso;
        }
        if (!t.imei.empty()) {
            g_props["persist.radio.imei"] = t.imei;
            g_props["ro.ril.oem.imei"] = t.imei;
        }
        g_props["gsm.sim.state"] = "LOADED";
    }

    // Phase C: Geolocation Cell Tower properties
    if (h.location.enabled()) {
        if (h.location.lac != 0) {
            g_props["gsm.cell.lac"] = std::to_string(h.location.lac);
        }
        if (h.location.cid != 0) {
            g_props["gsm.cell.cid"] = std::to_string(h.location.cid);
        }
    }

    // Phase C: GAID (Google Advertising ID)
    if (h.id.enabled() && !h.id.gaid.empty()) {
        g_props["ro.advertising.id"] = h.id.gaid;
        g_props["persist.sys.advertising_id"] = h.id.gaid;
    }

    // F4c: __system_property_get and __system_property_read_callback (modern Android)
    if (!h.props.empty() || h.telephony.enabled() || h.location.enabled() || (h.id.enabled() && !h.id.gaid.empty())) {
        for (const auto& kv : h.props) {
            g_props[kv.first] = kv.second;
        }
        any |= hookOne(nullptr, "__system_property_get", (void*)fake_system_property_get,
                       (void**)&orig_system_property_get, "__system_property_get");
        any |= hookOne("libc.so", "__system_property_read_callback", (void*)fake_system_property_read_callback,
                       (void**)&orig_system_property_read_callback, "__system_property_read_callback");
    }

    // Phase B: Hardware Identifiers (MAC address via ioctl)
    if (h.id.enabled() && !h.id.macAddress.empty()) {
        g_mac_address = h.id.macAddress;
        any |= hookOne("libc.so", "ioctl", (void*)fake_ioctl,
                       (void**)&orig_ioctl, "ioctl (SIOCGIFHWADDR)");
    }

    // F4e & F4f: GPU Spoofing (OpenGL, EGL, Vulkan)
    if (h.gpu.enabled()) {
        g_gpu_vendor = h.gpu.vendor;
        g_gpu_renderer = h.gpu.renderer;
        g_gpu_version = h.gpu.version;
        g_egl_vendor = h.gpu.eglVendor;
        g_egl_version = h.gpu.eglVersion;

        any |= hookOne("libGLESv2.so", "glGetString", (void*)fake_glGetString,
                       (void**)&orig_glGetString, "glGetString");
        any |= hookOne("libEGL.so", "eglQueryString", (void*)fake_eglQueryString,
                       (void**)&orig_eglQueryString, "eglQueryString");
        any |= hookOne("libEGL.so", "eglGetProcAddress", (void*)fake_eglGetProcAddress,
                       (void**)&orig_eglGetProcAddress, "eglGetProcAddress");
        any |= hookOne("libvulkan.so", "vkGetPhysicalDeviceProperties",
                       (void*)fake_vkGetPhysicalDeviceProperties,
                       (void**)&orig_vkGetPhysicalDeviceProperties, "vkGetPhysicalDeviceProperties");
        any |= hookOne("libvulkan.so", "vkGetInstanceProcAddr",
                       (void*)fake_vkGetInstanceProcAddr,
                       (void**)&orig_vkGetInstanceProcAddr, "vkGetInstanceProcAddr");
    }

    return any;
#else
    (void)h;
    LOGD("Phase 4 hooks not built (Dobby missing) — skipping");
    return false;
#endif
}

}  // namespace openspoof
