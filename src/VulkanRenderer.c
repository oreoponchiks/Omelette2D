#include "VulkanRenderer.h"
#include "Ui.h"

#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <wchar.h>

enum { MaxSprites = 100000, FramesInFlight = 2 };

typedef struct QueueFamilies {
    uint32_t graphics, present;
} QueueFamilies;

typedef struct SwapchainSupport {
    VkSurfaceCapabilitiesKHR capabilities;
    VkSurfaceFormatKHR* formats;
    VkPresentModeKHR* presentModes;
    uint32_t format_count, present_mode_count;
} SwapchainSupport;

struct VulkanRenderer {
    HWND window_;
    VkInstance instance_;
    VkSurfaceKHR surface_;
    VkPhysicalDevice physicalDevice_;
    VkDevice device_;
    VkQueue graphicsQueue_, presentQueue_;
    VkSwapchainKHR swapchain_;
    VkFormat swapchainFormat_;
    VkExtent2D swapchainExtent_;
    VkImage* swapchainImages_;
    VkImageView* imageViews_;
    uint32_t image_count;
    VkPipelineLayout pipelineLayout_;
    VkPipeline pipeline_;
    VkShaderModule vertex_shader, fragment_shader;
    VkCommandPool commandPool_;
    VkCommandBuffer commandBuffers_[FramesInFlight];
    VkBuffer vertexBuffer_;
    VkDeviceMemory vertexMemory_;
    void* mappedVertices_;
    VkSemaphore imageAvailable_[FramesInFlight];
    VkSemaphore* renderFinished_;
    VkFence inFlight_[FramesInFlight];
    uint32_t frame_;
    bool framebufferResized_;
    bool ui_initialized;
    QueueFamilies families;
    SwapchainSupport support;
    char error[256];
};

static bool recreateSwapchain(VulkanRenderer* r);
static void destroySwapchain(VulkanRenderer* r);

static bool fail(VulkanRenderer* r, const char* message) {
    snprintf(r->error, sizeof(r->error), "%s", message);
    return false;
}

static bool check_result(VulkanRenderer* r, VkResult result, const char* message) {
    if (result == VK_SUCCESS)
        return true;
    snprintf(r->error, sizeof(r->error), "%s (VkResult %d)", message, (int)result);
    return false;
}

#define CHECK(call, message)                                                                       \
    do {                                                                                           \
        if (!check_result(r, (call), (message)))                                                   \
            return false;                                                                          \
    } while (0)
#define CHECK_CALL(call)                                                                           \
    do {                                                                                           \
        if (!(call))                                                                               \
            return false;                                                                          \
    } while (0)

static uint32_t clamp_u32(uint32_t value, uint32_t minimum, uint32_t maximum) {
    return value < minimum ? minimum : (value > maximum ? maximum : value);
}

static bool findQueueFamilies(VulkanRenderer* r, VkPhysicalDevice device, QueueFamilies* result) {
    uint32_t count = 0;
    *result = (QueueFamilies){UINT32_MAX, UINT32_MAX};
    vkGetPhysicalDeviceQueueFamilyProperties(device, &count, NULL);
    VkQueueFamilyProperties* properties = calloc(count, sizeof(*properties));
    if (!properties)
        return fail(r, "Could not allocate queue family properties");
    vkGetPhysicalDeviceQueueFamilyProperties(device, &count, properties);
    for (uint32_t i = 0; i < count; ++i) {
        if (properties[i].queueFlags & VK_QUEUE_GRAPHICS_BIT)
            result->graphics = i;
        VkBool32 supported = VK_FALSE;
        VkResult status = vkGetPhysicalDeviceSurfaceSupportKHR(device, i, r->surface_, &supported);
        if (status != VK_SUCCESS) {
            free(properties);
            return check_result(r, status, "Failed to query presentation support");
        }
        if (supported)
            result->present = i;
        if (result->graphics != UINT32_MAX && result->present != UINT32_MAX)
            break;
    }
    free(properties);
    return true;
}

static bool querySwapchain(VulkanRenderer* r, VkPhysicalDevice device) {
    SwapchainSupport* support = &r->support;
    free(support->formats);
    free(support->presentModes);
    memset(support, 0, sizeof(*support));
    CHECK(vkGetPhysicalDeviceSurfaceCapabilitiesKHR(device, r->surface_, &support->capabilities),
          "Failed to query surface capabilities");
    CHECK(vkGetPhysicalDeviceSurfaceFormatsKHR(device, r->surface_, &support->format_count, NULL),
          "Failed to query surface formats");
    if (support->format_count) {
        support->formats = calloc(support->format_count, sizeof(*support->formats));
        if (!support->formats)
            return fail(r, "Could not allocate surface formats");
        CHECK(vkGetPhysicalDeviceSurfaceFormatsKHR(device, r->surface_, &support->format_count,
                                                   support->formats),
              "Failed to read surface formats");
    }
    CHECK(vkGetPhysicalDeviceSurfacePresentModesKHR(device, r->surface_,
                                                    &support->present_mode_count, NULL),
          "Failed to query presentation modes");
    if (support->present_mode_count) {
        support->presentModes = calloc(support->present_mode_count, sizeof(*support->presentModes));
        if (!support->presentModes)
            return fail(r, "Could not allocate presentation modes");
        CHECK(vkGetPhysicalDeviceSurfacePresentModesKHR(
                  device, r->surface_, &support->present_mode_count, support->presentModes),
              "Failed to read presentation modes");
    }
    return true;
}

