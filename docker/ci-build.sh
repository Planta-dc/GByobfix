#!/usr/bin/env bash
#
# Build the 3DS port.
#
# This is what the continuous integration runs, and it is also the quickest
# way to find out whether the port still builds after a change to the SDK:
#
#     bash docker/ci-build.sh
#
# It builds the three things the port can be shipped as - the .3dsx that
# the Homebrew launcher runs, the .cia that a title manager installs, and
# the .3ds card image - and leaves them next to the Makefile.
#
# The devkitPro tools have to be installed first; see
# .github/scripts/install-3ds-packaging-tools.sh.

set -euo pipefail

HERE="$(cd "$(dirname "$0")" && pwd)"
ROOT="$(cd "$HERE/.." && pwd)"
PLATFORM="$ROOT/platform/3ds"

export DEVKITPRO="${DEVKITPRO:-/opt/devkitpro}"
export DEVKITARM="${DEVKITARM:-$DEVKITPRO/devkitARM}"

if [ ! -d "$DEVKITARM" ]; then
    echo "ci-build: no devkitARM in $DEVKITARM."
    echo "  Install it with .github/scripts/install-3ds-packaging-tools.sh"
    exit 2
fi

# makerom is not part of devkitARM. It is built from source by the install
# script; if it is somewhere else, point MAKEROM at it.
if [ -z "${MAKEROM:-}" ]; then
    MAKEROM="$ROOT/tools/makerom/bin/makerom"
    export MAKEROM
fi

JOBS="$(nproc 2>/dev/null || echo 2)"

echo "Building the .3dsx"
make -C "$PLATFORM" -j"$JOBS" OUTPUT_FORMAT=3dsx

echo "Building the .cia"
make -C "$PLATFORM" cia

echo "Building the .3ds"
make -C "$PLATFORM" 3ds

ls -l "$PLATFORM"/gameyob.3dsx "$PLATFORM"/gameyob.cia "$PLATFORM"/gameyob.3ds
