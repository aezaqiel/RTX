#include "acceleration_structure.hpp"

namespace RHI {

    AccelerationStructure::AccelerationStructure(const std::shared_ptr<Device>& device)
        : m_device(device)
    {
    }

    AccelerationStructure::~AccelerationStructure()
    {
        vkDestroyAccelerationStructureKHR(m_device->device(), m_as, nullptr);
    }

    auto AccelerationStructure::address() const -> VkDeviceAddress
    {
        if (!m_as) return 0;

        VkAccelerationStructureDeviceAddressInfoKHR info {
            .sType = VK_STRUCTURE_TYPE_ACCELERATION_STRUCTURE_DEVICE_ADDRESS_INFO_KHR,
            .pNext = nullptr,
            .accelerationStructure = m_as
        };

        return vkGetAccelerationStructureDeviceAddressKHR(m_device->device(), &info);
    }

    auto AccelerationStructure::build(VkCommandBuffer cmd) -> void
    {
        u64 scratch_size = vkutils::align_up(m_size.buildScratchSize, m_device->as_props().minAccelerationStructureScratchOffsetAlignment);
        if (!m_scratch || m_scratch->size() < scratch_size) {
            m_scratch = std::make_unique<Buffer>(m_device, scratch_size, VK_BUFFER_USAGE_STORAGE_BUFFER_BIT, VMA_MEMORY_USAGE_AUTO_PREFER_DEVICE);
        }

        m_build_info.dstAccelerationStructure = m_as;
        m_build_info.scratchData.deviceAddress = m_scratch->address();

        const VkAccelerationStructureBuildRangeInfoKHR* p_range_info = m_build_ranges.data();

        vkCmdBuildAccelerationStructuresKHR(cmd, 1, &m_build_info, &p_range_info);

        VkMemoryBarrier2 barrier {
            .sType = VK_STRUCTURE_TYPE_MEMORY_BARRIER_2,
            .pNext = nullptr,
            .srcStageMask = VK_PIPELINE_STAGE_2_ACCELERATION_STRUCTURE_BUILD_BIT_KHR,
            .srcAccessMask = VK_ACCESS_2_ACCELERATION_STRUCTURE_WRITE_BIT_KHR,
            .dstStageMask = VK_PIPELINE_STAGE_2_ACCELERATION_STRUCTURE_BUILD_BIT_KHR,
            .dstAccessMask = VK_ACCESS_2_ACCELERATION_STRUCTURE_READ_BIT_KHR
        };

        VkDependencyInfo dependency {
            .sType = VK_STRUCTURE_TYPE_DEPENDENCY_INFO,
            .pNext = nullptr,
            .dependencyFlags = 0,
            .memoryBarrierCount = 1,
            .pMemoryBarriers = &barrier,
            .bufferMemoryBarrierCount = 0,
            .pBufferMemoryBarriers = nullptr,
            .imageMemoryBarrierCount = 0,
            .pImageMemoryBarriers = nullptr
        };

        vkCmdPipelineBarrier2(cmd, &dependency);
    }

    auto AccelerationStructure::compact(VkCommandBuffer cmd) -> void
    {
    }

