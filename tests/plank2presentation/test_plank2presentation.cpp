/* SPDX-License-Identifier: GPL-3.0-only */

#include "streaming/video/plank2presentationsource.h"

#include "presentation_source_v1.hpp"
#include "plank/display/presentation_interface_v1.h"
#include "plank/media/profile_v1.h"
#include "plank/platform/linux/presentation_backend_v1.h"

#include <cstdint>
#include <cstdlib>
#include <iostream>
#include <memory>
#include <string_view>
#include <vector>

namespace {
  using namespace plank::platform::linux_backend;

  [[noreturn]] void fail(std::string_view detail) {
    std::cerr << "plank2 presentation test failed: " << detail << '\n';
    std::exit(EXIT_FAILURE);
  }

  void require(bool condition, std::string_view detail) {
    if (!condition) fail(detail);
  }

  class fake_target_t final: public IPlankRetainedPresentationTarget {
  public:
    bool available() const override {
      return is_available;
    }

    bool qualifies(std::uint16_t profile_id, std::uint16_t pixel_layout,
                   std::uint16_t memory_kind) const override {
      return exact_profile == profile_id && exact_layout == pixel_layout &&
             memory_kind == PLANK_MEDIA_MEMORY_CPU_V1;
    }

    PlankBackendOperationResultV1 open(
        const PlankRetainedPresentationOpenRequest &request) override {
      ++open_calls;
      last_open = request;
      return accept_calls ? PLANK_BACKEND_OPERATION_OK_V1
                          : PLANK_BACKEND_OPERATION_UNAVAILABLE_V1;
    }

    PlankBackendOperationResultV1 present(
        const PlankRetainedPresentationFrame &frame,
        PlankRetainedPresentationCompletion &completion) override {
      ++present_calls;
      last_frame = frame;
      copied_plane.clear();
      if (frame.planeCount != 0U) {
        const auto &plane = frame.planes[0];
        const auto *data = reinterpret_cast<const std::uint8_t *>(
          plane.native_handle);
        copied_plane.assign(data + plane.offset_bytes,
                            data + plane.offset_bytes + plane.size_bytes);
      }
      if (!accept_calls) return PLANK_BACKEND_OPERATION_UNAVAILABLE_V1;
      completion = returned_completion;
      return PLANK_BACKEND_OPERATION_OK_V1;
    }

    PlankBackendOperationResultV1 reset() override {
      ++reset_calls;
      return accept_calls ? PLANK_BACKEND_OPERATION_OK_V1
                          : PLANK_BACKEND_OPERATION_UNAVAILABLE_V1;
    }

    void close() override {
      ++close_calls;
    }

    bool is_available {true};
    bool accept_calls {true};
    std::uint16_t exact_profile {
      PLANK_MEDIA_PROFILE_H264_HIGH10_444_SOFTWARE_NVFBC_V1};
    std::uint16_t exact_layout {PLANK_MEDIA_PIXEL_GBRP10_LE_V1};
    std::uint32_t open_calls {};
    std::uint32_t present_calls {};
    std::uint32_t reset_calls {};
    std::uint32_t close_calls {};
    PlankRetainedPresentationOpenRequest last_open;
    PlankRetainedPresentationFrame last_frame;
    std::vector<std::uint8_t> copied_plane;
    PlankRetainedPresentationCompletion returned_completion {
      PLANK_PRESENTATION_COMPLETION_PRESENTED_V1, 0U, UINT64_C(3000000000),
    };
  };

  PlankMediaProfileCapabilityV1 capability() {
    return {
      sizeof(PlankMediaProfileCapabilityV1), PLANK_MEDIA_INTERFACE_VERSION,
      PLANK_MEDIA_CAPABILITY_ROLE_PRESENTATION_V1,
      PLANK_MEDIA_PROFILE_H264_HIGH10_444_SOFTWARE_NVFBC_V1,
      PLANK_MEDIA_PIXEL_GBRP10_LE_V1, PLANK_MEDIA_MEMORY_CPU_BIT_V1,
      PLANK_MEDIA_CAPABILITY_REAL_FRAME_PROOF_V1,
    };
  }

