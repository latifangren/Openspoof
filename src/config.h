// OpenSpoof — minimal JSON parser (subset only) + config model.
// Supports exactly what module/config.json uses:
//   { "profiles": { "<key>": { "FIELD": "str"|123, ... } }, "apps": { "<pkg>": "<key>" } }
// No dependencies. Parse failure -> empty Config (module becomes a no-op, never crashes).
#pragma once

#include <cctype>
#include <fstream>
#include <map>
#include <sstream>
#include <string>
#include <utility>

// Phase 4: per-profile native hooks (all fields optional).
struct GpuConfig {
    std::string vendor;     // e.g. "Qualcomm"
    std::string renderer;   // e.g. "Adreno (TM) 750" or "Adreno (TM) 830"
    std::string version;    // e.g. "OpenGL ES 3.2 V@0750.0"
    std::string eglVendor;  // e.g. "Qualcomm"
    std::string eglVersion; // e.g. "1.5"

    bool enabled() const {
        return !renderer.empty() || !vendor.empty() || !eglVendor.empty();
    }
};

// Phase B: Telephony & SIM Identifiers (IMEI, IMSI, ICCID, Operator)
struct TelephonyConfig {
    std::string imei;               // 15-digit IMEI
    std::string imsi;               // 15-digit IMSI
    std::string iccid;              // 20-digit ICCID
    std::string simOperator;        // e.g. "310260"
    std::string simOperatorName;    // e.g. "T-Mobile"
    std::string simCountryIso;      // e.g. "us"
    std::string phoneType;          // e.g. "1" (GSM)

    bool enabled() const {
        return !imei.empty() || !imsi.empty() || !iccid.empty() || !simOperator.empty();
    }
};

// Phase B: Hardware & System Identifiers (Android ID, MAC)
struct IdentifierConfig {
    std::string androidId;          // 16-hex digit Android ID
    std::string macAddress;         // e.g. "00:1A:2B:3C:4D:5E"

    bool enabled() const {
        return !androidId.empty() || !macAddress.empty();
    }
};

struct NativeHooks {
    long uptimeOffsetSec = 0;                  // F4a: shift CLOCK_BOOTTIME/MONOTONIC
    std::string unameMachine;                  // F4b
    std::string unameSysname;                  // F4b
    std::map<std::string, std::string> props;  // F4c: key -> spoofed value
    GpuConfig gpu;                             // F4e: GPU / OpenGL / EGL / Vulkan
    TelephonyConfig telephony;                  // Phase B: Telephony & SIM
    IdentifierConfig id;                       // Phase B: Android ID & MAC

    bool enabled() const {
        return uptimeOffsetSec != 0 || !unameMachine.empty() ||
               !unameSysname.empty() || !props.empty() || gpu.enabled() ||
               telephony.enabled() || id.enabled();
    }
};

struct Profile {
    std::string brand, device, manufacturer, model, product;
    std::string board, hardware, fingerprint, id, display, serial;
    std::string release, incremental, codename, securityPatch;
    int sdkInt = 0;
    NativeHooks nativeHooks;  // Phase 4: optional
};

struct Config {
    std::map<std::string, Profile> profiles;  // profile key -> profile
    std::map<std::string, std::string> apps;   // package name -> profile key

    const Profile* findProfile(const std::string& key) const {
        auto it = profiles.find(key);
        return it == profiles.end() ? nullptr : &it->second;
    }
};

namespace openspoof_json {

struct Value {
    bool isObject = false;
    std::string str;                       // string or number literal
    std::map<std::string, Value> object;
};

class Parser {
public:
    explicit Parser(std::string s) : s_(std::move(s)) {}

    bool parse(Value& out) {
        skipWs();
        if (!parseValue(out)) return false;
        skipWs();
        return pos_ == s_.size() && out.isObject;
    }

private:
    std::string s_;
    size_t pos_ = 0;

