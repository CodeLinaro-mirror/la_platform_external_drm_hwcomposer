/*
 * Copyright (C) 2022 The Android Open Source Project
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

#include <drm_mode.h>

#include "compositor/DisplayInfo.h"
#include "drm/drm_mode.h"

namespace android::drm_hwcomposer {

class ColorUtil {
 public:
  /* HAL provides a transposed 4x4 float type matrix:
   * | 0  1  2  3|
   * | 4  5  6  7|
   * | 8  9 10 11|
   * |12 13 14 15|
   *
   * R_out = R*0 + G*4 + B*8 + 12
   * G_out = R*1 + G*5 + B*9 + 13
   * B_out = R*2 + G*6 + B*10 + 14
   *
   * drm_color_ctm expects a 3x3 s31.32 fixed point matrix:
   * out   matrix    in
   * |R|   |0 1 2|   |R|
   * |G| = |3 4 5| x |G|
   * |B|   |6 7 8|   |B|
   *
   * R_out = R*0 + G*1 + B*2
   * G_out = R*3 + G*4 + B*5
   * B_out = R*6 + G*7 + B*8
   */
  static std::shared_ptr<drm_color_ctm> ToColorTransform3x3(
      const HalColorTransforMatrix &color_transform_matrix);

  /* HAL provides a transposed 4x4 float type matrix:
   * | 0  1  2  3|
   * | 4  5  6  7|
   * | 8  9 10 11|
   * |12 13 14 15|
   *
   * R_out = R*0 + G*4 + B*8 + 12
   * G_out = R*1 + G*5 + B*9 + 13
   * B_out = R*2 + G*6 + B*10 + 14
   *
   * drm_color_ctm_3x4 expects a 3x4 s31.32 fixed point matrix:
   * out   matrix          in
   * |R|   |0  1  2  3 |   | R |
   * |G| = |4  5  6  7 | x | G |
   * |B|   |8  9  10 11|   | B |
   *                       |1.0|
   *
   * R_out = R*0 + G*1 + B*2 + 3
   * G_out = R*4 + G*5 + B*6 + 7
   * B_out = R*8 + G*9 + B*10 + 11
   */
  static std::shared_ptr<drm_color_ctm_3x4> ToColorTransform3x4(
      const HalColorTransforMatrix &color_transform_matrix);
};

}  // namespace android::drm_hwcomposer
