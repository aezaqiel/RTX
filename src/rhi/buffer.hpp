#pragma once

#include "vk_types.hpp"
#include "device.hpp"

namespace RHI {

    class Buffer
    {
    public:
        Buffer(const std::shared_ptr<Device>& device, u64 size, VkBufferUsageFlags buffer_usage, VmaMemoryUsage memory_usage, VmaAllocationCreateFlags allocation_flags = 0);
        ~Buffer();

        Buffer(const Buffer&) = delete;
        Buffer& operator=(const Buffer&) = delete;

        [[nodiscard]] auto buffer() const -> VkBuffer { return m_buffer; }
        [[nodiscard]] auto size() const -> u64 { return m_size; }

        auto address() const -> u64;

        auto map() -> std::byte*;
        auto unmap() -> void;

        auto write(const void* data, u64 size, u64 offset = 0) -> void;

        auto stage(VkCommandBuffer cmd, Buffer& staging, VkPipelineStageFlags2 stage = VK_PIPELINE_STAGE_2_TRANSFER_BIT, VkAccessFlags2 access = VK_ACCESS_2_TRANSFER_WRITE_BIT) -> void;

        static auto create_staged(const std::shared_ptr<Device>& device, VkCommandBuffer cmd, const void* data, u64 size, VkBufferUsageFlags usage, std::vector<std::unique_ptr<Buffer>>& stagings) -> std::unique_ptr<Buffer>;

        static auto copy(VkCommandBuffer cmd, Buffer& src, Buffer& dst,
            VkPipelineStageFlags2 src_stage,
            VkPipelineStageFlags2 dst_stage,
            VkAccessFlags2 src_access,
            VkAccessFlags2 dst_access
        ) -> void;

    private:
        std::shared_ptr<Device> m_device;

        VkBuffer m_buffer { VK_NULL_HANDLE };

        VmaAllocation m_allocation { VK_NULL_HANDLE };
        VmaAllocationInfo m_info;

        u64 m_size { 0 };
        bool m_mapped { false };
    };

}
