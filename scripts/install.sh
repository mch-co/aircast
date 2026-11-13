#!/usr/bin/env bash
# Installs AirCast on Arch, Ubuntu/Debian or Fedora and sets up what it needs:
#   curl -fsSL https://raw.githubusercontent.com/mch-co/aircast/master/scripts/install.sh | bash
# Add "-s -- --yes" after bash to accept every question.
set -euo pipefail

REPO=https://github.com/mch-co/aircast.git
SOURCE="${XDG_DATA_HOME:-$HOME/.local/share}/aircast-src"
# A local checkout to install from instead of cloning, for development.
LOCAL="${AIRCAST_SOURCE:-}"
PREFIX=/usr/local
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

((EUID != 0)) || { echo "Run this as your user, not root: it asks for sudo when needed." >&2; exit 1; }

# The same libraries under each family's package names.
. /etc/os-release
case " ${ID:-} ${ID_LIKE:-} " in
*" arch "*)
  FAMILY=arch
  PACKAGES=(base-devel git qt6-base qt6-declarative qt6-multimedia qt6-wayland gstreamer
    gst-plugins-base gst-plugins-good gst-plugins-bad gst-libav avahi libplist openssl
    bluez bluez-utils)
  ;;
*" debian "* | *" ubuntu "*)
  FAMILY=debian
  PACKAGES=(build-essential git pkg-config qmake6 qt6-base-dev qt6-declarative-dev
    qt6-multimedia-dev qt6-wayland qml6-module-qtquick qml6-module-qtquick-controls
    qml6-module-qtquick-layouts qml6-module-qtquick-shapes qml6-module-qtquick-window
    qml6-module-qtquick-templates qml6-module-qtqml-workerscript qml6-module-qtmultimedia
    libgstreamer1.0-dev libgstreamer-plugins-base1.0-dev gstreamer1.0-plugins-base
    gstreamer1.0-plugins-good gstreamer1.0-plugins-bad gstreamer1.0-libav
    libavahi-compat-libdnssd-dev avahi-daemon libplist-dev libssl-dev bluez)
  ;;
*" fedora "* | *" rhel "*)
  FAMILY=fedora
  PACKAGES=(gcc-c++ make git pkgconf-pkg-config qt6-qtbase-devel qt6-qtdeclarative-devel
    qt6-qtmultimedia-devel qt6-qtwayland gstreamer1-devel gstreamer1-plugins-base-devel
    gstreamer1-plugins-good gstreamer1-plugins-bad-free gstreamer1-plugin-libav
    gstreamer1-plugin-openh264 mozilla-openh264
    avahi avahi-compat-libdns_sd-devel libplist-devel openssl-devel bluez)
  ;;
*)
  echo "AirCast's installer knows Arch, Ubuntu/Debian and Fedora; this is ${PRETTY_NAME:-another system}." >&2
  echo "Install the libraries listed in the README, then run bin/build." >&2
  exit 1
  ;;
esac

step "Installing dependencies"
case $FAMILY in
arch) sudo pacman -S --needed --noconfirm "${PACKAGES[@]}" ;;
debian) sudo apt-get update && sudo apt-get install -y "${PACKAGES[@]}" ;;
fedora) sudo dnf install -y "${PACKAGES[@]}" ;;
esac

step "Getting AirCast"
if [[ -n "$LOCAL" ]]; then
  SOURCE="$LOCAL"
  note "Using the checkout in $SOURCE"
elif [[ -d "$SOURCE/.git" ]]; then
  git -C "$SOURCE" pull --ff-only
else
  git clone --depth 1 "$REPO" "$SOURCE"
fi

step "Building and installing"
# Quiet make: the compiler lines are noise unless something fails.
if [[ $FAMILY == arch ]]; then
  MAKEFLAGS=-s "$SOURCE/bin/install" --noconfirm
else
  # No native package yet outside Arch: the same three files go under $PREFIX.
  MAKEFLAGS=-s "$SOURCE/bin/build"
  sudo install -Dm755 "$SOURCE/build/aircast" "$PREFIX/bin/aircast"
  sudo install -Dm644 "$SOURCE/pkgbuild/aircast.desktop" "$PREFIX/share/applications/aircast.desktop"
  sudo install -Dm644 "$SOURCE/pkgbuild/aircast.svg" "$PREFIX/share/icons/hicolor/scalable/apps/aircast.svg"
  sudo update-desktop-database "$PREFIX/share/applications" 2>/dev/null || true
fi

step "Starting network discovery and Bluetooth"
sudo systemctl enable --now avahi-daemon bluetooth

step "Firewall"
if command -v firewall-cmd >/dev/null && sudo firewall-cmd --state >/dev/null 2>&1; then
  if sudo firewall-cmd --query-port=7000/tcp >/dev/null 2>&1; then
    note "The AirPlay ports are already open."
  elif ask "Open the AirPlay ports in firewalld (TCP 7000, 7100 and UDP 5353, 6000, 6001, 7011)?"; then
    sudo firewall-cmd --permanent --add-port={7000,7100}/tcp --add-port={5353,6000,6001,7011}/udp
    sudo firewall-cmd --reload
  else
    note "Skipped. Apple devices won't find this computer until the ports are open."
  fi
elif ! command -v ufw >/dev/null; then
  note "No ufw or running firewalld: nothing to open."
elif sudo grep -qs 'allow tcp 7000,7100' /etc/ufw/user.rules; then
  note "The AirPlay ports are already open."
elif ask "Open the AirPlay ports in ufw (TCP 7000, 7100 and UDP 5353, 6000, 6001, 7011)?"; then
  # ufw saves a rule before applying it, and applying fails when its service
  # is stopped. That must not end the install: the rules load when ufw starts.
  applied=1
  sudo ufw allow 7000,7100/tcp comment AirCast || applied=0
  sudo ufw allow 6000,6001,7011/udp comment AirCast || applied=0
  sudo ufw allow 5353/udp comment AirCast || applied=0
  if ((!applied)); then
    note "ufw saved the rules but could not apply them now."
    systemctl is-active --quiet ufw || note "Its service is stopped, so nothing is blocked; the rules load with: sudo systemctl start ufw"
  fi
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

    Uninstall with:
    curl -fsSL https://raw.githubusercontent.com/mch-co/aircast/master/scripts/uninstall.sh | bash
TEXT
