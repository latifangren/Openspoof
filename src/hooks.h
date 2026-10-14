// OpenSpoof Phase 4 — native hook installer (Dobby inline hook).
//
// Installs per-app native hooks into the current process. Call from
// preAppSpecialize, only for packages with a matching profile.
// Safe no-op when built without Dobby (-DHAVE_DOBBY not defined).
#pragma once

#include "config.h"

namespace openspoof {

bool InstallNativeHooks(const NativeHooks& hooks);

}  // namespace openspoof
