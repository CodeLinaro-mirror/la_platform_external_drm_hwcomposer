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

#pragma once

#include <chrono>
#include <condition_variable>
#include <functional>
#include <thread>

namespace android::drm_hwcomposer {

// NOLINTNEXTLINE(misc-unused-using-decls): False positive
using std::chrono_literals::operator""s;

struct FlatConCallbacks {
  std::function<void()> trigger;
};

class FlatteningController {
 public:
  static auto CreateInstance(FlatConCallbacks &cbks,
                             std::chrono::milliseconds timeout)
      -> std::shared_ptr<FlatteningController>;
  ~FlatteningController() {
    StopThread();
    thread_.join();
  }

  // Disable flattening and stop checking for an idle scene.
  void Disable() {
    auto lock = std::lock_guard<std::mutex>(mutex_);
    flatten_next_frame_ = false;
    should_flatten_ = false;
    disabled_ = true;
  }

  // Registers a new frame by updating the flattening state as needed and
  // resetting the idle timer.
  void NewFrame();

  // Returns true if the FlatteningController detects that the scene is idle
  // and should be flattened by the compositor.
  auto ShouldFlatten() const {
    return should_flatten_;
  }

  void StopThread() {
    auto lock = std::lock_guard<std::mutex>(mutex_);
    cbks_ = {};
    cv_.notify_all();
  }

 private:
  FlatteningController(FlatConCallbacks callbacks,
                       std::chrono::milliseconds timeout);
  void ThreadFn();

  /* Disable the controller by default as it can cause refresh event to be
   * issued at creation time, even when it is not required. This can fail VTS
   * tests at teardown that check for this behaviour. See:
   * https://cs.android.com/android/platform/superproject/main/+/cedca652b903e4f4e584e457b5a7038e0825fb94:hardware/interfaces/graphics/composer/aidl/vts/VtsComposerClient.cpp;drc=a2a6deaf5036e081f48379b6573db4465538b5ac;l=604
   */
  bool flatten_next_frame_ = false;
  bool should_flatten_ = false;
  bool disabled_ = true;
  decltype(std::chrono::system_clock::now()) sleep_until_{};
  std::thread thread_;
  std::mutex mutex_;
  std::condition_variable cv_;
  FlatConCallbacks cbks_;
  const std::chrono::milliseconds timeout_;
};

}  // namespace android::drm_hwcomposer
