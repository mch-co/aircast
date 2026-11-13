QT += core gui qml quick quickcontrols2 dbus multimedia network

CONFIG += c++17 release link_pkgconfig
PKGCONFIG += gstreamer-1.0 gstreamer-app-1.0 gstreamer-video-1.0 libplist-2.0 openssl avahi-compat-libdns_sd
TARGET = aircast
TEMPLATE = app

include(vendor/uxplay.pri)

HEADERS += src/backend.h src/theme.h src/translator.h
SOURCES += src/main.cpp src/backend.cpp src/theme.cpp src/translator.cpp

RESOURCES += src/resources.qrc
