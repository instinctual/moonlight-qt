/* SPDX-License-Identifier: GPL-3.0-only */

#include "streaming/video/plank2sdlvulkanpresentationtarget.h"

#include "plank/display/presentation_v1.h"
#include "plank/media/profile_v1.h"

extern "C" {
#include <libavutil/pixfmt.h>
}

#include <array>
#include <cstdint>
#include <cstdlib>
#include <iostream>
#include <memory>
#include <string_view>
#include <thread>
#include <vector>

namespace {
  [[noreturn]] void fail(std::string_view detail) {
    std::cerr << "PLANK2 SDL/Vulkan presentation target test failed: "
              << detail << '\n';
    std::exit(EXIT_FAILURE);
  }

  void require(bool condition, std::string_view detail) {
    if (!condition) fail(detail);
  }

  struct tuple_t {
    std::uint16_t profile;
    std::uint16_t layout;
    AVPixelFormat format;
  };

  constexpr std::array<tuple_t, 9U> tuples {{
    {PLANK_MEDIA_PROFILE_H264_HIGH10_444_SOFTWARE_NVFBC_V1,
     PLANK_MEDIA_PIXEL_GBRP10_LE_V1, AV_PIX_FMT_GBRP10LE},
    {PLANK_MEDIA_PROFILE_H264_HIGH8_422_SOFTWARE_NVFBC_V1,
     PLANK_MEDIA_PIXEL_YUV422P8_V1, AV_PIX_FMT_YUV422P},
    {PLANK_MEDIA_PROFILE_H264_HIGH8_444_SOFTWARE_NVFBC_V1,
     PLANK_MEDIA_PIXEL_GBRP8_V1, AV_PIX_FMT_GBRP},
    {PLANK_MEDIA_PROFILE_H264_HIGH10_422_SOFTWARE_NVFBC_V1,
     PLANK_MEDIA_PIXEL_YUV422P10_LE_V1, AV_PIX_FMT_YUV422P10LE},
    {PLANK_MEDIA_PROFILE_H264_HIGH8_444_NVENC_NVFBC_V1,
     PLANK_MEDIA_PIXEL_GBRP8_V1, AV_PIX_FMT_GBRP},
    {PLANK_MEDIA_PROFILE_HEVC_REXT8_444_NVENC_NVFBC_V1,
     PLANK_MEDIA_PIXEL_GBRP8_V1, AV_PIX_FMT_GBRP},
    {PLANK_MEDIA_PROFILE_HEVC_REXT10_444_NVENC_NVFBC_V1,
     PLANK_MEDIA_PIXEL_GBRP10_LE_V1, AV_PIX_FMT_GBRP10LE},
    {PLANK_MEDIA_PROFILE_H264_HIGH10_444_SOFTWARE_X11_V1,
     PLANK_MEDIA_PIXEL_GBRP10_LE_V1, AV_PIX_FMT_GBRP10LE},
    {PLANK_MEDIA_PROFILE_HEVC_REXT10_444_NVENC_X11_V1,
     PLANK_MEDIA_PIXEL_GBRP10_LE_V1, AV_PIX_FMT_GBRP10LE},
  }};

  class fake_sink_t final: public IPlank2SdlVulkanFrameSink {
  public:
    bool plank2PresentationAvailable() const override {
      return is_available;
    }

    bool plank2TestPresentationFrame(AVFrame* frame) override {
      ++test_calls;
      last_test_format = frame != nullptr
        ? static_cast<AVPixelFormat>(frame->format) : AV_PIX_FMT_NONE;
      return accept_test && frame != nullptr && frame->width == 64 &&
             frame->height == 32 && frame->buf[0] == nullptr;
    }

    Plank2SdlVulkanPresentResult plank2PresentFrame(
        AVFrame* frame, std::uint64_t target_timestamp_ns,
        std::uint64_t& actual_timestamp_ns) override {
      ++present_calls;
      last_target_timestamp_ns = target_timestamp_ns;
      last_present_data = frame != nullptr ? frame->data[0] : nullptr;
      actual_timestamp_ns = returned_timestamp_ns;
      return returned_result;
    }

