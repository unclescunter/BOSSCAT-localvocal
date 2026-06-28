#!/usr/bin/env bash
# Installs BOSSCAT-localvocal to the OBS user plugins directory.
# Run from the repo root after building: ./install.sh
set -euo pipefail

RELEASE_DIR="$(dirname "$0")/release"
OBS_PLUGIN_DIR="$HOME/.config/obs-studio/plugins/obs-localvocal"

if [ ! -f "$RELEASE_DIR/lib64/obs-plugins/obs-localvocal.so" ]; then
    echo "ERROR: release/lib64/obs-plugins/obs-localvocal.so not found."
    echo "Build the plugin first:"
    echo ""
    echo "  export ACCELERATION=amd   # or nvidia / generic"
    echo "  export CFLAGS=\"-fPIC\""
    echo "  export CXXFLAGS=\"-fPIC\""
    echo "  cmake -B build_x86_64 --preset linux-x86_64 \\"
    echo "        -DCMAKE_INSTALL_PREFIX=./release \\"
    echo "        -DCMAKE_POSITION_INDEPENDENT_CODE=ON"
    echo "  cmake --build build_x86_64 --target install"
    exit 1
fi

echo "Installing to $OBS_PLUGIN_DIR ..."

mkdir -p "$OBS_PLUGIN_DIR/bin/64bit"
mkdir -p "$OBS_PLUGIN_DIR/data"

cp "$RELEASE_DIR/lib64/obs-plugins/obs-localvocal.so" \
   "$OBS_PLUGIN_DIR/bin/64bit/"

cp -r "$RELEASE_DIR/lib64/obs-plugins/obs-localvocal" \
   "$OBS_PLUGIN_DIR/bin/64bit/"

cp -r "$RELEASE_DIR/share/obs/obs-plugins/obs-localvocal/." \
   "$OBS_PLUGIN_DIR/data/"

echo "Done. Restart OBS to load the plugin."
echo "If OBS reports a load failure, run:  QT_QPA_PLATFORM=xcb obs"
echo "to see the real error in the terminal."
