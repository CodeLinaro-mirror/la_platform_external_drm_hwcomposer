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

#include "SdmBackend.h"

#include <string>

#include <cutils/properties.h>
#include <ui/GraphicBufferMapper.h>

#include <sdm_compositor_cb_intf.h>
#include <sdm_interface_factory_v2.h>
// Need to #include <sdm_interface_factory_v2.h> first to get the base classes'
// definitions.
#include <hwc_buffer_allocator.h>
#include <hwc_socket_handler.h>

#include "backend/sdm/SdmAtomicCommitSink.h"
#include "backend/sdm/SdmCompositionPlanner.h"
#include "backend/sdm/SdmDebugCallback.h"
#include "backend/sdm/SnapAllocHandle.h"
#include "backend/sdm/sdm_error.h"
#include "bufferinfo/BufferInfoMapperMetadata.h"
#include "bufferinfo/GrallocBufferHandle.h"
#include "compositor/CompositionPlanner.h"
#include "compositor/LayerToPlaneJoiningPlan.h"
#include "drm/DrmAtomicStateManager.h"
#include "drm/DrmConnector.h"
#include "drm/DrmCrtc.h"
#include "drm/DrmDevice.h"
#include "drm/DrmEncoder.h"
#include "drm/DrmPlane.h"
#include "drm/ResourceManager.h"
#include "hwc/HwcDisplay.h"
#include "utils/log.h"

namespace android::drm_hwcomposer {

using sdm_error::ErrorToString;

namespace {

// Overrides BufferInfoMapperMetadata::Import so that the buffer_handle_t
// allocated by snapalloc gets imported for use in SDM. The PrimeFdsSharedBase
// handle on the BufferInfo struct can be cast to a SnapAllocHandle in order to
// access the underlying snapalloc::SnapHandle
class SnapAllocBufferInfoGetter : public BufferInfoMapperMetadata {
 public:
  // Import the buffer_handle_t into this process. The imported buffer_handle_t
  // will be released when the GrallocBufferHandle is destructed.
  std::shared_ptr<GrallocBufferHandle> Import(buffer_handle_t handle) override {
    auto snap_handle = SnapAllocHandle::Create(handle);
    if (snap_handle == nullptr) {
      ALOGE("SnapAllocBufferInfoGetter:: Failed to import buffer handle");
      return nullptr;
    }
    return snap_handle;
  }
};

// TODO: These need to be plumbed back to drm_hwcomposer, or intentionally left
// unimplemented.
class StubCallbackInterface : public sdm::SDMCompositorCbIntf {
 public:
  void OnHotplug(uint64_t in_display, bool in_connected) override {
    ALOGI("*STUB* StubCallbackInterface::OnHotplug(%d, %d)", (int)in_display,
          in_connected);
  }

  void OnRefresh(uint64_t in_display) override {
    ALOGI("*STUB* StubCallbackInterface::OnRefresh");
  }

  void OnVsync(uint64_t in_display, int64_t in_timestamp,
               int32_t in_vsync_period_nanos) override {
    ALOGI("*STUB* StubCallbackInterface::OnVsync");
  }

  void OnSeamlessPossible(uint64_t in_display) override {
    ALOGI("*STUB* StubCallbackInterface::OnSeamlessPossible");
  }

  void OnVsyncIdle(uint64_t in_display) override {
    ALOGI("*STUB* StubCallbackInterface::OnVsyncIdle");
  }

