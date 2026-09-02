/* SPDX-License-Identifier: GPL-3.0-only */

#include "streaming/video/ffmpegtestframes.h"
#include "streaming/video/plank2ffmpegdecodertarget.h"

#include "plank/media/profile_v1.h"

#include <array>
#include <cstdint>
#include <cstdlib>
#include <iostream>
#include <string_view>

namespace {
  [[noreturn]] void fail(std::string_view detail) {
    std::cerr << "PLANK2 FFmpeg decoder target test failed: " << detail
              << '\n';
    std::exit(EXIT_FAILURE);
  }

  void require(bool condition, std::string_view detail) {
    if (!condition) fail(detail);
  }

  struct exact_tuple_t {
    std::uint16_t profile_id;
    std::uint16_t pixel_layout;
  };

  constexpr std::array<exact_tuple_t, 9U> exact_tuples {{
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
}

int main() {
  auto target = createPlank2FfmpegSoftwareDecoderTarget();
  require(target && target->available(), "software codecs are unavailable");

  for (const auto tuple : exact_tuples) {
    if (!target->qualifies(tuple.profile_id, tuple.pixel_layout,
                           PLANK_MEDIA_MEMORY_CPU_V1)) {
      std::cerr << "Rejected profile ID " << tuple.profile_id << '\n';
      fail("an exact profile test frame did not qualify");
    }
    require(!target->qualifies(tuple.profile_id,
                               PLANK_MEDIA_PIXEL_BGRX8_V1,
                               PLANK_MEDIA_MEMORY_CPU_V1),
            "a mismatched pixel layout qualified");
    require(!target->qualifies(tuple.profile_id, tuple.pixel_layout,
                               PLANK_MEDIA_MEMORY_VULKAN_V1),
            "an unimplemented memory kind qualified");
  }

  PlankRetainedDecoderOpenRequest open_request {
    PLANK_MEDIA_PROFILE_H264_HIGH10_444_SOFTWARE_NVFBC_V1,
    PLANK_MEDIA_PIXEL_GBRP10_LE_V1, PLANK_MEDIA_MEMORY_CPU_V1,
    1280U, 720U, 60000U, "test-topology",
  };
  require(target->open(open_request) == PLANK_BACKEND_OPERATION_OK_V1,
          "exact software decoder did not open");
  require(target->open(open_request) == PLANK_BACKEND_OPERATION_UNAVAILABLE_V1,
          "a second stream opened on a single target");

  const auto sample = plankFfmpegTestFrame(
    PlankFfmpegTestFrameKind::H264High10_444);
  require(sample.data != nullptr && sample.size > 2U,
          "H.264 High 10 qualification frame is missing");
  const auto split = sample.size / 2U;
  PlankRetainedDecoderPacket first {
    open_request.profileId, PLANK_MEDIA_PACKET_KEY_FRAME_V1,
    1U, UINT64_C(2000000000), UINT64_C(1999000000),
    sample.data, split, 11U,
  };
  PlankRetainedDecoderPacket second {
    open_request.profileId, PLANK_MEDIA_PACKET_END_OF_FRAME_V1,
    1U, UINT64_C(2000000000), UINT64_C(1999000000),
    sample.data + split, sample.size - split, 12U,
  };
  require(target->submit(first) == PLANK_BACKEND_OPERATION_OK_V1,
          "first compressed-frame fragment was rejected");
  require(target->submit(second) == PLANK_BACKEND_OPERATION_OK_V1,
          "completed compressed frame was rejected");

  PlankRetainedDecodedFrame frame;
  require(target->next(0U, frame) == PLANK_BACKEND_OPERATION_OK_V1,
          "software decoder produced no frame");
  require(frame.profileId == open_request.profileId &&
              frame.pixelLayout == open_request.pixelLayout &&
              frame.memoryKind == PLANK_MEDIA_MEMORY_CPU_V1 &&
              frame.planeCount == 3U && frame.width == 1280U &&
              frame.height == 720U && frame.frameSequence == 1U &&
              frame.monotonicTimestampNs == UINT64_C(2000000000) &&
              frame.owner,
          "decoded frame identity changed");
  for (std::uint16_t index = 0U; index != frame.planeCount; ++index) {
    require(frame.planes[index].native_handle != 0U &&
                frame.planes[index].dma_buf_fd == -1 &&
                frame.planes[index].stride_bytes != 0U &&
                frame.planes[index].size_bytes != 0U,
            "decoded CPU plane is invalid");
  }

  const auto first_byte = *reinterpret_cast<const std::uint8_t *>(
    frame.planes[0].native_handle);
  require(target->reset(1U) == PLANK_BACKEND_OPERATION_OK_V1,
          "decoder recovery reset failed");
  require(*reinterpret_cast<const std::uint8_t *>(
              frame.planes[0].native_handle) == first_byte,
          "reset invalidated an outstanding decoded-frame owner");
  require(target->reset(1U) == PLANK_BACKEND_OPERATION_INVALID_ARGUMENT_V1,
          "a repeated recovery epoch was accepted");
  require(target->next(0U, frame) == PLANK_BACKEND_OPERATION_AGAIN_V1,
          "reset left a decoded frame queued");
  require(target->flush() == PLANK_BACKEND_OPERATION_OK_V1,
          "decoder flush failed");
  target->close();

  auto invalid_target = createPlank2FfmpegSoftwareDecoderTarget();
  open_request.pixelLayout = PLANK_MEDIA_PIXEL_GBRP8_V1;
  require(invalid_target->open(open_request) ==
              PLANK_BACKEND_OPERATION_INVALID_ARGUMENT_V1,
          "mismatched open tuple was accepted");

  std::cout << "plank2_ffmpeg_decoder_target_test=pass\n";
  return EXIT_SUCCESS;
}
