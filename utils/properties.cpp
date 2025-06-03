/*
 * Copyright (C) 2023 The Android Open Source Project
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 *      http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 */

#include "properties.h"

#include <string>

#include <android-base/properties.h>

#include "utils/log.h"

namespace android {

/**
 * @brief Determine if the "Present Not Reliable" property is enabled.
 *
 * @return boolean
 */
auto Properties::IsPresentFenceNotReliable() -> bool {
  return (property_get_bool("ro.vendor.hwc.drm.present_fence_not_reliable",
                            0) != 0);
}

auto Properties::UseConfigGroups() -> bool {
  return (property_get_bool("ro.vendor.hwc.drm.use_config_groups", 0) != 0);
}

auto Properties::InternalDisplayNames() -> std::string {
  char buf[PROPERTY_VALUE_MAX] = {};
  property_get("vendor.hwc.drm.internal_display_names", buf, "");
  return {buf};
}

auto Properties::UseOverlayPlanes() -> bool {
  return (property_get_bool("ro.vendor.hwc.use_overlay_planes", 1) != 0);
}

auto Properties::ScaleWithGpu() -> bool {
  return (property_get_bool("vendor.hwc.drm.scale_with_gpu", 0) != 0);
}

auto Properties::EnableVirtualDisplay() -> bool {
  return (property_get_bool("vendor.hwc.drm.enable_virtual_display", 0) != 0);
}

auto Properties::GetCtmHandling() -> CtmHandling {
  char proptext[PROPERTY_VALUE_MAX];
  constexpr char kDrmOrGpu[] = "DRM_OR_GPU";
  constexpr char kDrmOrIgnore[] = "DRM_OR_IGNORE";
  property_get("vendor.hwc.drm.ctm", proptext, "");
  if (strncmp(proptext, kDrmOrGpu, sizeof(kDrmOrGpu)) == 0) {
    return CtmHandling::kDrmOrGpu;
  }
  if (strncmp(proptext, kDrmOrIgnore, sizeof(kDrmOrIgnore)) == 0) {
    return CtmHandling::kDrmOrIgnore;
  }

  ALOGE_IF(proptext[0] != '\0', "Invalid value for vendor.hwc.drm.ctm: %s",
           proptext);
  // Default value.
  return CtmHandling::kDrmOrGpu;
}

auto Properties::GetBackendOverride() -> std::string {
  char backend_override[PROPERTY_VALUE_MAX];
  property_get("vendor.hwc.backend_override", backend_override, "");
  return {backend_override};
}

auto Properties::GetDevicePath() -> std::string {
  char path_pattern[PROPERTY_VALUE_MAX];
  // Could be a valid path or it can have at the end of it the wildcard %
  // which means that it will try open all devices until an error is met.
  property_get("vendor.hwc.drm.device", path_pattern, "");
  return {path_pattern};
}

}  // namespace android