static bool hasSwapchainExtension(VulkanRenderer* r, VkPhysicalDevice device, bool* supported) {
    uint32_t count = 0;
    *supported = false;
    CHECK(vkEnumerateDeviceExtensionProperties(device, NULL, &count, NULL),
          "Failed to query device extensions");
    if (!count)
        return true;
    VkExtensionProperties* extensions = calloc(count, sizeof(*extensions));
    if (!extensions)
        return fail(r, "Could not allocate device extensions");
    VkResult status = vkEnumerateDeviceExtensionProperties(device, NULL, &count, extensions);
    if (status == VK_SUCCESS)
        for (uint32_t i = 0; i < count; ++i)
            if (strcmp(extensions[i].extensionName, VK_KHR_SWAPCHAIN_EXTENSION_NAME) == 0)
                *supported = true;
    free(extensions);
    return check_result(r, status, "Failed to read device extensions");
}

static bool selectPhysicalDevice(VulkanRenderer* r) {
    uint32_t count = 0;
    CHECK(vkEnumeratePhysicalDevices(r->instance_, &count, NULL), "Failed to query Vulkan GPUs");
    if (!count)
        return fail(r, "No Vulkan-capable GPU found");
    VkPhysicalDevice* devices = calloc(count, sizeof(*devices));
    if (!devices)
        return fail(r, "Could not allocate GPU list");
    bool success = check_result(r, vkEnumeratePhysicalDevices(r->instance_, &count, devices),
                                "Failed to read GPU list");
    for (uint32_t i = 0; success && i < count; ++i) {
        VkPhysicalDeviceProperties properties;
        vkGetPhysicalDeviceProperties(devices[i], &properties);
        if (properties.apiVersion < VK_API_VERSION_1_3)
            continue;
        VkPhysicalDeviceDynamicRenderingFeatures dynamic = {
            VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_DYNAMIC_RENDERING_FEATURES};
        VkPhysicalDeviceFeatures2 features = {VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_FEATURES_2};
        features.pNext = &dynamic;
        vkGetPhysicalDeviceFeatures2(devices[i], &features);
        if (!dynamic.dynamicRendering)
            continue;
        QueueFamilies families;
        bool has_swapchain;
        success = findQueueFamilies(r, devices[i], &families) &&
                  hasSwapchainExtension(r, devices[i], &has_swapchain);
        if (!success)
            break;
        if (families.graphics == UINT32_MAX || families.present == UINT32_MAX || !has_swapchain)
            continue;
        success = querySwapchain(r, devices[i]);
        if (success && r->support.format_count && r->support.present_mode_count) {
            r->physicalDevice_ = devices[i];
            r->families = families;
            break;
        }
    }
    free(devices);
    if (!success)
        return false;
    return r->physicalDevice_ ? true
                              : fail(r, "No GPU supporting Vulkan 1.3 dynamic rendering found");
}

static bool createDevice(VulkanRenderer* r) {
    uint32_t families[2] = {r->families.graphics, r->families.present};
    uint32_t count = families[0] == families[1] ? 1 : 2;
    float priority = 1.0f;
    VkDeviceQueueCreateInfo queues[2] = {0};
    for (uint32_t i = 0; i < count; ++i) {
        queues[i].sType = VK_STRUCTURE_TYPE_DEVICE_QUEUE_CREATE_INFO;
        queues[i].queueFamilyIndex = families[i];
        queues[i].queueCount = 1;
        queues[i].pQueuePriorities = &priority;
    }
    const char* extensions[] = {VK_KHR_SWAPCHAIN_EXTENSION_NAME};
    VkPhysicalDeviceDynamicRenderingFeatures dynamic = {
        VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_DYNAMIC_RENDERING_FEATURES};
    dynamic.dynamicRendering = VK_TRUE;
    VkDeviceCreateInfo info = {VK_STRUCTURE_TYPE_DEVICE_CREATE_INFO};
    info.pNext = &dynamic;
    info.queueCreateInfoCount = count;
    info.pQueueCreateInfos = queues;
    info.enabledExtensionCount = 1;
    info.ppEnabledExtensionNames = extensions;
    CHECK(vkCreateDevice(r->physicalDevice_, &info, NULL, &r->device_),
          "Failed to create Vulkan device");
    vkGetDeviceQueue(r->device_, families[0], 0, &r->graphicsQueue_);
    vkGetDeviceQueue(r->device_, families[1], 0, &r->presentQueue_);
    return true;
}

