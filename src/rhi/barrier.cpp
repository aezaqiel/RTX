#include "barrier.hpp"

namespace RHI {

    BarrierBatch::BarrierBatch(VkCommandBuffer cmd)
        : m_cmd(cmd)
    {
    }

    auto BarrierBatch::buffer(
        const Buffer& buffer,
        VkPipelineStageFlags2 src_stage,
        VkAccessFlags2 src_access,
        VkPipelineStageFlags2 dst_stage,
        VkAccessFlags2 dst_access,
        u32 src_queue,
        u32 dst_queue
    ) -> BarrierBatch&
    {
        m_buffers.push_back(VkBufferMemoryBarrier2 {
            .sType = VK_STRUCTURE_TYPE_BUFFER_MEMORY_BARRIER_2,
            .pNext = nullptr,
            .srcStageMask = src_stage,
            .srcAccessMask = src_access,
            .dstStageMask = dst_stage,
            .dstAccessMask = dst_access,
            .srcQueueFamilyIndex = src_queue,
            .dstQueueFamilyIndex = dst_queue,
            .buffer = buffer.buffer(),
            .offset = 0,
            .size = buffer.size()
        });

        return *this;
    }

    auto BarrierBatch::image(
        const Image& image,
        VkPipelineStageFlags2 src_stage,
        VkAccessFlags2 src_access,
        VkPipelineStageFlags2 dst_stage,
        VkAccessFlags2 dst_access,
        VkImageLayout old_layout,
        VkImageLayout new_layout
    ) -> BarrierBatch&
    {
        m_images.push_back(VkImageMemoryBarrier2 {
            .sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER_2,
            .pNext = nullptr,
            .srcStageMask = src_stage,
            .srcAccessMask = src_access,
            .dstStageMask = dst_stage,
            .dstAccessMask = dst_access,
            .oldLayout = old_layout,
            .newLayout = new_layout,
            .srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED,
            .dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED,
            .image = image.image(),
            .subresourceRange = {
                .aspectMask = image.aspect(),
                .baseMipLevel = 0,
                .levelCount = image.mips(),
                .baseArrayLayer = 0,
                .layerCount = image.layers()
            }
        });

        return *this;
    }

    auto BarrierBatch::memory(
        VkPipelineStageFlags2 src_stage,
        VkAccessFlags2 src_access,
        VkPipelineStageFlags2 dst_stage,
        VkAccessFlags2 dst_access
    ) -> BarrierBatch&
    {
        m_memory.push_back(VkMemoryBarrier2 {
            .sType = VK_STRUCTURE_TYPE_MEMORY_BARRIER_2,
            .pNext = nullptr,
            .srcStageMask = src_stage,
            .srcAccessMask = src_access,
            .dstStageMask = dst_stage,
            .dstAccessMask = dst_access
        });

        return *this;
    }

    auto BarrierBatch::insert() -> void
    {
        if (m_memory.empty() && m_buffers.empty() && m_images.empty()) return;

        VkDependencyInfo dependency {
            .sType = VK_STRUCTURE_TYPE_DEPENDENCY_INFO,
            .pNext = nullptr,
            .dependencyFlags = 0,
            .memoryBarrierCount = static_cast<u32>(m_memory.size()),
            .pMemoryBarriers = m_memory.data(),
            .bufferMemoryBarrierCount = static_cast<u32>(m_buffers.size()),
            .pBufferMemoryBarriers = m_buffers.data(),
            .imageMemoryBarrierCount = static_cast<u32>(m_images.size()),
            .pImageMemoryBarriers = m_images.data()
        };

        vkCmdPipelineBarrier2(m_cmd, &dependency);
    }

}
