#ifndef VFLASH_VIDEO_H
#define VFLASH_VIDEO_H
#include "vflash/framebuffer.h"
#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

int vflash_video_init(vflash_framebuffer_t *fb);
int vflash_video_present(const vflash_framebuffer_t *fb);
int vflash_video_backbuffer(vflash_framebuffer_t *fb);
uint32_t vflash_video_front_bank(void);
/* Experimental native tiled view with checked placement; called by GPU HAL. */
int vflash_video_present_tiled_view(uint32_t physical_address,uint32_t backing_bytes,uint32_t width,uint32_t height,int32_t x,int32_t y);
int vflash_video_present_tiled_ex(uint32_t physical_address,uint32_t width,uint32_t height,int32_t x,int32_t y);
int vflash_video_present_tiled(uint32_t physical_address,int32_t x,int32_t y);
int vflash_video_restore_linear(void);
uint32_t vflash_video_tiled_address(void);
uint32_t vflash_video_tiled_bytes(void);

/* Native MJP Video Playback API */

typedef enum {
    VFLASH_VIDEO_STOPPED = 0,
    VFLASH_VIDEO_PLAYING,
    VFLASH_VIDEO_PAUSED,
    VFLASH_VIDEO_FINISHED,
    VFLASH_VIDEO_ERROR
} vflash_video_state_t;

typedef struct {
    uint32_t width;
    uint32_t height;
    uint32_t frame_count;
    uint32_t fps;
    uint32_t current_frame;
    uint32_t audio_format;
    uint32_t audio_samples_queued;
    uint32_t dropped_frames;
    uint32_t decode_time_us;
    vflash_video_state_t state;
} vflash_video_stats_t;

#define VFLASH_VIDEO_CHUNK_BUF_SIZE (48 * 1024)

typedef struct vflash_video_player {
    int fd;
    uint32_t width;
    uint32_t height;
    uint32_t frame_count;
    uint32_t fps;
    uint32_t audio_flags;
    uint32_t payload_size;
    uint32_t data_start_offset;

    int dst_x;
    int dst_y;

    vflash_video_state_t state;
    uint32_t current_frame;
    uint16_t last_tick;
    uint32_t tick_accumulator;
    uint32_t ticks_per_frame;

    bool loop;
    bool audio_enabled;
    uint32_t audio_samples_queued;
    uint32_t audio_samples_base;
    uint32_t audio_clock_base_ticks;
    uint32_t dropped_frames;
    uint32_t last_decode_time_us;

    /* Pipelined backbuffer decode state */
    bool frame_decoded;

    /* Frame bitstream buffer */
    uint32_t chunk_buf_size;
    uint32_t chunk_buf_pos;
    uint8_t chunk_buf[VFLASH_VIDEO_CHUNK_BUF_SIZE] __attribute__((aligned(4)));

    /* Audio scratch buffer */
    int16_t audio_pcm_buf[4096] __attribute__((aligned(4)));
} vflash_video_player_t;

int vflash_video_open(vflash_video_player_t *player, const char *path, int x, int y);
int vflash_video_play(vflash_video_player_t *player);
int vflash_video_pause(vflash_video_player_t *player);
int vflash_video_stop(vflash_video_player_t *player);
int vflash_video_restart(vflash_video_player_t *player);
int vflash_video_set_loop(vflash_video_player_t *player, bool loop);
int vflash_video_update(vflash_video_player_t *player, vflash_framebuffer_t *fb);
int vflash_video_close(vflash_video_player_t *player);

vflash_video_state_t vflash_video_get_state(const vflash_video_player_t *player);
int vflash_video_get_stats(const vflash_video_player_t *player, vflash_video_stats_t *stats);

#ifdef __cplusplus
}
#endif

#endif
