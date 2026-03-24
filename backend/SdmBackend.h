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

#pragma once

#include <memory>

#include "backend/BackendManager.h"
#include "drm/DrmDisplayPipeline.h"

namespace sdm {
class SocketHandler;
class DebugCallbackIntf;
class HWCBufferAllocator;
class HWCSocketHandler;
class SDMCompositorCbIntf;
class SDMDisplayCapsIntf;
class SDMDisplayDrawCycleIntf;
class SDMDisplayLayerBuilderIntf;
class SDMDisplaySettingsIntf;
class SDMDisplaySideBandIntf;
class SDMDisplayLifeCycleIntf;
class SDMSideBandCompositorCbIntf;
}  // namespace sdm

namespace android {
namespace drm_hwcomposer {

// SdmBackend interfaces with Qualcomm's SDM library to implement a
// drm_hwcomposer Backend.
class SdmBackend : public BackendManager::Backend {
 public:
  SdmBackend();
  virtual ~SdmBackend();
  std::unique_ptr<DrmDisplayPipeline> CreatePipeline(
      DrmConnector& connector) override;
  std::unique_ptr<BufferInfoGetter> CreateBufferInfoGetter() override;

  bool Init() override;

 private:
  using SdmDisplayId = uint64_t;

  std::optional<SdmDisplayId> GetBuiltinDisplayId();

  // Interfaces used to interact with SDM. These are created by SDM and returned
  // to the SdmBackend.
  std::shared_ptr<sdm::SDMDisplayLifeCycleIntf> life_cycle_intf_;
  std::shared_ptr<sdm::SDMDisplayLayerBuilderIntf> layer_builder_intf_;
  std::shared_ptr<sdm::SDMDisplayCapsIntf> display_caps_intf_;
  std::shared_ptr<sdm::SDMDisplayDrawCycleIntf> draw_cycle_intf_;
  std::shared_ptr<sdm::SDMDisplaySettingsIntf> settings_intf_;
  std::shared_ptr<sdm::SDMDisplaySideBandIntf> sideband_intf_;

  // Interfaces that need to be implemented by us and passed to SDM. These are
  // created by the SdmBackend and passed to different SDM interfaces as needed.
  std::unique_ptr<sdm::HWCBufferAllocator> buffer_allocator_;
  std::unique_ptr<sdm::HWCSocketHandler> socket_handler_;
  std::unique_ptr<sdm::DebugCallbackIntf> debug_callback_;
  std::unique_ptr<sdm::SDMCompositorCbIntf> callback_interface_;
  std::unique_ptr<sdm::SDMSideBandCompositorCbIntf> sideband_callbacks_;

  static SdmBackend instance;
};

}  // namespace drm_hwcomposer
}  // namespace android
