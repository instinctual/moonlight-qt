/* SPDX-License-Identifier: GPL-3.0-only */

#include "streaming/video/plank2decodersource.h"

#include "decoder_source_v1.hpp"
#include "plank/media/profile_v1.h"

#include <cstdint>
#include <cstdlib>
#include <iostream>
#include <memory>
#include <string_view>
#include <utility>
#include <vector>

namespace {
  using namespace plank::platform::linux_backend;

  [[noreturn]] void fail(std::string_view detail) {
    std::cerr << "plank2 decoder test failed: " << detail << '\n';
    std::exit(EXIT_FAILURE);
  }

  void require(bool condition, std::string_view detail) {
    if (!condition) fail(detail);
  }

  class fake_target_t final: public IPlankRetainedDecoderTarget {
  public:
    bool available() const override {
      return is_available;
    }

    bool qualifies(std::uint16_t profile_id, std::uint16_t pixel_layout,
                   std::uint16_t memory_kind) const override {
      return profile_id == exact_profile && pixel_layout == exact_layout &&
             memory_kind == PLANK_MEDIA_MEMORY_CPU_V1;
    }

    PlankBackendOperationResultV1 open(
        const PlankRetainedDecoderOpenRequest &request) override {
      ++open_calls;
      last_open = request;
      return accept_calls ? PLANK_BACKEND_OPERATION_OK_V1
                          : PLANK_BACKEND_OPERATION_UNAVAILABLE_V1;
    }

    PlankBackendOperationResultV1 submit(
        const PlankRetainedDecoderPacket &packet) override {
      ++submit_calls;
      last_packet = packet;
      copied_packet.assign(packet.data, packet.data + packet.size);
      return accept_calls ? PLANK_BACKEND_OPERATION_OK_V1
                          : PLANK_BACKEND_OPERATION_UNAVAILABLE_V1;
    }

    PlankBackendOperationResultV1 next(
        std::uint32_t timeout_ms,
        PlankRetainedDecodedFrame &frame) override {
      ++next_calls;
      last_timeout_ms = timeout_ms;
      if (!accept_calls) return PLANK_BACKEND_OPERATION_UNAVAILABLE_V1;
      frame = returned_frame;
      return PLANK_BACKEND_OPERATION_OK_V1;
    }

    PlankBackendOperationResultV1 reset(
        std::uint64_t recovery_epoch) override {
      ++reset_calls;
      last_recovery_epoch = recovery_epoch;
      return accept_calls ? PLANK_BACKEND_OPERATION_OK_V1
                          : PLANK_BACKEND_OPERATION_UNAVAILABLE_V1;
    }

    PlankBackendOperationResultV1 flush() override {
      ++flush_calls;
      return accept_calls ? PLANK_BACKEND_OPERATION_OK_V1
                          : PLANK_BACKEND_OPERATION_UNAVAILABLE_V1;
    }

    void close() override {
      ++close_calls;
    }

    bool is_available {true};
    bool accept_calls {true};
    std::uint16_t exact_profile {
      PLANK_MEDIA_PROFILE_H264_HIGH10_444_SOFTWARE_NVFBC_V1};
    std::uint16_t exact_layout {PLANK_MEDIA_PIXEL_GBRP10_LE_V1};
    std::uint32_t open_calls {};
    std::uint32_t submit_calls {};
    std::uint32_t next_calls {};
    std::uint32_t reset_calls {};
    std::uint32_t flush_calls {};
    std::uint32_t close_calls {};
    std::uint32_t last_timeout_ms {};
    std::uint64_t last_recovery_epoch {};
    PlankRetainedDecoderOpenRequest last_open;
    PlankRetainedDecoderPacket last_packet;
    PlankRetainedDecodedFrame returned_frame;
    std::vector<std::uint8_t> copied_packet;
  };

  PlankMediaProfileCapabilityV1 capability() {
    return {
      sizeof(PlankMediaProfileCapabilityV1), PLANK_MEDIA_INTERFACE_VERSION,
      PLANK_MEDIA_CAPABILITY_ROLE_DECODE_V1,
      PLANK_MEDIA_PROFILE_H264_HIGH10_444_SOFTWARE_NVFBC_V1,
      PLANK_MEDIA_PIXEL_GBRP10_LE_V1, PLANK_MEDIA_MEMORY_CPU_BIT_V1,
      PLANK_MEDIA_CAPABILITY_REAL_FRAME_PROOF_V1,
    };
  }
}

