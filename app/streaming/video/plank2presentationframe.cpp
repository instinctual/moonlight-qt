/* SPDX-License-Identifier: GPL-3.0-only */

#include "streaming/video/plank2presentationframe.h"

#include "plank/media/interfaces_v1.h"

extern "C" {
#include <libavutil/pixfmt.h>
#include <libavutil/hwcontext_drm.h>
}

#include <array>
#include <climits>
#include <cstdint>
#include <limits>

namespace {
  Plank2AvFramePtr dma_buf_view(const PlankRetainedPresentationFrame& retained) {
    PlankMediaFrameLeaseV1 lease {
      sizeof(PlankMediaFrameLeaseV1), PLANK_MEDIA_INTERFACE_VERSION,
      PLANK_MEDIA_FRAME_STAGE_DECODED_V1, retained.profileId,
      retained.pixelLayout, retained.memoryKind, retained.planeCount,
      retained.width, retained.height, retained.frameSequence,
      retained.frameTimestampNs, retained.topologyGeneration.c_str(),
      retained.frameLeaseId, {}, 0U,
    };
    if (retained.pixelLayout != PLANK_MEDIA_PIXEL_Y410_LE_V1 ||
        retained.planeCount > PLANK_MEDIA_MAX_PLANES_V1 ||
        retained.frameSequence == 0U || retained.frameTimestampNs == 0U ||
        retained.frameTimestampNs > static_cast<std::uint64_t>(INT64_MAX)) {
      return {};
    }
    for (std::uint16_t i = 0; i < retained.planeCount; ++i) {
      lease.planes[i] = retained.planes[i];
    }
    if (plank_media_frame_lease_validate_v1(&lease) != PLANK_MEDIA_INTERFACE_OK_V1) {
      return {};
    }
    Plank2AvFramePtr frame(av_frame_alloc());
    if (!frame) return {};
    frame->buf[0] = av_buffer_allocz(sizeof(AVDRMFrameDescriptor));
    if (!frame->buf[0]) return {};
    frame->data[0] = frame->buf[0]->data;
    auto* descriptor = reinterpret_cast<AVDRMFrameDescriptor*>(frame->data[0]);
    descriptor->nb_layers = 1;
    auto& layer = descriptor->layers[0];
    // Preserve storage identity. Y410 -> XR30 interpretation belongs to the
    // qualified EGL backend, not this ownership/descriptor adapter.
    layer.format = static_cast<std::uint32_t>('Y') |
                   (static_cast<std::uint32_t>('4') << 8) |
                   (static_cast<std::uint32_t>('1') << 16) |
                   (static_cast<std::uint32_t>('0') << 24);
    layer.nb_planes = retained.planeCount;
    for (std::uint16_t i = 0; i < retained.planeCount; ++i) {
      const auto& plane = retained.planes[i];
      if (plane.size_bytes > std::numeric_limits<std::size_t>::max() ||
          plane.stride_bytes > INT_MAX || plane.offset_bytes > INT_MAX) return {};
      int object_index = 0;
      while (object_index < descriptor->nb_objects &&
             descriptor->objects[object_index].fd != plane.dma_buf_fd) ++object_index;
      if (object_index == descriptor->nb_objects) {
        auto& object = descriptor->objects[descriptor->nb_objects++];
        object.fd = plane.dma_buf_fd;
        object.size = plane.size_bytes;
        object.format_modifier = plane.modifier;
      } else if (descriptor->objects[object_index].size != plane.size_bytes ||
                 descriptor->objects[object_index].format_modifier != plane.modifier) {
        return {};
      }
      layer.planes[i].object_index = object_index;
      layer.planes[i].offset = plane.offset_bytes;
      layer.planes[i].pitch = plane.stride_bytes;
    }
    frame->format = AV_PIX_FMT_DRM_PRIME;
    frame->width = retained.width;
    frame->height = retained.height;
    frame->pts = retained.frameTimestampNs;
    frame->time_base = {1, 1000000000};
    frame->sample_aspect_ratio = {1, 1};
    frame->color_range = AVCOL_RANGE_JPEG;
    frame->color_primaries = AVCOL_PRI_BT709;
    frame->color_trc = AVCOL_TRC_IEC61966_2_1;
    frame->colorspace = AVCOL_SPC_RGB;
    frame->chroma_location = AVCHROMA_LOC_UNSPECIFIED;
    return frame;
  }

  struct layout_spec_t {
    AVPixelFormat format;
    std::uint8_t bytes_per_sample;
    bool identity_gbr;
    bool chroma_422;
  };

