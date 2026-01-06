#include "acceleration_structure.hpp"

namespace RHI {

    AccelerationStructure::AccelerationStructure(const std::shared_ptr<Device>& device, VkAccelerationStructureTypeKHR type, u64 size)
        : m_device(device)
    {
        m_buffer = std::make_unique<Buffer>(device, size, VK_BUFFER_USAGE_ACCELERATION_STRUCTURE_STORAGE_BIT_KHR, VMA_MEMORY_USAGE_AUTO_PREFER_DEVICE);

        VkAccelerationStructureCreateInfoKHR create_info {
            .sType = VK_STRUCTURE_TYPE_ACCELERATION_STRUCTURE_CREATE_INFO_KHR,
            .pNext = nullptr,
            .createFlags = 0,
            .buffer = m_buffer->buffer(),
            .offset = 0,
            .size = size,
            .type = type,
            .deviceAddress = 0
        };

        VK_CHECK(vkCreateAccelerationStructureKHR(device->device(), &create_info, nullptr, &m_as));

        VkAccelerationStructureDeviceAddressInfoKHR address_info {
            .sType = VK_STRUCTURE_TYPE_ACCELERATION_STRUCTURE_DEVICE_ADDRESS_INFO_KHR,
            .pNext = nullptr,
            .accelerationStructure = m_as
        };

        m_address = vkGetAccelerationStructureDeviceAddressKHR(device->device(), &address_info);
    }

    AccelerationStructure::~AccelerationStructure()
    {
        vkDestroyAccelerationStructureKHR(m_device->device(), m_as, nullptr);
    }

    BLAS::BLAS(const std::shared_ptr<Device>& device, u64 size)
        : AccelerationStructure(device, VK_ACCELERATION_STRUCTURE_TYPE_BOTTOM_LEVEL_KHR, size)
    {
    }

    auto BLAS::Input::add_geometry(const Buffer& vertex_buffer, u32 vertex_count, u32 vertex_stride, const Buffer& index_buffer, u32 index_count, bool opaque) -> void
    {
        geometries.push_back(VkAccelerationStructureGeometryKHR {
            .sType = VK_STRUCTURE_TYPE_ACCELERATION_STRUCTURE_GEOMETRY_KHR,
            .pNext = nullptr,
            .geometryType = VK_GEOMETRY_TYPE_TRIANGLES_KHR,
            .geometry = {
                .triangles = {
                    .sType = VK_STRUCTURE_TYPE_ACCELERATION_STRUCTURE_GEOMETRY_TRIANGLES_DATA_KHR,
                    .pNext = nullptr,
                    .vertexFormat = VK_FORMAT_R32G32B32_SFLOAT,
                    .vertexData = { .deviceAddress = vertex_buffer.address() },
                    .vertexStride = vertex_stride,
                    .maxVertex = vertex_count,
                    .indexType = VK_INDEX_TYPE_UINT32,
                    .indexData = { .deviceAddress = index_buffer.address() },
                    .transformData = {}
                }
            },
            .flags = 0
        });

        ranges.push_back(VkAccelerationStructureBuildRangeInfoKHR {
            .primitiveCount = index_count / 3,
            .primitiveOffset = 0,
            .firstVertex = 0,
            .transformOffset = 0
        });
    }

    TLAS::TLAS(const std::shared_ptr<Device>& device, u64 size, std::unique_ptr<Buffer>&& instances)
        : AccelerationStructure(device, VK_ACCELERATION_STRUCTURE_TYPE_TOP_LEVEL_KHR, size), m_instances(std::move(instances))
    {
    }

    AccelerationStructureBuilder::AccelerationStructureBuilder(const std::shared_ptr<Device>& device)
        : m_device(device)
    {
    }

