#include <vflash/fs.h>
#include <vflash/cd.h>
#include <stddef.h>

typedef struct {
    bool used;
    uint32_t extent_lba;
    uint32_t file_size;
    uint32_t file_pos;
} vflash_file_handle_t;

static vflash_file_handle_t s_files[VFLASH_FS_MAX_OPEN_FILES];
static uint8_t s_cache[VFLASH_CD_SECTOR_SIZE] __attribute__((aligned(4)));
static uint8_t s_root_cache[VFLASH_CD_SECTOR_SIZE] __attribute__((aligned(4)));
static uint32_t s_cached_lba = 0xFFFFFFFFu;

static uint32_t s_root_lba = 0;
static uint32_t s_root_size = 0;
static bool s_root_cached = false;
static bool s_fs_ready = false;

static void fs_memcpy(void *dst, const void *src, uint32_t n)
{
    uint8_t *d = (uint8_t *)dst;
    const uint8_t *s = (const uint8_t *)src;
    while (n--) {
        *d++ = *s++;
    }
}

static bool char_eq_ci(char a, char b)
{
    if (a >= 'a' && a <= 'z') a = (char)(a - 'a' + 'A');
    if (b >= 'a' && b <= 'z') b = (char)(b - 'a' + 'A');
    return a == b;
}

static int fs_load_sector(uint32_t lba)
{
    if (s_cached_lba == lba) return 0;
    if (s_root_cached && lba == s_root_lba) {
        fs_memcpy(s_cache, s_root_cache, VFLASH_CD_SECTOR_SIZE);
        s_cached_lba = lba;
        return 0;
    }
    if (vflash_cd_read_sector(lba, s_cache) != 1) {
        s_cached_lba = 0xFFFFFFFFu;
        return -1;
    }
    s_cached_lba = lba;
    return 0;
}

int vflash_fs_init(void)
{
    if (s_fs_ready) {
        return 0;
    }
    s_cached_lba = 0xFFFFFFFFu;
    s_root_cached = false;
    s_fs_ready = false;
    for (int i = 0; i < VFLASH_FS_MAX_OPEN_FILES; ++i) {
        s_files[i].used = false;
    }

    if (vflash_cd_init() != 0) {
        return -1;
    }

    /* Read Primary Volume Descriptor at LBA 16 */
    if (fs_load_sector(16) != 0) {
        return -2;
    }

    /* PVD type must be 1, identifier 'CD001' */
    if (s_cache[0] != 1 ||
        s_cache[1] != 'C' || s_cache[2] != 'D' ||
        s_cache[3] != '0' || s_cache[4] != '0' || s_cache[5] != '1') {
        return -3;
    }

    /* Root directory record at offset 156 */
    const uint8_t *root_rec = s_cache + 156;
    s_root_lba = (uint32_t)root_rec[2] |
                 ((uint32_t)root_rec[3] << 8) |
                 ((uint32_t)root_rec[4] << 16) |
                 ((uint32_t)root_rec[5] << 24);
    s_root_size = (uint32_t)root_rec[10] |
                  ((uint32_t)root_rec[11] << 8) |
                  ((uint32_t)root_rec[12] << 16) |
                  ((uint32_t)root_rec[13] << 24);

    /* Pre-cache the first root directory sector to avoid repeated backward seeks */
    if (vflash_cd_read_sector(s_root_lba, s_root_cache) == 1) {
        s_root_cached = true;
    }

    s_fs_ready = true;
    return 0;
}

