#!/usr/bin/env bash
#
# Build the 3DS port.
#
# The build runs inside devkitPro's own image, which has the compiler, the
# SDK and 3dsxtool in it. That is a lot more reliable than installing a
# package manager and a keyring on a machine that is thrown away after
# every run, and it means the build is the same build on every machine.
#
#     bash docker/ci-build.sh
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

if ! command -v docker >/dev/null 2>&1; then
    echo "ci-build: docker is needed to run the build in devkitPro's image."
    exit 2
fi

echo "Building in $IMAGE"

docker run --rm \
    -v "$ROOT:/src" \
    -w /src \
    "$IMAGE" \
    bash -c '
        set -euo pipefail

        export DEVKITPRO=/opt/devkitpro
        export DEVKITARM=/opt/devkitpro/devkitARM

        # makerom is not in the image and not in the package manager.
        if [ ! -x tools/makerom/bin/makerom ]; then
            echo "Building makerom"
            apt-get update
            apt-get install -y --no-install-recommends cmake
            rm -rf tools/makerom
            git clone --depth 1 https://github.com/3DSGuy/Project_CTR.git tools/makerom
            make -C tools/makerom deps
            make -C tools/makerom
        fi

        # Does the port still fit the SDK? This is the check that catches
        # an SDK that has moved on.
        tools/host/check-libctru.sh "$DEVKITPRO/libctru/include"

        echo "Building the .3dsx"
        make -C platform/3ds -j"$(nproc)" OUTPUT_FORMAT=3dsx

        echo "Building the .cia"
        make -C platform/3ds cia

        echo "Building the .3ds"
        make -C platform/3ds 3ds
    '

ls -l "$ROOT/platform/3ds"/gameyob.3dsx "$ROOT/platform/3ds"/gameyob.cia \
      "$ROOT/platform/3ds"/gameyob.3ds