int main() {
  auto target = std::make_shared<fake_target_t>();
  auto source = create_retained_ffmpeg_decoder_source_v1(target);
  require(source != nullptr, "factory returned no source");
  require(source->available(), "available target was not reported");

  auto exact_capability = capability();
  require(source->qualify(exact_capability) ==
              decoder_qualify_result_t::available,
          "exact proven tuple was not qualified");
  auto unsupported = exact_capability;
  unsupported.pixel_layout = PLANK_MEDIA_PIXEL_GBRP8_V1;
  require(source->qualify(unsupported) ==
              decoder_qualify_result_t::unsupported,
          "mismatched tuple was qualified");

  PlankDecoderOpenRequestV1 open_request {
    sizeof(PlankDecoderOpenRequestV1), PLANK_MEDIA_INTERFACE_VERSION,
    exact_capability.profile_id, 3840U, 2160U, 60000U,
    PLANK_MEDIA_MEMORY_CPU_BIT_V1, "topology-19",
  };
  std::unique_ptr<decoder_stream_t> stream;
  require(source->open(open_request, exact_capability, stream) ==
              PLANK_BACKEND_OPERATION_OK_V1,
          "exact decoder stream did not open");
  require(stream != nullptr && target->open_calls == 1U,
          "open was not forwarded exactly once");
  require(target->last_open.profileId == exact_capability.profile_id &&
              target->last_open.pixelLayout == exact_capability.pixel_layout &&
              target->last_open.memoryKind == PLANK_MEDIA_MEMORY_CPU_V1 &&
              target->last_open.width == 3840U &&
              target->last_open.height == 2160U &&
              target->last_open.refreshMilliHz == 60000U &&
              target->last_open.topologyGeneration == "topology-19",
          "decoder open tuple changed at the retained boundary");

  std::vector<std::uint8_t> packet_bytes {0, 1, 2, 3, 4, 5};
  PlankMediaPacketLeaseV1 packet {
    sizeof(PlankMediaPacketLeaseV1), PLANK_MEDIA_INTERFACE_VERSION,
    exact_capability.profile_id,
    PLANK_MEDIA_PACKET_KEY_FRAME_V1 | PLANK_MEDIA_PACKET_END_OF_FRAME_V1,
    23U, UINT64_C(2000000000), UINT64_C(1999000000),
    packet_bytes.data(), packet_bytes.size(), 31U,
  };
  require(stream->submit(packet) == PLANK_BACKEND_OPERATION_OK_V1,
          "decoder packet submission failed");
  packet_bytes[0] = 0xff;
  require(target->submit_calls == 1U &&
              target->last_packet.frameSequence == 23U &&
              target->last_packet.packetLeaseId == 31U &&
              target->copied_packet ==
                std::vector<std::uint8_t>({0, 1, 2, 3, 4, 5}),
          "packet bytes or identity were not consumed synchronously");

  auto plane_owner = std::make_shared<std::vector<std::uint8_t>>(
    std::initializer_list<std::uint8_t> {9, 8, 7, 6});
  target->returned_frame = {
    exact_capability.profile_id, exact_capability.pixel_layout,
    PLANK_MEDIA_MEMORY_CPU_V1, 1U, 3840U, 2160U, 23U,
    UINT64_C(2001000000), {}, plane_owner,
  };
  target->returned_frame.planes[0] = {
    reinterpret_cast<std::uintptr_t>(plane_owner->data()), -1, 0U, 8U,
    plane_owner->size(), 0U,
  };
  decoded_frame_t frame;
  require(stream->next(17U, frame) == PLANK_BACKEND_OPERATION_OK_V1,
          "decoded frame was not returned");
  target->returned_frame.owner.reset();
  plane_owner.reset();
  require(target->next_calls == 1U && target->last_timeout_ms == 17U &&
              frame.profile_id == exact_capability.profile_id &&
              frame.pixel_layout == exact_capability.pixel_layout &&
              frame.memory_kind == PLANK_MEDIA_MEMORY_CPU_V1 &&
              frame.frame_sequence == 23U && frame.owner &&
              *reinterpret_cast<const std::uint8_t*>(
                frame.planes[0].native_handle) == 9U,
          "decoded frame identity or owner changed at the boundary");

  require(stream->reset(7U) == PLANK_BACKEND_OPERATION_OK_V1 &&
              target->reset_calls == 1U &&
              target->last_recovery_epoch == 7U,
          "decoder recovery epoch was not preserved");
  require(stream->flush() == PLANK_BACKEND_OPERATION_OK_V1 &&
              target->flush_calls == 1U,
          "decoder flush was not forwarded");

  stream.reset();
  require(target->close_calls == 1U, "stream did not close retained target");
  target.reset();
  require(!source->available(), "expired target remained available");
  require(source->qualify(exact_capability) ==
              decoder_qualify_result_t::unavailable,
          "expired target remained qualified");
  require(source->open(open_request, exact_capability, stream) ==
              PLANK_BACKEND_OPERATION_UNAVAILABLE_V1 && !stream,
          "expired target opened a decoder stream");

  std::cout << "plank2_retained_decoder_source_test=pass\n";
  return EXIT_SUCCESS;
}
