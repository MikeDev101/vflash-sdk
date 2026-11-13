#ifndef VFLASH_FS_H
#define VFLASH_FS_H

#include <stdbool.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

//
// Maximum number of simultaneously open ISO 9660 file descriptors.
//
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

//
// Initialize the native ISO 9660 filesystem, initialize the CD driver,
// and mount the Primary Volume Descriptor and root directory.
//
// Returns 0 on success, or a negative error code on failure.
//
int vflash_fs_init(void);

//
// Open a file by path from the mounted ISO 9660 filesystem.
// Separators '/' and '\\' are accepted; lookups are case-insensitive and
// strip ISO 9660 version suffixes (such as ";1").
//
// path: Null-terminated path to the file on disc.
//
// Returns a file descriptor (>= 0) on success, or -1 on failure.
//
int vflash_fs_open(const char *path);

//
// Read up to count bytes from an open file descriptor into buf.
//
// fd:    Open file descriptor returned by vflash_fs_open().
// buf:   Destination buffer to receive read bytes.
// count: Maximum number of bytes to read.
//
// Returns bytes read (0 at EOF), or -1 on error.
//
int32_t vflash_fs_read(int fd, void *buf, uint32_t count);

//
// Reposition the read offset of an open file descriptor.
//
// fd:     Open file descriptor returned by vflash_fs_open().
// offset: Byte offset relative to whence.
// whence: Origin constant: SEEK_SET, SEEK_CUR, or SEEK_END.
//
// Returns the resulting byte offset from file start, or -1 on error.
//
int32_t vflash_fs_seek(int fd, int32_t offset, int whence);

//
// Query the current read offset of an open file descriptor.
//
// fd: Open file descriptor returned by vflash_fs_open().
//
// Returns the current byte offset from file start, or -1 on error.
//
int32_t vflash_fs_tell(int fd);

//
// Query the total size in bytes of an open file.
//
// fd: Open file descriptor returned by vflash_fs_open().
//
// Returns the file size in bytes, or -1 on error.
//
int32_t vflash_fs_size(int fd);

//
// Close an open file descriptor and release its slot.
//
// fd: Open file descriptor returned by vflash_fs_open().
//
// Returns 0 on success, or -1 on error.
//
int vflash_fs_close(int fd);

#ifdef __cplusplus
}
#endif

#endif // VFLASH_FS_H
