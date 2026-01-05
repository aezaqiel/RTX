#pragma once

#include "vk_types.hpp"
#include "context.hpp"

namespace RHI {

    struct QueueFamilyIndices
    {
        u32 graphics { std::numeric_limits<u32>::max() };
        u32 compute  { std::numeric_limits<u32>::max() };
        u32 transfer { std::numeric_limits<u32>::max() };
    };

    class Device
    {
    public:
        Device(const std::shared_ptr<Context>& context);
        ~Device();

        Device(const Device&) = delete;
        Device& operator=(const Device&) = delete;

        [[nodiscard]] auto physical()  const -> VkPhysicalDevice { return m_physical_device; }
        [[nodiscard]] auto device()    const -> VkDevice { return m_device; }
        [[nodiscard]] auto allocator() const -> VmaAllocator { return m_allocator; }

        [[nodiscard]] auto graphics_index() const -> u32 { return m_queue_indices.graphics; }
        [[nodiscard]] auto compute_index()  const -> u32 { return m_queue_indices.compute;  }
        [[nodiscard]] auto transfer_index() const -> u32 { return m_queue_indices.transfer; }

        [[nodiscard]] auto props()    const -> VkPhysicalDeviceProperties { return m_props.properties; }
        [[nodiscard]] auto as_props() const -> VkPhysicalDeviceAccelerationStructurePropertiesKHR { return m_as_props; }
        [[nodiscard]] auto rt_props() const -> VkPhysicalDeviceRayTracingPipelinePropertiesKHR { return m_rt_props; }

        auto wait_idle() const -> void;

    private:
        std::shared_ptr<Context> m_context;

        VkPhysicalDevice m_physical_device { VK_NULL_HANDLE };
        VkDevice m_device { VK_NULL_HANDLE };
        VmaAllocator m_allocator { VK_NULL_HANDLE };

        QueueFamilyIndices m_queue_indices;

        VkPhysicalDeviceProperties2 m_props;
        VkPhysicalDeviceAccelerationStructurePropertiesKHR m_as_props;
        VkPhysicalDeviceRayTracingPipelinePropertiesKHR m_rt_props;
    };

}
