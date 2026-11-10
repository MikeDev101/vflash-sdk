#include "vflash/panic.h"
#include "vflash/font.h"

static void text(uint16_t *pixels, int x, int y, const char *value, int scale)
{
    for (; *value && x < 320; ++value, x += 6 * scale) {
        unsigned char c = (unsigned char)*value;
        if (c >= 'a' && c <= 'z') c = (unsigned char)(c - 'a' + 'A');
        const uint8_t *glyph = font_glyph(c);
        for (int row = 0; row < 7; ++row) for (int column = 0; column < 5; ++column)
            if (glyph[row] & (16u >> column))
                for (int py = 0; py < scale; ++py) for (int px = 0; px < scale; ++px) {
                    int dx = x + column * scale + px, dy = y + row * scale + py;
                    if (dx >= 0 && dx < 320 && dy >= 0 && dy < 240) pixels[dy * 320 + dx] = 0x7FFF;
                }
    }
}

static void hex(uint16_t *pixels, int y, const char *name, uint32_t value)
{
    char number[9];
    static const char digits[] = "0123456789ABCDEF";
    for (uint32_t i = 0; i < 8; ++i) number[i] = digits[value >> (28 - 4 * i) & 15];
    number[8] = 0;
    text(pixels, 12, y, name, 2);
    text(pixels, 96, y, number, 2);
}

void vflash_panic_draw(uint16_t *pixels, const vflash_crash_record_t *record)
{
    for (uint32_t i = 0; i < 320 * 240; ++i) pixels[i] = 0x6000;
    text(pixels, 12, 8, ":(", 2);
    const char *reason = "MANUAL FATAL";
    if (record->reason == VFLASH_PANIC_ASSERT) reason = "ASSERTION FAILED";
    if (record->reason == VFLASH_PANIC_UNDEFINED) reason = "UNDEFINED INSTRUCTION";
    if (record->reason == VFLASH_PANIC_SWI) reason = "SOFTWARE INTERRUPT";
    text(pixels, 12, 32, reason, 2);
    hex(pixels, 60, (record->flags & VFLASH_CRASH_PC_VALID) ? "PC" :
        ((record->flags & VFLASH_CRASH_HARDWARE) ? "PC ?" : "RETURN"), record->fault_pc);
    hex(pixels, 82, "CPSR", record->cpsr);
    hex(pixels, 104, "SPSR", record->spsr);
    hex(pixels, 126, "SP", record->origin_sp);
    hex(pixels, 148, "LR", record->exception_lr);
    text(pixels, 12, 184, "SYSTEM HALTED", 2);
    text(pixels, 12, 216, record->message, 1);
}
