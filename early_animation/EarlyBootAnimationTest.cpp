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

#include "early_animation/EarlyBootAnimation.h"

#include <cstdint>
#include <cstdio>
#include <cstring>
#include <memory>
#include <vector>

#include <gtest/gtest.h>

#include <android-base/file.h>
#include <android-base/unique_fd.h>
#include <drm_fourcc.h>
#include <fcntl.h>  // IWYU pragma: keep
#include <unistd.h>

namespace android::drm_hwcomposer {

class EarlyBootAnimationTest : public ::testing::Test {
 protected:
  using AnimationHeader = EarlyBootAnimation::AnimationHeader;

  void SetUp() override {
    anim_ = std::make_unique<EarlyBootAnimation>(nullptr);
  }

  static android::base::unique_fd CreateSeekableFdWithData(const void* data,
                                                           size_t size) {
    TemporaryFile tf;
    // NOLINTNEXTLINE(misc-include-cleaner)
    int raw_fd = open(tf.path, O_RDWR | O_CLOEXEC);
    if (raw_fd < 0) {
      return {};
    }
    android::base::unique_fd fd(raw_fd);
    if (size > 0 && data != nullptr) {
      if (write(fd.get(), data, size) != static_cast<ssize_t>(size)) {
        return {};
      }
      lseek(fd.get(), 0, SEEK_SET);
    }
    return fd;
  }

  bool ReadHeader(int fd) {
    return anim_->ReadHeader(fd);
  }

  bool ReadIndexTable(int fd) {
    return anim_->ReadIndexTable(fd);
  }

  const AnimationHeader& GetHeader() const {
    return anim_->header_;
  }

  bool IsCompressed() const {
    return anim_->compressed_;
  }

  const std::vector<uint32_t>& GetFrameSizes() const {
    return anim_->frame_sizes_;
  }

  const std::vector<uint64_t>& GetFrameOffsets() const {
    return anim_->frame_offsets_;
  }

