#include "Ui.h"

static bool win32_initialized;
static bool vulkan_initialized;
static VkFormat color_format;
static UiVulkanInfo current_config;

static bool init_vulkan(const UiVulkanInfo* config) {
    current_config = *config;
    ImGui_ImplVulkan_InitInfo info = {0};
    info.ApiVersion = VK_API_VERSION_1_3;
    info.Instance = config->instance;
    info.PhysicalDevice = config->physical_device;
    info.Device = config->device;
    info.QueueFamily = config->queue_family;
    info.Queue = config->queue;
    info.DescriptorPoolSize = 32;
    info.MinImageCount = 2;
    info.ImageCount = config->image_count;
    info.UseDynamicRendering = true;
    info.PipelineInfoMain.MSAASamples = VK_SAMPLE_COUNT_1_BIT;
    color_format = config->format;
    VkPipelineRenderingCreateInfoKHR* rendering =
        &info.PipelineInfoMain.PipelineRenderingCreateInfo;
    rendering->sType = VK_STRUCTURE_TYPE_PIPELINE_RENDERING_CREATE_INFO;
    rendering->colorAttachmentCount = 1;
    rendering->pColorAttachmentFormats = &color_format;
    vulkan_initialized = ImGui_ImplVulkan_Init(&info);
    return vulkan_initialized;
}

bool ui_init(const UiVulkanInfo* config) {
    /* Check the generated C declarations against the linked C++ library. */
    if (!igDebugCheckVersionAndDataLayout("1.92.8", sizeof(ImGuiIO), sizeof(ImGuiStyle),
                                          sizeof(ImVec2_c), sizeof(ImVec4_c), sizeof(ImDrawVert),
                                          sizeof(ImDrawIdx)))
        return false;
    if (!igCreateContext(NULL))
        return false;
    igStyleColorsDark(NULL);
    win32_initialized = ImGui_ImplWin32_Init(config->window);
    if (!win32_initialized || !init_vulkan(config)) {
        ui_shutdown();
        return false;
    }
    return true;
}

bool ui_reconfigure(uint32_t image_count, VkFormat format) {
    if (current_config.image_count == image_count && current_config.format == format)
        return true;
    ImGui_ImplVulkan_Shutdown();
    vulkan_initialized = false;
    current_config.image_count = image_count;
    current_config.format = format;
    return init_vulkan(&current_config);
}

void ui_shutdown(void) {
    if (vulkan_initialized)
        ImGui_ImplVulkan_Shutdown();
    if (win32_initialized)
        ImGui_ImplWin32_Shutdown();
    if (igGetCurrentContext())
        igDestroyContext(NULL);
    vulkan_initialized = win32_initialized = false;
}

void ui_begin_frame(void) {
    ImGui_ImplVulkan_NewFrame();
    ImGui_ImplWin32_NewFrame();
    igNewFrame();
}
