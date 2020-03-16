TARGET = qtixxatcanbus

QT = core serialbus

HEADERS += \
    ixxatcanbackend.h \
    CanDriver_ixxatVci.h

SOURCES += \
    main.cpp \
    ixxatcanbackend.cpp \
    CanDriver_ixxatVci.cpp \

INCLUDEPATH += \
    $$PWD/VciWindows_4.0/inc

DISTFILES = plugin.json

DEFINES += QTCAN_BASE_EXPORT= QTCAN_DRIVER_EXPORT= QTCAN_STATIC_DRIVERS=1

LIBS += -L"$$PWD/VciWindows_4.0/lib/x64/" vcinpl.lib

PLUGIN_TYPE = canbus
PLUGIN_CLASS_NAME = ixxatCanBusPlugin
load(qt_plugin)
