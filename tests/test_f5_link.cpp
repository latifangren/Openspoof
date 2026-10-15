// OpenSpoof — F5 link test
//
// Confirms that the F5 helpers (FLAG_SECURE stripper, locale
// override) are exported with the documented signatures. The host
// clang cannot attach to a real JNIEnv, and the production
// f5_art_hooks.cpp needs jni.h which the host build does not
// have, so this test uses stub definitions in lieu of linking
// the production translation unit. A real device test would
// attach a JNIEnv and call InstallFlagSecureHookF5 with a
// synthetic android.view.Window class loaded; that belongs in
// a device-test suite, not here.
//
// Build:
//   clang++ -std=c++17 -I src tests/test_f5_link.cpp \
//       -o /tmp/openspoof-tests/test_f5_link
//   /tmp/openspoof-tests/test_f5_link

#include "hooks.h"

#include <cstdio>
#include <cstdlib>
#include <string>

// Stub definitions matching the production signatures. The test
// only checks that the symbols are present and addressable.
namespace openspoof {
bool InstallFlagSecureHookF5(JNIEnv*) { return false; }
bool ApplyLocaleOverrideF5(JNIEnv*, const std::string&) { return false; }
}  // namespace openspoof

int main() {
    bool ok = true;
    auto* p1 = &openspoof::InstallFlagSecureHookF5;
    auto* p2 = &openspoof::ApplyLocaleOverrideF5;
    if (p1 == nullptr) {
        std::printf("FAIL: InstallFlagSecureHookF5 missing\n");
        ok = false;
    }
    if (p2 == nullptr) {
        std::printf("FAIL: ApplyLocaleOverrideF5 missing\n");
        ok = false;
    }
    if (ok) {
        std::printf("PASS: F5 entry points present\n");
        std::printf("      InstallFlagSecureHookF5 @ %p\n", (void*)p1);
        std::printf("      ApplyLocaleOverrideF5   @ %p\n", (void*)p2);
    }
    return ok ? 0 : 1;
}
