#!/usr/bin/env bash
#
# Check that every symbol the 3DS port needs is actually there.
#
# The port has been compiled and syntax-checked against the SDK's headers,
# but it has never been linked, and a link is the only thing that notices a
# function that is declared in a header and defined nowhere. This does the
# part of a link that can be done without an ARM compiler: it compiles the
# port's own files into objects, asks which symbols are still missing, and
# then says where each one is supposed to come from.
#
#   tools/host/check-link.sh [path to libctru's include directory]
#
# The result is a list of symbols with no home. An empty list means the
# port's sources are self-consistent: everything they call exists, either
# in the port itself, in libctru, in the C library, in newlib, or is made by
# the build from a data file.
#
# It is not a link. It cannot be: the C library here is the host's and not
# newlib's, and the ARM compiler is not here. What it does check is that
# every name the port uses is a name something defines.

set -u

HERE="$(cd "$(dirname "$0")" && pwd)"
ROOT="$(cd "$HERE/../.." && pwd)"
PLATFORM="$ROOT/platform/3ds"
COMMON="$ROOT/platform/common"
SHIM="$HERE/newlib-shim"

INCLUDE_DIR="${1:-}"
if [ -z "$INCLUDE_DIR" ] && [ -n "${DEVKITPRO:-}" ]; then
    INCLUDE_DIR="$DEVKITPRO/libctru/include"
fi
if [ -z "$INCLUDE_DIR" ]; then
    echo "check-link: no libctru headers found."
    echo "  Pass the path to libctru's include directory, or set DEVKITPRO."
    exit 2
fi
if [ ! -d "$INCLUDE_DIR" ]; then
    echo "check-link: $INCLUDE_DIR is not a directory."
    exit 2
fi

# Where libctru's sources are, so that a symbol declared in a header can be
# checked against the code that implements it.
LIBCTRU_SRC=""
for candidate in "$INCLUDE_DIR/../source" "$INCLUDE_DIR/../../source" \
                 "$INCLUDE_DIR/../libctru/source"; do
    if [ -d "$candidate" ]; then
        LIBCTRU_SRC="$(cd "$candidate" && pwd)"
        break
    fi
done

WORK="$(mktemp -d)"
trap 'rm -rf "$WORK"' EXIT

# The flags are the ones the real build uses, as far as the host can take
# them.
#
# -fno-rtti and -fno-exceptions are what the build passes. Without them the
# host compiler asks for the personality routine, which the 3DS build does
# not have.
#
# -fno-pic and -fno-pie are what the build gets by not asking for position
# independent code: it uses -mword-relocations, which addresses globals
# directly. The host's compiler would otherwise default to a position
# independent executable and ask for a global offset table, which the 3DS
# link script does not define.
FLAGS=(
    -fpermissive
    -std=gnu++11
    -fno-rtti
    -fno-exceptions
    -fno-pic
    -fno-pie
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

# Compile the port. The files are compiled, not just parsed, so that the
# objects carry the real undefined symbols.
objects=()
failed=0
for source in "$PLATFORM"/*.cpp "$COMMON"/*.cpp; do
    name="$(basename "$source" .cpp)"
    object="$WORK/$name.o"
    if ! g++ "${FLAGS[@]}" -c "$source" -o "$object" 2>"$WORK/$name.err"; then
        echo "FAIL $name does not compile"
        head -5 "$WORK/$name.err"
        failed=$((failed + 1))
        continue
    fi
    objects+=("$object")
done

name="audio"
if ! gcc -std=gnu99 \
        -I"$SHIM" -I"$INCLUDE_DIR" -I"$PLATFORM/include" -I"$COMMON/include" \
        -D__3DS__ -DARM11 -D_3DS -c "$PLATFORM/audio.c" -o "$WORK/audio.o" \
        2>"$WORK/audio.err"; then
    echo "FAIL audio.c does not compile"
    head -5 "$WORK/audio.err"
    failed=$((failed + 1))
else
    objects+=("$WORK/audio.o")
fi

if [ "$failed" -ne 0 ]; then
    echo "FAIL: $failed files do not compile."
    exit 1
fi

# nm prints a name, a type, and then a value and a size, but the size is
# left out for an undefined symbol, and it prints a header line for each
# archive member and each object file it is given. The type letter in the
# second field is the only thing that tells a symbol line from a header
# line, so that is what is looked for.
#
# The names are used as they are. A C++ name is mangled in the object file
# and in the library that defines it, and both sides mangle it the same
# way, so comparing the mangled names is both simpler and stricter than
# demangling them and comparing the results.
symbol_names() {
    nm "$@" --format=posix 2>/dev/null \
        | awk 'NF >= 2 && $2 ~ /^[A-Za-z]$/ { print $1 }'
}

# The symbols the port defines itself.
symbol_names --defined-only --extern-only "${objects[@]}" | sort -u \
    > "$WORK/defined.txt"

# The symbols it still wants.
symbol_names --undefined-only "${objects[@]}" | sort -u > "$WORK/undefined.txt"

# The symbols the C library and the C++ library provide. The host's own
# libraries are a close enough stand-in for newlib's to say whether a name
# is a library symbol at all.
#
# The shared libraries are read with -D, which is the dynamic symbol table:
# that is the list of names a program can actually call. Without it, nm
# reports "no symbols" for a stripped library, and every C library function
# looks missing. The static archives are read as well, because the C++
# library keeps most of its templates there and exports them only weakly.
#
# The files are looked for rather than written down, because where they
# live depends on the distribution and on the version of the compiler.
{
    for library in libstdc++.so.6 libc.so.6 libm.so.6 libgcc_s.so.1; do
        found="$(find /usr/lib /lib -name "$library" -type f 2>/dev/null | head -1)"
        [ -n "$found" ] || continue
        nm -D --defined-only --format=posix "$found" 2>/dev/null
    done
    for archive in libc.a libm.a libstdc++.a libgcc.a; do
        found="$(find /usr/lib /lib -name "$archive" -type f 2>/dev/null | head -1)"
        [ -n "$found" ] || continue
        nm --defined-only --format=posix "$found" 2>/dev/null
    done
    # The startup files. They are always linked in, and they are where
    # __dso_handle and __TMC_LIST__ live.
    for object in crt1.o crti.o crtn.o crtbegin.o crtend.o; do
        found="$(find /usr/lib /lib -name "$object" -type f 2>/dev/null | head -1)"
        [ -n "$found" ] || continue
        nm --defined-only --format=posix "$found" 2>/dev/null
    done
} | awk 'NF >= 2 && $2 ~ /^[A-Za-z]$/ { print $1 }' \
  | sed 's/@@.*//; s/@.*//' \
  | sort -u > "$WORK/library.txt"

# The symbols newlib provides.
#
# These are not on the host, so they cannot be read out of a library here.
# What can be done is to ask the compiler: a name that the newlib stand-in
# headers declare is a name newlib provides, and a name they do not
# declare is a name nothing provides. The probe is a tiny file that takes
# the address of the name; if the headers do not declare it, the compiler
# says so.
#
# The headers are the ones that stand in for newlib and not for the host,
# so the probe includes only those.
: > "$WORK/newlib.txt"
while IFS= read -r symbol; do
    case "$symbol" in
        [A-Za-z_]*) ;;
        *) continue ;;
    esac
    cat > "$WORK/probe.cpp" <<EOF
