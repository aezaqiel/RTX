#include "descriptor.hpp"

namespace RHI {

    DescriptorLayout::Builder::Builder(const std::shared_ptr<Device>& device)
        : m_device(device)
    {
    }

    auto DescriptorLayout::Builder::add_binding(u32 binding, VkDescriptorType type, VkShaderStageFlags stage, u32 count) -> Builder&
    {
        m_bindings.push_back(Binding {
            .binding = binding,
            .type = type,
            .stage = stage,
            .count = count
        });

        return *this;
    }

    auto DescriptorLayout::Builder::build() -> std::unique_ptr<DescriptorLayout>
    {
        return std::make_unique<DescriptorLayout>(m_device, m_bindings);
    }

    DescriptorLayout::DescriptorLayout(const std::shared_ptr<Device>& device, std::span<Binding> bindings)
        : m_device(device)
    {
        std::vector<VkDescriptorSetLayoutBinding> set_bindings;
        set_bindings.reserve(bindings.size());

        for (const auto& binding : bindings) {
            set_bindings.push_back(VkDescriptorSetLayoutBinding {
                .binding = binding.binding,
                .descriptorType = binding.type,
                .descriptorCount = binding.count,
                .stageFlags = binding.stage,
                .pImmutableSamplers = nullptr
            });
        }

        VkDescriptorSetLayoutCreateInfo layout_info {
            .sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO,
            .pNext = nullptr,
            .flags = 0,
            .bindingCount = static_cast<u32>(set_bindings.size()),
            .pBindings = set_bindings.data()
        };

        VK_CHECK(vkCreateDescriptorSetLayout(device->device(), &layout_info, nullptr, &m_layout));
    }

    DescriptorLayout::~DescriptorLayout()
    {
        vkDestroyDescriptorSetLayout(m_device->device(), m_layout, nullptr);
    }

    DescriptorAllocator::DescriptorAllocator(const std::shared_ptr<Device>& device, u32 max_sets, std::span<PoolSizeRatio> pool_ratios)
        : m_device(device), m_sets_per_pool(max_sets)
    {
        if (pool_ratios.empty()) {
            m_ratios.push_back({ VK_DESCRIPTOR_TYPE_STORAGE_IMAGE, 1.0F });
            m_ratios.push_back({ VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER, 1.0F });
            m_ratios.push_back({ VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, 1.0F });
        } else {
            m_ratios.assign(pool_ratios.begin(), pool_ratios.end());
        }

        m_current_pool = create_pool(m_sets_per_pool, 0);
        m_used_pools.push_back(m_current_pool);
    }

    DescriptorAllocator::~DescriptorAllocator()
    {
        for (auto& p : m_used_pools) vkDestroyDescriptorPool(m_device->device(), p, nullptr);
        for (auto& p : m_free_pools) vkDestroyDescriptorPool(m_device->device(), p, nullptr);
    }

    auto DescriptorAllocator::allocate(const DescriptorLayout& layout) -> VkDescriptorSet
    {
        auto vk_layout = layout.layout();

        VkDescriptorSetAllocateInfo allocate_info {
            .sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO,
            .pNext = nullptr,
            .descriptorPool = get_pool(),
            .descriptorSetCount = 1,
            .pSetLayouts = &vk_layout
        };

        VkDescriptorSet set;
        VkResult result = vkAllocateDescriptorSets(m_device->device(), &allocate_info, &set);

        if (result == VK_ERROR_OUT_OF_POOL_MEMORY || result == VK_ERROR_FRAGMENTED_POOL) {
            m_current_pool = VK_NULL_HANDLE;
            allocate_info.descriptorPool = get_pool();

            VK_CHECK(vkAllocateDescriptorSets(m_device->device(), &allocate_info, &set));
        }

        return set;
    }

    auto DescriptorAllocator::reset() -> void
    {
        for (auto p : m_used_pools) {
            vkResetDescriptorPool(m_device->device(), p, 0);
            m_free_pools.push_back(p);
        }

        m_used_pools.clear();
        m_current_pool = VK_NULL_HANDLE;
    }

    auto DescriptorAllocator::get_pool() -> VkDescriptorPool
    {
        if (m_current_pool != VK_NULL_HANDLE) {
            return m_current_pool;
        }

        if (!m_free_pools.empty()) {
            m_current_pool = m_free_pools.back();
            m_free_pools.pop_back();

            m_used_pools.push_back(m_current_pool);
            return m_current_pool;
        }

        m_current_pool = create_pool(m_sets_per_pool, 0);
        m_used_pools.push_back(m_current_pool);

        return m_current_pool;
    }