static bool findMemoryType(VulkanRenderer* r, uint32_t bits, VkMemoryPropertyFlags properties,
                           uint32_t* index) {
    VkPhysicalDeviceMemoryProperties memory;
    vkGetPhysicalDeviceMemoryProperties(r->physicalDevice_, &memory);
    for (uint32_t i = 0; i < memory.memoryTypeCount; ++i)
        if ((bits & (1u << i)) &&
            (memory.memoryTypes[i].propertyFlags & properties) == properties) {
            *index = i;
            return true;
        }
    return fail(r, "No suitable GPU memory type found");
}

static bool loadShader(VulkanRenderer* r, const wchar_t* relative_path, VkShaderModule* module) {
    wchar_t path[32768];
    DWORD length = GetModuleFileNameW(NULL, path, (DWORD)(sizeof(path) / sizeof(path[0])));
    if (!length || length >= sizeof(path) / sizeof(path[0]))
        return fail(r, "Could not locate executable");
    wchar_t* slash = wcsrchr(path, L'\\');
    if (!slash)
        return fail(r, "Invalid executable path");
    size_t remaining = sizeof(path) / sizeof(path[0]) - (size_t)(slash + 1 - path);
    if (wcscpy_s(slash + 1, remaining, relative_path))
        return fail(r, "Shader path is too long");
    FILE* file = NULL;
    if (_wfopen_s(&file, path, L"rb"))
        return fail(r, "Could not open compiled shader beside the executable");
    if (fseek(file, 0, SEEK_END)) {
        fclose(file);
        return fail(r, "Could not seek shader file");
    }
    long length_bytes = ftell(file);
    if (length_bytes <= 0 || length_bytes % 4 != 0 || fseek(file, 0, SEEK_SET)) {
        fclose(file);
        return fail(r, "Invalid compiled shader size");
    }
    uint32_t* bytes = malloc((size_t)length_bytes);
    if (!bytes) {
        fclose(file);
        return fail(r, "Could not allocate shader data");
    }
    bool read_ok = fread(bytes, 1, (size_t)length_bytes, file) == (size_t)length_bytes;
    fclose(file);
    if (!read_ok) {
        free(bytes);
        return fail(r, "Could not read compiled shader");
    }
    VkShaderModuleCreateInfo info = {VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO};
    info.codeSize = (size_t)length_bytes;
    info.pCode = bytes;
    VkResult result = vkCreateShaderModule(r->device_, &info, NULL, module);
    free(bytes);
    return check_result(r, result, "Failed to create shader module");
}
static bool createInstance(VulkanRenderer* r) {
    VkApplicationInfo app = {VK_STRUCTURE_TYPE_APPLICATION_INFO};
    app.pApplicationName = "Omelette2D";
    app.applicationVersion = VK_MAKE_VERSION(0, 1, 0);
    app.pEngineName = "Omelette2D";
    app.engineVersion = VK_MAKE_VERSION(0, 1, 0);
    app.apiVersion = VK_API_VERSION_1_3;

    const char* extensions[] = {VK_KHR_SURFACE_EXTENSION_NAME, VK_KHR_WIN32_SURFACE_EXTENSION_NAME};
    VkInstanceCreateInfo info = {VK_STRUCTURE_TYPE_INSTANCE_CREATE_INFO};
    info.pApplicationInfo = &app;
    info.enabledExtensionCount = (uint32_t)(sizeof(extensions) / sizeof(extensions[0]));
    info.ppEnabledExtensionNames = extensions;
    CHECK(vkCreateInstance(&info, NULL, &r->instance_), "Failed to create Vulkan instance");
    return true;
}

static bool createSurface(VulkanRenderer* r) {
    VkWin32SurfaceCreateInfoKHR info = {VK_STRUCTURE_TYPE_WIN32_SURFACE_CREATE_INFO_KHR};
    info.hinstance = GetModuleHandleW(NULL);
    info.hwnd = r->window_;
    CHECK(vkCreateWin32SurfaceKHR(r->instance_, &info, NULL, &r->surface_),
          "Failed to create window surface");
    return true;
}

