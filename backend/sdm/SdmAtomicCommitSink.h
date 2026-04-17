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

#include <cinttypes>

#include "drm/DrmAtomicCommitSink.h"

#include <core/sdm_types.h>

namespace sdm {
class SDMDisplayDrawCycleIntf;
class SDMDisplayLifeCycleIntf;
}  // namespace sdm

namespace android::drm_hwcomposer {

struct LayerToPlaneJoiningPlan;

// DrmAtomicCommitSink implementation that commits AtomicCommitArgs through SDM.
class SdmAtomicCommitSink : public DrmAtomicCommitSink {
 public:
  SdmAtomicCommitSink(uint64_t sdm_display_id,
                      sdm::SDMDisplayLifeCycleIntf* lifecycle_intf,
                      sdm::SDMDisplayDrawCycleIntf* draw_cycle_intf);

  bool TestAtomicCommit(AtomicCommitArgs& args) override;
  std::optional<AtomicCommitResult> ExecuteAtomicCommit(
      AtomicCommitArgs& args) override;
  void WaitLastFrame() override;
  bool IsActive() const override;

 private:
  // If the LayerToPlaneJoiningPlan has a client target, update the client
  // target in SDM.
  bool UpdateClientTarget(const LayerToPlaneJoiningPlan& plan);

  uint64_t sdm_display_id_;
  sdm::SDMDisplayLifeCycleIntf* lifecycle_intf_;
  sdm::SDMDisplayDrawCycleIntf* draw_cycle_intf_;
  sdm::SDMPowerMode power_mode_ = sdm::SDMPowerMode::POWER_MODE_OFF;
};

}  // namespace android::drm_hwcomposer