#include "vflash/font.h"

/* Original 5x7 bitmaps, indexed by printable ASCII U+0020 through U+007E.
 * Each byte is one row; bit 4 is the leftmost pixel. Space is blank.
 * Keep this table static: panic rendering needs no allocator or font loader. */
static const uint8_t glyphs[95][7] = {
    {0, 0, 0, 0, 0, 0, 0}, /* U+0020 SPACE */
    {4, 4, 4, 4, 4, 0, 4}, /* U+0021 EXCLAMATION MARK */
    {10, 10, 10, 0, 0, 0, 0}, /* U+0022 QUOTATION MARK */
    {10, 31, 10, 10, 31, 10, 10}, /* U+0023 NUMBER SIGN */
    {4, 15, 20, 14, 5, 30, 4}, /* U+0024 DOLLAR SIGN */
    {24, 25, 2, 4, 8, 19, 3}, /* U+0025 PERCENT SIGN */
    {12, 18, 20, 8, 21, 18, 13}, /* U+0026 AMPERSAND */
    {4, 4, 8, 0, 0, 0, 0}, /* U+0027 APOSTROPHE */
    {2, 4, 8, 8, 8, 4, 2}, /* U+0028 LEFT PARENTHESIS */
    {8, 4, 2, 2, 2, 4, 8}, /* U+0029 RIGHT PARENTHESIS */
    {0, 21, 14, 31, 14, 21, 0}, /* U+002A ASTERISK */
    {0, 4, 4, 31, 4, 4, 0}, /* U+002B PLUS SIGN */
    {0, 0, 0, 0, 0, 4, 8}, /* U+002C COMMA */
    {0, 0, 0, 31, 0, 0, 0}, /* U+002D HYPHEN-MINUS */
    {0, 0, 0, 0, 0, 4, 4}, /* U+002E FULL STOP */
    {1, 2, 2, 4, 8, 8, 16}, /* U+002F SOLIDUS */
    {14, 17, 19, 21, 25, 17, 14}, /* U+0030 DIGIT ZERO */
    {4, 12, 4, 4, 4, 4, 14}, /* U+0031 DIGIT ONE */
    {14, 17, 1, 2, 4, 8, 31}, /* U+0032 DIGIT TWO */
    {30, 1, 1, 14, 1, 1, 30}, /* U+0033 DIGIT THREE */
    {2, 6, 10, 18, 31, 2, 2}, /* U+0034 DIGIT FOUR */
    {31, 16, 16, 30, 1, 1, 30}, /* U+0035 DIGIT FIVE */
    {14, 16, 16, 30, 17, 17, 14}, /* U+0036 DIGIT SIX */
    {31, 1, 2, 4, 8, 8, 8}, /* U+0037 DIGIT SEVEN */
    {14, 17, 17, 14, 17, 17, 14}, /* U+0038 DIGIT EIGHT */
    {14, 17, 17, 15, 1, 1, 14}, /* U+0039 DIGIT NINE */
    {0, 4, 4, 0, 4, 4, 0}, /* U+003A COLON */
    {0, 4, 4, 0, 4, 4, 8}, /* U+003B SEMICOLON */
    {2, 4, 8, 16, 8, 4, 2}, /* U+003C LESS-THAN SIGN */
    {0, 0, 31, 0, 31, 0, 0}, /* U+003D EQUALS SIGN */
    {8, 4, 2, 1, 2, 4, 8}, /* U+003E GREATER-THAN SIGN */
    {14, 17, 1, 2, 4, 0, 4}, /* U+003F QUESTION MARK */
    {14, 17, 23, 21, 23, 16, 14}, /* U+0040 COMMERCIAL AT */
    {14, 17, 17, 31, 17, 17, 17}, /* U+0041 LATIN CAPITAL LETTER A */
    {30, 17, 17, 30, 17, 17, 30}, /* U+0042 LATIN CAPITAL LETTER B */
    {14, 17, 16, 16, 16, 17, 14}, /* U+0043 LATIN CAPITAL LETTER C */
    {30, 17, 17, 17, 17, 17, 30}, /* U+0044 LATIN CAPITAL LETTER D */
    {31, 16, 16, 30, 16, 16, 31}, /* U+0045 LATIN CAPITAL LETTER E */
    {31, 16, 16, 30, 16, 16, 16}, /* U+0046 LATIN CAPITAL LETTER F */
    {14, 17, 16, 23, 17, 17, 15}, /* U+0047 LATIN CAPITAL LETTER G */
    {17, 17, 17, 31, 17, 17, 17}, /* U+0048 LATIN CAPITAL LETTER H */
    {14, 4, 4, 4, 4, 4, 14}, /* U+0049 LATIN CAPITAL LETTER I */
    {7, 2, 2, 2, 2, 18, 12}, /* U+004A LATIN CAPITAL LETTER J */
    {17, 18, 20, 24, 20, 18, 17}, /* U+004B LATIN CAPITAL LETTER K */
    {16, 16, 16, 16, 16, 16, 31}, /* U+004C LATIN CAPITAL LETTER L */
    {17, 27, 21, 21, 17, 17, 17}, /* U+004D LATIN CAPITAL LETTER M */
    {17, 25, 25, 21, 19, 19, 17}, /* U+004E LATIN CAPITAL LETTER N */
    {14, 17, 17, 17, 17, 17, 14}, /* U+004F LATIN CAPITAL LETTER O */
    {30, 17, 17, 30, 16, 16, 16}, /* U+0050 LATIN CAPITAL LETTER P */
    {14, 17, 17, 17, 21, 18, 13}, /* U+0051 LATIN CAPITAL LETTER Q */
    {30, 17, 17, 30, 20, 18, 17}, /* U+0052 LATIN CAPITAL LETTER R */
    {15, 16, 16, 14, 1, 1, 30}, /* U+0053 LATIN CAPITAL LETTER S */
    {31, 4, 4, 4, 4, 4, 4}, /* U+0054 LATIN CAPITAL LETTER T */
    {17, 17, 17, 17, 17, 17, 14}, /* U+0055 LATIN CAPITAL LETTER U */
    {17, 17, 17, 17, 17, 10, 4}, /* U+0056 LATIN CAPITAL LETTER V */
    {17, 17, 17, 21, 21, 21, 10}, /* U+0057 LATIN CAPITAL LETTER W */
    {17, 17, 10, 4, 10, 17, 17}, /* U+0058 LATIN CAPITAL LETTER X */
    {17, 17, 10, 4, 4, 4, 4}, /* U+0059 LATIN CAPITAL LETTER Y */
    {31, 1, 2, 4, 8, 16, 31}, /* U+005A LATIN CAPITAL LETTER Z */
    {14, 8, 8, 8, 8, 8, 14}, /* U+005B LEFT SQUARE BRACKET */
    {16, 8, 8, 4, 2, 2, 1}, /* U+005C REVERSE SOLIDUS */
    {14, 2, 2, 2, 2, 2, 14}, /* U+005D RIGHT SQUARE BRACKET */
    {4, 10, 17, 0, 0, 0, 0}, /* U+005E CIRCUMFLEX ACCENT */
    {0, 0, 0, 0, 0, 0, 31}, /* U+005F LOW LINE */
    {8, 4, 2, 0, 0, 0, 0}, /* U+0060 GRAVE ACCENT */
    {0, 0, 14, 1, 15, 17, 15}, /* U+0061 LATIN SMALL LETTER A */
    {16, 16, 30, 17, 17, 17, 30}, /* U+0062 LATIN SMALL LETTER B */
    {0, 0, 14, 16, 16, 17, 14}, /* U+0063 LATIN SMALL LETTER C */
    {1, 1, 15, 17, 17, 17, 15}, /* U+0064 LATIN SMALL LETTER D */
    {0, 0, 14, 17, 31, 16, 14}, /* U+0065 LATIN SMALL LETTER E */
    {6, 8, 8, 28, 8, 8, 8}, /* U+0066 LATIN SMALL LETTER F */
    {0, 0, 15, 17, 15, 1, 14}, /* U+0067 LATIN SMALL LETTER G */
    {16, 16, 30, 17, 17, 17, 17}, /* U+0068 LATIN SMALL LETTER H */
    {4, 0, 12, 4, 4, 4, 14}, /* U+0069 LATIN SMALL LETTER I */
    {2, 0, 6, 2, 2, 18, 12}, /* U+006A LATIN SMALL LETTER J */
    {16, 16, 18, 20, 24, 20, 18}, /* U+006B LATIN SMALL LETTER K */
    {12, 4, 4, 4, 4, 4, 14}, /* U+006C LATIN SMALL LETTER L */
    {0, 0, 26, 21, 21, 21, 21}, /* U+006D LATIN SMALL LETTER M */
    {0, 0, 30, 17, 17, 17, 17}, /* U+006E LATIN SMALL LETTER N */
    {0, 0, 14, 17, 17, 17, 14}, /* U+006F LATIN SMALL LETTER O */
    {0, 0, 30, 17, 30, 16, 16}, /* U+0070 LATIN SMALL LETTER P */
    {0, 0, 15, 17, 15, 1, 1}, /* U+0071 LATIN SMALL LETTER Q */
    {0, 0, 22, 25, 16, 16, 16}, /* U+0072 LATIN SMALL LETTER R */
    {0, 0, 15, 16, 14, 1, 30}, /* U+0073 LATIN SMALL LETTER S */
    {8, 8, 28, 8, 8, 9, 6}, /* U+0074 LATIN SMALL LETTER T */
    {0, 0, 17, 17, 17, 19, 13}, /* U+0075 LATIN SMALL LETTER U */
    {0, 0, 17, 17, 17, 10, 4}, /* U+0076 LATIN SMALL LETTER V */
    {0, 0, 17, 17, 21, 21, 10}, /* U+0077 LATIN SMALL LETTER W */
    {0, 0, 17, 10, 4, 10, 17}, /* U+0078 LATIN SMALL LETTER X */
    {0, 0, 17, 17, 15, 1, 14}, /* U+0079 LATIN SMALL LETTER Y */
    {0, 0, 31, 2, 4, 8, 31}, /* U+007A LATIN SMALL LETTER Z */
    {2, 4, 4, 8, 4, 4, 2}, /* U+007B LEFT CURLY BRACKET */
    {4, 4, 4, 4, 4, 4, 4}, /* U+007C VERTICAL LINE */
    {8, 4, 4, 2, 4, 4, 8}, /* U+007D RIGHT CURLY BRACKET */
    {0, 0, 9, 22, 0, 0, 0}, /* U+007E TILDE */
};

const uint8_t *font_glyph(unsigned char character)
{
    if (character < 0x20 || character > 0x7E) character = '?';
    return glyphs[character - 0x20];
}
