#include <GLFW/glfw3.h>
#include <GLFW/glfw3native.h>

#include <glm/glm.hpp>

#include "rhi/context.hpp"
#include "rhi/device.hpp"
#include "rhi/swapchain.hpp"
#include "rhi/buffer.hpp"
#include "rhi/barrier.hpp"
#include "rhi/queue.hpp"
#include "rhi/command.hpp"
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

    std::vector<std::unique_ptr<RHI::Buffer>> staging_buffers;

    auto upload_cmd = transfer_command->begin();

    auto sponza = Loader::load_obj("assets/sponza/sponza.obj");

    u64 sponza_vb_size = sponza.mesh->vertices.size() * sizeof(Vertex);
    u64 sponza_ib_size = sponza.mesh->indices.size() * sizeof(u32);

    auto sponza_vb = RHI::Buffer::create_staged(device, upload_cmd, sponza.mesh->vertices.data(), sponza_vb_size, VK_BUFFER_USAGE_ACCELERATION_STRUCTURE_BUILD_INPUT_READ_ONLY_BIT_KHR, staging_buffers);
    auto sponza_ib = RHI::Buffer::create_staged(device, upload_cmd, sponza.mesh->indices.data(), sponza_ib_size, VK_BUFFER_USAGE_ACCELERATION_STRUCTURE_BUILD_INPUT_READ_ONLY_BIT_KHR | VK_BUFFER_USAGE_INDEX_BUFFER_BIT, staging_buffers);

    auto teapot = Loader::load_obj("assets/teapot.obj");

    u64 teapot_vb_size = teapot.mesh->vertices.size() * sizeof(Vertex);
    u64 teapot_ib_size = teapot.mesh->indices.size() * sizeof(u32);

    auto teapot_vb = RHI::Buffer::create_staged(device, upload_cmd, teapot.mesh->vertices.data(), teapot_vb_size, VK_BUFFER_USAGE_ACCELERATION_STRUCTURE_BUILD_INPUT_READ_ONLY_BIT_KHR, staging_buffers);
    auto teapot_ib = RHI::Buffer::create_staged(device, upload_cmd, teapot.mesh->indices.data(), teapot_ib_size, VK_BUFFER_USAGE_ACCELERATION_STRUCTURE_BUILD_INPUT_READ_ONLY_BIT_KHR | VK_BUFFER_USAGE_INDEX_BUFFER_BIT, staging_buffers);

    RHI::BarrierBatch(upload_cmd)
        .buffer(*sponza_vb, VK_PIPELINE_STAGE_2_TRANSFER_BIT, VK_ACCESS_2_TRANSFER_WRITE_BIT, VK_PIPELINE_STAGE_2_ALL_COMMANDS_BIT, VK_ACCESS_2_NONE)
        .buffer(*sponza_ib, VK_PIPELINE_STAGE_2_TRANSFER_BIT, VK_ACCESS_2_TRANSFER_WRITE_BIT, VK_PIPELINE_STAGE_2_ALL_COMMANDS_BIT, VK_ACCESS_2_NONE)
        .buffer(*teapot_vb, VK_PIPELINE_STAGE_2_TRANSFER_BIT, VK_ACCESS_2_TRANSFER_WRITE_BIT, VK_PIPELINE_STAGE_2_ALL_COMMANDS_BIT, VK_ACCESS_2_NONE)
        .buffer(*teapot_ib, VK_PIPELINE_STAGE_2_TRANSFER_BIT, VK_ACCESS_2_TRANSFER_WRITE_BIT, VK_PIPELINE_STAGE_2_ALL_COMMANDS_BIT, VK_ACCESS_2_NONE)
        .insert();

    transfer_command->end(upload_cmd);

    std::vector<VkSemaphoreSubmitInfo> upload_signals;
    transfer_queue->submit(upload_cmd, {}, upload_signals);

    RHI::AccelerationStructureBuilder as_builder(device);

    auto blas_cmd = compute_command->begin();

    // take ownership

    RHI::BarrierBatch(blas_cmd)
        .buffer(*sponza_vb, VK_PIPELINE_STAGE_2_ALL_COMMANDS_BIT, VK_ACCESS_2_NONE, VK_PIPELINE_STAGE_2_ACCELERATION_STRUCTURE_BUILD_BIT_KHR, VK_ACCESS_2_ACCELERATION_STRUCTURE_READ_BIT_KHR)
        .buffer(*sponza_ib, VK_PIPELINE_STAGE_2_ALL_COMMANDS_BIT, VK_ACCESS_2_NONE, VK_PIPELINE_STAGE_2_ACCELERATION_STRUCTURE_BUILD_BIT_KHR, VK_ACCESS_2_ACCELERATION_STRUCTURE_READ_BIT_KHR)
        .buffer(*teapot_vb, VK_PIPELINE_STAGE_2_ALL_COMMANDS_BIT, VK_ACCESS_2_NONE, VK_PIPELINE_STAGE_2_ACCELERATION_STRUCTURE_BUILD_BIT_KHR, VK_ACCESS_2_ACCELERATION_STRUCTURE_READ_BIT_KHR)
        .buffer(*teapot_ib, VK_PIPELINE_STAGE_2_ALL_COMMANDS_BIT, VK_ACCESS_2_NONE, VK_PIPELINE_STAGE_2_ACCELERATION_STRUCTURE_BUILD_BIT_KHR, VK_ACCESS_2_ACCELERATION_STRUCTURE_READ_BIT_KHR)
        .insert();

    RHI::BLAS::Input sponza_blas_input;
    sponza_blas_input.add_geometry(*sponza_vb, sponza.mesh->vertices.size(), sizeof(Vertex), *sponza_ib, sponza.mesh->indices.size());

    RHI::BLAS::Input teapot_blas_input;
    teapot_blas_input.add_geometry(*teapot_vb, teapot.mesh->vertices.size(), sizeof(Vertex), *teapot_ib, teapot.mesh->indices.size());

    auto raw_blases = as_builder.build_blas(blas_cmd, {
        sponza_blas_input,
        teapot_blas_input
    });

    compute_command->end(blas_cmd);

    std::vector<VkSemaphoreSubmitInfo> blas_signals;
    u64 blas_timeline = compute_queue->submit(blas_cmd, upload_signals, blas_signals);

    auto compact_cmd = compute_command->begin();

    auto blases = as_builder.compact_blas(compact_cmd, raw_blases);

    compute_command->end(compact_cmd);

    std::vector<VkSemaphoreSubmitInfo> compact_signals;
    u64 compact_timeline = compute_queue->submit(compact_cmd, blas_signals, compact_signals);

    auto tlas_cmd = compute_command->begin();

    RHI::TLAS::Input tlas_input;
    for (const auto& blas : blases) {
        tlas_input.instances.push_back(VkAccelerationStructureInstanceKHR {
            .transform = vkutils::glm_to_vkmatrix(glm::mat4(1.0f)),
            .instanceCustomIndex = 0,
            .mask = 0xFF,
            .instanceShaderBindingTableRecordOffset = 0,
            .flags = 0,
            .accelerationStructureReference = blas->address()
        });
    }

    auto tlas = as_builder.build_tlas(tlas_cmd, tlas_input);

    compute_command->end(tlas_cmd);
    
    std::vector<VkSemaphoreSubmitInfo> tlas_signals;
    u64 tlas_timeline = compute_queue->submit(tlas_cmd, compact_signals, tlas_signals);

    compute_queue->sync(tlas_timeline);

    staging_buffers.clear();
    raw_blases.clear();
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
        RHI::BarrierBatch(graphics_cmd)
            .image(swapchain->current_image(), VK_PIPELINE_STAGE_2_COLOR_ATTACHMENT_OUTPUT_BIT, VK_ACCESS_2_NONE, VK_PIPELINE_STAGE_2_BOTTOM_OF_PIPE_BIT, VK_ACCESS_2_NONE, VK_IMAGE_LAYOUT_UNDEFINED, VK_IMAGE_LAYOUT_PRESENT_SRC_KHR)
            .insert();
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
