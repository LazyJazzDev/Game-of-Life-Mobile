#!/bin/sh
set -eu

# Override for another macOS DevEco installation without changing the project.
DEVECO_STUDIO_DIR="${DEVECO_STUDIO_DIR:-/Applications/DevEco-Studio.app/Contents}"
export NODE_HOME="$DEVECO_STUDIO_DIR/tools/node"
export JAVA_HOME="$DEVECO_STUDIO_DIR/jbr/Contents/Home"
export DEVECO_SDK_HOME="${DEVECO_SDK_HOME:-$DEVECO_STUDIO_DIR/sdk}"
export PATH="$NODE_HOME/bin:$PATH"
# BUILD_MODE=release with TASK=assembleApp builds the signed .app package for AppGallery.
BUILD_MODE="${BUILD_MODE:-debug}"
TASK="${TASK:-assembleHap}"
cd "$(dirname "$0")"
# DevEco's native build cannot take CMake arguments; the mobile dependencies
# default to out/mobile/deps in the repository root.
export LONGMARCH_MOBILE_DEPS="${LONGMARCH_MOBILE_DEPS:-$(cd .. && pwd)/out/mobile/deps}"
if [ ! -f entry/src/main/resources/rawfile/Resources/manifest.json ]; then
  echo "Stage the game resources first (prepare_resources.py); see README.md." >&2
  exit 1
fi
"$DEVECO_STUDIO_DIR/tools/ohpm/bin/ohpm" install --all
if [ "$TASK" = assembleApp ]; then
  exec "$DEVECO_STUDIO_DIR/tools/hvigor/bin/hvigorw" --mode project \
    -p product=default -p buildMode="$BUILD_MODE" assembleApp --no-daemon "$@"
fi
exec "$DEVECO_STUDIO_DIR/tools/hvigor/bin/hvigorw" --mode module \
  -p product=default -p module=entry@default -p buildMode="$BUILD_MODE" \
  "$TASK" --no-daemon "$@"
