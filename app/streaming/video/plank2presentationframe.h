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

// Creates a non-owning AVFrame view over a retained CPU frame lease. The
// caller must destroy the returned view before releasing the submitted PLANK
// frame. The view never takes ownership of, copies, or extends the lifetime of
// the plane storage.
Plank2AvFramePtr createPlank2PresentationAvFrame(
        const PlankRetainedPresentationFrame& frame) noexcept;
