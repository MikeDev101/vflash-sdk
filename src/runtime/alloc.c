#include <stddef.h>
#include <stdint.h>

// Linker symbols for heap bounds from linker script
extern char __heap_start;
extern char __heap_end;

// 8-byte aligned memory block header
typedef struct BlockHeader {
    size_t size; // payload size; bit 0 = 1 if allocated, 0 if free
    struct BlockHeader *next;
    struct BlockHeader *prev;
} BlockHeader;

#define ALIGN8(x) (((x) + 7u) & ~7u)

static char *s_heap_current = NULL;
static BlockHeader *s_free_list = NULL;
static size_t s_heap_used = 0;
static size_t s_alloc_count = 0;

void *malloc(size_t size)
{
    if (size == 0) return NULL;
    size_t aligned_size = ALIGN8(size);

    // Search free list
    BlockHeader *curr = s_free_list;
    while (curr) {
        if (curr->size >= aligned_size) {
            // Check if we can split this block
            if (curr->size >= aligned_size + sizeof(BlockHeader) + 16u) {
                size_t remaining = curr->size - aligned_size - sizeof(BlockHeader);
                BlockHeader *split = (BlockHeader *)((char *)(curr + 1) + aligned_size);
                split->size = remaining;
                split->next = curr->next;
                split->prev = curr;
                if (curr->next) curr->next->prev = split;
                curr->next = split;
                curr->size = aligned_size;
            }

            // Unlink curr from free list
            if (curr->prev) curr->prev->next = curr->next;
            else s_free_list = curr->next;
            if (curr->next) curr->next->prev = curr->prev;

            curr->size |= 1u; // Mark allocated
            curr->next = NULL;
            curr->prev = NULL;
            s_heap_used += aligned_size;
            s_alloc_count++;
            return (void *)(curr + 1);
        }
        curr = curr->next;
    }

    // Bump allocate from heap
    if (!s_heap_current) {
        s_heap_current = &__heap_start;
    }

    size_t total_needed = sizeof(BlockHeader) + aligned_size;
    if (s_heap_current + total_needed > &__heap_end) {
        // Bounded heap exhausted
        return NULL;
    }

    BlockHeader *block = (BlockHeader *)s_heap_current;
    s_heap_current += total_needed;

    block->size = aligned_size | 1u; // Mark allocated
    block->next = NULL;
    block->prev = NULL;
    s_heap_used += aligned_size;
    s_alloc_count++;

    return (void *)(block + 1);
}

void free(void *ptr)
{
    if (!ptr) return;
    BlockHeader *block = ((BlockHeader *)ptr) - 1;
    if (!(block->size & 1u)) {
        // Double free or invalid block
        return;
    }

    block->size &= ~1u; // Mark free
    if (s_heap_used >= block->size) {
        s_heap_used -= block->size;
    }
    if (s_alloc_count > 0) {
        s_alloc_count--;
    }

    // Add to front of free list
    block->next = s_free_list;
    block->prev = NULL;
    if (s_free_list) {
        s_free_list->prev = block;
    }
    s_free_list = block;
}

void *calloc(size_t nmemb, size_t size)
{
    size_t total = nmemb * size;
    if (nmemb != 0 && total / nmemb != size) return NULL; // Overflow
    void *ptr = malloc(total);
    if (!ptr) return NULL;
    uint8_t *p = (uint8_t *)ptr;
    for (size_t i = 0; i < total; ++i) {
        p[i] = 0;
    }
    return ptr;
}

void *realloc(void *ptr, size_t size)
{
    if (!ptr) return malloc(size);
    if (size == 0) {
        free(ptr);
        return NULL;
    }
    BlockHeader *block = ((BlockHeader *)ptr) - 1;
    size_t old_size = block->size & ~1u;
    size_t aligned_new = ALIGN8(size);
    if (aligned_new <= old_size) {
        return ptr;
    }

    void *new_ptr = malloc(size);
    if (!new_ptr) return NULL;
    uint8_t *src = (uint8_t *)ptr;
    uint8_t *dst = (uint8_t *)new_ptr;
    for (size_t i = 0; i < old_size; ++i) {
        dst[i] = src[i];
    }
    free(ptr);
    return new_ptr;
}

size_t vflash_heap_used(void)
{
    return s_heap_used;
}

size_t vflash_alloc_count(void)
{
    return s_alloc_count;
}

// Builtin memory functions for freestanding GCC runtime
void *memcpy(void *dst, const void *src, size_t n)
{
    uint8_t *d = (uint8_t *)dst;
    const uint8_t *s = (const uint8_t *)src;
    while (n--) *d++ = *s++;
    return dst;
}

void *memset(void *s, int c, size_t n)
{
    uint8_t *p = (uint8_t *)s;
    uint8_t val = (uint8_t)c;
    while (n--) *p++ = val;
    return s;
}

void *memmove(void *dst, const void *src, size_t n)
{
    uint8_t *d = (uint8_t *)dst;
    const uint8_t *s = (const uint8_t *)src;
    if (d < s) {
        while (n--) *d++ = *s++;
    } else {
        d += n;
        s += n;
        while (n--) *--d = *--s;
    }
    return dst;
}

int memcmp(const void *s1, const void *s2, size_t n)
{
    const uint8_t *p1 = (const uint8_t *)s1;
    const uint8_t *p2 = (const uint8_t *)s2;
    while (n--) {
        if (*p1 != *p2) return (int)*p1 - (int)*p2;
        p1++;
        p2++;
    }
    return 0;
}

