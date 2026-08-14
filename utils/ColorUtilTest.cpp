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

// NOLINTBEGIN(readability-magic-numbers)

#include <gtest/gtest.h>

#include <drm/drm_mode.h>

#include <cmath>
#include <cstddef>
#include <cstdint>
#include <limits>
#include <memory>

#include "compositor/DisplayInfo.h"
#include "compositor/LayerData.h"
#include "drm/DrmColorspace.h"
#include "utils/ColorUtil.h"
#include "utils/TestUtils.h"

namespace android::drm_hwcomposer {

inline double From3132FixPt(uint64_t val) {
  constexpr uint64_t kSignMask = 1ULL << 63;
  double res = static_cast<double>(val & ~kSignMask) /
               static_cast<double>(1ULL << 32);
  return (val & kSignMask) ? -res : res;
}

// Tests for EvaluateHlgOetf
TEST(ColorUtilTest, EvaluateHlgOetfNegativeAndZero) {
  EXPECT_DOUBLE_EQ(ColorUtil::EvaluateHlgOetf(-0.5), 0.0);
  EXPECT_DOUBLE_EQ(ColorUtil::EvaluateHlgOetf(0.0), 0.0);
}

TEST(ColorUtilTest, EvaluateHlgOetfLinearRegion) {
  // For 0 < l <= 1/12, HlgOetf(l) = 0.5 * sqrt(12 * l)
  double l_val = 0.5 / 12.0;
  double expected = 0.5 * std::sqrt(0.5);
  EXPECT_NEAR(ColorUtil::EvaluateHlgOetf(l_val), expected, 1e-6);

  // At upper linear boundary: l = 1/12 -> 0.5 * sqrt(1) = 0.5
  EXPECT_NEAR(ColorUtil::EvaluateHlgOetf(1.0 / 12.0), 0.5, 1e-6);
}

TEST(ColorUtilTest, EvaluateHlgOetfLogarithmicRegion) {
  // For l > 1/12, HlgOetf(l) = a * ln(12*l - b) + c
  // At l = 1.0 (reference white level of 100 cd/m2), result should be 1.0
  EXPECT_NEAR(ColorUtil::EvaluateHlgOetf(1.0), 1.0, 1e-5);
}

// =============================================================================
// 2. Matrix Transformations & Operations
// =============================================================================

TEST(ColorUtilTest, ToColorTransform3x3Nullptr) {
  std::shared_ptr<const HalColorTransformMatrix> null_matrix = nullptr;
  EXPECT_EQ(ColorUtil::ToColorTransform3x3(null_matrix), nullptr);
}

TEST(ColorUtilTest, ToColorTransform3x3Identity) {
  auto hal_matrix = std::make_shared<const HalColorTransformMatrix>(
      kIdentityMatrix);
  auto ctm = ColorUtil::ToColorTransform3x3(hal_matrix);
  ASSERT_NE(ctm, nullptr);

  constexpr uint64_t kOneFixPt = 1ULL << 32;

  EXPECT_EQ(ctm->matrix[0], kOneFixPt);
  EXPECT_EQ(ctm->matrix[1], 0ULL);
  EXPECT_EQ(ctm->matrix[2], 0ULL);

  EXPECT_EQ(ctm->matrix[3], 0ULL);
  EXPECT_EQ(ctm->matrix[4], kOneFixPt);
  EXPECT_EQ(ctm->matrix[5], 0ULL);

  EXPECT_EQ(ctm->matrix[6], 0ULL);
  EXPECT_EQ(ctm->matrix[7], 0ULL);
  EXPECT_EQ(ctm->matrix[8], kOneFixPt);
}

TEST(ColorUtilTest, ToColorTransform3x3NightLightMatrix) {
  // Typical Night Light warmth matrix (Red=1.0, Green=0.85, Blue=0.5)
  HalColorTransformMatrix matrix = {
      1.0F, 0.0F,  0.0F, 0.0F,  //
      0.0F, 0.85F, 0.0F, 0.0F,  //
      0.0F, 0.0F,  0.5F, 0.0F,  //
      0.0F, 0.0F,  0.0F, 1.0F,  //
  };
  auto hal_matrix = std::make_shared<const HalColorTransformMatrix>(matrix);
  auto ctm = ColorUtil::ToColorTransform3x3(hal_matrix);
  ASSERT_NE(ctm, nullptr);

  EXPECT_EQ(ctm->matrix[0], ColorUtil::To3132FixPt(1.0F));
  EXPECT_EQ(ctm->matrix[1], 0ULL);
  EXPECT_EQ(ctm->matrix[2], 0ULL);

  EXPECT_EQ(ctm->matrix[3], 0ULL);
  EXPECT_EQ(ctm->matrix[4], ColorUtil::To3132FixPt(0.85F));
  EXPECT_EQ(ctm->matrix[5], 0ULL);

  EXPECT_EQ(ctm->matrix[6], 0ULL);
  EXPECT_EQ(ctm->matrix[7], 0ULL);
  EXPECT_EQ(ctm->matrix[8], ColorUtil::To3132FixPt(0.5F));
}

TEST(ColorUtilTest, ToColorTransform3x3DaltonizationWithNegativeCoefficients) {
  // Protanomaly Daltonization matrix with cross-channel and negative mixing
  HalColorTransformMatrix matrix = {
      0.625F, 0.375F, 0.0F, 0.0F,  //
      0.7F,   0.3F,   0.0F, 0.0F,  //
      -0.3F,  0.3F,   1.0F, 0.0F,  //
      0.0F,   0.0F,   0.0F, 1.0F,  //
  };
  auto hal_matrix = std::make_shared<const HalColorTransformMatrix>(matrix);
  auto ctm = ColorUtil::ToColorTransform3x3(hal_matrix);
  ASSERT_NE(ctm, nullptr);

  constexpr uint64_t kSignMask = 1ULL << 63;
  EXPECT_EQ(ctm->matrix[0], ColorUtil::To3132FixPt(0.625F));
  EXPECT_EQ(ctm->matrix[1], ColorUtil::To3132FixPt(0.7F));
  EXPECT_EQ(ctm->matrix[2], ColorUtil::To3132FixPt(0.3F) | kSignMask);

  EXPECT_EQ(ctm->matrix[3], ColorUtil::To3132FixPt(0.375F));
  EXPECT_EQ(ctm->matrix[4], ColorUtil::To3132FixPt(0.3F));
  EXPECT_EQ(ctm->matrix[5], ColorUtil::To3132FixPt(0.3F));

  EXPECT_EQ(ctm->matrix[6], 0ULL);
  EXPECT_EQ(ctm->matrix[7], 0ULL);
  EXPECT_EQ(ctm->matrix[8], ColorUtil::To3132FixPt(1.0F));
}

TEST(ColorUtilTest, ToColorTransform3x4Nullptr) {
  std::shared_ptr<const HalColorTransformMatrix> null_matrix = nullptr;
  EXPECT_EQ(ColorUtil::ToColorTransform3x4(null_matrix), nullptr);
}

TEST(ColorUtilTest, ToColorTransform3x4Identity) {
  auto hal_matrix = std::make_shared<const HalColorTransformMatrix>(
      kIdentityMatrix);
  auto ctm = ColorUtil::ToColorTransform3x4(hal_matrix);
  ASSERT_NE(ctm, nullptr);

  constexpr uint64_t kOneFixPt = 1ULL << 32;

  EXPECT_EQ(ctm->matrix[0], kOneFixPt);
  EXPECT_EQ(ctm->matrix[1], 0ULL);
  EXPECT_EQ(ctm->matrix[2], 0ULL);
  EXPECT_EQ(ctm->matrix[3], 0ULL);

  EXPECT_EQ(ctm->matrix[4], 0ULL);
  EXPECT_EQ(ctm->matrix[5], kOneFixPt);
  EXPECT_EQ(ctm->matrix[6], 0ULL);
  EXPECT_EQ(ctm->matrix[7], 0ULL);

  EXPECT_EQ(ctm->matrix[8], 0ULL);
  EXPECT_EQ(ctm->matrix[9], 0ULL);
  EXPECT_EQ(ctm->matrix[10], kOneFixPt);
  EXPECT_EQ(ctm->matrix[11], 0ULL);
}

TEST(ColorUtilTest, ToColorTransform3x4OffsetTranslationVectors) {
  // HAL column-major matrix with translation offsets in Red (12), Green (13),
  // Blue (14)
  HalColorTransformMatrix matrix = {
      1.0F, 0.0F, 0.0F, 0.0F,  //
      0.0F, 1.0F, 0.0F, 0.0F,  //
      0.0F, 0.0F, 1.0F, 0.0F,  //
      0.1F, 0.2F, 0.3F, 1.0F,  //
  };
  auto hal_matrix = std::make_shared<const HalColorTransformMatrix>(matrix);
  auto ctm = ColorUtil::ToColorTransform3x4(hal_matrix);
  ASSERT_NE(ctm, nullptr);

  constexpr uint64_t kOneFixPt = 1ULL << 32;

  // Row 0: R_out = 1.0*R + 0.0*G + 0.0*B + 0.1
  EXPECT_EQ(ctm->matrix[0], kOneFixPt);
  EXPECT_EQ(ctm->matrix[1], 0ULL);
  EXPECT_EQ(ctm->matrix[2], 0ULL);
  EXPECT_EQ(ctm->matrix[3], ColorUtil::To3132FixPt(0.1F));

  // Row 1: G_out = 0.0*R + 1.0*G + 0.0*B + 0.2
  EXPECT_EQ(ctm->matrix[4], 0ULL);
  EXPECT_EQ(ctm->matrix[5], kOneFixPt);
  EXPECT_EQ(ctm->matrix[6], 0ULL);
  EXPECT_EQ(ctm->matrix[7], ColorUtil::To3132FixPt(0.2F));

  // Row 2: B_out = 0.0*R + 0.0*G + 1.0*B + 0.3
  EXPECT_EQ(ctm->matrix[8], 0ULL);
  EXPECT_EQ(ctm->matrix[9], 0ULL);
  EXPECT_EQ(ctm->matrix[10], kOneFixPt);
  EXPECT_EQ(ctm->matrix[11], ColorUtil::To3132FixPt(0.3F));
}

TEST(ColorUtilTest, ToColorTransform3x4CombinedScaleAndOffset) {
  // HAL matrix with both scaling (gains) and translation offsets
  HalColorTransformMatrix matrix = {
      0.8F,  0.0F, 0.0F,  0.0F,  // Red gain 0.8
      0.0F,  0.9F, 0.0F,  0.0F,  // Green gain 0.9
      0.0F,  0.0F, 0.7F,  0.0F,  // Blue gain 0.7
      0.05F, 0.1F, 0.15F, 1.0F,  // Offsets: R=0.05, G=0.1, B=0.15
  };
  auto hal_matrix = std::make_shared<const HalColorTransformMatrix>(matrix);
  auto ctm = ColorUtil::ToColorTransform3x4(hal_matrix);
  ASSERT_NE(ctm, nullptr);

  // Row 0: R_out = 0.8*R + 0.0*G + 0.0*B + 0.05
  EXPECT_EQ(ctm->matrix[0], ColorUtil::To3132FixPt(0.8F));
  EXPECT_EQ(ctm->matrix[1], 0ULL);
  EXPECT_EQ(ctm->matrix[2], 0ULL);
  EXPECT_EQ(ctm->matrix[3], ColorUtil::To3132FixPt(0.05F));

  // Row 1: G_out = 0.0*R + 0.9*G + 0.0*B + 0.10
  EXPECT_EQ(ctm->matrix[4], 0ULL);
  EXPECT_EQ(ctm->matrix[5], ColorUtil::To3132FixPt(0.9F));
  EXPECT_EQ(ctm->matrix[6], 0ULL);
  EXPECT_EQ(ctm->matrix[7], ColorUtil::To3132FixPt(0.1F));

  // Row 2: B_out = 0.0*R + 0.0*G + 0.7*B + 0.15
  EXPECT_EQ(ctm->matrix[8], 0ULL);
  EXPECT_EQ(ctm->matrix[9], 0ULL);
  EXPECT_EQ(ctm->matrix[10], ColorUtil::To3132FixPt(0.7F));
  EXPECT_EQ(ctm->matrix[11], ColorUtil::To3132FixPt(0.15F));
}

TEST(ColorUtilTest, MultiplyIdentityPreservation) {
  auto a = std::make_shared<HalColorTransformMatrix>(HalColorTransformMatrix{
      0.5F,
      0.1F,
      0.0F,
      0.0F,  //
      0.2F,
      0.8F,
      0.0F,
      0.0F,  //
      0.1F,
      0.1F,
      0.9F,
      0.0F,  //
      0.0F,
      0.0F,
      0.0F,
      1.0F,  //
  });

  EXPECT_EQ(ColorUtil::Multiply(a, GetIdentityCtmPtr()), a);
  EXPECT_EQ(ColorUtil::Multiply(GetIdentityCtmPtr(), a), a);
  EXPECT_EQ(ColorUtil::Multiply(a, nullptr), a);
  EXPECT_EQ(ColorUtil::Multiply(nullptr, a), a);
  EXPECT_EQ(ColorUtil::Multiply(nullptr, nullptr), nullptr);
}

TEST(ColorUtilTest, MultiplyGeneralMatrices) {
  HalColorTransformMatrix a_val = {
      1.1F,  0.2F,  0.0F,  0.0F,  //
      0.1F,  0.9F,  0.1F,  0.0F,  //
      0.0F,  0.1F,  0.8F,  0.0F,  //
      0.05F, 0.02F, 0.01F, 1.0F,  //
  };
  HalColorTransformMatrix b_val = {
      0.9F,  0.05F, 0.0F,  0.0F,  //
      0.05F, 0.95F, 0.0F,  0.0F,  //
      0.0F,  0.0F,  1.0F,  0.0F,  //
      0.01F, 0.03F, 0.02F, 1.0F,  //
  };
  auto a = std::make_shared<HalColorTransformMatrix>(a_val);
  auto b = std::make_shared<HalColorTransformMatrix>(b_val);
  auto res = ColorUtil::Multiply(a, b);
  ASSERT_NE(res, nullptr);

  for (int col = 0; col < 4; ++col) {
    for (int row = 0; row < 4; ++row) {
      float expected = 0.0F;
      for (int k = 0; k < 4; ++k) {
        expected += a_val[(k * 4) + row] * b_val[(col * 4) + k];
      }
      EXPECT_NEAR((*res)[(col * 4) + row], expected, 1e-5F);
    }
  }
}

TEST(ColorUtilTest, MultiplyMatrixAssociativity) {
  auto a = std::make_shared<HalColorTransformMatrix>(HalColorTransformMatrix{
      0.9F,
      0.1F,
      0.0F,
      0.0F,  //
      0.0F,
      0.8F,
      0.2F,
      0.0F,  //
      0.1F,
      0.0F,
      0.7F,
      0.0F,  //
      0.0F,
      0.0F,
      0.0F,
      1.0F,  //
  });
  auto b = std::make_shared<HalColorTransformMatrix>(HalColorTransformMatrix{
      0.7F,
      0.2F,
      0.1F,
      0.0F,  //
      0.1F,
      0.6F,
      0.3F,
      0.0F,  //
      0.2F,
      0.1F,
      0.8F,
      0.0F,  //
      0.0F,
      0.0F,
      0.0F,
      1.0F,  //
  });
  auto c = std::make_shared<HalColorTransformMatrix>(HalColorTransformMatrix{
      0.8F,
      0.1F,
      0.1F,
      0.0F,  //
      0.2F,
      0.7F,
      0.1F,
      0.0F,  //
      0.1F,
      0.2F,
      0.6F,
      0.0F,  //
      0.0F,
      0.0F,
      0.0F,
      1.0F,  //
  });

  auto ab_c = ColorUtil::Multiply(ColorUtil::Multiply(a, b), c);
  auto a_bc = ColorUtil::Multiply(a, ColorUtil::Multiply(b, c));

  ASSERT_NE(ab_c, nullptr);
  ASSERT_NE(a_bc, nullptr);

  for (size_t i = 0; i < kColorMatrixSize; ++i) {
    EXPECT_NEAR((*ab_c)[i], (*a_bc)[i], 1e-5F);
  }
}

// =============================================================================
// 3. Gamut Adjustments & D65 White Point Preservation
// =============================================================================

TEST(ColorUtilTest, GamutAdjustIfNeededSameColorspace) {
  auto hal_matrix = std::make_shared<const HalColorTransformMatrix>(
      kIdentityMatrix);
  CscCache cache;

  auto ctm3x3 = ColorUtil::GamutAdjustIfNeeded<
      drm_color_ctm>(HwcColorspace::kBt709, HwcColorspace::kBt709, hal_matrix,
                     cache);
  ASSERT_NE(ctm3x3, nullptr);
  EXPECT_TRUE(cache.empty());

  auto ctm3x4 = ColorUtil::GamutAdjustIfNeeded<
      drm_color_ctm_3x4>(HwcColorspace::kBt709, HwcColorspace::kBt709,
                         hal_matrix, cache);
  ASSERT_NE(ctm3x4, nullptr);
  EXPECT_TRUE(cache.empty());
}

TEST(ColorUtilTest, GamutAdjustIfNeededDifferentColorspaceAndCaching) {
  auto hal_matrix = std::make_shared<const HalColorTransformMatrix>(
      kIdentityMatrix);
  CscCache cache;

  auto ctm3x3 = ColorUtil::GamutAdjustIfNeeded<
      drm_color_ctm>(HwcColorspace::kBt709, HwcColorspace::kDciP3, hal_matrix,
                     cache);
  ASSERT_NE(ctm3x3, nullptr);
  EXPECT_EQ(cache.size(), 1U);

  // Second call with same parameters should reuse cache entry
  auto ctm3x3_cached = ColorUtil::GamutAdjustIfNeeded<
      drm_color_ctm>(HwcColorspace::kBt709, HwcColorspace::kDciP3, hal_matrix,
                     cache);
  ASSERT_NE(ctm3x3_cached, nullptr);
  EXPECT_EQ(cache.size(), 1U);

  // Test 3x4 overload with different colorspaces
  auto ctm3x4 = ColorUtil::GamutAdjustIfNeeded<
      drm_color_ctm_3x4>(HwcColorspace::kBt709, HwcColorspace::kBt2020,
                         hal_matrix, cache);
  ASSERT_NE(ctm3x4, nullptr);
  EXPECT_EQ(cache.size(), 2U);
}

TEST(ColorUtilTest, GamutAdjustD65WhitePointPreservation) {
  const std::vector<std::pair<HwcColorspace, HwcColorspace>> gamut_pairs = {
      {HwcColorspace::kBt709, HwcColorspace::kDciP3},
      {HwcColorspace::kDciP3, HwcColorspace::kBt709},
      {HwcColorspace::kBt709, HwcColorspace::kBt2020},
      {HwcColorspace::kBt2020, HwcColorspace::kBt709},
      {HwcColorspace::kDciP3, HwcColorspace::kBt2020},
      {HwcColorspace::kBt2020, HwcColorspace::kDciP3},
  };

  auto hal_identity = std::make_shared<const HalColorTransformMatrix>(
      kIdentityMatrix);
  CscCache cache;

  for (const auto &[src, dest] : gamut_pairs) {
    auto ctm = ColorUtil::GamutAdjustIfNeeded<drm_color_ctm>(src, dest,
                                                             hal_identity,
                                                             cache);
    ASSERT_NE(ctm, nullptr);

    double r_out = From3132FixPt(ctm->matrix[0]) +
                   From3132FixPt(ctm->matrix[1]) +
                   From3132FixPt(ctm->matrix[2]);
    double g_out = From3132FixPt(ctm->matrix[3]) +
                   From3132FixPt(ctm->matrix[4]) +
                   From3132FixPt(ctm->matrix[5]);
    double b_out = From3132FixPt(ctm->matrix[6]) +
                   From3132FixPt(ctm->matrix[7]) +
                   From3132FixPt(ctm->matrix[8]);

    EXPECT_NEAR(r_out, 1.0, 1e-4);
    EXPECT_NEAR(g_out, 1.0, 1e-4);
    EXPECT_NEAR(b_out, 1.0, 1e-4);
  }
}

TEST(ColorUtilTest, GamutAdjust3x4OffsetVectorRotation) {
  HalColorTransformMatrix matrix = {
      1.0F, 0.0F, 0.0F, 0.0F,  //
      0.0F, 1.0F, 0.0F, 0.0F,  //
      0.0F, 0.0F, 1.0F, 0.0F,  //
      0.1F, 0.2F, 0.3F, 1.0F,  // Offsets O_src = [0.1, 0.2, 0.3]^T
  };
  auto hal_matrix = std::make_shared<const HalColorTransformMatrix>(matrix);
  CscCache cache;

  auto ctm3x4 = ColorUtil::GamutAdjustIfNeeded<
      drm_color_ctm_3x4>(HwcColorspace::kBt709, HwcColorspace::kBt2020,
                         hal_matrix, cache);
  ASSERT_NE(ctm3x4, nullptr);

  auto key = std::make_tuple(HwcColorspace::kBt709, HwcColorspace::kBt2020);
  ASSERT_EQ(cache.count(key), 1U);
  const mat3d &G = cache.at(key);

  double3 expected_o = G * double3(0.1, 0.2, 0.3);

  double actual_o_r = From3132FixPt(ctm3x4->matrix[3]);
  double actual_o_g = From3132FixPt(ctm3x4->matrix[7]);
  double actual_o_b = From3132FixPt(ctm3x4->matrix[11]);

  EXPECT_NEAR(actual_o_r, expected_o[0], 1e-4);
  EXPECT_NEAR(actual_o_g, expected_o[1], 1e-4);
  EXPECT_NEAR(actual_o_b, expected_o[2], 1e-4);
}

TEST(ColorUtilTest, GamutAdjust3x4FullMatrixAndOffset) {
  HalColorTransformMatrix matrix = {
      1.0F,  0.0F,  0.0F,  0.0F,  //
      0.0F,  0.85F, 0.0F,  0.0F,  //
      0.0F,  0.0F,  0.5F,  0.0F,  //
      0.05F, 0.1F,  0.15F, 1.0F,  // Offsets
  };
  auto hal_matrix = std::make_shared<const HalColorTransformMatrix>(matrix);
  CscCache cache;

  auto ctm3x4 = ColorUtil::GamutAdjustIfNeeded<
      drm_color_ctm_3x4>(HwcColorspace::kBt709, HwcColorspace::kDciP3,
                         hal_matrix, cache);
  ASSERT_NE(ctm3x4, nullptr);

  auto key = std::make_tuple(HwcColorspace::kBt709, HwcColorspace::kDciP3);
  ASSERT_EQ(cache.count(key), 1U);
  const mat3d &G = cache.at(key);

  double3 expected_o = G * double3(0.05, 0.1, 0.15);

  EXPECT_NEAR(From3132FixPt(ctm3x4->matrix[3]), expected_o[0], 1e-4);
  EXPECT_NEAR(From3132FixPt(ctm3x4->matrix[7]), expected_o[1], 1e-4);
  EXPECT_NEAR(From3132FixPt(ctm3x4->matrix[11]), expected_o[2], 1e-4);
}

TEST(ColorUtilTest, ToLinearCtmDiagonalScaling) {
  HalColorTransformMatrix night_light = {
      0.9F, 0.0F, 0.0F, 0.0F,  //
      0.0F, 0.7F, 0.0F, 0.0F,  //
      0.0F, 0.0F, 0.4F, 0.0F,  //
      0.0F, 0.0F, 0.0F, 1.0F,  //
  };

  auto linear_ctm = ColorUtil::ToLinearCtm(night_light, ColorMode::kSrgb);
  // Linearized diagonal values should differ from original (EOTF applied)
  EXPECT_NE(linear_ctm[0], night_light[0]);
  EXPECT_NE(linear_ctm[5], night_light[5]);
  EXPECT_NE(linear_ctm[10], night_light[10]);
  EXPECT_FLOAT_EQ(linear_ctm[15], 1.0F);

  // All off-diagonals should remain 0
  EXPECT_FLOAT_EQ(linear_ctm[1], 0.0F);
  EXPECT_FLOAT_EQ(linear_ctm[2], 0.0F);
  EXPECT_FLOAT_EQ(linear_ctm[3], 0.0F);
  EXPECT_FLOAT_EQ(linear_ctm[4], 0.0F);
  EXPECT_FLOAT_EQ(linear_ctm[6], 0.0F);
  EXPECT_FLOAT_EQ(linear_ctm[7], 0.0F);
  EXPECT_FLOAT_EQ(linear_ctm[8], 0.0F);
  EXPECT_FLOAT_EQ(linear_ctm[9], 0.0F);
  EXPECT_FLOAT_EQ(linear_ctm[11], 0.0F);
  EXPECT_FLOAT_EQ(linear_ctm[12], 0.0F);
  EXPECT_FLOAT_EQ(linear_ctm[13], 0.0F);
  EXPECT_FLOAT_EQ(linear_ctm[14], 0.0F);
}
TEST(ColorUtilTest, ToLinearCtmBypassesNonDiagonalAndPerspective) {
  // 1. Matrix with off-diagonal color cross-talk
  HalColorTransformMatrix cross_talk = {
      0.9F, 0.1F, 0.0F, 0.0F,  //
      0.1F, 0.8F, 0.0F, 0.0F,  //
      0.0F, 0.0F, 0.7F, 0.0F,  //
      0.0F, 0.0F, 0.0F, 1.0F,  //
  };
  EXPECT_EQ(ColorUtil::ToLinearCtm(cross_talk, ColorMode::kSrgb), cross_talk);

  // 2. Matrix with translation offsets (e.g. Invert Colors Y = 1 - X)
  HalColorTransformMatrix invert = {
      -1.0F, 0.0F,  0.0F,  0.0F,  //
      0.0F,  -1.0F, 0.0F,  0.0F,  //
      0.0F,  0.0F,  -1.0F, 0.0F,  //
      1.0F,  1.0F,  1.0F,  1.0F,  //
  };
  EXPECT_EQ(ColorUtil::ToLinearCtm(invert, ColorMode::kSrgb), invert);

  // 3. Matrix with non-zero perspective row entries (indices 3, 7, 11)
  HalColorTransformMatrix perspective = {
      1.0F, 0.0F, 0.0F, 0.1F,  // m[3] is non-zero
      0.0F, 1.0F, 0.0F, 0.0F,  //
      0.0F, 0.0F, 1.0F, 0.0F,  //
      0.0F, 0.0F, 0.0F, 1.0F,  //
  };
  EXPECT_EQ(ColorUtil::ToLinearCtm(perspective, ColorMode::kSrgb), perspective);

  // 4. Matrix with positive translation offsets (indices 12, 13, 14) without
  // negative values
  HalColorTransformMatrix translation = {
      1.0F, 0.0F, 0.0F, 0.0F,  //
      0.0F, 1.0F, 0.0F, 0.0F,  //
      0.0F, 0.0F, 1.0F, 0.0F,  //
      0.5F, 0.2F, 0.1F, 1.0F,  //
  };
  EXPECT_EQ(ColorUtil::ToLinearCtm(translation, ColorMode::kSrgb), translation);
}
// Tests for GetDegammaLut
TEST(ColorUtilTest, GetDegammaLutInvalidLutSize) {
  Lut1DCache<drm_color_lut32> cache;

  // Invalid size < 2
  const auto &lut0 = ColorUtil::GetDegammaLut(TransferFunction::kPq, 0, cache,
                                              1.0F);
  EXPECT_TRUE(lut0.empty());

  const auto &lut1 = ColorUtil::GetDegammaLut(TransferFunction::kPq, 1, cache,
                                              1.0F);
  EXPECT_TRUE(lut1.empty());
}

TEST(ColorUtilTest, GetDegammaLutValidAndCaching) {
  Lut1DCache<drm_color_lut32> cache;

  static constexpr size_t kLutSize = 256;
  const auto &lut = ColorUtil::GetDegammaLut(TransferFunction::kPq, kLutSize,
                                             cache, 1.0F);

  ASSERT_EQ(lut.size(), kLutSize);
  EXPECT_EQ(cache.size(), 1U);

  // Check initial and final values (linear / EOTF mapping)
  EXPECT_EQ(lut[0].red, 0U);
  EXPECT_EQ(lut[0].green, 0U);
  EXPECT_EQ(lut[0].blue, 0U);

  EXPECT_GT(lut[kLutSize - 1].red, 0U);

  // Second call with identical arguments should return exact cached reference
  const auto &lut_cached = ColorUtil::GetDegammaLut(TransferFunction::kPq,
                                                    kLutSize, cache, 1.0F);
  EXPECT_EQ(&lut, &lut_cached);
}

// Tests for GetGammaLut
TEST(ColorUtilTest, GetGammaLutInvalidLutSize) {
  Lut1DCache<drm_color_lut> cache;

  const auto &lut = ColorUtil::GetGammaLut(TransferFunction::kHlg, 1, cache,
                                           1.0F, 1.0F);
  EXPECT_TRUE(lut.empty());
}

TEST(ColorUtilTest, GetGammaLutValidAndCaching) {
  Lut1DCache<drm_color_lut> cache;

  static constexpr size_t kLutSize = 512;
  const auto &lut = ColorUtil::GetGammaLut(TransferFunction::kHlg, kLutSize,
                                           cache, 1.0F, 1.0F);

  ASSERT_EQ(lut.size(), kLutSize);
  EXPECT_EQ(cache.size(), 1U);

  EXPECT_EQ(lut[0].red, 0U);
  EXPECT_EQ(lut[0].green, 0U);
  EXPECT_EQ(lut[0].blue, 0U);

  EXPECT_GT(lut[kLutSize - 1].red, 0U);

  // Second call with identical arguments should return exact cached reference
  const auto &lut_cached = ColorUtil::GetGammaLut(TransferFunction::kHlg,
                                                  kLutSize, cache, 1.0F, 1.0F);
  EXPECT_EQ(&lut, &lut_cached);
}

TEST(ColorUtilTest, GetGammaLutHdrClampsToMinFloorAtZeroDisplayBrightness) {
  Lut1DCache<drm_color_lut> cache;
  ScopedTestProperty min_prop("vendor.hwc.drm.min_display_brightness", "0.01");

  static constexpr size_t kLutSize = 512;
  const auto &lut = ColorUtil::GetGammaLut(TransferFunction::kHlg, kLutSize,
                                           cache,
                                           ColorUtil::ScaleBrightnessIfNeeded(
                                               0.0F),
                                           1.0F);

  ASSERT_EQ(lut.size(), kLutSize);
  EXPECT_GT(lut[kLutSize - 1].red, 0U);
  EXPECT_GT(lut[kLutSize - 1].green, 0U);
  EXPECT_GT(lut[kLutSize - 1].blue, 0U);
}

// Tests for ScaleBrightnessIfNeeded
TEST(ColorUtilTest, ScaleBrightnessIfNeededDefaultZero) {
  EXPECT_FLOAT_EQ(ColorUtil::ScaleBrightnessIfNeeded(-1.0F), -1.0F);
  EXPECT_FLOAT_EQ(ColorUtil::ScaleBrightnessIfNeeded(0.0F), 0.0F);
  EXPECT_FLOAT_EQ(ColorUtil::ScaleBrightnessIfNeeded(0.5F), 0.5F);
  EXPECT_FLOAT_EQ(ColorUtil::ScaleBrightnessIfNeeded(1.0F), 1.0F);
}

TEST(ColorUtilTest, ScaleBrightnessIfNeededClampsToMinFloor) {
  ScopedTestProperty prop("vendor.hwc.drm.min_display_brightness", "0.01");

  EXPECT_FLOAT_EQ(ColorUtil::ScaleBrightnessIfNeeded(-1.0F), -1.0F);
  EXPECT_FLOAT_EQ(ColorUtil::ScaleBrightnessIfNeeded(0.0F), 0.01F);
  EXPECT_FLOAT_EQ(ColorUtil::ScaleBrightnessIfNeeded(0.005F), 0.01F);
  EXPECT_FLOAT_EQ(ColorUtil::ScaleBrightnessIfNeeded(0.5F), 0.5F);
  EXPECT_FLOAT_EQ(ColorUtil::ScaleBrightnessIfNeeded(1.0F), 1.0F);
}

TEST(ColorUtilTest, ScaleBrightnessIfNeededInvalidStringDefaultsToZero) {
  ScopedTestProperty prop("vendor.hwc.drm.min_display_brightness", "invalid");

  EXPECT_FLOAT_EQ(ColorUtil::ScaleBrightnessIfNeeded(0.0F), 0.0F);
  EXPECT_FLOAT_EQ(ColorUtil::ScaleBrightnessIfNeeded(0.5F), 0.5F);
}

TEST(ColorUtilTest, ScaleBrightnessIfNeededOutOfRangeDefaultsToZero) {
  ScopedTestProperty prop("vendor.hwc.drm.min_display_brightness", "1.5");

  EXPECT_FLOAT_EQ(ColorUtil::ScaleBrightnessIfNeeded(0.0F), 0.0F);
  EXPECT_FLOAT_EQ(ColorUtil::ScaleBrightnessIfNeeded(0.5F), 0.5F);
}

TEST(ColorUtilTest, ScaleBrightnessIfNeededRangeScaleNoopWhenMinZero) {
  ScopedTestProperty
      scale_prop("vendor.hwc.drm.scale_brightness_range_to_min_brightness",
                 "true");

  EXPECT_FLOAT_EQ(ColorUtil::ScaleBrightnessIfNeeded(0.0F), 0.0F);
  EXPECT_FLOAT_EQ(ColorUtil::ScaleBrightnessIfNeeded(0.5F), 0.5F);
  EXPECT_FLOAT_EQ(ColorUtil::ScaleBrightnessIfNeeded(1.0F), 1.0F);
}

TEST(ColorUtilTest, ScaleBrightnessIfNeededRangeScaleNonZeroAtZero) {
  ScopedTestProperty min_prop("vendor.hwc.drm.min_display_brightness", "0.1");
  ScopedTestProperty
      scale_prop("vendor.hwc.drm.scale_brightness_range_to_min_brightness",
                 "true");

  EXPECT_FLOAT_EQ(ColorUtil::ScaleBrightnessIfNeeded(0.0F), 0.1F);
}

TEST(ColorUtilTest, ScaleBrightnessIfNeededRangeScaleCalculatesExpectedScale) {
  ScopedTestProperty min_prop("vendor.hwc.drm.min_display_brightness", "0.1");
  ScopedTestProperty
      scale_prop("vendor.hwc.drm.scale_brightness_range_to_min_brightness",
                 "true");

  // expected scale = 0.1 + 0.5 * (1.0 - 0.1) = 0.55
  EXPECT_FLOAT_EQ(ColorUtil::ScaleBrightnessIfNeeded(0.5F), 0.55F);
  EXPECT_FLOAT_EQ(ColorUtil::ScaleBrightnessIfNeeded(1.0F), 1.0F);
}

TEST(ColorUtilTest, GetGammaLutHdrScaleRangeNoopWhenMinZero) {
  Lut1DCache<drm_color_lut> cache;
  ScopedTestProperty
      scale_prop("vendor.hwc.drm.scale_brightness_range_to_min_brightness",
                 "true");

  static constexpr size_t kLutSize = 512;
  const auto &lut = ColorUtil::GetGammaLut(TransferFunction::kHlg, kLutSize,
                                           cache,
                                           ColorUtil::ScaleBrightnessIfNeeded(
                                               0.0F),
                                           1.0F);

  ASSERT_EQ(lut.size(), kLutSize);
  EXPECT_EQ(lut[kLutSize - 1].red, 0U);
  EXPECT_EQ(lut[kLutSize - 1].green, 0U);
  EXPECT_EQ(lut[kLutSize - 1].blue, 0U);
}

TEST(ColorUtilTest, GetGammaLutHdrScaleRangeNonZeroAtZero) {
  Lut1DCache<drm_color_lut> cache;
  ScopedTestProperty min_prop("vendor.hwc.drm.min_display_brightness", "0.1");
  ScopedTestProperty
      scale_prop("vendor.hwc.drm.scale_brightness_range_to_min_brightness",
                 "true");

  static constexpr size_t kLutSize = 512;
  const auto &lut = ColorUtil::GetGammaLut(TransferFunction::kHlg, kLutSize,
                                           cache,
                                           ColorUtil::ScaleBrightnessIfNeeded(
                                               0.0F),
                                           1.0F);

  ASSERT_EQ(lut.size(), kLutSize);
  EXPECT_GT(lut[kLutSize - 1].red, 0U);
  EXPECT_GT(lut[kLutSize - 1].green, 0U);
  EXPECT_GT(lut[kLutSize - 1].blue, 0U);
}

TEST(ColorUtilTest, GetGammaLutHdrScaleRangeCalculatesExpectedScale) {
  Lut1DCache<drm_color_lut> cache;
  static constexpr size_t kLutSize = 512;

  const auto &lut_scaled = [&]() -> const auto & {
    ScopedTestProperty min_prop("vendor.hwc.drm.min_display_brightness", "0.1");
    ScopedTestProperty
        scale_prop("vendor.hwc.drm.scale_brightness_range_to_min_brightness",
                   "true");
    return ColorUtil::GetGammaLut(TransferFunction::kHlg, kLutSize, cache,
                                  ColorUtil::ScaleBrightnessIfNeeded(0.5F),
                                  1.0F);
  }();

  const auto &lut_expected = ColorUtil::GetGammaLut(TransferFunction::kHlg,
                                                    kLutSize, cache, 0.55F,
                                                    1.0F);

  EXPECT_EQ(&lut_scaled, &lut_expected);
}

TEST(ColorUtilTest, ToColorGamutMappings) {
  EXPECT_EQ(ColorUtil::ToColorGamut(HwcColorspace::kDefault).getName(),
            android::ColorSpace::BT709().getName());
  EXPECT_EQ(ColorUtil::ToColorGamut(HwcColorspace::kBt709).getName(),
            android::ColorSpace::BT709().getName());
  EXPECT_EQ(ColorUtil::ToColorGamut(HwcColorspace::kBt2020).getName(),
            android::ColorSpace::BT2020().getName());
  EXPECT_EQ(ColorUtil::ToColorGamut(HwcColorspace::kDciP3).getName(),
            android::ColorSpace::DCIP3().getName());
  EXPECT_EQ(ColorUtil::ToColorGamut(HwcColorspace::kBt601).getName(),
            android::ColorSpace::sRGB().getName());
}

TEST(ColorUtilTest, ToHwcColorspaceManyToOneMappings) {
  EXPECT_EQ(ColorUtil::ToHwcColorspace(ColorMode::kNative),
            HwcColorspace::kDefault);

  EXPECT_EQ(ColorUtil::ToHwcColorspace(ColorMode::kBt601_625),
            HwcColorspace::kBt601);
  EXPECT_EQ(ColorUtil::ToHwcColorspace(ColorMode::kBt601_625Unadjusted),
            HwcColorspace::kBt601);
  EXPECT_EQ(ColorUtil::ToHwcColorspace(ColorMode::kBt601_525),
            HwcColorspace::kBt601);
  EXPECT_EQ(ColorUtil::ToHwcColorspace(ColorMode::kBt601_525Unadjusted),
            HwcColorspace::kBt601);

  EXPECT_EQ(ColorUtil::ToHwcColorspace(ColorMode::kSrgb),
            HwcColorspace::kBt709);
  EXPECT_EQ(ColorUtil::ToHwcColorspace(ColorMode::kBt709),
            HwcColorspace::kBt709);

  EXPECT_EQ(ColorUtil::ToHwcColorspace(ColorMode::kDciP3),
            HwcColorspace::kDciP3);
  EXPECT_EQ(ColorUtil::ToHwcColorspace(ColorMode::kDisplayP3),
            HwcColorspace::kDciP3);

  EXPECT_EQ(ColorUtil::ToHwcColorspace(ColorMode::kBt2020),
            HwcColorspace::kBt2020);
  EXPECT_EQ(ColorUtil::ToHwcColorspace(ColorMode::kDisplayBt2020),
            HwcColorspace::kBt2020);

  EXPECT_EQ(ColorUtil::ToHwcColorspace(ColorMode::kAdobeRgb),
            HwcColorspace::kDefault);
  EXPECT_EQ(ColorUtil::ToHwcColorspace(ColorMode::kBt2100Pq),
            HwcColorspace::kDefault);
  EXPECT_EQ(ColorUtil::ToHwcColorspace(ColorMode::kBt2100Hlg),
            HwcColorspace::kDefault);
}

TEST(ColorUtilTest, GetEotfManyToOneMappings) {
  EXPECT_FLOAT_EQ(ColorUtil::GetEotf(ColorMode::kSrgb)(0.5F),
                  android::ColorSpace::sRGB().getEOTF()(0.5F));
  EXPECT_FLOAT_EQ(ColorUtil::GetEotf(ColorMode::kBt601_625)(0.5F),
                  android::ColorSpace::sRGB().getEOTF()(0.5F));
  EXPECT_FLOAT_EQ(ColorUtil::GetEotf(ColorMode::kBt601_625Unadjusted)(0.5F),
                  android::ColorSpace::sRGB().getEOTF()(0.5F));
  EXPECT_FLOAT_EQ(ColorUtil::GetEotf(ColorMode::kBt601_525)(0.5F),
                  android::ColorSpace::sRGB().getEOTF()(0.5F));
  EXPECT_FLOAT_EQ(ColorUtil::GetEotf(ColorMode::kBt601_525Unadjusted)(0.5F),
                  android::ColorSpace::sRGB().getEOTF()(0.5F));

  EXPECT_FLOAT_EQ(ColorUtil::GetEotf(ColorMode::kBt709)(0.5F),
                  android::ColorSpace::BT709().getEOTF()(0.5F));

  EXPECT_FLOAT_EQ(ColorUtil::GetEotf(ColorMode::kDciP3)(0.5F),
                  android::ColorSpace::DCIP3().getEOTF()(0.5F));
  EXPECT_FLOAT_EQ(ColorUtil::GetEotf(ColorMode::kDisplayP3)(0.5F),
                  android::ColorSpace::DCIP3().getEOTF()(0.5F));

  EXPECT_FLOAT_EQ(ColorUtil::GetEotf(ColorMode::kBt2020)(0.5F),
                  android::ColorSpace::BT2020().getEOTF()(0.5F));
  EXPECT_FLOAT_EQ(ColorUtil::GetEotf(ColorMode::kDisplayBt2020)(0.5F),
                  android::ColorSpace::BT2020().getEOTF()(0.5F));
}

TEST(ColorUtilTest, ToLinearCtmManyToOneMappings) {
  constexpr HalColorTransformMatrix kCtm = {
      0.5F, 0.0F, 0.0F, 0.0F, 0.0F, 0.5F, 0.0F, 0.0F,
      0.0F, 0.0F, 0.5F, 0.0F, 0.0F, 0.0F, 0.0F, 1.0F,
  };

  EXPECT_FLOAT_EQ(ColorUtil::ToLinearCtm(kCtm, ColorMode::kBt601_625)[0],
                  ColorUtil::ToLinearCtm(kCtm, ColorMode::kSrgb)[0]);
  EXPECT_FLOAT_EQ(ColorUtil::ToLinearCtm(kCtm,
                                         ColorMode::kBt601_625Unadjusted)[0],
                  ColorUtil::ToLinearCtm(kCtm, ColorMode::kSrgb)[0]);
  EXPECT_FLOAT_EQ(ColorUtil::ToLinearCtm(kCtm, ColorMode::kBt601_525)[0],
                  ColorUtil::ToLinearCtm(kCtm, ColorMode::kSrgb)[0]);
  EXPECT_FLOAT_EQ(ColorUtil::ToLinearCtm(kCtm,
                                         ColorMode::kBt601_525Unadjusted)[0],
                  ColorUtil::ToLinearCtm(kCtm, ColorMode::kSrgb)[0]);

  EXPECT_FLOAT_EQ(ColorUtil::ToLinearCtm(kCtm, ColorMode::kDciP3)[0],
                  ColorUtil::ToLinearCtm(kCtm, ColorMode::kDisplayP3)[0]);

  EXPECT_FLOAT_EQ(ColorUtil::ToLinearCtm(kCtm, ColorMode::kBt2020)[0],
                  ColorUtil::ToLinearCtm(kCtm, ColorMode::kDisplayBt2020)[0]);
}

TEST(ColorUtilTest, To3132FixPtBasics) {
  EXPECT_EQ(ColorUtil::To3132FixPt(0.0), 0ULL);
  EXPECT_EQ(ColorUtil::To3132FixPt(1.0), 1ULL << 32);
  EXPECT_EQ(ColorUtil::To3132FixPt(2.0), 2ULL << 32);
  EXPECT_EQ(ColorUtil::To3132FixPt(0.5), 1ULL << 31);
  EXPECT_EQ(ColorUtil::To3132FixPt(0.25), 1ULL << 30);
  EXPECT_EQ(ColorUtil::To3132FixPt(0.125), 1ULL << 29);

  constexpr uint64_t kSignBit = 1ULL << 63;
  EXPECT_EQ(ColorUtil::To3132FixPt(-1.0), kSignBit | (1ULL << 32));
  EXPECT_EQ(ColorUtil::To3132FixPt(-2.0), kSignBit | (2ULL << 32));
  EXPECT_EQ(ColorUtil::To3132FixPt(-0.5), kSignBit | (1ULL << 31));

  // Smallest representable non-zero step: 2^-32
  constexpr double kQuantum = 1.0 / 4294967296.0;
  EXPECT_EQ(ColorUtil::To3132FixPt(kQuantum), 1ULL);
  EXPECT_EQ(ColorUtil::To3132FixPt(-kQuantum), kSignBit | 1ULL);
}

TEST(ColorUtilTest, To3132FixPtRounding) {
  constexpr double kQuantum = 1.0 / 4294967296.0;

  // 0.4 * quantum should round down to 0
  EXPECT_EQ(ColorUtil::To3132FixPt(0.4 * kQuantum), 0ULL);

  // 0.6 * quantum should round up to 1
  EXPECT_EQ(ColorUtil::To3132FixPt(0.6 * kQuantum), 1ULL);

  // 1.4 * quantum should round to 1
  EXPECT_EQ(ColorUtil::To3132FixPt(1.4 * kQuantum), 1ULL);

  // 1.6 * quantum should round to 2
  EXPECT_EQ(ColorUtil::To3132FixPt(1.6 * kQuantum), 2ULL);
}

TEST(ColorUtilTest, To3132FixPtSaturationAndSpecialValues) {
  constexpr uint64_t kSignBit = 1ULL << 63;
  constexpr uint64_t kValueMask = (1ULL << 63) - 1;

  // Values exceeding (2^31 - 1) saturate at maximum magnitude
  EXPECT_EQ(ColorUtil::To3132FixPt(3e9), kValueMask);
  EXPECT_EQ(ColorUtil::To3132FixPt(-3e9), kSignBit | kValueMask);

  // Large infinity saturates
  EXPECT_EQ(ColorUtil::To3132FixPt(std::numeric_limits<double>::infinity()),
            kValueMask);
  EXPECT_EQ(ColorUtil::To3132FixPt(-std::numeric_limits<double>::infinity()),
            kSignBit | kValueMask);

  // NaN returns 0
  EXPECT_EQ(ColorUtil::To3132FixPt(std::numeric_limits<double>::quiet_NaN()),
            0ULL);

  // Negative zero
  EXPECT_EQ(ColorUtil::To3132FixPt(-0.0), kSignBit);
}

TEST(ColorUtilTest, TransformHasOffsetValueDetection) {
  // Red offset at index 12
  HalColorTransformMatrix matrix_r = kIdentityMatrix;
  matrix_r[12] = 0.05F;
  EXPECT_TRUE(ColorUtil::TransformHasOffsetValue(matrix_r));

  // Green offset at index 13
  HalColorTransformMatrix matrix_g = kIdentityMatrix;
  matrix_g[13] = -0.05F;
  EXPECT_TRUE(ColorUtil::TransformHasOffsetValue(matrix_g));

  // Blue offset at index 14
  HalColorTransformMatrix matrix_b = kIdentityMatrix;
  matrix_b[14] = 0.1F;
  EXPECT_TRUE(ColorUtil::TransformHasOffsetValue(matrix_b));

  // Identity matrix has no offsets
  EXPECT_FALSE(ColorUtil::TransformHasOffsetValue(kIdentityMatrix));

  // Sub-epsilon offset (< 0.001F) is ignored as noise
  HalColorTransformMatrix matrix_sub_eps = kIdentityMatrix;
  matrix_sub_eps[12] = 0.0001F;
  EXPECT_FALSE(ColorUtil::TransformHasOffsetValue(matrix_sub_eps));
}

TEST(ColorUtilTest, ToDrmColorspaceMappings) {
  EXPECT_EQ(ColorUtil::ToDrmColorspace(HwcColorspace::kDefault),
            DrmColorspace::kDefault);
  EXPECT_EQ(ColorUtil::ToDrmColorspace(HwcColorspace::kBt601),
            DrmColorspace::kDefault);
  EXPECT_EQ(ColorUtil::ToDrmColorspace(HwcColorspace::kBt709),
            DrmColorspace::kDefault);
  EXPECT_EQ(ColorUtil::ToDrmColorspace(HwcColorspace::kDciP3),
            DrmColorspace::kDciP3RgbD65);
  EXPECT_EQ(ColorUtil::ToDrmColorspace(HwcColorspace::kBt2020),
            DrmColorspace::kBt2020Rgb);
}

}  // namespace android::drm_hwcomposer

// NOLINTEND(readability-magic-numbers)