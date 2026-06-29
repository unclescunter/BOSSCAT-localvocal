#!/usr/bin/env bash
# Creates a .deb that installs obs-localvocal-bosscat into:
#   ~/.config/obs-studio/plugins/obs-localvocal/
# for the current user (native Ubuntu OBS install).
#
# ============================================================
# IMPORTANT: THIS SCRIPT MUST BE RUN ON UBUNTU, NOT FEDORA.
# ============================================================
# The binaries in release/ are linked against the system libraries
# of whichever machine ran cmake. Fedora binaries use a newer glibc
# and a different libobs ABI than Ubuntu — they will fail to load on
# Ubuntu with "GLIBC_2.4x not found" or missing OBS symbols.
#
# To produce a working .deb:
#   1. Clone this repo on an Ubuntu machine
#   2. Install Ubuntu deps and build (see README)
#   3. Run this script
#
# Tested targets: Ubuntu 22.04 / 24.04 with native obs-studio package.
# ============================================================
set -euo pipefail

RELEASE_DIR="$(dirname "$0")/release"
PKG_NAME="obs-localvocal-bosscat"
VERSION="0.6.2-bosscat1"
ARCH="amd64"
PKG_ROOT="/tmp/${PKG_NAME}_${VERSION}_${ARCH}"

# Verify we are on a Debian-based system
if ! command -v dpkg-deb &>/dev/null; then
    echo "ERROR: dpkg-deb not found. This script must be run on Ubuntu/Debian."
    exit 1
fi

# Verify the build output exists
if [ ! -f "$RELEASE_DIR/lib64/obs-plugins/obs-localvocal.so" ]; then
    echo "ERROR: release/lib64/obs-plugins/obs-localvocal.so not found."
    echo "Build first — see README for the full cmake command."
    exit 1
fi

echo "Packaging $PKG_NAME $VERSION ..."

# Clean any previous attempt
rm -rf "$PKG_ROOT"

# ── Staging area inside the package ─────────────────────────
# We ship files to /usr/lib/obs-localvocal-bosscat/ and the
# postinst script copies them into the user's home directory.
# dpkg tracks the staged files; postinst handles the home-dir copy.
STAGE="$PKG_ROOT/usr/lib/$PKG_NAME"
mkdir -p "$STAGE/bin"
mkdir -p "$STAGE/deps"
mkdir -p "$STAGE/data"

# Main plugin .so
cp "$RELEASE_DIR/lib64/obs-plugins/obs-localvocal.so" "$STAGE/bin/"

# Dependency .so files
cp "$RELEASE_DIR/lib64/obs-plugins/obs-localvocal/"* "$STAGE/deps/" 2>/dev/null || true

# Symlink chains (dpkg preserves symlinks in the package tree)
(cd "$STAGE/deps" && \
  ln -sf libonnxruntime.so.1.20.1 libonnxruntime.so.1 && \
  ln -sf libonnxruntime.so.1      libonnxruntime.so   && \
  ln -sf libwhisper.so.1.8.2      libwhisper.so.1     && \
  ln -sf libwhisper.so.1          libwhisper.so)

# Data files (locale, models)
cp -r "$RELEASE_DIR/share/obs/obs-plugins/obs-localvocal/." "$STAGE/data/"

# ── DEBIAN control files ─────────────────────────────────────
mkdir -p "$PKG_ROOT/DEBIAN"

cat > "$PKG_ROOT/DEBIAN/control" <<EOF
Package: $PKG_NAME
Version: $VERSION
Architecture: $ARCH
Maintainer: unclescunter <therealunclelem@gmail.com>
Section: video
Priority: optional
Depends: obs-studio, libqt6core6 | libqt6core6t64, libqt6widgets6 | libqt6widgets6t64, libcurl4, libopenblas0
Description: BOSSCAT localvocal — broadcast-grade local captioning OBS plugin
 GPL-2.0 fork of obs-localvocal adding 6 feature layers: rewritten caption
 buffer engine, configurable display properties, multi-source audio mix,
 remote whisper.cpp server backend, sentence-buffered SRT output, and an
 active-scene caption preview dock.
 .
 Most of the BOSSCAT feature code was written with Claude (Anthropic AI).
 Build and use at your own risk.
EOF

# ── postinst: copy staged files into the installing user's home ──
cat > "$PKG_ROOT/DEBIAN/postinst" <<'POSTINST'
#!/usr/bin/env bash
set -euo pipefail

PKG_NAME="obs-localvocal-bosscat"
STAGE="/usr/lib/$PKG_NAME"

# Resolve the real user who ran sudo (not root)
REAL_USER="${SUDO_USER:-}"
if [ -z "$REAL_USER" ]; then
    REAL_USER="$(logname 2>/dev/null || true)"
fi
if [ -z "$REAL_USER" ] || [ "$REAL_USER" = "root" ]; then
    echo "WARNING: Could not determine the installing user."
    echo "Manually copy files from /usr/lib/$PKG_NAME/ to:"
    echo "  ~/.config/obs-studio/plugins/obs-localvocal/"
    exit 0
fi

REAL_HOME="$(getent passwd "$REAL_USER" | cut -d: -f6)"
PLUGIN_DIR="$REAL_HOME/.config/obs-studio/plugins/obs-localvocal"

echo "Installing obs-localvocal-bosscat for user: $REAL_USER"

mkdir -p "$PLUGIN_DIR/bin/64bit/obs-localvocal"
mkdir -p "$PLUGIN_DIR/data"

cp "$STAGE/bin/obs-localvocal.so" "$PLUGIN_DIR/bin/64bit/"
cp -r "$STAGE/deps/." "$PLUGIN_DIR/bin/64bit/obs-localvocal/"
cp -r "$STAGE/data/." "$PLUGIN_DIR/data/"

chown -R "$REAL_USER:" "$PLUGIN_DIR"

echo "Plugin installed to $PLUGIN_DIR"
echo "Restart OBS to load it. The BOSSCAT Captions dock will appear under Docks."
POSTINST

# ── prerm: remove the plugin from the user's home ───────────
cat > "$PKG_ROOT/DEBIAN/prerm" <<'PRERM'
#!/usr/bin/env bash
set -euo pipefail

REAL_USER="${SUDO_USER:-}"
if [ -z "$REAL_USER" ]; then
    REAL_USER="$(logname 2>/dev/null || true)"
fi
if [ -z "$REAL_USER" ] || [ "$REAL_USER" = "root" ]; then
    echo "WARNING: Could not determine user. Remove manually:"
    echo "  rm -rf ~/.config/obs-studio/plugins/obs-localvocal"
    exit 0
fi

REAL_HOME="$(getent passwd "$REAL_USER" | cut -d: -f6)"
PLUGIN_DIR="$REAL_HOME/.config/obs-studio/plugins/obs-localvocal"

if [ -d "$PLUGIN_DIR" ]; then
    rm -rf "$PLUGIN_DIR"
    echo "Removed $PLUGIN_DIR"
fi
PRERM

chmod 0755 "$PKG_ROOT/DEBIAN/postinst" "$PKG_ROOT/DEBIAN/prerm"

# ── Build the .deb ───────────────────────────────────────────
OUTPUT_DEB="$(dirname "$0")/release/${PKG_NAME}_${VERSION}_${ARCH}.deb"
dpkg-deb --build --root-owner-group "$PKG_ROOT" "$OUTPUT_DEB"

echo ""
echo "Created: $OUTPUT_DEB"
echo ""
echo "Install with:"
echo "  sudo dpkg -i $OUTPUT_DEB"
echo ""
echo "Remove with:"
echo "  sudo dpkg -r $PKG_NAME"
