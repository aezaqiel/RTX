#pragma once

#include "vk_types.hpp"
#include "command.hpp"

struct QueueSync
{
    VkQueue queue;
    VkSemaphore timeline;
    u64 value { 0 };

    static auto create(VkDevice device, VkQueue queue) -> QueueSync
    {
        QueueSync sync;
        sync.queue = queue;
        sync.value = 0;

        VkSemaphoreTypeCreateInfo type_info {
            .sType = VK_STRUCTURE_TYPE_SEMAPHORE_TYPE_CREATE_INFO,
            .pNext = nullptr,
            .semaphoreType = VK_SEMAPHORE_TYPE_TIMELINE,
            .initialValue = sync.value
        };

        VkSemaphoreCreateInfo info {
            .sType = VK_STRUCTURE_TYPE_SEMAPHORE_CREATE_INFO,
            .pNext = &type_info,
            .flags = 0
        };

        VK_CHECK(vkCreateSemaphore(device, &info, nullptr, &sync.timeline));
        return sync;
    }

    auto destroy(VkDevice device) -> void
    {
        vkDestroySemaphore(device, timeline, nullptr);
    }

    auto submit(VkDevice device, VkCommandBuffer cmd, const std::vector<VkSemaphoreSubmitInfo>& waits, std::vector<VkSemaphoreSubmitInfo>& signals) -> u64
    {
        value++;

        signals.push_back(VkSemaphoreSubmitInfo {
            .sType = VK_STRUCTURE_TYPE_SEMAPHORE_SUBMIT_INFO,
            .pNext = nullptr,
            .semaphore = timeline,
            .value = value,
            .stageMask = VK_PIPELINE_STAGE_2_ALL_COMMANDS_BIT,
            .deviceIndex = 0
        });

        VkCommandBufferSubmitInfo cmd_info {
            .sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_SUBMIT_INFO,
            .pNext = nullptr,
            .commandBuffer = cmd,
            .deviceMask = 0
        };

        VkSubmitInfo2 submit {
            .sType = VK_STRUCTURE_TYPE_SUBMIT_INFO_2,
            .pNext = nullptr,
            .flags = 0,
            .waitSemaphoreInfoCount = static_cast<u32>(waits.size()),
            .pWaitSemaphoreInfos = waits.data(),
            .commandBufferInfoCount = 1,
            .pCommandBufferInfos = &cmd_info,
            .signalSemaphoreInfoCount = static_cast<u32>(signals.size()),
            .pSignalSemaphoreInfos = signals.data()
        };

        VK_CHECK(vkQueueSubmit2(queue, 1, &submit, VK_NULL_HANDLE));
        return value;
    }
};

struct FrameContext
{
    VkSemaphore image_available;
    VkSemaphore render_complete;

    CommandContext graphics;
    CommandContext compute;
    CommandContext transfer;

    static auto create(VkDevice device, u32 graphics, u32 compute, u32 transfer) -> FrameContext
    {
        FrameContext context;

        VkSemaphoreCreateInfo semaphore_info {
            .sType = VK_STRUCTURE_TYPE_SEMAPHORE_CREATE_INFO,
            .pNext = nullptr,
            .flags = 0
        };

        VK_CHECK(vkCreateSemaphore(device, &semaphore_info, nullptr, &context.image_available));
        VK_CHECK(vkCreateSemaphore(device, &semaphore_info, nullptr, &context.render_complete));

        context.graphics = CommandContext::create(device, graphics);
        context.compute = CommandContext::create(device, compute);
        context.transfer = CommandContext::create(device, transfer);

        return context;
    }

    auto destroy(VkDevice device) -> void
    {
        vkDestroySemaphore(device, image_available, nullptr);
        vkDestroySemaphore(device, render_complete, nullptr);

        transfer.destroy(device);
        compute.destroy(device);
        graphics.destroy(device);
    }
};
