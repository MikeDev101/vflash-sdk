#include "vflash/video.h"
#include "vflash/audio.h"
#include "vflash/fs.h"
#include "vflash/clock.h"
#include "vflash/picojpeg.h"
#include <string.h>

#define MIAV_MAGIC 0x5641494D /* "MIAV" */

static const int16_t s_ima_step_table[89] = {
    7, 8, 9, 10, 11, 12, 13, 14, 16, 17,
    19, 21, 23, 25, 28, 31, 34, 37, 41, 45,
    50, 55, 60, 66, 73, 80, 88, 97, 107, 118,
    130, 143, 157, 173, 190, 209, 230, 253, 279, 307,
    337, 371, 408, 449, 494, 544, 598, 658, 724, 796,
    876, 963, 1060, 1166, 1282, 1411, 1552, 1707, 1878, 2066,
    2272, 2499, 2749, 3024, 3327, 3660, 4026, 4428, 4871, 5358,
    5894, 6484, 7132, 7845, 8630, 9493, 10442, 11487, 12635, 13899,
    15289, 16818, 18500, 20350, 22385, 24623, 27086, 29794, 32767
};

static const int8_t s_ima_index_table[16] = {
    -1, -1, -1, -1, 2, 4, 6, 8,
    -1, -1, -1, -1, 2, 4, 6, 8
};

static uint32_t decode_adpcm_chunk(const uint8_t *input, uint32_t in_size,
                                   int16_t *output, uint32_t max_samples) {
    if (in_size < 4 || max_samples < 1) return 0;
    int32_t sample = (int16_t)(input[0] | (input[1] << 8));
    int step_index = input[2];
    if (step_index > 88) step_index = 88;

    uint32_t out_count = 0;
    output[out_count++] = (int16_t)sample;

    for (uint32_t i = 4; i < in_size && out_count + 1 < max_samples; i++) {
        uint8_t byte = input[i];
        for (int shift = 0; shift <= 4; shift += 4) {
            uint8_t nibble = (byte >> shift) & 0x0F;
            int step = s_ima_step_table[step_index];
            int diff = step >> 3;
            if (nibble & 4) diff += step;
            if (nibble & 2) diff += step >> 1;
            if (nibble & 1) diff += step >> 2;
            if (nibble & 8) sample -= diff;
            else sample += diff;

            if (sample > 32767) sample = 32767;
            else if (sample < -32768) sample = -32768;

            step_index += s_ima_index_table[nibble & 7];
            if (step_index < 0) step_index = 0;
            else if (step_index > 88) step_index = 88;

            output[out_count++] = (int16_t)sample;
        }
    }
    return out_count;
}

static uint16_t s_lut555[256];
static bool s_lut555_inited = false;

static void ensure_lut555(void) {
    if (!s_lut555_inited) {
        for (int i = 0; i < 256; ++i) {
            uint16_t v = (uint16_t)((i >> 3) & 0x1F);
            s_lut555[i] = (uint16_t)(v | (v << 5) | (v << 10));
        }
        s_lut555_inited = true;
    }
}

static inline uint16_t rgb_to_bgr555(uint8_t r, uint8_t g, uint8_t b) {
    if (r == g && g == b) {
        return s_lut555[r];
    }
    return (uint16_t)(((r >> 3) & 0x1F) |
                      (((g >> 3) & 0x1F) << 5) |
                      (((b >> 3) & 0x1F) << 10));
}

static inline uint16_t gray_to_bgr555(uint8_t y) {
    return s_lut555[y];
}

static unsigned char pjpeg_stream_callback(unsigned char *pBuf, unsigned char buf_size,
                                           unsigned char *pBytes_actually_read, void *pCallback_data) {
    vflash_video_player_t *player = (vflash_video_player_t *)pCallback_data;
    uint32_t available = player->chunk_buf_size - player->chunk_buf_pos;
    uint32_t to_read = buf_size;
    if (to_read > available) to_read = available;

    if (to_read > 0) {
        memcpy(pBuf, player->chunk_buf + player->chunk_buf_pos, to_read);
        player->chunk_buf_pos += to_read;
    }

    *pBytes_actually_read = (unsigned char)to_read;
    return 0;
}

