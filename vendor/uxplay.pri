# The AirPlay protocol library from UxPlay 1.73.7 (GPL-3.0), built from source
# with the same defines its own CMake build uses.
UXPLAY = $$PWD/uxplay
INCLUDEPATH += $$UXPLAY/lib $$UXPLAY/lib/llhttp $$UXPLAY/lib/playfair $$UXPLAY/renderers
DEFINES += NOHOLD PLIST_210 PLIST_230 OPENSSL_API_COMPAT=0x10101000L _LINUX TARGET_POSIX STANDALONE
QMAKE_CFLAGS += -w
SOURCES += $$files($$UXPLAY/lib/*.c) $$files($$UXPLAY/lib/llhttp/*.c) $$files($$UXPLAY/lib/playfair/*.c) \
    $$UXPLAY/renderers/audio_renderer.c
