#include "vk_types.hpp"

#include <GLFW/glfw3.h>
#include <GLFW/glfw3native.h>

#include <glm/glm.hpp>

auto messenger_callback(VkDebugUtilsMessageSeverityFlagBitsEXT severity, VkDebugUtilsMessageTypeFlagsEXT type, const VkDebugUtilsMessengerCallbackDataEXT* data, void* user) -> VkBool32;

auto main() -> i32
{
    i32 width = 1280;
    i32 height = 720;

    glfwSetErrorCallback([](i32 code, const char* desc) {
        std::println(std::cerr, "glfw error ({}): {}", code, desc);
    });

    glfwInit();

    glfwWindowHint(GLFW_CLIENT_API, GLFW_NO_API);
    glfwWindowHint(GLFW_RESIZABLE, GLFW_FALSE);

    GLFWwindow* window = glfwCreateWindow(1280, 720, "RTX", nullptr, nullptr);
    {
        glfwGetFramebufferSize(window, &width, &height);
        std::println("created window \"{}\" ({}, {})", glfwGetWindowTitle(window), width, height);
    }

    VK_CHECK(volkInitialize());

    VkInstance instance;
    VkDebugUtilsMessengerEXT messenger;

    {
        u32 version = volkGetInstanceVersion();

        std::println("vulkan instance: {}.{}.{}",
            VK_VERSION_MAJOR(version),
            VK_VERSION_MINOR(version),
            VK_VERSION_PATCH(version)
        );

        if (version < VK_API_VERSION_1_4) {
            std::println(std::cerr, "vulkan 1.4 required");
        }

        VkApplicationInfo info {
            .sType = VK_STRUCTURE_TYPE_APPLICATION_INFO,
            .pNext = nullptr,
            .pApplicationName = "RTX",
            .applicationVersion = VK_MAKE_VERSION(1, 0, 0),
            .pEngineName = "no engine",
            .engineVersion = VK_MAKE_VERSION(1, 0, 0),
            .apiVersion = VK_API_VERSION_1_4
        };

        std::vector<const char*> layers;
        std::vector<const char*> extensions;

#ifndef NDEBUG
        layers.push_back("VK_LAYER_KHRONOS_validation");
#endif

        extensions.push_back(VK_EXT_DEBUG_UTILS_EXTENSION_NAME);

        {
            u32 glfw_extension_count = 0;
            const char** glfw_extensions = glfwGetRequiredInstanceExtensions(&glfw_extension_count);
            extensions.insert(extensions.end(), glfw_extensions, glfw_extensions + glfw_extension_count);
        }

        VkDebugUtilsMessengerCreateInfoEXT messenger_info {
            .sType = VK_STRUCTURE_TYPE_DEBUG_UTILS_MESSENGER_CREATE_INFO_EXT,
            .pNext = nullptr,
            .flags = 0,
            .messageSeverity = VK_DEBUG_UTILS_MESSAGE_SEVERITY_WARNING_BIT_EXT | VK_DEBUG_UTILS_MESSAGE_SEVERITY_ERROR_BIT_EXT,
            .messageType = VK_DEBUG_UTILS_MESSAGE_TYPE_VALIDATION_BIT_EXT | VK_DEBUG_UTILS_MESSAGE_TYPE_PERFORMANCE_BIT_EXT,
            .pfnUserCallback = messenger_callback,
            .pUserData = nullptr
        };

        VkInstanceCreateInfo instance_info {
            .sType = VK_STRUCTURE_TYPE_INSTANCE_CREATE_INFO,
            .pNext = &messenger_info,
            .flags = 0,
            .pApplicationInfo = &info,
            .enabledLayerCount = static_cast<u32>(layers.size()),
            .ppEnabledLayerNames = layers.data(),
            .enabledExtensionCount = static_cast<u32>(extensions.size()),
            .ppEnabledExtensionNames = extensions.data()
        };

        VK_CHECK(vkCreateInstance(&instance_info, nullptr, &instance));
        volkLoadInstance(instance);

        VK_CHECK(vkCreateDebugUtilsMessengerEXT(instance, &messenger_info, nullptr, &messenger));

        std::println("instance layers:");
        for (const auto& layer : layers) {
            std::println(" - {}", layer);
        }

        std::println("instance extensions:");
        for (const auto& extension : extensions) {
            std::println(" - {}", extension);
        }
    }

    VkSurfaceKHR surface;
    VK_CHECK(glfwCreateWindowSurface(instance, window, nullptr, &surface));

    while (!glfwWindowShouldClose(window)) {
        glfwPollEvents();
    }

    vkDestroySurfaceKHR(instance, surface, nullptr);
    vkDestroyDebugUtilsMessengerEXT(instance, messenger, nullptr);
    vkDestroyInstance(instance, nullptr);

    glfwDestroyWindow(window);
    glfwTerminate();
}

