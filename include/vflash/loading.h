#ifndef VFLASH_LOADING_H
#define VFLASH_LOADING_H

#include <stdint.h>
#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

/* Splash screen presentation configuration */
typedef struct {
    const char *title;     /* Primary title (default: "V.FLASH SDK") */
    const char *subtitle;  /* Application / Demo subtitle (default: "Homebrew Application") */
    const char *caption;   /* Descriptive caption (default: "Open-Source Platform") */
    const char *copyright; /* Copyright notice (default: "(C) 2026 SDK Contributors") */
} vflash_loading_config_t;

/**
 * Initialize the retail V.DISC-style loading screen with custom splash configuration.
 * Displays the initial intro splash and sets up double buffering.
 */
void vflash_loading_init_ex(const vflash_loading_config_t *config);

/**
 * Initialize the retail V.DISC-style loading screen with generic SDK defaults.
 * Displays the initial SDK intro splash and sets up double buffering.
 */
void vflash_loading_init(void);

/**
 * Dynamically configure splash title and subtitle text.
 */
void vflash_loading_set_title(const char *title, const char *subtitle);

/**
 * Set the current loading progress percentage (0 to 100).
 */
void vflash_loading_set_progress(unsigned percent);

/**
 * Set the current status diagnostic string (e.g. "Mounting V.DISC").
 * Pass NULL to show default animated "Loading...".
 */
void vflash_loading_set_status(const char *status);

/**
 * Render the loading screen to the display (flicker-free double-buffered).
 */
void vflash_loading_render(void);

/**
 * Cleanly finish loading and transition to the application.
 */
void vflash_loading_finish(void);

/**
 * Query current loading progress percentage.
 */
unsigned vflash_loading_get_progress(void);

/**
 * Query current loading status string.
 */
const char *vflash_loading_get_status(void);

#ifdef __cplusplus
}
#endif

#endif /* VFLASH_LOADING_H */