static bool createSwapchain(VulkanRenderer* r) {
    CHECK_CALL(querySwapchain(r, r->physicalDevice_));
    SwapchainSupport support = r->support;
    if (!support.format_count || !support.present_mode_count)
        return fail(r, "Surface no longer supports presentation");
    VkSurfaceFormatKHR format = support.formats[0];
    for (uint32_t i = 0; i < support.format_count; ++i) {
        VkSurfaceFormatKHR candidate = support.formats[i];
        if (candidate.format == VK_FORMAT_B8G8R8A8_SRGB &&
            candidate.colorSpace == VK_COLOR_SPACE_SRGB_NONLINEAR_KHR) {
            format = candidate;
            break;
        }
    }
    VkPresentModeKHR presentMode = VK_PRESENT_MODE_FIFO_KHR;
    for (uint32_t i = 0; i < support.present_mode_count; ++i)
        if (support.presentModes[i] == VK_PRESENT_MODE_MAILBOX_KHR)
            presentMode = support.presentModes[i];

    RECT client = {0};
    GetClientRect(r->window_, &client);
    VkExtent2D extent = support.capabilities.currentExtent;
    if (extent.width == UINT32_MAX) {
        extent.width =
            clamp_u32((uint32_t)(client.right), support.capabilities.minImageExtent.width,
                      support.capabilities.maxImageExtent.width);
        extent.height =
            clamp_u32((uint32_t)(client.bottom), support.capabilities.minImageExtent.height,
                      support.capabilities.maxImageExtent.height);
    }
    uint32_t imageCount = support.capabilities.minImageCount + 1;
    if (support.capabilities.maxImageCount && imageCount > support.capabilities.maxImageCount)
        imageCount = support.capabilities.maxImageCount;

    QueueFamilies families = r->families;
    uint32_t familyIndices[] = {families.graphics, families.present};
    VkSwapchainCreateInfoKHR info = {VK_STRUCTURE_TYPE_SWAPCHAIN_CREATE_INFO_KHR};
    info.surface = r->surface_;
    info.minImageCount = imageCount;
    info.imageFormat = format.format;
    info.imageColorSpace = format.colorSpace;
    info.imageExtent = extent;
    info.imageArrayLayers = 1;
    info.imageUsage = VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT;
    if (families.graphics != families.present) {
        info.imageSharingMode = VK_SHARING_MODE_CONCURRENT;
        info.queueFamilyIndexCount = 2;
        info.pQueueFamilyIndices = familyIndices;
    } else
        info.imageSharingMode = VK_SHARING_MODE_EXCLUSIVE;
    info.preTransform = support.capabilities.currentTransform;
    info.compositeAlpha = VK_COMPOSITE_ALPHA_OPAQUE_BIT_KHR;
    info.presentMode = presentMode;
    info.clipped = VK_TRUE;
    CHECK(vkCreateSwapchainKHR(r->device_, &info, NULL, &r->swapchain_),
          "Failed to create swapchain");
    CHECK(vkGetSwapchainImagesKHR(r->device_, r->swapchain_, &imageCount, NULL),
          "Failed to query swapchain images");
    r->image_count = imageCount;
    r->swapchainImages_ = calloc(imageCount, sizeof(*r->swapchainImages_));
    if (!r->swapchainImages_)
        return fail(r, "Out of memory allocating swapchain images");
    CHECK(vkGetSwapchainImagesKHR(r->device_, r->swapchain_, &imageCount, r->swapchainImages_),
          "Failed to read swapchain images");
    r->image_count = imageCount;
    /* Presentation completion belongs to a swapchain image, not a CPU frame. */
    r->renderFinished_ = calloc(imageCount, sizeof(*r->renderFinished_));
    if (!r->renderFinished_)
        return fail(r, "Could not allocate presentation semaphores");
    VkSemaphoreCreateInfo semaphore = {VK_STRUCTURE_TYPE_SEMAPHORE_CREATE_INFO};
    for (uint32_t i = 0; i < imageCount; ++i)
        CHECK(vkCreateSemaphore(r->device_, &semaphore, NULL, &r->renderFinished_[i]),
              "Failed to create presentation semaphore");
    r->swapchainFormat_ = format.format;
    r->swapchainExtent_ = extent;
    return true;
}

static bool createImageViews(VulkanRenderer* r) {
    r->imageViews_ = calloc(r->image_count, sizeof(*r->imageViews_));
    if (!r->imageViews_)
        return fail(r, "Out of memory allocating image views");
    for (size_t i = 0; i < r->image_count; ++i) {
        VkImageViewCreateInfo info = {VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO};
        info.image = r->swapchainImages_[i];
        info.viewType = VK_IMAGE_VIEW_TYPE_2D;
        info.format = r->swapchainFormat_;
        info.subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
        info.subresourceRange.levelCount = 1;
        info.subresourceRange.layerCount = 1;
        CHECK(vkCreateImageView(r->device_, &info, NULL, &r->imageViews_[i]),
              "Failed to create image view");
    }
    return true;
}

