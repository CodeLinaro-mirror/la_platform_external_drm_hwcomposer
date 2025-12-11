/*
 * Copyright (C) 2025 The Android Open Source Project
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

#include "HwcBufferCache.h"

#include "drm/DrmDevice.h"
#include "drm/DrmDisplayPipeline.h"
#include "drm/DrmFbImporter.h"
#include "hwc/HwcDisplay.h"
#include "utils/log.h"

namespace android::drm_hwcomposer {

HwcBufferCache::HwcBufferCache(HwcDisplay* parent_display)
    : parent_display_(parent_display) {
}

void HwcBufferCache::SetSlot(int32_t slot_id,
                             const std::optional<BufferInfo>& bi) {
  if (bi == std::nullopt) {
    slots_.erase(slot_id);
    return;
  }
  slots_[slot_id] = {
      .bi = bi.value(),
      .fb = {},
  };
  bool success = ImportFb(slots_[slot_id]);
  ALOGE_IF(!success, "Unable to create framebuffer object for layer %p slot %d",
           this, slot_id);
}

std::optional<BufferInfo> HwcBufferCache::GetBufferInfo(int32_t slot_id) const {
  auto slot_entry = GetSlot(slot_id);
  if (slot_entry == std::nullopt) {
    return std::nullopt;
  }
  return slot_entry->bi;
}

std::shared_ptr<DrmFbIdHandle> HwcBufferCache::GetFb(int32_t slot_id) const {
  auto slot_entry = GetSlot(slot_id);
  if (slot_entry == std::nullopt) {
    return nullptr;
  }
  return slot_entry->fb;
}

void HwcBufferCache::Clear() {
  slots_.clear();
}

std::shared_ptr<DrmFbIdHandle> HwcBufferCache::ImportFb(
    const HwcDisplay* display, BufferInfo& bi) {
  if (display->IsInHeadlessMode()) {
    return nullptr;
  }
  auto& fb_importer = display->GetPipe().device->GetDrmFbImporter();
  return fb_importer.GetOrCreateFbId(&bi);
}

bool HwcBufferCache::ImportFb(BufferSlot& slot) const {
  if (parent_display_->IsInHeadlessMode()) {
    return true;
  }
  if (slot.fb == nullptr) {
    slot.fb = ImportFb(parent_display_, slot.bi);
  }
  return slot.fb != nullptr;
}

std::optional<HwcBufferCache::BufferSlot> HwcBufferCache::GetSlot(
    int32_t slot_id) const {
  auto it = slots_.find(slot_id);
  if (it == slots_.end()) {
    return std::nullopt;
  }
  return it->second;
}

}  // namespace android::drm_hwcomposer