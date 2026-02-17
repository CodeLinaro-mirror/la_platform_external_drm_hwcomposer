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
#include "GenericLayerMapperCompositionPlanner.h"

#include <vector>

#include "compositor/FlatteningController.h"
#include "compositor/LayerData.h"
#include "drm/DrmPlane.h"
#include "hwc/HwcDisplay.h"
#include "hwc/HwcLayer.h"
#include "utils/log.h"

namespace android::drm_hwcomposer {

namespace {

std::vector<LayerMapping> CreateZOrderedLayerMapping(
    const std::vector<const HwcLayer*>& layers) {
  std::vector<LayerMapping> mapping;
  mapping.reserve(layers.size());
  for (const auto* layer : layers) {
    mapping.push_back({layer, CompositionType::kInvalid});
  }

  // Sort from lowest to highest Z.
  // This is likely a no-op as the layers are passed in sorted.
  std::stable_sort(mapping.begin(), mapping.end(),
                   [](const LayerMapping& lhs, const LayerMapping& rhs) {
                     return lhs.layer->GetZOrder() < rhs.layer->GetZOrder();
                   });

  return mapping;
}

CompositionPlanner::CompositionTypeMap ToCompositionTypes(
    const std::vector<LayerMapping>& layers) {
  CompositionPlanner::CompositionTypeMap composition_types;
  for (const auto& [layer, composition_type] : layers) {
    composition_types[layer] = composition_type;
  }
  return composition_types;
}

// Convert all undetermined layers into client layers.
std::vector<LayerMapping> InvalidToClientLayers(
    const std::vector<LayerMapping>& layers) {
  std::vector<LayerMapping> new_mapping = layers;
  for (auto& [_, composition_type] : new_mapping) {
    if (composition_type == CompositionType::kInvalid) {
      composition_type = CompositionType::kClient;
    }
  }

  return new_mapping;
}

CompositionPlanner::ValidatedComposition CreateValidatedComposition(
    const std::vector<LayerMapping>& layers) {
  CompositionPlanner::ValidatedComposition validated_composition = {
      .composition_types = ToCompositionTypes(InvalidToClientLayers(layers))};
  return validated_composition;
}
}  // namespace

CompositionPlanner::ValidatedComposition
GenericLayerMapperCompositionPlanner::ValidateDisplay(
    const HwcDisplay* display) {
  // An element with higher stack order is always in front of an element with a
  // lower stack order.
  std::vector<LayerMapping> layers = CreateZOrderedLayerMapping(
      display->GetOrderLayersByZPos());

  // Early check and exit for flattened scenes.
  const FlatteningController* flatcon = display->GetFlatCon();
  if (flatcon != nullptr && flatcon->ShouldFlatten()) {
    return CreateFlattenedComposition(layers, FlattenReason::kStaticScene);
  }

  if (display->CtmByGpu()) {
    return CreateFlattenedComposition(layers, FlattenReason::kCtmWithOffset);
  }

  // If there's only one layer, no need to use the GPU to client composite.
  if (layers.size() == 1 &&
      !MustBeClientComposited(display, layers.front().layer)) {
    layers.front().composition_type = CompositionType::kDevice;
    return ValidatedComposition{
        .composition_types = ToCompositionTypes(layers)};
  }

  layers = MapAllClientCompositionRequiredLayers(display, layers);

  // TODO: cursor mapper
  // TODO: Layer caching mapper
  // TODO: Underlay mapper

  // Convert all unmapped layers into client composited layers.
  ValidatedComposition validated_composition = CreateValidatedComposition(
      layers);
  bool success = display->TestComposition(validated_composition);
  validated_composition.composition_plan.reset();

  // TODO: Fallbacks

  validated_composition.cursor_plane_validated = false;

  return validated_composition;
}

bool GenericLayerMapperCompositionPlanner::MustBeClientComposited(
    const HwcDisplay* display, const HwcLayer* layer) {
  // As per Composition.aidl, if Composition is CLIENT, HWC is not allowed to
  // request a change.
  return !HardwareSupportsLayerType(layer->GetSfType()) ||
         !layer->IsLayerUsableAsDevice() || display->CtmByGpu() ||
         (layer->GetLayerData().pi.RequireScalingOrPhasing() &&
          display->ForcedScalingWithGpu());
}

bool GenericLayerMapperCompositionPlanner::HardwareSupportsLayerType(
    CompositionType comp_type) {
  return comp_type == CompositionType::kDevice ||
         comp_type == CompositionType::kCursor;
}

CompositionPlanner::ValidatedComposition
GenericLayerMapperCompositionPlanner::CreateFlattenedComposition(
    const std::vector<LayerMapping>& layers,
    FlattenReason flatten_reason) const {
  return ValidatedComposition{.composition_types = ToCompositionTypes(
                                  force_client_composition_mapper_.AssignLayers(
                                      layers)),
                              .composition_plan = nullptr,
                              .flatten_reason = flatten_reason};
}

std::vector<LayerMapping>
GenericLayerMapperCompositionPlanner::MapAllClientCompositionRequiredLayers(
    const HwcDisplay* display, const std::vector<LayerMapping>& layers) {
  std::vector<LayerMapping> new_layers = layers;
  for (auto& [layer, composition_type] : new_layers) {
    if (MustBeClientComposited(display, layer)) {
      composition_type = CompositionType::kClient;
    }
  }

  return new_layers;
}

}  // namespace android::drm_hwcomposer
