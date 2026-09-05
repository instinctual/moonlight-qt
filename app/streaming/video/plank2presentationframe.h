/* SPDX-License-Identifier: GPL-3.0-only */

#pragma once

#include "streaming/video/plank2presentationsource.h"

extern "C" {
#include <libavutil/frame.h>
}

#include <memory>

struct Plank2AvFrameDeleter
{
    void operator()(AVFrame* frame) const noexcept;
};

using Plank2AvFramePtr = std::unique_ptr<AVFrame, Plank2AvFrameDeleter>;

// Creates a non-owning AVFrame view over a retained CPU or Y410 DMA-BUF lease. The
// caller must destroy the returned view before releasing the submitted PLANK
// frame. The view never takes ownership of, copies, or extends the lifetime of
// the plane storage. DMA-BUF views own only their AVDRMFrameDescriptor metadata;
// the producer retains the file descriptors and backing surface until release.
Plank2AvFramePtr createPlank2PresentationAvFrame(
        const PlankRetainedPresentationFrame& frame) noexcept;
