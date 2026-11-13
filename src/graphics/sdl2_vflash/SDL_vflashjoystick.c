#include "SDL_internal.h"

#ifdef SDL_JOYSTICK_PSP

#include "vflash/input.h"
#include "SDL_events.h"
#include "SDL_joystick.h"
#include "SDL_gamecontroller.h"
#include "joystick/SDL_sysjoystick.h"
#include "joystick/SDL_joystick_c.h"
#include "joystick/SDL_steam_virtual_gamepad.h"

// Freestanding stubs for Steam Virtual Gamepad
void SDL_InitSteamVirtualGamepadInfo(void) {}
SDL_bool SDL_SteamVirtualGamepadEnabled(void) { return SDL_FALSE; }
SDL_bool SDL_UpdateSteamVirtualGamepadInfo(void) { return SDL_FALSE; }
const SDL_SteamVirtualGamepadInfo *SDL_GetSteamVirtualGamepadInfo(int slot) { (void)slot; return NULL; }
void SDL_QuitSteamVirtualGamepadInfo(void) {}

static int VFLASH_JoystickInit(void)
{
    vflash_input_init();
    SDL_PrivateJoystickAdded(0);
    return 1;
}

static int VFLASH_JoystickGetCount(void)
{
    return 1;
}

static void VFLASH_JoystickDetect(void)
{
}

static const char *VFLASH_JoystickGetDeviceName(int device_index)
{
    if (device_index == 0) {
        return "VTech V.Flash Controller";
    }
    return NULL;
}

static const char *VFLASH_JoystickGetDevicePath(int device_index)
{
    if (device_index == 0) {
        return "vflash/joy0";
    }
    return NULL;
}

static int VFLASH_JoystickGetDeviceSteamVirtualGamepadSlot(int device_index)
{
    (void)device_index;
    return -1;
}

static int VFLASH_JoystickGetDevicePlayerIndex(int device_index)
{
    (void)device_index;
    return 0;
}

static void VFLASH_JoystickSetDevicePlayerIndex(int device_index, int player_index)
{
    (void)device_index;
    (void)player_index;
}

static SDL_JoystickGUID VFLASH_JoystickGetDeviceGUID(int device_index)
{
    const char *name = VFLASH_JoystickGetDeviceName(device_index);
    return SDL_CreateJoystickGUIDForName(name);
}

static SDL_JoystickID VFLASH_JoystickGetDeviceInstanceID(int device_index)
{
    return device_index;
}

static int VFLASH_JoystickOpen(SDL_Joystick *joystick, int device_index)
{
    (void)device_index;
    joystick->naxes = 2;
    joystick->nhats = 1;
    joystick->nbuttons = 7;
    joystick->instance_id = device_index;
    return 0;
}

static int VFLASH_JoystickRumble(SDL_Joystick *joystick, Uint16 low_frequency_rumble, Uint16 high_frequency_rumble)
{
    (void)joystick; (void)low_frequency_rumble; (void)high_frequency_rumble;
    return SDL_Unsupported();
}

static int VFLASH_JoystickRumbleTriggers(SDL_Joystick *joystick, Uint16 left_rumble, Uint16 right_rumble)
{
    (void)joystick; (void)left_rumble; (void)right_rumble;
    return SDL_Unsupported();
}

static Uint32 VFLASH_JoystickGetCapabilities(SDL_Joystick *joystick)
{
    (void)joystick;
    return 0;
}

static int VFLASH_JoystickSetLED(SDL_Joystick *joystick, Uint8 red, Uint8 green, Uint8 blue)
{
    (void)joystick; (void)red; (void)green; (void)blue;
    return SDL_Unsupported();
}

static int VFLASH_JoystickSendEffect(SDL_Joystick *joystick, const void *data, int size)
{
    (void)joystick; (void)data; (void)size;
    return SDL_Unsupported();
}

static int VFLASH_JoystickSetSensorsEnabled(SDL_Joystick *joystick, SDL_bool enabled)
{
    (void)joystick; (void)enabled;
    return SDL_Unsupported();
}

