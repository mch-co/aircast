# Contributing to AirCast

Thanks for helping. AirCast is a small app with one job: receive an AirPlay
screen mirror in a window on Arch Linux / Omarchy. Changes that keep it small
are the easiest to merge.

## Before you start

- For a bug, open an issue with what you did, what happened, and the
  `airplay:` lines printed when you run `aircast` from a terminal. Say which
  device and OS version was mirroring.
- For a feature, open an issue first so we can agree on it before you write
  code.

## Build and test

You need `qt6-base`, `qt6-declarative`, `qt6-multimedia`, `gstreamer` with the
base, good, bad and libav plugins, `avahi`, `libplist`, `openssl`, and `ffmpeg`
for the tests.

```sh
bin/build      # builds build/aircast
bin/test       # runs the tests offscreen
bin/install    # builds and installs the Arch package
```

Run `bin/test` before every pull request. Close a running AirCast first, or
the network announcement test is skipped.

## Where things live

| Path | What it holds |
| --- | --- |
| `src/backend.{h,cpp}` | The receiver: server, video pipeline, firewall, idle lock |
| `src/remote.{h,cpp}` | Phone control: a Bluetooth LE mouse and keyboard served through BlueZ |
| `src/Main.qml` | The window: layout and keyboard shortcuts, no logic |
| `src/theme.*`, `src/translator.*` | Omarchy theme and language following |
| `src/i18n/<code>.json` | Translations, English text to translation |
| `tests/` | Qt Test cases |
| `vendor/uxplay/` | UxPlay's AirPlay library, unmodified |

## Rules of the house

- **Don't edit `vendor/uxplay/`.** Protocol fixes belong upstream in
  [UxPlay](https://github.com/FDH2/UxPlay); we update the vendored copy from
  its releases.
- **Logic in C++, layout in QML.** Anything that can fail lives in `Backend`
  and is reported in the window, never silently skipped.
- **Every visible string is translatable**: `qsTr("…")` in QML, `tr("…")` in
  C++, with `%1` placeholders. Add the French line to `src/i18n/fr.json`; other
  languages are welcome as new `src/i18n/<code>.json` files listed in
  `src/resources.qrc`.
- **Every action has a keyboard shortcut**, shown in the `?` overlay and in the
  README table.
- **New behavior comes with a test** in `tests/aircast_tests.cpp`. Tests must
  not touch the real firewall, network identity, or user settings.
- **Comments explain why**, in full sentences. Commit messages are short and
  imperative ("Decode H.265").
- No new dependency for a few lines of code.

## Pull requests

Keep each pull request to one change, describe how you tested it (which
device, which resolution), and add a screenshot when the window changes. Use
made-up content in screenshots, never real names or devices.

## License

AirCast is GPL-3.0-or-later. By contributing, you agree that your work is
released under the same license.