static bool createPipeline(VulkanRenderer* r) {
    CHECK_CALL(loadShader(r, L"shaders/sprite.vert.spv", &r->vertex_shader));
    VkShaderModule vertex = r->vertex_shader;
    CHECK_CALL(loadShader(r, L"shaders/sprite.frag.spv", &r->fragment_shader));
    VkShaderModule fragment = r->fragment_shader;
    VkPipelineShaderStageCreateInfo stages[2] = {0};
    stages[0] =
        (VkPipelineShaderStageCreateInfo){VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO};
    stages[0].stage = VK_SHADER_STAGE_VERTEX_BIT;
    stages[0].module = vertex;
    stages[0].pName = "main";
    stages[1] =
        (VkPipelineShaderStageCreateInfo){VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO};
    stages[1].stage = VK_SHADER_STAGE_FRAGMENT_BIT;
    stages[1].module = fragment;
    stages[1].pName = "main";

    VkVertexInputBindingDescription binding = {0, sizeof(Sprite), VK_VERTEX_INPUT_RATE_INSTANCE};
    VkVertexInputAttributeDescription attributes[2] = {
        {0, 0, VK_FORMAT_R32G32B32A32_SFLOAT, offsetof(Sprite, x)},
        {1, 0, VK_FORMAT_R32G32B32A32_SFLOAT, offsetof(Sprite, color)}};
    VkPipelineVertexInputStateCreateInfo vertexInput = {
        VK_STRUCTURE_TYPE_PIPELINE_VERTEX_INPUT_STATE_CREATE_INFO};
    vertexInput.vertexBindingDescriptionCount = 1;
    vertexInput.pVertexBindingDescriptions = &binding;
    vertexInput.vertexAttributeDescriptionCount = 2;
    vertexInput.pVertexAttributeDescriptions = attributes;
    VkPipelineInputAssemblyStateCreateInfo assembly = {
        VK_STRUCTURE_TYPE_PIPELINE_INPUT_ASSEMBLY_STATE_CREATE_INFO};
    assembly.topology = VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST;
    VkPipelineViewportStateCreateInfo viewport = {
        VK_STRUCTURE_TYPE_PIPELINE_VIEWPORT_STATE_CREATE_INFO};
    viewport.viewportCount = 1;
    viewport.scissorCount = 1;
    VkPipelineRasterizationStateCreateInfo raster = {
        VK_STRUCTURE_TYPE_PIPELINE_RASTERIZATION_STATE_CREATE_INFO};
    raster.polygonMode = VK_POLYGON_MODE_FILL;
    raster.cullMode = VK_CULL_MODE_NONE;
    raster.frontFace = VK_FRONT_FACE_CLOCKWISE;
    raster.lineWidth = 1.0f;
    VkPipelineMultisampleStateCreateInfo multisample = {
        VK_STRUCTURE_TYPE_PIPELINE_MULTISAMPLE_STATE_CREATE_INFO};
    multisample.rasterizationSamples = VK_SAMPLE_COUNT_1_BIT;
    VkPipelineColorBlendAttachmentState attachment = {0};
    attachment.blendEnable = VK_TRUE;
    attachment.srcColorBlendFactor = VK_BLEND_FACTOR_SRC_ALPHA;
    attachment.dstColorBlendFactor = VK_BLEND_FACTOR_ONE_MINUS_SRC_ALPHA;
    attachment.colorBlendOp = VK_BLEND_OP_ADD;
    attachment.srcAlphaBlendFactor = VK_BLEND_FACTOR_ONE;
    attachment.dstAlphaBlendFactor = VK_BLEND_FACTOR_ONE_MINUS_SRC_ALPHA;
    attachment.alphaBlendOp = VK_BLEND_OP_ADD;
    attachment.colorWriteMask = VK_COLOR_COMPONENT_R_BIT | VK_COLOR_COMPONENT_G_BIT |
                                VK_COLOR_COMPONENT_B_BIT | VK_COLOR_COMPONENT_A_BIT;
    VkPipelineColorBlendStateCreateInfo blend = {
        VK_STRUCTURE_TYPE_PIPELINE_COLOR_BLEND_STATE_CREATE_INFO};
    blend.attachmentCount = 1;
    blend.pAttachments = &attachment;
    VkDynamicState dynamicStates[] = {VK_DYNAMIC_STATE_VIEWPORT, VK_DYNAMIC_STATE_SCISSOR};
    VkPipelineDynamicStateCreateInfo dynamic = {
        VK_STRUCTURE_TYPE_PIPELINE_DYNAMIC_STATE_CREATE_INFO};
    dynamic.dynamicStateCount = 2;
    dynamic.pDynamicStates = dynamicStates;
    VkPushConstantRange push = {VK_SHADER_STAGE_VERTEX_BIT, 0, sizeof(float) * 2};
    VkPipelineLayoutCreateInfo layout = {VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO};
    layout.pushConstantRangeCount = 1;
    layout.pPushConstantRanges = &push;
    CHECK(vkCreatePipelineLayout(r->device_, &layout, NULL, &r->pipelineLayout_),
          "Failed to create pipeline layout");
    VkPipelineRenderingCreateInfo rendering = {VK_STRUCTURE_TYPE_PIPELINE_RENDERING_CREATE_INFO};
    rendering.colorAttachmentCount = 1;
    rendering.pColorAttachmentFormats = &r->swapchainFormat_;
    VkGraphicsPipelineCreateInfo info = {VK_STRUCTURE_TYPE_GRAPHICS_PIPELINE_CREATE_INFO};
    info.pNext = &rendering;
    info.stageCount = 2;
    info.pStages = stages;
    info.pVertexInputState = &vertexInput;
    info.pInputAssemblyState = &assembly;
    info.pViewportState = &viewport;
    info.pRasterizationState = &raster;
    info.pMultisampleState = &multisample;
    info.pColorBlendState = &blend;
    info.pDynamicState = &dynamic;
    info.layout = r->pipelineLayout_;
    VkResult result =
        vkCreateGraphicsPipelines(r->device_, VK_NULL_HANDLE, 1, &info, NULL, &r->pipeline_);
    vkDestroyShaderModule(r->device_, fragment, NULL);
    vkDestroyShaderModule(r->device_, vertex, NULL);
    r->vertex_shader = r->fragment_shader = VK_NULL_HANDLE;
    CHECK(result, "Failed to create sprite pipeline");
    return true;
}

