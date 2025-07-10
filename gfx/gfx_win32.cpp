#include <Windows.h>
#include <brtoy/gfx.h>
#include <set>
#include <string_view>
#include <vulkan/vulkan_win32.h>

namespace brtoy {

std::set<std::string_view> getRequiredPlatformInstanceExtensions() {
    return {VK_KHR_WIN32_SURFACE_EXTENSION_NAME};
}

std::set<std::string_view> getRequiredPlatformDeviceExtensions() { return {}; }

VkSurfaceKHR GfxInstance::createSurface(OsHandle app_instance, OsHandle window) {
    VkWin32SurfaceCreateInfoKHR surface_create_info = {
        .sType = VK_STRUCTURE_TYPE_WIN32_SURFACE_CREATE_INFO_KHR,
        .pNext = nullptr,
        .flags = 0,
        .hinstance = (HINSTANCE)app_instance,
        .hwnd = (HWND)window,
    };
    VkSurfaceKHR surface = VK_NULL_HANDLE;
    vkCreateWin32SurfaceKHR(m_instance, &surface_create_info, nullptr, &surface);
    return surface;
}

} // namespace brtoy
