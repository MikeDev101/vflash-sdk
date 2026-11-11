#include <stddef.h>
#include <stdint.h>
#include "vflash/clock.h"

/* Linker symbols for heap bounds from linker script */
extern char __heap_start;
extern char __heap_end;

/* 8-byte aligned memory block header */
typedef struct BlockHeader {
    size_t size; /* payload size; bit 0 = 1 if allocated, 0 if free */
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

    /* Search free list */
    BlockHeader *curr = s_free_list;
    while (curr) {
        if (curr->size >= aligned_size) {
            /* Check if we can split this block */
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

            /* Unlink curr from free list */
            if (curr->prev) curr->prev->next = curr->next;
            else s_free_list = curr->next;
            if (curr->next) curr->next->prev = curr->prev;

            curr->size |= 1u; /* Mark allocated */
            curr->next = NULL;
            curr->prev = NULL;
            s_heap_used += aligned_size;
            s_alloc_count++;
            return (void *)(curr + 1);
        }
        curr = curr->next;
    }

    /* Bump allocate from heap */
    if (!s_heap_current) {
        s_heap_current = &__heap_start;
    }

    size_t total_needed = sizeof(BlockHeader) + aligned_size;
    if (s_heap_current + total_needed > &__heap_end) {
        /* Bounded heap exhausted */
        return NULL;
    }

    BlockHeader *block = (BlockHeader *)s_heap_current;
    s_heap_current += total_needed;

    block->size = aligned_size | 1u; /* Mark allocated */
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
        /* Double free or invalid block */
        return;
    }

    block->size &= ~1u; /* Mark free */
    if (s_heap_used >= block->size) {
        s_heap_used -= block->size;
    }
    if (s_alloc_count > 0) {
        s_alloc_count--;
    }

    /* Add to front of free list */
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
    if (nmemb != 0 && total / nmemb != size) return NULL; /* Overflow */
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

size_t vflash_sdl2_get_heap_used(void)
{
    return s_heap_used;
}

size_t vflash_sdl2_get_alloc_count(void)
{
    return s_alloc_count;
}

size_t vflash_sdl_get_heap_used(void)
{
    return s_heap_used;
}

size_t vflash_sdl_get_alloc_count(void)
{
    return s_alloc_count;
}

/* Builtin memory functions for freestanding GCC runtime */
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

/* Freestanding timer support backed by V.Flash 10 kHz hardware clock */
static uint16_t s_last_ticks = 0;
static uint64_t s_accumulated_ticks = 0;
static int s_timer_initialized = 0;

void SDL_TicksInit(void)
{
    if (s_timer_initialized) return;
    vflash_clock_init();
    s_last_ticks = vflash_clock_ticks();
    s_accumulated_ticks = 0;
    s_timer_initialized = 1;
}

void SDL_TicksQuit(void)
{
    s_timer_initialized = 0;
}

static void update_ticks(void)
{
    if (!s_timer_initialized) {
        SDL_TicksInit();
    }
    uint16_t now = vflash_clock_ticks();
    uint16_t delta = (uint16_t)(now - s_last_ticks);
    s_accumulated_ticks += delta;
    s_last_ticks = now;
}

uint64_t SDL_GetTicks64(void)
{
    update_ticks();
    /* 10 kHz clock: 10 ticks per millisecond */
    return s_accumulated_ticks / 10u;
}

uint64_t SDL_GetPerformanceCounter(void)
{
    update_ticks();
    return s_accumulated_ticks;
}

uint64_t SDL_GetPerformanceFrequency(void)
{
    return 10000u; /* 10 kHz hardware timer */
}

void SDL_Delay(uint32_t ms)
{
    uint64_t start = SDL_GetTicks64();
    while ((SDL_GetTicks64() - start) < (uint64_t)ms) {
        /* busy wait */
    }
}

/* Process termination for freestanding runtime */
void _exit(int status)
{
    (void)status;
    while (1) {
        /* halt */
    }
}

void exit(int status)
{
    _exit(status);
}

/* Standard pseudo-random and time helpers for upstream test suites */
static unsigned long s_next_rand = 1;

int rand(void)
{
    s_next_rand = s_next_rand * 1103515245u + 12345u;
    return (int)((s_next_rand / 65536u) % 32768u);
}

