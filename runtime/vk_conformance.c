/* SPDX-License-Identifier: GPL-3.0-or-later
 * Copyright (c) 2026 the PS5 port contributors */
/* SDL3's GPU API turns down a Vulkan driver whose conformance version is below 1.0 (VULKAN_INTERNAL_GetDeviceRank()
 * in SDL_gpu_vulkan.c), and RADV on the PS5 reports 0.0.0.0 until a CTS release has passed there (PS5_Mesa's
 * radv_physical_device.c; PS5_Vulkan's CTS_GAPS.md). SDL then finds "No supported SDL_GPU backend".
 *
 * vulkan/title-link.sh wraps RADV's entry point, through which SDL3 looks up every Vulkan function
 * (vulkan/sdl3-ps5's SDL_ps5vulkan.c), and this hands out vkGetPhysicalDeviceProperties2 with that one field
 * filled in: for RADV only, and only while it says 0. Nothing else about the device changes. */
#include <stdint.h>
#include <string.h>

typedef void (*vk_function)(void);

/* The parts of the Vulkan structures read here (vulkan_core.h) */
#define VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_DRIVER_PROPERTIES 1000196000
#define VK_DRIVER_ID_MESA_RADV 3
#define VK_MAX_DRIVER_NAME_SIZE 256
#define VK_MAX_DRIVER_INFO_SIZE 256

struct vk_chain {
	int32_t sType;
	struct vk_chain *pNext;
};

struct vk_driver_properties {
	int32_t sType;
	void *pNext;
	int32_t driverID;
	char driverName[VK_MAX_DRIVER_NAME_SIZE];
	char driverInfo[VK_MAX_DRIVER_INFO_SIZE];
	struct {
		uint8_t major, minor, subminor, patch;
	} conformanceVersion;
};

vk_function __real_vk_icdGetInstanceProcAddr(void *instance, const char *name);
vk_function __wrap_vk_icdGetInstanceProcAddr(void *instance, const char *name);

typedef void (*properties2_fn)(void *physical_device, void *properties);
static properties2_fn real_properties2;

static void properties2(void *physical_device, void *properties)
{
	real_properties2(physical_device, properties);
	for (struct vk_chain *s = ((struct vk_chain *)properties)->pNext; s; s = s->pNext) {
		if (s->sType != VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_DRIVER_PROPERTIES)
			continue;
		struct vk_driver_properties *driver = (struct vk_driver_properties *)s;
		if (driver->driverID == VK_DRIVER_ID_MESA_RADV && driver->conformanceVersion.major == 0) {
			driver->conformanceVersion.major = 1;
			driver->conformanceVersion.minor = 4;
		}
	}
}

vk_function __wrap_vk_icdGetInstanceProcAddr(void *instance, const char *name)
{
	vk_function f = __real_vk_icdGetInstanceProcAddr(instance, name);
	if (!f || !name)
		return f;
	if (!strcmp(name, "vkGetInstanceProcAddr"))
		return (vk_function)__wrap_vk_icdGetInstanceProcAddr;
	if (!strcmp(name, "vkGetPhysicalDeviceProperties2") || !strcmp(name, "vkGetPhysicalDeviceProperties2KHR")) {
		real_properties2 = (properties2_fn)f;
		return (vk_function)properties2;
	}
	return f;
}
