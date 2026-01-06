#pragma once

#include "events.hpp"
#include "window.hpp"

#include "rhi/context.hpp"
#include "rhi/device.hpp"
#include "rhi/swapchain.hpp"

#include "rhi/command.hpp"
#include "rhi/queue.hpp"

class Application
{
public:
    Application();
    ~Application();

    auto run() -> void;

private:
    auto dispatch_events(const Event& event) -> void;

private:
    bool m_running { true };
    bool m_minimized { false };

    std::unique_ptr<Window> m_window;

    std::shared_ptr<RHI::Context> m_context;
    std::shared_ptr<RHI::Device> m_device;

    std::unique_ptr<RHI::Swapchain> m_swapchain;

    std::unique_ptr<RHI::Command> m_graphics_command;
    std::unique_ptr<RHI::Command> m_compute_command;
    std::unique_ptr<RHI::Command> m_transfer_command;

    std::unique_ptr<RHI::Queue> m_graphics_queue;
    std::unique_ptr<RHI::Queue> m_compute_queue;
    std::unique_ptr<RHI::Queue> m_transfer_queue;

    u64 m_frame_index { 0 };
};
