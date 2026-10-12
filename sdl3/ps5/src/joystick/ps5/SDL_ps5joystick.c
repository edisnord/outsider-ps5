/*
  Simple DirectMedia Layer
  Copyright (C) 1997-2026 Sam Lantinga <slouken@libsdl.org>

  This software is provided 'as-is', without any express or implied
  warranty.  In no event will the authors be held liable for any damages
  arising from the use of this software.

  Permission is granted to anyone to use this software for any purpose,
  including commercial applications, and to alter it and redistribute it
  freely, subject to the following restrictions:

  1. The origin of this software must not be misrepresented; you must not
     claim that you wrote the original software. If you use this software
     in a product, an acknowledgment in the product documentation would be
     appreciated but is not required.
  2. Altered source versions must be plainly marked as such, and must not be
     misrepresented as being the original software.
  3. This notice may not be removed or altered from any source distribution.
*/
/* PS5 DualSense pads for SDL3: the SDL2 PS5 port's joystick backend (ps5-payload-dev/SDL,
 * src/joystick/ps5), with ps5-opengl's enumeration and handle fixes, moved to SDL3's driver
 * interface. Altered source: Outsider PS5. One pad per logged-in user, up to four. */
#include "SDL_internal.h"

#ifdef SDL_JOYSTICK_PS5

#include "../SDL_sysjoystick.h"
#include "../SDL_joystick_c.h"
#include "SDL_ps5joystick.h"

#define PS5_MAX_USERS 4

#define PS5_PAD_AXIS_LX 0
#define PS5_PAD_AXIS_LY 1
#define PS5_PAD_AXIS_RX 2
#define PS5_PAD_AXIS_RY 3
#define PS5_PAD_AXIS_L2 4
#define PS5_PAD_AXIS_R2 5

typedef struct PS5_PadContext
{
    int user_id;
    char user_name[255];
    int handle;
    SDL_JoystickID instance_id; /* 0: not connected */
    PS5_PadData pad;
} PS5_PadContext;

static PS5_PadContext pad_ctx[PS5_MAX_USERS];

/* The pad's buttons in SDL's order for a PlayStation controller: a b x y back guide start leftstick
 * rightstick leftshoulder rightshoulder dpup dpdown dpleft dpright, then the touchpad click. */
static const Uint32 btn_map[] = {
    PS5_PAD_BUTTON_CROSS,     /* b0: a */
    PS5_PAD_BUTTON_CIRCLE,    /* b1: b */
    PS5_PAD_BUTTON_SQUARE,    /* b2: x */
    PS5_PAD_BUTTON_TRIANGLE,  /* b3: y */
    PS5_PAD_BUTTON_TOUCH_PAD, /* b4: touchpad */
    0,                        /* b5: back (none) */
    PS5_PAD_BUTTON_OPTIONS,   /* b6: start */
    PS5_PAD_BUTTON_L3,        /* b7: leftstick */
    PS5_PAD_BUTTON_R3,        /* b8: rightstick */
    PS5_PAD_BUTTON_L1,        /* b9: leftshoulder */
    PS5_PAD_BUTTON_R1,        /* b10: rightshoulder */
    PS5_PAD_BUTTON_UP,        /* b11: dpup */
    PS5_PAD_BUTTON_DOWN,      /* b12: dpdown */
    PS5_PAD_BUTTON_LEFT,      /* b13: dpleft */
    PS5_PAD_BUTTON_RIGHT,     /* b14: dpright */
};

/* Sticks and triggers: 0..255, centre 128, to SDL's -32768..32767 */
static Sint16 analog(Uint8 value)
{
    return (Sint16)((int)value * 257 - 32768);
}

static PS5_PadContext *PS5_PadAtIndex(int device_index)
{
    for (int i = 0; i < PS5_MAX_USERS; ++i) {
        if (pad_ctx[i].instance_id && device_index-- == 0) {
            return &pad_ctx[i];
        }
    }
    return NULL;
}

