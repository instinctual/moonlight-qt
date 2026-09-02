QT -= gui
CONFIG += console c++17
CONFIG -= app_bundle
TEMPLATE = app
TARGET = plank2decoder

PLANK2_ROOT = $$(PLANK2_ROOT)
isEmpty(PLANK2_ROOT) {
    PLANK2_ROOT = $$clean_path($$PWD/../../../..)
}
!exists($$PLANK2_ROOT/platform/linux/media/src/decoder_source_v1.hpp) {
    error("PLANK2 decoder source interface is missing: $$PLANK2_ROOT")
}

SOURCES += \
    test_plank2decoder.cpp \
    ../../app/streaming/video/plank2decodersource.cpp

HEADERS += \
    ../../app/streaming/video/plank2decodersource.h

INCLUDEPATH += \
    ../../app \
    $$PLANK2_ROOT/core/backend/include \
    $$PLANK2_ROOT/core/display/include \
    $$PLANK2_ROOT/core/media/include \
    $$PLANK2_ROOT/core/session/include \
    $$PLANK2_ROOT/platform/linux/media/include \
    $$PLANK2_ROOT/platform/linux/media/src