    void skipWs() {
        while (pos_ < s_.size() && std::isspace((unsigned char)s_[pos_])) pos_++;
    }
    bool consume(char c) {
        skipWs();
        if (pos_ < s_.size() && s_[pos_] == c) { pos_++; return true; }
        return false;
    }
    bool parseString(std::string& out) {
        skipWs();
        if (pos_ >= s_.size() || s_[pos_] != '"') return false;
        pos_++;
        std::string r;
        while (pos_ < s_.size()) {
            char c = s_[pos_++];
            if (c == '"') { out = r; return true; }
            if (c == '\\' && pos_ < s_.size()) {
                char e = s_[pos_++];
                r += (e == 'n') ? '\n' : (e == 't') ? '\t' : e;
            } else {
                r += c;
            }
        }
        return false;
    }
    bool parseNumber(std::string& out) {
        skipWs();
        size_t start = pos_;
        if (pos_ < s_.size() && s_[pos_] == '-') pos_++;
        while (pos_ < s_.size() && std::isdigit((unsigned char)s_[pos_])) pos_++;
        if (pos_ == start) return false;
        out = s_.substr(start, pos_ - start);
        return true;
    }
    bool parseValue(Value& out) {
        skipWs();
        if (pos_ >= s_.size()) return false;
        if (s_[pos_] == '{') return parseObject(out);
        std::string lit;
        if (s_[pos_] == '"') {
            if (!parseString(lit)) return false;
        } else {
            if (!parseNumber(lit)) return false;
        }
        out.isObject = false;
        out.str = lit;
        return true;
    }
    bool parseObject(Value& out) {
        if (!consume('{')) return false;
        out.isObject = true;
        skipWs();
        if (consume('}')) return true;
        while (true) {
            std::string key;
            Value val;
            if (!parseString(key)) return false;
            if (!consume(':')) return false;
            if (!parseValue(val)) return false;
            out.object[key] = val;
            skipWs();
            if (consume('}')) return true;
            if (!consume(',')) return false;
        }
    }
};

inline void fillProfile(Profile& p, const Value& obj) {
    auto get = [&](const Value& o, const char* k) -> std::string {
        auto it = o.object.find(k);
        return it == o.object.end() ? std::string() : it->second.str;
    };
    p.brand = get(obj, "BRAND"); p.device = get(obj, "DEVICE");
    p.manufacturer = get(obj, "MANUFACTURER"); p.model = get(obj, "MODEL");
    p.product = get(obj, "PRODUCT"); p.board = get(obj, "BOARD");
    p.hardware = get(obj, "HARDWARE"); p.fingerprint = get(obj, "FINGERPRINT");
    p.id = get(obj, "ID"); p.display = get(obj, "DISPLAY"); p.serial = get(obj, "SERIAL");
    p.release = get(obj, "RELEASE"); p.incremental = get(obj, "INCREMENTAL");
    p.codename = get(obj, "CODENAME"); p.securityPatch = get(obj, "SECURITY_PATCH");
    std::string sdk = get(obj, "SDK_INT");
    if (!sdk.empty()) { try { p.sdkInt = std::stoi(sdk); } catch (...) {} }

    // Phase 4: optional "native_hooks" object.
    auto nh = obj.object.find("native_hooks");
    if (nh != obj.object.end() && nh->second.isObject) {
        const Value& o = nh->second;
        std::string off = get(o, "uptime_offset_sec");
        if (!off.empty()) { try { p.nativeHooks.uptimeOffsetSec = std::stol(off); } catch (...) {} }
        auto un = o.object.find("uname");
        if (un != o.object.end() && un->second.isObject) {
            p.nativeHooks.unameMachine = get(un->second, "machine");
            p.nativeHooks.unameSysname = get(un->second, "sysname");
        }
        auto pr = o.object.find("props");
        if (pr != o.object.end() && pr->second.isObject) {
            for (const auto& kv : pr->second.object) {
                if (!kv.second.isObject) p.nativeHooks.props[kv.first] = kv.second.str;
            }
        }
        auto gpuObj = o.object.find("gpu");
        if (gpuObj != o.object.end() && gpuObj->second.isObject) {
            p.nativeHooks.gpu.vendor = get(gpuObj->second, "vendor");
            p.nativeHooks.gpu.renderer = get(gpuObj->second, "renderer");
            p.nativeHooks.gpu.version = get(gpuObj->second, "version");
            p.nativeHooks.gpu.eglVendor = get(gpuObj->second, "egl_vendor");
            p.nativeHooks.gpu.eglVersion = get(gpuObj->second, "egl_version");
        }
        auto telObj = o.object.find("telephony");
        if (telObj != o.object.end() && telObj->second.isObject) {
            p.nativeHooks.telephony.imei = get(telObj->second, "imei");
            p.nativeHooks.telephony.imsi = get(telObj->second, "imsi");
            p.nativeHooks.telephony.iccid = get(telObj->second, "iccid");
            p.nativeHooks.telephony.simOperator = get(telObj->second, "sim_operator");
            p.nativeHooks.telephony.simOperatorName = get(telObj->second, "sim_operator_name");
            p.nativeHooks.telephony.simCountryIso = get(telObj->second, "sim_country_iso");
            p.nativeHooks.telephony.phoneType = get(telObj->second, "phone_type");
        }
        auto idObj = o.object.find("id");
        if (idObj != o.object.end() && idObj->second.isObject) {
            p.nativeHooks.id.androidId = get(idObj->second, "android_id");
            p.nativeHooks.id.macAddress = get(idObj->second, "mac_address");
        }
    }
}

}  // namespace openspoof_json

inline Config LoadConfig(const std::string& path) {
    Config cfg;
    std::ifstream f(path);
    if (!f) return cfg;
    std::stringstream ss;
    ss << f.rdbuf();
    openspoof_json::Value root;
    openspoof_json::Parser parser(ss.str());
    if (!parser.parse(root)) return cfg;

    auto pit = root.object.find("profiles");
    if (pit != root.object.end() && pit->second.isObject) {
        for (const auto& kv : pit->second.object) {
            if (!kv.second.isObject) continue;
            Profile p;
            openspoof_json::fillProfile(p, kv.second);
            cfg.profiles[kv.first] = p;
        }
    }
    auto ait = root.object.find("apps");
    if (ait != root.object.end() && ait->second.isObject) {
        for (const auto& kv : ait->second.object) {
            if (!kv.second.isObject) cfg.apps[kv.first] = kv.second.str;
        }
    }
    return cfg;
}
