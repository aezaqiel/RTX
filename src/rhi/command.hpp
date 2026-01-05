#pragma once

#include "vk_types.hpp"
#include "device.hpp"

namespace RHI {

    class Command
    {
    public:
        Command(const std::shared_ptr<Device>& device, u32 queue_index, usize frames_in_flight);
        ~Command();

        auto begin(VkCommandPoolResetFlags flags = 0) -> VkCommandBuffer;
        auto end() -> void;

    private:
        std::shared_ptr<Device> m_device;

        usize m_frames_in_flight { 0 };
        usize m_frame_index { 0 };

        std::vector<VkCommandPool> m_pools;
        std::vector<VkCommandBuffer> m_buffers;
    };

}
