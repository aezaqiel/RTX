#include "buffer.hpp"

namespace RHI {

    Buffer::Buffer(const std::shared_ptr<Device>& device, u64 size, VkBufferUsageFlags buffer_usage, VmaMemoryUsage memory_usage, VmaAllocationCreateFlags allocation_flags)
        : m_device(device), m_size(size)
    {
        buffer_usage |= VK_BUFFER_USAGE_SHADER_DEVICE_ADDRESS_BIT;

        VkBufferCreateInfo buffer_info {
            .sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO,
            .pNext = nullptr,
            .flags = 0,
            .size = size,
            .usage = buffer_usage,
            .sharingMode = VK_SHARING_MODE_EXCLUSIVE,
            .queueFamilyIndexCount = 0,
            .pQueueFamilyIndices = nullptr
        };

        VmaAllocationCreateInfo allocation_info {
            .flags = allocation_flags,
            .usage = memory_usage
        };

        VK_CHECK(vmaCreateBuffer(device->allocator(), &buffer_info, &allocation_info, &m_buffer, &m_allocation, &m_info));
    }

    Buffer::~Buffer()
    {
        if (m_mapped) unmap();
        vmaDestroyBuffer(m_device->allocator(), m_buffer, m_allocation);
    }

    auto Buffer::address() const -> u64
    {
        VkBufferDeviceAddressInfo address_info {
            .sType = VK_STRUCTURE_TYPE_BUFFER_DEVICE_ADDRESS_INFO,
            .pNext = nullptr,
            .buffer = m_buffer
        };

        return vkGetBufferDeviceAddress(m_device->device(), &address_info);
    }

    auto Buffer::map() -> std::byte*
    {
        if (m_mapped) {
            return static_cast<std::byte*>(m_info.pMappedData);
        }

        void* data;
        VK_CHECK(vmaMapMemory(m_device->allocator(), m_allocation, &data));
        m_mapped = true;

        return static_cast<std::byte*>(data);
    }

    auto Buffer::unmap() -> void
    {
        if (!m_mapped) return;

        vmaUnmapMemory(m_device->allocator(), m_allocation);
        m_mapped = false;
    }

    auto Buffer::write(const void* data, u64 size, u64 offset) -> void
    {
        std::byte* ptr = map();
        std::memcpy(ptr + offset, data, size);
        unmap();
    }

    auto Buffer::stage(VkCommandBuffer cmd, Buffer& staging, VkPipelineStageFlags2 stage, VkAccessFlags2 access) -> void
    {
        RHI::Buffer::copy(cmd, staging, *this,
            VK_PIPELINE_STAGE_2_TRANSFER_BIT, stage,
            VK_ACCESS_2_TRANSFER_READ_BIT, access
        );
    }

    auto Buffer::copy(VkCommandBuffer cmd, Buffer& src, Buffer& dst,
        VkPipelineStageFlags2 src_stage,
        VkPipelineStageFlags2 dst_stage,
        VkAccessFlags2 src_access,
        VkAccessFlags2 dst_access
    ) -> void
    {
        VkBufferCopy region { .srcOffset = 0, .dstOffset = 0, .size = dst.size() };
        vkCmdCopyBuffer(cmd, src.buffer(), dst.buffer(), 1, &region);

        VkBufferMemoryBarrier2 barrier {
            .sType = VK_STRUCTURE_TYPE_BUFFER_MEMORY_BARRIER_2,
            .srcStageMask = src_stage,
            .srcAccessMask = src_access,
            .dstStageMask = dst_stage,
            .dstAccessMask = dst_access,
            .srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED,
            .dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED,
            .buffer = dst.buffer(),
            .offset = 0,
            .size = dst.size()
        };

        VkDependencyInfo dependency {
            .sType = VK_STRUCTURE_TYPE_DEPENDENCY_INFO,
            .bufferMemoryBarrierCount = 1,
            .pBufferMemoryBarriers = &barrier
        };

        vkCmdPipelineBarrier2(cmd, &dependency);
    }

}
