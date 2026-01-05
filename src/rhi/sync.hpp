#pragma once

#include "vk_types.hpp"
#include "command.hpp"

struct FrameContext
{
    CommandContext graphics;
    CommandContext compute;

    static auto create(VkDevice device, u32 graphics, u32 compute) -> FrameContext
    {
        FrameContext context;
        context.graphics = CommandContext::create(device, graphics);
        context.compute = CommandContext::create(device, compute);

        return context;
    }

    auto destroy(VkDevice device) -> void
    {
        compute.destroy(device);
        graphics.destroy(device);
    }
};
