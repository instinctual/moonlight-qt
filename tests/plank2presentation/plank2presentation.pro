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
