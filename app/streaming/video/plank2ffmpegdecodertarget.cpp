/* SPDX-License-Identifier: GPL-3.0-only */

#include "streaming/video/plank2ffmpegdecodertarget.h"

#include "streaming/video/ffmpegtestframes.h"
#include "plank/media/profile_v1.h"

extern "C" {
#include <libavcodec/avcodec.h>
#include <libavutil/pixdesc.h>
}

#include <algorithm>
#include <cerrno>
#include <climits>
#include <cstdint>
#include <cstring>
#include <deque>
#include <memory>
#include <new>
#include <utility>
#include <vector>

namespace {
  struct profile_spec_t {
    std::uint16_t profile_id;
    std::uint16_t pixel_layout;
    AVCodecID codec_id;
    AVPixelFormat pixel_format;
    PlankFfmpegTestFrameKind test_frame;
    bool identity_gbr;
  };

  bool profile_spec(std::uint16_t profile_id, profile_spec_t &spec) {
    switch (profile_id) {
      case PLANK_MEDIA_PROFILE_H264_HIGH10_444_SOFTWARE_NVFBC_V1:
      case PLANK_MEDIA_PROFILE_H264_HIGH10_444_SOFTWARE_X11_V1:
        spec = {profile_id, PLANK_MEDIA_PIXEL_GBRP10_LE_V1,
                AV_CODEC_ID_H264, AV_PIX_FMT_GBRP10LE,
                PlankFfmpegTestFrameKind::H264High10_444, true};
        return true;
      case PLANK_MEDIA_PROFILE_H264_HIGH8_422_SOFTWARE_NVFBC_V1:
        spec = {profile_id, PLANK_MEDIA_PIXEL_YUV422P8_V1,
                AV_CODEC_ID_H264, AV_PIX_FMT_YUV422P,
                PlankFfmpegTestFrameKind::H264High8_422, false};
        return true;
      case PLANK_MEDIA_PROFILE_H264_HIGH8_444_SOFTWARE_NVFBC_V1:
      case PLANK_MEDIA_PROFILE_H264_HIGH8_444_NVENC_NVFBC_V1:
        spec = {profile_id, PLANK_MEDIA_PIXEL_GBRP8_V1,
                AV_CODEC_ID_H264, AV_PIX_FMT_GBRP,
                PlankFfmpegTestFrameKind::H264High8_444, true};
        return true;
      case PLANK_MEDIA_PROFILE_H264_HIGH10_422_SOFTWARE_NVFBC_V1:
        spec = {profile_id, PLANK_MEDIA_PIXEL_YUV422P10_LE_V1,
                AV_CODEC_ID_H264, AV_PIX_FMT_YUV422P10LE,
                PlankFfmpegTestFrameKind::H264High10_422, false};
        return true;
      case PLANK_MEDIA_PROFILE_HEVC_REXT8_444_NVENC_NVFBC_V1:
        spec = {profile_id, PLANK_MEDIA_PIXEL_GBRP8_V1,
                AV_CODEC_ID_HEVC, AV_PIX_FMT_GBRP,
                PlankFfmpegTestFrameKind::HevcRext8_444IdentityGbr, true};
        return true;
      case PLANK_MEDIA_PROFILE_HEVC_REXT10_444_NVENC_NVFBC_V1:
      case PLANK_MEDIA_PROFILE_HEVC_REXT10_444_NVENC_X11_V1:
        spec = {profile_id, PLANK_MEDIA_PIXEL_GBRP10_LE_V1,
                AV_CODEC_ID_HEVC, AV_PIX_FMT_GBRP10LE,
                PlankFfmpegTestFrameKind::HevcRext10_444IdentityGbr, true};
        return true;
      default:
        return false;
    }
  }

