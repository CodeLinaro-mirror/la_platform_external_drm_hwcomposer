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

#include "EarlyBootAnimation.h"

#include <cstdio>
#include <cstring>
#include <limits>

#include <sys/stat.h>
#include <unistd.h>

#include "bufferinfo/BufferInfoGetter.h"
#include "utils/log.h"
#include "utils/properties.h"

namespace android::drm_hwcomposer {

EarlyBootAnimation::EarlyBootAnimation(HwcDisplay* display)
    : display_(display), animation_path_(Properties::BootAnimationPath()) {
  Properties::SetBootAnimationCompleted(false);
}

EarlyBootAnimation::~EarlyBootAnimation() {
}

bool EarlyBootAnimation::Start() {
  // Unimplemented
  return false;
}

bool EarlyBootAnimation::ReadIndexTable(int fd) {
  const size_t index_table_offset = sizeof(AnimationHeader);
  size_t table_size = header_.num_frames * sizeof(uint32_t);
  frame_data_offset_ = index_table_offset + table_size;

  if (lseek(fd, static_cast<off_t>(index_table_offset), SEEK_SET) ==
      static_cast<off_t>(-1)) {
    ALOGE("EarlyBootAnimation: Failed to seek to index table");
    return false;
  }

  frame_sizes_.resize(header_.num_frames);
  if (read(fd, frame_sizes_.data(), table_size) !=
      static_cast<ssize_t>(table_size)) {
    ALOGE("EarlyBootAnimation: Failed to read index table data");
    frame_sizes_.clear();
    return false;
  }

  static constexpr uint32_t kBitsPerByte = 8U;
  uint32_t bytes_per_pixel = BufferInfoGetter::DrmFormatToBpp(header_.format) /
                             kBitsPerByte;
  if (bytes_per_pixel == 0) {
    frame_sizes_.clear();
    return false;
  }
  size_t max_uncompressed = static_cast<size_t>(header_.width) *
                            header_.height * bytes_per_pixel;
  size_t max_allowed_compressed = max_uncompressed;

  // Precompute cumulative frame offsets for O(1) seeking during playback.
  frame_offsets_.resize(header_.num_frames);
  uint64_t current_offset = frame_data_offset_;
  for (uint32_t i = 0; i < header_.num_frames; ++i) {
    if (frame_sizes_[i] == 0 || frame_sizes_[i] > max_allowed_compressed) {
      ALOGE("EarlyBootAnimation: Invalid compressed frame size %u at index %u",
            frame_sizes_[i], i);
      frame_sizes_.clear();
      frame_offsets_.clear();
      return false;
    }
    if (std::numeric_limits<uint64_t>::max() - current_offset <
        frame_sizes_[i]) {
      ALOGE("EarlyBootAnimation: Frame offset overflow at index %u", i);
      frame_sizes_.clear();
      frame_offsets_.clear();
      return false;
    }
    frame_offsets_[i] = current_offset;
    current_offset += frame_sizes_[i];
  }

  struct stat st{};
  if (fstat(fd, &st) == -1) {
    ALOGE("EarlyBootAnimation: Failed to stat animation file");
    frame_sizes_.clear();
    frame_offsets_.clear();
    return false;
  }

  if (current_offset > static_cast<uint64_t>(st.st_size)) {
    ALOGE(
        "EarlyBootAnimation: Total frame data size %llu exceeds file size %lld",
        static_cast<unsigned long long>(current_offset),
        static_cast<long long>(st.st_size));
    frame_sizes_.clear();
    frame_offsets_.clear();
    return false;
  }

  return true;
}

void EarlyBootAnimation::Stop() {
  // Unimplemented
}

bool EarlyBootAnimation::ReadHeader(int fd) {
  if (lseek(fd, 0, SEEK_SET) == static_cast<off_t>(-1)) {
    ALOGE("EarlyBootAnimation: Failed to seek to start of header");
    return false;
  }
  if (read(fd, &header_, sizeof(header_)) != sizeof(header_)) {
    ALOGE("EarlyBootAnimation: Failed to read header");
    return false;
  }
  if (memcmp(header_.magic.data(), "RAWF", 4) == 0) {
    compressed_ = false;
  } else if (memcmp(header_.magic.data(), "LZ4F", 4) == 0) {
    compressed_ = true;
  } else {
    ALOGE(
        "EarlyBootAnimation: Invalid magic 0x%02x%02x%02x%02x (expected RAWF "
        "or LZ4F)",
        static_cast<uint8_t>(header_.magic[0]),
        static_cast<uint8_t>(header_.magic[1]),
        static_cast<uint8_t>(header_.magic[2]),
        static_cast<uint8_t>(header_.magic[3]));
    return false;
  }

  if (header_.width == 0 || header_.height == 0) {
    ALOGE("EarlyBootAnimation: Invalid dimensions %ux%u", header_.width,
          header_.height);
    return false;
  }
  if (header_.fps == 0 || header_.fps > kMaxFps) {
    ALOGE("EarlyBootAnimation: Invalid FPS %u", header_.fps);
    return false;
  }
  if (header_.num_frames == 0 || header_.num_frames > kMaxFrames) {
    ALOGE("EarlyBootAnimation: Invalid frame count %u", header_.num_frames);
    return false;
  }
  if (BufferInfoGetter::DrmFormatToBpp(header_.format) == 0) {
    ALOGE("EarlyBootAnimation: Invalid or unsupported format 0x%08x",
          header_.format);
    return false;
  }

  if (header_.hold_duration_ms > kMaxHoldDurationMs) {
    ALOGE("EarlyBootAnimation: Hold duration %u ms exceeds %u ms",
          header_.hold_duration_ms, kMaxHoldDurationMs);
    return false;
  }
  if (header_.hold_frame >= header_.num_frames &&
      header_.hold_duration_ms > 0) {
    ALOGW("EarlyBootAnimation: hold_frame %u >= num_frames %u",
          header_.hold_frame, header_.num_frames);
    return false;
  }

  frame_data_offset_ = sizeof(AnimationHeader);
  return true;
}

}  // namespace android::drm_hwcomposer
