#ifndef VFLASH_LOADING_H
#define VFLASH_LOADING_H

#include <stdbool.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

//
// Splash screen text configuration for the retail-style loading screen.
//
typedef struct
{
    const char *title;     // Primary title (default: "V.FLASH SDK")
    const char *subtitle;  // Application subtitle (default: "Homebrew App")
    const char *caption;   // Descriptive caption (default: "Open-Source SDK")
    const char *copyright; // Copyright notice
} vflash_loading_config_t;

//
// Initialize the loading screen with custom splash text and set up
// double-buffered video output.
//
// config: Optional splash text configuration; NULL uses SDK defaults.
//
void vflash_loading_init_ex(const vflash_loading_config_t *config);

//
// Initialize the loading screen with default SDK splash text.
//
void vflash_loading_init(void);

//
// Dynamically update the loading screen title and subtitle strings.
//
// title:    Primary title string, or NULL to keep current value.
// subtitle: Subtitle string, or NULL to keep current value.
//
void vflash_loading_set_title(const char *title, const char *subtitle);

//
// Set the loading progress bar percentage, clamped to 0..100.
//
// percent: Progress percentage from 0 to 100.
//
void vflash_loading_set_progress(unsigned percent);

//
// Set the loading status diagnostic string.
//
// status: Status text to display, or NULL for animated "Loading...".
//
void vflash_loading_set_status(const char *status);

//
// Render the loading screen to the backbuffer and present it.
//
void vflash_loading_render(void);

//
// Finish the loading sequence and clear both framebuffers to black.
//
void vflash_loading_finish(void);

//
// Query the current loading progress percentage.
//
// Returns the clamped progress percentage (0..100).
//
unsigned vflash_loading_get_progress(void);

//
// Query the current loading status string.
//
// Returns a pointer to the active status string, or NULL if unset.
//
const char *vflash_loading_get_status(void);

#ifdef __cplusplus
}
#endif

#endif // VFLASH_LOADING_H
