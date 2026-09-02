/* SPDX-License-Identifier: GPL-3.0-only */

#pragma once

#include "streaming/video/plank2presentationsource.h"

extern "C" {
#include <libavutil/frame.h>
}

#include <cstdint>
#include <memory>
#include <string>

enum class Plank2SdlVulkanPresentResult
{
    Presented,
    NoDrawableTarget,
    Failed,
};

// Render-thread-owned portion of the retained SDL3/Vulkan presenter. A
// Presented result means all libplacebo/Vulkan access to frame storage has
// completed before the call returns. actualPresentTimestampNs is the
// monotonic time at which that completion was observed, not a claim of a
// physical scanout timestamp.
class IPlank2SdlVulkanFrameSink
{
public:
    virtual ~IPlank2SdlVulkanFrameSink() = default;

    virtual bool plank2PresentationAvailable() const = 0;
    virtual bool plank2TestPresentationFrame(AVFrame* frame) = 0;
    virtual Plank2SdlVulkanPresentResult plank2PresentFrame(
            AVFrame* frame,
            std::uint64_t targetPresentTimestampNs,
            std::uint64_t& actualPresentTimestampNs) = 0;
    virtual bool plank2ResetPresentation() = 0;
};

struct Plank2SdlVulkanPresentationConfiguration
{
    std::uint32_t refreshMilliHz = 0;
    PlankPresentationTransformV1 transform {};
    std::string topologyGeneration;
};

// The target is created, qualified, opened, presented, and reset on one
// renderer thread. The immutable configuration must describe the SDL windows
// and output topology already used to initialize the retained PlVkRenderer.
std::shared_ptr<IPlankRetainedPresentationTarget>
createPlank2SdlVulkanPresentationTarget(
        std::weak_ptr<IPlank2SdlVulkanFrameSink> sink,
        Plank2SdlVulkanPresentationConfiguration configuration);
