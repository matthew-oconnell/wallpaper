#!/usr/bin/env bash
set -euo pipefail

# release.sh - Build and produce a distributable Flatpak for this project
# Usage: ./release.sh [--install]
# - Creates/updates flathub remote
# - Ensures KDE SDK/Platform 6.4 are installed
# - Runs flatpak-builder to build a local repo
# - Produces wallaroo.flatpak bundle in project root
# - With --install will install the built app into the current user

PROJECT_ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
MANIFEST="$PROJECT_ROOT/flatpak/manifest.json"
BUILD_DIR="$PROJECT_ROOT/flatpak/build"
REPO_DIR="$PROJECT_ROOT/flatpak/repo"
BUNDLE="$PROJECT_ROOT/wallaroo.flatpak"
APP_ID="org.matthew.wallaroo"
SDK="org.kde.Sdk//6.4"
PLATFORM="org.kde.Platform//6.4"

INSTALL_AFTER_BUILD=false
if [[ "${1:-}" == "--install" ]]; then
  INSTALL_AFTER_BUILD=true
fi

echo "Project root: $PROJECT_ROOT"

command -v flatpak >/dev/null 2>&1 || { echo "flatpak is required. Install it and try again." >&2; exit 1; }
command -v flatpak-builder >/dev/null 2>&1 || { echo "flatpak-builder is required. Install it and try again." >&2; exit 1; }

# Ensure Flathub remote exists
echo "Ensuring Flathub remote exists..."
flatpak remote-add --if-not-exists flathub https://flathub.org/repo/flathub.flatpakrepo

# Ensure SDK and Platform installed (system or user)
ensure_flatpak_ref_installed() {
  local ref="$1"
  if flatpak --user info "$ref" >/dev/null 2>&1 || flatpak info "$ref" >/dev/null 2>&1; then
    echo "Found $ref"
  else
    echo "$ref not found locally. Installing from Flathub (may require sudo for system installs)..."
    flatpak install -y flathub "$ref"
  fi
}

ensure_flatpak_ref_installed "$SDK"
ensure_flatpak_ref_installed "$PLATFORM"

# Prepare build dirs
mkdir -p "$BUILD_DIR"
mkdir -p "$REPO_DIR"

# Run the build
echo "Running flatpak-builder..."
flatpak-builder --force-clean --repo="$REPO_DIR" "$BUILD_DIR" "$MANIFEST"

# Create bundle
echo "Creating Flatpak bundle: $BUNDLE"
# Use detected arch
ARCH="$(uname -m)"
flatpak build-bundle "$REPO_DIR" "$BUNDLE" "$APP_ID" --arch="$ARCH"

echo "Bundle created at: $BUNDLE"

if [[ "$INSTALL_AFTER_BUILD" == true ]]; then
  echo "Installing built Flatpak to the current user..."
  flatpak --user install --reinstall -y "$REPO_DIR" "$APP_ID"
  echo "Installed $APP_ID"
fi

echo "Done. To install on another machine copy $BUNDLE and run:"
echo "  flatpak install --user ./wallaroo.flatpak"

exit 0
