#include "streaming/input/plank2cursorpresentersink.h"

#include "cursor_presenter_sink_v1.hpp"
#include "streaming/input/plankcursorpresentertarget.h"

#include <SDL3/SDL.h>

#include <memory>
#include <utility>

namespace plank::platform::linux_backend {
  static_assert(
    static_cast<std::uint16_t>(PlankCursorPresenterOwner::LocalMouse) ==
      PLANK_CURSOR_OWNER_LOCAL_MOUSE_V1);
  static_assert(
    static_cast<std::uint16_t>(PlankCursorPresenterOwner::HostTablet) ==
      PLANK_CURSOR_OWNER_HOST_TABLET_V1);
  static_assert(
    static_cast<std::uint16_t>(PlankCursorPresenterOwner::LocalUi) ==
      PLANK_CURSOR_OWNER_LOCAL_UI_V1);

  namespace {
    class retained_cursor_presenter_stream_t final:
        public cursor_presenter_stream_t {
    public:
      explicit retained_cursor_presenter_stream_t(
          std::weak_ptr<PlankCursorPresenterTarget> target,
          std::uint32_t client_width, std::uint32_t client_height)
          : target_(std::move(target)),
            client_width_(client_width), client_height_(client_height) {
      }

      PlankBackendOperationResultV1 present_image(
          const presented_cursor_image_t &image) override {
        if (!SDL_IsMainThread()) return PLANK_BACKEND_OPERATION_AGAIN_V1;
        const auto target = target_.lock();
        if (!target) return PLANK_BACKEND_OPERATION_UNAVAILABLE_V1;
        PlankCursorPresenterImage retained {
          image.generation, image.width, image.height,
          image.hotspot_x, image.hotspot_y, image.visible, image.pixels,
        };
        return target->presentImage(retained)
          ? PLANK_BACKEND_OPERATION_OK_V1
          : PLANK_BACKEND_OPERATION_UNAVAILABLE_V1;
      }

      PlankBackendOperationResultV1 present_position(
          const presented_cursor_position_t &position) override {
        if (!SDL_IsMainThread()) return PLANK_BACKEND_OPERATION_AGAIN_V1;
        const auto target = target_.lock();
        if (!target) return PLANK_BACKEND_OPERATION_UNAVAILABLE_V1;
        const PlankCursorPresenterPosition retained {
          static_cast<PlankCursorPresenterOwner>(position.owner),
          position.sequence,
          position.client_x, position.client_y,
          client_width_, client_height_,
        };
        return target->presentPosition(retained)
          ? PLANK_BACKEND_OPERATION_OK_V1
          : PLANK_BACKEND_OPERATION_UNAVAILABLE_V1;
      }

      PlankBackendOperationResultV1 reset() override {
        if (!SDL_IsMainThread()) return PLANK_BACKEND_OPERATION_AGAIN_V1;
        const auto target = target_.lock();
        if (!target) return PLANK_BACKEND_OPERATION_UNAVAILABLE_V1;
        return target->reset()
          ? PLANK_BACKEND_OPERATION_OK_V1
          : PLANK_BACKEND_OPERATION_UNAVAILABLE_V1;
      }

    private:
      std::weak_ptr<PlankCursorPresenterTarget> target_;
      std::uint32_t client_width_;
      std::uint32_t client_height_;
    };

    class retained_cursor_presenter_sink_t final:
        public cursor_presenter_sink_t {
    public:
      explicit retained_cursor_presenter_sink_t(
          std::weak_ptr<PlankCursorPresenterTarget> target)
          : target_(std::move(target)) {
      }

      bool available() override {
        const auto target = target_.lock();
        return target && target->available();
      }

      PlankBackendOperationResultV1 open(
          const PlankDisplayTopologyV1 &,
          const PlankPresentationTransformV1 &transform,
          std::unique_ptr<cursor_presenter_stream_t> &stream) override {
        stream.reset();
        if (!SDL_IsMainThread()) return PLANK_BACKEND_OPERATION_AGAIN_V1;
        const auto target = target_.lock();
        if (!target || !target->available()) {
          return PLANK_BACKEND_OPERATION_UNAVAILABLE_V1;
        }
        stream = std::make_unique<retained_cursor_presenter_stream_t>(
          target_, transform.client_rect.width, transform.client_rect.height);
        return PLANK_BACKEND_OPERATION_OK_V1;
      }

    private:
      std::weak_ptr<PlankCursorPresenterTarget> target_;
    };
  }

  std::shared_ptr<cursor_presenter_sink_t>
  create_retained_sdl_cursor_presenter_sink_v1(
      std::weak_ptr<PlankCursorPresenterTarget> target) {
    return std::make_shared<retained_cursor_presenter_sink_t>(
      std::move(target));
  }
}
