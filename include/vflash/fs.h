#ifndef VFLASH_FS_H
#define VFLASH_FS_H

#include <stdbool.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

#define VFLASH_FS_MAX_OPEN_FILES 8

#ifndef SEEK_SET
#define SEEK_SET 0
#endif
#ifndef SEEK_CUR
#define SEEK_CUR 1
#endif
#ifndef SEEK_END
#define SEEK_END 2
#endif

/*
 * Initialize the native ISO 9660 filesystem.
 * Initializes the CD driver, mounts the Primary Volume Descriptor,
 * and sets up root directory descriptors.
 * Returns 0 on success, negative error code on failure.
 */
int vflash_fs_init(void);

/*
 * Open a file by path from the ISO 9660 filesystem.
 * Path separators can be '/' or '\'. Case-insensitive lookup.
 * Version suffixes (e.g., ";1") are handled automatically.
 * Returns file descriptor index (>= 0) on success, -1 on failure.
 */
int vflash_fs_open(const char *path);

/*
 * Read up to 'count' bytes from an open file descriptor into 'buf'.
 * Handles cross-sector boundaries and partial sector buffering via sector cache.
 * Returns number of bytes read (0 on EOF), or -1 on error.
 */
int32_t vflash_fs_read(int fd, void *buf, uint32_t count);

/*
 * Seek to an offset in an open file descriptor.
 * 'whence' is SEEK_SET, SEEK_CUR, or SEEK_END.
 * Returns resulting offset from the beginning of the file, or -1 on error.
 */
int32_t vflash_fs_seek(int fd, int32_t offset, int whence);

/*
 * Return current file position in bytes from the beginning.
 * Returns offset in bytes, or -1 on error.
 */
int32_t vflash_fs_tell(int fd);

/*
 * Return the total size of an open file in bytes.
 * Returns file size in bytes, or -1 on error.
 */
int32_t vflash_fs_size(int fd);

/*
 * Close an open file descriptor.
 * Returns 0 on success, -1 on error.
 */
int vflash_fs_close(int fd);

#ifdef __cplusplus
}
#endif

#endif /* VFLASH_FS_H */

