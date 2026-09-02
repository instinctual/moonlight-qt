/* SPDX-License-Identifier: GPL-3.0-only */

#include "streaming/video/plank2decodersource.h"

#include "decoder_source_v1.hpp"

#include <memory>
#include <utility>

namespace plank::platform::linux_backend {
  namespace {
    class retained_decoder_stream_t final: public decoder_stream_t {
    public:
      explicit retained_decoder_stream_t(
          std::weak_ptr<IPlankRetainedDecoderTarget> target)
          : target_(std::move(target)) {
      }

      ~retained_decoder_stream_t() override {
        if (const auto target = target_.lock()) target->close();
      }

      PlankBackendOperationResultV1 submit(
          const PlankMediaPacketLeaseV1 &packet) override {
        const auto target = target_.lock();
        if (!target || !target->available()) {
          return PLANK_BACKEND_OPERATION_UNAVAILABLE_V1;
        }
        const PlankRetainedDecoderPacket retained {
          packet.profile_id, packet.flags, packet.frame_sequence,
          packet.presentation_timestamp_ns, packet.decode_timestamp_ns,
          packet.data, packet.size, packet.lease_id,
        };
        return target->submit(retained);
      }

      PlankBackendOperationResultV1 next(
          std::uint32_t timeout_ms, decoded_frame_t &frame) override {
        const auto target = target_.lock();
        if (!target || !target->available()) {
          return PLANK_BACKEND_OPERATION_UNAVAILABLE_V1;
        }
        PlankRetainedDecodedFrame retained;
        const auto result = target->next(timeout_ms, retained);
        if (result != PLANK_BACKEND_OPERATION_OK_V1) return result;
        frame = {
          retained.profileId, retained.pixelLayout, retained.memoryKind,
          retained.planeCount, retained.width, retained.height,
          retained.frameSequence, retained.monotonicTimestampNs,
          retained.planes, std::move(retained.owner),
        };
        return PLANK_BACKEND_OPERATION_OK_V1;
      }

      PlankBackendOperationResultV1 reset(
          std::uint64_t recovery_epoch) override {
        const auto target = target_.lock();
        if (!target || !target->available()) {
          return PLANK_BACKEND_OPERATION_UNAVAILABLE_V1;
        }
        return target->reset(recovery_epoch);
      }

      PlankBackendOperationResultV1 flush() override {
        const auto target = target_.lock();
        if (!target || !target->available()) {
          return PLANK_BACKEND_OPERATION_UNAVAILABLE_V1;
        }
        return target->flush();
      }

    private:
      std::weak_ptr<IPlankRetainedDecoderTarget> target_;
    };

    class retained_decoder_source_t final: public decoder_source_t {
    public:
      explicit retained_decoder_source_t(
          std::weak_ptr<IPlankRetainedDecoderTarget> target)
          : target_(std::move(target)) {
      }

      bool available() override {
        const auto target = target_.lock();
        return target && target->available();
      }

      decoder_qualify_result_t qualify(
          const PlankMediaProfileCapabilityV1 &capability) override {
        const auto target = target_.lock();
        if (!target || !target->available()) {
          return decoder_qualify_result_t::unavailable;
        }
        return target->qualifies(
                 capability.profile_id, capability.pixel_layout,
                 PLANK_MEDIA_MEMORY_CPU_V1)
          ? decoder_qualify_result_t::available
          : decoder_qualify_result_t::unsupported;
      }

      PlankBackendOperationResultV1 open(
          const PlankDecoderOpenRequestV1 &request,
          const PlankMediaProfileCapabilityV1 &capability,
          std::unique_ptr<decoder_stream_t> &stream) override {
        stream.reset();
        const auto target = target_.lock();
        if (!target || !target->available()) {
          return PLANK_BACKEND_OPERATION_UNAVAILABLE_V1;
        }
        if (!target->qualifies(capability.profile_id, capability.pixel_layout,
                               PLANK_MEDIA_MEMORY_CPU_V1)) {
          return PLANK_BACKEND_OPERATION_UNSUPPORTED_V1;
        }
        const PlankRetainedDecoderOpenRequest retained {
          request.profile_id, capability.pixel_layout,
          PLANK_MEDIA_MEMORY_CPU_V1, request.width, request.height,
          request.refresh_millihz, request.topology_generation,
        };
        const auto result = target->open(retained);
        if (result != PLANK_BACKEND_OPERATION_OK_V1) return result;
        try {
          stream = std::make_unique<retained_decoder_stream_t>(target_);
        } catch (...) {
          target->close();
          throw;
        }
        return PLANK_BACKEND_OPERATION_OK_V1;
      }

    private:
      std::weak_ptr<IPlankRetainedDecoderTarget> target_;
    };
  }

  std::shared_ptr<decoder_source_t>
  create_retained_ffmpeg_decoder_source_v1(
      std::weak_ptr<IPlankRetainedDecoderTarget> target) {
    return std::make_shared<retained_decoder_source_t>(std::move(target));
  }
}
