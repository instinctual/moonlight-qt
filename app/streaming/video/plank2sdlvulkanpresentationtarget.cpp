/* SPDX-License-Identifier: GPL-3.0-only */

#include "streaming/video/plank2sdlvulkanpresentationtarget.h"

#include "streaming/video/plank2presentationframe.h"
#include "plank/display/presentation_v1.h"
#include "plank/media/interfaces_v1.h"

#include <array>
#include <cstdint>
#include <limits>
#include <memory>
#include <thread>
#include <utility>
#include <vector>

namespace {
  constexpr std::uint32_t reason_no_drawable_target = 1U;
  constexpr std::uint32_t reason_renderer_failed = 2U;

  constexpr std::array<std::pair<std::uint16_t, std::uint16_t>, 9U>
      exact_tuples {{
    {PLANK_MEDIA_PROFILE_H264_HIGH10_444_SOFTWARE_NVFBC_V1,
     PLANK_MEDIA_PIXEL_GBRP10_LE_V1},
    {PLANK_MEDIA_PROFILE_H264_HIGH8_422_SOFTWARE_NVFBC_V1,
     PLANK_MEDIA_PIXEL_YUV422P8_V1},
    {PLANK_MEDIA_PROFILE_H264_HIGH8_444_SOFTWARE_NVFBC_V1,
     PLANK_MEDIA_PIXEL_GBRP8_V1},
    {PLANK_MEDIA_PROFILE_H264_HIGH10_422_SOFTWARE_NVFBC_V1,
     PLANK_MEDIA_PIXEL_YUV422P10_LE_V1},
    {PLANK_MEDIA_PROFILE_H264_HIGH8_444_NVENC_NVFBC_V1,
     PLANK_MEDIA_PIXEL_GBRP8_V1},
    {PLANK_MEDIA_PROFILE_HEVC_REXT8_444_NVENC_NVFBC_V1,
     PLANK_MEDIA_PIXEL_GBRP8_V1},
    {PLANK_MEDIA_PROFILE_HEVC_REXT10_444_NVENC_NVFBC_V1,
     PLANK_MEDIA_PIXEL_GBRP10_LE_V1},
    {PLANK_MEDIA_PROFILE_H264_HIGH10_444_SOFTWARE_X11_V1,
     PLANK_MEDIA_PIXEL_GBRP10_LE_V1},
    {PLANK_MEDIA_PROFILE_HEVC_REXT10_444_NVENC_X11_V1,
     PLANK_MEDIA_PIXEL_GBRP10_LE_V1},
  }};

  bool exact_tuple(std::uint16_t profile_id, std::uint16_t pixel_layout) {
    for (const auto& tuple : exact_tuples) {
      if (tuple.first == profile_id && tuple.second == pixel_layout) {
        return true;
      }
    }
    return false;
  }

  bool rect_equal(const PlankRectI32V1& left,
                  const PlankRectI32V1& right) {
    return left.x == right.x && left.y == right.y &&
           left.width == right.width && left.height == right.height;
  }

  bool transform_equal(const PlankPresentationTransformV1& left,
                       const PlankPresentationTransformV1& right) {
    return left.struct_size == right.struct_size &&
           left.contract_version == right.contract_version &&
           left.mode == right.mode &&
           rect_equal(left.source_rect, right.source_rect) &&
           rect_equal(left.client_rect, right.client_rect) &&
           rect_equal(left.content_rect, right.content_rect);
  }

  std::uint8_t bytes_per_sample(std::uint16_t pixel_layout) {
    return pixel_layout == PLANK_MEDIA_PIXEL_GBRP10_LE_V1 ||
           pixel_layout == PLANK_MEDIA_PIXEL_YUV422P10_LE_V1 ? 2U : 1U;
  }

  bool chroma_422(std::uint16_t pixel_layout) {
    return pixel_layout == PLANK_MEDIA_PIXEL_YUV422P8_V1 ||
           pixel_layout == PLANK_MEDIA_PIXEL_YUV422P10_LE_V1;
  }

  struct qualification_frame_t {
    std::vector<std::uint8_t> bytes;
    PlankRetainedPresentationFrame retained;
  };

