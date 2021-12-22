/*
 * Copyright (C) 2016 The Android Open Source Project
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

#define LOG_TAG "hwc-platform-qcom"

#include "platformqcom.h"
#include "platform.h"

#include <stdatomic.h>
#include <xf86drm.h>
#include <xf86drmMode.h>
#include <cinttypes>

#include <log/log.h>
#include <QtiGrallocDefs.h>
#include <gr_utils.h>
#include <linux/dma-buf.h>


namespace android {

#define MAX_DELAYED_RELEASE_SLOTS   16

Importer *Importer::CreateInstance(DrmDevice *drm) {
  QcomImporter *importer = new QcomImporter(drm);
  if (!importer)
    return NULL;

  int ret = importer->Init();
  if (ret) {
    ALOGE("Failed to initialize the QCOM importer %d", ret);
    delete importer;
    return NULL;
  }
  return importer;
}

uint32_t QcomImporter::ConvertHalFormatToDrm(uint32_t hal_format) {
  switch (hal_format) {
    case HAL_PIXEL_FORMAT_RGB_888:
      return DRM_FORMAT_BGR888;
    case HAL_PIXEL_FORMAT_BGRA_8888:
      return DRM_FORMAT_ARGB8888;
    case HAL_PIXEL_FORMAT_RGBX_8888:
      return DRM_FORMAT_XBGR8888;
    case HAL_PIXEL_FORMAT_RGBA_8888:
      return DRM_FORMAT_ABGR8888;
    case HAL_PIXEL_FORMAT_RGB_565:
      return DRM_FORMAT_BGR565;
    case HAL_PIXEL_FORMAT_YV12:
      return DRM_FORMAT_YVU420;

    // Vendor uncompressed RGB
    case HAL_PIXEL_FORMAT_BGR_888:
      return DRM_FORMAT_RGB888;
    case HAL_PIXEL_FORMAT_RGBA_5551:
      return DRM_FORMAT_ABGR1555;
    case HAL_PIXEL_FORMAT_BGR_565:
      return DRM_FORMAT_RGB565;
    case HAL_PIXEL_FORMAT_RGBA_4444:
      return DRM_FORMAT_ABGR4444;
    case HAL_PIXEL_FORMAT_R_8:
      return DRM_FORMAT_R8;
    case HAL_PIXEL_FORMAT_RG_88:
      return DRM_FORMAT_GR88;
    case HAL_PIXEL_FORMAT_BGRX_8888:
      return DRM_FORMAT_XRGB8888;
    case HAL_PIXEL_FORMAT_RGBA_1010102:
      return DRM_FORMAT_ABGR2101010;
    case HAL_PIXEL_FORMAT_ARGB_2101010:
      return DRM_FORMAT_BGRA1010102;
    case HAL_PIXEL_FORMAT_RGBX_1010102:
      return DRM_FORMAT_XBGR2101010;
    case HAL_PIXEL_FORMAT_XRGB_2101010:
      return DRM_FORMAT_BGRX1010102;
    case HAL_PIXEL_FORMAT_BGRA_1010102:
      return DRM_FORMAT_ARGB2101010;
    case HAL_PIXEL_FORMAT_ABGR_2101010:
      return DRM_FORMAT_RGBA1010102;
    case HAL_PIXEL_FORMAT_BGRX_1010102:
      return DRM_FORMAT_XRGB2101010;
    case HAL_PIXEL_FORMAT_XBGR_2101010:
      return DRM_FORMAT_RGBX1010102;

    // Vendor YUV
    case HAL_PIXEL_FORMAT_YCbCr_420_SP:
      return DRM_FORMAT_YUV420;
    case HAL_PIXEL_FORMAT_YCbCr_422_SP:
      return DRM_FORMAT_YUV422;
    case HAL_PIXEL_FORMAT_YCbCr_420_SP_VENUS:
    case HAL_PIXEL_FORMAT_NV12_ENCODEABLE:  // Same as YCbCr_420_SP_VENUS
      return DRM_FORMAT_YUV420;
    case HAL_PIXEL_FORMAT_NV21_ENCODEABLE:
      return DRM_FORMAT_NV21;
    case HAL_PIXEL_FORMAT_YCbCr_420_SP_VENUS_UBWC:
      return DRM_FORMAT_YUV420;
    case HAL_PIXEL_FORMAT_YCrCb_420_SP:
      return DRM_FORMAT_YVU420;
    case HAL_PIXEL_FORMAT_YCrCb_422_SP:
      return DRM_FORMAT_YVU420;
    case HAL_PIXEL_FORMAT_YCrCb_420_SP_ADRENO:
      return DRM_FORMAT_YVU420;
    case HAL_PIXEL_FORMAT_YCrCb_420_SP_VENUS:
      return DRM_FORMAT_YVU420;
    case HAL_PIXEL_FORMAT_NV21_ZSL:
      return DRM_FORMAT_NV21;
    case HAL_PIXEL_FORMAT_RAW16:
      return DRM_FORMAT_R16;
    case HAL_PIXEL_FORMAT_Y16:
      return DRM_FORMAT_R16;
    case HAL_PIXEL_FORMAT_Y8:
      return DRM_FORMAT_R8;
    case HAL_PIXEL_FORMAT_YCbCr_420_P010:
      return DRM_FORMAT_YUV420_10BIT;
    case HAL_PIXEL_FORMAT_YCbCr_420_TP10_UBWC:
      return DRM_FORMAT_YUV420_10BIT;
    case HAL_PIXEL_FORMAT_YCbCr_420_P010_UBWC:
      return DRM_FORMAT_YUV420_10BIT;
    case HAL_PIXEL_FORMAT_YCbCr_420_P010_VENUS:
      return DRM_FORMAT_YUV420_10BIT;

    // Below formats used by camera and VR
    case HAL_PIXEL_FORMAT_NV12_HEIF:
      return DRM_FORMAT_NV12;
    case HAL_PIXEL_FORMAT_CbYCrY_422_I:
      return DRM_FORMAT_UYVY;

    // TODO:
    case HAL_PIXEL_FORMAT_RGBA_FP16:
    case HAL_PIXEL_FORMAT_RAW12:
    case HAL_PIXEL_FORMAT_RAW10:
    // Vendor compressed RGB
    case HAL_PIXEL_FORMAT_COMPRESSED_RGBA_ASTC_4x4_KHR:
    case HAL_PIXEL_FORMAT_COMPRESSED_SRGB8_ALPHA8_ASTC_4x4_KHR:
    case HAL_PIXEL_FORMAT_COMPRESSED_RGBA_ASTC_5x4_KHR:
    case HAL_PIXEL_FORMAT_COMPRESSED_SRGB8_ALPHA8_ASTC_5x4_KHR:
    case HAL_PIXEL_FORMAT_COMPRESSED_RGBA_ASTC_5x5_KHR:
    case HAL_PIXEL_FORMAT_COMPRESSED_SRGB8_ALPHA8_ASTC_5x5_KHR:
    case HAL_PIXEL_FORMAT_COMPRESSED_RGBA_ASTC_6x5_KHR:
    case HAL_PIXEL_FORMAT_COMPRESSED_SRGB8_ALPHA8_ASTC_6x5_KHR:
    case HAL_PIXEL_FORMAT_COMPRESSED_RGBA_ASTC_6x6_KHR:
    case HAL_PIXEL_FORMAT_COMPRESSED_SRGB8_ALPHA8_ASTC_6x6_KHR:
    case HAL_PIXEL_FORMAT_COMPRESSED_RGBA_ASTC_8x5_KHR:
    case HAL_PIXEL_FORMAT_COMPRESSED_SRGB8_ALPHA8_ASTC_8x5_KHR:
    case HAL_PIXEL_FORMAT_COMPRESSED_RGBA_ASTC_8x6_KHR:
    case HAL_PIXEL_FORMAT_COMPRESSED_SRGB8_ALPHA8_ASTC_8x6_KHR:
    case HAL_PIXEL_FORMAT_COMPRESSED_RGBA_ASTC_8x8_KHR:
    case HAL_PIXEL_FORMAT_COMPRESSED_SRGB8_ALPHA8_ASTC_8x8_KHR:
    case HAL_PIXEL_FORMAT_COMPRESSED_RGBA_ASTC_10x5_KHR:
    case HAL_PIXEL_FORMAT_COMPRESSED_SRGB8_ALPHA8_ASTC_10x5_KHR:
    case HAL_PIXEL_FORMAT_COMPRESSED_RGBA_ASTC_10x6_KHR:
    case HAL_PIXEL_FORMAT_COMPRESSED_SRGB8_ALPHA8_ASTC_10x6_KHR:
    case HAL_PIXEL_FORMAT_COMPRESSED_RGBA_ASTC_10x8_KHR:
    case HAL_PIXEL_FORMAT_COMPRESSED_SRGB8_ALPHA8_ASTC_10x8_KHR:
    case HAL_PIXEL_FORMAT_COMPRESSED_RGBA_ASTC_10x10_KHR:
    case HAL_PIXEL_FORMAT_COMPRESSED_SRGB8_ALPHA8_ASTC_10x10_KHR:
    case HAL_PIXEL_FORMAT_COMPRESSED_RGBA_ASTC_12x10_KHR:
    case HAL_PIXEL_FORMAT_COMPRESSED_SRGB8_ALPHA8_ASTC_12x10_KHR:
    case HAL_PIXEL_FORMAT_COMPRESSED_RGBA_ASTC_12x12_KHR:
    case HAL_PIXEL_FORMAT_COMPRESSED_SRGB8_ALPHA8_ASTC_12x12_KHR:
    // Below formats used by camera and VR
    case HAL_PIXEL_FORMAT_BLOB:
    case HAL_PIXEL_FORMAT_RAW_OPAQUE:
    default:
      ALOGE("Cannot convert hal format to drm format %u", hal_format);
      return DRM_FORMAT_INVALID;
  }
}

uint64_t QcomImporter::ConvertGrallocFormatToDrmModifiers(
    uint32_t hal_format, uint64_t flags, uint64_t usage) {
  uint64_t modifier = 0UL;

  switch (hal_format) {
    case HAL_PIXEL_FORMAT_YCbCr_420_SP_VENUS_UBWC:
    case HAL_PIXEL_FORMAT_YCbCr_420_TP10_UBWC:
    case HAL_PIXEL_FORMAT_YCbCr_420_P010_UBWC:
      modifier |= DRM_FORMAT_MOD_QCOM_COMPRESSED;
      break;
    default:
      break;
  }

  if ((flags & private_handle_t::PRIV_FLAGS_UBWC_ALIGNED) ||
      (flags & private_handle_t::PRIV_FLAGS_UBWC_ALIGNED_PI) ||
      (usage & GRALLOC_USAGE_PRIVATE_ALLOC_UBWC) ||
      (usage & GRALLOC_USAGE_PRIVATE_ALLOC_UBWC_PI)) {
    switch (hal_format) {
      case HAL_PIXEL_FORMAT_BGR_565:
      case HAL_PIXEL_FORMAT_RGBA_8888:
      case HAL_PIXEL_FORMAT_RGBX_8888:
      case HAL_PIXEL_FORMAT_NV12_ENCODEABLE:
      case HAL_PIXEL_FORMAT_YCbCr_420_SP_VENUS:
      case HAL_PIXEL_FORMAT_RGBA_1010102:
      case HAL_PIXEL_FORMAT_RGBX_1010102:
      case HAL_PIXEL_FORMAT_DEPTH_16:
      case HAL_PIXEL_FORMAT_DEPTH_24:
      case HAL_PIXEL_FORMAT_DEPTH_24_STENCIL_8:
      case HAL_PIXEL_FORMAT_DEPTH_32F:
      case HAL_PIXEL_FORMAT_STENCIL_8:
        modifier |= DRM_FORMAT_MOD_QCOM_COMPRESSED;
        break;
      default:
        break;
    }
  }

  return modifier;
}

bool QcomImporter::IsUncompressedRGBFormat(int format) {
  switch (format) {
    case HAL_PIXEL_FORMAT_RGBA_8888:
    case HAL_PIXEL_FORMAT_RGBX_8888:
    case HAL_PIXEL_FORMAT_RGB_888:
    case HAL_PIXEL_FORMAT_RGB_565:
    case HAL_PIXEL_FORMAT_BGR_565:
    case HAL_PIXEL_FORMAT_BGRA_8888:
    case HAL_PIXEL_FORMAT_RGBA_5551:
    case HAL_PIXEL_FORMAT_RGBA_4444:
    case HAL_PIXEL_FORMAT_R_8:
    case HAL_PIXEL_FORMAT_RG_88:
    case HAL_PIXEL_FORMAT_BGRX_8888:
    case HAL_PIXEL_FORMAT_RGBA_1010102:
    case HAL_PIXEL_FORMAT_ARGB_2101010:
    case HAL_PIXEL_FORMAT_RGBX_1010102:
    case HAL_PIXEL_FORMAT_XRGB_2101010:
    case HAL_PIXEL_FORMAT_BGRA_1010102:
    case HAL_PIXEL_FORMAT_ABGR_2101010:
    case HAL_PIXEL_FORMAT_BGRX_1010102:
    case HAL_PIXEL_FORMAT_XBGR_2101010:
    case HAL_PIXEL_FORMAT_RGBA_FP16:
    case HAL_PIXEL_FORMAT_BGR_888:
      return true;
    default:
      break;
  }

  return false;
}

uint32_t QcomImporter::GetBppForUncompressedRGB(int format) {
  uint32_t bpp = 0;
  switch (format) {
    case HAL_PIXEL_FORMAT_RGBA_FP16:
      bpp = 8;
      break;
    case HAL_PIXEL_FORMAT_RGBA_8888:
    case HAL_PIXEL_FORMAT_RGBX_8888:
    case HAL_PIXEL_FORMAT_BGRA_8888:
    case HAL_PIXEL_FORMAT_BGRX_8888:
    case HAL_PIXEL_FORMAT_RGBA_1010102:
    case HAL_PIXEL_FORMAT_ARGB_2101010:
    case HAL_PIXEL_FORMAT_RGBX_1010102:
    case HAL_PIXEL_FORMAT_XRGB_2101010:
    case HAL_PIXEL_FORMAT_BGRA_1010102:
    case HAL_PIXEL_FORMAT_ABGR_2101010:
    case HAL_PIXEL_FORMAT_BGRX_1010102:
    case HAL_PIXEL_FORMAT_XBGR_2101010:
      bpp = 4;
      break;
    case HAL_PIXEL_FORMAT_RGB_888:
    case HAL_PIXEL_FORMAT_BGR_888:
      bpp = 3;
      break;
    case HAL_PIXEL_FORMAT_RGB_565:
    case HAL_PIXEL_FORMAT_BGR_565:
    case HAL_PIXEL_FORMAT_RGBA_5551:
    case HAL_PIXEL_FORMAT_RGBA_4444:
      bpp = 2;
      break;
    default:
      ALOGE("Error : %s New format request = 0x%x", __FUNCTION__, format);
      break;
  }

  return bpp;
}

int QcomImporter::GetBpp(int format) {
  if (IsUncompressedRGBFormat(format)) {
    return GetBppForUncompressedRGB(format);
  }
  switch (format) {
    case HAL_PIXEL_FORMAT_COMPRESSED_RGBA_ASTC_4x4_KHR:
    case HAL_PIXEL_FORMAT_COMPRESSED_SRGB8_ALPHA8_ASTC_4x4_KHR:
    case HAL_PIXEL_FORMAT_RAW8:
    case HAL_PIXEL_FORMAT_Y8:
      return 1;
    case HAL_PIXEL_FORMAT_RAW16:
    case HAL_PIXEL_FORMAT_Y16:
    case HAL_PIXEL_FORMAT_YCbCr_422_SP:
    case HAL_PIXEL_FORMAT_YCrCb_422_SP:
    case HAL_PIXEL_FORMAT_YCbCr_422_I:
    case HAL_PIXEL_FORMAT_YCrCb_422_I:
    case HAL_PIXEL_FORMAT_CbYCrY_422_I:
      return 2;
    case HAL_PIXEL_FORMAT_YCbCr_420_P010_VENUS:
    case HAL_PIXEL_FORMAT_YCbCr_420_P010:
      return 3;
    case HAL_PIXEL_FORMAT_YV12:
	  return 1;
    default:
      return -1;
  }
}

QcomImporter::QcomImporter(DrmDevice *drm) : DrmGenericImporter(drm), drm_(drm) {
}

QcomImporter::~QcomImporter() {
}

int QcomImporter::Init() {
  int ret = hw_get_module(GRALLOC_HARDWARE_MODULE_ID,
                            (const hw_module_t **)&gralloc_);
  if (ret) {
    ALOGE("Failed to open gralloc module");
    return ret;
  }

  ALOGI("Using %s gralloc module: %s\n", gralloc_->common.name,
        gralloc_->common.author);

  return 0;
}

int QcomImporter::ImportBuffer(buffer_handle_t handle, hwc_drm_bo_t *bo) {
  if (private_handle_t::validate(handle) != 0) {
    return -EINVAL;
  }

  struct private_handle_t *gr_handle = (struct private_handle_t *)handle;
  uint32_t gem_handle;
  int ret;

  QcomBuffer_t *buf = GetQcomBuffer(gr_handle);
  if (buf) {
    ret = ConvertBoInfo(handle, bo);
    if (ret)
      return ret;
    bo->gem_handles[0] = buf->gem_handle;
    bo->fb_id = buf->fb_id;
    bo->priv = buf;
    buf->ref++;
    return 0;
  }

  buf = new QcomBuffer_t();
  if (!buf) {
    return -ENOMEM;
  }

  buf->ref = 1;
  buf->handle = gr_handle;
  buf->handle_id = gr_handle->id;

  ret = drmPrimeFDToHandle(drm_->fd(), gr_handle->fd, &gem_handle);
  if (ret) {
    ALOGE("failed to import prime fd %d ret=%d", gr_handle->fd, ret);
    delete buf;
    return ret;
  }

  buf->gem_handle = gem_handle;

  ret = ConvertBoInfo(handle, bo);
  if (ret) {
    delete buf;
    return ret;
  }
  bo->gem_handles[0] = gem_handle;
  bo->priv = buf;

  if (!bo->with_modifiers)
    ret = drmModeAddFB2(drm_->fd(), bo->width, bo->height, bo->format,
                        bo->gem_handles, bo->pitches, bo->offsets, &bo->fb_id,
                        0);
  else
    ret = drmModeAddFB2WithModifiers(drm_->fd(), bo->width, bo->height,
                                     bo->format, bo->gem_handles, bo->pitches,
                                     bo->offsets, bo->modifiers, &bo->fb_id,
                                     bo->modifiers[0] ? DRM_MODE_FB_MODIFIERS : 0);

  if (ret) {
    ALOGE("could not create drm fb %d", ret);
    delete buf;
    return ret;
  }

  buf->ref++;
  buf->fb_id = bo->fb_id;

  SetQcomBuffer(buf);

  //Dump();

  return ret;
}

int QcomImporter::ReleaseBuffer(hwc_drm_bo_t *bo) {
  QcomBuffer_t *buf = (QcomBuffer_t *)bo->priv;
  if (!buf) {
    ALOGE("Freeing bo %" PRIu32 ", buffer is NULL!", bo->fb_id);
    return 0;
  }

  if (--buf->ref) {
    if (buf->ref == 1) {
      /* These should be not more handle associated with */
      buf->handle = NULL;
    }
    return 0;
  }

  RemoveQcomBuffer(buf);

  return 0;
}