  void test_composed_backend() {
    auto target = std::make_shared<fake_target_t>();
    auto source = create_retained_sdl_vulkan_presentation_source_v1(target);
    require(source != nullptr, "composed presentation source was not created");

    PlankLinuxPresentationBackendV1 *backend = nullptr;
    require(create_presentation_backend_for_source_v1(source, &backend) ==
              PLANK_LINUX_PRESENTATION_BACKEND_OK_V1 &&
              backend != nullptr,
            "generic presentation backend was not created");
    const auto *descriptor =
      plank_linux_presentation_backend_descriptor_v1(backend);
    require(descriptor != nullptr &&
              descriptor->kind == PLANK_BACKEND_KIND_PRESENTATION_V1,
            "generic presentation descriptor is invalid");
    const auto *interface_v1 =
      reinterpret_cast<const PlankPresentationInterfaceV1 *>(
        descriptor->interface_v1);
    require(plank_presentation_interface_validate_v1(interface_v1) ==
              PLANK_PRESENTATION_INTERFACE_OK_V1 &&
              interface_v1->capabilities.count == 9U,
            "generic presentation interface is invalid");

    PlankBackendProbeV1 probe {};
    require(interface_v1->probe.probe(interface_v1->probe.context, &probe) ==
              PLANK_BACKEND_PROBE_OK_V1 &&
              probe.availability == PLANK_BACKEND_AVAILABILITY_AVAILABLE_V1,
            "generic presentation probe failed");
    require(plank_media_capability_probe_v1(
              &interface_v1->capabilities, 0U, &probe) ==
                PLANK_MEDIA_CAPABILITY_OK_V1 &&
              probe.availability == PLANK_BACKEND_AVAILABILITY_AVAILABLE_V1,
            "exact composed presentation tuple did not qualify");
    require(plank_media_capability_probe_v1(
              &interface_v1->capabilities, 1U, &probe) ==
                PLANK_MEDIA_CAPABILITY_OK_V1 &&
              probe.availability == PLANK_BACKEND_AVAILABILITY_UNAVAILABLE_V1,
            "mismatched composed presentation tuple qualified");

    PlankPresentationTransformV1 transform {};
    require(plank_presentation_transform_create_v1(
              {0, 0, 64U, 32U}, 128U, 72U,
              PLANK_PRESENTATION_MODE_SCALED_SPAN_V1, &transform) ==
                PLANK_PRESENTATION_OK_V1,
            "composed presentation transform was not created");
    PlankPresentationOpenRequestV1 request {
      sizeof(PlankPresentationOpenRequestV1),
      PLANK_PRESENTATION_INTERFACE_VERSION,
      PLANK_MEDIA_PROFILE_H264_HIGH10_444_SOFTWARE_NVFBC_V1,
      PLANK_MEDIA_PIXEL_GBRP10_LE_V1, PLANK_MEDIA_MEMORY_CPU_V1, 0U,
      60000U, &transform, "composed-topology-1",
    };
    PlankBackendErrorV1 error {};
    void *session = nullptr;
    require(interface_v1->open(interface_v1->probe.context, &request, &session,
                               &error) == PLANK_BACKEND_OPERATION_OK_V1 &&
              session != nullptr,
            "composed presentation stream did not open");

    constexpr std::size_t plane_size = 64U * 32U * 2U;
    std::vector<std::uint8_t> planes(plane_size * 3U, 0x5aU);
    PlankMediaFrameLeaseV1 frame {
      sizeof(PlankMediaFrameLeaseV1), PLANK_MEDIA_INTERFACE_VERSION,
      PLANK_MEDIA_FRAME_STAGE_DECODED_V1, request.profile_id,
      request.pixel_layout, request.memory_kind, 3U, 64U, 32U, 17U,
      UINT64_C(1000000000), "composed-topology-1", 29U, {},
    };
    for (std::uint16_t index = 0U; index != frame.plane_count; ++index) {
      frame.planes[index] = {
        reinterpret_cast<std::uintptr_t>(
          planes.data() + plane_size * index),
        -1, 0U, 128U, plane_size, 0U,
      };
    }
    PlankPresentationSubmitRequestV1 submission {
      sizeof(PlankPresentationSubmitRequestV1),
      PLANK_PRESENTATION_INTERFACE_VERSION, 0U, &frame,
      UINT64_C(2000000000),
    };
    require(interface_v1->submit_frame(session, &submission, &error) ==
              PLANK_BACKEND_OPERATION_OK_V1,
            "composed presentation submission failed");
    require(target->present_calls == 1U &&
              target->last_frame.frameSequence == frame.frame_sequence &&
              target->last_frame.frameLeaseId == frame.lease_id &&
              target->last_frame.targetPresentTimestampNs ==
                submission.target_present_timestamp_ns &&
              target->last_frame.topologyGeneration ==
                frame.topology_generation &&
              target->copied_plane.size() == plane_size,
            "composed frame identity or synchronous access changed");
    require(interface_v1->submit_frame(session, &submission, &error) ==
              PLANK_BACKEND_OPERATION_AGAIN_V1,
            "composed presentation accepted a second active frame");

    PlankPresentationCompletionV1 completion {};
    require(interface_v1->next_completion(session, 0U, &completion, &error) ==
              PLANK_BACKEND_OPERATION_OK_V1 &&
              plank_presentation_completion_validate_v1(&completion) ==
                PLANK_PRESENTATION_INTERFACE_OK_V1,
            "composed presentation produced no valid completion");
    require(completion.state == PLANK_PRESENTATION_COMPLETION_PRESENTED_V1 &&
              completion.frame_sequence == frame.frame_sequence &&
              completion.frame_lease_id == frame.lease_id &&
              completion.target_present_timestamp_ns ==
                submission.target_present_timestamp_ns &&
              completion.actual_present_timestamp_ns == UINT64_C(3000000000),
            "composed completion identity changed");
    require(interface_v1->next_completion(
              session, 0U, &completion, &error) ==
                PLANK_BACKEND_OPERATION_AGAIN_V1,
            "composed completion was returned twice");

    frame.frame_sequence = 18U;
    frame.monotonic_timestamp_ns = UINT64_C(1100000000);
    frame.lease_id = 30U;
    target->returned_completion = {
      PLANK_PRESENTATION_COMPLETION_DROPPED_V1, 7U, 0U,
    };
    require(interface_v1->submit_frame(session, &submission, &error) ==
              PLANK_BACKEND_OPERATION_OK_V1 &&
              interface_v1->reset(session, &error) ==
                PLANK_BACKEND_OPERATION_OK_V1 &&
              target->reset_calls == 1U &&
              interface_v1->next_completion(
                session, 0U, &completion, &error) ==
                  PLANK_BACKEND_OPERATION_AGAIN_V1,
            "composed reset did not cancel the active completion");

    interface_v1->close(session);
    require(target->close_calls == 1U,
            "composed close did not reach the retained target");
    plank_linux_presentation_backend_destroy_v1(backend);
  }
}