  qualification_frame_t qualification_frame(std::uint16_t profile_id,
                                             std::uint16_t pixel_layout) {
    constexpr std::uint32_t width = 64U;
    constexpr std::uint32_t height = 32U;
    qualification_frame_t result;
    result.retained.profileId = profile_id;
    result.retained.pixelLayout = pixel_layout;
    result.retained.memoryKind = PLANK_MEDIA_MEMORY_CPU_V1;
    result.retained.planeCount = 3U;
    result.retained.width = width;
    result.retained.height = height;
    result.retained.frameSequence = 1U;
    result.retained.frameTimestampNs = 1U;
    result.retained.frameLeaseId = 1U;
    result.retained.topologyGeneration = "plank2-vulkan-qualification";

    const auto sample_bytes = bytes_per_sample(pixel_layout);
    std::array<std::uint64_t, 3U> sizes {};
    std::array<std::uint32_t, 3U> strides {};
    std::uint64_t total = 0U;
    for (std::uint16_t index = 0U; index != 3U; ++index) {
      const auto samples = chroma_422(pixel_layout) && index != 0U
        ? width / 2U : width;
      strides[index] = samples * sample_bytes;
      sizes[index] = static_cast<std::uint64_t>(strides[index]) * height;
      if (sizes[index] > std::numeric_limits<std::size_t>::max() - total) {
        return {};
      }
      total += sizes[index];
    }
    result.bytes.resize(static_cast<std::size_t>(total), 0x80U);
    std::size_t offset = 0U;
    for (std::uint16_t index = 0U; index != 3U; ++index) {
      result.retained.planes[index] = {
        reinterpret_cast<std::uintptr_t>(result.bytes.data() + offset),
        -1, 0U, strides[index], sizes[index], 0U,
      };
      offset += static_cast<std::size_t>(sizes[index]);
    }
    return result;
  }

