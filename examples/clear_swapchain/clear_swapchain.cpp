#include <brtoy/container.h>
#include <brtoy/gfx.h>
#include <brtoy/gfx_swapchain.h>
#include <brtoy/gfx_utils.h>
#include <brtoy/platform.h>
#include <iomanip>
#include <iostream>
#include <sstream>
#include <vector>

namespace brtoy {

int runExample() {
    auto platform = Platform::init();
    if (!platform) {
        Platform::errorMessage("Could not initialize platform layer");
        return -1;
    }

    Window window = platform->createWindow("Example - Clear Swapchain");
    if (!window) {
        Platform::errorMessage("Could not create window.");
        return -1;
    }

    auto instance = GfxInstance::create("clear_swapchain", 0, GfxDebugFlag::ValidationEnable);
    if (!instance) {
        Platform::errorMessage("Could not create graphics instance");
        return -1;
    }

    auto device = GfxDevice::createDefault(*instance);
    if (!device) {
        Platform::errorMessage("Could not create graphics device");
        return -1;
    }

    WindowState window_state = platform->windowState(window);

    Swapchain swapchain(*instance, platform->appInstanceHandle(), window_state.native_handle,
                        device->m_physical_device, device->m_device);
    std::optional<Backbuffer> backbuffer;

    VkSemaphoreCreateInfo sem_create_info = {VK_STRUCTURE_TYPE_SEMAPHORE_CREATE_INFO, nullptr, 0};
    VkSemaphore begin_sem, end_sem;
    vkCreateSemaphore(device->m_device, &sem_create_info, nullptr, &begin_sem);
    vkCreateSemaphore(device->m_device, &sem_create_info, nullptr, &end_sem);

    CommandBufferPool cb_pool(*device);

    u64 prev_timestamp = platform->getTimestamp();
    Input input;
    while (platform->tick(input)) {
        u64 timestamp = platform->getTimestamp();
        u64 elapsed = timestamp - prev_timestamp;
        prev_timestamp = timestamp;
        double elapsed_millis =
            double(elapsed) / double(platform->getTimestampTicksPerSecond()) * 1000.0;

        window_state = platform->windowState(window);
        if (window_state.is_closing) {
            platform->requestQuit();
            continue;
        }

        cb_pool.sync();

        if (window_state.dim != swapchain.m_dim) {
            backbuffer.reset();
            swapchain.recreate(window_state.dim);
            backbuffer = Backbuffer::createFromSwapchain(device->m_device, swapchain);
        }

        if (!backbuffer)
            continue;

        std::stringstream ss;
        ss << "Example - Clear Swapchain (" << backbuffer->m_dim.x << "x" << backbuffer->m_dim.y
           << ")" << " " << std::setprecision(3) << elapsed_millis << " ms";
        platform->setWindowTitle(window, ss.str().c_str());

        u32 image_index;
        vkAcquireNextImageKHR(device->m_device, swapchain.m_swapchain, UINT64_MAX, begin_sem,
                              VK_NULL_HANDLE, &image_index);
        const Backbuffer::Buffer &current_buffer = backbuffer->m_buffers[image_index];

        vkWaitForFences(device->m_device, 1, &current_buffer.fence, VK_TRUE, UINT64_MAX);
        vkResetFences(device->m_device, 1, &current_buffer.fence);

        VkCommandBuffer cmd = cb_pool.acquire();
        VkCommandBufferBeginInfo cmd_begin_info = {
            .sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO,
            .pNext = nullptr,
            .flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT,
            .pInheritanceInfo = nullptr};
        vkBeginCommandBuffer(cmd, &cmd_begin_info);

        {
            VkImageMemoryBarrier image_barrier = {
                .sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER,
                .pNext = nullptr,
                .srcAccessMask = VK_ACCESS_NONE,
                .dstAccessMask = VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT,
                .oldLayout = VK_IMAGE_LAYOUT_UNDEFINED,
                .newLayout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL,
                .srcQueueFamilyIndex = device->m_queue_family_index,
                .dstQueueFamilyIndex = device->m_queue_family_index,
                .image = current_buffer.image,
                .subresourceRange =
                    {
                        .aspectMask = VK_IMAGE_ASPECT_COLOR_BIT,
                        .baseMipLevel = 0,
                        .levelCount = 1,
                        .baseArrayLayer = 0,
                        .layerCount = 1,
                    },
            };
            vkCmdPipelineBarrier(cmd, VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT,
                                 VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT, 0, 0, nullptr, 0,
                                 nullptr, 1, &image_barrier);
        }

        VkClearValue clear;
        clear.color.float32[0] = 0.1f;
        clear.color.float32[1] = 0.2f;
        clear.color.float32[2] = 0.3f;
        clear.color.float32[3] = 1.0f;
        VkRenderingAttachmentInfo color_attachment = {
            .sType = VK_STRUCTURE_TYPE_RENDERING_ATTACHMENT_INFO,
            .pNext = nullptr,
            .imageView = current_buffer.view,
            .imageLayout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL,
            .resolveMode = VK_RESOLVE_MODE_NONE,
            .resolveImageView = VK_NULL_HANDLE,
            .resolveImageLayout = VK_IMAGE_LAYOUT_UNDEFINED,
            .loadOp = VK_ATTACHMENT_LOAD_OP_CLEAR,
            .storeOp = VK_ATTACHMENT_STORE_OP_STORE,
            .clearValue = clear,
        };

        VkRenderingInfo rendering_info = {
            .sType = VK_STRUCTURE_TYPE_RENDERING_INFO,
            .pNext = nullptr,
            .flags = 0,
            .renderArea = {.offset = {0, 0}, .extent = {backbuffer->m_dim.x, backbuffer->m_dim.y}},
            .layerCount = 1,
            .viewMask = 0,
            .colorAttachmentCount = 1,
            .pColorAttachments = &color_attachment,
            .pDepthAttachment = nullptr,
            .pStencilAttachment = nullptr,
        };
        vkCmdBeginRendering(cmd, &rendering_info);
        vkCmdEndRendering(cmd);

        {
            VkImageMemoryBarrier image_barrier = {
                .sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER,
                .pNext = nullptr,
                .srcAccessMask = VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT,
                .dstAccessMask = VK_ACCESS_NONE,
                .oldLayout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL,
                .newLayout = VK_IMAGE_LAYOUT_PRESENT_SRC_KHR,
                .srcQueueFamilyIndex = device->m_queue_family_index,
                .dstQueueFamilyIndex = device->m_queue_family_index,
                .image = current_buffer.image,
                .subresourceRange =
                    {
                        .aspectMask = VK_IMAGE_ASPECT_COLOR_BIT,
                        .baseMipLevel = 0,
                        .levelCount = 1,
                        .baseArrayLayer = 0,
                        .layerCount = 1,
                    },
            };
            vkCmdPipelineBarrier(cmd, VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT,
                                 VK_PIPELINE_STAGE_BOTTOM_OF_PIPE_BIT, 0, 0, nullptr, 0, nullptr, 1,
                                 &image_barrier);
        }
        vkEndCommandBuffer(cmd);

        VkPipelineStageFlags sem_wait_stage = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT;
        VkSubmitInfo submit_info = {
            .sType = VK_STRUCTURE_TYPE_SUBMIT_INFO,
            .pNext = nullptr,
            .waitSemaphoreCount = 1,
            .pWaitSemaphores = &begin_sem,
            .pWaitDstStageMask = &sem_wait_stage,
            .commandBufferCount = 1,
            .pCommandBuffers = &cmd,
            .signalSemaphoreCount = 1,
            .pSignalSemaphores = &end_sem,
        };
        vkQueueSubmit(device->m_queue, 1, &submit_info, current_buffer.fence);
        cb_pool.release(cmd, current_buffer.fence);

        VkPresentInfoKHR present_info = {.sType = VK_STRUCTURE_TYPE_PRESENT_INFO_KHR,
                                         .pNext = nullptr,
                                         .waitSemaphoreCount = 1,
                                         .pWaitSemaphores = &end_sem,
                                         .swapchainCount = 1,
                                         .pSwapchains = &swapchain.m_swapchain,
                                         .pImageIndices = &image_index,
                                         .pResults = nullptr};
        vkQueuePresentKHR(device->m_queue, &present_info);
    }

    VkFence flush_fence;
    VkFenceCreateInfo fence_create_info = {VK_STRUCTURE_TYPE_FENCE_CREATE_INFO, nullptr, 0};
    vkCreateFence(device->m_device, &fence_create_info, nullptr, &flush_fence);
    VkSubmitInfo submit_info = {
        .sType = VK_STRUCTURE_TYPE_SUBMIT_INFO,
        .pNext = nullptr,
        .waitSemaphoreCount = 0,
        .pWaitSemaphores = nullptr,
        .pWaitDstStageMask = nullptr,
        .commandBufferCount = 0,
        .pCommandBuffers = nullptr,
        .signalSemaphoreCount = 0,
        .pSignalSemaphores = nullptr,
    };
    vkQueueSubmit(device->m_queue, 1, &submit_info, flush_fence);
    vkWaitForFences(device->m_device, 1, &flush_fence, VK_TRUE, UINT64_MAX);
    vkDestroyFence(device->m_device, flush_fence, nullptr);

    vkDestroySemaphore(device->m_device, begin_sem, nullptr);
    vkDestroySemaphore(device->m_device, end_sem, nullptr);
    return 0;
}

} // namespace brtoy

int main(int argc, char **argv) { return brtoy::runExample(); }
