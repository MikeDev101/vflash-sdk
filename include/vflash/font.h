#ifndef VFLASH_FONT_H
#define VFLASH_FONT_H

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

//
// Look up the 5x7 monochrome bitmap glyph for an ASCII character.
// Covers printable ASCII (0x20..0x7E); unsupported bytes fall back to '?'.
// Each of the 7 returned bytes holds 5 pixels with bit 4 as the leftmost.
//
// character: Byte value of the character to look up.
//
// Returns a pointer to a 7-byte static glyph row array.
//
const uint8_t *font_glyph(unsigned char character);

#ifdef __cplusplus
}
#endif

#endif // VFLASH_FONT_H