static int decode_and_render_frame(vflash_video_player_t *player, vflash_framebuffer_t *fb) {
    ensure_lut555();
    pjpeg_image_info_t info;
    unsigned char err = pjpeg_decode_init(&info, pjpeg_stream_callback, player, 0);
    if (err) return -1;

    for (int mcu_y = 0; mcu_y < info.m_MCUSPerCol; mcu_y++) {
        for (int mcu_x = 0; mcu_x < info.m_MCUSPerRow; mcu_x++) {
            err = pjpeg_decode_mcu();
            if (err) return -2;

            if (!fb || !fb->pixels) continue;

            if (info.m_scanType == PJPG_YH2V2) {
                /* 4:2:0 subsampled: 16x16 MCU composed of 4 8x8 subblocks */
                for (int sb_y = 0; sb_y < 2; sb_y++) {
                    for (int sb_x = 0; sb_x < 2; sb_x++) {
                        int base_x = player->dst_x + mcu_x * 16 + sb_x * 8;
                        int base_y = player->dst_y + mcu_y * 16 + sb_y * 8;
                        int blk_offset = (sb_y * 2 + sb_x) * 64;

                        if (base_x >= 0 && base_x + 8 <= (int)fb->width && ((base_x & 1) == 0)) {
                            for (int py = 0; py < 8; py++) {
                                int fy = base_y + py;
                                if (fy < 0 || fy >= (int)fb->height) continue;
                                uint32_t *dst32 = (uint32_t *)(fb->pixels + fy * fb->stride + base_x);
                                int src_row = blk_offset + py * 8;
                                const uint8_t *r = info.m_pMCUBufR + src_row;
                                const uint8_t *g = info.m_pMCUBufG + src_row;
                                const uint8_t *b = info.m_pMCUBufB + src_row;

                                uint32_t c0 = rgb_to_bgr555(r[0], g[0], b[0]);
                                uint32_t c1 = rgb_to_bgr555(r[1], g[1], b[1]);
                                dst32[0] = c0 | (c1 << 16);

                                uint32_t c2 = rgb_to_bgr555(r[2], g[2], b[2]);
                                uint32_t c3 = rgb_to_bgr555(r[3], g[3], b[3]);
                                dst32[1] = c2 | (c3 << 16);

                                uint32_t c4 = rgb_to_bgr555(r[4], g[4], b[4]);
                                uint32_t c5 = rgb_to_bgr555(r[5], g[5], b[5]);
                                dst32[2] = c4 | (c5 << 16);

                                uint32_t c6 = rgb_to_bgr555(r[6], g[6], b[6]);
                                uint32_t c7 = rgb_to_bgr555(r[7], g[7], b[7]);
                                dst32[3] = c6 | (c7 << 16);
                            }
                        } else {
                            for (int py = 0; py < 8; py++) {
                                int fy = base_y + py;
                                if (fy < 0 || fy >= (int)fb->height) continue;
                                uint16_t *line = fb->pixels + fy * fb->stride;
                                int src_row = blk_offset + py * 8;

                                for (int px = 0; px < 8; px++) {
                                    int fx = base_x + px;
                                    if (fx < 0 || fx >= (int)fb->width) continue;
                                    uint8_t r = info.m_pMCUBufR[src_row + px];
                                    uint8_t g = info.m_pMCUBufG[src_row + px];
                                    uint8_t b = info.m_pMCUBufB[src_row + px];
                                    line[fx] = rgb_to_bgr555(r, g, b);
                                }
                            }
                        }
                    }
                }
            } else if (info.m_scanType == PJPG_GRAYSCALE) {
                int base_x = player->dst_x + mcu_x * 8;
                int base_y = player->dst_y + mcu_y * 8;
                if (base_x >= 0 && base_x + 8 <= (int)fb->width && ((base_x & 1) == 0)) {
                    for (int py = 0; py < 8; py++) {
                        int fy = base_y + py;
                        if (fy < 0 || fy >= (int)fb->height) continue;
                        uint32_t *dst32 = (uint32_t *)(fb->pixels + fy * fb->stride + base_x);
                        int src_row = py * 8;
                        const uint8_t *r = info.m_pMCUBufR + src_row;

                        uint32_t c0 = gray_to_bgr555(r[0]);
                        uint32_t c1 = gray_to_bgr555(r[1]);
                        dst32[0] = c0 | (c1 << 16);

                        uint32_t c2 = gray_to_bgr555(r[2]);
                        uint32_t c3 = gray_to_bgr555(r[3]);
                        dst32[1] = c2 | (c3 << 16);

                        uint32_t c4 = gray_to_bgr555(r[4]);
                        uint32_t c5 = gray_to_bgr555(r[5]);
                        dst32[2] = c4 | (c5 << 16);

                        uint32_t c6 = gray_to_bgr555(r[6]);
                        uint32_t c7 = gray_to_bgr555(r[7]);
                        dst32[3] = c6 | (c7 << 16);
                    }
                } else {
                    for (int py = 0; py < 8; py++) {
                        int fy = base_y + py;
                        if (fy < 0 || fy >= (int)fb->height) continue;
                        uint16_t *line = fb->pixels + fy * fb->stride;
                        int src_row = py * 8;
                        for (int px = 0; px < 8; px++) {
                            int fx = base_x + px;
                            if (fx < 0 || fx >= (int)fb->width) continue;
                            line[fx] = gray_to_bgr555(info.m_pMCUBufR[src_row + px]);
                        }
                    }
                }
            } else if (info.m_scanType == PJPG_YH1V1) {
                int base_x = player->dst_x + mcu_x * 8;
                int base_y = player->dst_y + mcu_y * 8;
                if (base_x >= 0 && base_x + 8 <= (int)fb->width && ((base_x & 1) == 0)) {
                    for (int py = 0; py < 8; py++) {
                        int fy = base_y + py;
                        if (fy < 0 || fy >= (int)fb->height) continue;
                        uint32_t *dst32 = (uint32_t *)(fb->pixels + fy * fb->stride + base_x);
                        int src_row = py * 8;
                        const uint8_t *r = info.m_pMCUBufR + src_row;
                        const uint8_t *g = info.m_pMCUBufG + src_row;
                        const uint8_t *b = info.m_pMCUBufB + src_row;

                        uint32_t c0 = rgb_to_bgr555(r[0], g[0], b[0]);
                        uint32_t c1 = rgb_to_bgr555(r[1], g[1], b[1]);
                        dst32[0] = c0 | (c1 << 16);

                        uint32_t c2 = rgb_to_bgr555(r[2], g[2], b[2]);
                        uint32_t c3 = rgb_to_bgr555(r[3], g[3], b[3]);
                        dst32[1] = c2 | (c3 << 16);

                        uint32_t c4 = rgb_to_bgr555(r[4], g[4], b[4]);
                        uint32_t c5 = rgb_to_bgr555(r[5], g[5], b[5]);
                        dst32[2] = c4 | (c5 << 16);

                        uint32_t c6 = rgb_to_bgr555(r[6], g[6], b[6]);
                        uint32_t c7 = rgb_to_bgr555(r[7], g[7], b[7]);
                        dst32[3] = c6 | (c7 << 16);
                    }
                } else {
                    for (int py = 0; py < 8; py++) {
                        int fy = base_y + py;
                        if (fy < 0 || fy >= (int)fb->height) continue;
                        uint16_t *line = fb->pixels + fy * fb->stride;
                        int src_row = py * 8;
                        for (int px = 0; px < 8; px++) {
                            int fx = base_x + px;
                            if (fx < 0 || fx >= (int)fb->width) continue;
                            uint8_t r = info.m_pMCUBufR[src_row + px];
                            uint8_t g = info.m_pMCUBufG ? info.m_pMCUBufG[src_row + px] : r;
                            uint8_t b = info.m_pMCUBufB ? info.m_pMCUBufB[src_row + px] : r;
                            line[fx] = rgb_to_bgr555(r, g, b);
                        }
                    }
                }
            }
        }
    }
    return 0;
}