  bool frame_matches(const AVFrame *frame, const profile_spec_t &spec,
                     std::uint32_t width = 0U, std::uint32_t height = 0U) {
    const bool matches = frame != nullptr &&
      frame->format == spec.pixel_format &&
      (width == 0U || frame->width == static_cast<int>(width)) &&
      (height == 0U || frame->height == static_cast<int>(height)) &&
      frame->color_range == AVCOL_RANGE_JPEG &&
      (spec.identity_gbr ? frame->colorspace == AVCOL_SPC_RGB
                         : frame->colorspace == AVCOL_SPC_BT709);
    if (!matches) {
      av_log(nullptr, AV_LOG_WARNING,
             "PLANK2 exact decoder frame mismatch: profile=%u "
             "format=%s expected=%s matrix=%d expected-matrix=%d "
             "range=%d expected-range=%d size=%dx%d expected=%ux%u\n",
             spec.profile_id,
             frame != nullptr && av_get_pix_fmt_name(
               static_cast<AVPixelFormat>(frame->format)) != nullptr
               ? av_get_pix_fmt_name(static_cast<AVPixelFormat>(frame->format))
               : "unavailable",
             av_get_pix_fmt_name(spec.pixel_format) != nullptr
               ? av_get_pix_fmt_name(spec.pixel_format) : "unavailable",
             frame != nullptr ? frame->colorspace : AVCOL_SPC_UNSPECIFIED,
             spec.identity_gbr ? AVCOL_SPC_RGB : AVCOL_SPC_BT709,
             frame != nullptr ? frame->color_range : AVCOL_RANGE_UNSPECIFIED,
             AVCOL_RANGE_JPEG,
             frame != nullptr ? frame->width : 0,
             frame != nullptr ? frame->height : 0, width, height);
    }
    return matches;
  }

  void configure_context(AVCodecContext *context,
                         const profile_spec_t &spec,
                         std::uint32_t width, std::uint32_t height) {
    context->flags |= AV_CODEC_FLAG_LOW_DELAY | AV_CODEC_FLAG_OUTPUT_CORRUPT;
    context->flags2 |= AV_CODEC_FLAG2_SHOW_ALL;
    context->err_recognition = AV_EF_EXPLODE;
    context->thread_type = FF_THREAD_SLICE;
    context->thread_count = 4;
    context->width = static_cast<int>(width);
    context->height = static_cast<int>(height);
    context->pix_fmt = spec.pixel_format;
    context->pkt_timebase = {1, 1000000000};
  }

  AVCodecContext *open_context(const profile_spec_t &spec,
                               std::uint32_t width,
                               std::uint32_t height) {
    const AVCodec *decoder = avcodec_find_decoder(spec.codec_id);
    if (decoder == nullptr) return nullptr;
    AVCodecContext *context = avcodec_alloc_context3(decoder);
    if (context == nullptr) return nullptr;
    configure_context(context, spec, width, height);
    if (avcodec_open2(context, decoder, nullptr) < 0) {
      avcodec_free_context(&context);
      return nullptr;
    }
    return context;
  }

  bool send_owned_packet(AVCodecContext *context, const std::uint8_t *data,
                         std::size_t size, std::uint64_t pts_ns,
                         std::uint64_t dts_ns, int &result) {
    if (context == nullptr || data == nullptr || size == 0U ||
        size > static_cast<std::size_t>(INT_MAX)) {
      result = AVERROR(EINVAL);
      return false;
    }
    AVPacket *packet = av_packet_alloc();
    if (packet == nullptr) {
      result = AVERROR(ENOMEM);
      return false;
    }
    result = av_new_packet(packet, static_cast<int>(size));
    if (result >= 0) {
      std::memcpy(packet->data, data, size);
      packet->pts = static_cast<std::int64_t>(pts_ns);
      packet->dts = static_cast<std::int64_t>(dts_ns);
      result = avcodec_send_packet(context, packet);
    }
    av_packet_free(&packet);
    return result >= 0;
  }

  bool qualify_profile(const profile_spec_t &spec) {
    const auto sample = plankFfmpegTestFrame(spec.test_frame);
    if (sample.data == nullptr || sample.size == 0U) return false;

    AVCodecContext *context = open_context(spec, 1280U, 720U);
    if (context == nullptr) return false;
    AVFrame *frame = av_frame_alloc();
    if (frame == nullptr) {
      avcodec_free_context(&context);
      return false;
    }

    bool qualified = false;
    for (int attempt = 0; attempt != 5 && !qualified; ++attempt) {
      int result = 0;
      if (!send_owned_packet(context, sample.data, sample.size, 1U, 1U,
                             result)) {
        if (result != AVERROR(EAGAIN)) break;
      }
      result = avcodec_receive_frame(context, frame);
      if (result == 0) {
        qualified = frame_matches(frame, spec);
      } else if (result != AVERROR(EAGAIN)) {
        break;
      }
      av_frame_unref(frame);
    }

    av_frame_free(&frame);
    avcodec_free_context(&context);
    return qualified;
  }

  struct submitted_frame_t {
    std::uint64_t frame_sequence;
    std::uint64_t presentation_timestamp_ns;
  };

