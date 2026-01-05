#pragma once

#include "vk_types.hpp"
#include "device.hpp"

namespace RHI {

    class Shader
    {
    public:
        Shader(const std::shared_ptr<Device>& device, const std::string& filename, VkShaderStageFlagBits stage);
        ~Shader();

        [[nodiscard]] auto module() const -> VkShaderModule { return m_module; }
        [[nodiscard]] auto stage_info() const -> VkPipelineShaderStageCreateInfo
        {
            return VkPipelineShaderStageCreateInfo {
                .sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO,
                .pNext = nullptr,
                .flags = 0,
                .stage = m_stage,
                .module = m_module,
                .pName = "main",
                .pSpecializationInfo = nullptr
            };
        }

    private:
        std::shared_ptr<Device> m_device;

        VkShaderModule m_module { VK_NULL_HANDLE };
        VkShaderStageFlagBits m_stage { VK_SHADER_STAGE_ALL };
    };

}