int main() {
  auto target = std::make_shared<fake_target_t>();
  auto source = create_retained_sdl_vulkan_presentation_source_v1(target);
  require(source != nullptr, "factory returned no source");
  require(source->available(), "available target was not reported");

  auto exact_capability = capability();
  require(source->qualify(exact_capability) ==
              presentation_qualify_result_t::available,
          "exact proven tuple was not qualified");
  auto unsupported = exact_capability;
  unsupported.pixel_layout = PLANK_MEDIA_PIXEL_GBRP8_V1;
  require(source->qualify(unsupported) ==
              presentation_qualify_result_t::unsupported,
          "mismatched tuple was qualified");

  PlankPresentationTransformV1 transform {
    sizeof(PlankPresentationTransformV1), 1U,
    PLANK_PRESENTATION_MODE_SCALED_SPAN_V1,
    {0, 0, 3840, 2160}, {0, 0, 5120, 2160}, {640, 0, 3840, 2160},
  };
  PlankPresentationOpenRequestV1 open_request {
    sizeof(PlankPresentationOpenRequestV1),
    PLANK_PRESENTATION_INTERFACE_VERSION,
    exact_capability.profile_id, exact_capability.pixel_layout,
    PLANK_MEDIA_MEMORY_CPU_V1, 0U, 60000U, &transform, "topology-17",
  };
  std::unique_ptr<presentation_stream_t> stream;
  require(source->open(open_request, exact_capability, stream) ==
              PLANK_BACKEND_OPERATION_OK_V1,
          "exact presentation stream did not open");
  require(stream != nullptr && target->open_calls == 1U,
          "open was not forwarded exactly once");
  require(target->last_open.profileId == exact_capability.profile_id &&
              target->last_open.pixelLayout == exact_capability.pixel_layout &&
              target->last_open.memoryKind == PLANK_MEDIA_MEMORY_CPU_V1 &&
              target->last_open.refreshMilliHz == 60000U &&
              target->last_open.topologyGeneration == "topology-17" &&
              target->last_open.transform.content_rect.x == 640,
          "open tuple or immutable transform changed at the boundary");

  std::vector<std::uint8_t> plane_bytes {0, 1, 2, 3, 4, 5, 6, 7};
  presentation_submission_t submission {
    exact_capability.profile_id, exact_capability.pixel_layout,
    PLANK_MEDIA_MEMORY_CPU_V1, 1U, 3840U, 2160U, 29U,
    UINT64_C(1000000000), "topology-17", 41U,
    UINT64_C(2000000000), {},
  };
  submission.planes[0] = {
    reinterpret_cast<std::uintptr_t>(plane_bytes.data()), -1, 2U, 32U, 4U, 0U,
  };
  require(stream->submit(submission) == PLANK_BACKEND_OPERATION_OK_V1,
          "presentation submission failed");
  plane_bytes[2] = 0xff;
  require(target->present_calls == 1U &&
              target->last_frame.frameSequence == 29U &&
              target->last_frame.frameLeaseId == 41U &&
              target->last_frame.targetPresentTimestampNs ==
                UINT64_C(2000000000) &&
              target->last_frame.topologyGeneration == "topology-17",
          "frame identity changed at the retained boundary");
  require(target->copied_plane ==
              std::vector<std::uint8_t>({2, 3, 4, 5}),
          "target did not consume plane storage synchronously");

  require(stream->submit(submission) == PLANK_BACKEND_OPERATION_AGAIN_V1 &&
              target->present_calls == 1U,
          "a second submission bypassed the pending completion");
  presentation_completion_t completion;
  require(stream->next(0U, completion) == PLANK_BACKEND_OPERATION_OK_V1,
          "terminal completion was not returned");
  require(completion.state == PLANK_PRESENTATION_COMPLETION_PRESENTED_V1 &&
              completion.frame_sequence == 29U &&
              completion.frame_lease_id == 41U &&
              completion.target_present_timestamp_ns ==
                UINT64_C(2000000000) &&
              completion.actual_present_timestamp_ns ==
                UINT64_C(3000000000),
          "completion identity changed at the retained boundary");
  require(stream->next(0U, completion) == PLANK_BACKEND_OPERATION_AGAIN_V1,
          "completion was returned more than once");

  target->returned_completion = {
    PLANK_PRESENTATION_COMPLETION_DROPPED_V1, 7U, 0U,
  };
  submission.frame_sequence = 30U;
  submission.frame_lease_id = 42U;
  require(stream->submit(submission) == PLANK_BACKEND_OPERATION_OK_V1 &&
              stream->reset() == PLANK_BACKEND_OPERATION_OK_V1 &&
              target->reset_calls == 1U &&
              stream->next(0U, completion) == PLANK_BACKEND_OPERATION_AGAIN_V1,
          "reset did not synchronously discard the pending completion");

  target->returned_completion = {
    PLANK_PRESENTATION_COMPLETION_PRESENTED_V1, 0U, 0U,
  };
  submission.frame_sequence = 31U;
  submission.frame_lease_id = 43U;
  require(stream->submit(submission) == PLANK_BACKEND_OPERATION_FAILED_V1,
          "invalid presented completion was accepted");

  stream.reset();
  require(target->close_calls == 1U, "stream did not close the retained target");
  target.reset();
  require(!source->available(), "expired target remained available");
  require(source->qualify(exact_capability) ==
              presentation_qualify_result_t::unavailable,
          "expired target remained qualified");
  require(source->open(open_request, exact_capability, stream) ==
              PLANK_BACKEND_OPERATION_UNAVAILABLE_V1 && !stream,
          "expired target opened a stream");

  test_composed_backend();

  std::cout << "plank2_retained_presentation_source_test=pass\n";
  return EXIT_SUCCESS;
}
