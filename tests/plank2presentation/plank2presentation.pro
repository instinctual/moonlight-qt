QT -= gui
CONFIG += console c++17
CONFIG -= app_bundle
TEMPLATE = app
TARGET = plank2presentation

PLANK2_ROOT = $$(PLANK2_ROOT)
isEmpty(PLANK2_ROOT) {
    PLANK2_ROOT = $$clean_path($$PWD/../../../..)
}
!exists($$PLANK2_ROOT/platform/linux/presentation/src/presentation_source_v1.hpp) {
    error("PLANK2 presentation source interface is missing: $$PLANK2_ROOT")
}

SOURCES += \
    test_plank2presentation.cpp \
    ../../app/streaming/video/plank2presentationsource.cpp

SOURCES += \
    $$PLANK2_ROOT/core/backend/src/registry_v1.c \
    $$PLANK2_ROOT/core/display/src/presentation_interface_v1.c \
    $$PLANK2_ROOT/core/display/src/presentation_v1.c \
    $$PLANK2_ROOT/core/display/src/topology_v1.c \
    $$PLANK2_ROOT/core/media/src/capabilities_v1.c \
    $$PLANK2_ROOT/core/media/src/interfaces_v1.c \
    $$PLANK2_ROOT/core/media/src/profile_v1.c \
    $$PLANK2_ROOT/platform/linux/presentation/src/presentation_backend_v1.cpp

HEADERS += \
    ../../app/streaming/video/plank2presentationsource.h

INCLUDEPATH += \
    ../../app \
    $$PLANK2_ROOT/core/backend/include \
    $$PLANK2_ROOT/core/display/include \
    $$PLANK2_ROOT/core/media/include \
    $$PLANK2_ROOT/core/session/include \
    $$PLANK2_ROOT/platform/linux/presentation/include \
    $$PLANK2_ROOT/platform/linux/presentation/src
