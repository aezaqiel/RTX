#pragma once

#include "vk_types.hpp"

struct CommandContext
{
    VkCommandPool pool;
    VkCommandBuffer buffer;

    static auto create(VkDevice device, u32 queue_index) -> CommandContext
    {
        CommandContext context;

        VkCommandPoolCreateInfo pool_info {
            .sType = VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO,
            .pNext = nullptr,
            .flags = VK_COMMAND_POOL_CREATE_TRANSIENT_BIT,
            .queueFamilyIndex = queue_index
        };
        VK_CHECK(vkCreateCommandPool(device, &pool_info, nullptr, &context.pool));

        VkCommandBufferAllocateInfo buffer_allocation {
            .sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO,
            .pNext = nullptr,
            .commandPool = context.pool,
            .level = VK_COMMAND_BUFFER_LEVEL_PRIMARY,
            .commandBufferCount = 1
        };
        VK_CHECK(vkAllocateCommandBuffers(device, &buffer_allocation, &context.buffer));

        return context;
    }

    auto destroy(VkDevice device) -> void
    {
        vkResetCommandPool(device, pool, 0);
        vkDestroyCommandPool(device, pool, nullptr);
    }

    auto record(VkDevice device, std::function<void(VkCommandBuffer)>&& function) -> void
    {
        VK_CHECK(vkResetCommandPool(device, pool, 0));

        VkCommandBufferBeginInfo begin_info {
            .sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO,
            .pNext = nullptr,
            .flags = 0,
            .pInheritanceInfo = nullptr
        };

        VK_CHECK(vkBeginCommandBuffer(buffer, &begin_info));
        function(buffer);
        VK_CHECK(vkEndCommandBuffer(buffer));
    }

    // NOT OPTIMAL
    auto execute(VkDevice device, VkQueue queue, std::function<void(VkCommandBuffer)>&& function) -> void
    {
        record(device, std::move(function));

        VkSubmitInfo submit {
            .sType = VK_STRUCTURE_TYPE_SUBMIT_INFO,
            .pNext = nullptr,
            .waitSemaphoreCount = 0,
            .pWaitSemaphores = nullptr,
            .pWaitDstStageMask = nullptr,
            .commandBufferCount = 1,
            .pCommandBuffers = &buffer,
            .signalSemaphoreCount = 0,
            .pSignalSemaphores = nullptr
        };

        vkQueueSubmit(queue, 1, &submit, VK_NULL_HANDLE);
        vkQueueWaitIdle(queue);
    }
};