static void VFLASH_JoystickUpdate(SDL_Joystick *joystick)
{
    vflash_input_poll();
    const vflash_input_state_t *st = vflash_input_get_state();

    // Axes: 0 = X, 1 = Y
    SDL_PrivateJoystickAxis(joystick, 0, (Sint16)st->axis_x);
    SDL_PrivateJoystickAxis(joystick, 1, (Sint16)st->axis_y);

    // D-Pad Hat: 0
    Uint8 hat = SDL_HAT_CENTERED;
    if (st->buttons & VFLASH_BTN_UP)    hat |= SDL_HAT_UP;
    if (st->buttons & VFLASH_BTN_DOWN)  hat |= SDL_HAT_DOWN;
    if (st->buttons & VFLASH_BTN_LEFT)  hat |= SDL_HAT_LEFT;
    if (st->buttons & VFLASH_BTN_RIGHT) hat |= SDL_HAT_RIGHT;
    SDL_PrivateJoystickHat(joystick, 0, hat);

    // Buttons:
    // 0: OK (A)
    // 1: Red (B)
    // 2: Yellow (X)
    // 3: Green (Y)
    // 4: Blue (Guide)
    // 5: Stick (LeftStick)
    // 6: Exit (Back / Home)
    SDL_PrivateJoystickButton(joystick, 0, (st->buttons & VFLASH_BTN_OK) ? SDL_PRESSED : SDL_RELEASED);
    SDL_PrivateJoystickButton(joystick, 1, (st->buttons & VFLASH_BTN_RED) ? SDL_PRESSED : SDL_RELEASED);
    SDL_PrivateJoystickButton(joystick, 2, (st->buttons & VFLASH_BTN_YELLOW) ? SDL_PRESSED : SDL_RELEASED);
    SDL_PrivateJoystickButton(joystick, 3, (st->buttons & VFLASH_BTN_GREEN) ? SDL_PRESSED : SDL_RELEASED);
    SDL_PrivateJoystickButton(joystick, 4, (st->buttons & VFLASH_BTN_BLUE) ? SDL_PRESSED : SDL_RELEASED);
    SDL_PrivateJoystickButton(joystick, 5, (st->buttons & VFLASH_BTN_STICK) ? SDL_PRESSED : SDL_RELEASED);
    SDL_PrivateJoystickButton(joystick, 6, (st->buttons & VFLASH_BTN_EXIT) ? SDL_PRESSED : SDL_RELEASED);
}

static void VFLASH_JoystickClose(SDL_Joystick *joystick)
{
    (void)joystick;
}

static void VFLASH_JoystickQuit(void)
{
    vflash_input_shutdown();
}

static SDL_bool VFLASH_JoystickGetGamepadMapping(int device_index, SDL_GamepadMapping *out)
{
    (void)device_index;
    *out = (SDL_GamepadMapping){
        .a = { EMappingKind_Button, 0 },         // OK -> A
        .b = { EMappingKind_Button, 1 },         // Red -> B
        .x = { EMappingKind_Button, 2 },         // Yellow -> X
        .y = { EMappingKind_Button, 3 },         // Green -> Y
        .back = { EMappingKind_Button, 6 },      // Exit -> Back
        .guide = { EMappingKind_Button, 4 },     // Blue -> Guide
        .start = { EMappingKind_Button, 0 },     // OK -> Start
        .leftstick = { EMappingKind_Button, 5 }, // Stick -> LeftStick
        .rightstick = { EMappingKind_None, 255 },
        .leftshoulder = { EMappingKind_None, 255 },
        .rightshoulder = { EMappingKind_None, 255 },
        .dpup = { EMappingKind_Hat, 1 },         // Hat 0 Up (0x1)
        .dpdown = { EMappingKind_Hat, 4 },       // Hat 0 Down (0x4)
        .dpleft = { EMappingKind_Hat, 8 },       // Hat 0 Left (0x8)
        .dpright = { EMappingKind_Hat, 2 },      // Hat 0 Right (0x2)
        .misc1 = { EMappingKind_None, 255 },
        .paddle1 = { EMappingKind_None, 255 },
        .paddle2 = { EMappingKind_None, 255 },
        .paddle3 = { EMappingKind_None, 255 },
        .paddle4 = { EMappingKind_None, 255 },
        .leftx = { EMappingKind_Axis, 0 },
        .lefty = { EMappingKind_Axis, 1 },
        .rightx = { EMappingKind_None, 255 },
        .righty = { EMappingKind_None, 255 },
        .lefttrigger = { EMappingKind_None, 255 },
        .righttrigger = { EMappingKind_None, 255 },
    };
    return SDL_TRUE;
}

SDL_JoystickDriver SDL_VFLASH_JoystickDriver = {
    VFLASH_JoystickInit,
    VFLASH_JoystickGetCount,
    VFLASH_JoystickDetect,
    VFLASH_JoystickGetDeviceName,
    VFLASH_JoystickGetDevicePath,
    VFLASH_JoystickGetDeviceSteamVirtualGamepadSlot,
    VFLASH_JoystickGetDevicePlayerIndex,
    VFLASH_JoystickSetDevicePlayerIndex,
    VFLASH_JoystickGetDeviceGUID,
    VFLASH_JoystickGetDeviceInstanceID,
    VFLASH_JoystickOpen,
    VFLASH_JoystickRumble,
    VFLASH_JoystickRumbleTriggers,
    VFLASH_JoystickGetCapabilities,
    VFLASH_JoystickSetLED,
    VFLASH_JoystickSendEffect,
    VFLASH_JoystickSetSensorsEnabled,
    VFLASH_JoystickUpdate,
    VFLASH_JoystickClose,
    VFLASH_JoystickQuit,
    VFLASH_JoystickGetGamepadMapping
};

#endif // SDL_JOYSTICK_PSP
