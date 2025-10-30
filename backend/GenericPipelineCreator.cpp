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

#include "backend/GenericPipelineCreator.h"

#include "backend/GenericCompositionPlanner.h"
#include "drm/DrmDisplayPipeline.h"

namespace android::drm_hwcomposer {

GenericPipelineCreator::GenericPipelineCreator(const std::string& name)
    : BackendManager::PipelineCreator(name) {
}

std::unique_ptr<DrmDisplayPipeline> GenericPipelineCreator::CreatePipeline(
    DrmConnector& connector) {
  auto pipeline = DrmDisplayPipeline::CreatePipeline(connector);
  if (pipeline) {
    pipeline->backend = CreateCompositionPlanner();
  }
  return pipeline;
}

std::unique_ptr<CompositionPlanner>
GenericPipelineCreator::CreateCompositionPlanner() {
  return std::make_unique<GenericCompositionPlanner>();
}

// NOLINTNEXTLINE(cppcoreguidelines-avoid-non-const-global-variables)
static GenericPipelineCreator generic_pipeline_creator("generic");

}  // namespace android::drm_hwcomposer
