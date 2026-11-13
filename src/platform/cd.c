#include <vflash/cd.h>

#define CD_REG(off) (*(volatile uint32_t *)(0xC4000000u + (off)))

static int32_t s_current_lba = 31;

static uint32_t udiv32(uint32_t n, uint32_t d, uint32_t *rem)
{
    uint32_t q = 0;
    for (int i = 31; i >= 0; --i) {
        if ((n >> i) >= d) {
            n -= (d << i);
            q |= (1u << i);
        }
    }
    if (rem) *rem = n;
    return q;
}

static uint32_t lba_to_msf(uint32_t lba)
{
    uint32_t abs_frame = lba + 150u;
    uint32_t rem;
    uint32_t m = udiv32(abs_frame, 60u * 75u, &rem);
    uint32_t f;
    uint32_t s = udiv32(rem, 75u, &f);

    uint32_t rem_m, rem_s, rem_f;
    uint32_t div_m = udiv32(m, 10u, &rem_m);
    uint32_t div_s = udiv32(s, 10u, &rem_s);
    uint32_t div_f = udiv32(f, 10u, &rem_f);

    uint32_t bcd_m = (div_m << 4) | rem_m;
    uint32_t bcd_s = (div_s << 4) | rem_s;
    uint32_t bcd_f = (div_f << 4) | rem_f;
    return (bcd_m << 16) | (bcd_s << 8) | bcd_f;
}

static int32_t msf_to_signed_lba(uint32_t msf)
{
    uint32_t m = ((msf >> 20) & 0x0Fu) * 10u + ((msf >> 16) & 0x0Fu);
    uint32_t s = ((msf >> 12) & 0x0Fu) * 10u + ((msf >> 8) & 0x0Fu);
    uint32_t f = ((msf >> 4) & 0x0Fu) * 10u + (msf & 0x0Fu);
    int32_t abs_frame = (int32_t)(m * 60u * 75u + s * 75u + f);
    return abs_frame - 150;
}

static void cd_dsp_cmd(uint32_t bit_len, uint32_t payload)
{
    CD_REG(0x00) = bit_len;
    CD_REG(0x08) = payload;
    CD_REG(0x0C) = 0;
    CD_REG(0x14) = 1;
    for (volatile int i = 0; i < 1000; ++i) {
        if (CD_REG(0x64) & 1u) {
            CD_REG(0x64) = 1u;
            break;
        }
    }
}

static void cd_seek_tracks(int tracks)
{
    if (tracks == 0) return;
    uint32_t count = (tracks < 0) ? (uint32_t)(-tracks) : (uint32_t)tracks;
    if (count > 0x3FFF) count = 0x3FFF;

    // 1. Set traverse count
    cd_dsp_cmd(20, (0x7u << 16) | count);

    // 2. Execute seek
    if (tracks < 0) {
        cd_dsp_cmd(8, 0x4Fu); // reverse seek
    } else {
        cd_dsp_cmd(8, 0x4Eu); // forward seek
    }

    // 3. Wait for seek completion (SENS bit 1)
    for (volatile int i = 0; i < 200000; ++i) {
        if (CD_REG(0x18) & 2u) break;
    }

    // 4. Re-enable tracking mode
    cd_dsp_cmd(8, 0x24u);
}

static void cd_home_head(void)
{
        // Deterministically reset the optical pickup to the 22.5 mm physical inner
    // stop regardless of how far outward it has tracked during idle menu time,
    // then step forward 1580 tracks (25.028 mm = LBA 8) right before LBA 16.
    cd_seek_tracks(-16383);
    cd_seek_tracks(-16383);
    cd_seek_tracks(1580);
    s_current_lba = 8;
}

static void cd_position_head(int32_t target_lba)
{
    if (target_lba < s_current_lba) {
        uint32_t diff = (uint32_t)(s_current_lba - target_lba);
        // 1 track is ~9.1 sectors. Seek back enough tracks to land 4..12 sectors before target
        uint32_t tr = udiv32(diff + 4u, 9u, 0) + 1u;
        if (tr > 0x3FFFu) tr = 0x3FFFu;
        cd_seek_tracks(-(int)tr);
        s_current_lba -= (int32_t)(tr * 9u);
    } else if (target_lba > s_current_lba + 18) {
        uint32_t diff = (uint32_t)(target_lba - s_current_lba);
        // Conservative ~10 sectors/track to never overshoot target_lba
        uint32_t tr = udiv32(diff - 6u, 10u, 0);
        if (tr > 0) {
            if (tr > 0x3FFFu) tr = 0x3FFFu;
            cd_seek_tracks((int)tr);
            s_current_lba += (int32_t)(tr * 9u);
        }
    }
}

int vflash_cd_init(void)
{
    uint32_t cur_msf = CD_REG(0x5C);
    if (cur_msf != 0) {
        s_current_lba = msf_to_signed_lba(cur_msf);
    } else {
        s_current_lba = 31;
    }
    if (s_current_lba < 0 || s_current_lba > 1000) {
        cd_home_head();
    }
    CD_REG(0x60) &= ~0x00F00000u;
    CD_REG(0x64) = 0xFFFFFFFFu;
    CD_REG(0x74) = 2;
    cd_dsp_cmd(32, 0x9F209000u); // Enable 2x base DSP speed
    cd_dsp_cmd(32, 0xD0C00040u); // Sony CXD3068Q CAV-W 4x spindle velocity
    cd_dsp_cmd(32, 0xE6670000u); // Spindle mode 0x65 for 4x transfer rate (300 sectors/sec)
    cd_dsp_cmd(8, 0x20u);        // Hold tracking while idle so head does not overshoot
    return 0;
}

