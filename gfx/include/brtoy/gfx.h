#pragma once
#include <brtoy/brtoy.h>
#include <optional>
#include <vulkan/vulkan.h>

namespace brtoy {

enum class GfxDebugFlag {
    None = 0,
    ValidationEnable = 1,
};

struct GfxInstance {
    static std::optional<GfxInstance> create(const char *app_name, u32 app_version,
                                             GfxDebugFlag flags = GfxDebugFlag::None);
    GfxInstance() = default;
    GfxInstance(GfxInstance &) = delete;
    GfxInstance(GfxInstance &&);
    GfxInstance &operator=(const GfxInstance &) = delete;
    GfxInstance &operator=(GfxInstance &&);
    ~GfxInstance();

    void destroy();
    VkSurfaceKHR createSurface(OsHandle app_instance, OsHandle window);

    VkInstance m_instance = VK_NULL_HANDLE;
    GfxDebugFlag m_flags = GfxDebugFlag::None;
    uint32_t m_api_version = 0;
};

struct GfxDevice {
    static std::optional<GfxDevice> createDefault(GfxInstance &instance);
    GfxDevice() = default;
    GfxDevice(GfxDevice &) = delete;
    GfxDevice(GfxDevice &&);
    GfxDevice &operator=(const GfxDevice &) = delete;
    GfxDevice &operator=(GfxDevice &&);
    ~GfxDevice();

    void destroy();

    VkPhysicalDevice m_physical_device = VK_NULL_HANDLE;
    VkDevice m_device = VK_NULL_HANDLE;
    VkQueue m_queue = VK_NULL_HANDLE;
    u32 m_queue_family_index = 0;
};

} // namespace brtoy
