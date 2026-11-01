#!/usr/bin/env bash
#
# Build the 3DS port.
#
# The build runs inside devkitPro's own image, which has the compiler, the
# SDK and 3dsxtool in it. That is a lot more reliable than installing a
# package manager and a keyring on a machine that is thrown away after
# every run, and it means the build is the same build on every machine.
#
#     bash docker/ci-build.sh            build everything
#     bash docker/ci-build.sh 3dsx       build only the .3dsx
#
# It builds the three things the port can be shipped as - the .3dsx that
# the Homebrew launcher runs, the .cia that a title manager installs, and
# the .3ds card image - and leaves them next to the Makefile.
#
# makerom, which the .cia and the .3ds need, is not in devkitPro's image
# and is not in its package manager either, so it is built from its source
# in the same container.

set -euo pipefail

HERE="$(cd "$(dirname "$0")" && pwd)"
ROOT="$(cd "$HERE/.." && pwd)"
IMAGE="${DEVKITPRO_IMAGE:-devkitpro/devkitarm:latest}"
TASK="${1:-all}"

if ! command -v docker >/dev/null 2>&1; then
    echo "ci-build: docker is needed to run the build in devkitPro's image."
    exit 2
fi

echo "Building '$TASK' in $IMAGE"

docker run --rm \
    -v "$ROOT:/src" \
    -w /src \
    "$IMAGE" \
    bash docker/in-image.sh "$TASK"

# The container runs as root, so everything it wrote into the checkout
# belongs to root. Give it back, or the next step cannot write next to it.
if [ "$(id -u)" = "0" ]; then
    chown -R "$(id -u):$(id -g)" "$ROOT" 2>/dev/null || true
fi

case "$TASK" in
    all)
        ls -l "$ROOT/platform/3ds"/gameyob.cia "$ROOT/platform/3ds"/gameyob.3ds
        ;;
    cia)
        ls -l "$ROOT/platform/3ds"/gameyob.cia
        ;;
    3ds)
        ls -l "$ROOT/platform/3ds"/gameyob.3ds
        ;;
esac
case "$TASK" in
    all|3dsx)
        ls -l "$ROOT/platform/3ds"/gameyob.3dsx
        ;;
esac
