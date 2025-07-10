#pragma once
#include <brtoy/gfx.h>
#include <brtoy/vec.h>

namespace brtoy {

struct Swapchain {
    Swapchain(GfxInstance &instance, u64 app_instance, u64 window, VkPhysicalDevice physical_device,
              VkDevice device);
    ~Swapchain();

    void recreate(V2u dim);

    VkInstance m_instance = VK_NULL_HANDLE;
    VkSurfaceKHR m_surface = VK_NULL_HANDLE;

    VkPhysicalDevice m_physical_device = VK_NULL_HANDLE;
    VkDevice m_device = VK_NULL_HANDLE;
    VkSwapchainKHR m_swapchain = VK_NULL_HANDLE;
    VkSurfaceFormatKHR m_format = {VK_FORMAT_UNDEFINED, VK_COLORSPACE_SRGB_NONLINEAR_KHR};
    V2u m_dim = {};
};

} // namespace brtoy