    bool plank2ResetPresentation() override {
      ++reset_calls;
      return accept_reset;
    }

    bool is_available {true};
    bool accept_test {true};
    bool accept_reset {true};
    Plank2SdlVulkanPresentResult returned_result {
      Plank2SdlVulkanPresentResult::Presented};
    std::uint64_t returned_timestamp_ns {UINT64_C(3000000000)};
    std::uint32_t test_calls {};
    std::uint32_t present_calls {};
    std::uint32_t reset_calls {};
    AVPixelFormat last_test_format {AV_PIX_FMT_NONE};
    std::uint64_t last_target_timestamp_ns {};
    std::uint8_t* last_present_data {};
  };

  Plank2SdlVulkanPresentationConfiguration configuration() {
    Plank2SdlVulkanPresentationConfiguration result;
    result.refreshMilliHz = 60000U;
    result.topologyGeneration = "sdl-vulkan-target-test";
    require(plank_presentation_transform_create_v1(
              {0, 0, 64U, 32U}, 128U, 72U,
              PLANK_PRESENTATION_MODE_SCALED_SPAN_V1, &result.transform) ==
                PLANK_PRESENTATION_OK_V1,
            "test transform could not be created");
    return result;
  }

  PlankRetainedPresentationOpenRequest open_request(
      const Plank2SdlVulkanPresentationConfiguration& configured) {
    return {
      tuples[0].profile, tuples[0].layout, PLANK_MEDIA_MEMORY_CPU_V1,
      configured.refreshMilliHz, configured.transform,
      configured.topologyGeneration,
    };
  }

  struct frame_fixture_t {
    explicit frame_fixture_t(
        const Plank2SdlVulkanPresentationConfiguration& configured)
        : bytes(3U * 64U * 32U * 2U, 0x5aU) {
      frame.profileId = tuples[0].profile;
      frame.pixelLayout = tuples[0].layout;
      frame.memoryKind = PLANK_MEDIA_MEMORY_CPU_V1;
      frame.planeCount = 3U;
      frame.width = 64U;
      frame.height = 32U;
      frame.frameSequence = 7U;
      frame.frameTimestampNs = UINT64_C(1000000000);
      frame.frameLeaseId = 11U;
      frame.targetPresentTimestampNs = UINT64_C(2000000000);
      frame.topologyGeneration = configured.topologyGeneration;
      constexpr std::uint32_t stride = 64U * 2U;
      constexpr std::uint64_t size = stride * 32U;
      for (std::uint16_t index = 0U; index != 3U; ++index) {
        frame.planes[index] = {
          reinterpret_cast<std::uintptr_t>(
            bytes.data() + static_cast<std::size_t>(index) * size),
          -1, 0U, stride, size, 0U,
        };
      }
    }

    std::vector<std::uint8_t> bytes;
    PlankRetainedPresentationFrame frame;
  };
}

