# AirCast

![AirCast mirroring an iPhone](screenshot.png)

An AirPlay receiver for Omarchy. Open it, and your iPhone, iPad or Mac can
mirror its screen (with sound) into the window: Control Center > Screen
Mirroring > the name of this computer. The first time, the phone asks for the
four-digit code AirCast shows.

Both devices must be on the same network, and `avahi-daemon` must be running.
H.264 and H.265 sources are decoded, and the screen stays awake while a device
is mirroring.

| Key | Action |
| --- | --- |
| Space | Show or hide this computer on the network |
| F | Full screen |
| Esc | Exit full screen |
| D | Disconnect |
| ? | Help |
| Q | Quit |

On first launch AirCast offers to open its ports in `ufw` (TCP 7000, 7100 and
UDP 5353, 6000, 6001, 7011); the choice is yours and is asked once.

Build with `bin/build`, test with `bin/test`, install with `bin/install`.

## Credits and license

AirCast is an independent project, not affiliated with or endorsed by Apple.
AirPlay, iPhone, iPad and Mac are trademarks of Apple Inc.

The AirPlay protocol code in `vendor/uxplay` is the library of
[UxPlay](https://github.com/FDH2/UxPlay) 1.73.7 (GPL-3.0), unmodified, so
AirCast is GPL-3.0 too. `theme` and `translator` come from the Omarchy app
templates (Omacut and Monologue, MIT, David Heinemeier Hansson).
