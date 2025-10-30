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

#pragma once

#include "backend/BackendManager.h"
#include "backend/CompositionPlanner.h"

namespace android::drm_hwcomposer {

// Implement the PipelineCreator interface by creating a DrmDisplayPipeline
// using generic heuristics on top of upstream drm uAPI.
class GenericPipelineCreator : public BackendManager::PipelineCreator {
 public:
  explicit GenericPipelineCreator(const std::string& name);
  std::unique_ptr<DrmDisplayPipeline> CreatePipeline(
      DrmConnector& connector) override;

 protected:
  // Create a new GenericCompositionPlanner by default. Classes can override
  // this to create different composition planners while using the default logic
  // to create a DrmDisplayPipeline.
  virtual std::unique_ptr<CompositionPlanner> CreateCompositionPlanner();
};

}  // namespace android::drm_hwcomposer
