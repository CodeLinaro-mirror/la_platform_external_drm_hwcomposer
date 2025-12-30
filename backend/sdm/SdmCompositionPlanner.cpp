/*
 * Copyright (C) 2026 The Android Open Source Project
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

#include "backend/sdm/SdmCompositionPlanner.h"

#include "backend/sdm/SnapAllocHandle.h"
#include "backend/sdm/sdm_error.h"
#include "compositor/LayerData.h"
#include "hwc/HwcDisplay.h"
#include "utils/log.h"

#include <sdm_interface_factory_v2.h>

namespace android::drm_hwcomposer {
namespace {
using sdm_error::ErrorToString;

// SDMDisplayLayerBuilderIntf::SetLayerCompositionType takes an int32_t but the
// acceptable values are not defined or documented. QTI AidlComposerClient shows
// that the value passed in from HWC3 is sent through directly. Redefine the
// supported HWC3 AIDL values here.
enum class SDMLayerComposition : int32_t {
  kInvalid = 0,
  kClient = 1,
  kDevice = 2,
  kSolidColor = 3,
  kCursor = 4
};
}  // namespace

SdmCompositionPlanner::SdmCompositionPlanner(
    uint64_t sdm_display_id, sdm::SDMDisplayLayerBuilderIntf* layer_intf,
    sdm::SDMDisplayDrawCycleIntf* draw_cycle_intf)
    : sdm_display_id_(sdm_display_id),
      layer_intf_(layer_intf),
      draw_cycle_intf_(draw_cycle_intf) {
}

auto SdmCompositionPlanner::ValidateDisplay(const HwcDisplay* display)
    -> ValidatedComposition {
  // Create/destroy new/old layers and update the layer properties for each
  // layer.
  UpdateLayerMapping(*display);
  // Plumb each layer's properties into the corresponding SDM layers.
  for (const auto& [hwc_layer_id, layer] : display->layers()) {
    UpdateLayer(hwc_layer_id, layer);
  }

  // TODO: validate_only is hardcoded right now. Handle validateOrPresent case.
  const bool validate_only = true;

  std::shared_ptr<sdm::Fence> out_retire_fence;
  // num_types and num_requests are not used.
  uint32_t out_num_types = 0;
  uint32_t out_num_requests = 0;

  // This will always be true because validate_only is true.
  bool out_needs_commit = false;
  auto display_error = draw_cycle_intf_->CommitOrPrepare(sdm_display_id_,
                                                         validate_only,
                                                         &out_retire_fence,
                                                         &out_num_types,
                                                         &out_num_requests,
                                                         &out_needs_commit);
  ALOGE_IF(display_error != sdm::kErrorNone, "CommitOrPrepare failed: %s",
           ErrorToString(display_error).c_str());

  display_error = draw_cycle_intf_->AcceptDisplayChanges(sdm_display_id_);
  ALOGE_IF(display_error != sdm::kErrorNone, "AcceptDisplayChanges failed: %s",
           ErrorToString(display_error).c_str());

  // TODO: Read the composition types back from SDM. Right now assuming all is
  // GPU.
  return GetFlattenedComposition(display->GetOrderLayersByZPos(),
                                 FlattenReason::kNone);
}

void SdmCompositionPlanner::UpdateLayerMapping(const HwcDisplay& display) {
  const std::map<ILayerId, HwcLayer>& hwc_layers = display.layers();
  bool layers_updated = false;

  // Create new SDM layers if needed.
  for (const auto& [hwc_layer_id, layer] : hwc_layers) {
    if (layer_mappings_.count(hwc_layer_id) > 0) {
      continue;
    }
    SDMLayerId new_sdm_layer_id = 0;
    auto display_error = layer_intf_->CreateLayer(sdm_display_id_,
                                                  &new_sdm_layer_id);
    if (display_error != sdm::kErrorNone) {
      ALOGE("CreateLayer failed: %s", ErrorToString(display_error).c_str());
      continue;
    }
    layers_updated = true;
    layer_mappings_[hwc_layer_id] = new_sdm_layer_id;
  }

  // New layers have been added, and stale layers have not yet been destroyed.
  // If the sizes of these structures are not the same, then there are some
  // stale layers that need to be deleted.
  if (layer_mappings_.size() > hwc_layers.size()) {
    // This needs to be called to ensure async commit job is complete before
    // destroying layers.
    draw_cycle_intf_->WaitForDrawCycleToComplete(sdm_display_id_);
    // Iterate through the HwcLayer->SdmLayer mapping and delete SDM layers if
    // needed.
    for (auto it = layer_mappings_.begin(); it != layer_mappings_.end();) {
      if (hwc_layers.count(it->first) > 0) {
        ++it;
        continue;
      }

      auto display_error = layer_intf_->DestroyLayer(sdm_display_id_,
                                                     it->second);
      ALOGE_IF(display_error != sdm::kErrorNone, "DestroyLayer failed: %s",
               ErrorToString(display_error).c_str());
      it = layer_mappings_.erase(it);
      layers_updated = true;
    }
  }

  // After layers have been added or removed, call this to ensure that internal
  // layer bookkeeping is updated.
  if (layers_updated) {
    draw_cycle_intf_->LayerStackUpdated(sdm_display_id_);
  }
}

void SdmCompositionPlanner::UpdateLayer(ILayerId hwc_layer_id,
                                        const HwcLayer& layer) {
  if (layer_mappings_.count(hwc_layer_id) == 0) {
    ALOGE("couldn't find sdm layer for hwc layer id: %d", (int)hwc_layer_id);
    return;
  }
  int64_t sdm_layer_id = layer_mappings_[hwc_layer_id];
  const LayerData& layer_data = layer.GetLayerData();

  // Update all relevant layer properties.
  UpdateLayerBuffer(sdm_layer_id, layer_data);
  UpdateLayerBlendMode(sdm_layer_id, layer_data);
  UpdateLayerDisplayFrame(sdm_layer_id, layer_data);
  UpdateLayerAlpha(sdm_layer_id, layer_data);
  UpdateLayerSourceCrop(sdm_layer_id, layer_data);
  UpdateLayerTransform(sdm_layer_id, layer_data);
  UpdateZOrder(sdm_layer_id, layer.GetZOrder());
  UpdateCompositionType(sdm_layer_id, layer.GetSfType());

  // TODO: damage and visible region, and others.
}

void SdmCompositionPlanner::UpdateLayerBuffer(SDMLayerId sdm_layer_id,
                                              const LayerData& layer_data) {
  if (!layer_data.bi.has_value()) {
    return;
  }
  if (layer_data.bi->fds_shared == nullptr) {
    ALOGE("layer_data.fds_shared is null");
    return;
  }
  std::shared_ptr<SnapAllocHandle>
      snap_handle = std::static_pointer_cast<SnapAllocHandle>(
          layer_data.bi->fds_shared);

  shared_ptr<sdm::Fence> acquire_fence;
  if (layer_data.acquire_fence != nullptr) {
    acquire_fence = sdm::Fence::Create(DupFd(layer_data.acquire_fence),
                                       "acquire_fence");
  }
  auto display_error = layer_intf_->SetLayerBuffer(sdm_display_id_,
                                                   sdm_layer_id,
                                                   snap_handle->GetSnapHandle(),
                                                   acquire_fence);
  ALOGE_IF(display_error != sdm::kErrorNone, "SetLayerBuffer failed: %s",
           ErrorToString(display_error).c_str());
}

void SdmCompositionPlanner::UpdateLayerBlendMode(SDMLayerId sdm_layer_id,
                                                 const LayerData& layer_data) {
  if (!layer_data.bi) {
    return;
  }
  sdm::LayerBlending sdm_blend_mode = sdm::kBlendingSkip;
  switch (layer_data.bi->blend_mode) {
    case BufferBlendMode::kNone:
      sdm_blend_mode = sdm::kBlendingOpaque;
      break;
    case BufferBlendMode::kPreMult:
      sdm_blend_mode = sdm::kBlendingPremultiplied;
      break;
    case BufferBlendMode::kCoverage:
      sdm_blend_mode = sdm::kBlendingCoverage;
      break;
    case BufferBlendMode::kUndefined:
      ALOGE("BufferBlendMode::kUndefined for layer.");
      return;
  }
  auto display_error = layer_intf_->SetLayerBlendMode(sdm_display_id_,
                                                      sdm_layer_id,
                                                      sdm_blend_mode);
  ALOGE_IF(display_error != sdm::kErrorNone, "SetLayerBlendMode failed: %s",
           ErrorToString(display_error).c_str());
}

void SdmCompositionPlanner::UpdateLayerDisplayFrame(
    SDMLayerId sdm_layer_id, const LayerData& layer_data) {
  if (!layer_data.pi.display_frame.i_rect.has_value()) {
    return;
  }
  sdm::SDMRect frame{.left = layer_data.pi.display_frame.i_rect->left,
                     .top = layer_data.pi.display_frame.i_rect->top,
                     .right = layer_data.pi.display_frame.i_rect->right,
                     .bottom = layer_data.pi.display_frame.i_rect->bottom};
  auto display_error = layer_intf_->SetLayerDisplayFrame(sdm_display_id_,
                                                         sdm_layer_id, frame);
  ALOGE_IF(display_error != sdm::kErrorNone, "SetLayerDisplayFrame failed: %s",
           ErrorToString(display_error).c_str());
}

void SdmCompositionPlanner::UpdateLayerAlpha(SDMLayerId sdm_layer_id,
                                             const LayerData& layer_data) {
  auto display_error = layer_intf_->SetLayerPlaneAlpha(sdm_display_id_,
                                                       sdm_layer_id,
                                                       layer_data.pi.alpha);
  ALOGE_IF(display_error != sdm::kErrorNone, "SetLayerPlaneAlpha failed: %s",
           ErrorToString(display_error).c_str());
}

void SdmCompositionPlanner::UpdateLayerSourceCrop(SDMLayerId sdm_layer_id,
                                                  const LayerData& layer_data) {
  if (!layer_data.pi.source_crop.f_rect.has_value()) {
    return;
  }

  // Use ceil/floor to get an int rect per the documentation for sourceCrop:
  // https://android.googlesource.com/platform/hardware/interfaces/+/refs/heads/main/graphics/composer/aidl/android/hardware/graphics/composer3/LayerCommand.aidl
  sdm::SDMRect crop{.left = (int)ceilf(layer_data.pi.source_crop.f_rect->left),
                    .top = (int)ceilf(layer_data.pi.source_crop.f_rect->top),
                    .right = (int)floorf(
                        layer_data.pi.source_crop.f_rect->right),
                    .bottom = (int)floorf(
                        layer_data.pi.source_crop.f_rect->bottom)};
  auto display_error = layer_intf_->SetLayerSourceCrop(sdm_display_id_,
                                                       sdm_layer_id, crop);
  ALOGE_IF(display_error != sdm::kErrorNone, "SetLayerSourceCrop failed: %s",
           ErrorToString(display_error).c_str());
}

void SdmCompositionPlanner::UpdateLayerTransform(SDMLayerId sdm_layer_id,
                                                 const LayerData& layer_data) {
  int32_t sdm_transform = sdm::SDMTransform::TRANSFORM_NONE;
  if (layer_data.pi.transform.hflip) {
    sdm_transform |= sdm::SDMTransform::TRANSFORM_FLIP_H;
  }
  if (layer_data.pi.transform.vflip) {
    sdm_transform |= sdm::SDMTransform::TRANSFORM_FLIP_V;
  }
  if (layer_data.pi.transform.rotate90) {
    sdm_transform |= sdm::SDMTransform::TRANSFORM_ROT_90;
  }
  auto display_error = layer_intf_
                           ->SetLayerTransform(sdm_display_id_, sdm_layer_id,
                                               static_cast<sdm::SDMTransform>(
                                                   sdm_transform));

  ALOGE_IF(display_error != sdm::kErrorNone, "SetLayerTransform failed: %s",
           ErrorToString(display_error).c_str());
}

void SdmCompositionPlanner::UpdateZOrder(SDMLayerId sdm_layer_id,
                                         uint32_t z_order) {
  auto display_error = layer_intf_->SetLayerZOrder(sdm_display_id_,
                                                   sdm_layer_id, z_order);
  ALOGE_IF(display_error != sdm::kErrorNone, "SetLayerZOrder failed: %s",
           ErrorToString(display_error).c_str());
}

void SdmCompositionPlanner::UpdateCompositionType(
    SDMLayerId sdm_layer_id, CompositionType composition_type) {
  // TODO: Translate to sdm composition type and handle more than just client
  // composition.
  SDMLayerComposition sdm_composition_type = SDMLayerComposition::kClient;
  auto display_error = layer_intf_
                           ->SetLayerCompositionType(sdm_display_id_,
                                                     sdm_layer_id,
                                                     static_cast<int32_t>(
                                                         sdm_composition_type));
  ALOGE_IF(display_error != sdm::kErrorNone,
           "SetLayerCompositionType failed: %s",
           ErrorToString(display_error).c_str());
}
}  // namespace android::drm_hwcomposer