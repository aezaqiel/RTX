#include "application.hpp"

#include "rhi/barrier.hpp"
#include "rhi/acceleration_structure.hpp"
#include "rhi/shader.hpp"

#include "scene/loader.hpp"

Application::Application()
{
    m_window = std::make_unique<Window>(1280, 720, "RTX");
    m_window->bind_event_callback(BIND_EVENT_FN(Application::dispatch_events));

    m_context = std::make_shared<RHI::Context>(m_window->native());
    m_device = std::make_shared<RHI::Device>(m_context);

    m_swapchain = std::make_unique<RHI::Swapchain>(m_context, m_device, VkExtent2D { m_window->width(), m_window->height() });

    m_graphics_command = std::make_unique<RHI::Command>(m_device, m_device->graphics_index(), s_FramesInFlight);
    m_compute_command = std::make_unique<RHI::Command>(m_device, m_device->compute_index(), s_FramesInFlight);
    m_transfer_command = std::make_unique<RHI::Command>(m_device, m_device->transfer_index(), s_FramesInFlight);

    m_graphics_queue = std::make_unique<RHI::Queue>(m_device, m_device->graphics_index());
    m_compute_queue = std::make_unique<RHI::Queue>(m_device, m_device->compute_index());
    m_transfer_queue = std::make_unique<RHI::Queue>(m_device, m_device->transfer_index());

    m_storage = std::make_unique<RHI::Image>(
        m_device,
        VkExtent3D { m_swapchain->width(), m_swapchain->height(), 1 },
        VK_FORMAT_R32G32B32A32_SFLOAT,
        VK_IMAGE_USAGE_STORAGE_BIT | VK_IMAGE_USAGE_TRANSFER_SRC_BIT
    );

    std::vector<RHI::DescriptorAllocator::PoolSizeRatio> pool_ratios {
        { VK_DESCRIPTOR_TYPE_ACCELERATION_STRUCTURE_KHR, 1.0f },
        { VK_DESCRIPTOR_TYPE_STORAGE_IMAGE, 1.0f }
    };

    for (usize i = 0; i < s_FramesInFlight; ++i) {
        m_descriptor_allocators[i] = std::make_unique<RHI::DescriptorAllocator>(m_device, 64, pool_ratios);
    }
}

Application::~Application()
{
    m_device->wait_idle();
}

auto Application::run() -> void
{
    load_scene();

    auto rt_descriptor_layout = RHI::DescriptorLayout::Builder(m_device)
        .add_binding(0, VK_DESCRIPTOR_TYPE_ACCELERATION_STRUCTURE_KHR, VK_SHADER_STAGE_RAYGEN_BIT_KHR | VK_SHADER_STAGE_CLOSEST_HIT_BIT_KHR)
        .add_binding(1, VK_DESCRIPTOR_TYPE_STORAGE_IMAGE, VK_SHADER_STAGE_RAYGEN_BIT_KHR)
        .build();

    // auto raygen_shader = std::make_unique<RHI::Shader>(m_device, "raygen.rgen.spv", VK_SHADER_STAGE_RAYGEN_BIT_KHR);
    // auto closesthit_shader = std::make_unique<RHI::Shader>(m_device, "closesthit.rchit.spv", VK_SHADER_STAGE_CLOSEST_HIT_BIT_KHR);
    // auto miss_shader = std::make_unique<RHI::Shader>(m_device, "miss.rmiss.spv", VK_SHADER_STAGE_MISS_BIT_KHR);

    while (m_running) {
        Window::poll_events();

        if (!m_minimized) {
            if (m_frame_count >= s_FramesInFlight) {
                u64 wait_value = m_frame_count - s_FramesInFlight + 1;
                m_graphics_queue->sync(wait_value);
            }

            // acquire swapchain image

            if (!m_swapchain->acquire_image()) {
                m_swapchain->recreate(VkExtent2D { m_window->width(), m_window->height() });
                continue;
            }

            u64 frame_index = m_frame_count % s_FramesInFlight;

            // descriptors

            m_descriptor_allocators[frame_index]->reset();

            VkDescriptorSet rt_set = m_descriptor_allocators[frame_index]->allocate(*rt_descriptor_layout);
            RHI::DescriptorWriter(m_device)
                .write_as(0, *m_tlas)
                .write_storage_image(1, *m_storage)
                .update(rt_set);

            // record commands

            auto compute_cmd = m_compute_command->begin();
            m_compute_command->end(compute_cmd);

            auto graphics_cmd = m_graphics_command->begin();
            RHI::BarrierBatch(graphics_cmd)
                .image(m_swapchain->current_image(), VK_PIPELINE_STAGE_2_COLOR_ATTACHMENT_OUTPUT_BIT, VK_ACCESS_2_NONE, VK_PIPELINE_STAGE_2_BOTTOM_OF_PIPE_BIT, VK_ACCESS_2_NONE, VK_IMAGE_LAYOUT_UNDEFINED, VK_IMAGE_LAYOUT_PRESENT_SRC_KHR)
                .insert();
            m_graphics_command->end(graphics_cmd);

            // submit commands

            std::vector<VkSemaphoreSubmitInfo> compute_waits;
            std::vector<VkSemaphoreSubmitInfo> compute_signals;
            u64 compute_signal_value = m_compute_queue->submit(compute_cmd, compute_waits, compute_signals);

            std::vector<VkSemaphoreSubmitInfo> graphics_waits {
                m_swapchain->acquire_wait_info(),
                m_compute_queue->wait_info()
            };

            std::vector<VkSemaphoreSubmitInfo> graphics_signals {
                m_swapchain->present_signal_info()
            };

            u64 graphics_signal_value = m_graphics_queue->submit(graphics_cmd, graphics_waits, graphics_signals);

            // swapchain present

            if (!m_swapchain->present(m_graphics_queue->queue())) {
                m_swapchain->recreate(VkExtent2D { m_window->width(), m_window->height() });
            }

            m_frame_count++;
        }
    }
}

