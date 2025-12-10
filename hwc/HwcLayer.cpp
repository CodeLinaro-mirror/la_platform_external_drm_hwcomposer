/*
 * Copyright (C) 2022 The Android Open Source Project
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

#define LOG_TAG "drmhwc"

#include "HwcLayer.h"

#include "drm/DrmDevice.h"
#include "drm/DrmDisplayPipeline.h"
#include "drm/DrmFbImporter.h"
#include "hwc/HwcDisplay.h"
#include "utils/log.h"

namespace android::drm_hwcomposer {

HwcLayer::HwcLayer(HwcDisplay* parent_display)
    : parent_(parent_display), buffer_cache_(parent_display) {
}

void HwcLayer::SetLayerProperties(const LayerProperties& layer_properties) {
  if (layer_properties.slot_buffer) {
    buffer_cache_.SetSlot(layer_properties.slot_buffer->slot_id,
                          layer_properties.slot_buffer->bi);
  }
  if (layer_properties.active_slot) {
    active_slot_id_ = layer_properties.active_slot->slot_id;
    layer_data_.acquire_fence = layer_properties.active_slot->fence;
    auto bi = buffer_cache_.GetBufferInfo(*active_slot_id_);
    ALOGE_IF(!bi, "Internal error: active cache slot is not populated.");
    if (bi) {
      layer_data_.bi = bi.value();
      layer_data_.fb = buffer_cache_.GetFb(*active_slot_id_);
    }
  }
  if (layer_properties.blend_mode) {
    blend_mode_ = layer_properties.blend_mode.value();
  }
  if (layer_properties.color_space) {
    color_space_ = layer_properties.color_space.value();
  }
  if (layer_properties.sample_range) {
    sample_range_ = layer_properties.sample_range.value();
  }
  if (layer_properties.composition_type) {
    sf_type_ = layer_properties.composition_type.value();
  }
  if (layer_properties.display_frame) {
    layer_data_.pi.display_frame = layer_properties.display_frame.value();
  }
  if (layer_properties.alpha) {
    layer_data_.pi.alpha = layer_properties.alpha.value();
  }
  if (layer_properties.source_crop) {
    layer_data_.pi.source_crop = layer_properties.source_crop.value();
  }
  if (layer_properties.transform) {
    layer_data_.pi.transform = layer_properties.transform.value();
  }
  if (layer_properties.z_order) {
    z_order_ = layer_properties.z_order.value();
  }
  if (layer_properties.damage) {
    layer_data_.pi.damage = layer_properties.damage.value();
  }

  if (active_slot_id_.has_value()) {
    PopulateLayerData();
  }
}

void HwcLayer::PopulateLayerData() {
  if (!layer_data_.bi) {
    ALOGE("Internal error: PopulateLayerData called without valid bi.");
    return;
  }

  if (blend_mode_ != BufferBlendMode::kUndefined) {
    layer_data_.bi->blend_mode = blend_mode_;
  }
  if (color_space_ != BufferColorSpace::kUndefined) {
    layer_data_.bi->color_space = color_space_;
  }
  if (sample_range_ != BufferSampleRange::kUndefined) {
    layer_data_.bi->sample_range = sample_range_;
  }
}

void HwcLayer::ClearSlots() {
  buffer_cache_.Clear();
  active_slot_id_.reset();
}

/* Check that the layer has an active slot set, and there is a valid
   * framebuffer in the active slot.
 */
bool HwcLayer::IsLayerUsableAsDevice() const {
  if (!active_slot_id_.has_value()) {
    return false;
  }
  return buffer_cache_.GetFb(*active_slot_id_) != nullptr;
}

uint32_t HwcLayer::GetPixOps() const {
  const auto& df = GetLayerData().pi.display_frame;
  if (df.i_rect.has_value()) {
    return df.i_rect->Width() * df.i_rect->Height();
  }
  return parent_->GetSize().first * parent_->GetSize().second;
}

}  // namespace android::drm_hwcomposer
