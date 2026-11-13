#ifndef VFLASH_INPUT_H
#define VFLASH_INPUT_H

#include <stdbool.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

//
// Digital button bitmask flags for the V.Flash controller.
//
#define VFLASH_BTN_UP       (1u << 0)
#define VFLASH_BTN_DOWN     (1u << 1)
#define VFLASH_BTN_LEFT     (1u << 2)
#define VFLASH_BTN_RIGHT    (1u << 3)
#define VFLASH_BTN_RED      (1u << 4)  // Action button 1
#define VFLASH_BTN_YELLOW   (1u << 5)  // Action button 2
#define VFLASH_BTN_GREEN    (1u << 6)  // Action button 3
#define VFLASH_BTN_BLUE     (1u << 7)  // Action button 4
#define VFLASH_BTN_OK       (1u << 8)  // Primary / Center / Enter
#define VFLASH_BTN_STICK    (1u << 9)  // Thumbstick button
#define VFLASH_BTN_EXIT     (1u << 10) // Exit / Home button

//
// Total number of mapped digital controller buttons.
//
#define VFLASH_BTN_COUNT    11

//
// V.Flash controller input state snapshot.
//
typedef struct vflash_input_state
{
    uint32_t buttons;  // Bitmask of currently held digital buttons
    uint32_t pressed;  // Bitmask of buttons pressed this poll (0 -> 1)
    uint32_t released; // Bitmask of buttons released this poll (1 -> 0)
    int16_t axis_x;    // Normalized X axis (-32768 left .. +32767 right)
    int16_t axis_y;    // Normalized Y axis (-32768 up .. +32767 down)
    uint16_t raw_x;    // Raw 12-bit X axis (~256 left, 2048 center, ~3840 right)
    uint16_t raw_y;    // Raw 12-bit Y axis (~3840 up, 2048 center, ~256 down)
    bool connected;    // True if the controller responded to the poll
} vflash_input_state_t;

//
// Initialize the V.Flash native controller input driver over UART0.
//
// Returns 0 on success, or a negative error code on failure.
//
int vflash_input_init(void);

//
// Poll the V.Flash controller for the latest input packet and update
// digital edge transitions (pressed/released), analog axes, and status.
//
void vflash_input_poll(void);

//
// Retrieve the current static controller input state snapshot.
//
// Returns a pointer to the internal vflash_input_state_t structure.
//
const vflash_input_state_t *vflash_input_get_state(void);

//
// Shut down the V.Flash input driver and release hardware resources.
//
void vflash_input_shutdown(void);

//
// Test whether any button in button_mask transitioned from released to
// pressed during the most recent poll.
//
// state:       Controller state snapshot from vflash_input_get_state().
// button_mask: Bitmask of VFLASH_BTN_* flags to test.
//
// Returns true if any masked button was pressed this poll, else false.
//
static inline bool vflash_input_is_pressed(const vflash_input_state_t *state,
                                          uint32_t button_mask)
{
    return state && ((state->pressed & button_mask) != 0);
}

//
// Test whether any button in button_mask transitioned from pressed to
// released during the most recent poll.
//
// state:       Controller state snapshot from vflash_input_get_state().
// button_mask: Bitmask of VFLASH_BTN_* flags to test.
//
// Returns true if any masked button was released this poll, else false.
//
static inline bool vflash_input_is_released(const vflash_input_state_t *state,
                                           uint32_t button_mask)
{
    return state && ((state->released & button_mask) != 0);
}

//
// Test whether any button in button_mask is currently held down.
//
// state:       Controller state snapshot from vflash_input_get_state().
// button_mask: Bitmask of VFLASH_BTN_* flags to test.
//
// Returns true if any masked button is held, else false.
//
static inline bool vflash_input_is_held(const vflash_input_state_t *state,
                                       uint32_t button_mask)
{
    return state && ((state->buttons & button_mask) != 0);
}

#ifdef __cplusplus
}
#endif

#endif // VFLASH_INPUT_H