  class ffmpeg_software_decoder_target_t final:
      public IPlankRetainedDecoderTarget {
  public:
    ~ffmpeg_software_decoder_target_t() override {
      close();
    }

    bool available() const override {
      return avcodec_find_decoder(AV_CODEC_ID_H264) != nullptr &&
             avcodec_find_decoder(AV_CODEC_ID_HEVC) != nullptr;
    }

    bool qualifies(std::uint16_t profile_id, std::uint16_t pixel_layout,
                   std::uint16_t memory_kind) const override {
      profile_spec_t spec {};
      return memory_kind == PLANK_MEDIA_MEMORY_CPU_V1 &&
             profile_spec(profile_id, spec) &&
             spec.pixel_layout == pixel_layout && qualify_profile(spec);
    }

    PlankBackendOperationResultV1 open(
        const PlankRetainedDecoderOpenRequest &request) override {
      if (context_ != nullptr) return PLANK_BACKEND_OPERATION_UNAVAILABLE_V1;
      profile_spec_t spec {};
      if (request.memoryKind != PLANK_MEDIA_MEMORY_CPU_V1 ||
          request.width == 0U || request.height == 0U ||
          request.refreshMilliHz == 0U || request.topologyGeneration.empty() ||
          !profile_spec(request.profileId, spec) ||
          spec.pixel_layout != request.pixelLayout) {
        return PLANK_BACKEND_OPERATION_INVALID_ARGUMENT_V1;
      }
      context_ = open_context(spec, request.width, request.height);
      if (context_ == nullptr) return PLANK_BACKEND_OPERATION_UNAVAILABLE_V1;
      spec_ = spec;
      width_ = request.width;
      height_ = request.height;
      return PLANK_BACKEND_OPERATION_OK_V1;
    }

    PlankBackendOperationResultV1 submit(
        const PlankRetainedDecoderPacket &packet) override {
      if (context_ == nullptr) return PLANK_BACKEND_OPERATION_UNAVAILABLE_V1;
      if (packet.profileId != spec_.profile_id || packet.frameSequence == 0U ||
          packet.presentationTimestampNs == 0U ||
          packet.decodeTimestampNs == 0U ||
          packet.decodeTimestampNs > packet.presentationTimestampNs ||
          packet.data == nullptr || packet.size == 0U ||
          packet.size > static_cast<std::size_t>(INT_MAX) ||
          (!assembly_.empty() && packet.frameSequence != assembly_sequence_) ||
          (assembly_complete_ &&
           (packet.flags & PLANK_MEDIA_PACKET_END_OF_FRAME_V1) == 0U)) {
        return PLANK_BACKEND_OPERATION_INVALID_ARGUMENT_V1;
      }

      if (assembly_.empty()) {
        assembly_sequence_ = packet.frameSequence;
        assembly_pts_ns_ = packet.presentationTimestampNs;
        assembly_dts_ns_ = packet.decodeTimestampNs;
      } else if (packet.presentationTimestampNs != assembly_pts_ns_ ||
                 packet.decodeTimestampNs != assembly_dts_ns_) {
        return PLANK_BACKEND_OPERATION_INVALID_ARGUMENT_V1;
      }
      if (!assembly_complete_) {
        if (assembly_.size() > static_cast<std::size_t>(INT_MAX) - packet.size) {
          return PLANK_BACKEND_OPERATION_INVALID_ARGUMENT_V1;
        }
        try {
          assembly_.insert(assembly_.end(), packet.data,
                           packet.data + packet.size);
        } catch (const std::bad_alloc &) {
          return PLANK_BACKEND_OPERATION_FAILED_V1;
        }
        if ((packet.flags & PLANK_MEDIA_PACKET_END_OF_FRAME_V1) == 0U) {
          return PLANK_BACKEND_OPERATION_OK_V1;
        }
        assembly_complete_ = true;
      }
      int result = 0;
      if (!send_owned_packet(context_, assembly_.data(), assembly_.size(),
                             assembly_pts_ns_, assembly_dts_ns_, result)) {
        return result == AVERROR(EAGAIN) ? PLANK_BACKEND_OPERATION_AGAIN_V1
                                        : PLANK_BACKEND_OPERATION_FAILED_V1;
      }
      try {
        submitted_.push_back({assembly_sequence_, assembly_pts_ns_});
      } catch (const std::bad_alloc &) {
        avcodec_flush_buffers(context_);
        clear_queues();
        return PLANK_BACKEND_OPERATION_FAILED_V1;
      }
      assembly_.clear();
      assembly_sequence_ = 0U;
      assembly_pts_ns_ = 0U;
      assembly_dts_ns_ = 0U;
      assembly_complete_ = false;
      return PLANK_BACKEND_OPERATION_OK_V1;
    }

    PlankBackendOperationResultV1 next(
        std::uint32_t, PlankRetainedDecodedFrame &decoded) override {
      decoded = {};
      if (context_ == nullptr) return PLANK_BACKEND_OPERATION_UNAVAILABLE_V1;
      AVFrame *raw_frame = av_frame_alloc();
      if (raw_frame == nullptr) return PLANK_BACKEND_OPERATION_FAILED_V1;
      const int result = avcodec_receive_frame(context_, raw_frame);
      if (result == AVERROR(EAGAIN)) {
        av_frame_free(&raw_frame);
        return PLANK_BACKEND_OPERATION_AGAIN_V1;
      }
      if (result == AVERROR_EOF) {
        av_frame_free(&raw_frame);
        return PLANK_BACKEND_OPERATION_END_OF_STREAM_V1;
      }
      if (result < 0 || submitted_.empty() ||
          !frame_matches(raw_frame, spec_, width_, height_)) {
        av_frame_free(&raw_frame);
        return PLANK_BACKEND_OPERATION_FAILED_V1;
      }

      std::shared_ptr<AVFrame> owner(raw_frame, [](AVFrame *frame) {
        av_frame_free(&frame);
      });
      const auto metadata = submitted_.front();
      submitted_.pop_front();
      decoded.profileId = spec_.profile_id;
      decoded.pixelLayout = spec_.pixel_layout;
      decoded.memoryKind = PLANK_MEDIA_MEMORY_CPU_V1;
      decoded.planeCount = 3U;
      decoded.width = width_;
      decoded.height = height_;
      decoded.frameSequence = metadata.frame_sequence;
      decoded.monotonicTimestampNs = metadata.presentation_timestamp_ns;
      for (std::uint16_t index = 0U; index != decoded.planeCount; ++index) {
        if (raw_frame->data[index] == nullptr ||
            raw_frame->linesize[index] <= 0) {
          return PLANK_BACKEND_OPERATION_FAILED_V1;
        }
        decoded.planes[index] = {
          reinterpret_cast<std::uintptr_t>(raw_frame->data[index]), -1, 0U,
          static_cast<std::uint32_t>(raw_frame->linesize[index]),
          static_cast<std::uint64_t>(raw_frame->linesize[index]) * height_, 0U,
        };
      }
      decoded.owner = std::move(owner);
      return PLANK_BACKEND_OPERATION_OK_V1;
    }

    PlankBackendOperationResultV1 reset(
        std::uint64_t recovery_epoch) override {
      if (context_ == nullptr) return PLANK_BACKEND_OPERATION_UNAVAILABLE_V1;
      if (recovery_epoch == 0U || recovery_epoch <= recovery_epoch_) {
        return PLANK_BACKEND_OPERATION_INVALID_ARGUMENT_V1;
      }
      avcodec_flush_buffers(context_);
      clear_queues();
      recovery_epoch_ = recovery_epoch;
      return PLANK_BACKEND_OPERATION_OK_V1;
    }

    PlankBackendOperationResultV1 flush() override {
      if (context_ == nullptr) return PLANK_BACKEND_OPERATION_UNAVAILABLE_V1;
      if (!assembly_.empty()) return PLANK_BACKEND_OPERATION_INVALID_ARGUMENT_V1;
      const int result = avcodec_send_packet(context_, nullptr);
      if (result == AVERROR(EAGAIN)) return PLANK_BACKEND_OPERATION_AGAIN_V1;
      return result < 0 && result != AVERROR_EOF
        ? PLANK_BACKEND_OPERATION_FAILED_V1
        : PLANK_BACKEND_OPERATION_OK_V1;
    }

    void close() override {
      if (context_ != nullptr) avcodec_free_context(&context_);
      clear_queues();
      spec_ = {};
      width_ = 0U;
      height_ = 0U;
      recovery_epoch_ = 0U;
    }

  private:
    void clear_queues() {
      assembly_.clear();
      submitted_.clear();
      assembly_sequence_ = 0U;
      assembly_pts_ns_ = 0U;
      assembly_dts_ns_ = 0U;
      assembly_complete_ = false;
    }

    AVCodecContext *context_ {};
    profile_spec_t spec_ {};
    std::uint32_t width_ {};
    std::uint32_t height_ {};
    std::uint64_t recovery_epoch_ {};
    std::vector<std::uint8_t> assembly_;
    std::uint64_t assembly_sequence_ {};
    std::uint64_t assembly_pts_ns_ {};
    std::uint64_t assembly_dts_ns_ {};
    bool assembly_complete_ {};
    std::deque<submitted_frame_t> submitted_;
  };
}

std::shared_ptr<IPlankRetainedDecoderTarget>
createPlank2FfmpegSoftwareDecoderTarget()
{
  return std::make_shared<ffmpeg_software_decoder_target_t>();
}
