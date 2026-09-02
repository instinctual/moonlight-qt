/* SPDX-License-Identifier: GPL-3.0-only */

#include "streaming/video/ffmpegtestframes.h"
#include "streaming/video/plank2ffmpegdecodertarget.h"

#include "decoder_source_v1.hpp"
#include "plank/media/interfaces_v1.h"
#include "plank/media/profile_v1.h"
#include "plank/platform/linux/decoder_backend_v1.h"

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

  void test_composed_backend() {
    auto target = createPlank2FfmpegSoftwareDecoderTarget();
    require(target && target->available(),
            "composed software decoder is unavailable");
    auto source =
      plank::platform::linux_backend::create_retained_ffmpeg_decoder_source_v1(
        target);
    require(source != nullptr, "retained decoder source was not created");

    PlankLinuxDecoderBackendV1 *backend = nullptr;
    require(
      plank::platform::linux_backend::create_decoder_backend_for_source_v1(
        source, &backend) == PLANK_LINUX_DECODER_BACKEND_OK_V1 &&
        backend != nullptr,
      "generic decoder backend was not created");
    const auto *descriptor = plank_linux_decoder_backend_descriptor_v1(backend);
    require(descriptor != nullptr &&
              descriptor->kind == PLANK_BACKEND_KIND_DECODE_V1,
            "generic decoder descriptor is invalid");
    const auto *interface_v1 =
      reinterpret_cast<const PlankDecoderInterfaceV1 *>(
        descriptor->interface_v1);
    require(plank_decoder_interface_validate_v1(interface_v1) ==
              PLANK_MEDIA_INTERFACE_OK_V1 &&
              interface_v1->capabilities.count == exact_tuples.size(),
            "generic decoder interface is invalid");

    PlankBackendProbeV1 probe {};
    require(interface_v1->probe.probe(interface_v1->probe.context, &probe) ==
              PLANK_BACKEND_PROBE_OK_V1 &&
              probe.availability == PLANK_BACKEND_AVAILABILITY_AVAILABLE_V1,
            "generic decoder probe failed");
    for (std::size_t index = 0U; index != exact_tuples.size(); ++index) {
      require(plank_media_capability_probe_v1(
                &interface_v1->capabilities, index, &probe) ==
                  PLANK_MEDIA_CAPABILITY_OK_V1 &&
                probe.availability == PLANK_BACKEND_AVAILABILITY_AVAILABLE_V1,
              "an exact composed decoder capability did not qualify");
    }

    constexpr std::uint16_t profile =
      PLANK_MEDIA_PROFILE_H264_HIGH10_444_SOFTWARE_NVFBC_V1;
    PlankDecoderOpenRequestV1 request {
      sizeof(PlankDecoderOpenRequestV1), PLANK_MEDIA_INTERFACE_VERSION,
      profile, 1280U, 720U, 60000U, PLANK_MEDIA_MEMORY_CPU_BIT_V1,
      "composed-generation-1",
    };
    PlankBackendErrorV1 error {};
    void *session = nullptr;
    require(interface_v1->open(interface_v1->probe.context, &request, &session,
                               &error) == PLANK_BACKEND_OPERATION_OK_V1 &&
              session != nullptr,
            "composed decoder did not open");

    const auto sample = plankFfmpegTestFrame(
      PlankFfmpegTestFrameKind::H264High10_444);
    require(sample.data != nullptr && sample.size > 2U,
            "composed decoder test frame is missing");
    const auto split = sample.size / 2U;
    PlankMediaPacketLeaseV1 first {
      sizeof(PlankMediaPacketLeaseV1), PLANK_MEDIA_INTERFACE_VERSION,
      profile, PLANK_MEDIA_PACKET_KEY_FRAME_V1, 1U, UINT64_C(3000000000),
      UINT64_C(2999000000), sample.data, split, 21U,
    };
    PlankMediaPacketLeaseV1 second {
      sizeof(PlankMediaPacketLeaseV1), PLANK_MEDIA_INTERFACE_VERSION,
      profile, PLANK_MEDIA_PACKET_END_OF_FRAME_V1, 1U,
      UINT64_C(3000000000), UINT64_C(2999000000), sample.data + split,
      sample.size - split, 22U,
    };
    require(interface_v1->submit_packet(session, &first, &error) ==
              PLANK_BACKEND_OPERATION_OK_V1 &&
              interface_v1->submit_packet(session, &second, &error) ==
                PLANK_BACKEND_OPERATION_OK_V1,
            "composed decoder rejected fragmented input");

    PlankMediaFrameLeaseV1 frame {};
    require(interface_v1->next_frame(session, 0U, &frame, &error) ==
              PLANK_BACKEND_OPERATION_OK_V1 &&
              plank_media_frame_lease_validate_v1(&frame) ==
                PLANK_MEDIA_INTERFACE_OK_V1,
            "composed decoder produced no valid frame lease");
    require(frame.stage == PLANK_MEDIA_FRAME_STAGE_DECODED_V1 &&
              frame.profile_id == profile &&
              frame.pixel_layout == PLANK_MEDIA_PIXEL_GBRP10_LE_V1 &&
              frame.memory_kind == PLANK_MEDIA_MEMORY_CPU_V1 &&
              frame.plane_count == 3U && frame.width == request.width &&
              frame.height == request.height && frame.frame_sequence == 1U &&
              frame.monotonic_timestamp_ns == UINT64_C(3000000000) &&
              std::string_view(frame.topology_generation) ==
                request.topology_generation,
            "composed decoded-frame identity changed");
    for (std::uint16_t index = 0U; index != frame.plane_count; ++index) {
      require(frame.planes[index].native_handle != 0U &&
                frame.planes[index].dma_buf_fd == -1 &&
                frame.planes[index].stride_bytes != 0U &&
                frame.planes[index].size_bytes != 0U,
              "composed decoded CPU plane is invalid");
    }
    require(interface_v1->reset(session, 1U, &error) ==
              PLANK_BACKEND_OPERATION_INVALID_ARGUMENT_V1,
            "composed reset discarded an outstanding frame lease");
    interface_v1->release_frame(session, frame.lease_id);
    require(interface_v1->reset(session, 1U, &error) ==
              PLANK_BACKEND_OPERATION_OK_V1,
            "composed decoder recovery reset failed");

    PlankMediaPacketLeaseV1 recovered {
      sizeof(PlankMediaPacketLeaseV1), PLANK_MEDIA_INTERFACE_VERSION,
      profile,
      PLANK_MEDIA_PACKET_KEY_FRAME_V1 | PLANK_MEDIA_PACKET_END_OF_FRAME_V1,
      1U, UINT64_C(4000000000), UINT64_C(3999000000), sample.data,
      sample.size, 23U,
    };
    require(interface_v1->submit_packet(session, &recovered, &error) ==
              PLANK_BACKEND_OPERATION_OK_V1 &&
              interface_v1->next_frame(session, 0U, &frame, &error) ==
                PLANK_BACKEND_OPERATION_OK_V1,
            "composed decoder did not recover after reset");
    require(frame.frame_sequence == 1U &&
              frame.monotonic_timestamp_ns == UINT64_C(4000000000),
            "composed recovery frame identity changed");
    interface_v1->release_frame(session, frame.lease_id);
    require(interface_v1->flush(session, &error) ==
              PLANK_BACKEND_OPERATION_OK_V1,
            "composed decoder flush failed");
    interface_v1->close(session);
    plank_linux_decoder_backend_destroy_v1(backend);
  }
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

  test_composed_backend();

  std::cout << "plank2_ffmpeg_decoder_target_test=pass\n";
  return EXIT_SUCCESS;
}
