#pragma once
#include <brtoy/container.h>
#include <brtoy/gfx.h>
#include <brtoy/vec.h>
#include <span>
#include <vector>
#include <vk_mem_alloc.h>

namespace brtoy {

struct Swapchain;

struct Backbuffer {
    static constexpr size_t BufferCountMax = 3;
    Backbuffer(VkDevice device);
    ~Backbuffer();
    Backbuffer(const Backbuffer &) = delete;
    Backbuffer &operator=(const Backbuffer &) = delete;
    Backbuffer(Backbuffer &&) = default;
    Backbuffer &operator=(Backbuffer &&) = default;

    static Backbuffer createFromSwapchain(VkDevice device, const Swapchain &swapchain);

    VkDevice m_device;
    V2u m_dim;

    struct Buffer {
        VkImage image;
        VkImageView view;
        VkFence fence;
    };
    StackVector<Buffer, BufferCountMax> m_buffers;
};

struct CommandBufferPool {
    CommandBufferPool(const GfxDevice &device);
    ~CommandBufferPool();

    void sync();
    VkCommandBuffer acquire();
    void release(VkCommandBuffer cmd, VkFence fence);

    const GfxDevice &m_device;
    VkCommandPool m_cmd_pool;

    struct CmdBufferAllocation {
        VkCommandBuffer cmd_buffer;
        VkFence fence;
    };
    std::vector<CmdBufferAllocation> m_pending;
    std::vector<VkCommandBuffer> m_free;
};

struct TexturePool {
    struct Texture {
        V2u dim;
        VmaAllocation memory;
        VkImage image;
        VkImageView view;
    };

    TexturePool(const GfxDevice &device, VmaAllocator memory_allocator,
                VkImageCreateInfo image_create_info, VkImageViewCreateInfo view_create_info,
                VkImageMemoryBarrier init_barrier, VkPipelineStageFlags init_dst_stage_mask);
    ~TexturePool();

    void sync();
    Texture acquire(VkCommandBuffer cmd, V2u extent);
    void release(VkCommandBuffer cmd, const Texture &texture, VkFence fence);
    void free(Texture &texture);

    const GfxDevice &m_device;
    VmaAllocator m_memory_allocator;
    VkImageCreateInfo m_image_create_info;
    VkImageViewCreateInfo m_view_create_info;
    VkImageMemoryBarrier m_init_barrier;
    VkPipelineStageFlags m_init_dst_stage_mask;

    struct TextureAllocation {
        Texture texture;
        VkFence fence;
    };
    std::vector<TextureAllocation> m_pending;
    std::vector<Texture> m_free;
};

struct BufferSubAllocation {
    VkBuffer buffer;
    VkDeviceSize offset;
    VkDeviceSize size;
    void *mapped_ptr;

    void *ptr();
};

VkDeviceSize alignUp(VkDeviceSize x, VkDeviceSize alignment);
// Sum of aligned values
VkDeviceSize alignUp(std::span<const VkDeviceSize> values, VkDeviceSize alignment);

struct LinearAllocator {
    VkDeviceSize m_min_alignment = 0;
    VkBuffer m_buffer = VK_NULL_HANDLE;
    VkDeviceSize m_start = 0;
    VkDeviceSize m_end = 0;
    VkDeviceSize m_cur = 0;
    void *m_mapped_ptr = nullptr;

    LinearAllocator() = default;
    LinearAllocator(VkBuffer buffer, VkDeviceSize start_offset, VkDeviceSize cap,
                    VkDeviceSize min_alignment, void *mapped_ptr = nullptr)
        : m_min_alignment(min_alignment), m_buffer(buffer), m_start(start_offset),
          m_end(start_offset + cap), m_cur(start_offset), m_mapped_ptr(mapped_ptr) {}
    LinearAllocator(const LinearAllocator &) = delete;
    LinearAllocator &operator=(const LinearAllocator &) = delete;
    LinearAllocator(LinearAllocator &&) = default;
    LinearAllocator &operator=(LinearAllocator &&) = default;

    BufferSubAllocation allocateBytes(VkDeviceSize size, VkDeviceSize alignment = 0);
    template <typename T> BufferSubAllocation allocate(VkDeviceSize count = 1) {
        return allocateBytes(count * sizeof(T), alignof(T));
    }
    inline size_t capacity() const { return m_end - m_start; }
    void reset();
};

} // namespace brtoy
