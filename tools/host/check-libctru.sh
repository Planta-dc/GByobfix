#!/usr/bin/env bash
#
# Check that the 3DS port compiles against the current libctru.
#
# This does not link anything and it does not produce a .3dsx: it reads
# every source file of the 3DS port with the SDK's own headers and reports
# what does not fit. It is the check that can be run anywhere, and it is
# what catches an SDK that has moved on - the port was written against
# libctru v0.6, whose file system and graphics interfaces no longer exist.
#
# The SDK's headers are meant for the ARM compiler, and a few of them do
# things that only make sense on a 32 bit target, so the check is a syntax
# check with the pointer sized casts turned into warnings. That is enough
# to find the API drift, which is what this is for.
#
# Usage: tools/host/check-libctru.sh [path to libctru's include directory]

set -u

HERE="$(cd "$(dirname "$0")" && pwd)"
ROOT="$(cd "$HERE/../.." && pwd)"
PLATFORM="$ROOT/platform/3ds"
COMMON="$ROOT/platform/common"
SHIM="$HERE/newlib-shim"

# Where the SDK's headers are. The first argument wins, then the usual
# place devkitPro puts them, then the copy in the repository's own tree.
INCLUDE_DIR="${1:-}"
if [ -z "$INCLUDE_DIR" ] && [ -n "${DEVKITPRO:-}" ]; then
    INCLUDE_DIR="$DEVKITPRO/libctru/include"
fi
if [ -z "$INCLUDE_DIR" ]; then
    echo "check-libctru: no libctru headers found."
    echo "  Pass the path to libctru's include directory, or set DEVKITPRO."
    exit 2
fi
if [ ! -d "$INCLUDE_DIR" ]; then
    echo "check-libctru: $INCLUDE_DIR is not a directory."
    exit 2
fi

echo "Using the SDK headers in $INCLUDE_DIR"

FLAGS=(
    -fsyntax-only
    -fpermissive
    -std=gnu++11
    -include 3ds/types.h
    -DVERSION_STRING='"check"'
    -D__3DS__
    -DARM11
    -D_3DS
    "-I$SHIM"
    "-I$INCLUDE_DIR"
    "-I$PLATFORM/include"
    "-I$PLATFORM"
    "-I$COMMON/include"
)

failed=0
checked=0

for source in "$PLATFORM"/*.cpp "$COMMON"/*.cpp; do
    name="${source#"$ROOT"/}"
    checked=$((checked + 1))
    if ! output=$(g++ "${FLAGS[@]}" "$source" 2>&1 | grep -E "error"); then
        :
    else
        if [ -n "$output" ]; then
            echo "FAIL $name"
            echo "$output" | head -5
            failed=$((failed + 1))
        fi
    fi
done

# The sound transport is C, not C++.
name="platform/3ds/audio.c"
checked=$((checked + 1))
if ! output=$(gcc -fsyntax-only -std=gnu99 \
        -I"$SHIM" -I"$INCLUDE_DIR" -I"$PLATFORM/include" -I"$COMMON/include" \
        -D__3DS__ -DARM11 -D_3DS "$PLATFORM/audio.c" 2>&1 | grep -E "error"); then
    :
else
    if [ -n "$output" ]; then
        echo "FAIL $name"
        echo "$output" | head -5
        failed=$((failed + 1))
    fi
fi

if [ "$failed" -ne 0 ]; then
    echo "FAIL: $failed of $checked files do not fit the SDK."
    exit 1
fi

echo "PASS: $checked files fit the SDK."
