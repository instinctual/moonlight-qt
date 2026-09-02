/* SPDX-License-Identifier: GPL-3.0-only */

#include "streaming/video/plank2presentationframe.h"

#include "plank/media/profile_v1.h"

#include <array>
#include <cstdint>
#include <cstdlib>
#include <iostream>
#include <limits>
#include <string_view>
#include <vector>

namespace {
  [[noreturn]] void fail(std::string_view detail) {
    std::cerr << "PLANK2 presentation-frame test failed: " << detail << '\n';
    std::exit(EXIT_FAILURE);
  }

  void require(bool condition, std::string_view detail) {
    if (!condition) fail(detail);
  }

  struct tuple_t {
    std::uint16_t profile;
    std::uint16_t layout;
    AVPixelFormat format;
    bool identity_gbr;
    bool chroma_422;
    std::uint8_t bytes_per_sample;
  };

  constexpr std::array<tuple_t, 9U> tuples {{
    {PLANK_MEDIA_PROFILE_H264_HIGH10_444_SOFTWARE_NVFBC_V1,
     PLANK_MEDIA_PIXEL_GBRP10_LE_V1, AV_PIX_FMT_GBRP10LE, true, false, 2U},
    {PLANK_MEDIA_PROFILE_H264_HIGH8_422_SOFTWARE_NVFBC_V1,
     PLANK_MEDIA_PIXEL_YUV422P8_V1, AV_PIX_FMT_YUV422P, false, true, 1U},
    {PLANK_MEDIA_PROFILE_H264_HIGH8_444_SOFTWARE_NVFBC_V1,
     PLANK_MEDIA_PIXEL_GBRP8_V1, AV_PIX_FMT_GBRP, true, false, 1U},
    {PLANK_MEDIA_PROFILE_H264_HIGH10_422_SOFTWARE_NVFBC_V1,
     PLANK_MEDIA_PIXEL_YUV422P10_LE_V1, AV_PIX_FMT_YUV422P10LE,
     false, true, 2U},
    {PLANK_MEDIA_PROFILE_H264_HIGH8_444_NVENC_NVFBC_V1,
     PLANK_MEDIA_PIXEL_GBRP8_V1, AV_PIX_FMT_GBRP, true, false, 1U},
    {PLANK_MEDIA_PROFILE_HEVC_REXT8_444_NVENC_NVFBC_V1,
     PLANK_MEDIA_PIXEL_GBRP8_V1, AV_PIX_FMT_GBRP, true, false, 1U},
    {PLANK_MEDIA_PROFILE_HEVC_REXT10_444_NVENC_NVFBC_V1,
     PLANK_MEDIA_PIXEL_GBRP10_LE_V1, AV_PIX_FMT_GBRP10LE, true, false, 2U},
    {PLANK_MEDIA_PROFILE_H264_HIGH10_444_SOFTWARE_X11_V1,
     PLANK_MEDIA_PIXEL_GBRP10_LE_V1, AV_PIX_FMT_GBRP10LE, true, false, 2U},
    {PLANK_MEDIA_PROFILE_HEVC_REXT10_444_NVENC_X11_V1,
     PLANK_MEDIA_PIXEL_GBRP10_LE_V1, AV_PIX_FMT_GBRP10LE, true, false, 2U},
  }};

  struct retained_fixture_t {
    explicit retained_fixture_t(const tuple_t& tuple)
        : bytes(3U * 128U * 32U, 0x5aU) {
      frame.profileId = tuple.profile;
      frame.pixelLayout = tuple.layout;
      frame.memoryKind = PLANK_MEDIA_MEMORY_CPU_V1;
      frame.planeCount = 3U;
      frame.width = 63U;
      frame.height = 32U;
      frame.frameSequence = 7U;
      frame.frameTimestampNs = UINT64_C(123456789);
      frame.frameLeaseId = 11U;
      frame.topologyGeneration = "presentation-frame-test";
      for (std::uint16_t index = 0U; index != frame.planeCount; ++index) {
        frame.planes[index] = {
          reinterpret_cast<std::uintptr_t>(bytes.data() + index * 128U * 32U),
          -1, 0U, 128U, 128U * 32U, 0U,
        };
      }
    }

