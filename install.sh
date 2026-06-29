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

DEPS_DIR="$OBS_PLUGIN_DIR/bin/64bit/obs-localvocal"

mkdir -p "$OBS_PLUGIN_DIR/bin/64bit"
mkdir -p "$OBS_PLUGIN_DIR/data"

# Main plugin .so
cp "$RELEASE_DIR/lib64/obs-plugins/obs-localvocal.so" \
   "$OBS_PLUGIN_DIR/bin/64bit/"

# Dependency libraries in named subdirectory
cp -r "$RELEASE_DIR/lib64/obs-plugins/obs-localvocal/." \
   "$DEPS_DIR/"

# Symlink chains required for the versioned .so files to be found at load time
(cd "$DEPS_DIR" && \
  ln -sf libonnxruntime.so.1.20.1 libonnxruntime.so.1 && \
  ln -sf libonnxruntime.so.1      libonnxruntime.so   && \
  ln -sf libwhisper.so.1.8.2      libwhisper.so.1     && \
  ln -sf libwhisper.so.1          libwhisper.so)

# Data files (locale, models)
cp -r "$RELEASE_DIR/share/obs/obs-plugins/obs-localvocal/." \
   "$OBS_PLUGIN_DIR/data/"

echo "Done. Restart OBS to load the plugin."
echo ""
echo "If OBS reports a load failure, run:  QT_QPA_PLATFORM=xcb obs"
echo "to see the real error in the terminal."
