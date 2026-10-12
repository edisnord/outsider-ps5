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
/* Vulkan for the PS5 video driver: RADV is linked into the program (the console cannot load a
 * driver at run time); its entry point is the ICD's, vk_icdGetInstanceProcAddr. Outsider PS5. */
#include "SDL_internal.h"

#if defined(SDL_VIDEO_DRIVER_PS5) && defined(SDL_VIDEO_VULKAN)

#include "../SDL_vulkan_internal.h"
#include "SDL_ps5video.h"

/* RADV's own, from libvulkan_radeon.ps5.a */
extern VKAPI_ATTR PFN_vkVoidFunction VKAPI_CALL vk_icdGetInstanceProcAddr(VkInstance instance, const char *name);

bool PS5_Vulkan_LoadLibrary(SDL_VideoDevice *_this, const char *path)
{
    VkExtensionProperties *extensions;
    Uint32 count = 0;
    bool has_surface = false, has_display = false;

    _this->vulkan_config.vkGetInstanceProcAddr = (void *)vk_icdGetInstanceProcAddr;
    _this->vulkan_config.vkEnumerateInstanceExtensionProperties =
        (void *)vk_icdGetInstanceProcAddr(VK_NULL_HANDLE, "vkEnumerateInstanceExtensionProperties");
    if (!_this->vulkan_config.vkEnumerateInstanceExtensionProperties) {
        return SDL_SetError("ps5: the Vulkan driver has no vkEnumerateInstanceExtensionProperties");
    }
    extensions = SDL_Vulkan_CreateInstanceExtensionsList(
        (PFN_vkEnumerateInstanceExtensionProperties)_this->vulkan_config.vkEnumerateInstanceExtensionProperties,
        &count);
    if (!extensions) {
        return false;
    }
    for (Uint32 i = 0; i < count; ++i) {
        has_surface |= SDL_strcmp(VK_KHR_SURFACE_EXTENSION_NAME, extensions[i].extensionName) == 0;
        has_display |= SDL_strcmp(VK_KHR_DISPLAY_EXTENSION_NAME, extensions[i].extensionName) == 0;
    }
    SDL_free(extensions);
    if (!has_surface || !has_display) {
        return SDL_SetError("ps5: the Vulkan driver lacks " VK_KHR_SURFACE_EXTENSION_NAME " or " VK_KHR_DISPLAY_EXTENSION_NAME);
    }
    SDL_strlcpy(_this->vulkan_config.loader_path, "RADV (linked)", SDL_arraysize(_this->vulkan_config.loader_path));
    return true;
}

void PS5_Vulkan_UnloadLibrary(SDL_VideoDevice *_this)
{
}

char const * const *PS5_Vulkan_GetInstanceExtensions(SDL_VideoDevice *_this, Uint32 *count)
{
    static const char *const extensions[] = { VK_KHR_SURFACE_EXTENSION_NAME, VK_KHR_DISPLAY_EXTENSION_NAME };
    if (count) {
        *count = SDL_arraysize(extensions);
    }
    return extensions;
}

bool PS5_Vulkan_CreateSurface(SDL_VideoDevice *_this, SDL_Window *window, VkInstance instance,
                              const struct VkAllocationCallbacks *allocator, VkSurfaceKHR *surface)
{
    /* the display's largest mode at its highest refresh rate; 119.88 Hz is offered only to a title
     * whose param.json declares high-frame-rate output */
    return SDL_Vulkan_Display_CreateSurface(_this->vulkan_config.vkGetInstanceProcAddr, instance, allocator, surface);
}

void PS5_Vulkan_DestroySurface(SDL_VideoDevice *_this, VkInstance instance, VkSurfaceKHR surface,
                               const struct VkAllocationCallbacks *allocator)
{
    SDL_Vulkan_DestroySurface_Internal(_this->vulkan_config.vkGetInstanceProcAddr, instance, surface, allocator);
}

#endif
