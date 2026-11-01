#!/usr/bin/env bash
#
# Install the tools the 3DS port is built with.
#
# Three things are needed and none of them are in this repository:
#
#   devkitARM and libctru   the compiler and the SDK, from devkitPro's
#                            package manager;
#   3dsxtool                turns the executable into a .3dsx, and is in
#                            the same package manager;
#   makerom                 turns the executable into a .cia or a .3ds, and
#                            is not in the package manager at all, so it is
#                            built from its source.
#
# The parts that need root ask for it themselves, so this can be run as
# whoever is doing the build.

set -euo pipefail

HERE="$(cd "$(dirname "$0")" && pwd)"
ROOT="$(cd "$HERE/../.." && pwd)"

export DEVKITPRO="${DEVKITPRO:-/opt/devkitpro}"

echo "Installing devkitPro's package manager"
curl -fsSL https://apt.devkitpro.org/install-devkitpro-pacman | sudo bash

echo "Installing the compiler and the SDK"
sudo dkp-pacman -Sy --noconfirm 3ds-dev 3dsxtool

# makerom is not in the package manager. It is built from its source, which
# needs a compiler and make; the runner has both.
if [ ! -x "$ROOT/tools/makerom/bin/makerom" ]; then
    echo "Building makerom"
    rm -rf "$ROOT/tools/makerom"
    git clone --depth 1 https://github.com/3DSGuy/Project_CTR.git "$ROOT/tools/makerom"
    make -C "$ROOT/tools/makerom" deps
    make -C "$ROOT/tools/makerom"
fi

echo "The tools are in place:"
ls -l "$ROOT/tools/makerom/bin/makerom"
ls -d "$DEVKITPRO"/devkitARM "$DEVKITPRO"/libctru/include
