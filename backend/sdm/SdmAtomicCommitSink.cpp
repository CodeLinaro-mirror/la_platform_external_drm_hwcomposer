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

#include "backend/sdm/SdmAtomicCommitSink.h"

#include "backend/sdm/SnapAllocHandle.h"
#include "backend/sdm/sdm_error.h"
#include "compositor/LayerData.h"
#include "compositor/LayerToPlaneJoiningPlan.h"
#include "hwc/HwcDisplay.h"
#include "utils/log.h"

#include <sdm_interface_factory_v2.h>

namespace android::drm_hwcomposer {

using sdm_error::ErrorToString;

SdmAtomicCommitSink::SdmAtomicCommitSink(
    uint64_t sdm_display_id, sdm::SDMDisplayLifeCycleIntf* lifecycle_intf,
    sdm::SDMDisplayDrawCycleIntf* draw_cycle_intf)
    : sdm_display_id_(sdm_display_id),
      lifecycle_intf_(lifecycle_intf),
      draw_cycle_intf_(draw_cycle_intf) {
}

bool SdmAtomicCommitSink::TestAtomicCommit(AtomicCommitArgs& args) {
  ALOGE("SdmAtomicCommitSink::TestAtomicCommit: test_only not supported.");
  return false;
}

std::optional<AtomicCommitResult> SdmAtomicCommitSink::ExecuteAtomicCommit(
    AtomicCommitArgs& args) {
  AtomicCommitResult result = {};
  if (args.active) {
    auto new_power_mode = *args.active ? sdm::SDMPowerMode::POWER_MODE_ON
                                       : sdm::SDMPowerMode::POWER_MODE_OFF;
    auto display_error = lifecycle_intf_->SetPowerMode(sdm_display_id_,
                                                       static_cast<int32_t>(
                                                           new_power_mode));
    if (display_error != sdm::kErrorNone) {
      ALOGE("SetPowerMode failed: %s", ErrorToString(display_error).c_str());
      return std::nullopt;
    }
    power_mode_ = new_power_mode;
  }
  if (args.composition) {
    if (!UpdateClientTarget(*args.composition)) {
      ALOGE("UpdateClientTarget failed");
      return std::nullopt;
    }

    shared_ptr<sdm::Fence> out_retire_fence;
    auto display_error = draw_cycle_intf_->PresentDisplay(sdm_display_id_,
                                                          &out_retire_fence);
    if (display_error != sdm::kErrorNone) {
      ALOGE("PresentDisplay failed: %s", ErrorToString(display_error).c_str());
      return std::nullopt;
    }
    if (out_retire_fence) {
      // Dup to take ownership of new fence.
      result.present_fence = MakeSharedFd(sdm::Fence::Dup(out_retire_fence));
    }
  }
  return result;
}

void SdmAtomicCommitSink::WaitLastFrame() {
  // TODO: Implement this.
}

bool SdmAtomicCommitSink::IsActive() const {
  return power_mode_ != sdm::SDMPowerMode::POWER_MODE_OFF;
}

bool SdmAtomicCommitSink::UpdateClientTarget(
    const LayerToPlaneJoiningPlan& plan) {
  if (!plan.client_z_order.has_value()) {
    // No client target to update.
    return true;
  }
  if (*plan.client_z_order >= plan.plan.size()) {
    ALOGE("client_z_order is out of bounds: %d >= %d", *plan.client_z_order,
          (int)plan.plan.size());
    return false;
  }
  const LayerData* layer_data = &plan.plan[*plan.client_z_order].layer;
  std::shared_ptr<SnapAllocHandle>
      snap_handle = std::static_pointer_cast<SnapAllocHandle>(
          layer_data->bi->fds_shared);
  shared_ptr<sdm::Fence> acquire_fence;
  if (layer_data->acquire_fence != nullptr) {
    acquire_fence = sdm::Fence::Create(DupFd(layer_data->acquire_fence),
                                       "client target acquire_fence");
  }

  // TODO: Plumb the client target damage regions.
  sdm::SDMRegion region{0, {}};
  // TODO: What should these be filled out to?
  int32_t dataspace = 0;
  uint32_t version = 0;
  auto display_error = draw_cycle_intf_
                           ->SetClientTarget(sdm_display_id_,
                                             snap_handle->GetSnapHandle(),
                                             acquire_fence, dataspace, region,
                                             version);
  ALOGE_IF(display_error != sdm::kErrorNone, "SetClientTarget failed: %s",
           ErrorToString(display_error).c_str());
  return display_error == sdm::kErrorNone;
}

}  // namespace android::drm_hwcomposer