#include <sys/iosupport.h>
#include <unistd.h>
void *arena_probe(void) { return (void *)($symbol); }
EOF
    if g++ -w -fpermissive -I"$SHIM" -c "$WORK/probe.cpp" \
            -o "$WORK/probe.o" 2>/dev/null; then
        echo "$symbol" >> "$WORK/newlib.txt"
    fi
done < "$WORK/undefined.txt"

# The symbols the build makes out of a data file. bin2s turns a file into
# an array and an array that ends where it starts, both named after the
# file with the dots turned into underscores.
: > "$WORK/generated.txt"
for data in "$PLATFORM"/data/*; do
    [ -f "$data" ] || continue
    base="$(basename "$data")"
    symbol="$(echo "$base" | tr '.' '_')"
    echo "$symbol" >> "$WORK/generated.txt"
    echo "${symbol}_end" >> "$WORK/generated.txt"
done

# The symbols libctru provides: the ones its sources define.
#
# The C and C++ sources are read for their definitions, which is why a
# function returning a pointer is looked for as well as one returning
# nothing. The assembly sources are read for the names they export, which
# is where the system call wrappers live.
if [ -n "$LIBCTRU_SRC" ]; then
    {
        grep -rhoE '^[[:space:]]*[A-Za-z_][A-Za-z0-9_]*[[:space:]]*\*?[[:space:]]*[A-Za-z_][A-Za-z0-9_]*[[:space:]]*\(' \
            "$LIBCTRU_SRC" --include='*.c' --include='*.cpp' 2>/dev/null \
            | sed -E 's/.*[ *]([A-Za-z_][A-Za-z0-9_]*)[[:space:]]*\($/\1/'
        grep -rhoE '(SVC_BEGIN|BEGIN_ASM_FUNC)[[:space:]]+[A-Za-z_][A-Za-z0-9_]*' \
            "$LIBCTRU_SRC" --include='*.s' 2>/dev/null | awk '{ print $2 }'
        grep -rhoE '^[[:space:]]*\.global[[:space:]]+[A-Za-z_][A-Za-z0-9_]*' \
            "$LIBCTRU_SRC" --include='*.s' 2>/dev/null | awk '{ print $2 }'
    } | grep -E '^[A-Za-z_][A-Za-z0-9_]*$' | sort -u > "$WORK/libctru.txt"
else
    : > "$WORK/libctru.txt"
fi

missing=0
> "$WORK/missing.txt"
while IFS= read -r symbol; do
    [ -n "$symbol" ] || continue

    # The port's own.
    if grep -qxF "$symbol" "$WORK/defined.txt"; then
        continue
    fi

    # The C library and the C++ library.
    if grep -qxF "$symbol" "$WORK/library.txt"; then
        continue
    fi

    # newlib.
    if grep -qxF "$symbol" "$WORK/newlib.txt"; then
        continue
    fi

    # The build's own output, made from a data file.
    if grep -qxF "$symbol" "$WORK/generated.txt"; then
        continue
    fi

    # libctru.
    if grep -qxF "$symbol" "$WORK/libctru.txt"; then
        continue
    fi

    echo "MISSING $symbol" >> "$WORK/missing.txt"
    missing=$((missing + 1))
done < "$WORK/undefined.txt"

if [ "$missing" -ne 0 ]; then
    echo "FAIL: $missing symbols have no home."
    cat "$WORK/missing.txt"
    exit 1
fi

echo "PASS: every symbol the port uses is defined by the port, by libctru, by the C library or by newlib."
