/*
 * Copyright (C) 2020 The Android Open Source Project
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

#include <functional>
#include <map>
#include <string>
#include <vector>

#include "CompositionPlanner.h"

namespace android::drm_hwcomposer {

class BackendManager {
 public:
  // PipelineCreator implementations should inherit from this class and provide
  // a name that will not collide with other PipelineCreators.
  class PipelineCreator {
   public:
    // PipelineCreator will register a creator called |name| on construction,
    // and deregister on destruction.
    explicit PipelineCreator(const std::string &name);
    virtual ~PipelineCreator();

    // Create a DrmDisplayPipeline for the given DrmConnector. The
    // implementation will also create the Backend for this DrmDisplayPipeline.
    virtual std::unique_ptr<DrmDisplayPipeline> CreatePipeline(
        DrmConnector &connector) = 0;

   private:
    std::string name_;
  };

  static BackendManager &GetInstance();
  void RegisterBackend(const std::string &name,
                       PipelineCreator *pipeline_creator);
  void UnregisterBackend(const std::string &name);

  std::unique_ptr<DrmDisplayPipeline> CreatePipelineForConnector(
      DrmConnector &connector);

 private:
  PipelineCreator *GetBackendByName(std::string &name);

  BackendManager() = default;

  static const std::vector<std::string> kClientDevices;

  std::map<std::string, PipelineCreator *> available_backends_;
};

}  // namespace android::drm_hwcomposer
