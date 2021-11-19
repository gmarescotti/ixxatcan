TARGET = qtixxatcanbus

QT = core serialbus

DISTFILES = plugin.json

DEFINES += QTCAN_BASE_EXPORT= QTCAN_DRIVER_EXPORT= QTCAN_STATIC_DRIVERS=1

# set USE_SOCKET to enable use of Socket CAN instead of
# old Vci classes.
# Socket CAN enables multiple instances to communicate
# with the Ixxat at the same time.
DEFINES += USE_SOCKET

HEADERS += \
    ixxatcanbackend.h \
    CanDriver_ixxatVci.h

SOURCES += \
    main.cpp \
    ixxatcanbackend.cpp

contains( DEFINES, USE_SOCKET ) {

HEADERS += \

SOURCES += \
    CanDriver_ixxatVciSocket.cpp

INCLUDEPATH += \
    $$PWD/VciWindows_4.1/inc

    contains(QT_ARCH, x86_64): LIBS += "$$PWD/VciWindows_4.1/lib/x64/vcinpl2.lib"
    contains(QT_ARCH, x86_64): LIBS += "$$PWD/VciWindows_4.1/lib/x64/Release/vciapi.lib"

    contains(QT_ARCH, i386): LIBS += "$$PWD/VciWindows_4.1/lib/x32/vciapi2.lib"
    contains(QT_ARCH, i386): LIBS += "$$PWD/VciWindows_4.1/lib/x32/Release/vciapi.lib"

} else {

HEADERS += \

SOURCES += \
    CanDriver_ixxatVci.cpp \


INCLUDEPATH += \
    $$PWD/VciWindows_4.0/inc

    equals(QT_ARCH, x86_64): LIBS += "$$PWD/VciWindows_4.0/lib/x64/vcinpl.lib"
    equals(QT_ARCH, i386): LIBS += "$$PWD/VciWindows_4.0/lib/ia32/vcinpl.lib"
}

PLUGIN_TYPE = canbus
PLUGIN_CLASS_NAME = ixxatCanBusPlugin
load(qt_plugin)
