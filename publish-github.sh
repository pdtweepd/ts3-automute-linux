#!/usr/bin/env bash
# Initializes git repository, configures author (pdtweepd@gmail.com),
# authenticates with GitHub CLI (gh), creates the remote repo, pushes code,
# and publishes release artifacts (.deb and .ts3_plugin).

set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
cd "$SCRIPT_DIR"

REPO_NAME="${1:-ts3-automute-linux}"
GIT_EMAIL="pdtweepd@gmail.com"
GIT_NAME="${GIT_AUTHOR_NAME:-Emiel}"

echo "=== 1. Checking git and GitHub CLI (gh) ==="
MISSING_PKGS=()
command -v git >/dev/null 2>&1 || MISSING_PKGS+=("git")
command -v gh >/dev/null 2>&1 || MISSING_PKGS+=("gh")

if [ "${#MISSING_PKGS[@]}" -gt 0 ]; then
  echo "Installing missing packages (${MISSING_PKGS[*]}) via apt..."
  sudo apt update && sudo apt install -y "${MISSING_PKGS[@]}"
fi

echo "=== 2. Building & Testing Plugin Packages ==="
make all
make test

echo "=== 3. Initializing Local Git Repository ==="
if [ ! -d .git ]; then
  git init -b main
fi

git config user.email "$GIT_EMAIL"
git config user.name "$GIT_NAME"

git add README.md LICENSE Makefile install.sh publish-github.sh .gitignore src/ tests/ assets/
if ! git diff --cached --quiet; then
  git commit -m "Initial release: Native Linux AutoMute plugin for TeamSpeak 3 (API v26)"
fi

echo "=== 4. Checking GitHub Authentication ==="
if ! gh auth status >/dev/null 2>&1; then
  echo "Please log in to GitHub (account: $GIT_EMAIL):"
  gh auth login
fi

echo "=== 5. Creating & Pushing GitHub Repository ($REPO_NAME) ==="
if ! git remote get-url origin >/dev/null 2>&1; then
  gh repo create "$REPO_NAME" --public --source=. --remote=origin --push \
    --description "Native Linux Auto Mute plugin for TeamSpeak 3 Client (Debian/Ubuntu/Flatpak, API v26)"
else
  git push -u origin main
fi

echo "=== 6. Creating GitHub Release v1.1.8 with .deb & .ts3_plugin Packages ==="
if ! gh release view v1.1.8 >/dev/null 2>&1; then
  gh release create v1.1.8 \
    build/ts3-plugin-automute_1.1.8_amd64.deb \
    build/automute_1.1.8_linux_amd64.ts3_plugin \
    build/libauto_mute_linux_amd64.so \
    --title "Auto Mute v1.1.8 for Linux (Debian / Ubuntu / Flatpak)" \
    --notes "Native Linux build (API v26) of the Auto Mute plugin for TeamSpeak 3 Client. Includes Debian/Ubuntu .deb package, .ts3_plugin package, and standalone shared library."
fi

echo "Successfully published to GitHub!"
gh repo view --web || true
