# The AirPlay protocol library from UxPlay 1.73.7 (GPL-3.0), built from source
# with the same defines its own CMake build uses.
UXPLAY = $$PWD/uxplay
INCLUDEPATH += $$UXPLAY/lib $$UXPLAY/lib/llhttp $$UXPLAY/lib/playfair $$UXPLAY/renderers
DEFINES += NOHOLD OPENSSL_API_COMPAT=0x10101000L _LINUX TARGET_POSIX STANDALONE
# The library adapts to the installed libplist: Ubuntu 22.04 ships 2.2, which
# has no plist_mem_free.
system(pkg-config --atleast-version=2.1.0 libplist-2.0): DEFINES += PLIST_210
system(pkg-config --atleast-version=2.3.0 libplist-2.0): DEFINES += PLIST_230
QMAKE_CFLAGS += -w
SOURCES += $$files($$UXPLAY/lib/*.c) $$files($$UXPLAY/lib/llhttp/*.c) $$files($$UXPLAY/lib/playfair/*.c) \
    $$UXPLAY/renderers/audio_renderer.c
