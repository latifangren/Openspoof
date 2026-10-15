// OpenSpoof — config parser regression tests
//
// The C++ source uses the header-only JSON parser in src/config.h.
// We test it the same way the parser itself is used at runtime:
// build a small JSON string, hand it to the parser, and check the
// resulting Config / Profile / NativeHooks.
//
// Build:
//   clang++ -std=c++17 -I src -I tests \
//       tests/test_config_parser.cpp src/config.h \
//       -o /tmp/test_config_parser
//   /tmp/test_config_parser
//
// Exit code 0 == all assertions passed. Any non-zero exit == at
// least one failure. The script run by tests/run.sh uses this
// contract.

#include "config.h"

#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>

static int g_pass = 0;
static int g_fail = 0;

#define EXPECT(cond, msg) do {                                  \
    if (cond) { ++g_pass;                                       \
        std::printf("PASS: %s\n", msg);                         \
    } else { ++g_fail;                                           \
        std::printf("FAIL: %s (line %d)\n", msg, __LINE__);      \
    }                                                            \
} while (0)

static openspoof_json::Value parseString(const std::string& s) {
    openspoof_json::Value v;
    openspoof_json::Parser p(s);
    bool ok = p.parse(v);
    EXPECT(ok, "json parse ok");
    return v;
}

static Config loadFromString(const std::string& s) {
    // We reuse LoadConfig's file path, but the parser is also
    // exported via the Parser class. The simplest way to load
    // arbitrary text is to write to a temp file, but that pulls
    // in <fstream> at the call site. Instead, this helper
    // re-implements the public Loader using the public Value
    // type. The production code path is the same Parser::parse
    // call followed by the same object lookups.
    Config cfg;
    openspoof_json::Value root;
    openspoof_json::Parser p(s);
    if (!p.parse(root)) return cfg;
    auto pit = root.object.find("profiles");
    if (pit != root.object.end() && pit->second.isObject) {
        for (const auto& kv : pit->second.object) {
            if (!kv.second.isObject) continue;
            Profile prof;
            openspoof_json::fillProfile(prof, kv.second);
            cfg.profiles[kv.first] = prof;
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

static void testRefreshRate() {
    const char* json = R"({
      "apps": {},
      "profiles": {
        "demo": {
          "BRAND": "google",
          "FINGERPRINT": "google/caiman/caiman:17/x/y:user/release-keys",
          "native_hooks": {
            "refresh_rate_hz": 144
          }
        }
      }
    })";
    Config cfg = loadFromString(json);
    EXPECT(cfg.profiles.count("demo") == 1, "demo profile loaded");
    auto& p = cfg.profiles["demo"];
    EXPECT(p.nativeHooks.refreshRateHz == 144, "refresh_rate_hz parsed to 144");
    EXPECT(p.nativeHooks.enabled(), "native hooks enabled when refresh_rate set");
}

static void testRefreshRateRangeClamp() {
    // Out-of-range values must be rejected.
    const char* json = R"({
      "apps": {},
      "profiles": {
        "demo": {
          "BRAND": "google",
          "native_hooks": {
            "refresh_rate_hz": 9999
          }
        }
      }
    })";
    Config cfg = loadFromString(json);
    EXPECT(cfg.profiles["demo"].nativeHooks.refreshRateHz == 0,
           "refresh_rate_hz=9999 is rejected (out of range)");
}

static void testFlagSecureBool() {
    const char* json = R"({
      "apps": {},
      "profiles": {
        "demo": {
          "BRAND": "google",
          "native_hooks": {
            "disable_flag_secure": true
          }
        }
      }
    })";
    Config cfg = loadFromString(json);
    EXPECT(cfg.profiles["demo"].nativeHooks.disableFlagSecure,
           "disable_flag_secure=true parsed as bool");
}

static void testFlagSecureMissing() {
    const char* json = R"({
      "apps": {},
      "profiles": {
        "demo": { "BRAND": "google" }
      }
    })";
    Config cfg = loadFromString(json);
    EXPECT(!cfg.profiles["demo"].nativeHooks.disableFlagSecure,
           "disable_flag_secure absent defaults to false");
    EXPECT(!cfg.profiles["demo"].nativeHooks.enabled(),
           "native hooks not enabled when only Build fields are set");
}

static void testLanguage() {
    const char* json = R"({
      "apps": {},
      "profiles": {
        "demo": {
          "BRAND": "google",
          "native_hooks": {
            "language": "pt-BR"
          }
        }
      }
    })";
    Config cfg = loadFromString(json);
    EXPECT(cfg.profiles["demo"].nativeHooks.languageTag == "pt-BR",
           "language parsed");
}

static void testLanguageRejectsSpace() {
    // BCP-47 forbids spaces; the parser should drop such input.
    const char* json = R"({
      "apps": {},
      "profiles": {
        "demo": {
          "BRAND": "google",
          "native_hooks": {
            "language": "en US"
          }
        }
      }
    })";
    Config cfg = loadFromString(json);
    EXPECT(cfg.profiles["demo"].nativeHooks.languageTag.empty(),
           "language with space is rejected");
}

static void testCombinedProfile() {
    // All three new fields together.
    const char* json = R"({
      "apps": { "com.example.game": "demo" },
      "profiles": {
        "demo": {
          "BRAND": "google",
          "FINGERPRINT": "google/caiman/caiman:17/x/y:user/release-keys",
          "native_hooks": {
            "refresh_rate_hz": 120,
            "language": "zh-Hans-CN",
            "disable_flag_secure": true,
            "gpu": { "vendor": "ARM", "renderer": "Mali-G715" }
          }
        }
      }
    })";
    Config cfg = loadFromString(json);
    auto& p = cfg.profiles["demo"];
    EXPECT(p.nativeHooks.refreshRateHz == 120, "refresh=120");
    EXPECT(p.nativeHooks.languageTag == "zh-Hans-CN", "lang zh");
    EXPECT(p.nativeHooks.disableFlagSecure, "flag secure");
    EXPECT(p.nativeHooks.gpu.renderer == "Mali-G715", "gpu still parsed");
    EXPECT(p.nativeHooks.enabled(), "hooks enabled");
    EXPECT(cfg.apps["com.example.game"] == "demo", "app->profile map");
}

int main() {
    testRefreshRate();
    testRefreshRateRangeClamp();
    testFlagSecureBool();
    testFlagSecureMissing();
    testLanguage();
    testLanguageRejectsSpace();
    testCombinedProfile();
    std::printf("---\nTotal: %d  Pass: %d  Fail: %d\n",
                g_pass + g_fail, g_pass, g_fail);
    return g_fail == 0 ? 0 : 1;
}
