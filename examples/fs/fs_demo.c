#include <vflash/fs.h>
#include <vflash/clock.h>
#include <stdint.h>
#include <stdbool.h>

#if defined(VFLASH_ENABLE_MMU) && VFLASH_ENABLE_MMU
#include <vflash/mmu.h>
#endif

volatile uint32_t startup_status;
volatile uint32_t boot_marker;
volatile uint32_t data_cookie = 0x13579BDFu;

volatile uint32_t fs_demo_active = 0;
volatile uint32_t fs_demo_tests_passed = 0;
volatile uint32_t fs_demo_bytes_read = 0;
volatile uint32_t fs_demo_err = 0;
volatile uint32_t fs_demo_loop_ticks = 0;

static bool str_equals(const char *a, const char *b)
{
    while (*a && *b) {
        if (*a++ != *b++) return false;
    }
    return *a == *b;
}

static uint8_t g_buf[8192] __attribute__((aligned(4)));

void vflash_main(void)
{
    startup_status = (boot_marker == 0 && data_cookie == 0x13579BDFu) ? 0x600D600Du : 0;
    boot_marker = 0x56464C53u;

#if defined(VFLASH_ENABLE_MMU) && VFLASH_ENABLE_MMU
    vflash_mmu_init();
    vflash_mmu_identity();
    vflash_mmu_enable();
#endif

    /* Test 1: Initialize filesystem */
    if (vflash_fs_init() != 0) {
        fs_demo_err = 0x101;
        goto loop;
    }
    fs_demo_tests_passed++;

    /* Test 2: Open and read /TEST.TXT */
    int fd_test = vflash_fs_open("TEST.TXT");
    if (fd_test < 0) {
        fs_demo_err = 0x201;
        goto loop;
    }
    int32_t test_size = vflash_fs_size(fd_test);
    if (test_size != 95) {
        fs_demo_err = 0x202;
        vflash_fs_close(fd_test);
        goto loop;
    }
    int32_t r_test = vflash_fs_read(fd_test, g_buf, sizeof(g_buf));
    if (r_test != 95) {
        fs_demo_err = 0x203;
        vflash_fs_close(fd_test);
        goto loop;
    }
    g_buf[95] = '\0';
    if (!str_equals((const char *)g_buf, "Hello V.Flash ISO 9660 filesystem!\nThis is a test text file read directly from optical CD-ROM.\n")) {
        fs_demo_err = 0x204;
        vflash_fs_close(fd_test);
        goto loop;
    }
    if (vflash_fs_tell(fd_test) != 95) {
        fs_demo_err = 0x205;
        vflash_fs_close(fd_test);
        goto loop;
    }
    /* Verify EOF */
    if (vflash_fs_read(fd_test, g_buf, 10) != 0) {
        fs_demo_err = 0x206;
        vflash_fs_close(fd_test);
        goto loop;
    }
    vflash_fs_close(fd_test);
    fs_demo_bytes_read += (uint32_t)r_test;
    fs_demo_tests_passed++;

    /* Test 3: Seeking in CONFIG.TXT (case-insensitive path with leading slash) */
    int fd_cfg = vflash_fs_open("/config.txt");
    if (fd_cfg < 0) {
        fs_demo_err = 0x301;
        goto loop;
    }
    if (vflash_fs_size(fd_cfg) != 56) {
        fs_demo_err = 0x302;
        vflash_fs_close(fd_cfg);
        goto loop;
    }
    /* SEEK_SET to offset 6 ("V.Flash...") */
    if (vflash_fs_seek(fd_cfg, 6, SEEK_SET) != 6) {
        fs_demo_err = 0x303;
        vflash_fs_close(fd_cfg);
        goto loop;
    }
    int32_t r_cfg = vflash_fs_read(fd_cfg, g_buf, 7);
    if (r_cfg != 7) {
        fs_demo_err = 0x304;
        vflash_fs_close(fd_cfg);
        goto loop;
    }
    g_buf[7] = '\0';
    if (!str_equals((const char *)g_buf, "V.Flash")) {
        fs_demo_err = 0x305;
        vflash_fs_close(fd_cfg);
        goto loop;
    }
    /* SEEK_CUR forward by 18 bytes */
    if (vflash_fs_seek(fd_cfg, 18, SEEK_CUR) != 31) {
        fs_demo_err = 0x306;
        vflash_fs_close(fd_cfg);
        goto loop;
    }
    /* SEEK_END back by 8 bytes ("MikeDEV\n") */
    if (vflash_fs_seek(fd_cfg, -8, SEEK_END) != 48) {
        fs_demo_err = 0x307;
        vflash_fs_close(fd_cfg);
        goto loop;
    }
    r_cfg = vflash_fs_read(fd_cfg, g_buf, 7);
    if (r_cfg != 7) {
        fs_demo_err = 0x308;
        vflash_fs_close(fd_cfg);
        goto loop;
    }
    g_buf[7] = '\0';
    if (!str_equals((const char *)g_buf, "MikeDEV")) {
        fs_demo_err = 0x309;
        vflash_fs_close(fd_cfg);
        goto loop;
    }
    vflash_fs_close(fd_cfg);
    fs_demo_bytes_read += (uint32_t)r_cfg;
    fs_demo_tests_passed++;

    /* Test 4: Nested directory file DIR1/SUB.TXT */
    int fd_sub = vflash_fs_open("DIR1/SUB.TXT");
    if (fd_sub < 0) {
        fs_demo_err = 0x401;
        goto loop;
    }
    if (vflash_fs_size(fd_sub) != 62) {
        fs_demo_err = 0x402;
        vflash_fs_close(fd_sub);
        goto loop;
    }
    int32_t r_sub = vflash_fs_read(fd_sub, g_buf, sizeof(g_buf));
    if (r_sub != 62) {
        fs_demo_err = 0x403;
        vflash_fs_close(fd_sub);
        goto loop;
    }
    g_buf[62] = '\0';
    if (!str_equals((const char *)g_buf, "Directory traversal verified: nested file inside DIR1/SUB.TXT\n")) {
        fs_demo_err = 0x404;
        vflash_fs_close(fd_sub);
        goto loop;
    }
    vflash_fs_close(fd_sub);
    fs_demo_bytes_read += (uint32_t)r_sub;
    fs_demo_tests_passed++;

    /* Test 5: Multi-sector read of LARGE.BIN (6144 bytes = exactly 3 sectors) */
    int fd_large = vflash_fs_open("large.bin");
    if (fd_large < 0) {
        fs_demo_err = 0x501;
        goto loop;
    }
    if (vflash_fs_size(fd_large) != 6144) {
        fs_demo_err = 0x502;
        vflash_fs_close(fd_large);
        goto loop;
    }
    int32_t r_large = vflash_fs_read(fd_large, g_buf, 6144);
    if (r_large != 6144) {
        fs_demo_err = 0x503;
        vflash_fs_close(fd_large);
        goto loop;
    }
    /* Verify byte sequence: (i * 37 + 13) & 0xFF */
    for (uint32_t i = 0; i < 6144; ++i) {
        uint8_t expected = (uint8_t)((i * 37u + 13u) & 0xFFu);
        if (g_buf[i] != expected) {
            fs_demo_err = 0x504;
            vflash_fs_close(fd_large);
            goto loop;
        }
    }
    vflash_fs_close(fd_large);
    fs_demo_bytes_read += (uint32_t)r_large;
    fs_demo_tests_passed++;

    /* Test 6: Multiple concurrent open file handles */
    int h1 = vflash_fs_open("test.txt");
    int h2 = vflash_fs_open("config.txt");
    if (h1 < 0 || h2 < 0 || h1 == h2) {
        fs_demo_err = 0x601;
        if (h1 >= 0) vflash_fs_close(h1);
        if (h2 >= 0) vflash_fs_close(h2);
        goto loop;
    }
    uint8_t b1[16], b2[16];
    if (vflash_fs_read(h1, b1, 5) != 5 || vflash_fs_read(h2, b2, 5) != 5) {
        fs_demo_err = 0x602;
        vflash_fs_close(h1);
        vflash_fs_close(h2);
        goto loop;
    }
    b1[5] = '\0';
    b2[5] = '\0';
    if (!str_equals((const char *)b1, "Hello") || !str_equals((const char *)b2, "title")) {
        fs_demo_err = 0x603;
        vflash_fs_close(h1);
        vflash_fs_close(h2);
        goto loop;
    }
    vflash_fs_close(h1);
    vflash_fs_close(h2);
    fs_demo_tests_passed++;

    /* Test 7: Error handling (invalid path, non-existent file, seek out of range) */
    if (vflash_fs_open("NO_SUCH_FILE.DAT") >= 0) {
        fs_demo_err = 0x701;
        goto loop;
    }
    if (vflash_fs_open("") >= 0) {
        fs_demo_err = 0x702;
        goto loop;
    }
    int fd_err = vflash_fs_open("test.txt");
    if (fd_err < 0) {
        fs_demo_err = 0x703;
        goto loop;
    }
    if (vflash_fs_seek(fd_err, 9999, SEEK_SET) != -1) {
        fs_demo_err = 0x704;
        vflash_fs_close(fd_err);
        goto loop;
    }
    if (vflash_fs_seek(fd_err, -10, SEEK_SET) != -1) {
        fs_demo_err = 0x705;
        vflash_fs_close(fd_err);
        goto loop;
    }
    vflash_fs_close(fd_err);
    /* Close invalid fd */
    if (vflash_fs_close(99) != -1) {
        fs_demo_err = 0x706;
        goto loop;
    }
    fs_demo_tests_passed++;

    /* Test 8: Read BMP header verification from optical disc */
    int fd_bmp = vflash_fs_open("TEST.BMP");
    if (fd_bmp < 0) {
        fs_demo_err = 0x801;
        goto loop;
    }
    if (vflash_fs_read(fd_bmp, g_buf, 14) != 14) {
        fs_demo_err = 0x802;
        vflash_fs_close(fd_bmp);
        goto loop;
    }
    if (g_buf[0] != 'B' || g_buf[1] != 'M') {
        fs_demo_err = 0x803;
        vflash_fs_close(fd_bmp);
        goto loop;
    }
    vflash_fs_close(fd_bmp);
    fs_demo_tests_passed++;

    /* All acceptance tests succeeded */
    fs_demo_active = 1;

loop:
    while (1) {
        fs_demo_loop_ticks++;
        __asm__ volatile("nop");
    }
}
