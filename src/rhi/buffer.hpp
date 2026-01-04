#pragma once

#include "vk_types.hpp"

struct Buffer
{
    VkBuffer handle { VK_NULL_HANDLE };
    VmaAllocation allocation { VK_NULL_HANDLE };
    VkDeviceAddress address { 0 };
    VkDeviceSize size { 0 };

    static auto create(VmaAllocator allocator, VkDeviceSize size, VkBufferUsageFlags usage, VmaMemoryUsage memory_usage, VmaAllocationCreateFlags alloc_flags = 0) -> Buffer
    {
        Buffer buffer;
        buffer.size = size;

        usage |= VK_BUFFER_USAGE_SHADER_DEVICE_ADDRESS_BIT;

        VkBufferCreateInfo buffer_info {
            .sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO,
            .pNext = nullptr,
            .flags = 0,
            .size = size,
            .usage = usage,
            .sharingMode = VK_SHARING_MODE_EXCLUSIVE,
            .queueFamilyIndexCount = 0,
            .pQueueFamilyIndices = nullptr
        };

        VmaAllocationCreateInfo alloc_info {
            .flags = alloc_flags,
            .usage = memory_usage
        };

        VK_CHECK(vmaCreateBuffer(allocator, &buffer_info, &alloc_info, &buffer.handle, &buffer.allocation, nullptr));

        VkBufferDeviceAddressInfo address_info {
            .sType = VK_STRUCTURE_TYPE_BUFFER_DEVICE_ADDRESS_INFO,
            .pNext = nullptr,
            .buffer = buffer.handle
        };

        buffer.address = vkGetBufferDeviceAddress(volkGetLoadedDevice(), &address_info);

        return buffer;
    }

    static auto create_staging(VmaAllocator allocator, VkDeviceSize size, const void* data = nullptr) -> Buffer
    {
        Buffer buffer = create(
            allocator, 
            size, 
            VK_BUFFER_USAGE_TRANSFER_SRC_BIT, 
            VMA_MEMORY_USAGE_AUTO, 
            VMA_ALLOCATION_CREATE_HOST_ACCESS_SEQUENTIAL_WRITE_BIT | VMA_ALLOCATION_CREATE_MAPPED_BIT
        );

        if (data) {
            buffer.upload(allocator, data, size);
        }

        return buffer;
    }

    static auto create_aligned(VmaAllocator allocator, VkDeviceSize size, u32 alignment, VkBufferUsageFlags usage, VmaMemoryUsage memory_usage, VmaAllocationCreateFlags alloc_flags = 0) -> Buffer
    {
        VkDeviceSize aligned_size = (size + alignment - 1) & ~(alignment - 1);

        return create(
            allocator, 
            aligned_size, 
            usage,
            memory_usage,
            alloc_flags
        );
    }

    auto destroy(VmaAllocator allocator) -> void
    {
        if (handle) {
            vmaDestroyBuffer(allocator, handle, allocation);
            handle = VK_NULL_HANDLE;
            allocation = VK_NULL_HANDLE;
        }
    }

    auto upload(VmaAllocator allocator, const void* data, VkDeviceSize copy_size) -> void
    {
        void* mapped_data;
        VK_CHECK(vmaMapMemory(allocator, allocation, &mapped_data));
        std::memcpy(mapped_data, data, copy_size);
        vmaUnmapMemory(allocator, allocation);
    }

    static auto copy(VkCommandBuffer cmd, Buffer& src, Buffer& dst, VkDeviceSize size) -> void
    {
        VkBufferCopy region { .srcOffset = 0, .dstOffset = 0, .size = size };
        vkCmdCopyBuffer(cmd, src.handle, dst.handle, 1, &region);
        
        VkBufferMemoryBarrier2 barrier {
            .sType = VK_STRUCTURE_TYPE_BUFFER_MEMORY_BARRIER_2,
            .srcStageMask = VK_PIPELINE_STAGE_2_TRANSFER_BIT,
            .srcAccessMask = VK_ACCESS_2_TRANSFER_WRITE_BIT,
            .dstStageMask = VK_PIPELINE_STAGE_2_ACCELERATION_STRUCTURE_BUILD_BIT_KHR,
            .dstAccessMask = VK_ACCESS_2_ACCELERATION_STRUCTURE_READ_BIT_KHR, // Read-only input for build
            .srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED,
            .dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED,
            .buffer = dst.handle,
            .offset = 0,
            .size = size
        };

        VkDependencyInfo dep {
            .sType = VK_STRUCTURE_TYPE_DEPENDENCY_INFO,
            .bufferMemoryBarrierCount = 1,
            .pBufferMemoryBarriers = &barrier
        };

        vkCmdPipelineBarrier2(cmd, &dep);
    }

};
