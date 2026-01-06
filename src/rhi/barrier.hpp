#pragma once

#include "vk_types.hpp"
#include "buffer.hpp"
#include "image.hpp"

namespace RHI {

    class BarrierBatch
    {
    public:
        BarrierBatch(VkCommandBuffer cmd);
        ~BarrierBatch() = default;

        auto buffer(
            const Buffer& buffer,
            VkPipelineStageFlags2 src_stage,
            VkAccessFlags2 src_access,
            VkPipelineStageFlags2 dst_stage,
            VkAccessFlags2 dst_access,
            u32 src_queue = VK_QUEUE_FAMILY_IGNORED,
            u32 dst_queue = VK_QUEUE_FAMILY_IGNORED
        ) -> BarrierBatch&;

        auto image(
            const Image& buffer,
            VkPipelineStageFlags2 src_stage,
            VkAccessFlags2 src_access,
            VkPipelineStageFlags2 dst_stage,
            VkAccessFlags2 dst_access,
            VkImageLayout old_layout,
            VkImageLayout new_layout
        ) -> BarrierBatch&;

        auto memory(
            VkPipelineStageFlags2 src_stage,
            VkAccessFlags2 src_access,
            VkPipelineStageFlags2 dst_stage,
            VkAccessFlags2 dst_access
        ) -> BarrierBatch&;

        auto insert() -> void;

    private:
        VkCommandBuffer m_cmd;

        std::vector<VkBufferMemoryBarrier2> m_buffers;
        std::vector<VkImageMemoryBarrier2> m_images;
        std::vector<VkMemoryBarrier2> m_memory;
    };

}
