#include <brtoy/gfx_swapchain.h>
#include <brtoy/gfx_utils.h>
#include <vk_mem_alloc.h>

namespace brtoy {

Backbuffer::Backbuffer(VkDevice device) : m_device(device), m_dim({}) {}

Backbuffer::~Backbuffer() {
    if (!m_buffers.empty()) {
        StackVector<VkFence, BufferCountMax> fences;
        for (const Buffer &buffer : m_buffers) {
            fences.push_back(buffer.fence);
        }
        vkWaitForFences(m_device, fences.size(), fences.data(), true, UINT64_MAX);
        for (const Buffer &buffer : m_buffers) {
            vkDestroyImageView(m_device, buffer.view, nullptr);
            vkDestroyFence(m_device, buffer.fence, nullptr);
        }
    }
    m_device = VK_NULL_HANDLE;
    m_dim = {};
    m_buffers.clear();
}

Backbuffer Backbuffer::createFromSwapchain(VkDevice device, const Swapchain &swapchain) {
    Backbuffer result(device);

    u32 image_count;
    vkGetSwapchainImagesKHR(device, swapchain.m_swapchain, &image_count, nullptr);
    BRTOY_ASSERT(image_count <= result.m_buffers.capacity());
    StackVector<VkImage, BufferCountMax> images(image_count);
    vkGetSwapchainImagesKHR(device, swapchain.m_swapchain, &image_count, images.data());

    result.m_dim = swapchain.m_dim;
    for (const VkImage image : images) {
        VkImageViewCreateInfo view_create_info = {
            .sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO,
            .pNext = nullptr,
            .flags = 0,
            .image = image,
            .viewType = VK_IMAGE_VIEW_TYPE_2D,
            .format = swapchain.m_format.format,
            .components = {},
            .subresourceRange =
                {
                    .aspectMask = VK_IMAGE_ASPECT_COLOR_BIT,
                    .baseMipLevel = 0,
                    .levelCount = 1,
                    .baseArrayLayer = 0,
                    .layerCount = 1,
                },
        };

        Buffer buffer;
        buffer.image = image;
        vkCreateImageView(device, &view_create_info, nullptr, &buffer.view);

        VkFenceCreateInfo fence_create_info = {VK_STRUCTURE_TYPE_FENCE_CREATE_INFO, nullptr,
                                               VK_FENCE_CREATE_SIGNALED_BIT};
        vkCreateFence(device, &fence_create_info, nullptr, &buffer.fence);
        result.m_buffers.push_back(std::move(buffer));
    }
    return result;
}

CommandBufferPool::CommandBufferPool(const GfxDevice &device) : m_device(device) {
    VkCommandPoolCreateInfo cmd_pool_create_info = {
        .sType = VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO,
        .pNext = nullptr,
        .flags = VK_COMMAND_POOL_CREATE_RESET_COMMAND_BUFFER_BIT,
        .queueFamilyIndex = m_device.m_queue_family_index};

    vkCreateCommandPool(m_device.m_device, &cmd_pool_create_info, nullptr, &m_cmd_pool);
}

CommandBufferPool::~CommandBufferPool() {
    std::vector<VkFence> fences;
    fences.reserve(m_pending.size());
    for (const auto &alloc : m_pending) {
        fences.push_back(alloc.fence);
    }
    vkWaitForFences(m_device.m_device, fences.size(), fences.data(), VK_TRUE, UINT64_MAX);
    sync();
    BRTOY_ASSERT(m_pending.empty());
    vkDestroyCommandPool(m_device.m_device, m_cmd_pool, nullptr);
}

void CommandBufferPool::sync() {
    for (auto it = m_pending.begin(); it != m_pending.end();) {
        VkResult fence_status = vkGetFenceStatus(m_device.m_device, it->fence);
        if (fence_status == VK_SUCCESS) {
            vkResetCommandBuffer(it->cmd_buffer, VK_COMMAND_BUFFER_RESET_RELEASE_RESOURCES_BIT);
            m_free.push_back(it->cmd_buffer);
            it = m_pending.erase(it);
        } else {
            ++it;
        }
    }
}

VkCommandBuffer CommandBufferPool::acquire() {
    VkCommandBuffer result = VK_NULL_HANDLE;
    if (m_free.empty()) {
        VkCommandBufferAllocateInfo cb_alloc_info = {
            .sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO,
            .pNext = nullptr,
            .commandPool = m_cmd_pool,
            .level = VK_COMMAND_BUFFER_LEVEL_PRIMARY,
            .commandBufferCount = 1,
        };
        vkAllocateCommandBuffers(m_device.m_device, &cb_alloc_info, &result);
    } else {
        result = m_free.back();
        m_free.pop_back();
    }
    return result;
}

void CommandBufferPool::release(VkCommandBuffer cmd, VkFence fence) {
    m_pending.emplace_back(cmd, fence);
}

TexturePool::TexturePool(const GfxDevice &device, VmaAllocator memory_allocator,
                         VkImageCreateInfo image_create_info,
                         VkImageViewCreateInfo view_create_info, VkImageMemoryBarrier init_barrier,
                         VkPipelineStageFlags init_dst_stage_mask)
    : m_device(device), m_memory_allocator(memory_allocator),
      m_image_create_info(image_create_info), m_view_create_info(view_create_info),
      m_init_barrier(init_barrier), m_init_dst_stage_mask(init_dst_stage_mask) {}

TexturePool::~TexturePool() {
    std::vector<VkFence> fences;
    fences.reserve(m_pending.size());
    for (const auto &alloc : m_pending) {
        fences.push_back(alloc.fence);
    }
    vkWaitForFences(m_device.m_device, fences.size(), fences.data(), VK_TRUE, UINT64_MAX);
    sync();
    BRTOY_ASSERT(m_pending.empty());
    for (auto &texture : m_free)
        free(texture);
}

void TexturePool::sync() {
    for (auto it = m_pending.begin(); it != m_pending.end();) {
        VkResult fence_status = vkGetFenceStatus(m_device.m_device, it->fence);
        if (fence_status == VK_SUCCESS) {
            m_free.push_back(it->texture);
            it = m_pending.erase(it);
        } else {
            ++it;
        }
    }
}

TexturePool::Texture TexturePool::acquire(VkCommandBuffer cmd, V2u dim) {
    Texture texture = {};
    while (texture.image == VK_NULL_HANDLE && !m_free.empty()) {
        texture = m_free.back();
        m_free.pop_back();
        if (texture.dim != dim) {
            free(texture);
            texture = {};
        }
    }

    if (texture.image == VK_NULL_HANDLE) {
        texture.dim = dim;

        VmaAllocationCreateInfo allocation_info = {
            .flags = 0,
            .usage = VMA_MEMORY_USAGE_AUTO,
            .requiredFlags = VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT,
        };

        VkImageCreateInfo image_create_info = m_image_create_info;
        image_create_info.extent = {dim.x, dim.y, 1};
        VkResult result;
        result = vmaCreateImage(m_memory_allocator, &image_create_info, &allocation_info,
                                &texture.image, &texture.memory, nullptr);
        if (result == VK_SUCCESS) {
            VkImageViewCreateInfo view_create_info = m_view_create_info;
            view_create_info.image = texture.image;
            result =
                vkCreateImageView(m_device.m_device, &view_create_info, nullptr, &texture.view);
            if (result == VK_SUCCESS) {
                VkImageMemoryBarrier barrier = m_init_barrier;
                barrier.image = texture.image;
                std::array image_barriers = {barrier};
                vkCmdPipelineBarrier(cmd, VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT, m_init_dst_stage_mask,
                                     0, 0, nullptr, 0, nullptr, image_barriers.size(),
                                     image_barriers.data());
            } else {
                vmaDestroyImage(m_memory_allocator, texture.image, texture.memory);
                texture = {};
            }
        }
    }
    return texture;
}

void TexturePool::release(VkCommandBuffer cmd, const Texture &texture, VkFence fence) {
    m_pending.emplace_back(texture, fence);
}

void TexturePool::free(Texture &texture) {
    vkDestroyImageView(m_device.m_device, texture.view, nullptr);
    vmaDestroyImage(m_memory_allocator, texture.image, texture.memory);
    texture = {};
}

void *BufferSubAllocation::ptr() { return (std::byte *)mapped_ptr + offset; }

VkDeviceSize alignUp(VkDeviceSize x, VkDeviceSize alignment) {
    VkDeviceSize result = x;
    VkDeviceSize rem = x % alignment;
    if (rem != 0)
        result = x + alignment - rem;
    BRTOY_ASSERT(result % alignment == 0);
    return result;
}

VkDeviceSize alignUp(std::span<VkDeviceSize> values, VkDeviceSize alignment) {
    VkDeviceSize result = 0;
    for (auto x : values)
        result += alignUp(x, alignment);
    return result;
}

BufferSubAllocation LinearAllocator::allocateBytes(VkDeviceSize size, VkDeviceSize alignment) {
    BufferSubAllocation result = {};
    alignment = alignment == 0 ? m_min_alignment : alignment;
    VkDeviceSize offset = alignUp(m_cur, alignment);
    VkDeviceSize new_cur = offset + size;
    if (new_cur <= m_end) {
        result.buffer = m_buffer;
        result.offset = offset;
        result.size = size;
        result.mapped_ptr = m_mapped_ptr;
        m_cur = new_cur;
    }
    return result;
}

void LinearAllocator::reset() { m_cur = m_start; }

} // namespace brtoy