static bool createCommandPool(VulkanRenderer* r) {
    VkCommandPoolCreateInfo info = {VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO};
    info.flags = VK_COMMAND_POOL_CREATE_RESET_COMMAND_BUFFER_BIT;
    info.queueFamilyIndex = r->families.graphics;
    CHECK(vkCreateCommandPool(r->device_, &info, NULL, &r->commandPool_),
          "Failed to create command pool");
    VkCommandBufferAllocateInfo allocate = {VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO};
    allocate.commandPool = r->commandPool_;
    allocate.level = VK_COMMAND_BUFFER_LEVEL_PRIMARY;
    allocate.commandBufferCount = FramesInFlight;
    CHECK(vkAllocateCommandBuffers(r->device_, &allocate, r->commandBuffers_),
          "Failed to allocate command buffers");
    return true;
}

static bool createVertexBuffer(VulkanRenderer* r) {
    VkDeviceSize size = sizeof(Sprite) * MaxSprites * FramesInFlight;
    VkBufferCreateInfo info = {VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO};
    info.size = size;
    info.usage = VK_BUFFER_USAGE_VERTEX_BUFFER_BIT;
    info.sharingMode = VK_SHARING_MODE_EXCLUSIVE;
    CHECK(vkCreateBuffer(r->device_, &info, NULL, &r->vertexBuffer_),
          "Failed to create vertex buffer");
    VkMemoryRequirements requirements = {0};
    vkGetBufferMemoryRequirements(r->device_, r->vertexBuffer_, &requirements);
    VkMemoryAllocateInfo allocate = {VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO};
    allocate.allocationSize = requirements.size;
    CHECK_CALL(
        findMemoryType(r, requirements.memoryTypeBits,
                       VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT,
                       &allocate.memoryTypeIndex));
    CHECK(vkAllocateMemory(r->device_, &allocate, NULL, &r->vertexMemory_),
          "Failed to allocate vertex memory");
    CHECK(vkBindBufferMemory(r->device_, r->vertexBuffer_, r->vertexMemory_, 0),
          "Failed to bind vertex memory");
    CHECK(vkMapMemory(r->device_, r->vertexMemory_, 0, size, 0, &r->mappedVertices_),
          "Failed to map vertex memory");
    return true;
}

static bool createSyncObjects(VulkanRenderer* r) {
    VkSemaphoreCreateInfo semaphore = {VK_STRUCTURE_TYPE_SEMAPHORE_CREATE_INFO};
    VkFenceCreateInfo fence = {VK_STRUCTURE_TYPE_FENCE_CREATE_INFO};
    fence.flags = VK_FENCE_CREATE_SIGNALED_BIT;
    for (uint32_t i = 0; i < FramesInFlight; ++i) {
        CHECK(vkCreateSemaphore(r->device_, &semaphore, NULL, &r->imageAvailable_[i]),
              "Failed to create semaphore");
        CHECK(vkCreateFence(r->device_, &fence, NULL, &r->inFlight_[i]), "Failed to create fence");
    }
    return true;
}

