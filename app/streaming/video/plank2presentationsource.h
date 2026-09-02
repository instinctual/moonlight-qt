#pragma once

#include <array>
#include <cstdint>
#include <memory>
#include <string>

#include "plank/backend/operations_v1.h"
#include "plank/display/presentation_interface_v1.h"
#include "plank/media/interfaces_v1.h"

namespace plank::platform::linux_backend {
  class presentation_source_t;
}

struct PlankRetainedPresentationOpenRequest
{
    std::uint16_t profileId = 0;
    std::uint16_t pixelLayout = 0;
    std::uint16_t memoryKind = 0;
    std::uint32_t refreshMilliHz = 0;
    PlankPresentationTransformV1 transform {};
    std::string topologyGeneration;
};

struct PlankRetainedPresentationFrame
{
    std::uint16_t profileId = 0;
    std::uint16_t pixelLayout = 0;
    std::uint16_t memoryKind = 0;
    std::uint16_t planeCount = 0;
    std::uint32_t width = 0;
    std::uint32_t height = 0;
    std::uint64_t frameSequence = 0;
    std::uint64_t frameTimestampNs = 0;
    std::uint64_t frameLeaseId = 0;
    std::uint64_t targetPresentTimestampNs = 0;
    std::string topologyGeneration;
    std::array<PlankMediaPlaneV1, PLANK_MEDIA_MAX_PLANES_V1> planes {};
};

struct PlankRetainedPresentationCompletion
{
    std::uint16_t state = PLANK_PRESENTATION_COMPLETION_INVALID_V1;
    std::uint32_t reasonCode = 0;
    std::uint64_t actualPresentTimestampNs = 0;
};

// Narrow, synchronous boundary to the retained SDL3/Vulkan presentation
// implementation. present() may inspect the submitted plane storage only for
// the duration of the call. An OK result must return a terminal completion, so
// the PLANK2 presenter can release the decoder lease after next_completion().
class IPlankRetainedPresentationTarget
{
public:
    virtual ~IPlankRetainedPresentationTarget() = default;

    virtual bool available() const = 0;
    virtual bool qualifies(std::uint16_t profileId,
                           std::uint16_t pixelLayout,
                           std::uint16_t memoryKind) const = 0;
    virtual PlankBackendOperationResultV1 open(
            const PlankRetainedPresentationOpenRequest& request) = 0;
    virtual PlankBackendOperationResultV1 present(
            const PlankRetainedPresentationFrame& frame,
            PlankRetainedPresentationCompletion& completion) = 0;
    virtual PlankBackendOperationResultV1 reset() = 0;
    virtual void close() = 0;
};

namespace plank::platform::linux_backend {
  std::shared_ptr<presentation_source_t>
  create_retained_sdl_vulkan_presentation_source_v1(
    std::weak_ptr<IPlankRetainedPresentationTarget> target
  );
}
