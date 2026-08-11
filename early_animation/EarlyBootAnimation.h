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

#include <array>
#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

namespace android::drm_hwcomposer {

class HwcDisplay;

class EarlyBootAnimation {
  friend class EarlyBootAnimationTest;

 public:
  explicit EarlyBootAnimation(HwcDisplay* display);
  ~EarlyBootAnimation();

  EarlyBootAnimation(const EarlyBootAnimation&) = delete;
  EarlyBootAnimation& operator=(const EarlyBootAnimation&) = delete;
  EarlyBootAnimation(EarlyBootAnimation&&) = delete;
  EarlyBootAnimation& operator=(EarlyBootAnimation&&) = delete;

  [[nodiscard]] bool Start();
  void Stop();

 private:
  static constexpr size_t kAnimationHeaderSize = 32;

  struct AnimationHeader {
    std::array<char, 4> magic;  // "LZ4F" or similar
    uint32_t width;
    uint32_t height;
    uint32_t fps;
    uint32_t num_frames;
    uint32_t format;
    uint32_t hold_frame;
    uint32_t hold_duration_ms;
  } __attribute__((packed));
  static_assert(sizeof(AnimationHeader) == kAnimationHeaderSize,
                "AnimationHeader size must be exactly 32 bytes");

  // 240 Hz max refresh rate to prevent division-by-zero and bound frame timing.
  static constexpr uint32_t kMaxFps = 240;
  // 10,000 frames max (~2.7 min @ 60 FPS) to bound index table memory
  // allocation.
  static constexpr uint32_t kMaxFrames = 10000;
  // 10 second max hold duration as a safety watchdog against early boot hangs.
  static constexpr uint32_t kMaxHoldDurationMs = 10000;

  // Reads and validates the 32-byte header into header_, determines compressed_
  // mode from magic, and initializes frame_data_offset_ to point past the
  // header. Returns true on success
  [[nodiscard]] bool ReadHeader(int fd);

  // Reads per-frame compressed sizes into frame_sizes_, precomputes cumulative
  // seek positions in frame_offsets_ for O(1) frame lookup, and advances
  // frame_data_offset_. Returns true on success
  [[nodiscard]] bool ReadIndexTable(int fd);

  // Non-owning pointer to the primary internal display. Owned by DrmHwc;
  // guaranteed valid for the animation lifetime because the animation is
  // destroyed prior to display deinit.
  HwcDisplay* const display_;

  AnimationHeader header_{};
  std::vector<uint32_t> frame_sizes_;
  std::vector<uint64_t> frame_offsets_;
  uint64_t frame_data_offset_ = 0;
  bool compressed_ = false;
  std::string animation_path_;
};

}  // namespace android::drm_hwcomposer
