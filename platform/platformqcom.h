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
 *
 * Changes from Qualcomm Innovation Center are provided under the following license:
 * Copyright (c) 2022 Qualcomm Innovation Center, Inc. All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted (subject to the limitations in the
 * disclaimer below) provided that the following conditions are met:
 *
 *   * Redistributions of source code must retain the above copyright
 *     notice, this list of conditions and the following disclaimer.
 *
 *   * Redistributions in binary form must reproduce the above
 *     copyright notice, this list of conditions and the following
 *     disclaimer in the documentation and/or other materials provided
 *     with the distribution.
 *
 *   * Neither the name of Qualcomm Innovation Center, Inc. nor the names of its
 *     contributors may be used to endorse or promote products derived
 *     from this software without specific prior written permission.
 *
 * NO EXPRESS OR IMPLIED LICENSES TO ANY PARTY'S PATENT RIGHTS ARE
 * GRANTED BY THIS LICENSE. THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT
 * HOLDERS AND CONTRIBUTORS "AS IS" AND ANY EXPRESS OR IMPLIED
 * WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED WARRANTIES OF
 * MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE DISCLAIMED.
 * IN NO EVENT SHALL THE COPYRIGHT HOLDER OR CONTRIBUTORS BE LIABLE FOR
 * ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL
 * DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE
 * GOODS OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS
 * INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER
 * IN CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR
 * OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN
 * IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 */

#ifndef ANDROID_PLATFORM_QCOM_H_
#define ANDROID_PLATFORM_QCOM_H_

#include "drmdevice.h"
#include "platform.h"
#include "platformdrmgeneric.h"

#include <stdatomic.h>
#include <list>
#include <gr_priv_handle.h>
#include <hardware/gralloc.h>

namespace android {

class QcomImporter : public DrmGenericImporter {
 public:
  using DrmGenericImporter::DrmGenericImporter;

  QcomImporter(DrmDevice *drm);
  ~QcomImporter() override;

  int Init();

  int ImportBuffer(buffer_handle_t handle, hwc_drm_bo_t *bo) override;
  int ReleaseBuffer(hwc_drm_bo_t *bo) override;

  int ConvertBoInfo(buffer_handle_t handle, hwc_drm_bo_t *bo) override;
  uint32_t ConvertHalFormatToDrm(uint32_t hal_format);

 private:
  typedef struct {
    native_handle_t *handle;
    uint64_t handle_id;
    int gem_handle;
    int fb_id;
    int ref;
  } QcomBuffer_t;

  QcomBuffer_t* GetQcomBuffer(struct private_handle_t* handle);
  int SetQcomBuffer(QcomBuffer_t *buf);
  int RemoveQcomBuffer(QcomBuffer_t *buf);
  uint64_t ConvertGrallocFormatToDrmModifiers(uint32_t hal_format,
                  uint64_t flags, uint64_t usage);
  bool IsUncompressedRGBFormat(int format);
  uint32_t GetBppForUncompressedRGB(int format);
  int GetBpp(int format);
  void Dump(void);

  std::list<QcomBuffer_t*> buffers_list_;
  DrmDevice *drm_;
  const gralloc_module_t *gralloc_;
};
}  // namespace android

#endif
