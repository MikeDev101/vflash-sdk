#include "font.h"

/* Original compact 5x7 glyphs for the demo, plus a visible fallback.
 * Bitmaps are constant executable data; no font library or filesystem. */
static const uint8_t glyphs[][7] = {
    {0, 0, 0, 0, 0, 0, 0},                       /* space */
    {17, 17, 17, 31, 17, 17, 17},                /* H */
    {0, 0, 14, 17, 31, 16, 14},                  /* e */
    {12, 4, 4, 4, 4, 4, 14},                    /* l */
    {0, 0, 14, 17, 17, 17, 14},                 /* o */
    {0, 0, 0, 0, 0, 4, 8},                      /* comma */
    {17, 17, 17, 21, 21, 21, 10},                /* W */
    {0, 0, 22, 25, 16, 16, 16},                 /* r */
    {1, 1, 15, 17, 17, 17, 15},                 /* d */
    {4, 4, 4, 4, 4, 0, 4},                      /* ! */
    {14, 17, 1, 2, 4, 0, 4},                    /* ? */
    {14, 17, 19, 21, 25, 17, 14},                /* 0 */
    {4, 12, 4, 4, 4, 4, 14},                    /* 1 */
    {14, 17, 1, 2, 4, 8, 31},                   /* 2 */
    {30, 1, 1, 14, 1, 1, 30},                   /* 3 */
    {2, 6, 10, 18, 31, 2, 2},                   /* 4 */
    {31, 16, 16, 30, 1, 1, 30},                 /* 5 */
    {14, 16, 16, 30, 17, 17, 14},               /* 6 */
    {31, 1, 2, 4, 8, 8, 8},                     /* 7 */
    {14, 17, 17, 14, 17, 17, 14},               /* 8 */
    {14, 17, 17, 15, 1, 1, 14},                 /* 9 */
    {31, 16, 16, 30, 16, 16, 16},               /* F */
    {30, 17, 17, 30, 16, 16, 16},               /* P */
    {15, 16, 16, 14, 1, 1, 30},                 /* S */
    {14, 17, 16, 16, 16, 17, 14},               /* C */
    {17, 17, 17, 17, 17, 17, 14},               /* U */
    {0, 0, 26, 21, 21, 21, 21},                 /* m */
    {0, 0, 15, 16, 14, 1, 30},                  /* s */
    {0, 4, 4, 0, 4, 4, 0},                      /* : */
    {0, 0, 0, 0, 0, 4, 4},                      /* . */
};

const uint8_t *font_glyph(unsigned char character)
{
    uint32_t index;
    if (character >= '0' && character <= '9') return glyphs[11 + character - '0'];
    switch (character) {
    case ' ': index = 0; break;
    case 'H': index = 1; break;
    case 'e': index = 2; break;
    case 'l': index = 3; break;
    case 'o': index = 4; break;
    case ',': index = 5; break;
    case 'W': index = 6; break;
    case 'r': index = 7; break;
    case 'd': index = 8; break;
    case '!': index = 9; break;
    case 'F': index = 21; break;
    case 'P': index = 22; break;
    case 'S': index = 23; break;
    case 'C': index = 24; break;
    case 'U': index = 25; break;
    case 'm': index = 26; break;
    case 's': index = 27; break;
    case ':': index = 28; break;
    case '.': index = 29; break;
    default: index = 10; break;
    }
    return glyphs[index];
}