static bool match_iso_name(const char *name_on_disc, uint8_t len_on_disc, const char *token, uint32_t tok_len)
{
    /* Check if token contains a semicolon version delimiter */
    bool tok_has_ver = false;
    for (uint32_t i = 0; i < tok_len; ++i) {
        if (token[i] == ';') {
            tok_has_ver = true;
            break;
        }
    }

    /* Effective length on disc ignoring version suffix if token has none */
    uint32_t disc_effective_len = len_on_disc;
    if (!tok_has_ver) {
        for (uint32_t i = 0; i < len_on_disc; ++i) {
            if (name_on_disc[i] == ';') {
                disc_effective_len = i;
                break;
            }
        }
    }

    /* Exact match */
    if (disc_effective_len == tok_len) {
        bool match = true;
        for (uint32_t i = 0; i < tok_len; ++i) {
            if (!char_eq_ci(name_on_disc[i], token[i])) {
                match = false;
                break;
            }
        }
        if (match) return true;
    }

    /*
     * ISO 9660 Level 1 8.3 truncation fallback:
     * If the requested filename exceeds 8 characters before the extension dot,
     * check if the disc entry has the first 8 characters and matching extension.
     * E.g. requested "bubblesystem.wav" matches disc "BUBBLESY.WAV".
     */
    const char *tok_dot = NULL;
    for (int32_t i = (int32_t)tok_len - 1; i >= 0; --i) {
        if (token[i] == '.') {
            tok_dot = &token[i];
            break;
        }
    }
    const char *disc_dot = NULL;
    for (int32_t i = (int32_t)disc_effective_len - 1; i >= 0; --i) {
        if (name_on_disc[i] == '.') {
            disc_dot = &name_on_disc[i];
            break;
        }
    }

    if (tok_dot && disc_dot) {
        uint32_t tok_base_len = (uint32_t)(tok_dot - token);
        uint32_t disc_base_len = (uint32_t)(disc_dot - name_on_disc);
        uint32_t tok_ext_len = tok_len - tok_base_len;
        uint32_t disc_ext_len = disc_effective_len - disc_base_len;

        if (tok_base_len > 8u && disc_base_len == 8u && tok_ext_len == disc_ext_len) {
            /* Check 8-char base prefix */
            for (uint32_t i = 0; i < 8u; ++i) {
                if (!char_eq_ci(name_on_disc[i], token[i])) return false;
            }
            /* Check extension including dot */
            for (uint32_t i = 0; i < tok_ext_len; ++i) {
                if (!char_eq_ci(disc_dot[i], tok_dot[i])) return false;
            }
            return true;
        }
    }

    return false;
}

int vflash_fs_open(const char *path)
{
    if (!path) return -1;
    if (!s_fs_ready) {
        if (vflash_fs_init() != 0) return -1;
    }

    /* Find a free descriptor slot */
    int fd = -1;
    for (int i = 0; i < VFLASH_FS_MAX_OPEN_FILES; ++i) {
        if (!s_files[i].used) {
            fd = i;
            break;
        }
    }
    if (fd < 0) return -1;

    /* Skip leading slashes */
    while (*path == '/' || *path == '\\') path++;
    if (!*path) return -1;

    uint32_t cur_dir_lba = s_root_lba;
    uint32_t cur_dir_size = s_root_size;

    const char *p = path;
    while (*p) {
        /* Extract next path segment */
        const char *token_start = p;
        while (*p && *p != '/' && *p != '\\') p++;
        uint32_t tok_len = (uint32_t)(p - token_start);

        /* Skip trailing separator */
        while (*p == '/' || *p == '\\') p++;
        bool is_last_segment = (*p == '\0');

        /* Search for token in cur_dir */
        uint32_t dir_sectors = (cur_dir_size + VFLASH_CD_SECTOR_SIZE - 1u) >> 11;
        bool found = false;
        uint32_t next_lba = 0;
        uint32_t next_size = 0;
        uint8_t next_flags = 0;

        for (uint32_t s = 0; s < dir_sectors && !found; ++s) {
            if (fs_load_sector(cur_dir_lba + s) != 0) return -1;

            uint32_t off = 0;
            while (off < VFLASH_CD_SECTOR_SIZE) {
                uint8_t len_dr = s_cache[off];
                if (len_dr == 0) {
                    /* Padding to end of sector */
                    break;
                }
                if (off + len_dr > VFLASH_CD_SECTOR_SIZE) break;

                uint8_t len_fi = s_cache[off + 32];
                const char *name_ptr = (const char *)(s_cache + off + 33);

                if (match_iso_name(name_ptr, len_fi, token_start, tok_len)) {
                    next_lba = (uint32_t)s_cache[off + 2] |
                               ((uint32_t)s_cache[off + 3] << 8) |
                               ((uint32_t)s_cache[off + 4] << 16) |
                               ((uint32_t)s_cache[off + 5] << 24);
                    next_size = (uint32_t)s_cache[off + 10] |
                                ((uint32_t)s_cache[off + 11] << 8) |
                                ((uint32_t)s_cache[off + 12] << 16) |
                                ((uint32_t)s_cache[off + 13] << 24);
                    next_flags = s_cache[off + 25];
                    found = true;
                    break;
                }
                off += len_dr;
            }
        }

        if (!found) return -1;

        if (is_last_segment) {
            /* Reject opening a directory as a regular file */
            if (next_flags & 2u) return -1;
            s_files[fd].used = true;
            s_files[fd].extent_lba = next_lba;
            s_files[fd].file_size = next_size;
            s_files[fd].file_pos = 0;
            return fd;
        } else {
            /* Must be a directory */
            if (!(next_flags & 2u)) return -1;
            cur_dir_lba = next_lba;
            cur_dir_size = next_size;
        }
    }

    return -1;
}

