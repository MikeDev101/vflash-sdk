# Printable ASCII font

The SDK includes all 95 printable ASCII characters, U+0020 through U+007E:
space, uppercase and lowercase English letters, digits and punctuation. Space
is intentionally blank. These characters use the same single-byte representation
in UTF-8. This is ASCII coverage, not a general Unicode font or UTF-8 decoder.
Control bytes and non-ASCII bytes fall back to `?`; a multibyte UTF-8 character
therefore produces a fallback for each byte. Newlines and tabs are not layout
commands in the current single-line text API.

## Source and format

`src/graphics/font.c` defines `glyphs[95][7]`, indexed by character minus 0x20, and
`font_glyph()` exposes each bitmap. Every glyph uses seven row bytes with five
pixels per row. Bit 4 is the leftmost pixel, bit 0 the rightmost. A set bit draws
in the foreground color; an unset bit leaves the background untouched.

For example, `31` is binary `11111`, a solid five-pixel row. The letter `H` uses
`{17, 17, 17, 31, 17, 17, 17}`. Comments give every glyph's ASCII code and name.
The glyph data occupy 665 bytes, with no runtime font loading or allocation.
All glyphs from the earlier partial font retain their original bitmaps.

`draw_text()` in `src/graphics/draw.c` uses a six-pixel horizontal advance, including the
one-pixel gap, multiplied by its integer scale. Its supported scale range is
1 through 16. Lowercase is rendered as lowercase. The panic renderer has its own
bounded pixel loop in `src/runtime/panic_render.c`; it intentionally uppercases messages
before glyph lookup and retains its existing screen layout.

## Preview every glyph

```sh
python3 tools/host_renderer.py --font-atlas build/font-preview.ppm
```

The atlas uses the SDK's actual C font and drawing code compiled on the host.
Each glyph is labeled with its hexadecimal ASCII code; `20` is blank space.
This preview is host-generated, not an emulator screenshot. It requires a native
C compiler (`cc`, or `VFLASH_HOST_CC`) and Python, as with the other drawing tests.

The font tests check all 95 distinct glyphs, blank space, row bounds, lowercase
shapes and fallback bytes. Existing drawing and panic renderer tests continue to
exercise clipping and pixel output.
