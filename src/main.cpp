#include <GLFW/glfw3.h>
#include <GLFW/glfw3native.h>

#include <glm/glm.hpp>

#include "rhi/context.hpp"
#include "rhi/device.hpp"
#include "rhi/swapchain.hpp"
#include "rhi/queue.hpp"
#include "rhi/command.hpp"
#include "rhi/buffer.hpp"

#include "scene/loader.hpp"

namespace {

    bool g_running = true;
    constexpr usize FRAMES_IN_FLIGHT = 3;

    struct WindowData
    {
        u32 width;
        u32 height;
    };

}

auto main() -> i32
{
    glfwSetErrorCallback([](i32 code, const char* desc) {
        std::println(std::cerr, "glfw error ({}): {}", code, desc);
    });

    glfwInit();

    WindowData window_data;
    glfwWindowHint(GLFW_CLIENT_API, GLFW_NO_API);

    GLFWwindow* window = glfwCreateWindow(1280, 720, "RTX", nullptr, nullptr);
    {
        i32 w;
        i32 h;
        glfwGetFramebufferSize(window, &w, &h);

        window_data.width = static_cast<u32>(w);
        window_data.height = static_cast<u32>(h);

        std::println("created window \"{}\" ({}, {})", glfwGetWindowTitle(window), window_data.width, window_data.height);
    }

    glfwSetWindowUserPointer(window, &window_data);

    glfwSetWindowCloseCallback(window, [](GLFWwindow*) {
        g_running = false;
    });

    glfwSetFramebufferSizeCallback(window, [](GLFWwindow* window, i32 width, i32 height) {
        WindowData& data = *static_cast<WindowData*>(glfwGetWindowUserPointer(window));
        data.width = static_cast<u32>(width);
        data.height = static_cast<u32>(height);
    });

    auto context = std::make_shared<RHI::Context>(window);
    auto device = std::make_shared<RHI::Device>(context);

    auto swapchain = std::make_unique<RHI::Swapchain>(context, device, VkExtent2D { window_data.width, window_data.height });

    std::println("creating command contexts");

    auto graphics_queue = std::make_unique<RHI::Queue>(device, device->graphics_index());
    auto compute_queue  = std::make_unique<RHI::Queue>(device, device->compute_index());
    auto transfer_queue = std::make_unique<RHI::Queue>(device, device->transfer_index());

    auto graphics_command = std::make_unique<RHI::Command>(device, device->graphics_index(), FRAMES_IN_FLIGHT);
    auto compute_command = std::make_unique<RHI::Command>(device, device->compute_index(), FRAMES_IN_FLIGHT);
    auto transfer_command = std::make_unique<RHI::Command>(device, device->transfer_index(), FRAMES_IN_FLIGHT);

    std::unique_ptr<RHI::Buffer> blas_buffer;
    VkAccelerationStructureKHR blas;

    std::unique_ptr<RHI::Buffer> tlas_buffer;
    VkAccelerationStructureKHR tlas;

    { // acceleration structure
        std::string model_filename = "assets/sponza/sponza.obj";

        std::println("loading {}", model_filename);
        auto model = Loader::load_obj(model_filename);

        std::println("uploading model and building AS");

        usize vertex_size = model.mesh->positions.size() * sizeof(glm::vec3);
        usize index_size = model.mesh->indices.size() * sizeof(u32);

        RHI::Buffer vertex_staging(device, vertex_size,
            VK_BUFFER_USAGE_TRANSFER_SRC_BIT,
            VMA_MEMORY_USAGE_CPU_TO_GPU,
            VMA_ALLOCATION_CREATE_HOST_ACCESS_SEQUENTIAL_WRITE_BIT
        );

        RHI::Buffer vertex_buffer(device, vertex_size,
            VK_BUFFER_USAGE_ACCELERATION_STRUCTURE_BUILD_INPUT_READ_ONLY_BIT_KHR | VK_BUFFER_USAGE_TRANSFER_DST_BIT,
            VMA_MEMORY_USAGE_AUTO_PREFER_DEVICE
        );

        RHI::Buffer index_staging(device, index_size,
            VK_BUFFER_USAGE_TRANSFER_SRC_BIT,
            VMA_MEMORY_USAGE_CPU_TO_GPU,
            VMA_ALLOCATION_CREATE_HOST_ACCESS_SEQUENTIAL_WRITE_BIT
        );

        RHI::Buffer index_buffer(device, vertex_size,
            VK_BUFFER_USAGE_ACCELERATION_STRUCTURE_BUILD_INPUT_READ_ONLY_BIT_KHR | VK_BUFFER_USAGE_TRANSFER_DST_BIT | VK_BUFFER_USAGE_INDEX_BUFFER_BIT,
            VMA_MEMORY_USAGE_AUTO_PREFER_DEVICE
        );

        VkAccelerationStructureGeometryTrianglesDataKHR triangles {
            .sType = VK_STRUCTURE_TYPE_ACCELERATION_STRUCTURE_GEOMETRY_TRIANGLES_DATA_KHR,
            .pNext = nullptr,
            .vertexFormat = VK_FORMAT_R32G32B32_SFLOAT,
            .vertexData = { .deviceAddress = vertex_buffer.address() },
            .vertexStride = sizeof(glm::vec3),
            .maxVertex = static_cast<u32>(model.mesh->positions.size() - 1),
            .indexType = VK_INDEX_TYPE_UINT32,
            .indexData = { .deviceAddress = index_buffer.address() },
            .transformData = {}
        };

        VkAccelerationStructureGeometryKHR blas_geometry {
            .sType = VK_STRUCTURE_TYPE_ACCELERATION_STRUCTURE_GEOMETRY_KHR,
            .pNext = nullptr,
            .geometryType = VK_GEOMETRY_TYPE_TRIANGLES_KHR,
            .geometry = { .triangles = triangles },
            .flags = VK_GEOMETRY_OPAQUE_BIT_KHR
        };

        VkAccelerationStructureBuildGeometryInfoKHR blas_build_info {
            .sType = VK_STRUCTURE_TYPE_ACCELERATION_STRUCTURE_BUILD_GEOMETRY_INFO_KHR,
            .pNext = nullptr,
            .type = VK_ACCELERATION_STRUCTURE_TYPE_BOTTOM_LEVEL_KHR,
            .flags = VK_BUILD_ACCELERATION_STRUCTURE_PREFER_FAST_TRACE_BIT_KHR,
            .mode = VK_BUILD_ACCELERATION_STRUCTURE_MODE_BUILD_KHR,
            .srcAccelerationStructure = VK_NULL_HANDLE,
            .dstAccelerationStructure = VK_NULL_HANDLE,
            .geometryCount = 1,
            .pGeometries = &blas_geometry,
            .ppGeometries = nullptr,
            .scratchData = {}
        };

        u32 primitive_count = static_cast<u32>(model.mesh->indices.size() / 3);

        VkAccelerationStructureBuildSizesInfoKHR blas_size_info {
            .sType = VK_STRUCTURE_TYPE_ACCELERATION_STRUCTURE_BUILD_SIZES_INFO_KHR,
            .pNext = nullptr
        };

        vkGetAccelerationStructureBuildSizesKHR(device->device(), VK_ACCELERATION_STRUCTURE_BUILD_TYPE_DEVICE_KHR, &blas_build_info, &primitive_count, &blas_size_info);
        std::println("blas size: {}", blas_size_info.accelerationStructureSize);

        blas_buffer = std::make_unique<RHI::Buffer>(device, blas_size_info.accelerationStructureSize, VK_BUFFER_USAGE_ACCELERATION_STRUCTURE_STORAGE_BIT_KHR, VMA_MEMORY_USAGE_AUTO_PREFER_DEVICE);

        VkAccelerationStructureCreateInfoKHR blas_create_info {
            .sType = VK_STRUCTURE_TYPE_ACCELERATION_STRUCTURE_CREATE_INFO_KHR,
            .pNext = nullptr,
            .createFlags = 0,
            .buffer = blas_buffer->buffer(),
            .offset = 0,
            .size = blas_size_info.accelerationStructureSize,
            .type = VK_ACCELERATION_STRUCTURE_TYPE_BOTTOM_LEVEL_KHR,
            .deviceAddress = 0
        };

        VK_CHECK(vkCreateAccelerationStructureKHR(device->device(), &blas_create_info, nullptr, &blas));

        u64 blas_scratch_size = vkutils::align_up(blas_size_info.buildScratchSize, device->as_props().minAccelerationStructureScratchOffsetAlignment);
        RHI::Buffer blas_scratch(device, blas_scratch_size, VK_BUFFER_USAGE_STORAGE_BUFFER_BIT, VMA_MEMORY_USAGE_AUTO_PREFER_DEVICE);

        blas_build_info.dstAccelerationStructure = blas;
        blas_build_info.scratchData.deviceAddress = blas_scratch.address();

        VkAccelerationStructureBuildRangeInfoKHR blas_range_info {
            .primitiveCount = primitive_count,
            .primitiveOffset = 0,
            .firstVertex = 0,
            .transformOffset = 0
        };

        const VkAccelerationStructureBuildRangeInfoKHR* p_blas_range_info = &blas_range_info;

        VkAccelerationStructureDeviceAddressInfoKHR blas_address_info {
            .sType = VK_STRUCTURE_TYPE_ACCELERATION_STRUCTURE_DEVICE_ADDRESS_INFO_KHR,
            .pNext = nullptr,
            .accelerationStructure = blas
        };

        VkDeviceAddress blas_address = vkGetAccelerationStructureDeviceAddressKHR(device->device(), &blas_address_info);

        VkAccelerationStructureInstanceKHR tlas_instance {
            .transform = {
                .matrix = {
                    { 1.0f, 0.0f, 0.0f, 0.0f },
                    { 0.0f, 1.0f, 0.0f, 0.0f },
                    { 0.0f, 0.0f, 1.0f, 0.0f }
                }
            },
            .instanceCustomIndex = 0,
            .mask = 0xFF,
            .instanceShaderBindingTableRecordOffset = 0,
            .flags = VK_GEOMETRY_INSTANCE_TRIANGLE_FACING_CULL_DISABLE_BIT_KHR,
            .accelerationStructureReference = blas_address
        };

        RHI::Buffer instance_buffer(device, sizeof(VkAccelerationStructureInstanceKHR), VK_BUFFER_USAGE_ACCELERATION_STRUCTURE_BUILD_INPUT_READ_ONLY_BIT_KHR, VMA_MEMORY_USAGE_CPU_TO_GPU);
        instance_buffer.upload(&tlas_instance, sizeof(VkAccelerationStructureInstanceKHR));

        VkDeviceAddress instance_buffer_address = instance_buffer.address();

        VkAccelerationStructureGeometryInstancesDataKHR instances_data {
            .sType = VK_STRUCTURE_TYPE_ACCELERATION_STRUCTURE_GEOMETRY_INSTANCES_DATA_KHR,
            .pNext = nullptr,
            .arrayOfPointers = VK_FALSE,
            .data = { .deviceAddress = instance_buffer_address }
        };

        VkAccelerationStructureGeometryKHR tlas_geometry {
            .sType = VK_STRUCTURE_TYPE_ACCELERATION_STRUCTURE_GEOMETRY_KHR,
            .pNext = nullptr,
            .geometryType = VK_GEOMETRY_TYPE_INSTANCES_KHR,
            .geometry = { .instances = instances_data },
            .flags = VK_GEOMETRY_OPAQUE_BIT_KHR
        };

        VkAccelerationStructureBuildGeometryInfoKHR tlas_build_info {
            .sType = VK_STRUCTURE_TYPE_ACCELERATION_STRUCTURE_BUILD_GEOMETRY_INFO_KHR,
            .pNext = nullptr,
            .type = VK_ACCELERATION_STRUCTURE_TYPE_TOP_LEVEL_KHR,
            .flags = VK_BUILD_ACCELERATION_STRUCTURE_PREFER_FAST_TRACE_BIT_KHR,
            .mode = VK_BUILD_ACCELERATION_STRUCTURE_MODE_BUILD_KHR,
            .srcAccelerationStructure = VK_NULL_HANDLE,
            .dstAccelerationStructure = VK_NULL_HANDLE,
            .geometryCount = 1,
            .pGeometries = &tlas_geometry,
            .ppGeometries = nullptr,
            .scratchData = {}
        };

        u32 instance_count = 1;

        VkAccelerationStructureBuildSizesInfoKHR tlas_size_info {
            .sType = VK_STRUCTURE_TYPE_ACCELERATION_STRUCTURE_BUILD_SIZES_INFO_KHR,
            .pNext = nullptr
        };

        vkGetAccelerationStructureBuildSizesKHR(device->device(), VK_ACCELERATION_STRUCTURE_BUILD_TYPE_DEVICE_KHR, &tlas_build_info, &instance_count, &tlas_size_info);
        std::println("tlas size: {}", tlas_size_info.accelerationStructureSize);

        tlas_buffer = std::make_unique<RHI::Buffer>(device, tlas_size_info.accelerationStructureSize, VK_BUFFER_USAGE_ACCELERATION_STRUCTURE_STORAGE_BIT_KHR, VMA_MEMORY_USAGE_AUTO_PREFER_DEVICE);

        VkAccelerationStructureCreateInfoKHR tlas_create_info {
            .sType = VK_STRUCTURE_TYPE_ACCELERATION_STRUCTURE_CREATE_INFO_KHR,
            .pNext = nullptr,
            .createFlags = 0,
            .buffer = tlas_buffer->buffer(),
            .offset = 0,
            .size = tlas_size_info.accelerationStructureSize,
            .type = VK_ACCELERATION_STRUCTURE_TYPE_TOP_LEVEL_KHR,
            .deviceAddress = 0
        };

        VK_CHECK(vkCreateAccelerationStructureKHR(device->device(), &tlas_create_info, nullptr, &tlas));

        u64 tlas_scratch_size = vkutils::align_up(tlas_size_info.buildScratchSize, device->as_props().minAccelerationStructureScratchOffsetAlignment);
        RHI::Buffer tlas_scratch(device, tlas_scratch_size, VK_BUFFER_USAGE_STORAGE_BUFFER_BIT, VMA_MEMORY_USAGE_AUTO_PREFER_DEVICE);

        tlas_build_info.dstAccelerationStructure = tlas;
        tlas_build_info.scratchData.deviceAddress = tlas_scratch.address();

        VkAccelerationStructureBuildRangeInfoKHR tlas_range_info {
            .primitiveCount = instance_count,
            .primitiveOffset = 0,
            .firstVertex = 0,
            .transformOffset = 0
        };

        const VkAccelerationStructureBuildRangeInfoKHR* p_tlas_range_info = &tlas_range_info;

        auto build_cmd = compute_command->begin();
        {
            RHI::Buffer::copy(build_cmd, vertex_staging, vertex_buffer, vertex_size,
                VK_PIPELINE_STAGE_2_TRANSFER_BIT, VK_PIPELINE_STAGE_2_ACCELERATION_STRUCTURE_BUILD_BIT_KHR,
                VK_ACCESS_2_TRANSFER_WRITE_BIT, VK_ACCESS_2_ACCELERATION_STRUCTURE_READ_BIT_KHR
            );

            RHI::Buffer::copy(build_cmd, index_staging, index_buffer, index_size,
                VK_PIPELINE_STAGE_2_TRANSFER_BIT, VK_PIPELINE_STAGE_2_ACCELERATION_STRUCTURE_BUILD_BIT_KHR,
                VK_ACCESS_2_TRANSFER_WRITE_BIT, VK_ACCESS_2_ACCELERATION_STRUCTURE_READ_BIT_KHR
            );

            VkMemoryBarrier2 upload_barrier {
                .sType = VK_STRUCTURE_TYPE_MEMORY_BARRIER_2,
                .pNext = nullptr,
                .srcStageMask = VK_PIPELINE_STAGE_2_TRANSFER_BIT,
                .srcAccessMask = VK_ACCESS_2_TRANSFER_WRITE_BIT,
                .dstStageMask = VK_PIPELINE_STAGE_2_ACCELERATION_STRUCTURE_BUILD_BIT_KHR,
                .dstAccessMask = VK_ACCESS_2_ACCELERATION_STRUCTURE_READ_BIT_KHR
            };

            VkDependencyInfo upload_dependency {
                .sType = VK_STRUCTURE_TYPE_DEPENDENCY_INFO,
                .pNext = nullptr,
                .dependencyFlags = 0,
                .memoryBarrierCount = 1,
                .pMemoryBarriers = &upload_barrier,
                .bufferMemoryBarrierCount = 0,
                .pBufferMemoryBarriers = nullptr,
                .imageMemoryBarrierCount = 0,
                .pImageMemoryBarriers = nullptr
            };

            vkCmdPipelineBarrier2(build_cmd, &upload_dependency);

            vkCmdBuildAccelerationStructuresKHR(build_cmd, 1, &blas_build_info, &p_blas_range_info);

            VkMemoryBarrier2 blas_barrier {
                .sType = VK_STRUCTURE_TYPE_MEMORY_BARRIER_2,
                .pNext = nullptr,
                .srcStageMask = VK_PIPELINE_STAGE_2_ACCELERATION_STRUCTURE_BUILD_BIT_KHR,
                .srcAccessMask = VK_ACCESS_2_ACCELERATION_STRUCTURE_WRITE_BIT_KHR,
                .dstStageMask = VK_PIPELINE_STAGE_2_ACCELERATION_STRUCTURE_BUILD_BIT_KHR,
                .dstAccessMask = VK_ACCESS_2_ACCELERATION_STRUCTURE_READ_BIT_KHR
            };

            VkDependencyInfo blas_dependency {
                .sType = VK_STRUCTURE_TYPE_DEPENDENCY_INFO,
                .pNext = nullptr,
                .dependencyFlags = 0,
                .memoryBarrierCount = 1,
                .pMemoryBarriers = &blas_barrier,
                .bufferMemoryBarrierCount = 0,
                .pBufferMemoryBarriers = nullptr,
                .imageMemoryBarrierCount = 0,
                .pImageMemoryBarriers = nullptr
            };

            vkCmdPipelineBarrier2(build_cmd, &blas_dependency);

            vkCmdBuildAccelerationStructuresKHR(build_cmd, 1, &tlas_build_info, &p_tlas_range_info);
        }
        compute_command->end();

        std::vector<VkSemaphoreSubmitInfo> as_waits;
        std::vector<VkSemaphoreSubmitInfo> as_signals;
        compute_queue->submit(build_cmd, as_waits, as_signals);

        compute_queue->sync();
    }

    std::println("render loop start");

    u64 frame_count = 0;
    while (g_running) {
        glfwPollEvents();

        // stall if minimized
        while (g_running && (window_data.width == 0 || window_data.height == 0)) {
            glfwWaitEvents();
        }

        if (!g_running) break;

        // sync frames in flight

        if (frame_count >= FRAMES_IN_FLIGHT) {
            u64 wait_value = frame_count - FRAMES_IN_FLIGHT + 1;
            graphics_queue->sync(wait_value);
        }

        // acquire swapchain image

        if (!swapchain->acquire_image()) {
            swapchain->recreate(VkExtent2D { window_data.width, window_data.height });
            continue;
        }

        // record commands

        auto compute_cmd = compute_command->begin();
        compute_command->end();

        auto graphics_cmd = graphics_command->begin();
        {
            VkImageMemoryBarrier2 barrier {
                .sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER_2,
                .pNext = nullptr,
                .srcStageMask = VK_PIPELINE_STAGE_2_COLOR_ATTACHMENT_OUTPUT_BIT,
                .srcAccessMask = VK_ACCESS_2_NONE,
                .dstStageMask = VK_PIPELINE_STAGE_2_BOTTOM_OF_PIPE_BIT,
                .dstAccessMask = VK_ACCESS_2_NONE,
                .oldLayout = VK_IMAGE_LAYOUT_UNDEFINED,
                .newLayout = VK_IMAGE_LAYOUT_PRESENT_SRC_KHR,
                .srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED,
                .dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED,
                .image = swapchain->current_image().image(),
                .subresourceRange = {
                    .aspectMask = VK_IMAGE_ASPECT_COLOR_BIT,
                    .baseMipLevel = 0,
                    .levelCount = 1,
                    .baseArrayLayer = 0,
                    .layerCount = 1
                }
            };

            VkDependencyInfo dependency {
                .sType = VK_STRUCTURE_TYPE_DEPENDENCY_INFO,
                .pNext = nullptr,
                .dependencyFlags = 0,
                .memoryBarrierCount = 0,
                .pMemoryBarriers = nullptr,
                .bufferMemoryBarrierCount = 0,
                .pBufferMemoryBarriers = nullptr,
                .imageMemoryBarrierCount = 1,
                .pImageMemoryBarriers = &barrier
            };

            vkCmdPipelineBarrier2(graphics_cmd, &dependency);
        }
        graphics_command->end();

        // submit commands

        std::vector<VkSemaphoreSubmitInfo> compute_waits;
        std::vector<VkSemaphoreSubmitInfo> compute_signals;
        u64 compute_signal_value = compute_queue->submit(compute_cmd, compute_waits, compute_signals);

        std::vector<VkSemaphoreSubmitInfo> graphics_waits {
            swapchain->acquire_wait_info(),
            compute_queue->wait_info()
        };

        std::vector<VkSemaphoreSubmitInfo> graphics_signals {
            swapchain->present_signal_info()
        };

        u64 graphics_signal_value = graphics_queue->submit(graphics_cmd, graphics_waits, graphics_signals);

        // swapchain present

        if (!swapchain->present(graphics_queue->queue())) {
            swapchain->recreate(VkExtent2D { window_data.width, window_data.height });
        }

        frame_count++;
    }

    vkDeviceWaitIdle(device->device());

    vkDestroyAccelerationStructureKHR(device->device(), tlas, nullptr);
    vkDestroyAccelerationStructureKHR(device->device(), blas, nullptr);

    glfwDestroyWindow(window);
    glfwTerminate();
}
