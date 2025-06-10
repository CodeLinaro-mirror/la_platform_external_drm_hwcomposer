/*
 * Copyright (C) 2015 The Android Open Source Project
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

#include <pthread.h>

#include <memory>
#include <optional>

#include "compositor/DisplayInfo.h"
#include "compositor/DrmKmsPlan.h"
#include "compositor/LayerData.h"
#include "drm/DrmPlane.h"
#include "drm/ResourceManager.h"
#include "drm/VSyncWorker.h"

namespace android {

struct AtomicCommitArgs {
  /* inputs. All fields are optional, but at least one has to be specified */
  bool test_only = false;
  bool blocking = false;
  bool teardown = false;
  std::optional<DrmMode> display_mode;
  std::optional<bool> active;
  std::shared_ptr<DrmKmsPlan> composition;
  std::shared_ptr<drm_color_ctm> color_matrix;
  std::optional<Colorspace> colorspace;
  std::optional<ContentType> content_type;
  std::shared_ptr<hdr_output_metadata> hdr_metadata;
  std::optional<int32_t> min_bpc;

  std::shared_ptr<DrmFbIdHandle> writeback_fb;
  SharedFd writeback_release_fence;

  /* out */
  SharedFd out_writeback_complete_fence;
  SharedFd out_fence;

  /* helpers */
  auto HasInputs() const -> bool {
    return display_mode || active || composition;
  }
};

class DrmAtomicStateManager {
 public:
  static auto CreateInstance(DrmDisplayPipeline *pipe)
      -> std::shared_ptr<DrmAtomicStateManager>;

  ~DrmAtomicStateManager() = default;

  auto ExecuteAtomicCommit(AtomicCommitArgs &args) -> int;
  auto ActivateDisplayUsingDPMS() -> int;

  void StopThread() {
    std::lock_guard lock(main_mutex_);
    {
      const std::lock_guard lock(mutex_);
      exit_thread_ = true;
    }
    cv_.notify_all();
  }

 private:
  void ThreadFn(const std::shared_ptr<DrmAtomicStateManager> &dasm);
  std::condition_variable cv_;
  std::mutex mutex_;
  bool exit_thread_ GUARDED_BY(mutex_){};

  std::mutex main_mutex_;

  DrmAtomicStateManager() = default;
  int CommitFrame(AtomicCommitArgs &args) REQUIRES(main_mutex_);

  // Collection of kms objects that were committed to the kernel. There must be
  // a userspace handle to keep these from being removed/unregistered until the
  // commit that used them is no longer being presented.
  struct KmsObjects {
    /* We have to hold a reference to framebuffer while displaying it ,
     * otherwise picture will blink */
    std::vector<std::shared_ptr<DrmFbIdHandle>> framebuffers;
    std::vector<DrmModeUserPropertyBlobUnique> blobs;
  };

  struct KmsState {
    /* Required to cleanup unused planes */
    std::vector<std::shared_ptr<BindingOwner<DrmPlane>>> used_planes;

    /* To avoid setting the inactive state twice, which will fail the commit */
    bool crtc_active_state{};

    KmsObjects used_kms_objects;
  };

  KmsState NewFrameState() REQUIRES(main_mutex_) {
    auto *prev_frame_state = &active_frame_state_;
    return (KmsState){
        .used_planes = prev_frame_state->used_planes,
        .crtc_active_state = prev_frame_state->crtc_active_state,
    };
  }

  // Only accessed from main thread.
  DrmDisplayPipeline *pipe_{};

  void CleanupPriorFrameResources() REQUIRES(main_mutex_);

  DstRectInfo whole_display_rect_{};

  // Accessed from both threads.
  KmsState staged_frame_state_ GUARDED_BY(main_mutex_);
  KmsState active_frame_state_ GUARDED_BY(main_mutex_);
  SharedFd last_present_fence_ GUARDED_BY(main_mutex_);
  int frames_staged_ GUARDED_BY(main_mutex_){};
  int frames_tracked_ GUARDED_BY(main_mutex_){};
};

}  // namespace android
