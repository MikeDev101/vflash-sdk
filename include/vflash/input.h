#ifndef VFLASH_INPUT_H
#define VFLASH_INPUT_H

#include <stdbool.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/*
 * Digital button bitmask flags for V.Flash controller.
 */
#define VFLASH_BTN_UP       (1u << 0)
#define VFLASH_BTN_DOWN     (1u << 1)
#define VFLASH_BTN_LEFT     (1u << 2)
#define VFLASH_BTN_RIGHT    (1u << 3)
#define VFLASH_BTN_RED      (1u << 4)   /* Action button 1 */
#define VFLASH_BTN_YELLOW   (1u << 5)   /* Action button 2 */
#define VFLASH_BTN_GREEN    (1u << 6)   /* Action button 3 */
#define VFLASH_BTN_BLUE     (1u << 7)   /* Action button 4 */
#define VFLASH_BTN_OK       (1u << 8)   /* Primary / Center / Enter */
#define VFLASH_BTN_STICK    (1u << 9)   /* Thumbstick button */
#define VFLASH_BTN_EXIT     (1u << 10)  /* Exit / Home button */

/* Total number of mapped digital buttons */
#define VFLASH_BTN_COUNT    11

/*
 * V.Flash controller input state snapshot.
 */
typedef struct vflash_input_state {
    uint32_t buttons;       /* Bitmask of currently held digital buttons */
    uint32_t pressed;       /* Bitmask of buttons pressed in this poll (0 -> 1) */
    uint32_t released;      /* Bitmask of buttons released in this poll (1 -> 0) */
    int16_t axis_x;         /* Normalized analog X axis: -32768 (Left) to +32767 (Right), 0 center */
    int16_t axis_y;         /* Normalized analog Y axis: -32768 (Up) to +32767 (Down), 0 center */
    uint16_t raw_x;         /* Raw 12-bit X axis: ~256 (Left), 2048 (Center), ~3840 (Right) */
    uint16_t raw_y;         /* Raw 12-bit Y axis: ~3840 (Up), 2048 (Center), ~256 (Down) */
    bool connected;         /* True if controller is responding to polls */
} vflash_input_state_t;

/*
 * Initialize the V.Flash native controller input driver.
 * Configures UART0 for controller communication.
 * Returns 0 on success, negative error code on failure.
 */
int vflash_input_init(void);

/*
 * Poll the V.Flash controller for the latest input state.
 * Sends a poll request to the controller, processes incoming packets,
 * updates axes, edge transitions (pressed/released), and connection status.
 */
void vflash_input_poll(void);

/*
 * Retrieve the current controller state.
 * Returns a pointer to the static input state structure.
 */
const vflash_input_state_t *vflash_input_get_state(void);

/*
 * Shutdown the V.Flash input driver and release hardware resources.
 */
void vflash_input_shutdown(void);

/*
 * Edge detection helper: returns true if the specified button was pressed this poll.
 */
static inline bool vflash_input_is_pressed(const vflash_input_state_t *state, uint32_t button_mask)
{
    return state && ((state->pressed & button_mask) != 0);
}

/*
 * Edge detection helper: returns true if the specified button was released this poll.
 */
static inline bool vflash_input_is_released(const vflash_input_state_t *state, uint32_t button_mask)
{
    return state && ((state->released & button_mask) != 0);
}

/*
 * Button state helper: returns true if the specified button is currently held.
 */
static inline bool vflash_input_is_held(const vflash_input_state_t *state, uint32_t button_mask)
{
    return state && ((state->buttons & button_mask) != 0);
}

#ifdef __cplusplus
}
#endif

#endif /* VFLASH_INPUT_H */