    auto AccelerationStructureBuilder::build_blas(VkCommandBuffer cmd, const std::vector<BLAS::Input>& inputs) -> std::vector<std::unique_ptr<BLAS>>
    {
        std::vector<std::unique_ptr<BLAS>> blases;
        std::vector<VkAccelerationStructureBuildGeometryInfoKHR> build_infos;
        build_infos.reserve(inputs.size());

        std::vector<const VkAccelerationStructureBuildRangeInfoKHR*> range_ptrs;

        // u64 max_scratch = 0;
        u64 total_scratch = 0;
        std::vector<u64> scratch_offsets;

        usize count = 0;
        for (const auto& input : inputs) {
            build_infos.push_back(VkAccelerationStructureBuildGeometryInfoKHR {
                .sType = VK_STRUCTURE_TYPE_ACCELERATION_STRUCTURE_BUILD_GEOMETRY_INFO_KHR,
                .pNext = nullptr,
                .type = VK_ACCELERATION_STRUCTURE_TYPE_BOTTOM_LEVEL_KHR,
                .flags = input.flags,
                .mode = VK_BUILD_ACCELERATION_STRUCTURE_MODE_BUILD_KHR,
                .srcAccelerationStructure = VK_NULL_HANDLE,
                .dstAccelerationStructure = VK_NULL_HANDLE,
                .geometryCount = static_cast<u32>(input.geometries.size()),
                .pGeometries = input.geometries.data(),
                .ppGeometries = nullptr,
                .scratchData = {}
            });

            std::vector<u32> max_prims;
            max_prims.reserve(input.ranges.size());
            for (const auto& range : input.ranges) {
                max_prims.push_back(range.primitiveCount);
            }

            VkAccelerationStructureBuildSizesInfoKHR size_info {
                .sType = VK_STRUCTURE_TYPE_ACCELERATION_STRUCTURE_BUILD_SIZES_INFO_KHR,
                .pNext = nullptr
            };

            vkGetAccelerationStructureBuildSizesKHR(m_device->device(), VK_ACCELERATION_STRUCTURE_BUILD_TYPE_DEVICE_KHR, &build_infos.back(), max_prims.data(), &size_info);
            std::println("blas [{}] size: {}", count, size_info.accelerationStructureSize);

            scratch_offsets.push_back(total_scratch);
            total_scratch += vkutils::align_up(size_info.buildScratchSize, m_device->as_props().minAccelerationStructureScratchOffsetAlignment);

            blases.push_back(std::make_unique<BLAS>(m_device, size_info.accelerationStructureSize));

            build_infos.back().dstAccelerationStructure = blases.back()->as();
            range_ptrs.push_back(input.ranges.data());
        }

        VkDeviceAddress scratch_address = ensure_scratch(total_scratch);

        for (usize i = 0; i < build_infos.size(); ++i) {
            build_infos[i].scratchData.deviceAddress = scratch_address + scratch_offsets[i];

            const VkAccelerationStructureBuildRangeInfoKHR* ranges = range_ptrs[i];
            vkCmdBuildAccelerationStructuresKHR(cmd, 1, &build_infos[i], &ranges);
        }

        VkMemoryBarrier2 build_barrier {
            .sType = VK_STRUCTURE_TYPE_MEMORY_BARRIER_2,
            .pNext = nullptr,
            .srcStageMask = VK_PIPELINE_STAGE_2_ACCELERATION_STRUCTURE_BUILD_BIT_KHR,
            .srcAccessMask = VK_ACCESS_2_ACCELERATION_STRUCTURE_WRITE_BIT_KHR,
            .dstStageMask = VK_PIPELINE_STAGE_2_ACCELERATION_STRUCTURE_BUILD_BIT_KHR,
            .dstAccessMask = VK_ACCESS_2_ACCELERATION_STRUCTURE_READ_BIT_KHR
        };

        VkDependencyInfo build_dependency {
            .sType = VK_STRUCTURE_TYPE_DEPENDENCY_INFO,
            .pNext = nullptr,
            .dependencyFlags = 0,
            .memoryBarrierCount = 1,
            .pMemoryBarriers = &build_barrier,
            .bufferMemoryBarrierCount = 0,
            .pBufferMemoryBarriers = nullptr,
            .imageMemoryBarrierCount = 0,
            .pImageMemoryBarriers = nullptr
        };

        vkCmdPipelineBarrier2(cmd, &build_dependency);

        return blases;
    }

