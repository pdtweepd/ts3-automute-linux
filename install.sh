#!/usr/bin/env bash
# Installer for TeamSpeak 3 Auto Mute Plugin on Debian/Ubuntu & Linux systems
# Supports both Native (~/.ts3client) and Flatpak (com.teamspeak.TeamSpeak3) installations.

set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
PLUGIN_SO=""
ASSETS_DIR=""

if [ -f "${SCRIPT_DIR}/build/libauto_mute_linux_amd64.so" ]; then
  PLUGIN_SO="${SCRIPT_DIR}/build/libauto_mute_linux_amd64.so"
  ASSETS_DIR="${SCRIPT_DIR}/assets/auto_mute"
elif [ -f "/usr/share/ts3-plugin-automute/plugins/libauto_mute_linux_amd64.so" ]; then
  PLUGIN_SO="/usr/share/ts3-plugin-automute/plugins/libauto_mute_linux_amd64.so"
  ASSETS_DIR="/usr/share/ts3-plugin-automute/plugins/auto_mute"
elif [ -f "${SCRIPT_DIR}/src/auto_mute_linux.c" ]; then
  echo "Building libauto_mute_linux_amd64.so from source..."
  if ! command -v gcc >/dev/null 2>&1; then
    echo "Error: gcc is not installed. On Debian/Ubuntu, run: sudo apt update && sudo apt install -y build-essential" >&2
    exit 1
  fi
  mkdir -p "${SCRIPT_DIR}/build"
  gcc -O2 -Wall -Wextra -fPIC -shared "${SCRIPT_DIR}/src/auto_mute_linux.c" -o "${SCRIPT_DIR}/build/libauto_mute_linux_amd64.so"
  PLUGIN_SO="${SCRIPT_DIR}/build/libauto_mute_linux_amd64.so"
  ASSETS_DIR="${SCRIPT_DIR}/assets/auto_mute"
else
  echo "Error: Could not find libauto_mute_linux_amd64.so or source files." >&2
  exit 1
fi

TARGETS=()
NATIVE_DIR="${HOME}/.ts3client/plugins"
FLATPAK_DIR="${HOME}/.var/app/com.teamspeak.TeamSpeak3/.ts3client/plugins"

if [ -d "${HOME}/.ts3client" ]; then
  TARGETS+=("$NATIVE_DIR")
fi

if [ -d "${HOME}/.var/app/com.teamspeak.TeamSpeak3" ]; then
  TARGETS+=("$FLATPAK_DIR")
fi

if [ "${#TARGETS[@]}" -eq 0 ]; then
  TARGETS+=("$NATIVE_DIR")
fi

for target in "${TARGETS[@]}"; do
  mkdir -p "${target}/auto_mute"
  cp -f "$PLUGIN_SO" "${target}/libauto_mute_linux_amd64.so"
  chmod 0755 "${target}/libauto_mute_linux_amd64.so"
  if [ -n "$ASSETS_DIR" ] && [ -d "$ASSETS_DIR" ]; then
    cp -rf "${ASSETS_DIR}/." "${target}/auto_mute/" 2>/dev/null || true
  fi
  echo "Installed Auto Mute plugin to: ${target}/libauto_mute_linux_amd64.so"
done

echo "Done! Restart TeamSpeak 3 (or reload plugins in Tools -> Options -> Addons)."
