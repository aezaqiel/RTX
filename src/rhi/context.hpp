#pragma once

#include "vk_types.hpp"

struct GLFWwindow;

namespace RHI {

    class Context
    {
    public:
        Context(GLFWwindow* window);
        ~Context();

        Context(const Context&) = delete;
        Context& operator=(const Context&) = delete;

        [[nodiscard]] auto instance() -> VkInstance { return m_instance; }
        [[nodiscard]] auto surface() -> VkSurfaceKHR { return m_surface; }

    private:
        VkInstance m_instance { VK_NULL_HANDLE };
        VkDebugUtilsMessengerEXT m_messenger { VK_NULL_HANDLE };

        VkSurfaceKHR m_surface { VK_NULL_HANDLE };
    };

}
