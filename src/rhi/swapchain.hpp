#pragma once

#include "vk_types.hpp"
#include "context.hpp"
#include "device.hpp"

namespace RHI {

    class Swapchain
    {
    public:
        Swapchain(const std::shared_ptr<Context>& context, const std::shared_ptr<Device>& device, VkExtent2D extent);
        ~Swapchain();

        Swapchain(const Swapchain&) = delete;
        Swapchain& operator=(const Swapchain&) = delete;

        [[nodiscard]] auto swapchain() const -> VkSwapchainKHR { return m_swapchain; }

        [[nodiscard]] auto extent() const -> VkExtent2D { return m_extent; }
        [[nodiscard]] auto width()  const -> u32 { return m_extent.width; }
        [[nodiscard]] auto height() const -> u32 { return m_extent.height; }

        [[nodiscard]] auto surface_capabilities() const -> VkSurfaceCapabilitiesKHR { return m_capabilities; }
        [[nodiscard]] auto surface_format() const -> VkSurfaceFormatKHR { return m_surface_format; }
        [[nodiscard]] auto present_mode() const -> VkPresentModeKHR { return m_present_mode; }

        [[nodiscard]] auto current_image() const -> VkImage { return m_images.at(m_image_index); }

        auto recreate(VkExtent2D request) -> void;

        auto acquire_wait_info() const -> VkSemaphoreSubmitInfo;
        auto present_signal_info() const -> VkSemaphoreSubmitInfo;

        auto acquire_image() -> bool;
        auto present(VkQueue queue) -> bool;

    private:
        auto create_resources() -> void;
        auto destroy_resources() -> void;

    private:
        std::shared_ptr<Context> m_context;
        std::shared_ptr<Device> m_device;

        VkSwapchainKHR m_swapchain { VK_NULL_HANDLE };

        VkSurfaceCapabilitiesKHR m_capabilities;
        VkSurfaceFormatKHR m_surface_format;
        VkPresentModeKHR m_present_mode;
        VkExtent2D m_extent { 0, 0 };

        u32 m_image_count { 0 };
        std::vector<VkImage> m_images;
        std::vector<VkImageView> m_image_views;

        std::vector<VkSemaphore> m_image_acquired_semaphores;
        std::vector<VkSemaphore> m_present_signal_semaphores;

        u32 m_image_index { 0 };
        u32 m_sync_index  { 0 };
    };

}