 private:
  std::unique_ptr<EarlyBootAnimation> anim_;
};

// NOLINTBEGIN(readability-magic-numbers)
TEST_F(EarlyBootAnimationTest, HeaderStructSizeAndMagic) {
  EXPECT_EQ(sizeof(AnimationHeader), 32U);

  AnimationHeader raw_header{};
  std::memcpy(raw_header.magic.data(), "RAWF", 4);
  EXPECT_EQ(std::memcmp(raw_header.magic.data(), "RAWF", 4), 0);

  AnimationHeader lz4_header{};
  std::memcpy(lz4_header.magic.data(), "LZ4F", 4);
  EXPECT_EQ(std::memcmp(lz4_header.magic.data(), "LZ4F", 4), 0);
}

TEST_F(EarlyBootAnimationTest, ReadHeaderValidRawf) {
  AnimationHeader h{};
  std::memcpy(h.magic.data(), "RAWF", 4);
  h.width = 1920;
  h.height = 1080;
  h.fps = 60;
  h.num_frames = 120;
  h.format = DRM_FORMAT_ARGB8888;
  h.hold_frame = 74;
  h.hold_duration_ms = 1000;

  android::base::unique_fd fd = CreateSeekableFdWithData(&h, sizeof(h));
  ASSERT_TRUE(fd.ok());

  EXPECT_TRUE(ReadHeader(fd.get()));
  EXPECT_FALSE(IsCompressed());
  EXPECT_EQ(GetHeader().width, 1920U);
  EXPECT_EQ(GetHeader().height, 1080U);
  EXPECT_EQ(GetHeader().fps, 60U);
  EXPECT_EQ(GetHeader().num_frames, 120U);
  EXPECT_EQ(GetHeader().hold_frame, 74U);
  EXPECT_EQ(GetHeader().hold_duration_ms, 1000U);
}

TEST_F(EarlyBootAnimationTest, ReadHeaderValidLz4f) {
  AnimationHeader h{};
  std::memcpy(h.magic.data(), "LZ4F", 4);
  h.width = 2880;
  h.height = 1800;
  h.fps = 60;
  h.num_frames = 121;
  h.format = DRM_FORMAT_ARGB8888;

  android::base::unique_fd fd = CreateSeekableFdWithData(&h, sizeof(h));
  ASSERT_TRUE(fd.ok());

  EXPECT_TRUE(ReadHeader(fd.get()));
  EXPECT_TRUE(IsCompressed());
  EXPECT_EQ(GetHeader().width, 2880U);
}

TEST_F(EarlyBootAnimationTest, ReadHeaderCorruptMagic) {
  AnimationHeader h{};
  std::memcpy(h.magic.data(), "BADM", 4);
  h.width = 1920;
  h.height = 1080;
  h.fps = 60;
  h.num_frames = 120;
  h.format = DRM_FORMAT_ARGB8888;

  android::base::unique_fd fd = CreateSeekableFdWithData(&h, sizeof(h));
  ASSERT_TRUE(fd.ok());

  EXPECT_FALSE(ReadHeader(fd.get()));
}

TEST_F(EarlyBootAnimationTest, ReadHeaderInvalidDimensions) {
  AnimationHeader h{};
  std::memcpy(h.magic.data(), "RAWF", 4);
  h.width = 0;
  h.height = 1080;
  h.fps = 60;
  h.num_frames = 120;
  h.format = DRM_FORMAT_ARGB8888;

  android::base::unique_fd fd = CreateSeekableFdWithData(&h, sizeof(h));
  ASSERT_TRUE(fd.ok());

  EXPECT_FALSE(ReadHeader(fd.get()));
}

TEST_F(EarlyBootAnimationTest, ReadHeaderInvalidFps) {
  AnimationHeader h{};
  std::memcpy(h.magic.data(), "RAWF", 4);
  h.width = 1920;
  h.height = 1080;
  h.fps = 0;
  h.num_frames = 120;
  h.format = DRM_FORMAT_ARGB8888;

  android::base::unique_fd fd = CreateSeekableFdWithData(&h, sizeof(h));
  ASSERT_TRUE(fd.ok());

  EXPECT_FALSE(ReadHeader(fd.get()));
}

TEST_F(EarlyBootAnimationTest, ReadIndexTableValid) {
  AnimationHeader h{};
  std::memcpy(h.magic.data(), "LZ4F", 4);
  h.width = 1920;
  h.height = 1080;
  h.fps = 60;
  h.num_frames = 3;
  h.format = DRM_FORMAT_ARGB8888;

  std::vector<uint32_t> frame_sizes = {1000, 2000, 1500};
  std::vector<uint8_t> payload(4500, 0xAB);

  std::vector<uint8_t> full_file;
  full_file.resize(sizeof(h) + (frame_sizes.size() * sizeof(uint32_t)) +
                   payload.size());
  std::memcpy(full_file.data(), &h, sizeof(h));
  // NOLINTNEXTLINE(cppcoreguidelines-pro-bounds-pointer-arithmetic)
  std::memcpy(full_file.data() + sizeof(h), frame_sizes.data(),
              frame_sizes.size() * sizeof(uint32_t));
  // NOLINTNEXTLINE(cppcoreguidelines-pro-bounds-pointer-arithmetic)
  std::memcpy(full_file.data() + sizeof(h) +
                  (frame_sizes.size() * sizeof(uint32_t)),
              payload.data(), payload.size());

  android::base::unique_fd fd = CreateSeekableFdWithData(full_file.data(),
                                                         full_file.size());
  ASSERT_TRUE(fd.ok());

  EXPECT_TRUE(ReadHeader(fd.get()));
  EXPECT_TRUE(ReadIndexTable(fd.get()));

  ASSERT_EQ(GetFrameSizes().size(), 3U);
  EXPECT_EQ(GetFrameSizes()[0], 1000U);
  EXPECT_EQ(GetFrameSizes()[1], 2000U);
  EXPECT_EQ(GetFrameSizes()[2], 1500U);

  ASSERT_EQ(GetFrameOffsets().size(), 3U);
  EXPECT_EQ(GetFrameOffsets()[0], 44U);
  EXPECT_EQ(GetFrameOffsets()[1], 1044U);
  EXPECT_EQ(GetFrameOffsets()[2], 3044U);
}

TEST_F(EarlyBootAnimationTest, ReadIndexTableCorruptFrameSize) {
  AnimationHeader h{};
  std::memcpy(h.magic.data(), "LZ4F", 4);
  h.width = 100;
  h.height = 100;
  h.fps = 60;
  h.num_frames = 1;
  h.format = DRM_FORMAT_ARGB8888;

  uint32_t huge_size = 10 * 1024 * 1024;
  std::vector<uint8_t> file_buf;
  file_buf.resize(sizeof(h) + sizeof(huge_size));
  std::memcpy(file_buf.data(), &h, sizeof(h));
  // NOLINTNEXTLINE(cppcoreguidelines-pro-bounds-pointer-arithmetic)
  std::memcpy(file_buf.data() + sizeof(h), &huge_size, sizeof(huge_size));

  android::base::unique_fd fd = CreateSeekableFdWithData(file_buf.data(),
                                                         file_buf.size());
  ASSERT_TRUE(fd.ok());

  EXPECT_TRUE(ReadHeader(fd.get()));
  EXPECT_FALSE(ReadIndexTable(fd.get()));
}
// NOLINTEND(readability-magic-numbers)

}  // namespace android::drm_hwcomposer