static PS5_PadContext *PS5_PadForJoystick(SDL_Joystick *joystick)
{
    SDL_JoystickID id = SDL_GetJoystickID(joystick);
    for (int i = 0; i < PS5_MAX_USERS; ++i) {
        if (pad_ctx[i].instance_id == id) {
            return &pad_ctx[i];
        }
    }
    return NULL;
}

static void PS5_RemovePad(PS5_PadContext *ctx)
{
    SDL_JoystickID id = ctx->instance_id;
    ctx->instance_id = 0;
    if (id) {
        SDL_PrivateJoystickRemoved(id);
    }
}

static void PS5_ClosePad(PS5_PadContext *ctx)
{
    if (ctx->handle >= 0) {
        scePadClose(ctx->handle);
        ctx->handle = -1;
    }
}

static void PS5_JoystickDetect(void)
{
    int user_ids[PS5_MAX_USERS];
    if (sceUserServiceGetLoginUserIdList(user_ids) != 0) {
        return; /* a failed read is not a disconnect */
    }
    /* Match users by id, not by position in the list: a reordered list must not reconnect pads */
    for (int i = 0; i < PS5_MAX_USERS; ++i) {
        PS5_PadContext *ctx = &pad_ctx[i];
        bool found = false;
        for (int j = 0; j < PS5_MAX_USERS; ++j) {
            found |= user_ids[j] == ctx->user_id;
        }
        if (ctx->user_id != -1 && !found) {
            PS5_RemovePad(ctx);
            PS5_ClosePad(ctx);
            ctx->user_id = -1;
        }
    }
    for (int i = 0; i < PS5_MAX_USERS; ++i) {
        PS5_PadContext *ctx = NULL;
        PS5_PadData sample;
        if (user_ids[i] == -1) {
            continue;
        }
        for (int j = 0; j < PS5_MAX_USERS; ++j) {
            if (pad_ctx[j].user_id == user_ids[i]) {
                ctx = &pad_ctx[j];
            }
        }
        if (!ctx) {
            for (int j = 0; j < PS5_MAX_USERS && !ctx; ++j) {
                if (pad_ctx[j].user_id == -1) {
                    ctx = &pad_ctx[j];
                }
            }
            if (!ctx) {
                continue;
            }
            ctx->user_id = user_ids[i];
            if (sceUserServiceGetUserName(ctx->user_id, ctx->user_name, sizeof(ctx->user_name)) != 0) {
                SDL_snprintf(ctx->user_name, sizeof(ctx->user_name), "DualSense %08x", ctx->user_id);
            }
        }
        /* The handle stays open while the pad is unplugged, so that plugging it in again is seen */
        if (ctx->handle < 0) {
            ctx->handle = scePadOpen(ctx->user_id, 0, 0, NULL);
            if (ctx->handle < 0) {
                continue;
            }
            scePadSetVibrationMode(ctx->handle, 2);
        }
        SDL_zero(sample);
        if (scePadReadState(ctx->handle, &sample) != 0) {
            continue;
        }
        if (!sample.connected) {
            PS5_RemovePad(ctx);
        } else if (!ctx->instance_id) {
            ctx->instance_id = SDL_GetNextObjectID();
            SDL_zero(ctx->pad);
            SDL_PrivateJoystickAdded(ctx->instance_id);
        }
    }
}

static bool PS5_JoystickInit(void)
{
    for (int i = 0; i < PS5_MAX_USERS; ++i) {
        pad_ctx[i].user_id = -1;
        pad_ctx[i].user_name[0] = '\0';
        pad_ctx[i].handle = -1;
        pad_ctx[i].instance_id = 0;
    }
    int err = sceUserServiceInitialize(NULL);
    if (err != 0 && err != (int)0x80960003) { /* already initialized */
        return SDL_SetError("sceUserServiceInitialize: 0x%08x", err);
    }
    err = scePadInit();
    if (err != 0) {
        return SDL_SetError("scePadInit: 0x%08x", err);
    }
    PS5_JoystickDetect();
    return true;
}

static int PS5_JoystickGetCount(void)
{
    int n = 0;
    for (int i = 0; i < PS5_MAX_USERS; ++i) {
        n += pad_ctx[i].instance_id != 0;
    }
    return n;
}

