QT -= gui
CONFIG += console c++17 link_pkgconfig
CONFIG -= app_bundle
TEMPLATE = app
TARGET = plank2presentationframe

PLANK2_ROOT = $$(PLANK2_ROOT)
isEmpty(PLANK2_ROOT) {
    PLANK2_ROOT = $$clean_path($$PWD/../../../..)
}
!exists($$PLANK2_ROOT/core/media/include/plank/media/interfaces_v1.h) {
    error("PLANK2 media interface is missing: $$PLANK2_ROOT")
}

PKGCONFIG += libavutil

SOURCES += \
    test_plank2presentationframe.cpp \
    ../../app/streaming/video/plank2presentationframe.cpp \
    $$PLANK2_ROOT/core/backend/src/registry_v1.c \
    $$PLANK2_ROOT/core/display/src/topology_v1.c \
    $$PLANK2_ROOT/core/media/src/capabilities_v1.c \
    $$PLANK2_ROOT/core/media/src/interfaces_v1.c \
    $$PLANK2_ROOT/core/media/src/profile_v1.c

HEADERS += \
    ../../app/streaming/video/plank2presentationframe.h \
    ../../app/streaming/video/plank2presentationsource.h

INCLUDEPATH += \
    ../../app \
    $$PLANK2_ROOT/core/backend/include \
    $$PLANK2_ROOT/core/display/include \
    $$PLANK2_ROOT/core/media/include \
    $$PLANK2_ROOT/core/session/include