int vflash_video_open(vflash_video_player_t *player, const char *path, int x, int y) {
    if (!player || !path) return -1;
    memset(player, 0, sizeof(*player));
    player->fd = -1;
    player->dst_x = x;
    player->dst_y = y;

    int fd = vflash_fs_open(path);
    if (fd < 0) {
        player->state = VFLASH_VIDEO_ERROR;
        return -2;
    }
    player->fd = fd;

    uint8_t hdr[64];
    int32_t rd = vflash_fs_read(fd, hdr, 64);
    if (rd != 64) {
        vflash_fs_close(fd);
        player->fd = -1;
        player->state = VFLASH_VIDEO_ERROR;
        return -3;
    }

    uint32_t magic = hdr[0] | ((uint32_t)hdr[1] << 8) | ((uint32_t)hdr[2] << 16) | ((uint32_t)hdr[3] << 24);
    if (magic != MIAV_MAGIC) {
        vflash_fs_close(fd);
        player->fd = -1;
        player->state = VFLASH_VIDEO_ERROR;
        return -4;
    }

    uint32_t hdr_size = hdr[4] | ((uint32_t)hdr[5] << 8) | ((uint32_t)hdr[6] << 16) | ((uint32_t)hdr[7] << 24);
    if (hdr_size < 64) {
        vflash_fs_close(fd);
        player->fd = -1;
        player->state = VFLASH_VIDEO_ERROR;
        return -5;
    }

    player->payload_size = hdr[8] | ((uint32_t)hdr[9] << 8) | ((uint32_t)hdr[10] << 16) | ((uint32_t)hdr[11] << 24);
    player->frame_count = hdr[16] | ((uint32_t)hdr[17] << 8) | ((uint32_t)hdr[18] << 16) | ((uint32_t)hdr[19] << 24);
    player->fps = hdr[20] | ((uint32_t)hdr[21] << 8) | ((uint32_t)hdr[22] << 16) | ((uint32_t)hdr[23] << 24);
    if (player->fps == 0) player->fps = 15;
    player->audio_flags = hdr[24] | ((uint32_t)hdr[25] << 8) | ((uint32_t)hdr[26] << 16) | ((uint32_t)hdr[27] << 24);
    player->width = hdr[28] | ((uint32_t)hdr[29] << 8) | ((uint32_t)hdr[30] << 16) | ((uint32_t)hdr[31] << 24);
    player->height = hdr[32] | ((uint32_t)hdr[33] << 8) | ((uint32_t)hdr[34] << 16) | ((uint32_t)hdr[35] << 24);
    player->data_start_offset = hdr_size;

    player->ticks_per_frame = 10000 / player->fps;
    player->state = VFLASH_VIDEO_STOPPED;
    player->current_frame = 0;
    player->last_tick = vflash_clock_ticks();
    player->tick_accumulator = 0;

    if (player->audio_flags != 0) {
        vflash_audio_config_t cfg;
        cfg.sample_rate = 22050;
        cfg.channels = 1;
        cfg.format = VFLASH_AUDIO_FMT_S16LE;
        cfg.buffer_samples = 4096;
        if (vflash_audio_init(&cfg)) {
            player->audio_enabled = true;
        }
    }

    return 0;
}

