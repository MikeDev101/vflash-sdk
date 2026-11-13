#ifndef VFLASH_VIDEO_H
#define VFLASH_VIDEO_H

#include "vflash/framebuffer.h"
#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

//
// Initialize linear BGR555 double-buffered video scanout at 320x240.
//
// fb: Output framebuffer descriptor initialized to the active buffer.
//
// Returns 1 on success, or 0 if hardware register setup fails.
//
int vflash_video_init(vflash_framebuffer_t *fb);

//
// Wait for V-Blank and flip display scanout to the given framebuffer.
//
// fb: Framebuffer descriptor pointing to the completed backbuffer.
//
// Returns 1 on success, or 0 on invalid descriptor or V-Blank timeout.
//
int vflash_video_present(const vflash_framebuffer_t *fb);

//
// Populate fb with the non-visible backbuffer for double-buffered drawing.
//
// fb: Output framebuffer descriptor set to the current backbuffer.
//
// Returns 1 on success, or 0 if fb is NULL or double buffering is disabled.
//
int vflash_video_backbuffer(vflash_framebuffer_t *fb);

//
// Query which framebuffer bank is currently displayed on scanout.
//
// Returns 0 for primary bank, or 1 for secondary bank.
//
uint32_t vflash_video_front_bank(void);

//
// Present an experimental Zevio tiled view at (x, y) on screen.
//
// physical_address: 64-byte aligned SDRAM physical base address.
// backing_bytes:    Total byte size of the tiled backing surface.
// width:            Visible view width in pixels.
// height:           Visible view height in pixels.
// x:                Destination screen X coordinate.
// y:                Destination screen Y coordinate.
//
// Returns 1 on success, or 0 on invalid placement or timeout.
//
int vflash_video_present_tiled_view(uint32_t physical_address,
                                    uint32_t backing_bytes,
                                    uint32_t width, uint32_t height,
                                    int32_t x, int32_t y);

//
// Present an experimental Zevio tiled surface with explicit dimensions.
//
// physical_address: 64-byte aligned SDRAM physical base address.
// width:            Surface width in pixels.
// height:           Surface height in pixels.
// x:                Destination screen X coordinate.
// y:                Destination screen Y coordinate.
//
// Returns 1 on success, or 0 on invalid placement or timeout.
//
int vflash_video_present_tiled_ex(uint32_t physical_address,
                                  uint32_t width, uint32_t height,
                                  int32_t x, int32_t y);

//
// Present a default 64x16 experimental Zevio tiled surface at (x, y).
//
// physical_address: 64-byte aligned SDRAM physical base address.
// x:                Destination screen X coordinate.
// y:                Destination screen Y coordinate.
//
// Returns 1 on success, or 0 on failure.
//
int vflash_video_present_tiled(uint32_t physical_address, int32_t x, int32_t y);

//
// Restore linear BGR555 framebuffer scanout after tiled presentation.
//
// Returns 1 on success, or 0 on failure.
//
int vflash_video_restore_linear(void);

//
// Query the active tiled scanout physical address.
//
// Returns the tiled physical address, or 0 when linear scanout is active.
//
uint32_t vflash_video_tiled_address(void);

//
// Query the byte size of the active tiled scanout surface.
//
// Returns the tiled surface byte count, or 0 when linear scanout is active.
//
uint32_t vflash_video_tiled_bytes(void);

//
// Native MJP Motion-JPEG playback states.
//
typedef enum
{
    VFLASH_VIDEO_STOPPED = 0,
    VFLASH_VIDEO_PLAYING,
    VFLASH_VIDEO_PAUSED,
    VFLASH_VIDEO_FINISHED,
    VFLASH_VIDEO_ERROR
} vflash_video_state_t;

//
// Snapshot of MJP video player telemetry and stream metadata.
//
typedef struct
{
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

//
// Maximum compressed video or audio chunk size in bytes.
//
#define VFLASH_VIDEO_CHUNK_BUF_SIZE (48 * 1024)

//
// MJP video player instance state and aligned decode scratch buffers.
//
typedef struct vflash_video_player
{
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

    bool frame_decoded;

    uint32_t chunk_buf_size;
    uint32_t chunk_buf_pos;
    uint8_t chunk_buf[VFLASH_VIDEO_CHUNK_BUF_SIZE] __attribute__((aligned(4)));

    int16_t audio_pcm_buf[4096] __attribute__((aligned(4)));
} vflash_video_player_t;

//
// Open an MJP video file from the ISO 9660 filesystem and parse its header.
//
// player: Player instance to initialize.
// path:   Disc file path to the MJP stream.
// x:      Destination X coordinate on the framebuffer.
// y:      Destination Y coordinate on the framebuffer.
//
// Returns 0 on success, or a negative error code on failure.
//
int vflash_video_open(vflash_video_player_t *player, const char *path,
                      int x, int y);

//
// Start or resume MJP video and audio playback.
//
// player: Open player instance.
//
// Returns 0 on success, or -1 if player is invalid.
//
int vflash_video_play(vflash_video_player_t *player);

//
// Pause MJP video and audio playback, preserving current position.
//
// player: Open player instance.
//
// Returns 0 on success, or -1 if player is invalid.
//
int vflash_video_pause(vflash_video_player_t *player);

//
// Stop MJP playback, reset audio and stream position to frame 0.
//
// player: Open player instance.
//
// Returns 0 on success, or -1 if player is invalid.
//
int vflash_video_stop(vflash_video_player_t *player);

//
// Stop MJP playback, reset audio and frame position, and start playing.
//
// player: Open player instance.
//
// Returns 0 on success, or -1 if player is invalid.
//
int vflash_video_restart(vflash_video_player_t *player);

//
// Enable or disable automatic looping at end of stream.
//
// player: Open player instance.
// loop:   True to loop continuously, false to stop at end of stream.
//
// Returns 0 on success, or -1 if player is invalid.
//
int vflash_video_set_loop(vflash_video_player_t *player, bool loop);

//
// Advance audio-mastered timing and decode/render the next frame onto fb
// when its presentation deadline is reached.
//
// player: Playing MJP player instance.
// fb:     Target framebuffer to receive decoded pixels.
//
// Returns 1 if a frame was decoded and rendered, or 0 otherwise.
//
int vflash_video_update(vflash_video_player_t *player,
                        vflash_framebuffer_t *fb);

//
// Close the MJP stream, pause audio, and reset player state.
//
// player: Player instance to close.
//
// Returns 0 on success, or -1 if player is NULL.
//
int vflash_video_close(vflash_video_player_t *player);

//
// Query the current playback state of an MJP player.
//
// player: Player instance to query.
//
// Returns the current vflash_video_state_t, or VFLASH_VIDEO_ERROR if NULL.
//
vflash_video_state_t vflash_video_get_state(const vflash_video_player_t *player);

//
// Copy current stream metadata and playback statistics into stats.
//
// player: Player instance to query.
// stats:  Output statistics structure.
//
// Returns 0 on success, or -1 if player or stats is NULL.
//
int vflash_video_get_stats(const vflash_video_player_t *player,
                           vflash_video_stats_t *stats);

#ifdef __cplusplus
}
#endif

#endif // VFLASH_VIDEO_H
