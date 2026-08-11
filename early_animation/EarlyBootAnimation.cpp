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

#include <algorithm>
#include <cerrno>
#include <chrono>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <limits>
#include <optional>
#include <vector>

#include <drm_fourcc.h>
#include <fcntl.h>  // IWYU pragma: keep
#include <sys/mman.h>   // IWYU pragma: keep
#include <sys/stat.h>
#include <unistd.h>
#include <xf86drm.h>      // IWYU pragma: keep

#include "bufferinfo/BufferInfo.h"
#include "bufferinfo/BufferInfoGetter.h"
#include "compositor/DisplayInfo.h"
#include "drm/DrmDevice.h"
#include "hwc/HwcDisplay.h"
#include "hwc/HwcLayer.h"
#include "utils/fd.h"
#include "utils/log.h"
#include "utils/properties.h"

namespace android::drm_hwcomposer {

namespace {

constexpr bool IsHdrFormat(uint32_t format) {
  switch (format) {
    case DRM_FORMAT_ARGB2101010:
    case DRM_FORMAT_ABGR2101010:
    case DRM_FORMAT_XBGR2101010:
    case DRM_FORMAT_XRGB2101010:
    case DRM_FORMAT_RGBA1010102:
    case DRM_FORMAT_RGBX1010102:
      return true;
    default:
      return false;
  }
}

}  // namespace

EarlyBootAnimation::EarlyBootAnimation(HwcDisplay* display)
    : display_(display), animation_path_(Properties::BootAnimationPath()) {
  Properties::SetBootAnimationCompleted(false);
}

EarlyBootAnimation::~EarlyBootAnimation() {
  Stop();
}

uint32_t EarlyBootAnimation::CalculateMultiplier(float vrefresh,
                                                 uint32_t anim_fps) {
  if (vrefresh > static_cast<float>(anim_fps) && anim_fps > 0) {
    return std::max(1U, static_cast<uint32_t>(std::round(
                            vrefresh / static_cast<float>(anim_fps))));
  }
  return 1U;
}

std::chrono::nanoseconds EarlyBootAnimation::CalculateFrameDuration(
    float vrefresh, uint32_t anim_fps) {
  constexpr double kNanosecondsPerSecond = 1e9;
  uint32_t multiplier = CalculateMultiplier(vrefresh, anim_fps);
  if (vrefresh > 0.0F) {
    return std::chrono::nanoseconds(static_cast<uint64_t>(
        std::round((kNanosecondsPerSecond * static_cast<double>(multiplier)) /
                   static_cast<double>(vrefresh))));
  }
  return std::chrono::nanoseconds(static_cast<uint64_t>(kNanosecondsPerSecond) /
                                  std::max(1U, anim_fps));
}

bool EarlyBootAnimation::Start() {
  {
    std::lock_guard<std::mutex> lock(mutex_);
    if (state_ != AnimationState::kStopped || thread_.joinable()) {
      ALOGE(
          "EarlyBootAnimation: Attempting to start while animation is running");
      return false;
    }
  }

  UniqueFd fd = MakeUniqueFd(
      // NOLINTNEXTLINE(misc-include-cleaner)
      open(animation_path_.c_str(), O_RDONLY | O_CLOEXEC));
  if (!fd) {
    ALOGE("EarlyBootAnimation: Failed to open %s (errno=%d)",
          animation_path_.c_str(), errno);
    Stop();
    return false;
  }

  if (!ReadHeader(*fd)) {
    Stop();
    return false;
  }

  if (compressed_) {
    if (!ReadIndexTable(*fd)) {
      Stop();
      return false;
    }
  }

  const HwcDisplayConfig* current_config = display_->GetCurrentConfig();
  if (current_config == nullptr) {
    ALOGE("EarlyBootAnimation: No current display config");
    Stop();
    return false;
  }
  ConfigId best_config_id = SelectBestConfig(current_config,
                                             display_->GetDisplayConfigs(),
                                             static_cast<float>(header_.fps));

  ALOGD("EarlyBootAnimation: Current config is %u (%ux%u @ %.2f Hz)",
        current_config->id, current_config->mode.GetRawMode().hdisplay,
        current_config->mode.GetRawMode().vdisplay,
        current_config->mode.GetVRefresh());
  if (best_config_id == current_config->id) {
    ALOGD("EarlyBootAnimation: Keeping current config %u to match FPS %u",
          current_config->id, header_.fps);
  } else {
    ALOGD("EarlyBootAnimation: Switching config from %u to %u to match FPS %u",
          current_config->id, best_config_id, header_.fps);
    display_->SetConfig(best_config_id);
    current_config = display_->GetCurrentConfig();
    if (current_config == nullptr) {
      ALOGE("EarlyBootAnimation: No current display config after SetConfig");
      Stop();
      return false;
    }
  }

  // Match sleep duration to the panel's actual refresh rate to avoid clock
  // drift (e.g. 59.94Hz), and use multiplier for high refresh panels (e.g.
  // 120Hz).
  float vrefresh = current_config != nullptr
                       ? current_config->mode.GetVRefresh()
                       : static_cast<float>(header_.fps);
  frame_duration_ = CalculateFrameDuration(vrefresh, header_.fps);

  if (!AllocateBuffers(header_.format, current_config)) {
    ALOGE("EarlyBootAnimation: Failed to allocate buffers");
    Stop();
    return false;
  }

  ALOGD(
      "EarlyBootAnimation: Starting: %ux%u @ %u FPS, %u frames, format 0x%08x",
      header_.width, header_.height, header_.fps, header_.num_frames,
      static_cast<uint32_t>(header_.format));

  SetAnimationState(AnimationState::kRunning);
  thread_ = std::thread(&EarlyBootAnimation::Loop, this, std::move(fd));
  return true;
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

void EarlyBootAnimation::Loop(UniqueFd /*fd*/) {
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

bool EarlyBootAnimation::AllocateBuffers(
    uint32_t format, const HwcDisplayConfig* current_config) {
  uint32_t drm_format = format;
  if (drm_format == 0) {
    ALOGE("EarlyBootAnimation: Unsupported format %u", format);
    return false;
  }

  uint32_t display_width = current_config->mode.GetRawMode().hdisplay;
  uint32_t display_height = current_config->mode.GetRawMode().vdisplay;

  if (header_.width > display_width || header_.height > display_height) {
    ALOGE("EarlyBootAnimation: Animation size %ux%u exceeds display size %ux%u",
          header_.width, header_.height, display_width, display_height);
    return false;
  }

  offset_x_ = (display_width - header_.width) / 2;
  offset_y_ = (display_height - header_.height) / 2;

  ALOGD(
      "EarlyBootAnimation: Centering %ux%u animation on %ux%u display at "
      "(%u,%u)",
      header_.width, header_.height, display_width, display_height, offset_x_,
      offset_y_);

  DstRectInfo display_frame{
      .i_rect = IRect{.left = 0,
                      .top = 0,
                      .right = static_cast<int32_t>(display_width),
                      .bottom = static_cast<int32_t>(display_height)}};

  SrcRectInfo source_crop{
      .f_rect = FRect{.left = 0.0F,
                      .top = 0.0F,
                      .right = static_cast<float>(display_width),
                      .bottom = static_cast<float>(display_height)}};

  for (size_t i = 0; i < buffers_.size(); ++i) {
    auto bi_opt = display_->GetPipe()
                      .device
                      ->CreateDumbBuffer(display_width, display_height,
                                         drm_format,
                                         // NOLINTNEXTLINE(misc-include-cleaner)
                                         DRM_CLOEXEC | DRM_RDWR);
    if (!bi_opt) {
      ALOGE("EarlyBootAnimation: Failed to allocate dumb buffer %zu", i);
      FreeBuffers();
      return false;
    }
    buffers_[i].bi = bi_opt.value();
    ALOGD(
        "HWC_DUMB_BUFFER: Allocated dumb buffer %zu (width=%u, height=%u, "
        "format=%u, pitch=%u, size=%u)",
        i, buffers_[i].bi.width, buffers_[i].bi.height, buffers_[i].bi.format,
        buffers_[i].bi.pitches[0], buffers_[i].bi.sizes[0]);

    if (buffers_[i].bi.prime_fds[0] >= 0 && buffers_[i].bi.sizes[0] > 0) {
      void* ptr = mmap(nullptr, buffers_[i].bi.sizes[0],
                       // NOLINTNEXTLINE(misc-include-cleaner)
                       PROT_READ | PROT_WRITE, MAP_SHARED,
                       buffers_[i].bi.prime_fds[0], 0);
      if (ptr != MAP_FAILED) {
        buffers_[i].mmap_addr = ptr;
        buffers_[i].mmap_size = buffers_[i].bi.sizes[0];
        buffers_[i].pitch = buffers_[i].bi.pitches[0];
        buffers_[i].prime_fd = buffers_[i].bi.prime_fds[0];
        memset(ptr, 0, buffers_[i].bi.sizes[0]);
      } else {
        ALOGE("EarlyBootAnimation: Failed to mmap dumb buffer %zu (errno=%d)",
              i, errno);
        FreeBuffers();
        return false;
      }
    } else {
      ALOGE("EarlyBootAnimation: Invalid fd or size for dumb buffer %zu", i);
      FreeBuffers();
      return false;
    }

    auto fb = display_->GetPipe().importer->GetOrCreateFbId(&buffers_[i].bi);
    if (!fb) {
      ALOGE("EarlyBootAnimation: Failed to get FB for dumb buffer %zu", i);
      FreeBuffers();
      return false;
    }
    buffers_[i].fb_id_handle = fb;

    bool is_hdr_format = IsHdrFormat(header_.format);
    HwcColorspace layer_colorspace = is_hdr_format ? HwcColorspace::kBt2020
                                                   : HwcColorspace::kBt709;
    TransferFunction layer_tf = is_hdr_format ? TransferFunction::kPq
                                              : TransferFunction::kSrgb;
    buffers_[i].hwc_layer = std::make_unique<HwcLayer>(display_);
    buffers_[i].hwc_layer->SetLayerProperties({
        .buffer = std::optional<HwcLayer::Buffer>({
            .bi = buffers_[i].bi,
            .fb = fb,
            .fence = {},
        }),
        .blend_mode = BufferBlendMode::kNone,
        .colorspace = layer_colorspace,
        .transfer_func = layer_tf,
        .display_frame = display_frame,
        .source_crop = source_crop,
    });
  }
  return true;
}

ConfigId EarlyBootAnimation::SelectBestConfig(
    const HwcDisplayConfig* current,
    const std::vector<HwcDisplayConfig>& configs, float anim_fps) {
  if (current == nullptr) {
    return 0;
  }

  std::optional<ConfigId> best_config_id;
  float current_refresh = current->mode.GetVRefresh();
  float max_refresh = 0.F;

  ALOGD(
      "EarlyBootAnimation: SelectBestConfig for anim_fps=%.2f. Current "
      "config=%u, group_id=%u, refresh=%.2f Hz, output_type=%d",
      anim_fps, current->id, current->group_id, current_refresh,
      static_cast<int>(current->output_type));

  // Pass 1: Search for highest refresh rate config strictly within the same
  // group.
  if (current->group_id != 0) {
    for (const auto& config : configs) {
      if (config.group_id == current->group_id &&
          config.output_type == current->output_type) {
        float refresh = config.mode.GetVRefresh();
        if (refresh > max_refresh) {
          max_refresh = refresh;
          best_config_id = config.id;
        }
      }
    }
  }

  // Pass 2: Fallback to same size and output_type across all configs.
  if (!best_config_id) {
    for (const auto& config : configs) {
      if (config.mode.SameSize(current->mode) &&
          config.output_type == current->output_type) {
        float refresh = config.mode.GetVRefresh();
        if (refresh > max_refresh) {
          max_refresh = refresh;
          best_config_id = config.id;
        }
      }
    }
  }

  if (best_config_id) {
    ALOGD(
        "EarlyBootAnimation: SelectBestConfig selected highest refresh rate "
        "config %u "
        "(%.2f Hz)",
        *best_config_id, max_refresh);
    return *best_config_id;
  }

  // Fallback to current if no matching config found
  ALOGW(
      "EarlyBootAnimation: SelectBestConfig could not find a suitable "
      "config. Falling back to current %u",
      current->id);
  return current->id;
}

void EarlyBootAnimation::FreeBuffers() {
  for (auto& buffer : buffers_) {
    if (buffer.hwc_layer) {
      buffer.hwc_layer.reset();
    }
    if (buffer.mmap_addr != nullptr && buffer.mmap_addr != MAP_FAILED) {
      munmap(buffer.mmap_addr, buffer.mmap_size);
      buffer.mmap_addr = nullptr;
    }
    buffer.fb_id_handle.reset();
    buffer.bi = {};
    buffer.prime_fd = -1;
  }
}

}  // namespace android::drm_hwcomposer