  bool layout_spec(std::uint16_t layout, layout_spec_t& spec) {
    switch (layout) {
      case PLANK_MEDIA_PIXEL_GBRP8_V1:
        spec = {AV_PIX_FMT_GBRP, 1U, true, false};
        return true;
      case PLANK_MEDIA_PIXEL_GBRP10_LE_V1:
        spec = {AV_PIX_FMT_GBRP10LE, 2U, true, false};
        return true;
      case PLANK_MEDIA_PIXEL_YUV422P8_V1:
        spec = {AV_PIX_FMT_YUV422P, 1U, false, true};
        return true;
      case PLANK_MEDIA_PIXEL_YUV422P10_LE_V1:
        spec = {AV_PIX_FMT_YUV422P10LE, 2U, false, true};
        return true;
      default:
        return false;
    }
  }

  bool plane_is_addressable(const PlankMediaPlaneV1& plane,
                            std::uint32_t row_samples,
                            std::uint32_t height,
                            std::uint8_t bytes_per_sample) {
    if (plane.native_handle == 0U || plane.dma_buf_fd != -1 ||
        plane.stride_bytes == 0U || plane.stride_bytes > INT_MAX ||
        plane.size_bytes == 0U || row_samples == 0U || height == 0U ||
        plane.native_handle >
          std::numeric_limits<std::uintptr_t>::max() - plane.offset_bytes) {
      return false;
    }
    const std::uint64_t row_bytes =
      static_cast<std::uint64_t>(row_samples) * bytes_per_sample;
    if (row_bytes == 0U || row_bytes > plane.stride_bytes) return false;
    const std::uint64_t last_row =
      static_cast<std::uint64_t>(height - 1U) * plane.stride_bytes;
    if (last_row > std::numeric_limits<std::uint64_t>::max() - row_bytes) {
      return false;
    }
    const std::uint64_t required_without_offset = last_row + row_bytes;
    return plane.offset_bytes <= plane.size_bytes &&
           required_without_offset <= plane.size_bytes - plane.offset_bytes;
  }
}

void Plank2AvFrameDeleter::operator()(AVFrame* frame) const noexcept
{
    av_frame_free(&frame);
}

Plank2AvFramePtr createPlank2PresentationAvFrame(
        const PlankRetainedPresentationFrame& retained) noexcept
{
    if (retained.memoryKind == PLANK_MEDIA_MEMORY_DMA_BUF_V1) {
        return dma_buf_view(retained);
    }
    layout_spec_t spec {};
    if (retained.memoryKind != PLANK_MEDIA_MEMORY_CPU_V1 ||
            retained.planeCount != 3U || retained.width == 0U ||
            retained.height == 0U || retained.width > INT_MAX ||
            retained.height > INT_MAX || retained.frameSequence == 0U ||
            retained.frameTimestampNs == 0U || retained.frameLeaseId == 0U ||
            retained.frameTimestampNs > static_cast<std::uint64_t>(INT64_MAX) ||
            retained.topologyGeneration.empty() ||
            !layout_spec(retained.pixelLayout, spec) ||
            !plank_media_pixel_layout_matches_profile_stage_v1(
                PLANK_MEDIA_FRAME_STAGE_DECODED_V1,
                retained.pixelLayout, retained.profileId)) {
        return {};
    }

    for (std::uint16_t index = 0U; index != retained.planeCount; ++index) {
        const std::uint32_t row_samples =
                spec.chroma_422 && index != 0U ?
                    (retained.width + 1U) / 2U : retained.width;
        if (!plane_is_addressable(retained.planes[index], row_samples,
                                  retained.height, spec.bytes_per_sample)) {
            return {};
        }
    }

    Plank2AvFramePtr frame(av_frame_alloc());
    if (!frame) return {};
    frame->format = spec.format;
    frame->width = static_cast<int>(retained.width);
    frame->height = static_cast<int>(retained.height);
    frame->pts = static_cast<std::int64_t>(retained.frameTimestampNs);
    frame->time_base = {1, 1000000000};
    frame->sample_aspect_ratio = {1, 1};
    frame->color_range = AVCOL_RANGE_JPEG;
    frame->color_primaries = AVCOL_PRI_BT709;
    frame->color_trc = spec.identity_gbr ? AVCOL_TRC_IEC61966_2_1
                                         : AVCOL_TRC_BT709;
    frame->colorspace = spec.identity_gbr ? AVCOL_SPC_RGB : AVCOL_SPC_BT709;
    frame->chroma_location = spec.chroma_422 ? AVCHROMA_LOC_LEFT
                                             : AVCHROMA_LOC_UNSPECIFIED;

    for (std::uint16_t index = 0U; index != retained.planeCount; ++index) {
        frame->data[index] = reinterpret_cast<std::uint8_t*>(
                retained.planes[index].native_handle +
                retained.planes[index].offset_bytes);
        frame->linesize[index] =
                static_cast<int>(retained.planes[index].stride_bytes);
    }
    return frame;
}
