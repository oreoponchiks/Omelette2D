#pragma once

#include <vulkan/vulkan.h>
#include <windows.h>

#define CIMGUI_DEFINE_ENUMS_AND_STRUCTS
#define CIMGUI_USE_VULKAN
#include "cimgui.h"
#include "cimgui_impl.h"
#include "cimgui_impl_win32.h"

typedef struct UiVulkanInfo {
    HWND window;
    VkInstance instance;
    VkPhysicalDevice physical_device;
    VkDevice device;
    uint32_t queue_family;
    VkQueue queue;
    uint32_t image_count;
    VkFormat format;
} UiVulkanInfo;

bool ui_init(const UiVulkanInfo* info);
bool ui_reconfigure(uint32_t image_count, VkFormat format);
void ui_shutdown(void);
void ui_begin_frame(void);
