QT += core gui quick quickcontrols2 testlib dbus multimedia network
CONFIG += c++17 testcase link_pkgconfig
PKGCONFIG += gstreamer-1.0 gstreamer-app-1.0 gstreamer-video-1.0 libplist-2.0 openssl avahi-compat-libdns_sd
TARGET = aircast_tests
TEMPLATE = app

include(../vendor/uxplay.pri)
INCLUDEPATH += ../src

HEADERS += ../src/backend.h
SOURCES += aircast_tests.cpp ../src/backend.cpp
