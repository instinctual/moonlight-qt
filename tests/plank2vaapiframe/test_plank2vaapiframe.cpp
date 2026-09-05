/* SPDX-License-Identifier: GPL-3.0-only */
#include "streaming/video/plank2vaapiframe.h"
#include "streaming/video/plank2presentationframe.h"
#include "streaming/video/ffmpegtestframes.h"
#include "plank/media/profile_v1.h"
#include "plank/platform/linux/egl_dma_buf_image_v1.hpp"
#include <GLES3/gl3.h>
#include <GLES2/gl2ext.h>
#include <gbm.h>

extern "C" {
#include <libavcodec/avcodec.h>
#include <libavutil/hwcontext_vaapi.h>
#include <libavutil/hwcontext_drm.h>
}
#include <cstdlib>
#include <cstring>
#include <iostream>
#include <fcntl.h>
#include <unistd.h>
#include <vector>
#include <algorithm>

namespace {
void check(bool ok, const char* detail) {
    if (!ok) { std::cerr << detail << '\n'; std::exit(1); }
}
constexpr auto profile = PLANK_MEDIA_PROFILE_HEVC_REXT10_444_NVENC_NVFBC_V1;
int originalFd = -1;
int exportedFd = -1;
int fault = 0;
unsigned exports = 0;
unsigned syncs = 0;
VAStatus exportFake(VADisplay, VASurfaceID, uint32_t type, uint32_t flags, void* output) {
    ++exports;
    check(type == VA_SURFACE_ATTRIB_MEM_TYPE_DRM_PRIME_2 &&
          flags == (VA_EXPORT_SURFACE_READ_ONLY | VA_EXPORT_SURFACE_COMPOSED_LAYERS),
          "wrong export policy");
    if (fault == 1) return VA_STATUS_ERROR_OPERATION_FAILED;
    auto& desc = *static_cast<VADRMPRIMESurfaceDescriptor*>(output);
    exportedFd = fcntl(originalFd, F_DUPFD_CLOEXEC, 0);
    check(exportedFd >= 0, "fd duplication failed");
    desc.width = 1280; desc.height = 720; desc.num_objects = 1; desc.num_layers = 1;
    desc.objects[0].fd = exportedFd;
    desc.objects[0].size = 4U * 1024U * 1024U;
    desc.objects[0].drm_format_modifier = UINT64_C(0x0100000000000008);
    auto& layer = desc.layers[0];
    layer.drm_format = UINT32_C(0x30313459);
    layer.num_planes = 3;
    for (unsigned i = 0; i < 3; ++i) {
        layer.object_index[i] = 0; layer.offset[i] = i * 4096; layer.pitch[i] = 5120;
    }
    if (fault == 3) layer.object_index[2] = 4;
    if (fault == 4) layer.drm_format = UINT32_C(0x3231564e); // NV12
    if (fault == 5) desc.width++;
    if (fault == 6) layer.num_planes = 5;
    if (fault == 7) layer.offset[2] = desc.objects[0].size;
    return VA_STATUS_SUCCESS;
}
VAStatus syncFake(VADisplay, VASurfaceID) {
    ++syncs;
    return fault == 2 ? VA_STATUS_ERROR_OPERATION_FAILED : VA_STATUS_SUCCESS;
}

PlankRetainedPresentationFrame presentation(const PlankRetainedDecodedFrame& decoded) {
    PlankRetainedPresentationFrame result;
    result.profileId = decoded.profileId; result.pixelLayout = decoded.pixelLayout;
    result.memoryKind = decoded.memoryKind; result.planeCount = decoded.planeCount;
    result.width = decoded.width; result.height = decoded.height;
    result.frameSequence = decoded.frameSequence;
    result.frameTimestampNs = decoded.monotonicTimestampNs;
    result.frameLeaseId = 1; result.topologyGeneration = "vaapi-proof";
    result.planes = decoded.planes;
    return result;
}

PlankMediaFrameLeaseV1 generic(const PlankRetainedDecodedFrame& frame) {
    PlankMediaFrameLeaseV1 result {
        sizeof(PlankMediaFrameLeaseV1), PLANK_MEDIA_INTERFACE_VERSION,
        PLANK_MEDIA_FRAME_STAGE_DECODED_V1, frame.profileId, frame.pixelLayout,
        frame.memoryKind, frame.planeCount, frame.width, frame.height,
        frame.frameSequence, frame.monotonicTimestampNs, "vaapi-proof", 1, {}, 0,
    };
    for (uint16_t i = 0; i < frame.planeCount; ++i) result.planes[i] = frame.planes[i];
    return result;
}

void fakeProof() {
    originalFd = open("/dev/null", O_RDONLY | O_CLOEXEC);
    check(originalFd >= 0, "test fd open failed");
    AVVAAPIDeviceContext va {};
    va.display = reinterpret_cast<VADisplay>(uintptr_t{1});
    AVHWDeviceContext device {};
    device.type = AV_HWDEVICE_TYPE_VAAPI; device.hwctx = &va;
    AVFrame* decoded = av_frame_alloc();
    check(decoded, "frame allocation failed");
    decoded->buf[0] = av_buffer_alloc(1);
    decoded->hw_frames_ctx = av_buffer_allocz(sizeof(AVHWFramesContext));
    check(decoded->buf[0] && decoded->hw_frames_ctx, "frame context allocation failed");
    auto* frames = reinterpret_cast<AVHWFramesContext*>(decoded->hw_frames_ctx->data);
    frames->format = AV_PIX_FMT_VAAPI; frames->sw_format = AV_PIX_FMT_XV30LE;
    frames->device_ctx = &device;
    decoded->format = AV_PIX_FMT_VAAPI; decoded->width = 1280; decoded->height = 720;
    decoded->color_range = AVCOL_RANGE_JPEG; decoded->colorspace = AVCOL_SPC_RGB;
    decoded->data[0] = decoded->buf[0]->data;
    decoded->data[3] = reinterpret_cast<uint8_t*>(uintptr_t{1});
    const Plank2VaapiExportOperations operations {exportFake, syncFake};
    PlankRetainedDecodedFrame lease;
    check(createPlank2VaapiFrameLease(decoded, profile, 7, 1000, lease, &operations) ==
          PLANK_BACKEND_OPERATION_OK_V1, "valid export failed");
    check(lease.planeCount == 3 && exports == 1 && syncs == 1 &&
          av_buffer_get_ref_count(decoded->buf[0]) == 2,
          "decode surface not retained or export not synchronized");
    auto view = createPlank2PresentationAvFrame(presentation(lease));
    check(view && view->format == AV_PIX_FMT_DRM_PRIME, "composed descriptor conversion failed");
    std::vector<EGLint> attributes;
    check(plank::platform::linux_backend::identity_dma_buf_attributes_v1(generic(lease), true, attributes),
          "EGL auxiliary plane attributes failed");
    check(attributes.size() == 39 && attributes[5] == EGLint{0x30335258} &&
          attributes.back() == EGL_NONE, "EGL import shape/identity changed");
    check(!plank::platform::linux_backend::identity_dma_buf_attributes_v1(generic(lease), false, attributes) &&
          attributes.empty(), "tiled frame accepted without modifier support");
    view.reset();
    check(fcntl(exportedFd, F_GETFD) >= 0, "presentation view closed export");
    lease = {};
    check(fcntl(exportedFd, F_GETFD) < 0 && av_buffer_get_ref_count(decoded->buf[0]) == 1,
          "release leaked fd or decode surface");
    for (fault = 1; fault <= 7; ++fault) {
        exportedFd = -1;
        check(createPlank2VaapiFrameLease(decoded, profile, 8, 900, lease, &operations) !=
              PLANK_BACKEND_OPERATION_OK_V1, "malformed/failing export accepted");
        check(!lease.owner && (exportedFd < 0 || fcntl(exportedFd, F_GETFD) < 0),
              "failed export leaked ownership");
    }
    fault = 0;
    frames->sw_format = AV_PIX_FMT_NV12;
    const auto previousExports = exports;
    check(createPlank2VaapiFrameLease(decoded, profile, 8, 900, lease, &operations) ==
          PLANK_BACKEND_OPERATION_UNSUPPORTED_V1 && exports == previousExports,
          "wrong precision/chroma reached exporter");
    frames->sw_format = AV_PIX_FMT_XV30LE;
    decoded->colorspace = AVCOL_SPC_BT709;
    check(createPlank2VaapiFrameLease(decoded, profile, 8, 900, lease, &operations) !=
          PLANK_BACKEND_OPERATION_OK_V1, "non-identity frame accepted");
    decoded->colorspace = AVCOL_SPC_RGB;
    check(createPlank2VaapiFrameLease(decoded, profile, 8, 900, lease, &operations) ==
          PLANK_BACKEND_OPERATION_OK_V1, "replacement lease failed");
    av_frame_free(&decoded);
    check(fcntl(exportedFd, F_GETFD) >= 0, "source close invalidated leased export");
    lease = {};
    check(fcntl(exportedFd, F_GETFD) < 0, "final release leaked export");
    close(originalFd);
    std::cout << "vaapi_frame_ownership_fake=pass\n";
}

AVPixelFormat hardwareFormat(AVCodecContext*, const AVPixelFormat* formats) {
    for (; *formats != AV_PIX_FMT_NONE; ++formats) {
        if (*formats == AV_PIX_FMT_VAAPI) return *formats;
    }
    return AV_PIX_FMT_NONE; // No software substitution in a hardware proof.
}

GLuint compileShader(GLenum kind, const char* text) {
    const GLuint shader = glCreateShader(kind);
    glShaderSource(shader, 1, &text, nullptr);
    glCompileShader(shader);
    GLint ok = 0; glGetShaderiv(shader, GL_COMPILE_STATUS, &ok);
    check(ok, "probe shader compilation failed");
    return shader;
}

void eglProof(const char* node, const PlankRetainedDecodedFrame& frame) {
    const int fd = open(node, O_RDWR | O_CLOEXEC);
    check(fd >= 0, "EGL render node unavailable");
    auto* gbm = gbm_create_device(fd);
    check(gbm, "GBM device unavailable");
    const EGLDisplay display = eglGetPlatformDisplay(EGL_PLATFORM_GBM_KHR, gbm, nullptr);
    check(display != EGL_NO_DISPLAY && eglInitialize(display, nullptr, nullptr), "EGL initialization failed");
    check(eglBindAPI(EGL_OPENGL_ES_API), "GLES API unavailable");
    const EGLint contextAttributes[] = {EGL_CONTEXT_CLIENT_VERSION, 3, EGL_NONE};
    const EGLContext context = eglCreateContext(display, EGL_NO_CONFIG_KHR, EGL_NO_CONTEXT, contextAttributes);
    check(context != EGL_NO_CONTEXT && eglMakeCurrent(display, EGL_NO_SURFACE, EGL_NO_SURFACE, context),
          "surfaceless EGL context unavailable");
    const auto destroy = reinterpret_cast<PFNEGLDESTROYIMAGEKHRPROC>(eglGetProcAddress("eglDestroyImageKHR"));
    const auto bindImage = reinterpret_cast<PFNGLEGLIMAGETARGETTEXTURE2DOESPROC>(eglGetProcAddress("glEGLImageTargetTexture2DOES"));
    check(destroy && bindImage, "EGL image entrypoints unavailable");
    const EGLImageKHR image = plank::platform::linux_backend::import_identity_dma_buf_image_v1(display, generic(frame));
    check(image != EGL_NO_IMAGE_KHR, "real leased surface EGL import failed");
    GLuint input = 0; glGenTextures(1, &input);
    glBindTexture(GL_TEXTURE_EXTERNAL_OES, input);
    glTexParameteri(GL_TEXTURE_EXTERNAL_OES, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
    glTexParameteri(GL_TEXTURE_EXTERNAL_OES, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
    glTexParameteri(GL_TEXTURE_EXTERNAL_OES, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_EXTERNAL_OES, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
    bindImage(GL_TEXTURE_EXTERNAL_OES, image);
    GLuint output = 0; glGenTextures(1, &output); glBindTexture(GL_TEXTURE_2D, output);
    glTexStorage2D(GL_TEXTURE_2D, 1, GL_RGB10_A2, frame.width, frame.height);
    GLuint framebuffer = 0; glGenFramebuffers(1, &framebuffer);
    glBindFramebuffer(GL_FRAMEBUFFER, framebuffer);
    glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, output, 0);
    check(glCheckFramebufferStatus(GL_FRAMEBUFFER) == GL_FRAMEBUFFER_COMPLETE, "ten-bit FBO incomplete");
    const GLuint vertex = compileShader(GL_VERTEX_SHADER,
        "#version 300 es\nprecision highp float; out vec2 uv; void main(){"
        "vec2 p=vec2((gl_VertexID<<1)&2,gl_VertexID&2);uv=p;gl_Position=vec4(p*2.0-1.0,0,1);}");
    const GLuint fragment = compileShader(GL_FRAGMENT_SHADER,
        "#version 300 es\n#extension GL_OES_EGL_image_external_essl3 : require\n"
        "precision highp float; uniform highp samplerExternalOES tex; in vec2 uv; out vec4 color;"
        "void main(){color=texture(tex,uv);}");
    const GLuint program = glCreateProgram(); glAttachShader(program, vertex); glAttachShader(program, fragment);
    glLinkProgram(program); GLint linked = 0; glGetProgramiv(program, GL_LINK_STATUS, &linked);
    check(linked, "probe shader link failed");
    glUseProgram(program); glUniform1i(glGetUniformLocation(program, "tex"), 0);
    glViewport(0, 0, frame.width, frame.height); glDisable(GL_DITHER);
    glDrawArrays(GL_TRIANGLES, 0, 3);
    glFinish(); // Explicit producer-lease release boundary, not scanout timing.
    std::vector<uint32_t> pixels(size_t(frame.width) * frame.height);
    glReadPixels(0, 0, frame.width, frame.height, GL_RGBA, GL_UNSIGNED_INT_2_10_10_10_REV, pixels.data());
    check(glGetError() == GL_NO_ERROR, "GPU image sampling/readback failed");
    unsigned minCode = 1023, maxCode = 0;
    for (auto pixel : pixels) {
        minCode = std::min(minCode, (pixel >> 10) & 1023U);
        maxCode = std::max(maxCode, (pixel >> 10) & 1023U);
    }
    check(maxCode > minCode, "hardware sample unexpectedly uniform");
    std::cout << "egl_hardware_lease_sampling=pass green_min=" << minCode << " green_max=" << maxCode << '\n';
    glDeleteProgram(program); glDeleteShader(vertex); glDeleteShader(fragment);
    glDeleteFramebuffers(1, &framebuffer); glDeleteTextures(1, &output); glDeleteTextures(1, &input);
    check(destroy(display, image), "EGL image destruction failed");
    eglMakeCurrent(display, EGL_NO_SURFACE, EGL_NO_SURFACE, EGL_NO_CONTEXT);
    eglDestroyContext(display, context); eglTerminate(display);
    gbm_device_destroy(gbm); close(fd);
}

void hardwareProof(const char* node) {
    AVBufferRef* device = nullptr;
    check(av_hwdevice_ctx_create(&device, AV_HWDEVICE_TYPE_VAAPI, node, nullptr, 0) == 0,
          "VA-API device unavailable");
    const AVCodec* codec = avcodec_find_decoder(AV_CODEC_ID_HEVC);
    AVCodecContext* decoder = avcodec_alloc_context3(codec);
    check(decoder, "decoder allocation failed");
    decoder->hw_device_ctx = av_buffer_ref(device);
    decoder->get_format = hardwareFormat;
    decoder->thread_count = 1;
    check(avcodec_open2(decoder, codec, nullptr) == 0, "hardware decoder open failed");
    const auto sample = plankFfmpegTestFrame(PlankFfmpegTestFrameKind::HevcRext10_444IdentityGbr);
    AVPacket* packet = av_packet_alloc();
    AVFrame* decoded = av_frame_alloc();
    check(packet && decoded && av_new_packet(packet, sample.size) == 0, "sample allocation failed");
    std::memcpy(packet->data, sample.data, sample.size);
    bool received = false;
    for (int attempt = 0; attempt < 5 && !received; ++attempt) {
        packet->pts = attempt + 1; packet->dts = packet->pts;
        check(avcodec_send_packet(decoder, packet) >= 0, "test packet submission failed");
        const int result = avcodec_receive_frame(decoder, decoded);
        check(result == 0 || result == AVERROR(EAGAIN), "test decode failed");
        received = result == 0;
    }
    check(received, "no hardware frame returned");
    PlankRetainedDecodedFrame lease;
    check(createPlank2VaapiFrameLease(decoded, profile, 1, 1000, lease) ==
          PLANK_BACKEND_OPERATION_OK_V1, "real hardware lease export failed");
    av_frame_free(&decoded);
    av_packet_free(&packet);
    avcodec_free_context(&decoder);
    av_buffer_unref(&device);
    auto view = createPlank2PresentationAvFrame(presentation(lease));
    check(view != nullptr, "hardware descriptor view failed after decoder close");
    const auto* descriptor = reinterpret_cast<const AVDRMFrameDescriptor*>(view->data[0]);
    std::cout << "vaapi_hardware_lease=pass width=" << lease.width << " height=" << lease.height
              << " planes=" << lease.planeCount << " objects=" << descriptor->nb_objects << '\n';
    for (int i = 0; i < descriptor->nb_objects; ++i) {
        check(fcntl(descriptor->objects[i].fd, F_GETFD) >= 0, "hardware export fd invalid");
        std::cout << "object=" << i << " modifier=" << std::hex
                  << descriptor->objects[i].format_modifier << std::dec << '\n';
    }
    eglProof(node, lease);
    view.reset();
    lease = {};
}
}

int main(int argc, char** argv) {
    fakeProof();
    if (argc == 3 && std::strcmp(argv[1], "--hardware") == 0) hardwareProof(argv[2]);
    else if (argc != 1) return 2;
    return 0;
}