    auto DescriptorAllocator::create_pool(u32 count, VkDescriptorPoolCreateFlags flags) -> VkDescriptorPool
    {
        std::vector<VkDescriptorPoolSize> sizes;
        sizes.reserve(m_ratios.size());

        for (const auto& ratio : m_ratios) {
            sizes.push_back(VkDescriptorPoolSize {
                .type = ratio.type,
                .descriptorCount = static_cast<u32>(ratio.ratio * count)
            });
        }

        VkDescriptorPoolCreateInfo pool_info {
            .sType = VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO,
            .pNext = nullptr,
            .flags = 0,
            .maxSets = count,
            .poolSizeCount = static_cast<u32>(sizes.size()),
            .pPoolSizes = sizes.data()
        };

        VkDescriptorPool pool;
        VK_CHECK(vkCreateDescriptorPool(m_device->device(), &pool_info, nullptr, &pool));

        return pool;
    }

    DescriptorWriter::DescriptorWriter(const std::shared_ptr<Device>& device)
        : m_device(device)
    {
    }

    DescriptorWriter& DescriptorWriter::write_buffer(u32 binding, const Buffer& buffer, u64 offset, u64 range, VkDescriptorType type)
    {
        auto& info = m_buffers.emplace_back(VkDescriptorBufferInfo {
            .buffer = buffer.buffer(),
            .offset = offset,
            .range = range
        });

        m_writes.push_back(VkWriteDescriptorSet {
            .sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET,
            .pNext = nullptr,
            .dstSet = VK_NULL_HANDLE,
            .dstBinding = binding,
            .dstArrayElement = 0,
            .descriptorCount = 1,
            .descriptorType = type,
            .pImageInfo = nullptr,
            .pBufferInfo = &info,
            .pTexelBufferView = nullptr
        });

        return *this;
    }

    DescriptorWriter& DescriptorWriter::write_image(u32 binding, const Image& image, VkSampler sampler, VkImageLayout layout, VkDescriptorType type)
    {
        auto& info = m_images.emplace_back(VkDescriptorImageInfo {
            .sampler = sampler,
            .imageView = image.view(),
            .imageLayout = layout
        });

        m_writes.push_back(VkWriteDescriptorSet {
            .sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET,
            .pNext = nullptr,
            .dstSet = VK_NULL_HANDLE,
            .dstBinding = binding,
            .dstArrayElement = 0,
            .descriptorCount = 1,
            .descriptorType = type,
            .pImageInfo = &info,
            .pBufferInfo = nullptr,
            .pTexelBufferView = nullptr
        });

        return *this;
    }

    DescriptorWriter& DescriptorWriter::write_storage_image(u32 binding, const Image& image, VkImageLayout layout)
    {
        auto& info = m_images.emplace_back(VkDescriptorImageInfo {
            .sampler = VK_NULL_HANDLE,
            .imageView = image.view(),
            .imageLayout = layout
        });

        m_writes.push_back(VkWriteDescriptorSet {
            .sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET,
            .pNext = nullptr,
            .dstSet = VK_NULL_HANDLE,
            .dstBinding = binding,
            .dstArrayElement = 0,
            .descriptorCount = 1,
            .descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_IMAGE,
            .pImageInfo = &info,
            .pBufferInfo = nullptr,
            .pTexelBufferView = nullptr
        });

        return *this;
    }

    DescriptorWriter& DescriptorWriter::write_as(u32 binding, const AccelerationStructure& as)
    {
        auto& accel = m_accelerations.emplace_back(as.as());

        auto& info = m_acceleration_infos.emplace_back(VkWriteDescriptorSetAccelerationStructureKHR {
            .sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET_ACCELERATION_STRUCTURE_KHR,
            .pNext = nullptr,
            .accelerationStructureCount = 1,
            .pAccelerationStructures = &accel
        });

        m_writes.push_back(VkWriteDescriptorSet {
            .sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET,
            .pNext = &info,
            .dstSet = VK_NULL_HANDLE,
            .dstBinding = binding,
            .dstArrayElement = 0,
            .descriptorCount = 1,
            .descriptorType = VK_DESCRIPTOR_TYPE_ACCELERATION_STRUCTURE_KHR,
            .pImageInfo = nullptr,
            .pBufferInfo = nullptr,
            .pTexelBufferView = nullptr
        });

        return *this;
    }

    auto DescriptorWriter::update(VkDescriptorSet set) -> void
    {
        for (auto& write : m_writes) {
            write.dstSet = set;
        }

        vkUpdateDescriptorSets(m_device->device(), static_cast<u32>(m_writes.size()), m_writes.data(), 0, nullptr);
        clear();
    }

    auto DescriptorWriter::push(VkCommandBuffer cmd, VkPipelineBindPoint bind, VkPipelineLayout layout) -> void
    {
        vkCmdPushDescriptorSet(cmd, bind, layout, 0, static_cast<u32>(m_writes.size()), m_writes.data());
        clear();
    }

    auto DescriptorWriter::clear() -> void
    {
        m_writes.clear();

        m_buffers.clear();
        m_images.clear();
        m_accelerations.clear();
        m_acceleration_infos.clear();
    }

}
