QT += core testlib
QT -= gui
CONFIG += console testcase c++17 link_pkgconfig
TEMPLATE = app
PKGCONFIG += sdl3 libavcodec libavutil
SOURCES += test_pacerclock.cpp \
    ../../app/streaming/video/ffmpeg-renderers/pacer/pacer.cpp \
    ../../app/streaming/avsynccontroller.cpp
INCLUDEPATH += ../../app \
    ../../moonlight-common-c/moonlight-common-c/src