    auto AccelerationStructureBuilder::build_tlas(VkCommandBuffer cmd, const TLAS::Input& input) -> std::unique_ptr<TLAS>
    {
        u64 instance_buffer_size = input.instances.size() * sizeof(VkAccelerationStructureInstanceKHR);

        // ideally we'd use DMA
        auto& stage = m_staging.emplace_back(std::make_unique<Buffer>(m_device, instance_buffer_size, VK_BUFFER_USAGE_TRANSFER_SRC_BIT, VMA_MEMORY_USAGE_CPU_TO_GPU, VMA_ALLOCATION_CREATE_HOST_ACCESS_SEQUENTIAL_WRITE_BIT));
        stage->write(input.instances.data(), instance_buffer_size);

        auto instance_buffer = std::make_unique<Buffer>(m_device, instance_buffer_size, VK_BUFFER_USAGE_ACCELERATION_STRUCTURE_BUILD_INPUT_READ_ONLY_BIT_KHR | VK_BUFFER_USAGE_TRANSFER_DST_BIT, VMA_MEMORY_USAGE_AUTO_PREFER_DEVICE);
        instance_buffer->stage(cmd, *stage,
            VK_PIPELINE_STAGE_2_ACCELERATION_STRUCTURE_BUILD_BIT_KHR,
            VK_ACCESS_2_ACCELERATION_STRUCTURE_READ_BIT_KHR
        );

        VkAccelerationStructureGeometryKHR geometry {
            .sType = VK_STRUCTURE_TYPE_ACCELERATION_STRUCTURE_GEOMETRY_KHR,
            .pNext = nullptr,
            .geometryType = VK_GEOMETRY_TYPE_INSTANCES_KHR,
            .geometry = {
                .instances = {
                    .sType = VK_STRUCTURE_TYPE_ACCELERATION_STRUCTURE_GEOMETRY_INSTANCES_DATA_KHR,
                    .pNext = nullptr,
                    .arrayOfPointers = VK_FALSE,
                    .data = { .deviceAddress = instance_buffer->address() }
                }
            },
            .flags = 0
        };

        VkAccelerationStructureBuildGeometryInfoKHR build_info {
            .sType = VK_STRUCTURE_TYPE_ACCELERATION_STRUCTURE_BUILD_GEOMETRY_INFO_KHR,
            .pNext = nullptr,
            .type = VK_ACCELERATION_STRUCTURE_TYPE_TOP_LEVEL_KHR,
            .flags = input.flags,
            .mode = VK_BUILD_ACCELERATION_STRUCTURE_MODE_BUILD_KHR,
            .srcAccelerationStructure = VK_NULL_HANDLE,
            .dstAccelerationStructure = VK_NULL_HANDLE,
            .geometryCount = 1,
            .pGeometries = &geometry,
            .ppGeometries = nullptr,
            .scratchData = {}
        };

        u32 max_prims = input.instances.size();

        VkAccelerationStructureBuildRangeInfoKHR range_info = {
            .primitiveCount = max_prims,
            .primitiveOffset = 0,
            .firstVertex = 0,
            .transformOffset = 0
        };

        VkAccelerationStructureBuildSizesInfoKHR size_info {
            .sType = VK_STRUCTURE_TYPE_ACCELERATION_STRUCTURE_BUILD_SIZES_INFO_KHR,
            .pNext = nullptr
        };

        vkGetAccelerationStructureBuildSizesKHR(m_device->device(), VK_ACCELERATION_STRUCTURE_BUILD_TYPE_DEVICE_KHR, &build_info, &max_prims, &size_info);
        std::println("tlas size: {}", size_info.accelerationStructureSize);

        auto tlas = std::make_unique<TLAS>(m_device, size_info.accelerationStructureSize, std::move(instance_buffer));
        VkDeviceAddress scratch_address = ensure_scratch(size_info.buildScratchSize);

        build_info.dstAccelerationStructure = tlas->as();
        build_info.scratchData.deviceAddress = scratch_address;

        const VkAccelerationStructureBuildRangeInfoKHR* p_range = &range_info;

        vkCmdBuildAccelerationStructuresKHR(cmd, 1, &build_info, &p_range);

        VkMemoryBarrier2 barrier {
            .sType = VK_STRUCTURE_TYPE_MEMORY_BARRIER_2,
            .pNext = nullptr,
            .srcStageMask = VK_PIPELINE_STAGE_2_ACCELERATION_STRUCTURE_BUILD_BIT_KHR,
            .srcAccessMask = VK_ACCESS_2_ACCELERATION_STRUCTURE_WRITE_BIT_KHR,
            .dstStageMask = VK_PIPELINE_STAGE_2_RAY_TRACING_SHADER_BIT_KHR,
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

        return std::move(tlas);
    }

    auto AccelerationStructureBuilder::ensure_scratch(u64 size) -> VkDeviceAddress
    {
        size = vkutils::align_up(size, m_device->as_props().minAccelerationStructureScratchOffsetAlignment);
        auto& scratch = m_scratch.emplace_back(std::make_unique<Buffer>(m_device, size, VK_BUFFER_USAGE_STORAGE_BUFFER_BIT, VMA_MEMORY_USAGE_AUTO_PREFER_DEVICE));

        return scratch->address();
    }

}
