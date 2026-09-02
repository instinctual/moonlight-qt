/* SPDX-License-Identifier: GPL-3.0-only */

#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <memory>
#include <string>

#include "plank/backend/operations_v1.h"
#include "plank/media/interfaces_v1.h"

namespace plank::platform::linux_backend {
  class decoder_source_t;
}

struct PlankRetainedDecoderOpenRequest
{
    std::uint16_t profileId = 0;
    std::uint16_t pixelLayout = 0;
    std::uint16_t memoryKind = 0;
    std::uint32_t width = 0;
    std::uint32_t height = 0;
    std::uint32_t refreshMilliHz = 0;
    std::string topologyGeneration;
};

struct PlankRetainedDecoderPacket
{
    std::uint16_t profileId = 0;
    std::uint32_t flags = 0;
    std::uint64_t frameSequence = 0;
    std::uint64_t presentationTimestampNs = 0;
    std::uint64_t decodeTimestampNs = 0;
    const std::uint8_t* data = nullptr;
    std::size_t size = 0;
    std::uint64_t packetLeaseId = 0;
};

struct PlankRetainedDecodedFrame
{
    std::uint16_t profileId = 0;
    std::uint16_t pixelLayout = 0;
    std::uint16_t memoryKind = 0;
    std::uint16_t planeCount = 0;
    std::uint32_t width = 0;
    std::uint32_t height = 0;
    std::uint64_t frameSequence = 0;
    std::uint64_t monotonicTimestampNs = 0;
    std::array<PlankMediaPlaneV1, PLANK_MEDIA_MAX_PLANES_V1> planes {};
    std::shared_ptr<void> owner;
};

// Narrow boundary to the retained FFmpeg decoder engine. submit() may inspect
// packet bytes only for the duration of the call and must not retain the
// producer's storage. A successful next() transfers a shared owner that keeps
// every returned plane/native handle valid until the PLANK2 frame lease ends.
// qualifies() means a real profile-specific test frame proved the exact codec,
// bit depth, chroma, range, identity mapping, pixel layout, and memory kind.
class IPlankRetainedDecoderTarget
{
public:
    virtual ~IPlankRetainedDecoderTarget() = default;

    virtual bool available() const = 0;
    virtual bool qualifies(std::uint16_t profileId,
                           std::uint16_t pixelLayout,
                           std::uint16_t memoryKind) const = 0;
    virtual PlankBackendOperationResultV1 open(
            const PlankRetainedDecoderOpenRequest& request) = 0;
    virtual PlankBackendOperationResultV1 submit(
            const PlankRetainedDecoderPacket& packet) = 0;
    virtual PlankBackendOperationResultV1 next(
            std::uint32_t timeoutMs,
            PlankRetainedDecodedFrame& frame) = 0;
    virtual PlankBackendOperationResultV1 reset(
            std::uint64_t recoveryEpoch) = 0;
    virtual PlankBackendOperationResultV1 flush() = 0;
    virtual void close() = 0;
};

namespace plank::platform::linux_backend {
  std::shared_ptr<decoder_source_t>
  create_retained_ffmpeg_decoder_source_v1(
    std::weak_ptr<IPlankRetainedDecoderTarget> target
  );
}
