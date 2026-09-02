#pragma once

#include <memory>

class PlankCursorPresenterTarget;

namespace plank::platform::linux_backend {
  class cursor_presenter_sink_t;

  std::shared_ptr<cursor_presenter_sink_t>
  create_retained_sdl_cursor_presenter_sink_v1(
    std::weak_ptr<PlankCursorPresenterTarget> target
  );
}