  void OnVsyncPeriodTimingChanged(
      uint64_t in_display,
      const sdm::SDMVsyncPeriodChangeTimeline& timeline) override {
    ALOGI("*STUB* StubCallbackInterface::OnVsyncPeriodTimingChanged");
  }
};

// TODO: Remove this once the logspam has been addressed in SDM. These callbacks
// should not be needed.
class StubSideBandCompositorCallbacks
    : public sdm::SDMSideBandCompositorCbIntf {
 public:
  void NotifyQsyncChange(uint64_t /*display_id*/, bool /*qsync_enabled*/,
                         uint32_t /*refresh_rate*/,
                         uint32_t /*qsync_refresh_rate*/) override {
    ALOGI("*STUB* StubSideBandCompositorCallbacks::NotifyQsyncChange");
  }

  void NotifyCameraSmoothInfo(sdm::SDMCameraSmoothOp /*op*/,
                              int32_t /*fps*/) override {
    ALOGI("*STUB* StubSideBandCompositorCallbacks::NotifyCameraSmoothInfo");
  }

  void NotifyResolutionChange(uint64_t /*display_id*/,
                              sdm::SDMConfigAttributes& /*attr*/) override {
    ALOGI("*STUB* StubSideBandCompositorCallbacks::NotifyResolutionChange");
  }

  void NotifyTUIEventDone(uint32_t /*ret*/, uint32_t /*disp_id*/,
                          sdm::SDMTUIEventType /*type*/) override {
    ALOGI("*STUB* StubSideBandCompositorCallbacks::NotifyTUIEventDone");
  }

  void NotifyIdleStatus(bool /*status*/) override {
    ALOGI("*STUB* StubSideBandCompositorCallbacks::NotifyIdleStatus");
  }

  void NotifyCWBStatus(int32_t /*status*/, void* /*buffer*/) override {
    ALOGI("*STUB* StubSideBandCompositorCallbacks::NotifyCWBStatus");
  }

  void NotifyContentFps(const std::string& /*name*/, int32_t /*fps*/) override {
    ALOGI("*STUB* StubSideBandCompositorCallbacks::NotifyContentFps");
  }

  // qservice
  void OnHdmiHotplug(bool /*connected*/) override {
    ALOGI("*STUB* StubSideBandCompositorCallbacks::OnHdmiHotplug");
  }
  void OnCECMessageReceived(char* /*message*/, int /*len*/) override {
    ALOGI("*STUB* StubSideBandCompositorCallbacks::OnCECMessageReceived");
  }

  // gl color convert callbacks
  void InitColorConvert(uint64_t /*display*/, bool /*secure*/) override {
    ALOGI("*STUB* StubSideBandCompositorCallbacks::InitColorConvert");
  }
  void ColorConvertBlit(uint64_t /*display*/,
                        sdm::ColorConvertBlitContext* /*ctx*/) override {
    ALOGI("*STUB* StubSideBandCompositorCallbacks::ColorConvertBlit");
  }
  void ResetColorConvert(uint64_t /*display*/) override {
    ALOGI("*STUB* StubSideBandCompositorCallbacks::ResetColorConvert");
  }
  void DestroyColorConvert(uint64_t /*display*/) override {
    ALOGI("*STUB* StubSideBandCompositorCallbacks::DestroyColorConvert");
  }

  // Histogram callbacks
  void StartHistogram(uint64_t /*display*/, int /*max_frames*/) override {
    ALOGI("*STUB* StubSideBandCompositorCallbacks::StartHistogram");
  }
  void StopHistogram(uint64_t /*display*/, bool /*teardown*/) override {
    ALOGI("*STUB* StubSideBandCompositorCallbacks::StopHistogram");
  }
  void NotifyHistogram(uint64_t /*display*/, int /*fd*/, uint64_t /*blob_id*/,
                       uint32_t /*panel_width*/,
                       uint32_t /*panel_height*/) override {
    ALOGI("*STUB* StubSideBandCompositorCallbacks::NotifyHistogram");
  }
  std::string DumpHistogram(uint64_t /*display*/) override {
    ALOGI("*STUB* StubSideBandCompositorCallbacks::DumpHistogram");
    return "";
  }
  void CollectHistogram(
      uint64_t /*display*/, uint64_t /*max_frames*/, uint64_t /*timestamp*/,
      int32_t /*samples_size*/[NUM_HISTOGRAM_COLOR_COMPONENTS],
      uint64_t* /*samples*/[NUM_HISTOGRAM_COLOR_COMPONENTS],
      uint64_t* /*numFrames*/) override {
    ALOGI("*STUB* StubSideBandCompositorCallbacks::CollectHistogram");
  }
  sdm::DisplayError GetHistogramAttributes(
      uint64_t /*display*/, int32_t* /*format*/, int32_t* /*dataspace*/,
      uint8_t* /*supported_components*/) override {
    ALOGI("*STUB* StubSideBandCompositorCallbacks::GetHistogramAttributes");
    return sdm::kErrorNone;
  }

  // gl layer stitch
  void StitchLayers(uint64_t /*display*/,
                    sdm::LayerStitchContext* /*params*/) override {
    ALOGI("*STUB* StubSideBandCompositorCallbacks::StitchLayers");
  }
  void InitLayerStitch(uint64_t /*display*/) override {
    ALOGI("*STUB* StubSideBandCompositorCallbacks::InitLayerStitch");
  }
  void DestroyLayerStitch(uint64_t /*display*/) override {
    ALOGI("*STUB* StubSideBandCompositorCallbacks::DestroyLayerStitch");
  }

  sdm::nsecs_t SystemTime(int clock) override {
    ALOGW_IF(clock != SYSTEM_TIME_MONOTONIC,
             "SystemTime not implemented for clock: %d", clock);
    int64_t time_ns = ResourceManager::GetTimeMonotonicNs();
    return static_cast<sdm::nsecs_t>(time_ns);
  }

  int GetDemuraFilePaths(const sdm::GenericPayload& /*in*/,
                         sdm::GenericPayload* /*out*/) override {
    ALOGI("*STUB* StubSideBandCompositorCallbacks::GetDemuraFilePaths");
    return 0;
  }
};

// Find an unused primary plane that's compatible with the crtc and bind it to
// the pipeline.
bool BindPrimaryPlane(DrmDisplayPipeline* pipeline) {
  std::vector<DrmPlane*> compatible_planes;
  for (const auto& plane : pipeline->device->GetPlanes()) {
    if (plane->IsCrtcSupported(*pipeline->crtc->Get())) {
      compatible_planes.push_back(plane.get());
    }
  }
  if (compatible_planes.empty()) {
    ALOGE("Couldn't find any compatible planes for crtc %d",
          pipeline->crtc->Get()->GetId());
    return false;
  }

  // Find an unbound primary plane and bind it.
  for (auto& plane : compatible_planes) {
    if (plane->GetType() != DRM_PLANE_TYPE_PRIMARY) {
      continue;
    }
    pipeline->primary_plane = plane->BindPipeline(pipeline);
    if (pipeline->primary_plane != nullptr) {
      return true;
    }
  }
  return false;
}

// Find the encoder currently associated with this connector. If there is no
// such encoder, pick the first compatible one.
DrmEncoder* GetEncoderForConnector(const DrmConnector& connector) {
  auto encoder_id = connector.GetCurrentEncoderId();
  ALOGI_IF(encoder_id == 0,
           "No current encoder for connector. Will use the first one.");

  for (const auto& enc : connector.GetDev().GetEncoders()) {
    if (enc->GetId() == encoder_id) {
      return enc.get();
    }
    // Use the first compatible one.
    if (encoder_id == 0 && connector.SupportsEncoder(*enc)) {
      return enc.get();
    }
  }
  return nullptr;
}

// Find the crtc currently associated with this encoder. If there is no such
// crtc, pick the first compatible one.
DrmCrtc* GetCrtcForEncoder(DrmDevice& device, DrmEncoder& encoder) {
  auto crtc_id = encoder.GetCurrentCrtcId();
  ALOGI_IF(crtc_id == 0, "No current crtc for encoder. Get first one.");
  for (const auto& crtc : device.GetCrtcs()) {
    if (crtc->GetId() == crtc_id) {
      return crtc.get();
    }
    if (crtc_id == 0 && encoder.SupportsCrtc(*crtc)) {
      return crtc.get();
    }
  }
  return nullptr;
}

const char* kDriverName = "msm_drm";

}  // namespace

// Default base class initialization will associate this backend with the
// msm_drm drm device.
SdmBackend::SdmBackend() : BackendManager::Backend(kDriverName) {
}

SdmBackend::~SdmBackend() = default;

bool SdmBackend::Init() {
  ALOGI("Initializing SdmBackend");
  sdm::SDMInterfaceFactory* factory = sdm::GetSDMInterfaceFactory();
  if (factory == nullptr) {
    ALOGE("Failed to get SdmInterfaceFactoryV2");
    return false;
  }
  // Create all the required interfaces from the SDMInterfaceFactory.
  display_caps_intf_ = factory->CreateCapsIntf();
  if (display_caps_intf_ == nullptr) {
    ALOGE("CreateCapsIntf failed");
    return false;
  }
  draw_cycle_intf_ = factory->CreateDrawCycleIntf();
  if (draw_cycle_intf_ == nullptr) {
    ALOGE("CreateDrawCycleIntf failed");
    return false;
  }
  layer_builder_intf_ = factory->CreateLayerBuilderIntf();
  if (layer_builder_intf_ == nullptr) {
    ALOGE("CreateLayerBuilderIntf failed");
    return false;
  }
  settings_intf_ = factory->CreateSettingsIntf();
  if (settings_intf_ == nullptr) {
    ALOGE("CreateSettingsIntf failed");
    return false;
  }
  life_cycle_intf_ = factory->CreateLifeCycleIntf();
  if (life_cycle_intf_ == nullptr) {
    ALOGE("CreateLifeCycleIntf failed");
    return false;
  }
  sideband_intf_ = factory->CreateSideBandIntf();
  if (sideband_intf_ == nullptr) {
    ALOGE("CreateSideBandIntf failed");
    return false;
  }

  // Instantiate the required interfaces needed for initializing the interfaces
  // that were created above.
  buffer_allocator_ = std::make_unique<sdm::HWCBufferAllocator>();
  socket_handler_ = std::make_unique<sdm::HWCSocketHandler>();
  debug_callback_ = std::make_unique<SdmDebugCallback>();
  callback_interface_ = std::make_unique<StubCallbackInterface>();
  sideband_callbacks_ = std::make_unique<StubSideBandCompositorCallbacks>();

  // Initialize the interface with snapalloc.
  if (!SnapAllocHandle::Init(debug_callback_.get())) {
    ALOGE("Failed to Initialize SnapMapper");
    return false;
  }

  // TODO: Figure out a better way to do this.
  BufferInfoGetter::Init(std::make_unique<SnapAllocBufferInfoGetter>());

  // Initialize the lifecycle interface.
  auto display_error = life_cycle_intf_->Init(buffer_allocator_.get(),
                                              socket_handler_.get(),
                                              debug_callback_.get());
  if (display_error != sdm::kErrorNone) {
    ALOGE("lifecycleintf Init failed with error %s",
          ErrorToString(display_error).c_str());
    return false;
  }
  life_cycle_intf_->RegisterCompositorCallback(callback_interface_.get(), true);
  life_cycle_intf_->RegisterSideBandCallback(sideband_callbacks_.get(), true);

  ALOGI("Finished initializing SdmBackend");
  return true;
}

std::unique_ptr<DrmDisplayPipeline> SdmBackend::CreatePipeline(
    DrmConnector& connector) {
  ALOGI("SdmBackend::CreatePipeline: %s", connector.GetName().c_str());

  // Don't create pipelines for virtual connectors.
  if (connector.GetName().find("Virtual-") != std::string::npos) {
    ALOGI("Found virtual connector. Returning null pipeline.");
    return nullptr;
  }

  // TODO: Handle connected displays. As a part of this, keep track of which
  // displays already have pipelines associated with them.
  std::optional<SdmDisplayId> sdm_display_id = GetBuiltinDisplayId();
  if (sdm_display_id == std::nullopt) {
    ALOGE("Failed to find built-in display id.");
    return nullptr;
  }

  // Update the connector properties.
  connector.UpdateModes();

  // Find DRM resources associated with this pipeline.
  DrmEncoder* encoder = GetEncoderForConnector(connector);
  if (encoder == nullptr) {
    ALOGE("Failed to get encoder for connector %s.",
          connector.GetName().c_str());
    return nullptr;
  }

  DrmCrtc* crtc = GetCrtcForEncoder(connector.GetDev(), *encoder);
  if (crtc == nullptr) {
    ALOGE("Couldn't find crtc for connector %s.", connector.GetName().c_str());
    return nullptr;
  }

  // Initialize the DrmDisplayPipeline and bind drm resources to the pipeline.
  auto pipe = std::make_unique<DrmDisplayPipeline>();
  pipe->device = &connector.GetDev();
  pipe->connector = connector.BindPipeline(pipe.get());
  pipe->crtc = crtc->BindPipeline(pipe.get());
  if (pipe->connector == nullptr || pipe->crtc == nullptr) {
    ALOGE("Failed to bind connector or crtc");
    return nullptr;
  }

  // Bind a primary plane.
  // TODO: SDM makes the decision as to which planes to use, and there is no way
  // to query this.
  //       Picking an arbitrary plane seems to work for an initial
  //       implementation.
  if (!BindPrimaryPlane(pipe.get())) {
    ALOGE("Failed to bind primary plane for pipeline");
    return nullptr;
  }

  // Initialize the layer builder for this display.
  auto display_error = layer_builder_intf_->Init(buffer_allocator_.get(),
                                                 sdm_display_id.value());
  if (display_error != sdm::kErrorNone) {
    ALOGE("layer_builder_intf_ Init failed with error %s",
          ErrorToString(display_error).c_str());
    return nullptr;
  }

  // Construct backend implementations.
  pipe->atomic_commit_sink = std::make_unique<
      SdmAtomicCommitSink>(sdm_display_id.value(), life_cycle_intf_.get(),
                           draw_cycle_intf_.get());
  pipe->planner = std::make_unique<
      SdmCompositionPlanner>(sdm_display_id.value(), layer_builder_intf_.get(),
                             draw_cycle_intf_.get());
  return pipe;
}

std::unique_ptr<BufferInfoGetter> SdmBackend::CreateBufferInfoGetter() {
  return std::make_unique<SnapAllocBufferInfoGetter>();
}

std::optional<SdmBackend::SdmDisplayId> SdmBackend::GetBuiltinDisplayId() {
  // TODO: query this rather than hardcode.
  constexpr SdmDisplayId kPrimaryDisplayId = 0;
  return kPrimaryDisplayId;
}

// NOLINTNEXTLINE(cert-err58-cpp)
SdmBackend SdmBackend::instance;

}  // namespace android::drm_hwcomposer