void srand(unsigned int seed)
{
    s_next_rand = seed;
}

unsigned int time(void *t)
{
    (void)t;
    return (unsigned int)SDL_GetTicks64();
}

/* Virtual asset loading for embedded test assets and V.Flash native disc filesystem */
#include "SDL_rwops.h"
#include "SDL_error.h"
#include "icon_bmp.h"
#include <vflash/fs.h>

extern SDL_RWops *SDL_RWFromFile_Upstream(const char *file, const char *mode);

static Sint64 SDLCALL vflash_rwops_size(SDL_RWops *context)
{
    if (!context) return -1;
    int fd = (int)(intptr_t)context->hidden.unknown.data1;
    return (Sint64)vflash_fs_size(fd);
}

static Sint64 SDLCALL vflash_rwops_seek(SDL_RWops *context, Sint64 offset, int whence)
{
    if (!context) return -1;
    int fd = (int)(intptr_t)context->hidden.unknown.data1;
    int fs_whence = SEEK_SET;
    if (whence == RW_SEEK_SET) fs_whence = SEEK_SET;
    else if (whence == RW_SEEK_CUR) fs_whence = SEEK_CUR;
    else if (whence == RW_SEEK_END) fs_whence = SEEK_END;
    else return -1;
    return (Sint64)vflash_fs_seek(fd, (int32_t)offset, fs_whence);
}

static size_t SDLCALL vflash_rwops_read(SDL_RWops *context, void *ptr, size_t size, size_t maxnum)
{
    if (!context || !ptr || size == 0 || maxnum == 0) return 0;
    int fd = (int)(intptr_t)context->hidden.unknown.data1;
    size_t total_bytes = size * maxnum;
    int32_t read_bytes = vflash_fs_read(fd, ptr, (uint32_t)total_bytes);
    if (read_bytes <= 0) return 0;
    return (size_t)read_bytes / size;
}

static size_t SDLCALL vflash_rwops_write(SDL_RWops *context, const void *ptr, size_t size, size_t num)
{
    (void)context; (void)ptr; (void)size; (void)num;
    SDL_SetError("V.Flash disc filesystem is read-only");
    return 0;
}

static int SDLCALL vflash_rwops_close(SDL_RWops *context)
{
    if (context) {
        int fd = (int)(intptr_t)context->hidden.unknown.data1;
        if (fd >= 0) {
            vflash_fs_close(fd);
        }
        SDL_FreeRW(context);
    }
    return 0;
}

SDL_RWops *SDL_RWFromFile(const char *file, const char *mode)
{
    (void)mode;
    if (file) {
        int fd = vflash_fs_open(file);
        if (fd >= 0) {
            SDL_RWops *rwops = SDL_AllocRW();
            if (rwops) {
                rwops->size = vflash_rwops_size;
                rwops->seek = vflash_rwops_seek;
                rwops->read = vflash_rwops_read;
                rwops->write = vflash_rwops_write;
                rwops->close = vflash_rwops_close;
                rwops->type = SDL_RWOPS_UNKNOWN;
                rwops->hidden.unknown.data1 = (void *)(intptr_t)fd;
                return rwops;
            }
            vflash_fs_close(fd);
        }

        size_t len = 0;
        while (file[len]) len++;
        if (len >= 8 &&
            file[len-8] == 'i' && file[len-7] == 'c' && file[len-6] == 'o' && file[len-5] == 'n' &&
            file[len-4] == '.' && file[len-3] == 'b' && file[len-2] == 'm' && file[len-1] == 'p') {
            return SDL_RWFromConstMem(g_icon_bmp, sizeof(g_icon_bmp));
        }
    }
    return SDL_RWFromFile_Upstream(file, mode);
}

/* Single-core ARMv5TEJ atomic primitives (no SMP) */
unsigned int __atomic_exchange_4(volatile void *ptr, unsigned int val, int memmodel)
{
    (void)memmodel;
    unsigned int old = *(volatile unsigned int *)ptr;
    *(volatile unsigned int *)ptr = val;
    return old;
}

unsigned int __sync_lock_test_and_set_4(volatile void *ptr, unsigned int val)
{
    unsigned int old = *(volatile unsigned int *)ptr;
    *(volatile unsigned int *)ptr = val;
    return old;
}