auto messenger_callback(VkDebugUtilsMessageSeverityFlagBitsEXT severity, VkDebugUtilsMessageTypeFlagsEXT type, const VkDebugUtilsMessengerCallbackDataEXT* data, void* user) -> VkBool32
{
    std::stringstream ss;

    if (type & VK_DEBUG_UTILS_MESSAGE_TYPE_GENERAL_BIT_EXT) { ss << "[GENERAL]"; }
    if (type & VK_DEBUG_UTILS_MESSAGE_TYPE_VALIDATION_BIT_EXT) { ss << "[VALIDATION]"; }
    if (type & VK_DEBUG_UTILS_MESSAGE_TYPE_PERFORMANCE_BIT_EXT) { ss << "[PERFORMANCE]"; }
    if (type & VK_DEBUG_UTILS_MESSAGE_TYPE_DEVICE_ADDRESS_BINDING_BIT_EXT) { ss << "[ADDRESS]"; }

    switch (severity) {
        case VK_DEBUG_UTILS_MESSAGE_SEVERITY_VERBOSE_BIT_EXT:   ss << "[VERBOSE]";
        case VK_DEBUG_UTILS_MESSAGE_SEVERITY_INFO_BIT_EXT:      ss << "[INFO]";
        case VK_DEBUG_UTILS_MESSAGE_SEVERITY_WARNING_BIT_EXT:   ss << "[WARNING]";
        case VK_DEBUG_UTILS_MESSAGE_SEVERITY_ERROR_BIT_EXT:     ss << "[ERROR]";
        default: ss << "[UNKNOWN]";
    }

    if (data->pMessageIdName) {
        ss << " (" << data->pMessageIdName << ")";
    }

    ss << ":\n";
    ss << "  message: " << data->pMessage << "\n";

    if (data->objectCount > 0) {
        ss << "  objects (" << data->objectCount << "):\n";

        for (u32 i = 0; i < data->objectCount; ++i) {
            const auto& obj = data->pObjects[i];

            ss << "    - object " << i << ": ";

            if (obj.pObjectName) {
                ss << "name = \"" << obj.pObjectName << "\"";
            } else {
                ss << "handle = " << reinterpret_cast<void*>(obj.objectHandle);
            }

            ss << ", type = " << obj.objectType << "\n";
        }
    }

    if (data->cmdBufLabelCount > 0) {
        ss << "  command buffer labels (" << data->cmdBufLabelCount << "):\n";
        for (u32 i = 0; i < data->cmdBufLabelCount; ++i) {
            ss << "    - " << data->pCmdBufLabels[i].pLabelName << "\n";
        }
    }

    if (data->queueLabelCount > 0) {
        ss << "  queue labels (" << data->queueLabelCount << "):\n";
        for (u32 i = 0; i < data->queueLabelCount; ++i) {
            ss << "    - " << data->pQueueLabels[i].pLabelName << "\n";
        }
    }

    std::println(std::cerr, "{}", ss.str());

    return VK_FALSE;
}