static bool recordCommandBuffer(VulkanRenderer* r, VkCommandBuffer command, uint32_t imageIndex,
                                uint32_t instanceCount, VkDeviceSize instanceOffset) {
    VkCommandBufferBeginInfo begin = {VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO};
    CHECK(vkBeginCommandBuffer(command, &begin), "Failed to begin command buffer");
    VkImageMemoryBarrier toAttachment = {VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER};
    toAttachment.srcAccessMask = 0;
    toAttachment.dstAccessMask = VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT;
    toAttachment.oldLayout = VK_IMAGE_LAYOUT_UNDEFINED;
    toAttachment.newLayout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;
    toAttachment.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    toAttachment.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    toAttachment.image = r->swapchainImages_[imageIndex];
    toAttachment.subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
    toAttachment.subresourceRange.levelCount = 1;
    toAttachment.subresourceRange.layerCount = 1;
    vkCmdPipelineBarrier(command, VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT,
                         VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT, 0, 0, NULL, 0, NULL, 1,
                         &toAttachment);
    VkClearValue clear = {{{0.0f, 0.0f, 0.0f, 1.0f}}};
    VkRenderingAttachmentInfo color = {VK_STRUCTURE_TYPE_RENDERING_ATTACHMENT_INFO};
    color.imageView = r->imageViews_[imageIndex];
    color.imageLayout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;
    color.loadOp = VK_ATTACHMENT_LOAD_OP_CLEAR;
    color.storeOp = VK_ATTACHMENT_STORE_OP_STORE;
    color.clearValue = clear;
    VkRenderingInfo rendering = {VK_STRUCTURE_TYPE_RENDERING_INFO};
    rendering.renderArea.extent = r->swapchainExtent_;
    rendering.layerCount = 1;
    rendering.colorAttachmentCount = 1;
    rendering.pColorAttachments = &color;
    vkCmdBeginRendering(command, &rendering);
    vkCmdBindPipeline(command, VK_PIPELINE_BIND_POINT_GRAPHICS, r->pipeline_);
    VkViewport viewport = {
        0, 0, (float)(r->swapchainExtent_.width), (float)(r->swapchainExtent_.height), 0, 1};
    VkRect2D scissor = {{0, 0}, r->swapchainExtent_};
    vkCmdSetViewport(command, 0, 1, &viewport);
    vkCmdSetScissor(command, 0, 1, &scissor);
    float screen[] = {(float)(r->swapchainExtent_.width), (float)(r->swapchainExtent_.height)};
    vkCmdPushConstants(command, r->pipelineLayout_, VK_SHADER_STAGE_VERTEX_BIT, 0, sizeof(screen),
                       screen);
    vkCmdBindVertexBuffers(command, 0, 1, &r->vertexBuffer_, &instanceOffset);
    vkCmdDraw(command, 6, instanceCount, 0, 0);
    ImGui_ImplVulkan_RenderDrawData(igGetDrawData(), command, VK_NULL_HANDLE);
    vkCmdEndRendering(command);
    VkImageMemoryBarrier toPresent = toAttachment;
    toPresent.srcAccessMask = VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT;
    toPresent.dstAccessMask = 0;
    toPresent.oldLayout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;
    toPresent.newLayout = VK_IMAGE_LAYOUT_PRESENT_SRC_KHR;
    vkCmdPipelineBarrier(command, VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT,
                         VK_PIPELINE_STAGE_BOTTOM_OF_PIPE_BIT, 0, 0, NULL, 0, NULL, 1, &toPresent);
    CHECK(vkEndCommandBuffer(command), "Failed to record command buffer");
    return true;
}

bool renderer_draw(VulkanRenderer* r, const Sprite* sprites, size_t sprite_count) {
    igRender();
    RECT area = {0};
    GetClientRect(r->window_, &area);
    if (IsIconic(r->window_) || area.right == 0 || area.bottom == 0)
        return true;
    CHECK(vkWaitForFences(r->device_, 1, &r->inFlight_[r->frame_], VK_TRUE, UINT64_MAX),
          "Failed waiting for frame fence");
    uint32_t imageIndex = 0;
    VkResult acquired =
        vkAcquireNextImageKHR(r->device_, r->swapchain_, UINT64_MAX, r->imageAvailable_[r->frame_],
                              VK_NULL_HANDLE, &imageIndex);
    if (acquired == VK_ERROR_OUT_OF_DATE_KHR) {
        CHECK_CALL(recreateSwapchain(r));
        return true;
    }
    if (acquired != VK_SUCCESS && acquired != VK_SUBOPTIMAL_KHR)
        CHECK(acquired, "Failed to acquire swapchain image");
    CHECK(vkResetFences(r->device_, 1, &r->inFlight_[r->frame_]), "Failed to reset frame fence");

    const size_t count = (sprite_count < MaxSprites ? sprite_count : MaxSprites);
    const VkDeviceSize frameStride = sizeof(Sprite) * MaxSprites;
    unsigned char* instances = (unsigned char*)r->mappedVertices_ + frameStride * r->frame_;
    if (count)
        memcpy(instances, sprites, count * sizeof(Sprite));
    CHECK(vkResetCommandBuffer(r->commandBuffers_[r->frame_], 0), "Failed to reset command buffer");
    CHECK_CALL(recordCommandBuffer(r, r->commandBuffers_[r->frame_], imageIndex, (uint32_t)(count),
                                   frameStride * r->frame_));
    VkPipelineStageFlags waitStage = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT;
    VkSubmitInfo submit = {VK_STRUCTURE_TYPE_SUBMIT_INFO};
    submit.waitSemaphoreCount = 1;
    submit.pWaitSemaphores = &r->imageAvailable_[r->frame_];
    submit.pWaitDstStageMask = &waitStage;
    submit.commandBufferCount = 1;
    submit.pCommandBuffers = &r->commandBuffers_[r->frame_];
    submit.signalSemaphoreCount = 1;
    submit.pSignalSemaphores = &r->renderFinished_[imageIndex];
    CHECK(vkQueueSubmit(r->graphicsQueue_, 1, &submit, r->inFlight_[r->frame_]),
          "Failed to submit draw");
    VkPresentInfoKHR present = {VK_STRUCTURE_TYPE_PRESENT_INFO_KHR};
    present.waitSemaphoreCount = 1;
    present.pWaitSemaphores = &r->renderFinished_[imageIndex];
    present.swapchainCount = 1;
    present.pSwapchains = &r->swapchain_;
    present.pImageIndices = &imageIndex;
    VkResult result = vkQueuePresentKHR(r->presentQueue_, &present);
    if (result == VK_ERROR_OUT_OF_DATE_KHR || result == VK_SUBOPTIMAL_KHR || r->framebufferResized_)
        CHECK_CALL(recreateSwapchain(r));
    else
        CHECK(result, "Failed to present frame");
    r->frame_ = (r->frame_ + 1) % FramesInFlight;
    return true;
}