static void reset_playback_clock(vflash_video_player_t *player) {
    player->last_tick = vflash_clock_ticks();
    player->tick_accumulator = 0;
    player->audio_clock_base_ticks = 0;
    player->audio_samples_base = 0;
    if (player->audio_enabled) {
        vflash_audio_stats_t a_stats;
        vflash_audio_get_stats(&a_stats);
        player->audio_samples_base = a_stats.samples_played;
    }
}

static void sync_playback_clock(vflash_video_player_t *player) {
    uint16_t now = vflash_clock_ticks();
    uint16_t elapsed = (uint16_t)(now - player->last_tick);
    player->last_tick = now;

    if (player->audio_enabled) {
        vflash_audio_stats_t a_stats;
        vflash_audio_get_stats(&a_stats);
        if (a_stats.samples_played > player->audio_samples_base) {
            uint32_t played = a_stats.samples_played - player->audio_samples_base;
            player->tick_accumulator = player->audio_clock_base_ticks +
                                       (uint32_t)(((uint64_t)played * 10000ULL) / 22050u);
            return;
        }
    }

    player->tick_accumulator += elapsed;
    player->audio_clock_base_ticks = player->tick_accumulator;
}

int vflash_video_play(vflash_video_player_t *player) {
    if (!player || player->fd < 0) return -1;
    if (player->state == VFLASH_VIDEO_STOPPED || player->state == VFLASH_VIDEO_FINISHED) {
        vflash_fs_seek(player->fd, (int32_t)player->data_start_offset, SEEK_SET);
        player->current_frame = 0;
        reset_playback_clock(player);
        player->frame_decoded = false;
    } else if (player->state == VFLASH_VIDEO_PAUSED) {
        player->last_tick = vflash_clock_ticks();
        if (player->audio_enabled) {
            vflash_audio_pause(false);
        }
    }
    player->state = VFLASH_VIDEO_PLAYING;
    return 0;
}

