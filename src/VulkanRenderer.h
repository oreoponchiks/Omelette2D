#pragma once

#include "Sprite.h"
#include <stdbool.h>
#include <stddef.h>
#include <windows.h>

typedef struct VulkanRenderer VulkanRenderer;

VulkanRenderer* renderer_create(HWND window, char* error, size_t error_size);
void renderer_destroy(VulkanRenderer* renderer);
void renderer_resized(VulkanRenderer* renderer);
bool renderer_draw(VulkanRenderer* renderer, const Sprite* sprites, size_t count);
const char* renderer_error(const VulkanRenderer* renderer);