static bool PS5_JoystickIsDevicePresent(Uint16 vendor_id, Uint16 product_id, Uint16 version, const char *name)
{
    return false;
}

static const char *PS5_JoystickGetDeviceName(int device_index)
{
    PS5_PadContext *ctx = PS5_PadAtIndex(device_index);
    return ctx ? ctx->user_name : NULL;
}

static const char *PS5_JoystickGetDevicePath(int device_index)
{
    return NULL;
}

static int PS5_JoystickGetDeviceSteamVirtualGamepadSlot(int device_index)
{
    return -1;
}

static int PS5_JoystickGetDevicePlayerIndex(int device_index)
{
    return -1;
}

static void PS5_JoystickSetDevicePlayerIndex(int device_index, int player_index)
{
}

static SDL_GUID PS5_JoystickGetDeviceGUID(int device_index)
{
    /* Sony DualSense: USB vendor 054c, product 0ce6 */
    return SDL_CreateJoystickGUID(SDL_HARDWARE_BUS_UNKNOWN, 0x054c, 0x0ce6, 0, NULL, "PS5 Controller", 0, 0);
}

static SDL_JoystickID PS5_JoystickGetDeviceInstanceID(int device_index)
{
    PS5_PadContext *ctx = PS5_PadAtIndex(device_index);
    return ctx ? ctx->instance_id : 0;
}

static bool PS5_JoystickOpen(SDL_Joystick *joystick, int device_index)
{
    PS5_PadContext *ctx = PS5_PadAtIndex(device_index);
    if (!ctx) {
        return SDL_SetError("PS5: no pad at index %d", device_index);
    }
    SDL_zero(ctx->pad);
    joystick->nbuttons = SDL_arraysize(btn_map);
    joystick->naxes = 6;
    joystick->nhats = 0;
    SDL_SetBooleanProperty(SDL_GetJoystickProperties(joystick), SDL_PROP_JOYSTICK_CAP_RGB_LED_BOOLEAN, true);
    SDL_SetBooleanProperty(SDL_GetJoystickProperties(joystick), SDL_PROP_JOYSTICK_CAP_RUMBLE_BOOLEAN, true);
    return true;
}

static bool PS5_JoystickRumble(SDL_Joystick *joystick, Uint16 low_frequency_rumble, Uint16 high_frequency_rumble)
{
    PS5_PadContext *ctx = PS5_PadForJoystick(joystick);
    PS5_PadVibration vib = { (uint8_t)(low_frequency_rumble >> 8), (uint8_t)(high_frequency_rumble >> 8) };
    if (!ctx) {
        return SDL_SetError("PS5: pad not connected");
    }
    int err = scePadSetVibration(ctx->handle, &vib);
    return err == 0 ? true : SDL_SetError("scePadSetVibration: 0x%08x", err);
}

static bool PS5_JoystickRumbleTriggers(SDL_Joystick *joystick, Uint16 left_rumble, Uint16 right_rumble)
{
    return SDL_Unsupported();
}

static bool PS5_JoystickSetLED(SDL_Joystick *joystick, Uint8 red, Uint8 green, Uint8 blue)
{
    PS5_PadContext *ctx = PS5_PadForJoystick(joystick);
    PS5_PadColor color = { red, green, blue, 255 };
    if (!ctx) {
        return SDL_SetError("PS5: pad not connected");
    }
    int err = scePadSetLightBar(ctx->handle, &color);
    return err == 0 ? true : SDL_SetError("scePadSetLightBar: 0x%08x", err);
}

static bool PS5_JoystickSendEffect(SDL_Joystick *joystick, const void *data, int size)
{
    return SDL_Unsupported();
}

static bool PS5_JoystickSetSensorsEnabled(SDL_Joystick *joystick, bool enabled)
{
    return SDL_Unsupported();
}

