#pragma once

#include <glm/glm.hpp>

#include "vk_types.hpp"
#include "device.hpp"
#include "buffer.hpp"
#include "command.hpp"
#include "queue.hpp"

namespace RHI {

    class AccelerationStructure
    {
    public:
        virtual ~AccelerationStructure();

        AccelerationStructure(const AccelerationStructure&) = delete;
        AccelerationStructure& operator=(const AccelerationStructure&) = delete;

        [[nodiscard]] auto as() const -> VkAccelerationStructureKHR { return m_as; }
        [[nodiscard]] auto buffer() const -> const Buffer& { return *m_buffer; }

        auto address() const -> VkDeviceAddress;

        auto build(VkCommandBuffer cmd) -> void;
        auto compact(VkCommandBuffer cmd) -> void;

    protected:
        AccelerationStructure(const std::shared_ptr<Device>& device);

    protected:
        std::shared_ptr<Device> m_device;

        VkAccelerationStructureKHR m_as { VK_NULL_HANDLE };
        std::unique_ptr<Buffer> m_buffer;

        std::vector<VkAccelerationStructureGeometryKHR> m_geometries;
        std::vector<VkAccelerationStructureBuildRangeInfoKHR> m_build_ranges;
        std::vector<u32> m_primitive_counts;

        VkAccelerationStructureBuildSizesInfoKHR m_size;
        VkAccelerationStructureBuildGeometryInfoKHR m_build_info;

        std::unique_ptr<Buffer> m_scratch;
    };

    class BLAS final : public AccelerationStructure
    {
    public:
        struct Geometry
        {
            struct
            {
                Buffer* buffer { nullptr };
                u32 count { 0 };
                VkFormat format { VK_FORMAT_UNDEFINED };
                u32 stride { 0 };
            } vertices;

            struct
            {
                Buffer* buffer { nullptr };
                u32 count = 0;
            } indices;

            bool opaque { true };
        };

    public:
        BLAS(const std::shared_ptr<Device>& device, const std::span<Geometry>& geometries);
        virtual ~BLAS() = default;

        BLAS(const BLAS&) = delete;
        BLAS& operator=(const BLAS&) = delete;
    };

    class TLAS final : public AccelerationStructure
    {
    public:
        struct Instance
        {
            BLAS* blas { nullptr };
            glm::mat4 transform { 1.0f };
            u32 instance_index { 0 };
            u32 mask { 0xFF };
            u32 sbt_offset { 0 };
            VkGeometryInstanceFlagsKHR flags { VK_GEOMETRY_INSTANCE_TRIANGLE_FACING_CULL_DISABLE_BIT_KHR };
        };

    public:
        TLAS(const std::shared_ptr<Device>& device, const std::span<Instance>& instances);
        virtual ~TLAS() = default;

        TLAS(const TLAS&) = delete;
        TLAS& operator=(const TLAS&) = delete;

    private:
        std::unique_ptr<Buffer> m_instances;
    };

}
