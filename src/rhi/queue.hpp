#pragma once

#include "vk_types.hpp"
#include "device.hpp"

namespace RHI {

    class Queue
    {
    public:
        Queue(const std::shared_ptr<Device>& device, u32 queue_index);
        ~Queue();

        Queue(const Queue&) = delete;
        Queue& operator=(const Queue&) = delete;

        [[nodiscard]] auto queue() const -> VkQueue { return m_queue; }
        [[nodiscard]] auto timeline() const -> VkSemaphore { return m_timeline; }
        [[nodiscard]] auto value() const -> u64 { return m_value; }

        auto submit(VkCommandBuffer cmd, const std::vector<VkSemaphoreSubmitInfo>& waits, std::vector<VkSemaphoreSubmitInfo>& signals, VkPipelineStageFlags2 stage = VK_PIPELINE_STAGE_2_ALL_COMMANDS_BIT) -> u64;
        auto wait_info(VkPipelineStageFlags2 stage = VK_PIPELINE_STAGE_2_ALL_COMMANDS_BIT) const -> VkSemaphoreSubmitInfo;

        auto sync(u64 value = 0, u64 limit = std::numeric_limits<u64>::max()) -> void;

    private:
        std::shared_ptr<Device> m_device;

        VkQueue m_queue { VK_NULL_HANDLE };
        VkSemaphore m_timeline { VK_NULL_HANDLE };
        u64 m_value { 0 };
    };

}