static void PS5_JoystickUpdate(SDL_Joystick *joystick)
{
    PS5_PadContext *ctx = PS5_PadForJoystick(joystick);
    PS5_PadData pad;
    Uint64 timestamp = SDL_GetTicksNS();

    if (!ctx || scePadReadState(ctx->handle, &pad) != 0 || !pad.connected) {
        return; /* detection owns removal */
    }
    SDL_SendJoystickAxis(timestamp, joystick, PS5_PAD_AXIS_LX, analog(pad.leftStick.x));
    SDL_SendJoystickAxis(timestamp, joystick, PS5_PAD_AXIS_LY, analog(pad.leftStick.y));
    SDL_SendJoystickAxis(timestamp, joystick, PS5_PAD_AXIS_RX, analog(pad.rightStick.x));
    SDL_SendJoystickAxis(timestamp, joystick, PS5_PAD_AXIS_RY, analog(pad.rightStick.y));
    SDL_SendJoystickAxis(timestamp, joystick, PS5_PAD_AXIS_L2, analog(pad.analogButtons.l2));
    SDL_SendJoystickAxis(timestamp, joystick, PS5_PAD_AXIS_R2, analog(pad.analogButtons.r2));

    Uint32 changed = ctx->pad.buttons ^ pad.buttons;
    for (int i = 0; i < (int)SDL_arraysize(btn_map); ++i) {
        if (btn_map[i] && (changed & btn_map[i])) {
            SDL_SendJoystickButton(timestamp, joystick, (Uint8)i, (pad.buttons & btn_map[i]) != 0);
        }
    }
    SDL_memcpy(&ctx->pad, &pad, sizeof(pad));
}

static void PS5_JoystickClose(SDL_Joystick *joystick)
{
    /* the native handle belongs to detection */
}

static void PS5_JoystickQuit(void)
{
    for (int i = 0; i < PS5_MAX_USERS; ++i) {
        PS5_ClosePad(&pad_ctx[i]);
        pad_ctx[i].instance_id = 0;
        pad_ctx[i].user_id = -1;
    }
}

static bool PS5_JoystickGetGamepadMapping(int device_index, SDL_GamepadMapping *out)
{
#define BUTTON(field, index) out->field.kind = EMappingKind_Button; out->field.target = index
#define AXIS(field, index) out->field.kind = EMappingKind_Axis; out->field.target = index
    BUTTON(a, 0);
    BUTTON(b, 1);
    BUTTON(x, 2);
    BUTTON(y, 3);
    BUTTON(touchpad, 4);
    BUTTON(start, 6);
    BUTTON(leftstick, 7);
    BUTTON(rightstick, 8);
    BUTTON(leftshoulder, 9);
    BUTTON(rightshoulder, 10);
    BUTTON(dpup, 11);
    BUTTON(dpdown, 12);
    BUTTON(dpleft, 13);
    BUTTON(dpright, 14);
    AXIS(leftx, PS5_PAD_AXIS_LX);
    AXIS(lefty, PS5_PAD_AXIS_LY);
    AXIS(rightx, PS5_PAD_AXIS_RX);
    AXIS(righty, PS5_PAD_AXIS_RY);
    AXIS(lefttrigger, PS5_PAD_AXIS_L2);
    AXIS(righttrigger, PS5_PAD_AXIS_R2);
#undef BUTTON
#undef AXIS
    return true;
}

SDL_JoystickDriver SDL_PS5_JoystickDriver = {
    PS5_JoystickInit,
    PS5_JoystickGetCount,
    PS5_JoystickDetect,
    PS5_JoystickIsDevicePresent,
    PS5_JoystickGetDeviceName,
    PS5_JoystickGetDevicePath,
    PS5_JoystickGetDeviceSteamVirtualGamepadSlot,
    PS5_JoystickGetDevicePlayerIndex,
    PS5_JoystickSetDevicePlayerIndex,
    PS5_JoystickGetDeviceGUID,
    PS5_JoystickGetDeviceInstanceID,
    PS5_JoystickOpen,
    PS5_JoystickRumble,
    PS5_JoystickRumbleTriggers,
    PS5_JoystickSetLED,
    PS5_JoystickSendEffect,
    PS5_JoystickSetSensorsEnabled,
    PS5_JoystickUpdate,
    PS5_JoystickClose,
    PS5_JoystickQuit,
    PS5_JoystickGetGamepadMapping,
};

#endif /* SDL_JOYSTICK_PS5 */
