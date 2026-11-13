#!/usr/bin/env bash
# Removes AirCast and, if you agree, the settings its installer changed:
#   curl -fsSL https://raw.githubusercontent.com/mch-co/aircast/master/scripts/uninstall.sh | bash
# Add "-s -- --yes" after bash to accept every question.
set -euo pipefail

SOURCE="${XDG_DATA_HOME:-$HOME/.local/share}/aircast-src"
DATA=("${XDG_CONFIG_HOME:-$HOME/.config}/aircast" "${XDG_DATA_HOME:-$HOME/.local/share}/aircast")
BLUEZ_CONF=/etc/bluetooth/main.conf

YES=0
[[ "${1:-}" == "--yes" ]] && YES=1

step() { printf '\n\033[1m==> %s\033[0m\n' "$1"; }
note() { printf '    %s\n' "$1"; }

# Questions go to the terminal, because the script itself arrives on stdin
# when piped from curl. Without a terminal, nothing optional is removed.
ask() {
  ((YES)) && return 0
  [[ -r /dev/tty ]] || { note "No terminal to ask on: kept. Use --yes to accept."; return 1; }
  local answer
  printf '    %s [Y/n] ' "$1" >/dev/tty
  read -r answer </dev/tty || return 1
  [[ ! "$answer" =~ ^[nN] ]]
}

((EUID != 0)) || { echo "Run this as your user, not root: it asks for sudo when needed." >&2; exit 1; }

step "Removing AirCast"
pkill -x aircast 2>/dev/null && note "Closed the running AirCast." || true
if command -v pacman >/dev/null && pacman -Qq aircast >/dev/null 2>&1; then
  sudo pacman -R --noconfirm aircast
elif [[ -e /usr/local/bin/aircast ]]; then
  # Outside Arch the installer copies three files instead of a package.
  sudo rm -f /usr/local/bin/aircast /usr/local/share/applications/aircast.desktop \
    /usr/local/share/icons/hicolor/scalable/apps/aircast.svg
else
  note "AirCast is not installed."
fi
rm -rf "$SOURCE"

step "Settings and pairing key"
if [[ -e "${DATA[0]}" || -e "${DATA[1]}" ]]; then
  if ask "Delete AirCast's settings and its pairing key? Phones will ask for the code again after a reinstall."; then
    rm -rf "${DATA[@]}"
  else
    note "Kept in ${DATA[*]}"
  fi
else
  note "Nothing to delete."
fi

step "Firewall"
if command -v firewall-cmd >/dev/null && sudo firewall-cmd --query-port=7000/tcp >/dev/null 2>&1; then
  if ask "Close the AirPlay ports in firewalld (TCP 7000, 7100 and UDP 5353, 6000, 6001, 7011)?"; then
    sudo firewall-cmd --permanent --remove-port={7000,7100}/tcp --remove-port={5353,6000,6001,7011}/udp
    sudo firewall-cmd --reload
  else
    note "Left open."
  fi
elif command -v ufw >/dev/null && sudo grep -qs 'allow tcp 7000,7100' /etc/ufw/user.rules; then
  if ask "Close the AirPlay ports in ufw (TCP 7000, 7100 and UDP 5353, 6000, 6001, 7011)?"; then
    sudo ufw delete allow 7000,7100/tcp || true
    sudo ufw delete allow 6000,6001,7011/udp || true
    sudo ufw delete allow 5353/udp || true
  else
    note "Left open."
  fi
else
  note "No AirCast rule found."
fi

step "Bluetooth"
# Only undone when the installer's backup is there: without it, the private
# address was someone else's choice.
if [[ -f "$BLUEZ_CONF.aircast-backup" ]] && grep -qs '^Privacy *= *device' "$BLUEZ_CONF"; then
  if ask "Turn BlueZ's private address back off? Bluetooth restarts: headphones cut out for a few seconds."; then
    sudo sed -i 's/^Privacy *= *device/#Privacy = off/' "$BLUEZ_CONF"
    sudo rm -f "$BLUEZ_CONF.aircast-backup"
    sudo systemctl restart bluetooth
  else
    note "Left as it is."
  fi
else
  note "Nothing set by the installer."
fi

step "AirCast is removed"
cat <<'TEXT'
    Left in place, because other programs may use them: the packages
    installed as dependencies, and the Avahi and Bluetooth services.

    On the iPhone, you can forget the computer in Settings > Bluetooth.
TEXT