int main()
{
    auto sink = std::make_shared<fake_sink_t>();
    const auto configured = configuration();
    auto target = createPlank2SdlVulkanPresentationTarget(sink, configured);
    require(target && target->available(), "valid target is unavailable");

    for (const auto& tuple : tuples) {
      require(target->qualifies(tuple.profile, tuple.layout,
                                PLANK_MEDIA_MEMORY_CPU_V1) &&
                  sink->last_test_format == tuple.format,
              "an exact tuple did not pass its real-frame mapping test");
    }
    require(sink->test_calls == tuples.size(),
            "qualification did not test every exact tuple");
    require(!target->qualifies(tuples[0].profile, tuples[1].layout,
                               PLANK_MEDIA_MEMORY_CPU_V1) &&
                !target->qualifies(tuples[0].profile, tuples[0].layout,
                                   PLANK_MEDIA_MEMORY_VULKAN_V1),
            "an inexact tuple qualified");

    auto request = open_request(configured);
    require(target->open(request) == PLANK_BACKEND_OPERATION_OK_V1,
            "exact target did not open");
    require(sink->test_calls == tuples.size() + 1U,
            "open did not repeat exact real-frame qualification");
    require(target->open(request) == PLANK_BACKEND_OPERATION_INVALID_ARGUMENT_V1,
            "already-open target reopened");

    frame_fixture_t fixture(configured);
    PlankRetainedPresentationCompletion completion;
    require(target->present(fixture.frame, completion) ==
              PLANK_BACKEND_OPERATION_OK_V1 &&
                completion.state == PLANK_PRESENTATION_COMPLETION_PRESENTED_V1 &&
                completion.reasonCode == 0U &&
                completion.actualPresentTimestampNs ==
                  sink->returned_timestamp_ns &&
                sink->last_target_timestamp_ns ==
                  fixture.frame.targetPresentTimestampNs &&
                sink->last_present_data == fixture.bytes.data(),
            "synchronous presented completion changed identity or storage");

    sink->returned_result = Plank2SdlVulkanPresentResult::NoDrawableTarget;
    require(target->present(fixture.frame, completion) ==
              PLANK_BACKEND_OPERATION_OK_V1 &&
                completion.state == PLANK_PRESENTATION_COMPLETION_DROPPED_V1 &&
                completion.reasonCode != 0U &&
                completion.actualPresentTimestampNs == 0U,
            "no-drawable result was not a terminal drop");
    sink->returned_result = Plank2SdlVulkanPresentResult::Failed;
    require(target->present(fixture.frame, completion) ==
              PLANK_BACKEND_OPERATION_OK_V1 &&
                completion.state == PLANK_PRESENTATION_COMPLETION_FAILED_V1 &&
                completion.reasonCode != 0U &&
                completion.actualPresentTimestampNs == 0U,
            "renderer failure was not a terminal failure");
    sink->returned_result = Plank2SdlVulkanPresentResult::Presented;
    sink->returned_timestamp_ns = 0U;
    require(target->present(fixture.frame, completion) ==
              PLANK_BACKEND_OPERATION_OK_V1 &&
                completion.state == PLANK_PRESENTATION_COMPLETION_FAILED_V1,
            "presented result without a completion timestamp was accepted");

    require(target->reset() == PLANK_BACKEND_OPERATION_OK_V1 &&
                sink->reset_calls == 1U,
            "synchronous target reset failed");

    bool wrong_thread_qualified = true;
    PlankBackendOperationResultV1 wrong_thread_reset =
      PLANK_BACKEND_OPERATION_OK_V1;
    std::thread wrong_thread([&] {
      wrong_thread_qualified = target->qualifies(
        tuples[0].profile, tuples[0].layout, PLANK_MEDIA_MEMORY_CPU_V1);
      wrong_thread_reset = target->reset();
    });
    wrong_thread.join();
    require(!wrong_thread_qualified &&
                wrong_thread_reset == PLANK_BACKEND_OPERATION_INVALID_ARGUMENT_V1,
            "renderer-thread affinity was not enforced");

    target->close();
    require(sink->reset_calls == 2U,
            "close did not leave the renderer synchronously idle");
    require(target->present(fixture.frame, completion) ==
              PLANK_BACKEND_OPERATION_INVALID_ARGUMENT_V1,
            "closed target accepted a frame");

    auto expired = createPlank2SdlVulkanPresentationTarget(sink, configured);
    sink.reset();
    require(expired && !expired->available() &&
                !expired->qualifies(tuples[0].profile, tuples[0].layout,
                                    PLANK_MEDIA_MEMORY_CPU_V1) &&
                expired->open(request) ==
                  PLANK_BACKEND_OPERATION_UNAVAILABLE_V1,
            "expired renderer remained available");

    auto invalid = configured;
    invalid.refreshMilliHz = 0U;
    require(!createPlank2SdlVulkanPresentationTarget({}, invalid),
            "invalid immutable configuration created a target");

    std::cout << "plank2_sdl_vulkan_presentation_target_test=pass\n";
    return EXIT_SUCCESS;
}