    std::vector<std::uint8_t> bytes;
    PlankRetainedPresentationFrame frame;
  };
}

int main()
{
    for (const auto& tuple : tuples) {
        retained_fixture_t fixture(tuple);
        auto frame = createPlank2PresentationAvFrame(fixture.frame);
        require(frame != nullptr, "an exact retained tuple was rejected");
        require(frame->format == tuple.format && frame->width == 63 &&
                    frame->height == 32 &&
                    frame->pts == static_cast<std::int64_t>(
                        fixture.frame.frameTimestampNs) &&
                    frame->time_base.num == 1 &&
                    frame->time_base.den == 1000000000 &&
                    frame->color_range == AVCOL_RANGE_JPEG &&
                    frame->color_primaries == AVCOL_PRI_BT709 &&
                    frame->colorspace == (tuple.identity_gbr ? AVCOL_SPC_RGB
                                                             : AVCOL_SPC_BT709) &&
                    frame->color_trc == (tuple.identity_gbr ?
                        AVCOL_TRC_IEC61966_2_1 : AVCOL_TRC_BT709) &&
                    frame->chroma_location == (tuple.chroma_422 ?
                        AVCHROMA_LOC_LEFT : AVCHROMA_LOC_UNSPECIFIED),
                "AVFrame identity or exact color metadata changed");
        for (std::uint16_t index = 0U; index != 3U; ++index) {
            require(frame->data[index] == reinterpret_cast<std::uint8_t*>(
                        fixture.frame.planes[index].native_handle) &&
                        frame->linesize[index] == 128 &&
                        frame->buf[index] == nullptr,
                    "AVFrame view copied or took ownership of a CPU plane");
        }
    }

    retained_fixture_t invalid(tuples.front());
    invalid.frame.pixelLayout = PLANK_MEDIA_PIXEL_YUV422P10_LE_V1;
    require(!createPlank2PresentationAvFrame(invalid.frame),
            "a profile/layout mismatch was accepted");
    invalid = retained_fixture_t(tuples.front());
    invalid.frame.memoryKind = PLANK_MEDIA_MEMORY_VULKAN_V1;
    require(!createPlank2PresentationAvFrame(invalid.frame),
            "non-CPU memory was accepted");
    invalid = retained_fixture_t(tuples.front());
    invalid.frame.planeCount = 1U;
    require(!createPlank2PresentationAvFrame(invalid.frame),
            "the wrong plane count was accepted");
    invalid = retained_fixture_t(tuples.front());
    invalid.frame.planes[0].stride_bytes = 63U;
    require(!createPlank2PresentationAvFrame(invalid.frame),
            "an undersized 10-bit stride was accepted");
    invalid = retained_fixture_t(tuples.front());
    invalid.frame.planes[0].size_bytes = 127U;
    require(!createPlank2PresentationAvFrame(invalid.frame),
            "an undersized plane allocation was accepted");
    invalid = retained_fixture_t(tuples.front());
    invalid.frame.planes[0].offset_bytes = 64U;
    invalid.frame.planes[0].size_bytes = 128U * 32U;
    require(!createPlank2PresentationAvFrame(invalid.frame),
            "an out-of-bounds plane offset was accepted");
    invalid = retained_fixture_t(tuples.front());
    invalid.frame.planes[0].native_handle =
            std::numeric_limits<std::uintptr_t>::max();
    invalid.frame.planes[0].offset_bytes = 1U;
    require(!createPlank2PresentationAvFrame(invalid.frame),
            "a wrapping plane address was accepted");
    invalid = retained_fixture_t(tuples.front());
    invalid.frame.frameTimestampNs =
            static_cast<std::uint64_t>(INT64_MAX) + 1U;
    require(!createPlank2PresentationAvFrame(invalid.frame),
            "an unrepresentable timestamp was accepted");

    std::cout << "plank2_presentation_frame_test=pass\n";
    return EXIT_SUCCESS;
}
