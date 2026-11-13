# AirCast

An AirPlay receiver for Omarchy. Open it, and your iPhone, iPad or Mac can
mirror its screen (with sound) into the window: Control Center > Screen
Mirroring > the name of this computer. The first time, the phone asks for the
four-digit code AirCast shows.

Both devices must be on the same network, and `avahi-daemon` must be running.

| Key | Action |
| --- | --- |
| Space | Show or hide this computer on the network |
| F | Full screen |
| Esc | Exit full screen |
| D | Disconnect |
| ? | Help |
| Q | Quit |

Build with `bin/build`, test with `bin/test`, install with `bin/install`.

## Credits and license

The AirPlay protocol code in `vendor/uxplay` is the library of
[UxPlay](https://github.com/FDH2/UxPlay) 1.73.7 (GPL-3.0), unmodified, so
AirCast is GPL-3.0 too. `theme` and `translator` come from the Omarchy app
templates (Omacut and Monologue, MIT, David Heinemeier Hansson).