auto Application::load_scene() -> void
{
    std::vector<std::unique_ptr<RHI::Buffer>> staging_buffers;

    auto upload_cmd = m_transfer_command->begin();

    auto sponza = Loader::load_obj("assets/sponza/sponza.obj");
    auto sponza_vb = RHI::Buffer::create_staged(m_device, upload_cmd, sponza.mesh->vertices.data(), sponza.mesh->vertices.size() * sizeof(Vertex), VK_BUFFER_USAGE_ACCELERATION_STRUCTURE_BUILD_INPUT_READ_ONLY_BIT_KHR, staging_buffers);
    auto sponza_ib = RHI::Buffer::create_staged(m_device, upload_cmd, sponza.mesh->indices.data(), sponza.mesh->indices.size() * sizeof(u32), VK_BUFFER_USAGE_ACCELERATION_STRUCTURE_BUILD_INPUT_READ_ONLY_BIT_KHR | VK_BUFFER_USAGE_INDEX_BUFFER_BIT, staging_buffers);

    auto teapot = Loader::load_obj("assets/teapot.obj");
    auto teapot_vb = RHI::Buffer::create_staged(m_device, upload_cmd, teapot.mesh->vertices.data(), teapot.mesh->vertices.size() * sizeof(Vertex), VK_BUFFER_USAGE_ACCELERATION_STRUCTURE_BUILD_INPUT_READ_ONLY_BIT_KHR, staging_buffers);
    auto teapot_ib = RHI::Buffer::create_staged(m_device, upload_cmd, teapot.mesh->indices.data(), teapot.mesh->indices.size() * sizeof(u32), VK_BUFFER_USAGE_ACCELERATION_STRUCTURE_BUILD_INPUT_READ_ONLY_BIT_KHR | VK_BUFFER_USAGE_INDEX_BUFFER_BIT, staging_buffers);

    RHI::BarrierBatch(upload_cmd)
        .buffer(*sponza_vb, VK_PIPELINE_STAGE_2_TRANSFER_BIT, VK_ACCESS_2_TRANSFER_WRITE_BIT, VK_PIPELINE_STAGE_2_ALL_COMMANDS_BIT, VK_ACCESS_2_NONE)
        .buffer(*sponza_ib, VK_PIPELINE_STAGE_2_TRANSFER_BIT, VK_ACCESS_2_TRANSFER_WRITE_BIT, VK_PIPELINE_STAGE_2_ALL_COMMANDS_BIT, VK_ACCESS_2_NONE)
        .buffer(*teapot_vb, VK_PIPELINE_STAGE_2_TRANSFER_BIT, VK_ACCESS_2_TRANSFER_WRITE_BIT, VK_PIPELINE_STAGE_2_ALL_COMMANDS_BIT, VK_ACCESS_2_NONE)
        .buffer(*teapot_ib, VK_PIPELINE_STAGE_2_TRANSFER_BIT, VK_ACCESS_2_TRANSFER_WRITE_BIT, VK_PIPELINE_STAGE_2_ALL_COMMANDS_BIT, VK_ACCESS_2_NONE)
        .insert();

    m_transfer_command->end(upload_cmd);

    std::vector<VkSemaphoreSubmitInfo> upload_signals;
    m_transfer_queue->submit(upload_cmd, {}, upload_signals);

    RHI::AccelerationStructureBuilder as_builder(m_device);

    auto blas_cmd = m_compute_command->begin();

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

    m_compute_command->end(blas_cmd);

    std::vector<VkSemaphoreSubmitInfo> blas_signals;
    u64 blas_timeline = m_compute_queue->submit(blas_cmd, upload_signals, blas_signals);

    auto compact_cmd = m_compute_command->begin();

    m_blases = as_builder.compact_blas(compact_cmd, raw_blases);

    m_compute_command->end(compact_cmd);

    std::vector<VkSemaphoreSubmitInfo> compact_signals;
    u64 compact_timeline = m_compute_queue->submit(compact_cmd, blas_signals, compact_signals);

    auto tlas_cmd = m_compute_command->begin();

    RHI::TLAS::Input tlas_input;
    for (const auto& blas : m_blases) {
        tlas_input.instances.push_back(VkAccelerationStructureInstanceKHR {
            .transform = vkutils::glm_to_vkmatrix(glm::mat4(1.0f)),
            .instanceCustomIndex = 0,
            .mask = 0xFF,
            .instanceShaderBindingTableRecordOffset = 0,
            .flags = 0,
            .accelerationStructureReference = blas->address()
        });
    }

    m_tlas = as_builder.build_tlas(tlas_cmd, tlas_input);

    m_compute_command->end(tlas_cmd);
    
    std::vector<VkSemaphoreSubmitInfo> tlas_signals;
    u64 tlas_timeline = m_compute_queue->submit(tlas_cmd, compact_signals, tlas_signals);

    m_compute_queue->sync(tlas_timeline);
}

auto Application::dispatch_events(const Event& event) -> void
{
    EventDispatcher dispatcher(event);

    dispatcher.dispatch<WindowClosedEvent>([&](const WindowClosedEvent& e) -> bool {
        m_running = false;
        return true;
    });

    dispatcher.dispatch<WindowMinimizeEvent>([&](const WindowMinimizeEvent& e) -> bool {
        m_minimized = e.minimized;
        return false;
    });
}
