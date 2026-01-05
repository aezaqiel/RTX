#include "vk_types.hpp"

#include <GLFW/glfw3.h>
#include <GLFW/glfw3native.h>

#include <glm/glm.hpp>

#include "rhi/context.hpp"
#include "rhi/device.hpp"
#include "rhi/command.hpp"
#include "rhi/sync.hpp"
#include "rhi/buffer.hpp"

#include "scene/loader.hpp"

namespace {

    constexpr usize FRAMES_IN_FLIGHT = 3;

}

auto main() -> i32
{
    i32 window_width = 1280;
    i32 window_height = 720;

    glfwSetErrorCallback([](i32 code, const char* desc) {
        std::println(std::cerr, "glfw error ({}): {}", code, desc);
    });

    std::println("creating window");

    glfwInit();

    glfwWindowHint(GLFW_CLIENT_API, GLFW_NO_API);
    glfwWindowHint(GLFW_RESIZABLE, GLFW_FALSE);

    GLFWwindow* window = glfwCreateWindow(1280, 720, "RTX", nullptr, nullptr);
    {
        glfwGetFramebufferSize(window, &window_width, &window_height);
        std::println("created window \"{}\" ({}, {})", glfwGetWindowTitle(window), window_width, window_height);
    }

    std::println("creating vulkan instance");
    auto context = std::make_shared<RHI::Context>(window);

    std::println("creating vulkan device");
    auto device = std::make_shared<RHI::Device>(context);

    std::println("query swapchain details");

    VkSurfaceCapabilitiesKHR surface_caps;
    VkSurfaceFormatKHR surface_format;
    VkPresentModeKHR present_mode;
    VkExtent2D extent;

    { // swapchain details
        vkGetPhysicalDeviceSurfaceCapabilitiesKHR(device->physical(), context->surface(), &surface_caps);

        u32 format_count = 0;
        vkGetPhysicalDeviceSurfaceFormatsKHR(device->physical(), context->surface(), &format_count, nullptr);
        std::vector<VkSurfaceFormatKHR> available_formats(format_count);
        vkGetPhysicalDeviceSurfaceFormatsKHR(device->physical(), context->surface(), &format_count, available_formats.data());

        surface_format = available_formats[0];
        for (const auto& format : available_formats) {
            if (format.format == VK_FORMAT_R8G8B8A8_SRGB && format.colorSpace == VK_COLOR_SPACE_SRGB_NONLINEAR_KHR) {
                surface_format = format;
                break;
            }
        }

        u32 mode_count = 0;
        vkGetPhysicalDeviceSurfacePresentModesKHR(device->physical(), context->surface(), &mode_count, nullptr);
        std::vector<VkPresentModeKHR> available_modes(mode_count);
        vkGetPhysicalDeviceSurfacePresentModesKHR(device->physical(), context->surface(), &mode_count, available_modes.data());

        present_mode = VK_PRESENT_MODE_FIFO_KHR;
        for (const auto& mode : available_modes) {
            if (mode == VK_PRESENT_MODE_MAILBOX_KHR) {
                present_mode = VK_PRESENT_MODE_MAILBOX_KHR;
                break;
            }
        }

        if (surface_caps.currentExtent.width != std::numeric_limits<u32>::max()) {
            extent = surface_caps.currentExtent;
        } else {
            extent = {
                .width = std::clamp(static_cast<u32>(window_width), surface_caps.minImageExtent.width, surface_caps.maxImageExtent.width),
                .height = std::clamp(static_cast<u32>(window_height), surface_caps.minImageExtent.height, surface_caps.maxImageExtent.height)
            };
        }
    }

    std::println("create swapchain");

    VkSwapchainKHR swapchain;
    u32 swapchain_image_count;
    std::vector<VkImage> swapchain_images;
    std::vector<VkImageView> swapchain_image_views;

    { // create swapchain
        swapchain_image_count = surface_caps.minImageCount + 1;
        if (surface_caps.maxImageCount > 0) {
            swapchain_image_count = std::min(swapchain_image_count, surface_caps.maxImageCount);
        }

        VkSwapchainCreateInfoKHR swapchain_info {
            .sType = VK_STRUCTURE_TYPE_SWAPCHAIN_CREATE_INFO_KHR,
            .pNext = nullptr,
            .flags = 0,
            .surface = context->surface(),
            .minImageCount = swapchain_image_count,
            .imageFormat = surface_format.format,
            .imageColorSpace = surface_format.colorSpace,
            .imageExtent = extent,
            .imageArrayLayers = 1,
            .imageUsage = VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT | VK_IMAGE_USAGE_TRANSFER_DST_BIT,
            .imageSharingMode = VK_SHARING_MODE_EXCLUSIVE,
            .queueFamilyIndexCount = 0,
            .pQueueFamilyIndices = nullptr,
            .preTransform = surface_caps.currentTransform,
            .compositeAlpha = VK_COMPOSITE_ALPHA_OPAQUE_BIT_KHR,
            .presentMode = present_mode,
            .clipped = VK_TRUE,
            .oldSwapchain = VK_NULL_HANDLE
        };

        VK_CHECK(vkCreateSwapchainKHR(device->device(), &swapchain_info, nullptr, &swapchain));

        vkGetSwapchainImagesKHR(device->device(), swapchain, &swapchain_image_count, nullptr);

        swapchain_images.resize(swapchain_image_count);
        swapchain_image_views.resize(swapchain_image_count);

        vkGetSwapchainImagesKHR(device->device(), swapchain, &swapchain_image_count, swapchain_images.data());

        for (u32 i = 0; i < swapchain_image_count; ++i) {
            VkImageViewCreateInfo view_info {
                .sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO,
                .pNext = nullptr,
                .flags = 0,
                .image = swapchain_images[i],
                .viewType = VK_IMAGE_VIEW_TYPE_2D,
                .format = surface_format.format,
                .components = {
                    .r = VK_COMPONENT_SWIZZLE_IDENTITY,
                    .g = VK_COMPONENT_SWIZZLE_IDENTITY,
                    .b = VK_COMPONENT_SWIZZLE_IDENTITY,
                    .a = VK_COMPONENT_SWIZZLE_IDENTITY
                },
                .subresourceRange = {
                    .aspectMask = VK_IMAGE_ASPECT_COLOR_BIT,
                    .baseMipLevel = 0,
                    .levelCount = 1,
                    .baseArrayLayer = 0,
                    .layerCount = 1
                }
            };

            VK_CHECK(vkCreateImageView(device->device(), &view_info, nullptr, &swapchain_image_views[i]));
        }
    }

    std::println("creating command and sync primitives");

    QueueSync graphics_sync = QueueSync::create(device->device(), device->graphics_queue());
    QueueSync compute_sync = QueueSync::create(device->device(), device->compute_queue());

    CommandContext compute_command = CommandContext::create(device->device(), device->compute_index());

    std::println("loading model");

    auto model = Loader::load_obj("assets/sponza/sponza.obj");

    std::println("Model loaded: {} vertices, {} indices", 
        model.mesh->positions.size(), 
        model.mesh->indices.size()
    );

    std::println("uploading model and building AS");

    usize vertex_size = model.mesh->positions.size() * sizeof(glm::vec3);
    usize index_size = model.mesh->indices.size() * sizeof(u32);

    VkBufferUsageFlags build_input_flags =
        VK_BUFFER_USAGE_ACCELERATION_STRUCTURE_BUILD_INPUT_READ_ONLY_BIT_KHR |
        VK_BUFFER_USAGE_TRANSFER_DST_BIT |
        VK_BUFFER_USAGE_SHADER_DEVICE_ADDRESS_BIT;

    Buffer vertex_buffer = Buffer::create(device->allocator(), vertex_size, build_input_flags, VMA_MEMORY_USAGE_AUTO_PREFER_DEVICE);
    Buffer index_buffer = Buffer::create(device->allocator(), index_size, build_input_flags | VK_BUFFER_USAGE_INDEX_BUFFER_BIT, VMA_MEMORY_USAGE_AUTO_PREFER_DEVICE);

    Buffer vertex_staging = Buffer::create_staging(device->allocator(), vertex_size, model.mesh->positions.data());
    Buffer index_staging = Buffer::create_staging(device->allocator(), index_size, model.mesh->indices.data());

    VkAccelerationStructureKHR blas;
    Buffer blas_buffer;
    Buffer blas_scratch;

    VkAccelerationStructureGeometryTrianglesDataKHR triangles {
        .sType = VK_STRUCTURE_TYPE_ACCELERATION_STRUCTURE_GEOMETRY_TRIANGLES_DATA_KHR,
        .pNext = nullptr,
        .vertexFormat = VK_FORMAT_R32G32B32_SFLOAT,
        .vertexData = { .deviceAddress = vertex_buffer.address },
        .vertexStride = sizeof(glm::vec3),
        .maxVertex = static_cast<u32>(model.mesh->positions.size() - 1),
        .indexType = VK_INDEX_TYPE_UINT32,
        .indexData = { .deviceAddress = index_buffer.address },
        .transformData = { .deviceAddress = 0 }
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

    blas_buffer = Buffer::create(device->allocator(), blas_size_info.accelerationStructureSize, VK_BUFFER_USAGE_ACCELERATION_STRUCTURE_STORAGE_BIT_KHR | VK_BUFFER_USAGE_SHADER_DEVICE_ADDRESS_BIT, VMA_MEMORY_USAGE_AUTO_PREFER_DEVICE);

    VkAccelerationStructureCreateInfoKHR blas_create_info {
        .sType = VK_STRUCTURE_TYPE_ACCELERATION_STRUCTURE_CREATE_INFO_KHR,
        .pNext = nullptr,
        .createFlags = 0,
        .buffer = blas_buffer.handle,
        .offset = 0,
        .size = blas_size_info.accelerationStructureSize,
        .type = VK_ACCELERATION_STRUCTURE_TYPE_BOTTOM_LEVEL_KHR,
        .deviceAddress = 0
    };

    VK_CHECK(vkCreateAccelerationStructureKHR(device->device(), &blas_create_info, nullptr, &blas));

    blas_scratch = Buffer::create_aligned(device->allocator(), blas_size_info.buildScratchSize, device->as_props().minAccelerationStructureScratchOffsetAlignment, VK_BUFFER_USAGE_STORAGE_BUFFER_BIT | VK_BUFFER_USAGE_SHADER_DEVICE_ADDRESS_BIT, VMA_MEMORY_USAGE_AUTO_PREFER_DEVICE);

    blas_build_info.dstAccelerationStructure = blas;
    blas_build_info.scratchData.deviceAddress = blas_scratch.address;

    VkAccelerationStructureBuildRangeInfoKHR blas_range_info {
        .primitiveCount = primitive_count,
        .primitiveOffset = 0,
        .firstVertex = 0,
        .transformOffset = 0
    };

    const VkAccelerationStructureBuildRangeInfoKHR* p_blas_range_info = &blas_range_info;

    VkAccelerationStructureKHR tlas;
    Buffer tlas_buffer;
    Buffer tlas_scratch;
    Buffer instance_buffer;

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

    instance_buffer = Buffer::create(device->allocator(), sizeof(VkAccelerationStructureInstanceKHR), VK_BUFFER_USAGE_ACCELERATION_STRUCTURE_BUILD_INPUT_READ_ONLY_BIT_KHR | VK_BUFFER_USAGE_SHADER_DEVICE_ADDRESS_BIT, VMA_MEMORY_USAGE_CPU_TO_GPU);
    instance_buffer.upload(device->allocator(), &tlas_instance, sizeof(VkAccelerationStructureInstanceKHR));

    VkDeviceAddress instance_buffer_address = instance_buffer.address;

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

    tlas_buffer = Buffer::create(device->allocator(), tlas_size_info.accelerationStructureSize, VK_BUFFER_USAGE_ACCELERATION_STRUCTURE_STORAGE_BIT_KHR | VK_BUFFER_USAGE_SHADER_DEVICE_ADDRESS_BIT, VMA_MEMORY_USAGE_AUTO_PREFER_DEVICE);

    VkAccelerationStructureCreateInfoKHR tlas_create_info {
        .sType = VK_STRUCTURE_TYPE_ACCELERATION_STRUCTURE_CREATE_INFO_KHR,
        .pNext = nullptr,
        .createFlags = 0,
        .buffer = tlas_buffer.handle,
        .offset = 0,
        .size = tlas_size_info.accelerationStructureSize,
        .type = VK_ACCELERATION_STRUCTURE_TYPE_TOP_LEVEL_KHR,
        .deviceAddress = 0
    };

    VK_CHECK(vkCreateAccelerationStructureKHR(device->device(), &tlas_create_info, nullptr, &tlas));

    tlas_scratch = Buffer::create_aligned(device->allocator(), tlas_size_info.buildScratchSize, device->as_props().minAccelerationStructureScratchOffsetAlignment, VK_BUFFER_USAGE_STORAGE_BUFFER_BIT | VK_BUFFER_USAGE_SHADER_DEVICE_ADDRESS_BIT, VMA_MEMORY_USAGE_AUTO_PREFER_DEVICE);

    tlas_build_info.dstAccelerationStructure = tlas;
    tlas_build_info.scratchData.deviceAddress = tlas_scratch.address;

    VkAccelerationStructureBuildRangeInfoKHR tlas_range_info {
        .primitiveCount = instance_count,
        .primitiveOffset = 0,
        .firstVertex = 0,
        .transformOffset = 0
    };

    const VkAccelerationStructureBuildRangeInfoKHR* p_tlas_range_info = &tlas_range_info;

    compute_command.record(device->device(), [&](VkCommandBuffer cmd) {
        Buffer::copy(cmd, vertex_staging, vertex_buffer, vertex_size);
        Buffer::copy(cmd, index_staging, index_buffer, index_size);

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

        vkCmdPipelineBarrier2(cmd, &upload_dependency);

        vkCmdBuildAccelerationStructuresKHR(cmd, 1, &blas_build_info, &p_blas_range_info);

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

        vkCmdPipelineBarrier2(cmd, &blas_dependency);

        vkCmdBuildAccelerationStructuresKHR(cmd, 1, &tlas_build_info, &p_tlas_range_info);
    });

    std::vector<VkSemaphoreSubmitInfo> as_waits;
    std::vector<VkSemaphoreSubmitInfo> as_signals;
    u64 as_signal_value = compute_sync.submit(device->device(), compute_command.buffer, as_waits, as_signals);

    std::println("creating frame resources");

    std::array<FrameContext, FRAMES_IN_FLIGHT> frames;
    for (auto& frame : frames) {
        frame = FrameContext::create(device->device(),
            device->graphics_index(),
            device->compute_index()
        );
    }
    
    std::println("sync AS build");

    VkSemaphoreWaitInfo as_wait_info {
        .sType = VK_STRUCTURE_TYPE_SEMAPHORE_WAIT_INFO,
        .pNext = nullptr,
        .flags = 0,
        .semaphoreCount = 1,
        .pSemaphores = &compute_sync.timeline,
        .pValues = &as_signal_value
    };

    VK_CHECK(vkWaitSemaphores(device->device(), &as_wait_info, std::numeric_limits<u64>::max()));

    tlas_scratch.destroy(device->allocator());
    instance_buffer.destroy(device->allocator());

    blas_scratch.destroy(device->allocator());

    vertex_staging.destroy(device->allocator());
    index_staging.destroy(device->allocator());

    vertex_buffer.destroy(device->allocator());
    index_buffer.destroy(device->allocator());

    compute_command.destroy(device->device());

    std::println("render loop start");

    u64 frame_count = 0;
    while (!glfwWindowShouldClose(window)) {
        glfwPollEvents();
        
        FrameContext& frame = frames[frame_count % FRAMES_IN_FLIGHT];
        if (frame_count >= FRAMES_IN_FLIGHT) {
            u64 wait_value = frame_count - FRAMES_IN_FLIGHT + 1;

            VkSemaphoreWaitInfo wait_info {
                .sType = VK_STRUCTURE_TYPE_SEMAPHORE_WAIT_INFO,
                .pNext = nullptr,
                .flags = 0,
                .semaphoreCount = 1,
                .pSemaphores = &graphics_sync.timeline,
                .pValues = &wait_value
            };

            VK_CHECK(vkWaitSemaphores(device->device(), &wait_info, std::numeric_limits<u64>::max()));
        }

        u32 swapchain_index;
        vkAcquireNextImageKHR(device->device(), swapchain, std::numeric_limits<u64>::max(), frame.image_available, VK_NULL_HANDLE, &swapchain_index);

        frame.compute.record(device->device(), [&](VkCommandBuffer cmd) {
        });

        std::vector<VkSemaphoreSubmitInfo> compute_waits;
        std::vector<VkSemaphoreSubmitInfo> compute_signals;
        u64 compute_signal_value = compute_sync.submit(device->device(), frame.compute.buffer, compute_waits, compute_signals);

        frame.graphics.record(device->device(), [&](VkCommandBuffer cmd) {
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
                .image = swapchain_images[swapchain_index],
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

            vkCmdPipelineBarrier2(cmd, &dependency);
        });

        std::vector<VkSemaphoreSubmitInfo> graphics_waits;

        graphics_waits.push_back(VkSemaphoreSubmitInfo {
            .sType = VK_STRUCTURE_TYPE_SEMAPHORE_SUBMIT_INFO,
            .pNext = nullptr,
            .semaphore = frame.image_available,
            .value = 0,
            .stageMask = VK_PIPELINE_STAGE_2_COLOR_ATTACHMENT_OUTPUT_BIT,
            .deviceIndex = 0
        });

        graphics_waits.push_back(VkSemaphoreSubmitInfo {
            .sType = VK_STRUCTURE_TYPE_SEMAPHORE_SUBMIT_INFO,
            .pNext = nullptr,
            .semaphore = compute_sync.timeline,
            .value = compute_signal_value,
            .stageMask = VK_PIPELINE_STAGE_2_ALL_COMMANDS_BIT,
            .deviceIndex = 0
        });

        std::vector<VkSemaphoreSubmitInfo> graphics_signals;

        graphics_signals.push_back(VkSemaphoreSubmitInfo {
            .sType = VK_STRUCTURE_TYPE_SEMAPHORE_SUBMIT_INFO,
            .pNext = nullptr,
            .semaphore = frame.render_complete,
            .value = 0,
            .stageMask = VK_PIPELINE_STAGE_2_ALL_COMMANDS_BIT,
            .deviceIndex = 0
        });

        u64 graphics_signal_value = graphics_sync.submit(device->device(), frame.graphics.buffer, graphics_waits, graphics_signals);

        VkPresentInfoKHR present_info {
            .sType = VK_STRUCTURE_TYPE_PRESENT_INFO_KHR,
            .pNext = nullptr,
            .waitSemaphoreCount = 1,
            .pWaitSemaphores = &frame.render_complete,
            .swapchainCount = 1,
            .pSwapchains = &swapchain,
            .pImageIndices = &swapchain_index,
            .pResults = nullptr
        };

        vkQueuePresentKHR(device->graphics_queue(), &present_info);

        frame_count++;
    }

    vkDeviceWaitIdle(device->device());

    for (auto& frame : frames) {
        frame.destroy(device->device());
    }

    graphics_sync.destroy(device->device());
    compute_sync.destroy(device->device());

    vkDestroyAccelerationStructureKHR(device->device(), tlas, nullptr);
    tlas_buffer.destroy(device->allocator());

    vkDestroyAccelerationStructureKHR(device->device(), blas, nullptr);
    blas_buffer.destroy(device->allocator());

    for (u32 i = 0; i < swapchain_image_count; ++i) {
        vkDestroyImageView(device->device(), swapchain_image_views[i], nullptr);
    }

    vkDestroySwapchainKHR(device->device(), swapchain, nullptr);
    swapchain_image_views.clear();
    swapchain_images.clear();

    glfwDestroyWindow(window);
    glfwTerminate();
}
