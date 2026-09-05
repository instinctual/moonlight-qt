/* SPDX-License-Identifier: GPL-3.0-only */
#include "streaming/video/plank2vaapiframe.h"

extern "C" {
#include <libavutil/hwcontext.h>
#include <libavutil/hwcontext_vaapi.h>
#include <libavutil/pixdesc.h>
}

#include <algorithm>
#include <climits>
#include <memory>
#include <unistd.h>

namespace {
    struct exported_frame_t {
        AVFrame* decoded = nullptr;
        VADRMPRIMESurfaceDescriptor descriptor {};

        exported_frame_t() {
            for (auto& object : descriptor.objects) object.fd = -1;
        }
        ~exported_frame_t() {
            const auto count = std::min(descriptor.num_objects, uint32_t{4});
            for (uint32_t i = 0; i < count; ++i) {
                if (descriptor.objects[i].fd < 0) continue;
                bool duplicate = false;
                for (uint32_t j = 0; j < i; ++j) {
                    duplicate |= descriptor.objects[j].fd == descriptor.objects[i].fd;
                }
                if (!duplicate) close(descriptor.objects[i].fd);
            }
            av_frame_free(&decoded);
        }
    };

    const Plank2VaapiExportOperations driverOperations {
        vaExportSurfaceHandle, vaSyncSurface,
    };
}

