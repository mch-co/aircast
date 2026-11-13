#!/usr/bin/env bash
# Installs AirCast on Arch Linux and sets up what it needs:
#   curl -fsSL https://raw.githubusercontent.com/mch-co/aircast/master/scripts/install.sh | bash
# Add "-s -- --yes" after bash to accept every question.
set -euo pipefail

REPO=https://github.com/mch-co/aircast.git
SOURCE="${XDG_DATA_HOME:-$HOME/.local/share}/aircast-src"
PACKAGES=(base-devel git qt6-base qt6-declarative qt6-multimedia qt6-wayland gstreamer
  gst-plugins-base gst-plugins-good gst-plugins-bad gst-libav avahi libplist openssl
  bluez bluez-utils)
BLUEZ_CONF=/etc/bluetooth/main.conf

YES=0
[[ "${1:-}" == "--yes" ]] && YES=1

step() { printf '\n\033[1m==> %s\033[0m\n' "$1"; }
note() { printf '    %s\n' "$1"; }

# The script arrives on stdin when piped from curl, so questions are asked on
# the terminal itself. Without one, optional changes are skipped, not assumed.
ask() {
  ((YES)) && return 0
  [[ -r /dev/tty ]] || { note "No terminal to ask on: skipped. Use --yes to accept."; return 1; }
  local answer
  printf '    %s [Y/n] ' "$1" >/dev/tty
  read -r answer </dev/tty || return 1
  [[ ! "$answer" =~ ^[nN] ]]
}

command -v pacman >/dev/null || { echo "AirCast is packaged for Arch-based systems only (pacman not found)." >&2; exit 1; }
((EUID != 0)) || { echo "Run this as your user, not root: it asks for sudo when needed." >&2; exit 1; }

step "Installing dependencies"
sudo pacman -S --needed --noconfirm "${PACKAGES[@]}"

step "Getting AirCast"
if [[ -d "$SOURCE/.git" ]]; then
  git -C "$SOURCE" pull --ff-only
else
  git clone --depth 1 "$REPO" "$SOURCE"
fi

step "Building and installing the package"
"$SOURCE/bin/install" --noconfirm

step "Starting network discovery and Bluetooth"
sudo systemctl enable --now avahi-daemon bluetooth

step "Firewall"
if ! command -v ufw >/dev/null; then
  note "ufw is not installed: nothing to open."
elif sudo grep -qs 'allow tcp 7000,7100' /etc/ufw/user.rules; then
  note "The AirPlay ports are already open."
elif ask "Open the AirPlay ports in ufw (TCP 7000, 7100 and UDP 5353, 6000, 6001, 7011)?"; then
  sudo ufw allow 7000,7100/tcp comment AirCast
  sudo ufw allow 6000,6001,7011/udp comment AirCast
  sudo ufw allow 5353/udp comment AirCast
else
  note "Skipped. AirCast will offer it again on first launch."
fi

step "Bluetooth, to control the iPhone"
# iOS doesn't list a Bluetooth LE device that shares its address with a
# classic Bluetooth computer, so BlueZ must advertise from a private address.
if grep -qs '^Privacy *= *\(device\|network\|limited-device\|limited-network\)' "$BLUEZ_CONF"; then
  note "BlueZ already advertises from a private address."
elif [[ ! -f "$BLUEZ_CONF" ]]; then
  note "$BLUEZ_CONF not found: skipped."
elif ask "Let BlueZ advertise from a private address? Bluetooth restarts: headphones cut out for a few seconds, and a paired Bluetooth LE device may need pairing again."; then
  sudo cp "$BLUEZ_CONF" "$BLUEZ_CONF.aircast-backup"
  if grep -q '^#\? *Privacy *=' "$BLUEZ_CONF"; then
    sudo sed -i '0,/^#\? *Privacy *=.*/s//Privacy = device/' "$BLUEZ_CONF"
  else
    sudo sed -i '0,/^\[General\]/s//[General]\nPrivacy = device/' "$BLUEZ_CONF"
  fi
  sudo systemctl restart bluetooth
  note "Done. The previous file is $BLUEZ_CONF.aircast-backup"
else
  note "Skipped. Mirroring works without it; control needs it (see the README)."
fi

step "AirCast is installed"
cat <<'TEXT'
    Open AirCast from the app launcher, then on the iPhone:

    Mirror   Control Center > Screen Mirroring > this computer's name,
             and type the code AirCast shows.
    Control  Turn on "Control the iPhone" in AirCast, then
             Settings > Bluetooth > AirCast, and
             Settings > Accessibility > Touch > AssistiveTouch.

    Uninstall with: sudo pacman -R aircast
TEXT