    BLAS::BLAS(const std::shared_ptr<Device>& device, const std::span<Geometry>& geometries)
        : AccelerationStructure(device)
    {
        m_geometries.reserve(geometries.size());
        m_build_ranges.reserve(geometries.size());
        m_primitive_counts.reserve(geometries.size());

        for (const auto& geometry : geometries) {
            VkAccelerationStructureGeometryTrianglesDataKHR triangles {
                .sType = VK_STRUCTURE_TYPE_ACCELERATION_STRUCTURE_GEOMETRY_TRIANGLES_DATA_KHR,
                .pNext = nullptr,
                .vertexFormat = geometry.vertices.format,
                .vertexData = { .deviceAddress = geometry.vertices.buffer->address() },
                .vertexStride = geometry.vertices.stride,
                .maxVertex = geometry.vertices.count,
                .indexType = VK_INDEX_TYPE_UINT32,
                .indexData = { .deviceAddress = geometry.indices.buffer->address() },
                .transformData = {}
            };

            m_geometries.push_back(VkAccelerationStructureGeometryKHR {
                .sType = VK_STRUCTURE_TYPE_ACCELERATION_STRUCTURE_GEOMETRY_KHR,
                .pNext = nullptr,
                .geometryType = VK_GEOMETRY_TYPE_TRIANGLES_KHR,
                .geometry = { .triangles = triangles },
                .flags = geometry.opaque ? VK_GEOMETRY_OPAQUE_BIT_KHR : 0u
            });

            m_build_ranges.push_back(VkAccelerationStructureBuildRangeInfoKHR {
                .primitiveCount = geometry.indices.count / 3,
                .primitiveOffset = 0,
                .firstVertex = 0,
                .transformOffset = 0
            });

            m_primitive_counts.push_back(m_build_ranges.back().primitiveCount);
        }

        m_build_info  = VkAccelerationStructureBuildGeometryInfoKHR {
            .sType = VK_STRUCTURE_TYPE_ACCELERATION_STRUCTURE_BUILD_GEOMETRY_INFO_KHR,
            .pNext = nullptr,
            .type = VK_ACCELERATION_STRUCTURE_TYPE_BOTTOM_LEVEL_KHR,
            .flags = VK_BUILD_ACCELERATION_STRUCTURE_PREFER_FAST_TRACE_BIT_KHR | VK_BUILD_ACCELERATION_STRUCTURE_ALLOW_COMPACTION_BIT_KHR,
            .mode = VK_BUILD_ACCELERATION_STRUCTURE_MODE_BUILD_KHR,
            .srcAccelerationStructure = VK_NULL_HANDLE,
            .dstAccelerationStructure = VK_NULL_HANDLE,
            .geometryCount = static_cast<u32>(m_geometries.size()),
            .pGeometries = m_geometries.data(),
            .ppGeometries = nullptr,
            .scratchData = {}
        };

        m_size = VkAccelerationStructureBuildSizesInfoKHR {
            .sType = VK_STRUCTURE_TYPE_ACCELERATION_STRUCTURE_BUILD_SIZES_INFO_KHR,
            .pNext = nullptr
        };

        vkGetAccelerationStructureBuildSizesKHR(device->device(), VK_ACCELERATION_STRUCTURE_BUILD_TYPE_DEVICE_KHR, &m_build_info, m_primitive_counts.data(), &m_size);
        std::println("blas size: {}", m_size.accelerationStructureSize);

        m_buffer = std::make_unique<Buffer>(device, m_size.accelerationStructureSize, VK_BUFFER_USAGE_ACCELERATION_STRUCTURE_STORAGE_BIT_KHR, VMA_MEMORY_USAGE_AUTO_PREFER_DEVICE);

        VkAccelerationStructureCreateInfoKHR as_info {
            .sType = VK_STRUCTURE_TYPE_ACCELERATION_STRUCTURE_CREATE_INFO_KHR,
            .pNext = nullptr,
            .createFlags = 0,
            .buffer = m_buffer->buffer(),
            .offset = 0,
            .size = m_buffer->size(),
            .type = VK_ACCELERATION_STRUCTURE_TYPE_BOTTOM_LEVEL_KHR,
            .deviceAddress = 0
        };

        VK_CHECK(vkCreateAccelerationStructureKHR(device->device(), &as_info, nullptr, &m_as));
    }

