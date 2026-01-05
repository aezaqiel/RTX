#include "shader.hpp"

#include <PathConfig.inl>

namespace RHI {

    namespace {

        std::filesystem::path s_shaderpath(PathConfig::shader_dir);

        auto read_shader(const std::filesystem::path& filepath) -> std::vector<char>
        {
            std::ifstream file(filepath, std::ios::ate | std::ios::binary);
            if (!file.is_open()) {
                std::println(std::cerr, "failed to open shader: {}", filepath.string());
                return {};
            }

            usize size = file.tellg();

            std::vector<char> src(size);

            file.seekg(SEEK_SET);
            file.read(src.data(), size);

            file.close();

            return src;
        }
    
    }

    Shader::Shader(const std::shared_ptr<Device>& device, const std::string& filename, VkShaderStageFlagBits stage)
        : m_device(device), m_stage(stage)
    {
        auto src = read_shader(s_shaderpath / filename);

        VkShaderModuleCreateInfo shader_info {
            .sType = VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO,
            .pNext = nullptr,
            .flags = 0,
            .codeSize = src.size(),
            .pCode = reinterpret_cast<const u32*>(src.data())
        };

        VK_CHECK(vkCreateShaderModule(device->device(), &shader_info, nullptr, &m_module));

        std::println("loaded shader: {}", filename);
    }

    Shader::~Shader()
    {
        vkDestroyShaderModule(m_device->device(), m_module, nullptr);
    }

}
