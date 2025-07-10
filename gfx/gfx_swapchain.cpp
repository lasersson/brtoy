#include <brtoy/gfx_swapchain.h>
#include <vector>

namespace brtoy {

Swapchain::Swapchain(GfxInstance &instance, u64 app_instance, u64 window,
                     VkPhysicalDevice physical_device, VkDevice device)
    : m_instance(instance.m_instance), m_surface(instance.createSurface(app_instance, window)),
      m_physical_device(physical_device), m_device(device) {
    u32 surface_format_count = 0;
    vkGetPhysicalDeviceSurfaceFormatsKHR(m_physical_device, m_surface, &surface_format_count,
                                         nullptr);
    std::vector<VkSurfaceFormatKHR> surface_formats(surface_format_count);
    vkGetPhysicalDeviceSurfaceFormatsKHR(m_physical_device, m_surface, &surface_format_count,
                                         surface_formats.data());
    VkSurfaceFormatKHR selected_surface_format = surface_formats[0];
    for (const auto &fmt : surface_formats) {
        if (fmt.colorSpace == VK_COLORSPACE_SRGB_NONLINEAR_KHR) {
            if (fmt.format == VK_FORMAT_R8G8B8A8_SRGB || fmt.format == VK_FORMAT_B8G8R8A8_SRGB) {
                selected_surface_format = fmt;
                break;
            }
        }
    }
    m_format = selected_surface_format;
}

Swapchain::~Swapchain() {
    if (m_swapchain != VK_NULL_HANDLE)
        vkDestroySwapchainKHR(m_device, m_swapchain, nullptr);
    if (m_surface != VK_NULL_HANDLE)
        vkDestroySurfaceKHR(m_instance, m_surface, nullptr);
}

void Swapchain::recreate(V2u dim) {

    VkSwapchainCreateInfoKHR sc_create_info = {
        .sType = VK_STRUCTURE_TYPE_SWAPCHAIN_CREATE_INFO_KHR,
        .pNext = nullptr,
        .flags = 0,
        .surface = m_surface,
        .minImageCount = 3,
        .imageFormat = m_format.format,
        .imageColorSpace = m_format.colorSpace,
        .imageExtent = {dim.x, dim.y},
        .imageArrayLayers = 1,
        .imageUsage = VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT,
        .imageSharingMode = VK_SHARING_MODE_EXCLUSIVE,
        .queueFamilyIndexCount = 0,
        .pQueueFamilyIndices = nullptr,
        .preTransform = VK_SURFACE_TRANSFORM_IDENTITY_BIT_KHR,
        .compositeAlpha = VK_COMPOSITE_ALPHA_OPAQUE_BIT_KHR,
        .presentMode = VK_PRESENT_MODE_FIFO_KHR,
        .clipped = VK_TRUE,
        .oldSwapchain = m_swapchain,
    };

    VkSwapchainKHR new_swapchain;
    VkResult result = vkCreateSwapchainKHR(m_device, &sc_create_info, nullptr, &new_swapchain);
    if (result == VK_SUCCESS) {
        vkDestroySwapchainKHR(m_device, m_swapchain, nullptr);
        m_swapchain = new_swapchain;
        m_dim = dim;
    }
}

} // namespace brtoy
