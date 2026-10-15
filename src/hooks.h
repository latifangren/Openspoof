// OpenSpoof Phase 4 — native hook installer (Dobby inline hook).
//
// Installs per-app native hooks into the current process. Call from
// preAppSpecialize, only for packages with a matching profile.
// Safe no-op when built without Dobby (-DHAVE_DOBBY not defined).
#pragma once

#include "config.h"

// JNIEnv is referenced only as a pointer in this header's function
// signatures, so a forward declaration is enough. We still pull in
// the real header when it is available (NDK builds) so callers
// that include this header and then use JNI types do not have to
// repeat the include. Host-only test builds (CI runner) do not
// have jni.h and the forward declaration is sufficient for them.
#if defined(__has_include)
#  if __has_include(<jni.h>)
#    include <jni.h>
#  else
struct JNIEnv;
#  endif
#else
struct JNIEnv;
#endif

#include <string>

namespace openspoof {

bool InstallNativeHooks(const NativeHooks& hooks, const Profile* profile = nullptr);

// Phase 5 — ART-side hooks (GAP #7 FLAG_SECURE, GAP #10 Locale).
// These are JNI-only and require a live JNIEnv. Call from
// postAppSpecialize (env_ is valid there) only for packages that
// opted in via the per-profile config. Safe no-op if the relevant
// Java class is not loaded yet — the helpers clear the pending
// JNI exception and return false, so the rest of the boot path
// is not affected.
bool InstallFlagSecureHookF5(JNIEnv* env);
bool ApplyLocaleOverrideF5(JNIEnv* env, const std::string& tag);

}  // namespace openspoof