QcomImporter::QcomBuffer_t* QcomImporter::GetQcomBuffer(struct private_handle_t* handle) {
  for(QcomBuffer_t *buf: buffers_list_) {
    if (buf->handle_id == handle->id)
      return buf;
  }
  return NULL;
}

int QcomImporter::SetQcomBuffer(QcomBuffer_t *buf) {
  buffers_list_.push_back(buf);

  /* Mainitain no more than MAX_DELAYED_RELEASE_SLOTS slots */
  while (buffers_list_.size() > MAX_DELAYED_RELEASE_SLOTS) {
    for(QcomBuffer_t *buf: buffers_list_) {
      if (buf->ref <= 1) {
        RemoveQcomBuffer(buf);
        /* Remove one slot, break to see if need to free more slots */
        break;
      }
    }

    /* No more slot can be freed */
    break;
  }

  return 0;
}

int QcomImporter::RemoveQcomBuffer(QcomBuffer_t *buf) {
  int ret = 0;

  if (buf->fb_id)
    if (drmModeRmFB(drm_->fd(), buf->fb_id))
      ALOGE("Failed to rm fb %d", buf->fb_id);

  if (buf->gem_handle) {
    struct drm_gem_close gem_close;
     memset(&gem_close, 0, sizeof(gem_close));

     gem_close.handle = buf->gem_handle;
     ret = drmIoctl(drm_->fd(), DRM_IOCTL_GEM_CLOSE, &gem_close);
     if (ret)
       ALOGE("Failed to close gem handle %d %d", buf->gem_handle, ret);
  }

  buffers_list_.remove(buf);
  delete(buf);

  return ret;
}

