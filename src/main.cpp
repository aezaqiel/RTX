#include <GLFW/glfw3.h>
#include <GLFW/glfw3native.h>

#include <glm/glm.hpp>

#include "rhi/context.hpp"
#include "rhi/device.hpp"
#include "rhi/swapchain.hpp"
#include "rhi/queue.hpp"
#include "rhi/command.hpp"
#include "rhi/buffer.hpp"
#include "rhi/acceleration_structure.hpp"
#include "rhi/shader.hpp"

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

    std::println("loading models");

    auto sponza = Loader::load_obj("assets/sponza/sponza.obj");

    auto upload_cmd = transfer_command->begin();

    std::unique_ptr<RHI::Buffer> sponza_vb;
    std::unique_ptr<RHI::Buffer> sponza_ib;

    u64 sponza_vb_size = sponza.mesh->vertices.size() * sizeof(Vertex);
    u64 sponza_ib_size = sponza.mesh->indices.size() * sizeof(u32);

    RHI::Buffer sponza_vb_staging(device, sponza_vb_size, VK_BUFFER_USAGE_TRANSFER_SRC_BIT, VMA_MEMORY_USAGE_CPU_TO_GPU, VMA_ALLOCATION_CREATE_HOST_ACCESS_SEQUENTIAL_WRITE_BIT);
    sponza_vb_staging.write(sponza.mesh->vertices.data(), sponza_vb_size);

    RHI::Buffer sponza_ib_staging(device, sponza_ib_size, VK_BUFFER_USAGE_TRANSFER_SRC_BIT, VMA_MEMORY_USAGE_CPU_TO_GPU, VMA_ALLOCATION_CREATE_HOST_ACCESS_SEQUENTIAL_WRITE_BIT);
    sponza_ib_staging.write(sponza.mesh->indices.data(), sponza_ib_size);

    sponza_vb = std::make_unique<RHI::Buffer>(device, sponza_vb_size,
        VK_BUFFER_USAGE_ACCELERATION_STRUCTURE_BUILD_INPUT_READ_ONLY_BIT_KHR | VK_BUFFER_USAGE_TRANSFER_DST_BIT,
        VMA_MEMORY_USAGE_AUTO_PREFER_DEVICE
    );

    sponza_ib = std::make_unique<RHI::Buffer>(device, sponza_ib_size,
        VK_BUFFER_USAGE_ACCELERATION_STRUCTURE_BUILD_INPUT_READ_ONLY_BIT_KHR | VK_BUFFER_USAGE_INDEX_BUFFER_BIT | VK_BUFFER_USAGE_TRANSFER_DST_BIT,
        VMA_MEMORY_USAGE_AUTO_PREFER_DEVICE
    );

    sponza_vb->stage(upload_cmd, sponza_vb_staging);
    sponza_ib->stage(upload_cmd, sponza_ib_staging);

    auto teapot = Loader::load_obj("assets/teapot.obj");

    std::unique_ptr<RHI::Buffer> teapot_vb;
    std::unique_ptr<RHI::Buffer> teapot_ib;

    u64 teapot_vb_size = teapot.mesh->vertices.size() * sizeof(Vertex);
    u64 teapot_ib_size = teapot.mesh->indices.size() * sizeof(u32);

    RHI::Buffer teapot_vb_staging(device, teapot_vb_size, VK_BUFFER_USAGE_TRANSFER_SRC_BIT, VMA_MEMORY_USAGE_CPU_TO_GPU, VMA_ALLOCATION_CREATE_HOST_ACCESS_SEQUENTIAL_WRITE_BIT);
    teapot_vb_staging.write(teapot.mesh->vertices.data(), teapot_vb_size);

    RHI::Buffer teapot_ib_staging(device, teapot_ib_size, VK_BUFFER_USAGE_TRANSFER_SRC_BIT, VMA_MEMORY_USAGE_CPU_TO_GPU, VMA_ALLOCATION_CREATE_HOST_ACCESS_SEQUENTIAL_WRITE_BIT);
    teapot_ib_staging.write(teapot.mesh->indices.data(), teapot_ib_size);

    teapot_vb = std::make_unique<RHI::Buffer>(device, teapot_vb_size,
        VK_BUFFER_USAGE_ACCELERATION_STRUCTURE_BUILD_INPUT_READ_ONLY_BIT_KHR | VK_BUFFER_USAGE_TRANSFER_DST_BIT,
        VMA_MEMORY_USAGE_AUTO_PREFER_DEVICE
    );

    teapot_ib = std::make_unique<RHI::Buffer>(device, teapot_ib_size,
        VK_BUFFER_USAGE_ACCELERATION_STRUCTURE_BUILD_INPUT_READ_ONLY_BIT_KHR | VK_BUFFER_USAGE_INDEX_BUFFER_BIT | VK_BUFFER_USAGE_TRANSFER_DST_BIT,
        VMA_MEMORY_USAGE_AUTO_PREFER_DEVICE
    );

    teapot_vb->stage(upload_cmd, teapot_vb_staging);
    teapot_ib->stage(upload_cmd, teapot_ib_staging);

    // release ownership
    std::array<VkBufferMemoryBarrier2, 4> release_barriers {
        VkBufferMemoryBarrier2 {
            .sType = VK_STRUCTURE_TYPE_BUFFER_MEMORY_BARRIER_2,
            .pNext = nullptr,
            .srcStageMask = VK_PIPELINE_STAGE_2_TRANSFER_BIT,
            .srcAccessMask = VK_ACCESS_2_TRANSFER_WRITE_BIT,
            .dstStageMask = VK_PIPELINE_STAGE_2_ALL_COMMANDS_BIT,
            .dstAccessMask = VK_ACCESS_2_NONE,
            .srcQueueFamilyIndex = device->transfer_index(),
            .dstQueueFamilyIndex = device->compute_index(),
            .buffer = sponza_vb->buffer(),
            .offset = 0,
            .size = sponza_vb->size()
        },
        VkBufferMemoryBarrier2 {
            .sType = VK_STRUCTURE_TYPE_BUFFER_MEMORY_BARRIER_2,
            .pNext = nullptr,
            .srcStageMask = VK_PIPELINE_STAGE_2_TRANSFER_BIT,
            .srcAccessMask = VK_ACCESS_2_TRANSFER_WRITE_BIT,
            .dstStageMask = VK_PIPELINE_STAGE_2_ALL_COMMANDS_BIT,
            .dstAccessMask = VK_ACCESS_2_NONE,
            .srcQueueFamilyIndex = device->transfer_index(),
            .dstQueueFamilyIndex = device->compute_index(),
            .buffer = sponza_ib->buffer(),
            .offset = 0,
            .size = sponza_ib->size()
        },
        VkBufferMemoryBarrier2 {
            .sType = VK_STRUCTURE_TYPE_BUFFER_MEMORY_BARRIER_2,
            .pNext = nullptr,
            .srcStageMask = VK_PIPELINE_STAGE_2_TRANSFER_BIT,
            .srcAccessMask = VK_ACCESS_2_TRANSFER_WRITE_BIT,
            .dstStageMask = VK_PIPELINE_STAGE_2_ALL_COMMANDS_BIT,
            .dstAccessMask = VK_ACCESS_2_NONE,
            .srcQueueFamilyIndex = device->transfer_index(),
            .dstQueueFamilyIndex = device->compute_index(),
            .buffer = teapot_vb->buffer(),
            .offset = 0,
            .size = teapot_vb->size()
        },
        VkBufferMemoryBarrier2 {
            .sType = VK_STRUCTURE_TYPE_BUFFER_MEMORY_BARRIER_2,
            .pNext = nullptr,
            .srcStageMask = VK_PIPELINE_STAGE_2_TRANSFER_BIT,
            .srcAccessMask = VK_ACCESS_2_TRANSFER_WRITE_BIT,
            .dstStageMask = VK_PIPELINE_STAGE_2_ALL_COMMANDS_BIT,
            .dstAccessMask = VK_ACCESS_2_NONE,
            .srcQueueFamilyIndex = device->transfer_index(),
            .dstQueueFamilyIndex = device->compute_index(),
            .buffer = teapot_ib->buffer(),
            .offset = 0,
            .size = teapot_ib->size()
        }
    };

    VkDependencyInfo release_dependency {
        .sType = VK_STRUCTURE_TYPE_DEPENDENCY_INFO,
        .pNext = nullptr,
        .dependencyFlags = 0,
        .memoryBarrierCount = 0,
        .pMemoryBarriers = nullptr,
        .bufferMemoryBarrierCount = static_cast<u32>(release_barriers.size()),
        .pBufferMemoryBarriers = release_barriers.data(),
        .imageMemoryBarrierCount = 0,
        .pImageMemoryBarriers = nullptr
    };

    vkCmdPipelineBarrier2(upload_cmd, &release_dependency);

    transfer_command->end(upload_cmd);

    std::vector<VkSemaphoreSubmitInfo> upload_signals;
    transfer_queue->submit(upload_cmd, {}, upload_signals);

    std::println("AS build");

    std::vector<std::unique_ptr<RHI::TLAS>> tlases;

    RHI::AccelerationStructureBuilder as_builder(device);

    auto as_cmd = compute_command->begin();

    // take ownership

    std::array<VkBufferMemoryBarrier2, 4> acquire_barriers {
        VkBufferMemoryBarrier2 {
            .sType = VK_STRUCTURE_TYPE_BUFFER_MEMORY_BARRIER_2,
            .pNext = nullptr,
            .srcStageMask = VK_PIPELINE_STAGE_2_ALL_COMMANDS_BIT,
            .srcAccessMask = VK_ACCESS_2_NONE,
            .dstStageMask = VK_PIPELINE_STAGE_2_ACCELERATION_STRUCTURE_BUILD_BIT_KHR,
            .dstAccessMask = VK_ACCESS_2_ACCELERATION_STRUCTURE_READ_BIT_KHR,
            .srcQueueFamilyIndex = device->transfer_index(),
            .dstQueueFamilyIndex = device->compute_index(),
            .buffer = sponza_vb->buffer(),
            .offset = 0,
            .size = sponza_vb->size()
        },
        VkBufferMemoryBarrier2 {
            .sType = VK_STRUCTURE_TYPE_BUFFER_MEMORY_BARRIER_2,
            .pNext = nullptr,
            .srcStageMask = VK_PIPELINE_STAGE_2_ALL_COMMANDS_BIT,
            .srcAccessMask = VK_ACCESS_2_NONE,
            .dstStageMask = VK_PIPELINE_STAGE_2_ACCELERATION_STRUCTURE_BUILD_BIT_KHR,
            .dstAccessMask = VK_ACCESS_2_ACCELERATION_STRUCTURE_READ_BIT_KHR,
            .srcQueueFamilyIndex = device->transfer_index(),
            .dstQueueFamilyIndex = device->compute_index(),
            .buffer = sponza_ib->buffer(),
            .offset = 0,
            .size = sponza_ib->size()
        },
        VkBufferMemoryBarrier2 {
            .sType = VK_STRUCTURE_TYPE_BUFFER_MEMORY_BARRIER_2,
            .pNext = nullptr,
            .srcStageMask = VK_PIPELINE_STAGE_2_ALL_COMMANDS_BIT,
            .srcAccessMask = VK_ACCESS_2_NONE,
            .dstStageMask = VK_PIPELINE_STAGE_2_ACCELERATION_STRUCTURE_BUILD_BIT_KHR,
            .dstAccessMask = VK_ACCESS_2_ACCELERATION_STRUCTURE_READ_BIT_KHR,
            .srcQueueFamilyIndex = device->transfer_index(),
            .dstQueueFamilyIndex = device->compute_index(),
            .buffer = teapot_vb->buffer(),
            .offset = 0,
            .size = teapot_vb->size()
        },
        VkBufferMemoryBarrier2 {
            .sType = VK_STRUCTURE_TYPE_BUFFER_MEMORY_BARRIER_2,
            .pNext = nullptr,
            .srcStageMask = VK_PIPELINE_STAGE_2_ALL_COMMANDS_BIT,
            .srcAccessMask = VK_ACCESS_2_NONE,
            .dstStageMask = VK_PIPELINE_STAGE_2_ACCELERATION_STRUCTURE_BUILD_BIT_KHR,
            .dstAccessMask = VK_ACCESS_2_ACCELERATION_STRUCTURE_READ_BIT_KHR,
            .srcQueueFamilyIndex = device->transfer_index(),
            .dstQueueFamilyIndex = device->compute_index(),
            .buffer = teapot_ib->buffer(),
            .offset = 0,
            .size = teapot_ib->size()
        }
    };

    VkDependencyInfo acquire_dependency {
        .sType = VK_STRUCTURE_TYPE_DEPENDENCY_INFO,
        .pNext = nullptr,
        .dependencyFlags = 0,
        .memoryBarrierCount = 0,
        .pMemoryBarriers = nullptr,
        .bufferMemoryBarrierCount = static_cast<u32>(acquire_barriers.size()),
        .pBufferMemoryBarriers = acquire_barriers.data(),
        .imageMemoryBarrierCount = 0,
        .pImageMemoryBarriers = nullptr
    };

    vkCmdPipelineBarrier2(as_cmd, &acquire_dependency);

    RHI::BLAS::Input sponza_blas;
    sponza_blas.add_geometry(*sponza_vb, sponza.mesh->vertices.size(), sizeof(Vertex), *sponza_ib, sponza.mesh->indices.size());

    RHI::BLAS::Input teapot_blas;
    teapot_blas.add_geometry(*teapot_vb, teapot.mesh->vertices.size(), sizeof(Vertex), *teapot_ib, teapot.mesh->indices.size());

    auto blases = as_builder.build_blas(as_cmd, {
        sponza_blas,
        teapot_blas
    });

    for (const auto& blas : blases) {
        VkAccelerationStructureInstanceKHR instance {
            .transform = vkutils::glm_to_vkmatrix(glm::mat4(1.0f)),
            .instanceCustomIndex = 0,
            .mask = 0xFF,
            .instanceShaderBindingTableRecordOffset = 0,
            .flags = 0,
            .accelerationStructureReference = blas->address()
        };

        RHI::TLAS::Input tlas_input {
            .instances = std::vector<VkAccelerationStructureInstanceKHR>(1, instance)
        };

        tlases.push_back(as_builder.build_tlas(as_cmd, tlas_input));
    }

    compute_command->end(as_cmd);

    std::vector<VkSemaphoreSubmitInfo> as_signals;
    u64 as_build_timeline = compute_queue->submit(as_cmd, upload_signals, as_signals);

    compute_queue->sync(as_build_timeline);

    as_builder.cleanup();

    std::println("loading shaders");

    auto raygen_shader = std::make_unique<RHI::Shader>(device, "raygen.rgen.spv", VK_SHADER_STAGE_RAYGEN_BIT_KHR);
    auto closesthit_shader = std::make_unique<RHI::Shader>(device, "closesthit.rchit.spv", VK_SHADER_STAGE_CLOSEST_HIT_BIT_KHR);
    auto miss_shader = std::make_unique<RHI::Shader>(device, "miss.rmiss.spv", VK_SHADER_STAGE_MISS_BIT_KHR);

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
        compute_command->end(compute_cmd);

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
        graphics_command->end(graphics_cmd);

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

    glfwDestroyWindow(window);
    glfwTerminate();
}
