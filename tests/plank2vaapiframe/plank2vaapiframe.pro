QT -= gui
CONFIG += console c++17 link_pkgconfig
CONFIG -= app_bundle
TEMPLATE = app
TARGET = plank2vaapiframe
PLANK2_ROOT = $$(PLANK2_ROOT)
isEmpty(PLANK2_ROOT): PLANK2_ROOT = $$clean_path($$PWD/../../../..)
PKGCONFIG += libavcodec libavutil libva egl glesv2 gbm
SOURCES += test_plank2vaapiframe.cpp \
    ../../app/streaming/video/plank2vaapiframe.cpp \
    ../../app/streaming/video/plank2presentationframe.cpp \
    ../../app/streaming/video/ffmpeg_videosamples.cpp \
    $$PLANK2_ROOT/platform/linux/presentation/src/egl_dma_buf_image_v1.cpp \
    $$PLANK2_ROOT/core/backend/src/registry_v1.c \
    $$PLANK2_ROOT/core/display/src/topology_v1.c \
    $$PLANK2_ROOT/core/media/src/capabilities_v1.c \
    $$PLANK2_ROOT/core/media/src/interfaces_v1.c \
    $$PLANK2_ROOT/core/media/src/profile_v1.c
INCLUDEPATH += ../../app \
    $$PLANK2_ROOT/platform/linux/presentation/include \
    $$PLANK2_ROOT/core/backend/include \
    $$PLANK2_ROOT/core/display/include \
    $$PLANK2_ROOT/core/media/include \
    $$PLANK2_ROOT/core/session/include