PlankBackendOperationResultV1 createPlank2VaapiFrameLease(
        const AVFrame* decoded, std::uint16_t profileId,
        std::uint64_t frameSequence, std::uint64_t timestampNs,
        PlankRetainedDecodedFrame& output,
        const Plank2VaapiExportOperations* operations)
{
    output = {};
    if (!decoded || decoded->format != AV_PIX_FMT_VAAPI ||
            !decoded->hw_frames_ctx || !decoded->hw_frames_ctx->data ||
            decoded->hw_frames_ctx->size < sizeof(AVHWFramesContext) ||
            decoded->width <= 0 || decoded->height <= 0 ||
            decoded->width > 32768 || decoded->height > 32768 ||
            frameSequence == 0 || timestampNs == 0 || timestampNs > INT64_MAX ||
            decoded->color_range != AVCOL_RANGE_JPEG ||
            decoded->colorspace != AVCOL_SPC_RGB ||
            !plank_media_pixel_layout_matches_profile_stage_v1(
                PLANK_MEDIA_FRAME_STAGE_DECODED_V1,
                PLANK_MEDIA_PIXEL_Y410_LE_V1, profileId)) {
        return PLANK_BACKEND_OPERATION_INVALID_ARGUMENT_V1;
    }
    const auto* frames = reinterpret_cast<const AVHWFramesContext*>(decoded->hw_frames_ctx->data);
    const auto* format = av_pix_fmt_desc_get(frames->sw_format);
    if (frames->format != AV_PIX_FMT_VAAPI || !frames->device_ctx ||
            frames->device_ctx->type != AV_HWDEVICE_TYPE_VAAPI ||
            !frames->device_ctx->hwctx || !format || format->nb_components < 3 ||
            format->log2_chroma_w != 0 || format->log2_chroma_h != 0 ||
            format->comp[0].depth != 10 || format->comp[1].depth != 10 ||
            format->comp[2].depth != 10) {
        return PLANK_BACKEND_OPERATION_UNSUPPORTED_V1;
    }
    const auto* device = static_cast<const AVVAAPIDeviceContext*>(frames->device_ctx->hwctx);
    if (!device->display) return PLANK_BACKEND_OPERATION_UNAVAILABLE_V1;
    const auto& ops = operations ? *operations : driverOperations;
    if (!ops.exportSurface || !ops.syncSurface) return PLANK_BACKEND_OPERATION_INVALID_ARGUMENT_V1;

    auto owner = std::make_shared<exported_frame_t>();
    owner->decoded = av_frame_clone(decoded);
    if (!owner->decoded) return PLANK_BACKEND_OPERATION_FAILED_V1;
    const auto surface = static_cast<VASurfaceID>(reinterpret_cast<uintptr_t>(decoded->data[3]));
    if (ops.exportSurface(device->display, surface,
            VA_SURFACE_ATTRIB_MEM_TYPE_DRM_PRIME_2,
            VA_EXPORT_SURFACE_READ_ONLY | VA_EXPORT_SURFACE_COMPOSED_LAYERS,
            &owner->descriptor) != VA_STATUS_SUCCESS) {
        return PLANK_BACKEND_OPERATION_FAILED_V1;
    }
    const auto& descriptor = owner->descriptor;
    // The coded allocation can include right/bottom padding (2160 visible
    // rows in a 2176-row HEVC surface). Import the visible rectangle with the
    // original allocation pitch/modifier, never stretch padding into view.
    if (frames->width < decoded->width || frames->height < decoded->height ||
            frames->width > 32768 || frames->height > 32768 ||
            decoded->crop_left != 0 || decoded->crop_top != 0 ||
            descriptor.width != static_cast<uint32_t>(frames->width) ||
            descriptor.height != static_cast<uint32_t>(frames->height) ||
            descriptor.num_objects == 0 || descriptor.num_objects > 4 ||
            descriptor.num_layers != 1 ||
            descriptor.layers[0].drm_format != UINT32_C(0x30313459) ||
            descriptor.layers[0].num_planes == 0 ||
            descriptor.layers[0].num_planes > PLANK_MEDIA_MAX_PLANES_V1) {
        return PLANK_BACKEND_OPERATION_UNSUPPORTED_V1;
    }
    PlankMediaFrameLeaseV1 lease {
        sizeof(PlankMediaFrameLeaseV1), PLANK_MEDIA_INTERFACE_VERSION,
        PLANK_MEDIA_FRAME_STAGE_DECODED_V1, profileId, PLANK_MEDIA_PIXEL_Y410_LE_V1,
        PLANK_MEDIA_MEMORY_DMA_BUF_V1,
        static_cast<uint16_t>(descriptor.layers[0].num_planes),
        static_cast<uint32_t>(decoded->width), static_cast<uint32_t>(decoded->height),
        frameSequence, timestampNs,
        "vaapi-export-validation", 1U, {}, 0U,
    };
    for (uint32_t i = 0; i < descriptor.num_objects; ++i) {
        if (descriptor.objects[i].fd < 0 || descriptor.objects[i].size == 0) {
            return PLANK_BACKEND_OPERATION_FAILED_V1;
        }
        for (uint32_t j = 0; j < i; ++j) {
            if (descriptor.objects[i].fd == descriptor.objects[j].fd) {
                return PLANK_BACKEND_OPERATION_FAILED_V1;
            }
        }
    }
    const auto& layer = descriptor.layers[0];
    for (uint32_t i = 0; i < layer.num_planes; ++i) {
        if (layer.object_index[i] >= descriptor.num_objects) return PLANK_BACKEND_OPERATION_FAILED_V1;
        const auto& object = descriptor.objects[layer.object_index[i]];
        lease.planes[i] = {0U, object.fd, layer.offset[i], layer.pitch[i],
                          object.size, object.drm_format_modifier};
    }
    if (plank_media_frame_lease_validate_v1(&lease) != PLANK_MEDIA_INTERFACE_OK_V1) {
        return PLANK_BACKEND_OPERATION_FAILED_V1;
    }
    if (ops.syncSurface(device->display, surface) != VA_STATUS_SUCCESS) {
        return PLANK_BACKEND_OPERATION_FAILED_V1;
    }
    output.profileId = profileId;
    output.pixelLayout = lease.pixel_layout;
    output.memoryKind = lease.memory_kind;
    output.planeCount = lease.plane_count;
    output.width = lease.width;
    output.height = lease.height;
    output.frameSequence = frameSequence;
    output.monotonicTimestampNs = timestampNs;
    for (uint16_t i = 0; i < lease.plane_count; ++i) output.planes[i] = lease.planes[i];
    output.owner = std::move(owner);
    return PLANK_BACKEND_OPERATION_OK_V1;
}
