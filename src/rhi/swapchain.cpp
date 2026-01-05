#include "swapchain.hpp"

namespace RHI {

    Swapchain::Swapchain(const std::shared_ptr<Context>& context, const std::shared_ptr<Device>& device, VkExtent2D extent)
        : m_context(context), m_device(device)
    {
        recreate(extent);
    }

    Swapchain::~Swapchain()
    {
        destroy_resources();
        vkDestroySwapchainKHR(m_device->device(), m_swapchain, nullptr);
    }

    auto Swapchain::recreate(VkExtent2D request) -> void
    {
        vkGetPhysicalDeviceSurfaceCapabilitiesKHR(m_device->physical(), m_context->surface(), &m_capabilities);

        u32 format_count = 0;
        vkGetPhysicalDeviceSurfaceFormatsKHR(m_device->physical(), m_context->surface(), &format_count, nullptr);
        std::vector<VkSurfaceFormatKHR> available_formats(format_count);
        vkGetPhysicalDeviceSurfaceFormatsKHR(m_device->physical(), m_context->surface(), &format_count, available_formats.data());

        m_surface_format = available_formats[0];
        for (const auto& format : available_formats) {
            if (format.format == VK_FORMAT_R8G8B8A8_SRGB && format.colorSpace == VK_COLOR_SPACE_SRGB_NONLINEAR_KHR) {
                m_surface_format = format;
                break;
            }
        }

        u32 mode_count = 0;
        vkGetPhysicalDeviceSurfacePresentModesKHR(m_device->physical(), m_context->surface(), &mode_count, nullptr);
        std::vector<VkPresentModeKHR> available_modes(mode_count);
        vkGetPhysicalDeviceSurfacePresentModesKHR(m_device->physical(), m_context->surface(), &mode_count, available_modes.data());

        m_present_mode = VK_PRESENT_MODE_FIFO_KHR;
        for (const auto& mode : available_modes) {
            if (mode == VK_PRESENT_MODE_MAILBOX_KHR) {
                m_present_mode = VK_PRESENT_MODE_MAILBOX_KHR;
                break;
            }
        }

        if (m_capabilities.currentExtent.width != std::numeric_limits<u32>::max()) {
            m_extent = m_capabilities.currentExtent;
        } else {
            m_extent = {
                .width = std::clamp(static_cast<u32>(request.width), m_capabilities.minImageExtent.width, m_capabilities.maxImageExtent.width),
                .height = std::clamp(static_cast<u32>(request.height), m_capabilities.minImageExtent.height, m_capabilities.maxImageExtent.height)
            };
        }

        m_image_count = m_capabilities.minImageCount + 1;
        if (m_capabilities.maxImageCount > 0) {
            m_image_count = std::min(m_image_count, m_capabilities.maxImageCount);
        }

        VkSwapchainKHR old = m_swapchain;

        VkSwapchainCreateInfoKHR swapchain_info {
            .sType = VK_STRUCTURE_TYPE_SWAPCHAIN_CREATE_INFO_KHR,
            .pNext = nullptr,
            .flags = 0,
            .surface = m_context->surface(),
            .minImageCount = m_image_count,
            .imageFormat = m_surface_format.format,
            .imageColorSpace = m_surface_format.colorSpace,
            .imageExtent = m_extent,
            .imageArrayLayers = 1,
            .imageUsage = VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT | VK_IMAGE_USAGE_TRANSFER_DST_BIT,
            .imageSharingMode = VK_SHARING_MODE_EXCLUSIVE,
            .queueFamilyIndexCount = 0,
            .pQueueFamilyIndices = nullptr,
            .preTransform = m_capabilities.currentTransform,
            .compositeAlpha = VK_COMPOSITE_ALPHA_OPAQUE_BIT_KHR,
            .presentMode = m_present_mode,
            .clipped = VK_TRUE,
            .oldSwapchain = old
        };

        VK_CHECK(vkCreateSwapchainKHR(m_device->device(), &swapchain_info, nullptr, &m_swapchain));

        if (old != VK_NULL_HANDLE) {
            m_device->wait_idle();
            destroy_resources();

            vkDestroySwapchainKHR(m_device->device(), old, nullptr);
        }

        vkGetSwapchainImagesKHR(m_device->device(), m_swapchain, &m_image_count, nullptr);
        create_resources();

        std::println("swapchain ({}, {}) with {} images", width(), height(), m_image_count);
    }

