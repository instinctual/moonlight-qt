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
    ../../app/streaming/video/plank2ffmpegdecodertarget.cpp

HEADERS += \
    ../../app/streaming/video/ffmpegtestframes.h \
    ../../app/streaming/video/plank2decodersource.h \
    ../../app/streaming/video/plank2ffmpegdecodertarget.h

INCLUDEPATH += \
    ../../app \
    $$PLANK2_ROOT/core/backend/include \
    $$PLANK2_ROOT/core/display/include \
    $$PLANK2_ROOT/core/media/include \
    $$PLANK2_ROOT/core/session/include
