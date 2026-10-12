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
/* PS5 video for SDL3: one display, one fullscreen window, Vulkan only. Outsider PS5.
 * Input comes from the joystick driver (scePad); there is no keyboard, mouse or touch. A window becomes a
 * Vulkan window when SDL_GPU (or the application) creates its surface. */
#include "SDL_internal.h"

#ifdef SDL_VIDEO_DRIVER_PS5

#include "../../events/SDL_events_c.h"
#include "../../events/SDL_keyboard_c.h"
#include "SDL_ps5video.h"

int sceSystemServiceHideSplashScreen(void);

static bool PS5_VideoInit(SDL_VideoDevice *_this)
{
    SDL_DisplayMode mode;
    SDL_zero(mode);
    mode.format = SDL_PIXELFORMAT_XRGB8888;
    mode.w = PS5_DISPLAY_WIDTH;
    mode.h = PS5_DISPLAY_HEIGHT;
    mode.refresh_rate_numerator = 60000;
    mode.refresh_rate_denominator = 1001;
    if (SDL_AddBasicVideoDisplay(&mode) == 0) {
        return false;
    }
    /* the shell's start-up splash covers the screen until the title takes it away */
    sceSystemServiceHideSplashScreen();
    return true;
}

static void PS5_VideoQuit(SDL_VideoDevice *_this)
{
}

static bool PS5_CreateWindow(SDL_VideoDevice *_this, SDL_Window *window, SDL_PropertiesID props)
{
    if (_this->windows != window || window->next) {
        return SDL_SetError("ps5: only one window is supported");
    }
    /* the whole screen, always */
    window->flags |= SDL_WINDOW_FULLSCREEN | SDL_WINDOW_BORDERLESS | SDL_WINDOW_INPUT_FOCUS;
    window->flags &= ~SDL_WINDOW_RESIZABLE;
    window->x = window->floating.x = window->windowed.x = 0;
    window->y = window->floating.y = window->windowed.y = 0;
    window->w = window->floating.w = window->windowed.w = PS5_DISPLAY_WIDTH;
    window->h = window->floating.h = window->windowed.h = PS5_DISPLAY_HEIGHT;
    SDL_SetKeyboardFocus(window);
    return true;
}

static void PS5_SetWindowSize(SDL_VideoDevice *_this, SDL_Window *window)
{
    /* the window stays the size of the screen */
    SDL_SendWindowEvent(window, SDL_EVENT_WINDOW_RESIZED, PS5_DISPLAY_WIDTH, PS5_DISPLAY_HEIGHT);
}

static SDL_FullscreenResult PS5_SetWindowFullscreen(SDL_VideoDevice *_this, SDL_Window *window,
                                                    SDL_VideoDisplay *display, SDL_FullscreenOp fullscreen)
{
    return SDL_FULLSCREEN_SUCCEEDED;
}

static void PS5_DestroyWindow(SDL_VideoDevice *_this, SDL_Window *window)
{
}

static void PS5_PumpEvents(SDL_VideoDevice *_this)
{
    /* the pads are polled by the joystick subsystem */
}

static void PS5_DeleteDevice(SDL_VideoDevice *device)
{
    SDL_free(device);
}

static SDL_VideoDevice *PS5_CreateDevice(void)
{
    SDL_VideoDevice *device = (SDL_VideoDevice *)SDL_calloc(1, sizeof(SDL_VideoDevice));
    if (!device) {
        return NULL;
    }
    device->VideoInit = PS5_VideoInit;
    device->VideoQuit = PS5_VideoQuit;
    device->CreateSDLWindow = PS5_CreateWindow;
    device->SetWindowSize = PS5_SetWindowSize;
    device->SetWindowFullscreen = PS5_SetWindowFullscreen;
    device->DestroyWindow = PS5_DestroyWindow;
    device->PumpEvents = PS5_PumpEvents;
    device->Vulkan_LoadLibrary = PS5_Vulkan_LoadLibrary;
    device->Vulkan_UnloadLibrary = PS5_Vulkan_UnloadLibrary;
    device->Vulkan_GetInstanceExtensions = PS5_Vulkan_GetInstanceExtensions;
    device->Vulkan_CreateSurface = PS5_Vulkan_CreateSurface;
    device->Vulkan_DestroySurface = PS5_Vulkan_DestroySurface;
    device->free = PS5_DeleteDevice;
    device->device_caps = VIDEO_DEVICE_CAPS_FULLSCREEN_ONLY;
    return device;
}

VideoBootStrap PS5_bootstrap = {
    "ps5", "PS5 Vulkan video driver (RADV on VideoOut)",
    PS5_CreateDevice,
    NULL, /* no ShowMessageBox implementation */
    false
};

#endif /* SDL_VIDEO_DRIVER_PS5 */
