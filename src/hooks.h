// OpenSpoof Phase 4 — native hook installer (Dobby inline hook).
//
// Installs per-app native hooks into the current process. Call from
// preAppSpecialize, only for packages with a matching profile.
// Safe no-op when built without Dobby (-DHAVE_DOBBY not defined).
#pragma once

#include "config.h"

#include <jni.h>

#include <string>

namespace openspoof {

bool InstallNativeHooks(const NativeHooks& hooks);

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
