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

#include "bufferinfo/GrallocBufferCache.h"

#include "bufferinfo/BufferInfoGetter.h"
#include "bufferinfo/GrallocBufferHandle.h"
#include "utils/log.h"

namespace android::drm_hwcomposer {

GrallocBufferCache::GrallocBufferCache(HwcDisplay* parent_display)
    : buffer_cache_(parent_display) {
}
auto GrallocBufferCache::HandleNextBuffer(
    std::optional<buffer_handle_t> raw_handle, SharedFd fence_fd,
    int32_t slot_id) -> std::optional<HwcLayer::Buffer> {
  // raw_handle is specified, so add/update the buffer cache.
  if (raw_handle) {
    // raw_handle is specified, so add/update the slot in the cache.
    auto hwc3 = GrallocBufferHandle::Create(*raw_handle);
    if (!hwc3) {
      ALOGE("Failed to create GrallocBufferHandle.");
      return std::nullopt;
    }
    auto bi = BufferInfoGetter::GetInstance()->GetBoInfo(hwc3->GetHandle());
    // If we fail to get the BufferInfo, just leave the cache alone and log
    // the error.
    if (bi == std::nullopt) {
      ALOGE("Failed to get buffer info for handle %p", raw_handle.value());
      return std::nullopt;
    }

    bi->fds_shared = hwc3;
    buffer_cache_.SetSlot(slot_id, bi);
  }

  auto bi = buffer_cache_.GetBufferInfo(slot_id);
  if (bi == std::nullopt) {
    ALOGE("Failed to get buffer info for slot %d", slot_id);
    return std::nullopt;
  }

  // Cache has possibly been updated above, so populate the Buffer
  // using the contents of the cache.
  return HwcLayer::Buffer{
      .bi = bi.value(),
      .fb = buffer_cache_.GetFb(slot_id),
      .fence = std::move(fence_fd),
  };
}

void GrallocBufferCache::ClearSlot(int32_t slot_id) {
  buffer_cache_.SetSlot(slot_id, std::nullopt);
}

void GrallocBufferCache::ClearSlots() {
  buffer_cache_.Clear();
}

}  // namespace android::drm_hwcomposer