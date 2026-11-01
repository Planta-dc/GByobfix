#!/usr/bin/env bash
#
# The part of the build that runs inside devkitPro's container.
#
# The container has the compiler, the SDK and the tools that turn an ELF
# file into a .3dsx. It does not have makerom, which the .cia and the .3ds
# need, so that is built from its source here as well.
#
# This script is run by docker/ci-build.sh, which is what a person should
# run. It takes one task at a time so that a CI run can say which part of
# the build broke:
#
#     makerom     build makerom, which packages the .cia and the .3ds
#     sdk-check   check that the port still fits the SDK's headers
#     link-check  check that every symbol the port uses has a home
#     3dsx        build the .3dsx
#     cia         build the .cia
#     3ds         build the .3ds
#
# It expects the repository to be the working directory and to be writable.

set -euo pipefail

export DEVKITPRO="${DEVKITPRO:-/opt/devkitpro}"
export DEVKITARM="${DEVKITARM:-$DEVKITPRO/devkitARM}"

# Where Project_CTR is cloned to, and where makerom is expected to end up.
MAKEROM_SRC="tools/makerom"
MAKEROM_BIN="$MAKEROM_SRC/bin/makerom"

# Build makerom from its source. It is not in devkitPro's image and it is
# not in its package manager either, and the published binaries are built
# against a newer C library than the one in the image.
build_makerom() {
    if [ -x "$MAKEROM_BIN" ]; then
        echo "makerom is already built"
        return
    fi

    echo "Building makerom"
    rm -rf "$MAKEROM_SRC"
    git clone --depth 1 https://github.com/3DSGuy/Project_CTR.git "$MAKEROM_SRC"

    # Project_CTR has a makefile at the top and one in each program
    # directory. The top one only knows how to build the programs (all,
    # progs, clean); the makefile that knows how to build the
    # dependencies (mbedtls, blz and yaml) is the one in makerom/.
    #
    # Running "make -C tools/makerom deps" - which is what this used to
    # do - asks the top makefile for a target it does not have, and make
    # answers "No rule to make target 'deps'" and exits 2. That is the
    # whole of the CI failure: not a compiler error, a wrong directory.
    #
    # The dependencies (mbedtls, blz and yaml) are in the tree, and they
    # are plain makefiles, so no cmake is needed.
    make -C "$MAKEROM_SRC/makerom" deps
    make -C "$MAKEROM_SRC/makerom" program

    # The packaging Makefile looks for makerom in tools/makerom/bin, and
    # Project_CTR builds it in tools/makerom/makerom/bin.
    mkdir -p "$MAKEROM_SRC/bin"
    cp "$MAKEROM_SRC/makerom/bin/makerom" "$MAKEROM_BIN"
    "$MAKEROM_BIN" 2>&1 | head -n 1 || true
}

check_sdk() {
    # Does the port still fit the SDK? This is the check that catches an
    # SDK that has moved on.
    tools/host/check-libctru.sh "$DEVKITPRO/libctru/include"
}

check_link() {
    # Does every symbol the port uses have a home? A link is what notices
    # a function that is declared in a header and defined nowhere, and
    # this does the part of that which does not need the ARM compiler.
    #
    # It runs here rather than on the host because it needs the SDK's
    # headers, and because the container is the only place the SDK is.
    tools/host/check-link.sh "$DEVKITPRO/libctru/include"
}

build_3dsx() {
    echo "Building the .3dsx"
    make -C platform/3ds OUTPUT_FORMAT=3dsx
}

build_cia() {
    echo "Building the .cia"
    make -C platform/3ds cia
}

build_3ds() {
    echo "Building the .3ds"
    make -C platform/3ds 3ds
}

task="${1:-all}"

case "$task" in
    makerom)   build_makerom ;;
    sdk-check) check_sdk ;;
    link-check) check_link ;;
    3dsx)      build_3dsx ;;
    cia)       build_cia ;;
    3ds)       build_3ds ;;
    all)
        build_makerom
        check_sdk
        check_link
        build_3dsx
        build_cia
        build_3ds
        ;;
    *)
        echo "in-image: unknown task '$task'" >&2
        exit 2
        ;;
esac