int32_t vflash_fs_read(int fd, void *buf, uint32_t count)
{
    if (fd < 0 || fd >= VFLASH_FS_MAX_OPEN_FILES || !s_files[fd].used || !buf) {
        return -1;
    }

    uint32_t file_pos = s_files[fd].file_pos;
    uint32_t file_size = s_files[fd].file_size;
    if (file_pos >= file_size) return 0; /* EOF */

    uint32_t remaining_in_file = file_size - file_pos;
    if (count > remaining_in_file) {
        count = remaining_in_file;
    }
    if (count == 0) return 0;

    uint32_t bytes_left = count;
    uint8_t *dst = (uint8_t *)buf;
    uint32_t extent = s_files[fd].extent_lba;

    while (bytes_left > 0) {
        uint32_t sec_idx = file_pos >> 11;
        uint32_t sec_off = file_pos & 2047u;
        uint32_t cur_lba = extent + sec_idx;

        if (sec_off == 0 && bytes_left >= VFLASH_CD_SECTOR_SIZE && (((uintptr_t)dst & 3u) == 0)) {
            /* Fast path: burst multi-sector DMA read into 4-byte aligned destination */
            uint32_t full_sectors = bytes_left >> 11;
            if (vflash_cd_read_sectors(cur_lba, full_sectors, dst) != (int)full_sectors) {
                s_files[fd].file_pos = file_pos;
                return (count > bytes_left) ? (int32_t)(count - bytes_left) : -1;
            }
            uint32_t burst_bytes = full_sectors << 11;
            dst += burst_bytes;
            file_pos += burst_bytes;
            bytes_left -= burst_bytes;
        } else {
            /* Cached read for partial or unaligned chunks */
            if (fs_load_sector(cur_lba) != 0) {
                s_files[fd].file_pos = file_pos;
                return (count > bytes_left) ? (int32_t)(count - bytes_left) : -1;
            }
            uint32_t chunk = VFLASH_CD_SECTOR_SIZE - sec_off;
            if (chunk > bytes_left) chunk = bytes_left;
            fs_memcpy(dst, s_cache + sec_off, chunk);
            dst += chunk;
            file_pos += chunk;
            bytes_left -= chunk;
        }
    }

    s_files[fd].file_pos = file_pos;
    return (int32_t)count;
}

int32_t vflash_fs_seek(int fd, int32_t offset, int whence)
{
    if (fd < 0 || fd >= VFLASH_FS_MAX_OPEN_FILES || !s_files[fd].used) {
        return -1;
    }

    int32_t new_pos = 0;
    switch (whence) {
    case SEEK_SET:
        new_pos = offset;
        break;
    case SEEK_CUR:
        new_pos = (int32_t)s_files[fd].file_pos + offset;
        break;
    case SEEK_END:
        new_pos = (int32_t)s_files[fd].file_size + offset;
        break;
    default:
        return -1;
    }

    if (new_pos < 0 || new_pos > (int32_t)s_files[fd].file_size) {
        return -1;
    }

    s_files[fd].file_pos = (uint32_t)new_pos;
    return new_pos;
}

int32_t vflash_fs_tell(int fd)
{
    if (fd < 0 || fd >= VFLASH_FS_MAX_OPEN_FILES || !s_files[fd].used) {
        return -1;
    }
    return (int32_t)s_files[fd].file_pos;
}

int32_t vflash_fs_size(int fd)
{
    if (fd < 0 || fd >= VFLASH_FS_MAX_OPEN_FILES || !s_files[fd].used) {
        return -1;
    }
    return (int32_t)s_files[fd].file_size;
}

int vflash_fs_close(int fd)
{
    if (fd < 0 || fd >= VFLASH_FS_MAX_OPEN_FILES || !s_files[fd].used) {
        return -1;
    }
    s_files[fd].used = false;
    return 0;
}
