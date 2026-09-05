QT -= core gui
CONFIG += console
CONFIG -= app_bundle
TEMPLATE = app
TARGET = test-audiopts
SOURCES += test_audiopts.c ../../moonlight-common-c/moonlight-common-c/src/AudioStream.c
INCLUDEPATH += ../../moonlight-common-c/moonlight-common-c/src
isEmpty(PLANK_TRANSPORT_DIR): error("Set PLANK_TRANSPORT_DIR to the matching root core/transport")
INCLUDEPATH += $$PLANK_TRANSPORT_DIR/include
