#pragma once

#include "vk_types.hpp"
#include "device.hpp"

namespace RHI {

    class Image
    {
    public:
        Image(const std::shared_ptr<Device>& device, VkExtent3D extent, VkFormat format, VkImageUsageFlags usage, u32 mips = 1, u32 layers = 1);
        Image(const std::shared_ptr<Device>& device, VkImage image, VkExtent3D extent, VkFormat format);

        ~Image();

        Image(const Image&) = delete;
        Image& operator=(const Image&) = delete;

        [[nodiscard]] auto image() const -> VkImage { return m_image; }
        [[nodiscard]] auto view() const -> VkImageView { return m_view; }

        [[nodiscard]] auto extent() const -> VkExtent3D { return m_extent; }
        [[nodiscard]] auto width() const -> u32 { return m_extent.width; }
        [[nodiscard]] auto height() const -> u32 { return m_extent.height; }
        [[nodiscard]] auto depth() const -> u32 { return m_extent.depth; }

        [[nodiscard]] auto format() const -> VkFormat { return m_format; }
        [[nodiscard]] auto aspect() const -> VkImageAspectFlags { return m_aspect; }

        [[nodiscard]] auto mips() const -> u32 { return m_mips; }
        [[nodiscard]] auto layers() const -> u32 { return m_layers; }

    private:
        auto create_view() -> void;

    private:
        std::shared_ptr<Device> m_device;

        bool m_owner { false };

        VkImage m_image { VK_NULL_HANDLE };
        VkImageView m_view { VK_NULL_HANDLE };
        VmaAllocation m_allocation { VK_NULL_HANDLE };

        VkExtent3D m_extent { 0, 0, 0 };

        VkFormat m_format { VK_FORMAT_UNDEFINED };
        VkImageAspectFlags m_aspect { VK_IMAGE_ASPECT_NONE };

        u32 m_mips { 1 };
        u32 m_layers { 1 };
    };

}
