#include "streaming/video/plank2presentationsource.h"

#include "presentation_source_v1.hpp"

#include <memory>
#include <optional>
#include <utility>

namespace plank::platform::linux_backend {
  namespace {
    class retained_presentation_stream_t final: public presentation_stream_t {
    public:
      retained_presentation_stream_t(
          std::weak_ptr<IPlankRetainedPresentationTarget> target)
          : target_(std::move(target)) {
      }

      ~retained_presentation_stream_t() override {
        if (const auto target = target_.lock()) target->close();
      }

      PlankBackendOperationResultV1 submit(
          const presentation_submission_t &submission) override {
        if (completion_) return PLANK_BACKEND_OPERATION_AGAIN_V1;
        const auto target = target_.lock();
        if (!target || !target->available()) {
          return PLANK_BACKEND_OPERATION_UNAVAILABLE_V1;
        }
        PlankRetainedPresentationFrame frame {
          submission.profile_id, submission.pixel_layout,
          submission.memory_kind, submission.plane_count,
          submission.width, submission.height, submission.frame_sequence,
          submission.frame_timestamp_ns, submission.frame_lease_id,
          submission.target_present_timestamp_ns,
          submission.topology_generation, submission.planes,
        };
        PlankRetainedPresentationCompletion retained;
        const auto result = target->present(frame, retained);
        if (result != PLANK_BACKEND_OPERATION_OK_V1) return result;
        if (retained.state < PLANK_PRESENTATION_COMPLETION_PRESENTED_V1 ||
            retained.state > PLANK_PRESENTATION_COMPLETION_FAILED_V1 ||
            (retained.state == PLANK_PRESENTATION_COMPLETION_PRESENTED_V1) !=
              (retained.actualPresentTimestampNs != 0U)) {
          return PLANK_BACKEND_OPERATION_FAILED_V1;
        }
        completion_ = presentation_completion_t {
          retained.state, retained.reasonCode, submission.frame_sequence,
          submission.frame_lease_id, submission.target_present_timestamp_ns,
          retained.actualPresentTimestampNs,
        };
        return PLANK_BACKEND_OPERATION_OK_V1;
      }

      PlankBackendOperationResultV1 next(
          std::uint32_t, presentation_completion_t &completion) override {
        if (!completion_) return PLANK_BACKEND_OPERATION_AGAIN_V1;
        completion = *completion_;
        completion_.reset();
        return PLANK_BACKEND_OPERATION_OK_V1;
      }

      PlankBackendOperationResultV1 reset() override {
        const auto target = target_.lock();
        if (!target) return PLANK_BACKEND_OPERATION_UNAVAILABLE_V1;
        const auto result = target->reset();
        if (result == PLANK_BACKEND_OPERATION_OK_V1) completion_.reset();
        return result;
      }

    private:
      std::weak_ptr<IPlankRetainedPresentationTarget> target_;
      std::optional<presentation_completion_t> completion_;
    };

    class retained_presentation_source_t final: public presentation_source_t {
    public:
      explicit retained_presentation_source_t(
          std::weak_ptr<IPlankRetainedPresentationTarget> target)
          : target_(std::move(target)) {
      }

      bool available() override {
        const auto target = target_.lock();
        return target && target->available();
      }

      presentation_qualify_result_t qualify(
          const PlankMediaProfileCapabilityV1 &capability) override {
        const auto target = target_.lock();
        if (!target || !target->available()) {
          return presentation_qualify_result_t::unavailable;
        }
        return target->qualifies(
                 capability.profile_id, capability.pixel_layout,
                 PLANK_MEDIA_MEMORY_CPU_V1)
          ? presentation_qualify_result_t::available
          : presentation_qualify_result_t::unsupported;
      }

      PlankBackendOperationResultV1 open(
          const PlankPresentationOpenRequestV1 &request,
          const PlankMediaProfileCapabilityV1 &capability,
          std::unique_ptr<presentation_stream_t> &stream) override {
        stream.reset();
        const auto target = target_.lock();
        if (!target || !target->available()) {
          return PLANK_BACKEND_OPERATION_UNAVAILABLE_V1;
        }
        if (!target->qualifies(capability.profile_id, capability.pixel_layout,
                               request.memory_kind)) {
          return PLANK_BACKEND_OPERATION_UNSUPPORTED_V1;
        }
        PlankRetainedPresentationOpenRequest retained {
          request.profile_id, request.pixel_layout, request.memory_kind,
          request.refresh_millihz, *request.transform,
          request.topology_generation,
        };
        const auto result = target->open(retained);
        if (result != PLANK_BACKEND_OPERATION_OK_V1) return result;
        try {
          stream = std::make_unique<retained_presentation_stream_t>(target_);
        } catch (...) {
          target->close();
          throw;
        }
        return PLANK_BACKEND_OPERATION_OK_V1;
      }

    private:
      std::weak_ptr<IPlankRetainedPresentationTarget> target_;
    };
  }

  std::shared_ptr<presentation_source_t>
  create_retained_sdl_vulkan_presentation_source_v1(
      std::weak_ptr<IPlankRetainedPresentationTarget> target) {
    return std::make_shared<retained_presentation_source_t>(
      std::move(target));
  }
}
