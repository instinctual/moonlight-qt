QT -= gui
CONFIG += console c++17 link_pkgconfig
CONFIG -= app_bundle
TEMPLATE = app
TARGET = plank2cursorpresenter

PLANK2_ROOT = $$(PLANK2_ROOT)
isEmpty(PLANK2_ROOT) {
    PLANK2_ROOT = $$clean_path($$PWD/../../../..)
}
!exists($$PLANK2_ROOT/platform/linux/presentation/src/cursor_presenter_sink_v1.hpp) {
    error("PLANK2 cursor presenter sink interface is missing: $$PLANK2_ROOT")
}

SOURCES += \
    test_plank2cursorpresenter.cpp \
    ../../app/streaming/input/plank2cursorpresentersink.cpp

HEADERS += \
    ../../app/streaming/input/plank2cursorpresentersink.h \
    ../../app/streaming/input/plankcursorpresentertarget.h

INCLUDEPATH += \
    ../../app \
    $$PLANK2_ROOT/core/backend/include \
    $$PLANK2_ROOT/core/display/include \
    $$PLANK2_ROOT/core/input/include \
    $$PLANK2_ROOT/core/session/include \
    $$PLANK2_ROOT/platform/linux/presentation/include \
    $$PLANK2_ROOT/platform/linux/presentation/src

PKGCONFIG += sdl3
