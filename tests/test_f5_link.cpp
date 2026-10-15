// OpenSpoof — F5 link test
//
// Confirms that the F5 helpers (FLAG_SECURE stripper, locale
// override) are exported and callable as the documented symbols.
// The host clang cannot attach to a real JNIEnv, so this test
// is limited to a symbol-presence check: it includes f5_art_hooks.cpp
// directly, then takes the addresses of the public entry points.
// A real device test would attach a JNIEnv and call
// InstallFlagSecureHookF5 with a synthetic android.view.Window
// class loaded; that belongs in a device-test suite, not here.
//
// Build:
//   clang++ -std=c++17 -I src tests/test_f5_link.cpp \
//       src/f5_art_hooks.cpp src/config.h -o /tmp/openspoof-tests/test_f5_link
//   /tmp/openspoof-tests/test_f5_link

#include "hooks.h"

#include <cstdio>
#include <cstdlib>

int main() {
    bool ok = true;
    auto* p1 = &openspoof::InstallFlagSecureHookF5;
    auto* p2 = &openspoof::ApplyLocaleOverrideF5;
    if (p1 == nullptr) { std::printf("FAIL: InstallFlagSecureHookF5 missing\n"); ok = false; }
    if (p2 == nullptr) { std::printf("FAIL: ApplyLocaleOverrideF5 missing\n"); ok = false; }
    if (ok) {
        std::printf("PASS: F5 entry points present\n");
        std::printf("      InstallFlagSecureHookF5 @ %p\n", (void*)p1);
        std::printf("      ApplyLocaleOverrideF5   @ %p\n", (void*)p2);
    }
    return ok ? 0 : 1;
}