uint32_t vflash_cd_get_current_lba(void)
{
    uint32_t cur_msf = CD_REG(0x5C);
    if (cur_msf != 0) {
        s_current_lba = msf_to_signed_lba(cur_msf);
    }
    return (s_current_lba < 0) ? 0u : (uint32_t)s_current_lba;
}

int vflash_cd_read_sectors(uint32_t lba, uint32_t count, void *dst)
{
    if (count == 0 || !dst) return 0;

    uint32_t cur_msf = CD_REG(0x5C);
    if (cur_msf != 0) {
        s_current_lba = msf_to_signed_lba(cur_msf);
    }
    if (s_current_lba < 0 || s_current_lba > (int32_t)lba + 1000) {
        cd_home_head();
    }

    cd_position_head((int32_t)lba);
    cd_dsp_cmd(8, 0x24u);

    uint32_t msf = lba_to_msf(lba);
    uint32_t bytes = count * VFLASH_CD_SECTOR_SIZE;
    uint32_t addr = (uint32_t)(uintptr_t)dst;

    CD_REG(0x1C) = msf;
    CD_REG(0x20) = count;
    CD_REG(0x28) = addr;
    CD_REG(0x2C) = addr + bytes + 0x800u;
    CD_REG(0x24) = addr;
    CD_REG(0x80) = addr + bytes;
    CD_REG(0x84) = 1u;
    CD_REG(0x60) &= ~0x00F00000u;
    CD_REG(0x64) = 0xFFFFFFFFu;
    CD_REG(0x74) = 2;

    // Start read: Mode 3 (0x18) | Read active (0x04)
    CD_REG(0x40) = (CD_REG(0x40) & ~0x18u) | 0x18u | 0x04u;

    uint32_t delivered = 0;
    uint32_t idle_polls = 0;
    uint32_t timeout = 50000000u;
    while (timeout--) {
        CD_REG(0x74) = 2; // clear slot overrun check
        uint32_t st = CD_REG(0x64);
        if (st & (0x10u | 0x10000u)) {
            CD_REG(0x40) &= ~0x04u;
            cd_dsp_cmd(8, 0x20u); // Hold tracking so head stays at lba + count - 1
            CD_REG(0x64) = 0xFFFFFFFFu;
            s_current_lba = (int32_t)(lba + count - 1u);
            return (int)count;
        }
        if (st & 0x1000u) {
            CD_REG(0x64) = 0x1000u;
            idle_polls = 0;
            delivered++;
            if (delivered >= count) {
                CD_REG(0x40) &= ~0x04u;
                cd_dsp_cmd(8, 0x20u); // Hold tracking so head stays at lba + count - 1
                CD_REG(0x64) = 0xFFFFFFFFu;
                s_current_lba = (int32_t)(lba + count - 1u);
                return (int)count;
            }
        }
        // Closed-loop subchannel Q check before first sector arrives
        if (delivered == 0 && (st & 0x20u)) {
            CD_REG(0x64) = 0x20u;
            idle_polls = 0;
            uint32_t q_msf = CD_REG(0x5C);
            if (q_msf != 0) {
                s_current_lba = msf_to_signed_lba(q_msf);
                if (s_current_lba > (int32_t)lba || s_current_lba < (int32_t)lba - 25) {
                    CD_REG(0x40) &= ~0x04u;
                    cd_position_head((int32_t)lba);
                    cd_dsp_cmd(8, 0x24u);
                    CD_REG(0x1C) = msf;
                    CD_REG(0x20) = count;
                    CD_REG(0x24) = addr;
                    CD_REG(0x64) = 0xFFFFFFFFu;
                    CD_REG(0x74) = 2;
                    CD_REG(0x40) = (CD_REG(0x40) & ~0x18u) | 0x18u | 0x04u;
                }
            }
        } else if (delivered == 0 && ++idle_polls >= 200000u) {
            // Head drifted into lead-out where SubQ stops firing; home and retry
            idle_polls = 0;
            CD_REG(0x40) &= ~0x04u;
            cd_home_head();
            cd_position_head((int32_t)lba);
            cd_dsp_cmd(8, 0x24u);
            CD_REG(0x1C) = msf;
            CD_REG(0x20) = count;
            CD_REG(0x24) = addr;
            CD_REG(0x64) = 0xFFFFFFFFu;
            CD_REG(0x74) = 2;
            CD_REG(0x40) = (CD_REG(0x40) & ~0x18u) | 0x18u | 0x04u;
        }
    }

    CD_REG(0x40) &= ~0x04u;
    cd_dsp_cmd(8, 0x20u);
    CD_REG(0x64) = 0xFFFFFFFFu;
    return -1;
}

int vflash_cd_read_sector(uint32_t lba, void *dst)
{
    return vflash_cd_read_sectors(lba, 1, dst);
}