static void destroySwapchain(VulkanRenderer* r) {
    if (r->renderFinished_)
        for (uint32_t i = 0; i < r->image_count; ++i)
            vkDestroySemaphore(r->device_, r->renderFinished_[i], NULL);
    free(r->renderFinished_);
    r->renderFinished_ = NULL;
    vkDestroyPipeline(r->device_, r->pipeline_, NULL);
    r->pipeline_ = VK_NULL_HANDLE;
    vkDestroyPipelineLayout(r->device_, r->pipelineLayout_, NULL);
    r->pipelineLayout_ = VK_NULL_HANDLE;
    if (r->imageViews_)
        for (uint32_t i = 0; i < r->image_count; ++i)
            vkDestroyImageView(r->device_, r->imageViews_[i], NULL);
    free(r->imageViews_);
    r->imageViews_ = NULL;
    free(r->swapchainImages_);
    r->swapchainImages_ = NULL;
    vkDestroySwapchainKHR(r->device_, r->swapchain_, NULL);
    r->swapchain_ = VK_NULL_HANDLE;
}

static bool recreateSwapchain(VulkanRenderer* r) {
    RECT area = {0};
    GetClientRect(r->window_, &area);
    if (area.right == 0 || area.bottom == 0)
        return true;
    CHECK(vkDeviceWaitIdle(r->device_), "Failed waiting for GPU");
    destroySwapchain(r);
    CHECK_CALL(createSwapchain(r));
    CHECK_CALL(createImageViews(r));
    CHECK_CALL(createPipeline(r));
    if (r->ui_initialized && !ui_reconfigure(r->image_count, r->swapchainFormat_))
        return fail(r, "Failed to recreate Dear ImGui Vulkan backend");
    r->framebufferResized_ = false;
    return true;
}

VulkanRenderer* renderer_create(HWND window, char* error, size_t error_size) {
    VulkanRenderer* r = calloc(1, sizeof(*r));
    if (!r) {
        if (error && error_size)
            snprintf(error, error_size, "Could not allocate renderer");
        return NULL;
    }
    r->window_ = window;
    bool initialized = createInstance(r) && createSurface(r) && selectPhysicalDevice(r) &&
                       createDevice(r) && createSwapchain(r) && createImageViews(r) &&
                       createPipeline(r) && createCommandPool(r) && createVertexBuffer(r) &&
                       createSyncObjects(r);
    if (initialized) {
        UiVulkanInfo info = {window,         r->instance_,         r->physicalDevice_,
                             r->device_,     r->families.graphics, r->graphicsQueue_,
                             r->image_count, r->swapchainFormat_};
        r->ui_initialized = ui_init(&info);
        initialized = r->ui_initialized;
        if (!initialized)
            fail(r, "Failed to initialize Dear ImGui");
    }
    if (!initialized) {
        if (error && error_size)
            snprintf(error, error_size, "%s", r->error);
        renderer_destroy(r);
        return NULL;
    }
    if (error && error_size)
        error[0] = '\0';
    return r;
}

void renderer_destroy(VulkanRenderer* r) {
    if (!r)
        return;
    if (r->device_) {
        vkDeviceWaitIdle(r->device_);
        if (r->ui_initialized)
            ui_shutdown();
        for (uint32_t i = 0; i < FramesInFlight; ++i) {
            vkDestroySemaphore(r->device_, r->imageAvailable_[i], NULL);
            vkDestroyFence(r->device_, r->inFlight_[i], NULL);
        }
        if (r->mappedVertices_)
            vkUnmapMemory(r->device_, r->vertexMemory_);
        vkDestroyBuffer(r->device_, r->vertexBuffer_, NULL);
        vkFreeMemory(r->device_, r->vertexMemory_, NULL);
        vkDestroyCommandPool(r->device_, r->commandPool_, NULL);
        vkDestroyShaderModule(r->device_, r->vertex_shader, NULL);
        vkDestroyShaderModule(r->device_, r->fragment_shader, NULL);
        destroySwapchain(r);
        vkDestroyDevice(r->device_, NULL);
    }
    if (r->surface_)
        vkDestroySurfaceKHR(r->instance_, r->surface_, NULL);
    if (r->instance_)
        vkDestroyInstance(r->instance_, NULL);
    free(r->support.formats);
    free(r->support.presentModes);
    free(r);
}

void renderer_resized(VulkanRenderer* r) {
    if (r)
        r->framebufferResized_ = true;
}

const char* renderer_error(const VulkanRenderer* r) {
    return r->error;
}
