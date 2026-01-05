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

    std::println("uploading model and AS build");

    std::unique_ptr<RHI::BLAS> blas;
    std::unique_ptr<RHI::TLAS> tlas;

    {
        std::string model_filename = "assets/sponza/sponza.obj";

        std::println("loading {}", model_filename);
        auto model = Loader::load_obj(model_filename);

        RHI::Buffer vertex_buffer(device, model.mesh->positions.size() * sizeof(glm::vec3),
            VK_BUFFER_USAGE_ACCELERATION_STRUCTURE_BUILD_INPUT_READ_ONLY_BIT_KHR | VK_BUFFER_USAGE_TRANSFER_DST_BIT,
            VMA_MEMORY_USAGE_AUTO_PREFER_DEVICE
        );

        RHI::Buffer vertex_staging(device, vertex_buffer.size(), VK_BUFFER_USAGE_TRANSFER_SRC_BIT, VMA_MEMORY_USAGE_CPU_TO_GPU, VMA_ALLOCATION_CREATE_HOST_ACCESS_SEQUENTIAL_WRITE_BIT);
        vertex_staging.write(model.mesh->positions.data(), vertex_buffer.size());

        RHI::Buffer index_buffer(device, model.mesh->indices.size() * sizeof(u32),
            VK_BUFFER_USAGE_ACCELERATION_STRUCTURE_BUILD_INPUT_READ_ONLY_BIT_KHR | VK_BUFFER_USAGE_TRANSFER_DST_BIT | VK_BUFFER_USAGE_INDEX_BUFFER_BIT,
            VMA_MEMORY_USAGE_AUTO_PREFER_DEVICE
        );

        RHI::Buffer index_staging(device, index_buffer.size(), VK_BUFFER_USAGE_TRANSFER_SRC_BIT, VMA_MEMORY_USAGE_CPU_TO_GPU, VMA_ALLOCATION_CREATE_HOST_ACCESS_SEQUENTIAL_WRITE_BIT);
        index_staging.write(model.mesh->indices.data(), index_buffer.size());

        std::vector<RHI::BLAS::Geometry> geometries {
            RHI::BLAS::Geometry {
                .vertices = {
                    .buffer = &vertex_buffer,
                    .count = static_cast<u32>(model.mesh->positions.size()),
                    .format = VK_FORMAT_R32G32B32_SFLOAT,
                    .stride = sizeof(glm::vec3)
                },
                .indices = {
                    .buffer = &index_buffer,
                    .count = static_cast<u32>(model.mesh->indices.size())
                }
            }
        };

        blas = std::make_unique<RHI::BLAS>(device, geometries);

        std::vector<RHI::TLAS::Instance> instances {
            RHI::TLAS::Instance {
                .blas = blas.get()
            }
        };

        tlas = std::make_unique<RHI::TLAS>(device, instances);

        auto as_cmd = compute_command->begin();
        {
            vertex_buffer.stage(as_cmd, vertex_staging,
                VK_PIPELINE_STAGE_2_ACCELERATION_STRUCTURE_BUILD_BIT_KHR,
                VK_ACCESS_2_ACCELERATION_STRUCTURE_READ_BIT_KHR
            );

            index_buffer.stage(as_cmd, index_staging,
                VK_PIPELINE_STAGE_2_ACCELERATION_STRUCTURE_BUILD_BIT_KHR,
                VK_ACCESS_2_ACCELERATION_STRUCTURE_READ_BIT_KHR
            );

            blas->build(as_cmd);
            tlas->build(as_cmd);
        }
        compute_command->end(as_cmd);

        std::vector<VkSemaphoreSubmitInfo> signals;
        compute_queue->submit(as_cmd, {}, signals);

        compute_queue->sync(signals.back().value);
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
