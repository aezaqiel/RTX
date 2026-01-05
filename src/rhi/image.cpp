#include "image.hpp"

namespace RHI {

    namespace {

        auto image_aspect_from_format(VkFormat format) -> VkImageAspectFlags
        {
            switch (format) {
                case VK_FORMAT_D16_UNORM:
                case VK_FORMAT_D16_UNORM_S8_UINT:
                case VK_FORMAT_D24_UNORM_S8_UINT:
                case VK_FORMAT_D32_SFLOAT_S8_UINT:
                case VK_FORMAT_D32_SFLOAT:
                    return VK_IMAGE_ASPECT_DEPTH_BIT;
                default:
                    return VK_IMAGE_ASPECT_COLOR_BIT;
            }
        }

    }

    Image::Image(const std::shared_ptr<Device>& device, VkExtent3D extent, VkFormat format, VkImageUsageFlags usage, u32 mips, u32 layers)
        : m_device(device), m_extent(extent), m_format(format), m_mips(mips), m_layers(layers), m_owner(true)
    {
        VkImageCreateInfo image_info {
            .sType = VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO,
            .pNext = nullptr,
            .flags = 0,
            .imageType = extent.depth > 1 ? VK_IMAGE_TYPE_3D : VK_IMAGE_TYPE_2D,
            .format = format,
            .extent = extent,
            .mipLevels = mips,
            .arrayLayers = layers,
            .samples = VK_SAMPLE_COUNT_1_BIT,
            .tiling = VK_IMAGE_TILING_OPTIMAL,
            .usage = usage,
            .sharingMode = VK_SHARING_MODE_EXCLUSIVE,
            .queueFamilyIndexCount = 0,
            .pQueueFamilyIndices = nullptr,
            .initialLayout = VK_IMAGE_LAYOUT_UNDEFINED
        };

        VmaAllocationCreateInfo allocation_info {
            .usage = VMA_MEMORY_USAGE_AUTO,
            .priority = 1.0f
        };

        VK_CHECK(vmaCreateImage(device->allocator(), &image_info, &allocation_info, &m_image, &m_allocation, nullptr));

        create_view();
    }

    Image::Image(const std::shared_ptr<Device>& device, VkImage image, VkExtent3D extent, VkFormat format)
        : m_device(device), m_image(image), m_extent(extent), m_format(format), m_owner(false)
    {
        create_view();
    }

    Image::~Image()
    {
        vkDestroyImageView(m_device->device(), m_view, nullptr);
        if (m_owner) vmaDestroyImage(m_device->allocator(), m_image, m_allocation);
    }

    auto Image::create_view() -> void
    {
        VkImageViewCreateInfo view_info {
            .sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO,
            .pNext = nullptr,
            .flags = 0,
            .image = m_image,
            .viewType = m_layers > 1 ? VK_IMAGE_VIEW_TYPE_2D_ARRAY : (m_extent.depth > 1 ? VK_IMAGE_VIEW_TYPE_3D : VK_IMAGE_VIEW_TYPE_2D),
            .format = m_format,
            .components = {
                .r = VK_COMPONENT_SWIZZLE_IDENTITY,
                .g = VK_COMPONENT_SWIZZLE_IDENTITY,
                .b = VK_COMPONENT_SWIZZLE_IDENTITY,
                .a = VK_COMPONENT_SWIZZLE_IDENTITY
            },
            .subresourceRange = {
                .aspectMask = image_aspect_from_format(m_format),
                .baseMipLevel = 0,
                .levelCount = m_mips,
                .baseArrayLayer = 0,
                .layerCount = m_layers
            }
        };

        VK_CHECK(vkCreateImageView(m_device->device(), &view_info, nullptr, &m_view));
    }

}