int vflash_video_pause(vflash_video_player_t *player) {
    if (!player) return -1;
    if (player->state == VFLASH_VIDEO_PLAYING) {
        player->state = VFLASH_VIDEO_PAUSED;
        if (player->audio_enabled) {
            vflash_audio_pause(true);
        }
    }
    return 0;
}

int vflash_video_stop(vflash_video_player_t *player) {
    if (!player || player->fd < 0) return -1;
    vflash_fs_seek(player->fd, (int32_t)player->data_start_offset, SEEK_SET);
    player->current_frame = 0;
    reset_playback_clock(player);
    player->frame_decoded = false;
    player->state = VFLASH_VIDEO_STOPPED;
    if (player->audio_enabled) {
        vflash_audio_pause(true);
    }
    return 0;
}

int vflash_video_restart(vflash_video_player_t *player) {
    if (!player) return -1;
    vflash_video_stop(player);
    return vflash_video_play(player);
}

int vflash_video_set_loop(vflash_video_player_t *player, bool loop) {
    if (!player) return -1;
    player->loop = loop;
    return 0;
}

int vflash_video_update(vflash_video_player_t *player, vflash_framebuffer_t *fb) {
    if (!player || player->state != VFLASH_VIDEO_PLAYING || player->fd < 0) {
        vflash_audio_update();
        return 0;
    }

    sync_playback_clock(player);

    /* Check presentation timer deadline before reading or decoding video.
     * Target tick is based on the frame to be displayed: player->current_frame.
     * Lead deadline provides half a V-blank (~80 ticks / 8.0ms) of presentation headroom
     * so decode finishes and vflash_video_present() cleanly latches the target V-Blank. */
    uint32_t target_tick = (uint32_t)(((uint64_t)player->current_frame * 10000u) / player->fps);
    uint32_t deadline = (target_tick >= 80u) ? (target_tick - 80u) : 0;
    if (player->tick_accumulator < deadline) {
        return 0;
    }

    while (true) {
        uint8_t chunk_hdr[12];
        int32_t rd = vflash_fs_read(player->fd, chunk_hdr, 12);
        if (rd < 12) {
            if (player->loop) {
                vflash_fs_seek(player->fd, (int32_t)player->data_start_offset, SEEK_SET);
                player->current_frame = 0;
                reset_playback_clock(player);
                continue;
            } else {
                player->state = VFLASH_VIDEO_FINISHED;
                return 0;
            }
        }

        uint32_t chunk_size = chunk_hdr[8] | ((uint32_t)chunk_hdr[9] << 8) |
                              ((uint32_t)chunk_hdr[10] << 16) | ((uint32_t)chunk_hdr[11] << 24);

        if (chunk_size > VFLASH_VIDEO_CHUNK_BUF_SIZE) {
            vflash_fs_seek(player->fd, (int32_t)(chunk_size + (chunk_size & 1)), SEEK_CUR);
            continue;
        }

        if (chunk_hdr[0] == '0' && chunk_hdr[1] == '0' && chunk_hdr[2] == 'd' && chunk_hdr[3] == 'c') {
            /* Check if playback has slipped behind real-time schedule.
             * If tick_accumulator is more than 1 frame behind target deadline,
             * drop the video decode to catch up, but still process following audio! */
            uint32_t cur_target_tick = (uint32_t)(((uint64_t)player->current_frame * 10000u) / player->fps);
            if (player->tick_accumulator > cur_target_tick + (player->ticks_per_frame * 3u) / 2u) {
                vflash_fs_seek(player->fd, (int32_t)(chunk_size + (chunk_size & 1)), SEEK_CUR);
                player->current_frame++;
                player->dropped_frames++;
                continue;
            }
        }

        rd = vflash_fs_read(player->fd, player->chunk_buf, chunk_size);
        if (chunk_size & 1) {
            uint8_t pad;
            vflash_fs_read(player->fd, &pad, 1);
        }
        if (rd != (int32_t)chunk_size) {
            player->state = VFLASH_VIDEO_ERROR;
            return 0;
        }

        if (chunk_hdr[0] == '0' && chunk_hdr[1] == '1' && chunk_hdr[2] == 'w' && chunk_hdr[3] == 'b') {
            /* Audio chunk: decode and enqueue into audio ring buffer */
            if (player->audio_enabled) {
                if (player->audio_flags == 0x0001) {
                    /* ADPCM */
                    uint32_t samples = decode_adpcm_chunk(player->chunk_buf, chunk_size,
                                                         player->audio_pcm_buf, 4096);
                    vflash_audio_write(player->audio_pcm_buf, samples * 2);
                    player->audio_samples_queued += samples;
                } else if (player->audio_flags == 0x0004) {
                    /* PCM16 */
                    vflash_audio_write(player->chunk_buf, chunk_size);
                    player->audio_samples_queued += (chunk_size / 2);
                }
            }
            continue;
        } else if (chunk_hdr[0] == '0' && chunk_hdr[1] == '0' && chunk_hdr[2] == 'd' && chunk_hdr[3] == 'c') {
            /* Video frame chunk: in-place 16-bit word pairwise byte-swap for JPEG bitstream */
            uint32_t words = chunk_size >> 2;
            uint32_t *wptr = (uint32_t *)player->chunk_buf;
            for (uint32_t i = 0; i < words; ++i) {
                uint32_t v = wptr[i];
                wptr[i] = ((v & 0x00FF00FFu) << 8) | ((v & 0xFF00FF00u) >> 8);
            }
            if (chunk_size & 2) {
                uint8_t *bptr = player->chunk_buf + (words << 2);
                uint8_t tmp = bptr[0]; bptr[0] = bptr[1]; bptr[1] = tmp;
            }

            player->chunk_buf_size = chunk_size;
            player->chunk_buf_pos = 0;

            uint16_t t_start = vflash_clock_ticks();
            decode_and_render_frame(player, fb);
            uint16_t t_end = vflash_clock_ticks();
            player->last_decode_time_us = (uint16_t)(t_end - t_start) * 100;

            player->current_frame++;

            /* In MJP bitstreams, each video frame chunk (00dc) is immediately followed by its
             * corresponding audio chunk (01wb). Ingest it immediately so the audio DMA ring
             * buffer is continuously saturated ahead of presentation, preventing starvation. */
            if (player->audio_enabled) {
                uint8_t a_hdr[12];
                int32_t ard = vflash_fs_read(player->fd, a_hdr, 12);
                if (ard == 12 && a_hdr[0] == '0' && a_hdr[1] == '1' && a_hdr[2] == 'w' && a_hdr[3] == 'b') {
                    uint32_t asize = a_hdr[8] | ((uint32_t)a_hdr[9] << 8) |
                                     ((uint32_t)a_hdr[10] << 16) | ((uint32_t)a_hdr[11] << 24);
                    if (asize <= VFLASH_VIDEO_CHUNK_BUF_SIZE) {
                        ard = vflash_fs_read(player->fd, player->chunk_buf, asize);
                        if (asize & 1) {
                            uint8_t pad;
                            vflash_fs_read(player->fd, &pad, 1);
                        }
                        if (ard == (int32_t)asize) {
                            if (player->audio_flags == 0x0001) {
                                uint32_t samples = decode_adpcm_chunk(player->chunk_buf, asize,
                                                                     player->audio_pcm_buf, 4096);
                                vflash_audio_write(player->audio_pcm_buf, samples * 2);
                                player->audio_samples_queued += samples;
                            } else if (player->audio_flags == 0x0004) {
                                vflash_audio_write(player->chunk_buf, asize);
                                player->audio_samples_queued += (asize / 2);
                            }
                        }
                    }
                } else if (ard > 0) {
                    /* Rewind header bytes if not an audio chunk */
                    vflash_fs_seek(player->fd, -ard, SEEK_CUR);
                }
            }
            break;
        }
    }

    /* Keep audio hardware DMA pumping and refresh playback clock after decode */
    sync_playback_clock(player);

    if (player->current_frame >= player->frame_count) {
        if (player->loop) {
            vflash_fs_seek(player->fd, (int32_t)player->data_start_offset, SEEK_SET);
            player->current_frame = 0;
            reset_playback_clock(player);
        } else {
            player->state = VFLASH_VIDEO_FINISHED;
        }
    }

    return 1;
}

int vflash_video_close(vflash_video_player_t *player) {
    if (!player) return -1;
    if (player->fd >= 0) {
        vflash_fs_close(player->fd);
        player->fd = -1;
    }
    if (player->audio_enabled) {
        vflash_audio_pause(true);
        player->audio_enabled = false;
    }
    player->state = VFLASH_VIDEO_STOPPED;
    player->chunk_buf_size = 0;
    player->chunk_buf_pos = 0;
    player->tick_accumulator = 0;
    player->frame_decoded = false;
    return 0;
}

vflash_video_state_t vflash_video_get_state(const vflash_video_player_t *player) {
    return player ? player->state : VFLASH_VIDEO_ERROR;
}

int vflash_video_get_stats(const vflash_video_player_t *player, vflash_video_stats_t *stats) {
    if (!player || !stats) return -1;
    stats->width = player->width;
    stats->height = player->height;
    stats->frame_count = player->frame_count;
    stats->fps = player->fps;
    stats->current_frame = player->current_frame;
    stats->audio_format = player->audio_flags;
    stats->audio_samples_queued = player->audio_samples_queued;
    stats->dropped_frames = player->dropped_frames;
    stats->decode_time_us = player->last_decode_time_us;
    stats->state = player->state;
    return 0;
}
