#ifndef VFLASH_FONT_H
#define VFLASH_FONT_H
#include <stdint.h>
/* Seven rows, five pixels per row, most significant used bit is bit 4. */
const uint8_t *font_glyph(unsigned char character);
#endif
