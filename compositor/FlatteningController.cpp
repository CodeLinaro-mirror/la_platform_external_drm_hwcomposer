/*
 * Copyright (C) 2023 The Android Open Source Project
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

/*
 * Usually, display controllers do not use intermediate buffer for composition
 * results. Instead, they scan-out directly from the input buffers, composing
 * the planes on the fly every VSYNC.
 *
 * Flattening is a technique that reduces memory bandwidth and power consumption
 * by converting non-updating multi-plane composition into a single-plane.
 * Additionally, flattening also makes more shared planes available for use by
 * other CRTCs.
 *
 * If the client is not updating layers for 1 second, FlatCon triggers a
 * callback to refresh the screen. The compositor should mark all layers to be
 * composed by the client into a single framebuffer using GPU.
 */

#define LOG_TAG "drmhwc"

#include "FlatteningController.h"

#include "utils/log.h"

namespace android::drm_hwcomposer {

auto FlatteningController::CreateInstance(FlatConCallbacks &cbks,
                                          std::chrono::milliseconds timeout)
    -> std::shared_ptr<FlatteningController> {
  return std::shared_ptr<FlatteningController>(
      new FlatteningController(cbks, timeout));
}

FlatteningController::FlatteningController(FlatConCallbacks callbacks,
                                           std::chrono::milliseconds timeout)
    : cbks_(std::move(callbacks)), timeout_(timeout) {
  thread_ = std::thread(&FlatteningController::ThreadFn, this);
}

/* Compositor should call this every frame */
void FlatteningController::NewFrame() {
  auto lock = std::lock_guard<std::mutex>(mutex_);

  if (state_ == State::kTriggeredCallback) {
    state_ = State::kFlattened;
    return;
  }

  sleep_until_ = std::chrono::system_clock::now() + timeout_;
  bool was_active = (state_ == State::kActive);
  state_ = State::kActive;

  if (!was_active) {
    cv_.notify_all();
  }
}

void FlatteningController::ThreadFn() {
  for (;;) {
    std::unique_lock<std::mutex> lock(mutex_);
    base::ScopedLockAssertion lock_assertion(mutex_);
    if (!cbks_.trigger)
      break;

    if (sleep_until_ <= std::chrono::system_clock::now() &&
        (state_ == State::kActive)) {
      state_ = State::kTriggeredCallback;
      ALOGV("Timeout. Sending an event to compositor");
      cbks_.trigger();
    }

    if (state_ != State::kActive) {
      ALOGV("Wait");
      cv_.wait(lock);
    } else {
      ALOGV("Wait_until");
      cv_.wait_until(lock, sleep_until_);
    }
  }
}

}  // namespace android::drm_hwcomposer
