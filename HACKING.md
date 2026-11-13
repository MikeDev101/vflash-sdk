# V.Flash SDK Coding Style Guidelines

These guidelines adapt Chocolate Doom's coding philosophy for the V.Flash SDK.
They apply to all SDK headers, platform drivers, graphics code, and examples
(excluding third-party vendor code such as `src/graphics/picojpeg.c` and
`external/SDL2/`).

These guidelines are based on [Chocolate Doom's coding philosophy.](https://raw.githubusercontent.com/chocolate-doom/chocolate-doom/master/HACKING.md)

## Formatting & Layout

- **Indentation:** 4 spaces per level. Do not use tab characters.
- **Line length:** 80 columns maximum.
- **Trailing whitespace:** Do not leave trailing whitespace at the end of lines.
- **Braces:** Always use `{` and `}` for `if`, `else`, `do`, `while`, and `for`
  blocks, even for single-statement bodies. Place opening and closing braces on
  their own lines (Allman/BSD style).
- **Casts:** Put a space after the closing parenthesis of a type cast:
  `(uint32_t) value` or `(uint16_t *) ptr`.
- **Pointers:** Place the `*` next to the variable or parameter name, not the
  type: `uint8_t *buf`, not `uint8_t* buf`.

## Comments & Documentation

- **Comment syntax:** Use C++-style `//` comments exclusively. Do not use
  `/* ... */` block comments.
- **Public & internal APIs:** Document every type definition, macro/enum
  constant, function declaration, parameter reference, and return value:

```c
//
// Short description of what the function does.
//
// param_a: Description of parameter reference or value.
//
// Returns 0 on success, or a negative error code on failure.
//
int vflash_example_func(void *param_a);
```

## Naming & Types

- **Public functions:** Prefix public SDK symbols with `vflash_<subsystem>_`
  (or subsystem prefix where established, e.g., `draw_`, `font_`).
- **Static functions & globals:** Keep file-local helpers and state `static`.
- **Types:** Always `typedef` structs and enums and end type names with `_t`
  in lowercase snake_case (e.g., `vflash_audio_config_t`).
- **Variables:** Use `snake_case` for variable and struct member names. Do not
  use Hungarian notation.
- **Fixed-width types:** Use ISO C99 fixed-width integer types (`int8_t`,
  `uint8_t`, `int16_t`, `uint16_t`, `int32_t`, `uint32_t`, `uint64_t`) instead
  of `long`.

## Control Flow & Safety

- **No `goto`:** Do not use `goto`.
- **Bounded memory & buffers:** Avoid unbounded string or memory operations;
  validate buffer capacities, alignment, and pointer arguments at API
  boundaries.
