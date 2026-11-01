/* A stand-in for the header the build generates from data/default_font.bin,
 * for checking the 3DS port's syntax on a host. The real one is made by
 * bin2s at build time.
 *
 * This is a copy of what bin2s writes, not a sketch of it. The real header
 * says the symbols are const uint8_t arrays and gives the size as a
 * constant, so a source file that reads the font as bytes and one that
 * asks for its length both compile the same way here as they do on the
 * console.
 */
#ifndef _DEFAULT_FONT_BIN_H_
#define _DEFAULT_FONT_BIN_H_
#pragma once
#include <stddef.h>
#include <stdint.h>

extern const uint8_t default_font_bin[];
extern const uint8_t default_font_bin_end[];
#if __cplusplus >= 201103L
static constexpr size_t default_font_bin_size=8192;
#else
static const size_t default_font_bin_size=8192;
#endif
#endif
