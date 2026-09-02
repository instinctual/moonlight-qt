/* SPDX-License-Identifier: GPL-3.0-only */

#include "streaming/input/plank2cursorpresentersink.h"

#include "cursor_presenter_sink_v1.hpp"
#include "streaming/input/plankcursorpresentertarget.h"

#include <SDL3/SDL.h>

#include <cstdint>
#include <cstdlib>
#include <iostream>
#include <memory>
#include <string_view>
#include <thread>
#include <vector>

namespace {
  using namespace plank::platform::linux_backend;

  [[noreturn]] void fail(std::string_view detail) {
    std::cerr << "plank2 cursor presenter test failed: " << detail << '\n';
    std::exit(EXIT_FAILURE);
  }

  void require(bool condition, std::string_view detail) {
    if (!condition) fail(detail);
  }

  class fake_target_t final: public IPlankCursorPresenterTarget {
  public:
    bool available() const override {
      return is_available;
    }

    bool presentImage(const PlankCursorPresenterImage &image) override {
      ++image_calls;
      last_image = image;
      return accept_updates;
    }

    bool presentPosition(
        const PlankCursorPresenterPosition &position) override {
      ++position_calls;
      last_position = position;
      return accept_updates;
    }

    bool reset() override {
      ++reset_calls;
      return accept_updates;
    }

    bool is_available {true};
    bool accept_updates {true};
    std::uint32_t image_calls {};
    std::uint32_t position_calls {};
    std::uint32_t reset_calls {};
    PlankCursorPresenterImage last_image;
    PlankCursorPresenterPosition last_position;
  };
}

int main() {
  require(SDL_Init(0), SDL_GetError());

  auto target = std::make_shared<fake_target_t>();
  auto sink = create_retained_sdl_cursor_presenter_sink_v1(target);
  require(sink != nullptr, "factory returned no sink");
  require(sink->available(), "available target was not reported");

  PlankDisplayTopologyV1 topology {};
  PlankPresentationTransformV1 transform {};
  transform.client_rect = {0, 0, 5120, 2160};
  std::unique_ptr<cursor_presenter_stream_t> stream;
  require(sink->open(topology, transform, stream) ==
              PLANK_BACKEND_OPERATION_OK_V1,
          "main-thread open failed");
  require(stream != nullptr, "open returned no stream");

  presented_cursor_image_t image {
    17, 2, 2, 1, 0, true,
    {0x10, 0x20, 0x30, 0x40, 0x50, 0x60, 0x70, 0x80,
     0x90, 0xa0, 0xb0, 0xc0, 0xd0, 0xe0, 0xf0, 0xff},
  };
  require(stream->present_image(image) == PLANK_BACKEND_OPERATION_OK_V1,
          "image presentation failed");
  image.pixels[0] = 0;
  require(target->image_calls == 1, "image was not presented exactly once");
  require(target->last_image.generation == 17 &&
              target->last_image.width == 2 &&
              target->last_image.height == 2 &&
              target->last_image.hotspotX == 1 &&
              target->last_image.hotspotY == 0 &&
              target->last_image.visible,
          "image metadata changed at the retained boundary");
  require(target->last_image.pixels.size() == 16 &&
              target->last_image.pixels[0] == 0x10 &&
              target->last_image.pixels[15] == 0xff,
          "image storage was not copied before returning");

  const presented_cursor_position_t position {
    PLANK_CURSOR_OWNER_HOST_TABLET_V1, 29, 3839, 1079, 4479, 1079,
  };
  require(stream->present_position(position) ==
              PLANK_BACKEND_OPERATION_OK_V1,
          "position presentation failed");
  require(target->position_calls == 1,
          "position was not presented exactly once");
  require(target->last_position.owner ==
              PlankCursorPresenterOwner::HostTablet &&
              target->last_position.sequence == 29 &&
              target->last_position.clientX == 4479 &&
              target->last_position.clientY == 1079 &&
              target->last_position.clientWidth == 5120 &&
              target->last_position.clientHeight == 2160,
          "mapped position or client canvas changed at the retained boundary");

  require(stream->reset() == PLANK_BACKEND_OPERATION_OK_V1,
          "reset failed");
  require(target->reset_calls == 1, "reset was not forwarded exactly once");

  PlankBackendOperationResultV1 threaded_result =
      PLANK_BACKEND_OPERATION_FAILED_V1;
  std::thread worker([&] {
    threaded_result = stream->present_position(position);
  });
  worker.join();
  require(threaded_result == PLANK_BACKEND_OPERATION_AGAIN_V1,
          "off-main presentation was not deferred");
  require(target->position_calls == 1,
          "off-main presentation reached the retained target");

  target->accept_updates = false;
  require(stream->reset() == PLANK_BACKEND_OPERATION_UNAVAILABLE_V1,
          "target rejection was not surfaced");
  target->accept_updates = true;

  target.reset();
  require(!sink->available(), "expired target remained available");
  require(stream->present_position(position) ==
              PLANK_BACKEND_OPERATION_UNAVAILABLE_V1,
          "stream called an expired target");
  stream.reset();
  require(sink->open(topology, transform, stream) ==
              PLANK_BACKEND_OPERATION_UNAVAILABLE_V1 && !stream,
          "expired target opened a stream");

  SDL_Quit();
  std::cout << "plank2_cursor_presenter_sink_test=pass\n";
  return EXIT_SUCCESS;
}
