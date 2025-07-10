#include <algorithm>
#include <array>
#include <brtoy/brtoy.h>
#include <brtoy/gfx.h>
#include <set>
#include <span>
#include <string>
#include <vector>

namespace brtoy {

template <typename T> std::underlying_type_t<T> underlying(T x) {
    return static_cast<std::underlying_type_t<T>>(x);
}

GfxDebugFlag operator&(GfxDebugFlag a, GfxDebugFlag b) {
    return static_cast<GfxDebugFlag>(underlying(a) & underlying(b));
}

static bool hasFlag(GfxDebugFlag flags, GfxDebugFlag flag) {
    return (flags & flag) != GfxDebugFlag::None;
}

static std::set<std::string_view> getRequiredInstanceLayers(GfxDebugFlag flags) {
    std::set<std::string_view> layers;
    if (hasFlag(flags, GfxDebugFlag::ValidationEnable))
        layers.insert("VK_LAYER_KHRONOS_validation");
    return layers;
}

static std::vector<VkExtensionProperties> getInstanceExtensions(const char *layer_name) {
    u32 extension_count = 0;
    vkEnumerateInstanceExtensionProperties(layer_name, &extension_count, nullptr);
    std::vector<VkExtensionProperties> extensions;
    extensions.resize(extension_count);
    vkEnumerateInstanceExtensionProperties(layer_name, &extension_count, extensions.data());
    return extensions;
}

std::set<std::string_view> getRequiredPlatformInstanceExtensions();
static std::set<std::string_view> getRequiredInstanceExtensions(GfxDebugFlag flags) {
    std::set<std::string_view> extensions = {VK_KHR_SURFACE_EXTENSION_NAME};
    if (hasFlag(flags, GfxDebugFlag::ValidationEnable)) {
        extensions.insert(VK_EXT_DEBUG_UTILS_EXTENSION_NAME);
    }
    extensions.merge(getRequiredPlatformInstanceExtensions());
    return extensions;
}

static std::set<std::string_view> getRequiredDeviceLayers(GfxDebugFlag flags) {
    std::set<std::string_view> layers = {};
    if (hasFlag(flags, GfxDebugFlag::ValidationEnable))
        layers.insert("VK_LAYER_KHRONOS_validation");
    return layers;
}

static std::vector<VkExtensionProperties> getDeviceExtensions(VkPhysicalDevice physical_device,
                                                              const char *layer_name) {
    u32 extension_count = 0;
    vkEnumerateDeviceExtensionProperties(physical_device, layer_name, &extension_count, nullptr);
    std::vector<VkExtensionProperties> extensions;
    extensions.resize(extension_count);
    vkEnumerateDeviceExtensionProperties(physical_device, layer_name, &extension_count,
                                         extensions.data());
    return extensions;
}

std::set<std::string_view> getRequiredPlatformDeviceExtensions();
static std::set<std::string_view> getRequiredDeviceExtensions() {
    std::set<std::string_view> extensions = {
        VK_KHR_SWAPCHAIN_EXTENSION_NAME,
        VK_KHR_DYNAMIC_RENDERING_EXTENSION_NAME,
    };
    extensions.merge(getRequiredPlatformDeviceExtensions());
    return extensions;
}

static bool hasExt(std::span<const std::string_view> extensions, std::string_view ext) {
    return std::find(extensions.begin(), extensions.end(), ext) != extensions.end();
}

std::optional<GfxInstance> GfxInstance::create(const char *app_name, u32 app_version,
                                               GfxDebugFlag flags) {
    VkApplicationInfo app_info = {
        .sType = VK_STRUCTURE_TYPE_APPLICATION_INFO,
        .pNext = nullptr,
        .pApplicationName = app_name,
        .applicationVersion = app_version,
        .pEngineName = "brtoy",
        .engineVersion = 0,
        .apiVersion = VK_API_VERSION_1_3,
    };

    const auto required_layers = getRequiredInstanceLayers(flags);
    std::set<std::string> enabled_layers;

    const auto required_extensions = getRequiredInstanceExtensions(flags);
    size_t available_required_extensions = 0;
    std::set<std::string> enabled_extensions;

    u32 instance_layer_count;
    vkEnumerateInstanceLayerProperties(&instance_layer_count, nullptr);
    std::vector<VkLayerProperties> instance_layers;
    instance_layers.resize(instance_layer_count);
    vkEnumerateInstanceLayerProperties(&instance_layer_count, instance_layers.data());
    for (const auto &layer : instance_layers) {
        if (required_layers.contains(layer.layerName)) {
            enabled_layers.insert(layer.layerName);
            auto layer_extensions = getInstanceExtensions(layer.layerName);
            for (const auto &ext : layer_extensions) {
                if (required_extensions.contains(ext.extensionName))
                    enabled_extensions.insert(ext.extensionName);
            }
        }
    }

    auto instance_extensions = getInstanceExtensions(nullptr);
    for (const auto &ext : instance_extensions) {
        if (required_extensions.contains(ext.extensionName))
            enabled_extensions.insert(ext.extensionName);
    }

    std::optional<GfxInstance> result;
    if (std::includes(enabled_layers.begin(), enabled_layers.end(), required_layers.begin(),
                      required_layers.end()) &&
        std::includes(enabled_extensions.begin(), enabled_extensions.end(),
                      required_extensions.begin(), required_extensions.end())) {

        std::vector<const char *> layer_names;
        for (const auto &n : enabled_layers)
            layer_names.push_back(n.c_str());
        std::vector<const char *> ext_names;
        for (const auto &n : enabled_extensions)
            ext_names.push_back(n.c_str());

        VkInstanceCreateInfo instance_create_info = {
            .sType = VK_STRUCTURE_TYPE_INSTANCE_CREATE_INFO,
            .pNext = nullptr,
            .flags = 0,
            .pApplicationInfo = &app_info,
            .enabledLayerCount = (u32)layer_names.size(),
            .ppEnabledLayerNames = layer_names.data(),
            .enabledExtensionCount = (u32)ext_names.size(),
            .ppEnabledExtensionNames = ext_names.data(),
        };

        VkInstance vk_instance;
        VkResult vkr = vkCreateInstance(&instance_create_info, nullptr, &vk_instance);
        if (vkr == VK_SUCCESS) {
            result.emplace();
            result->m_instance = vk_instance;
            result->m_flags = flags;
            result->m_api_version = app_info.apiVersion;
        }
    }
    return result;
}

GfxInstance::GfxInstance(GfxInstance &&that) { *this = std::move(that); }

GfxInstance &GfxInstance::operator=(GfxInstance &&that) {
    destroy();
    this->m_instance = that.m_instance;
    this->m_flags = that.m_flags;
    this->m_api_version = that.m_api_version;
    that.m_instance = VK_NULL_HANDLE;
    that.m_flags = GfxDebugFlag::None;
    that.m_api_version = 0;
    return *this;
}

GfxInstance::~GfxInstance() { destroy(); }

void GfxInstance::destroy() {
    if (m_instance != VK_NULL_HANDLE)
        vkDestroyInstance(m_instance, nullptr);
}

static VkPhysicalDevice selectPhysicalDevice(VkInstance instance) {
    VkPhysicalDevice selected_device = VK_NULL_HANDLE;
    u32 physical_device_count = 0;
    vkEnumeratePhysicalDevices(instance, &physical_device_count, nullptr);
    std::vector<VkPhysicalDevice> physical_devices;
    physical_devices.resize(physical_device_count);
    vkEnumeratePhysicalDevices(instance, &physical_device_count, physical_devices.data());
    for (const VkPhysicalDevice dev : physical_devices) {
        VkPhysicalDeviceProperties2 device_properties{};
        device_properties.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_PROPERTIES_2;
        vkGetPhysicalDeviceProperties2(dev, &device_properties);
        if (device_properties.properties.deviceType == VK_PHYSICAL_DEVICE_TYPE_DISCRETE_GPU) {
            selected_device = dev;
            break;
        }
    }
    if (selected_device == VK_NULL_HANDLE && physical_device_count > 0) {
        selected_device = physical_devices[0];
    }
    return selected_device;
}

std::optional<GfxDevice> GfxDevice::createDefault(GfxInstance &instance) {
    std::optional<GfxDevice> result;
    VkPhysicalDevice physical_device = selectPhysicalDevice(instance.m_instance);
    if (physical_device) {
        u32 queue_family_count = 0;
        vkGetPhysicalDeviceQueueFamilyProperties2(physical_device, &queue_family_count, nullptr);
        std::vector<VkQueueFamilyProperties2> queue_families;
        queue_families.resize(queue_family_count);
        for (auto &qf : queue_families)
            qf.sType = VK_STRUCTURE_TYPE_QUEUE_FAMILY_PROPERTIES_2;
        vkGetPhysicalDeviceQueueFamilyProperties2(physical_device, &queue_family_count,
                                                  queue_families.data());

        // TODO(fl): multiple queues.
        u32 selected_queue_family_index = queue_family_count;
        VkQueueFlags required_queue_flags =
            VK_QUEUE_GRAPHICS_BIT | VK_QUEUE_COMPUTE_BIT | VK_QUEUE_TRANSFER_BIT;
        for (u32 queue_family_index = 0; queue_family_index < queue_family_count &&
                                         selected_queue_family_index == queue_family_count;
             ++queue_family_index) {
            VkQueueFlags flags =
                queue_families[queue_family_index].queueFamilyProperties.queueFlags;
            if ((flags & required_queue_flags) == required_queue_flags) {
                selected_queue_family_index = queue_family_index;
            }
        }

        if (selected_queue_family_index < queue_family_count) {
            float queue_priorities[] = {0.0f};
            VkDeviceQueueCreateInfo queue_create_info = {
                .sType = VK_STRUCTURE_TYPE_DEVICE_QUEUE_CREATE_INFO,
                .pNext = nullptr,
                .flags = 0,
                .queueFamilyIndex = selected_queue_family_index,
                .queueCount = 1,
                .pQueuePriorities = queue_priorities,
            };

            const auto required_layers = getRequiredDeviceLayers(instance.m_flags);
            std::set<std::string> enabled_layers;

            const auto required_extensions = getRequiredDeviceExtensions();
            std::set<std::string> enabled_extensions;

            u32 layer_count = 0;
            vkEnumerateDeviceLayerProperties(physical_device, &layer_count, nullptr);
            std::vector<VkLayerProperties> device_layers;
            device_layers.resize(layer_count);
            vkEnumerateDeviceLayerProperties(physical_device, &layer_count, device_layers.data());

            for (const auto &layer : device_layers) {
                if (required_layers.contains(layer.layerName)) {
                    enabled_layers.insert(layer.layerName);
                    auto layer_extensions = getDeviceExtensions(physical_device, layer.layerName);
                    for (const auto &ext : layer_extensions) {
                        if (required_extensions.contains(ext.extensionName)) {
                            enabled_extensions.insert(ext.extensionName);
                        }
                    }
                }
            }

            auto impl_extensions = getDeviceExtensions(physical_device, nullptr);
            for (const auto &ext : impl_extensions) {
                if (required_extensions.contains(ext.extensionName)) {
                    enabled_extensions.insert(ext.extensionName);
                }
            }

            if (std::includes(enabled_layers.begin(), enabled_layers.end(), required_layers.begin(),
                              required_layers.end()) &&
                std::includes(enabled_extensions.begin(), enabled_extensions.end(),
                              required_extensions.begin(), required_extensions.end())) {

                std::vector<const char *> layer_names;
                for (const auto &n : enabled_layers)
                    layer_names.push_back(n.c_str());
                std::vector<const char *> extension_names;
                for (const auto &n : enabled_extensions)
                    extension_names.push_back(n.c_str());

                VkPhysicalDeviceVulkan13Features features{};
                features.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_VULKAN_1_3_FEATURES;
                features.dynamicRendering = true;
                features.maintenance4 = true;

                VkDeviceCreateInfo device_create_info = {
                    .sType = VK_STRUCTURE_TYPE_DEVICE_CREATE_INFO,
                    .pNext = &features,
                    .flags = 0,
                    .queueCreateInfoCount = 1,
                    .pQueueCreateInfos = &queue_create_info,
                    .enabledLayerCount = (u32)layer_names.size(),
                    .ppEnabledLayerNames = layer_names.data(),
                    .enabledExtensionCount = (u32)extension_names.size(),
                    .ppEnabledExtensionNames = extension_names.data(),
                    .pEnabledFeatures = nullptr,
                };

                VkDevice vk_device;
                VkResult vkr =
                    vkCreateDevice(physical_device, &device_create_info, nullptr, &vk_device);

                if (vkr == VK_SUCCESS) {
                    VkQueue queue;
                    vkGetDeviceQueue(vk_device, selected_queue_family_index, 0, &queue);

                    result.emplace();
                    result->m_physical_device = physical_device;
                    result->m_device = vk_device;
                    result->m_queue = queue;
                    result->m_queue_family_index = selected_queue_family_index;
                }
            }
        }
    }

    return result;
}

GfxDevice::GfxDevice(GfxDevice &&that) { *this = std::move(that); }

GfxDevice &GfxDevice::operator=(GfxDevice &&that) {
    destroy();
    this->m_device = that.m_device;
    this->m_physical_device = that.m_physical_device;
    this->m_queue = that.m_queue;
    this->m_queue_family_index = that.m_queue_family_index;
    that.m_device = VK_NULL_HANDLE;
    that.m_physical_device = VK_NULL_HANDLE;
    that.m_queue = VK_NULL_HANDLE;
    that.m_queue_family_index = 0;
    return *this;
}

GfxDevice::~GfxDevice() { destroy(); }

void GfxDevice::destroy() {
    if (m_device != VK_NULL_HANDLE)
        vkDestroyDevice(m_device, nullptr);
}

} // namespace brtoy
