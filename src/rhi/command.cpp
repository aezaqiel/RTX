#include "command.hpp"

namespace RHI {

    Command::Command(const std::shared_ptr<Device>& device, u32 queue_index, usize frames_in_flight)
        : m_device(device), m_frames_in_flight(frames_in_flight)
    {
        m_pools.resize(frames_in_flight);
        m_buffers.resize(frames_in_flight);

        VkCommandPoolCreateInfo pool_info {
            .sType = VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO,
            .pNext = nullptr,
            .flags = VK_COMMAND_POOL_CREATE_TRANSIENT_BIT,
            .queueFamilyIndex = queue_index
        };

        VkCommandBufferAllocateInfo buffer_allocation {
            .sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO,
            .pNext = nullptr,
            .commandPool = VK_NULL_HANDLE,
            .level = VK_COMMAND_BUFFER_LEVEL_PRIMARY,
            .commandBufferCount = 1
        };

        for (usize i = 0; i < frames_in_flight; ++i) {
            VK_CHECK(vkCreateCommandPool(device->device(), &pool_info, nullptr, &m_pools[i]));

            buffer_allocation.commandPool = m_pools[i];
            VK_CHECK(vkAllocateCommandBuffers(device->device(), &buffer_allocation, &m_buffers[i]));
        }
    }

    Command::~Command()
    {
        for (auto& pool : m_pools) {
            vkDestroyCommandPool(m_device->device(), pool, nullptr);
        }
    }

    auto Command::begin(VkCommandPoolResetFlags flags) -> VkCommandBuffer
    {
        m_frame_index = (m_frame_index + 1) % m_frames_in_flight;

        vkResetCommandPool(m_device->device(), m_pools[m_frame_index], flags);

        constexpr VkCommandBufferBeginInfo begin_info {
            .sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO,
            .pNext = nullptr,
            .flags = 0,
            .pInheritanceInfo = nullptr
        };

        VK_CHECK(vkBeginCommandBuffer(m_buffers[m_frame_index], &begin_info));

        return m_buffers[m_frame_index];
    }

    auto Command::end() -> void
    {
        VK_CHECK(vkEndCommandBuffer(m_buffers[m_frame_index]));
    }

}
