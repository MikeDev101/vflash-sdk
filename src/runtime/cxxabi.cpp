#include <cstddef>
#include <cstdint>
#include <new>
#include "vflash/panic.h"

extern "C" {
    void *malloc(size_t size);
    void free(void *ptr);

    void *__dso_handle = nullptr;

    void __cxa_pure_virtual() {
        vflash_panic(VFLASH_PANIC_FATAL, "pure virtual method called");
    }

    void __cxa_deleted_virtual() {
        vflash_panic(VFLASH_PANIC_FATAL, "deleted virtual method called");
    }

    int __cxa_atexit(void (*)(void *), void *, void *) {
        return 0;
    }

    int __aeabi_atexit(void *, void (*)(void *), void *) {
        return 0;
    }

    int __cxa_guard_acquire(int *guard) {
        return (*reinterpret_cast<volatile uint8_t *>(guard) & 1) ? 0 : 1;
    }

    void __cxa_guard_release(int *guard) {
        *reinterpret_cast<volatile uint8_t *>(guard) = 1;
    }

    void __cxa_guard_abort(int *guard) {
        (void)guard;
    }
}

static void *aligned_alloc_helper(size_t size, size_t alignment) {
    if (alignment <= 8) {
        return malloc(size);
    }
    size_t total = size + alignment + sizeof(void *);
    void *raw = malloc(total);
    if (!raw) return nullptr;
    uintptr_t raw_addr = reinterpret_cast<uintptr_t>(raw);
    uintptr_t aligned_addr = (raw_addr + sizeof(void *) + (alignment - 1)) & ~(alignment - 1);
    *(reinterpret_cast<void **>(aligned_addr) - 1) = raw;
    return reinterpret_cast<void *>(aligned_addr);
}

static void aligned_free_helper(void *ptr, size_t alignment) {
    if (!ptr) return;
    if (alignment <= 8) {
        free(ptr);
    } else {
        void *raw = *(reinterpret_cast<void **>(ptr) - 1);
        free(raw);
    }
}

void *operator new(size_t size) {
    void *ptr = malloc(size);
    if (!ptr) {
        vflash_panic(VFLASH_PANIC_FATAL, "out of memory");
    }
    return ptr;
}

void *operator new[](size_t size) {
    return ::operator new(size);
}

void operator delete(void *ptr) noexcept {
    free(ptr);
}

void operator delete[](void *ptr) noexcept {
    ::operator delete(ptr);
}

void operator delete(void *ptr, size_t) noexcept {
    ::operator delete(ptr);
}

void operator delete[](void *ptr, size_t) noexcept {
    ::operator delete[](ptr);
}

void *operator new(size_t size, const std::nothrow_t &) noexcept {
    return malloc(size);
}

void *operator new[](size_t size, const std::nothrow_t &) noexcept {
    return malloc(size);
}

void operator delete(void *ptr, const std::nothrow_t &) noexcept {
    free(ptr);
}

void operator delete[](void *ptr, const std::nothrow_t &) noexcept {
    ::operator delete(ptr);
}

void *operator new(size_t size, std::align_val_t al) {
    void *ptr = aligned_alloc_helper(size, static_cast<size_t>(al));
    if (!ptr) {
        vflash_panic(VFLASH_PANIC_FATAL, "out of memory (aligned)");
    }
    return ptr;
}

void *operator new[](size_t size, std::align_val_t al) {
    return ::operator new(size, al);
}

void operator delete(void *ptr, std::align_val_t al) noexcept {
    aligned_free_helper(ptr, static_cast<size_t>(al));
}

void operator delete[](void *ptr, std::align_val_t al) noexcept {
    ::operator delete(ptr, al);
}

void operator delete(void *ptr, size_t, std::align_val_t al) noexcept {
    ::operator delete(ptr, al);
}

void operator delete[](void *ptr, size_t, std::align_val_t al) noexcept {
    ::operator delete[](ptr, al);
}

void *operator new(size_t size, std::align_val_t al, const std::nothrow_t &) noexcept {
    return aligned_alloc_helper(size, static_cast<size_t>(al));
}

void *operator new[](size_t size, std::align_val_t al, const std::nothrow_t &) noexcept {
    return aligned_alloc_helper(size, static_cast<size_t>(al));
}

void operator delete(void *ptr, std::align_val_t al, const std::nothrow_t &) noexcept {
    aligned_free_helper(ptr, static_cast<size_t>(al));
}

void operator delete[](void *ptr, std::align_val_t al, const std::nothrow_t &) noexcept {
    ::operator delete(ptr, al);
}

