#ifndef VFLASH_FONT_H
#define VFLASH_FONT_H
#include <stdint.h>
/* All 95 printable ASCII characters (U+0020..U+007E), a subset of UTF-8.
 * Seven rows, five pixels per row; bit 4 is the leftmost pixel.
 * Unsupported bytes, including control and non-ASCII bytes, use '?'. */
const uint8_t *font_glyph(unsigned char character);
#endif
