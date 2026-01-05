#include "queue.hpp"

namespace RHI {

    Queue::Queue(const std::shared_ptr<Device>& device, u32 queue_index)
        : m_device(device)
    {
        vkGetDeviceQueue(device->device(), queue_index, 0, &m_queue);

        VkSemaphoreTypeCreateInfo semaphore_type {
            .sType = VK_STRUCTURE_TYPE_SEMAPHORE_TYPE_CREATE_INFO,
            .pNext = nullptr,
            .semaphoreType = VK_SEMAPHORE_TYPE_TIMELINE,
            .initialValue = m_value
        };

        VkSemaphoreCreateInfo semaphore_info {
            .sType = VK_STRUCTURE_TYPE_SEMAPHORE_CREATE_INFO,
            .pNext = &semaphore_type,
            .flags = 0
        };

        VK_CHECK(vkCreateSemaphore(device->device(), &semaphore_info, nullptr, &m_timeline));
    }

    Queue::~Queue()
    {
        vkDestroySemaphore(m_device->device(), m_timeline, nullptr);
    }

    auto Queue::submit(VkCommandBuffer cmd, const std::vector<VkSemaphoreSubmitInfo>& waits, std::vector<VkSemaphoreSubmitInfo>& signals, VkPipelineStageFlags2 stage) -> u64
    {
        m_value++;
        signals.push_back(wait_info(stage));

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

        VK_CHECK(vkQueueSubmit2(m_queue, 1, &submit, VK_NULL_HANDLE));

        return m_value;
    }

    auto Queue::wait_info(VkPipelineStageFlags2 stage) const -> VkSemaphoreSubmitInfo
    {
        return VkSemaphoreSubmitInfo {
            .sType = VK_STRUCTURE_TYPE_SEMAPHORE_SUBMIT_INFO,
            .pNext = nullptr,
            .semaphore = m_timeline,
            .value = m_value,
            .stageMask = stage,
            .deviceIndex = 0
        };
    }

    auto Queue::sync(u64 value, u64 limit) -> void
    {
        u64 wait_value = (value == 0) ? m_value : value;
        VkSemaphoreWaitInfo wait_info {
            .sType = VK_STRUCTURE_TYPE_SEMAPHORE_WAIT_INFO,
            .pNext = nullptr,
            .flags = 0,
            .semaphoreCount = 1,
            .pSemaphores = &m_timeline,
            .pValues = &wait_value
        };

        VK_CHECK(vkWaitSemaphores(m_device->device(), &wait_info, limit));
    }

}
