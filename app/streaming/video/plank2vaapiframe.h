/* SPDX-License-Identifier: GPL-3.0-only */
#pragma once

#include "streaming/video/plank2decodersource.h"

extern "C" {
#include <libavutil/frame.h>
#include <va/va.h>
#include <va/va_drmcommon.h>
}

// Injectable driver calls for ownership/failure tests. Production uses the
// same read-only composed export and synchronization as the retained renderer.
struct Plank2VaapiExportOperations
{
    VAStatus (*exportSurface)(VADisplay, VASurfaceID, uint32_t, uint32_t, void*);
    VAStatus (*syncSurface)(VADisplay, VASurfaceID);
};

// Retains the actual decoded surface and owns its exported fds for the full
// returned frame lease. Does not download pixels or select a decoder. The
// caller must have qualified the codec/profile through a real test decode.
PlankBackendOperationResultV1 createPlank2VaapiFrameLease(
        const AVFrame* decoded, std::uint16_t profileId,
        std::uint64_t frameSequence, std::uint64_t timestampNs,
        PlankRetainedDecodedFrame& output,
        const Plank2VaapiExportOperations* operations = nullptr);
