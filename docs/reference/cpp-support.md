# Freestanding C++17 Support in V.Flash SDK

The V.Flash SDK supports freestanding C++17 development alongside ARM assembly and C. The environment targets the Zevio 1020 (ARM926EJ-S, ARMv5TEJ) with soft-float ABI, no operating system (bare-metal), and a bounded SDRAM heap.

## Toolchain & Compilation Flags

C++ compilation is powered by `arm-none-eabi-g++` via `cmake/toolchain-vflash.cmake`.

Each target compiled through `vflash_add_program` uses the following architecture and dialect flags:

| Flag | Purpose |
|---|---|
| `-mcpu=arm926ej-s -marm` | Target ARMv5TEJ architecture in 32-bit ARM state |
| `-mfloat-abi=soft` | Software floating-point emulation (no VFP/FPU hardware) |
| `-mlittle-endian -mabi=aapcs -fno-short-enums` | Standard ARM EABI conventions |
| `-std=c++17` | ISO C++17 language standard |
| `-ffreestanding -fno-builtin` | Freestanding environment without hosted C library |
| `-fno-exceptions` | Exception handling disabled (zero unwinding table overhead) |
| `-fno-rtti` | Run-time type information disabled (no typeinfo metadata) |
| `-ffunction-sections -fdata-sections` | Dead-code elimination via linker `--gc-sections` |
| `-fno-unwind-tables -fno-asynchronous-unwind-tables` | Exclude unwinding frame tables (`.ARM.exidx`) |

## Global Constructors & Runtime Initialization

C++ global objects and priority constructors are executed before `vflash_main` is invoked.

1. **Linker Script Layout**:
   Both `startup/firmware-disc.ld` and `startup/flashem-direct.ld` define:
   - `.preinit_array`: `__preinit_array_start` to `__preinit_array_end`
   - `.init_array`: `__init_array_start` to `__init_array_end`, sorted by priority (`SORT_BY_INIT_PRIORITY`) followed by default constructors.
   - `.fini_array`: `__fini_array_start` to `__fini_array_end`
2. **Startup Sequence (`startup/startup.S`)**:
   - Stack pointer initialized to top of RAM (`__stack_top`).
   - `.bss` cleared to zero.
   - `vflash_panic_init` installs hardware crash vectors.
   - Pre-initialization array constructors called sequentially.
   - Initialization array constructors called in ascending priority order.
   - `vflash_main()` called.

## Minimal C++ ABI (`src/runtime/cxxabi.cpp`)

The SDK provides the minimal runtime ABI required for freestanding C++17:

- `__cxa_pure_virtual()`: Invokes `vflash_panic(VFLASH_PANIC_FATAL, ...)` if a pure-virtual function is called.
- `__cxa_deleted_virtual()`: Panic handler for deleted virtual calls.
- `__cxa_guard_acquire()`, `__cxa_guard_release()`, `__cxa_guard_abort()`: Function-local static variable initialization guards (ARM 32-bit EABI format).
- `__dso_handle`, `__cxa_atexit()`, `__aeabi_atexit()`: Exit-handler stubs for global destructor registration. In this bare-metal environment, execution loops indefinitely or resets on exit.

## Memory Management & Dynamic Allocation

The SDK's runtime allocator (`src/runtime/alloc.c`) provides a bounded bump-and-freelist allocator operating in SDRAM between `__heap_start` and `__heap_end`.

C++ operators are implemented in `src/runtime/cxxabi.cpp`:

- Standard scalar: `operator new(size_t)`, `operator delete(void*) noexcept`
- Array variants: `operator new[](size_t)`, `operator delete[](void*) noexcept`
- Sized deallocation (C++14/17): `operator delete(void*, size_t) noexcept`, `operator delete[](void*, size_t) noexcept`
- Nothrow variants: `operator new(size_t, const std::nothrow_t&) noexcept`, etc.
- Aligned allocation (C++17): `operator new(size_t, std::align_val_t)`, `operator delete(void*, std::align_val_t) noexcept`, and sized/array aligned variants.

## Standard Library Availability Matrix

### Supported Features
- **Core language**: Classes, single/multiple inheritance, polymorphic virtual functions, abstract interfaces, templates, variadic templates, fold expressions, lambda expressions, `constexpr`, `if constexpr`, `static_assert`.
- **`<cstddef>`**: `size_t`, `ptrdiff_t`, `nullptr_t`, `std::byte`.
- **`<cstdint>`**: Fixed-width integers (`uint8_t`..`uint64_t`, `intptr_t`, `uintptr_t`).
- **`<type_traits>`**: All type traits and template metaprogramming utilities.
- **`<utility>`**: `std::move`, `std::forward`, `std::pair`, `std::swap`, `std::integer_sequence`.
- **`<limits>`**: `std::numeric_limits`.
- **`<initializer_list>`**: Brace initialization lists.
- **`<new>`**: Placement new, `std::nothrow`, `std::align_val_t`.
- **`<array>`**: Fixed-size stack/static arrays (`std::array`).
- **`<algorithm>`**: Non-allocating algorithms (`std::sort`, `std::find`, `std::min`, `std::max`, `std::clamp`, `std::copy`, `std::fill`).
- **`<memory>`**: `std::unique_ptr`.
- **SDL2 Integration**: Seamless integration with the SDK's native SDL2 port (`<SDL.h>`, `<sdl2_vflash/SDL_vflash.h>`).

### Unsupported Features
- **Exceptions (`-fno-exceptions`)**: `throw`, `catch`, `try`, `<exception>`, `<stdexcept>`.
- **RTTI (`-fno-rtti`)**: `typeid`, `dynamic_cast`.
- **Hosted I/O (`<iostream>`, `<fstream>`)**: Requires hosted POSIX/C library syscalls (`read`, `write`, `open`, `close`). Use native `vflash/fs.h` or `SDL_RWops`.
- **Multithreading (`<thread>`, `<mutex>`)**: Requires an OS thread scheduler.
- **System Clocks (`<chrono>`)**: System clocks require POSIX `gettimeofday`. Use `vflash_clock_ticks()` or `SDL_GetTicks64()`.
- **Dynamic Strings and Vectors (`<string>`, `<vector>`)**: Standard libstdc++ implementations pull in exception throwing paths (`__throw_bad_alloc`, etc.) unless custom allocators/stubs are used.