    TLAS::TLAS(const std::shared_ptr<Device>& device, const std::span<Instance>& instances)
        : AccelerationStructure(device)
    {
        std::vector<VkAccelerationStructureInstanceKHR> vk_instances;
        vk_instances.reserve(instances.size());

        for (const auto& instance : instances) {
            vk_instances.push_back(VkAccelerationStructureInstanceKHR {
                .transform = vkutils::glm_to_vkmatrix(instance.transform),
                .instanceCustomIndex = instance.instance_index,
                .mask = instance.mask,
                .instanceShaderBindingTableRecordOffset = instance.sbt_offset,
                .flags = instance.flags,
                .accelerationStructureReference = instance.blas->address()
            });
        }

        u64 instance_buffer_size = vk_instances.size() * sizeof(VkAccelerationStructureInstanceKHR);
        m_instances = std::make_unique<Buffer>(device, instance_buffer_size, VK_BUFFER_USAGE_ACCELERATION_STRUCTURE_BUILD_INPUT_READ_ONLY_BIT_KHR, VMA_MEMORY_USAGE_CPU_TO_GPU);
        m_instances->write(vk_instances.data(), instance_buffer_size);

        VkAccelerationStructureGeometryInstancesDataKHR instance_data {
            .sType = VK_STRUCTURE_TYPE_ACCELERATION_STRUCTURE_GEOMETRY_INSTANCES_DATA_KHR,
            .pNext = nullptr,
            .arrayOfPointers = VK_FALSE,
            .data = { .deviceAddress = m_instances->address() }
        };

        m_geometries.push_back(VkAccelerationStructureGeometryKHR {
            .sType = VK_STRUCTURE_TYPE_ACCELERATION_STRUCTURE_GEOMETRY_KHR,
            .pNext = nullptr,
            .geometryType = VK_GEOMETRY_TYPE_INSTANCES_KHR,
            .geometry = { .instances = instance_data },
            .flags = 0
        });

        m_primitive_counts.push_back(static_cast<u32>(instances.size()));
        m_build_ranges.push_back(VkAccelerationStructureBuildRangeInfoKHR {
            .primitiveCount = m_primitive_counts.back(),
            .primitiveOffset = 0,
            .firstVertex = 0,
            .transformOffset = 0
        });

        m_build_info = VkAccelerationStructureBuildGeometryInfoKHR {
            .sType = VK_STRUCTURE_TYPE_ACCELERATION_STRUCTURE_BUILD_GEOMETRY_INFO_KHR,
            .pNext = nullptr,
            .type = VK_ACCELERATION_STRUCTURE_TYPE_TOP_LEVEL_KHR,
            .flags = VK_BUILD_ACCELERATION_STRUCTURE_PREFER_FAST_TRACE_BIT_KHR,
            .mode = VK_BUILD_ACCELERATION_STRUCTURE_MODE_BUILD_KHR,
            .srcAccelerationStructure = VK_NULL_HANDLE,
            .dstAccelerationStructure = VK_NULL_HANDLE,
            .geometryCount = static_cast<u32>(m_geometries.size()),
            .pGeometries = m_geometries.data(),
            .ppGeometries = nullptr,
            .scratchData = {}
        };

        m_size = VkAccelerationStructureBuildSizesInfoKHR {
            .sType = VK_STRUCTURE_TYPE_ACCELERATION_STRUCTURE_BUILD_SIZES_INFO_KHR,
            .pNext = nullptr
        };

        vkGetAccelerationStructureBuildSizesKHR(device->device(), VK_ACCELERATION_STRUCTURE_BUILD_TYPE_DEVICE_KHR, &m_build_info, m_primitive_counts.data(), &m_size);
        std::println("tlas size: {}", m_size.accelerationStructureSize);

        m_buffer = std::make_unique<Buffer>(device, m_size.accelerationStructureSize, VK_BUFFER_USAGE_ACCELERATION_STRUCTURE_STORAGE_BIT_KHR, VMA_MEMORY_USAGE_AUTO_PREFER_DEVICE);

        VkAccelerationStructureCreateInfoKHR as_info {
            .sType = VK_STRUCTURE_TYPE_ACCELERATION_STRUCTURE_CREATE_INFO_KHR,
            .pNext = nullptr,
            .createFlags = 0,
            .buffer = m_buffer->buffer(),
            .offset = 0,
            .size = m_size.accelerationStructureSize,
            .type = VK_ACCELERATION_STRUCTURE_TYPE_TOP_LEVEL_KHR,
            .deviceAddress = 0
        };

        VK_CHECK(vkCreateAccelerationStructureKHR(device->device(), &as_info, nullptr, &m_as));
    }

}