  class sdl_vulkan_presentation_target_t final:
      public IPlankRetainedPresentationTarget {
  public:
    sdl_vulkan_presentation_target_t(
        std::weak_ptr<IPlank2SdlVulkanFrameSink> sink,
        Plank2SdlVulkanPresentationConfiguration configuration)
        : sink_(std::move(sink)),
          configuration_(std::move(configuration)),
          renderer_thread_(std::this_thread::get_id()) {
    }

    ~sdl_vulkan_presentation_target_t() override {
      close();
    }

    bool available() const override {
      const auto sink = sink_.lock();
      return sink && sink->plank2PresentationAvailable();
    }

    bool qualifies(std::uint16_t profile_id, std::uint16_t pixel_layout,
                   std::uint16_t memory_kind) const override {
      if (!on_renderer_thread() ||
          memory_kind != PLANK_MEDIA_MEMORY_CPU_V1 ||
          !exact_tuple(profile_id, pixel_layout)) {
        return false;
      }
      const auto sink = sink_.lock();
      if (!sink || !sink->plank2PresentationAvailable()) return false;
      auto qualification = qualification_frame(profile_id, pixel_layout);
      auto frame = createPlank2PresentationAvFrame(qualification.retained);
      return frame && sink->plank2TestPresentationFrame(frame.get());
    }

    PlankBackendOperationResultV1 open(
        const PlankRetainedPresentationOpenRequest& request) override {
      if (!on_renderer_thread() || opened_ ||
          request.memoryKind != PLANK_MEDIA_MEMORY_CPU_V1 ||
          !exact_tuple(request.profileId, request.pixelLayout) ||
          request.refreshMilliHz != configuration_.refreshMilliHz ||
          request.topologyGeneration != configuration_.topologyGeneration ||
          !transform_equal(request.transform, configuration_.transform)) {
        return PLANK_BACKEND_OPERATION_INVALID_ARGUMENT_V1;
      }
      const auto sink = sink_.lock();
      if (!sink || !sink->plank2PresentationAvailable()) {
        return PLANK_BACKEND_OPERATION_UNAVAILABLE_V1;
      }
      opened_ = true;
      profile_id_ = request.profileId;
      pixel_layout_ = request.pixelLayout;
      return PLANK_BACKEND_OPERATION_OK_V1;
    }

    PlankBackendOperationResultV1 present(
        const PlankRetainedPresentationFrame& retained,
        PlankRetainedPresentationCompletion& completion) override {
      completion = {};
      if (!on_renderer_thread() || !opened_ ||
          retained.profileId != profile_id_ ||
          retained.pixelLayout != pixel_layout_ ||
          retained.memoryKind != PLANK_MEDIA_MEMORY_CPU_V1 ||
          retained.topologyGeneration != configuration_.topologyGeneration ||
          retained.width != configuration_.transform.source_rect.width ||
          retained.height != configuration_.transform.source_rect.height) {
        return PLANK_BACKEND_OPERATION_INVALID_ARGUMENT_V1;
      }
      const auto sink = sink_.lock();
      if (!sink || !sink->plank2PresentationAvailable()) {
        return PLANK_BACKEND_OPERATION_UNAVAILABLE_V1;
      }
      auto frame = createPlank2PresentationAvFrame(retained);
      if (!frame) return PLANK_BACKEND_OPERATION_INVALID_ARGUMENT_V1;

      std::uint64_t actual_timestamp_ns = 0U;
      switch (sink->plank2PresentFrame(
                frame.get(), retained.targetPresentTimestampNs,
                actual_timestamp_ns)) {
        case Plank2SdlVulkanPresentResult::Presented:
          if (actual_timestamp_ns == 0U) {
            completion = {PLANK_PRESENTATION_COMPLETION_FAILED_V1,
                          reason_renderer_failed, 0U};
          } else {
            completion = {PLANK_PRESENTATION_COMPLETION_PRESENTED_V1,
                          0U, actual_timestamp_ns};
          }
          break;
        case Plank2SdlVulkanPresentResult::NoDrawableTarget:
          completion = {PLANK_PRESENTATION_COMPLETION_DROPPED_V1,
                        reason_no_drawable_target, 0U};
          break;
        case Plank2SdlVulkanPresentResult::Failed:
        default:
          completion = {PLANK_PRESENTATION_COMPLETION_FAILED_V1,
                        reason_renderer_failed, 0U};
          break;
      }
      return PLANK_BACKEND_OPERATION_OK_V1;
    }

    PlankBackendOperationResultV1 reset() override {
      if (!on_renderer_thread() || !opened_) {
        return PLANK_BACKEND_OPERATION_INVALID_ARGUMENT_V1;
      }
      const auto sink = sink_.lock();
      if (!sink || !sink->plank2PresentationAvailable()) {
        return PLANK_BACKEND_OPERATION_UNAVAILABLE_V1;
      }
      if (!sink->plank2ResetPresentation()) {
        return PLANK_BACKEND_OPERATION_FAILED_V1;
      }
      return PLANK_BACKEND_OPERATION_OK_V1;
    }

    void close() override {
      if (!opened_) return;
      // present() is synchronous, so the target never retains caller storage
      // after it returns. Only touch thread-affine renderer state here when
      // close also occurs on the bound renderer thread.
      if (on_renderer_thread()) {
        if (const auto sink = sink_.lock()) {
          sink->plank2ResetPresentation();
        }
      }
      opened_ = false;
      profile_id_ = 0U;
      pixel_layout_ = 0U;
    }

  private:
    bool on_renderer_thread() const {
      return std::this_thread::get_id() == renderer_thread_;
    }

    std::weak_ptr<IPlank2SdlVulkanFrameSink> sink_;
    Plank2SdlVulkanPresentationConfiguration configuration_;
    std::thread::id renderer_thread_;
    bool opened_ {};
    std::uint16_t profile_id_ {};
    std::uint16_t pixel_layout_ {};
  };
}

std::shared_ptr<IPlankRetainedPresentationTarget>
createPlank2SdlVulkanPresentationTarget(
        std::weak_ptr<IPlank2SdlVulkanFrameSink> sink,
        Plank2SdlVulkanPresentationConfiguration configuration)
{
  if (configuration.refreshMilliHz == 0U ||
      configuration.topologyGeneration.empty() ||
      plank_presentation_transform_validate_v1(&configuration.transform) !=
        PLANK_PRESENTATION_OK_V1) {
    return {};
  }
  return std::make_shared<sdl_vulkan_presentation_target_t>(
    std::move(sink), std::move(configuration));
}
