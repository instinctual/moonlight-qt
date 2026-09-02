QT -= gui
CONFIG += console c++17 link_pkgconfig
CONFIG -= app_bundle
TEMPLATE = app
TARGET = plank2ffmpegdecoder

PLANK2_ROOT = $$(PLANK2_ROOT)
isEmpty(PLANK2_ROOT) {
    PLANK2_ROOT = $$clean_path($$PWD/../../../..)
}
!exists($$PLANK2_ROOT/core/media/include/plank/media/profile_v1.h) {
    error("PLANK2 media profile contract is missing: $$PLANK2_ROOT")
}

PKGCONFIG += libavcodec libavutil

SOURCES += \
    test_plank2ffmpegdecoder.cpp \
    ../../app/streaming/video/ffmpeg_videosamples.cpp \
    ../../app/streaming/video/plank2decodersource.cpp \
    ../../app/streaming/video/plank2ffmpegdecodertarget.cpp

SOURCES += \
    $$PLANK2_ROOT/core/backend/src/registry_v1.c \
    $$PLANK2_ROOT/core/display/src/topology_v1.c \
    $$PLANK2_ROOT/core/media/src/capabilities_v1.c \
    $$PLANK2_ROOT/core/media/src/interfaces_v1.c \
    $$PLANK2_ROOT/core/media/src/profile_v1.c \
    $$PLANK2_ROOT/platform/linux/media/src/decoder_backend_v1.cpp

HEADERS += \
    ../../app/streaming/video/ffmpegtestframes.h \
    ../../app/streaming/video/plank2decodersource.h \
    ../../app/streaming/video/plank2ffmpegdecodertarget.h

INCLUDEPATH += \
    ../../app \
    $$PLANK2_ROOT/core/backend/include \
    $$PLANK2_ROOT/core/display/include \
    $$PLANK2_ROOT/core/media/include \
    $$PLANK2_ROOT/core/session/include \
    $$PLANK2_ROOT/platform/linux/media/include \
    $$PLANK2_ROOT/platform/linux/media/src
