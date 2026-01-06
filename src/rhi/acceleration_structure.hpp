#pragma once

#include <glm/glm.hpp>

#include "vk_types.hpp"
#include "device.hpp"
#include "buffer.hpp"

namespace RHI {

    class AccelerationStructure
    {
    public:
        virtual ~AccelerationStructure();

        [[nodiscard]] auto as() const -> VkAccelerationStructureKHR { return m_as; }
        [[nodiscard]] auto buffer() const -> const Buffer& { return *m_buffer; }
        [[nodiscard]] auto address() const -> const VkDeviceAddress { return m_address; }

    protected:
        AccelerationStructure(const std::shared_ptr<Device>& device, VkAccelerationStructureTypeKHR type, u64 size);

    private:
        std::shared_ptr<Device> m_device;

        std::unique_ptr<Buffer> m_buffer;

        VkAccelerationStructureKHR m_as { VK_NULL_HANDLE };
        VkDeviceAddress m_address { 0 };
    };

    class BLAS final : public AccelerationStructure
    {
        friend class AccelerationStructureBuilder;
    public:
        struct Input
        {
            std::vector<VkAccelerationStructureGeometryKHR> geometries;
            std::vector<VkAccelerationStructureBuildRangeInfoKHR> ranges;

            auto add_geometry(const Buffer& vertex_buffer, u32 vertex_count, u32 vertex_stride, const Buffer& index_buffer, u32 index_count, bool opaque = true) -> void;
        };

    public:
        BLAS(const std::shared_ptr<Device>& device, u64 size);
        virtual ~BLAS() = default;
    };

    class TLAS final : public AccelerationStructure
    {
        friend class AccelerationStructureBuilder;
    public:
        struct Input
        {
            std::vector<VkAccelerationStructureInstanceKHR> instances;
        };

    public:
        TLAS(const std::shared_ptr<Device>& device, u64 size, std::unique_ptr<Buffer>&& instances);
        virtual ~TLAS() = default;

        [[nodiscard]] auto instances() const -> const Buffer& { return *m_instances; }

    private:
        std::unique_ptr<Buffer> m_instances;
    };


    class AccelerationStructureBuilder
    {
    public:
        AccelerationStructureBuilder(const std::shared_ptr<Device>& device);
        ~AccelerationStructureBuilder();

        auto build_blas(VkCommandBuffer cmd, const std::vector<BLAS::Input>& inputs) -> std::vector<std::unique_ptr<BLAS>>;
        auto compact_blas(VkCommandBuffer cmd, const std::vector<std::unique_ptr<BLAS>>& blases) -> std::vector<std::unique_ptr<BLAS>>;

        auto build_tlas(VkCommandBuffer cmd, const TLAS::Input& input) -> std::unique_ptr<TLAS>;

        auto cleanup() -> void;

    private:
        auto create_scratch(u64 size) -> VkDeviceAddress;
        auto create_query(u32 count) -> const VkQueryPool&;

    private:
        std::shared_ptr<Device> m_device;

        std::vector<std::unique_ptr<Buffer>> m_scratch;
        std::vector<std::unique_ptr<Buffer>> m_staging;
        std::vector<VkQueryPool> m_query;
    };

}