    auto Swapchain::acquire_wait_info() const -> VkSemaphoreSubmitInfo
    {
        return VkSemaphoreSubmitInfo {
            .sType = VK_STRUCTURE_TYPE_SEMAPHORE_SUBMIT_INFO,
            .pNext = nullptr,
            .semaphore = m_image_acquired_semaphores[m_sync_index],
            .value = 0,
            .stageMask = VK_PIPELINE_STAGE_2_COLOR_ATTACHMENT_OUTPUT_BIT,
            .deviceIndex = 0
        };
    }

    auto Swapchain::present_signal_info() const -> VkSemaphoreSubmitInfo
    {
        return VkSemaphoreSubmitInfo {
            .sType = VK_STRUCTURE_TYPE_SEMAPHORE_SUBMIT_INFO,
            .pNext = nullptr,
            .semaphore = m_present_signal_semaphores[m_image_index],
            .value = 0,
            .stageMask = VK_PIPELINE_STAGE_2_COLOR_ATTACHMENT_OUTPUT_BIT,
            .deviceIndex = 0
        };
    }

    auto Swapchain::acquire_image() -> bool
    {
        VkResult result = vkAcquireNextImageKHR(
            m_device->device(),
            m_swapchain,
            std::numeric_limits<u64>::max(),
            m_image_acquired_semaphores[m_sync_index],
            VK_NULL_HANDLE,
            &m_image_index
        );

        if (result == VK_SUCCESS) return true;
        if (result == VK_ERROR_OUT_OF_DATE_KHR || result == VK_SUBOPTIMAL_KHR) return false;

        VK_CHECK(result);

        return false;
    }

    auto Swapchain::present(VkQueue queue) -> bool
    {
        VkPresentInfoKHR present_info {
            .sType = VK_STRUCTURE_TYPE_PRESENT_INFO_KHR,
            .pNext = nullptr,
            .waitSemaphoreCount = 1,
            .pWaitSemaphores = &m_present_signal_semaphores[m_image_index],
            .swapchainCount = 1,
            .pSwapchains = &m_swapchain,
            .pImageIndices = &m_image_index,
            .pResults = nullptr
        };

        VkResult result = vkQueuePresentKHR(queue, &present_info);

        m_sync_index = (m_sync_index + 1) % m_image_count;

        if (result == VK_SUCCESS) return true;
        if (result == VK_ERROR_OUT_OF_DATE_KHR || result == VK_SUBOPTIMAL_KHR) return false;

        VK_CHECK(result);

        return false;
    }

    auto Swapchain::create_resources() -> void
    {
        m_images.resize(m_image_count);
        m_image_views.resize(m_image_count);
        m_image_acquired_semaphores.resize(m_image_count);
        m_present_signal_semaphores.resize(m_image_count);

        vkGetSwapchainImagesKHR(m_device->device(), m_swapchain, &m_image_count, m_images.data());

        VkImageViewCreateInfo view_info {
            .sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO,
            .pNext = nullptr,
            .flags = 0,
            .image = VK_NULL_HANDLE,
            .viewType = VK_IMAGE_VIEW_TYPE_2D,
            .format = m_surface_format.format,
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

        VkSemaphoreCreateInfo semaphore_info {
            .sType = VK_STRUCTURE_TYPE_SEMAPHORE_CREATE_INFO,
            .pNext = nullptr,
            .flags = 0
        };

        for (usize i = 0; i < m_images.size(); ++i) {
            view_info.image = m_images[i];

            VK_CHECK(vkCreateImageView(m_device->device(), &view_info, nullptr, &m_image_views[i]));
            VK_CHECK(vkCreateSemaphore(m_device->device(), &semaphore_info, nullptr, &m_image_acquired_semaphores[i]));
            VK_CHECK(vkCreateSemaphore(m_device->device(), &semaphore_info, nullptr, &m_present_signal_semaphores[i]));
        }
    }

    auto Swapchain::destroy_resources() -> void
    {
        for (u32 i = 0; i < m_image_count; ++i) {
            vkDestroySemaphore(m_device->device(), m_present_signal_semaphores[i], nullptr);
            vkDestroySemaphore(m_device->device(), m_image_acquired_semaphores[i], nullptr);
            vkDestroyImageView(m_device->device(), m_image_views[i], nullptr);
        }
    }

}