void QcomImporter::Dump(void ) {
  struct private_handle_t *gr_handle;
  ALOGD("***** Importer buffer dump *****\n");
  for(QcomBuffer_t *buf: buffers_list_) {
    gr_handle = (struct private_handle_t *)buf->handle;
    if (gr_handle) {
      ALOGD("\t%p  %dx%d %dx%d  fmt %d  fd %d:%d  sz %d  usage %" PRIx64 "  id %" PRIu64 "  hdl %d  fb_id %d  ref %d",
            buf->handle, gr_handle->width, gr_handle->height,
            gr_handle->unaligned_width, gr_handle->unaligned_height,
            gr_handle->format, gr_handle->fd, gr_handle->fd_metadata,
            gr_handle->size, gr_handle->usage, gr_handle->id,
            buf->gem_handle, buf->fb_id, buf->ref);
    } else {
      ALOGD("\t%p  id %" PRIu64 "  hdl %d  fb_id %d  ref %d",
            buf->handle, buf->handle_id, buf->gem_handle, buf->fb_id, buf->ref);
    }
  }
  ALOGD("***** Importer buffer dump end *****\n");
}

int QcomImporter::ConvertBoInfo(buffer_handle_t handle, hwc_drm_bo_t *bo) {
  private_handle_t const *gr_handle = reinterpret_cast<private_handle_t const *>(
      handle);

  if (!gr_handle)
    return -EINVAL;

  //private_handle_t::Dump(__FUNCTION__, gr_handle);

  memset(bo, 0, sizeof(hwc_drm_bo_t));

  bo->width = gr_handle->GetUnalignedWidth();
  bo->height = gr_handle->GetUnalignedHeight();
  bo->format = ConvertHalFormatToDrm(gr_handle->GetColorFormat());
  if (bo->format == DRM_FORMAT_INVALID)
    return -EINVAL;
  bo->hal_format = gr_handle->GetColorFormat();
  bo->usage = gr_handle->GetUsage();
  bo->pixel_stride = gr_handle->GetStride();
  bo->pitches[0] = gr_handle->width * GetBpp(gr_handle->format);
  bo->offsets[0] = gr_handle->offset;
  bo->prime_fds[0] = gr_handle->fd;
  bo->modifiers[0] = ConvertGrallocFormatToDrmModifiers(gr_handle->GetColorFormat(),
      gr_handle->flags, gr_handle->GetUsage());
  bo->with_modifiers = bo->modifiers[0] ? true : false;

  return 0;
}

std::unique_ptr<Planner> Planner::CreateInstance(DrmDevice *) {
  std::unique_ptr<Planner> planner(new Planner);
  planner->AddStage<PlanStageGreedy>();
  return planner;
}
}  // namespace android
