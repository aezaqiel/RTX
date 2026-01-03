#include "vk_types.hpp"

#include <GLFW/glfw3.h>
#include <GLFW/glfw3native.h>

#include <glm/glm.hpp>

auto messenger_callback(VkDebugUtilsMessageSeverityFlagBitsEXT severity, VkDebugUtilsMessageTypeFlagsEXT type, const VkDebugUtilsMessengerCallbackDataEXT* data, void* user) -> VkBool32;

namespace {

    constexpr usize FRAMES_IN_FLIGHT = 3;

    struct CommandContext
    {
        VkCommandPool pool;
        VkCommandBuffer buffer;
        VkSemaphore semaphore;

        static auto create(VkDevice device, u32 queue_index) -> CommandContext
        {
            CommandContext context;

            VkCommandPoolCreateInfo pool_info {
                .sType = VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO,
                .pNext = nullptr,
                .flags = VK_COMMAND_POOL_CREATE_TRANSIENT_BIT,
                .queueFamilyIndex = queue_index
            };
            VK_CHECK(vkCreateCommandPool(device, &pool_info, nullptr, &context.pool));

            VkCommandBufferAllocateInfo buffer_allocation {
                .sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO,
                .pNext = nullptr,
                .commandPool = context.pool,
                .level = VK_COMMAND_BUFFER_LEVEL_PRIMARY,
                .commandBufferCount = 1
            };
            VK_CHECK(vkAllocateCommandBuffers(device, &buffer_allocation, &context.buffer));

            VkSemaphoreCreateInfo semaphore_info {
                .sType = VK_STRUCTURE_TYPE_SEMAPHORE_CREATE_INFO,
                .pNext = nullptr,
                .flags = 0
            };
            VK_CHECK(vkCreateSemaphore(device, &semaphore_info, nullptr, &context.semaphore));

            return context;
        }

        auto destroy(VkDevice device) -> void
        {
            vkDestroySemaphore(device, semaphore, nullptr);
            vkDestroyCommandPool(device, pool, nullptr);
        }

        auto record(VkDevice device, std::function<void(VkCommandBuffer)>&& function) -> void
        {
            VK_CHECK(vkResetCommandPool(device, pool, 0));

            VkCommandBufferBeginInfo begin_info {
                .sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO,
                .pNext = nullptr,
                .flags = 0,
                .pInheritanceInfo = nullptr
            };

            VK_CHECK(vkBeginCommandBuffer(buffer, &begin_info));
            function(buffer);
            VK_CHECK(vkEndCommandBuffer(buffer));
        }

        auto submit(VkDevice device, VkQueue queue, const std::vector<VkSemaphore>& wait = {}, const std::vector<VkPipelineStageFlags>& stages = {}) -> void
        {
            VkSubmitInfo submit_info {
                .sType = VK_STRUCTURE_TYPE_SUBMIT_INFO,
                .pNext = nullptr,
                .waitSemaphoreCount = static_cast<u32>(wait.size()),
                .pWaitSemaphores = wait.data(),
                .pWaitDstStageMask = stages.data(),
                .commandBufferCount = 1,
                .pCommandBuffers = &buffer,
                .signalSemaphoreCount = 1,
                .pSignalSemaphores = &semaphore
            };

            VK_CHECK(vkQueueSubmit(queue, 1, &submit_info, VK_NULL_HANDLE));
        }
    };

    struct FrameContext
    {
        VkSemaphore image_available;
        VkFence fence;

        CommandContext graphics;
        CommandContext compute;
        CommandContext transfer;

        static auto create(VkDevice device, u32 graphics, u32 compute, u32 transfer) -> FrameContext
        {
            FrameContext context;

            VkSemaphoreCreateInfo semaphore_info {
                .sType = VK_STRUCTURE_TYPE_SEMAPHORE_CREATE_INFO,
                .pNext = nullptr,
                .flags = 0
            };

            VkFenceCreateInfo fence_info {
                .sType = VK_STRUCTURE_TYPE_FENCE_CREATE_INFO,
                .pNext = nullptr,
                .flags = VK_FENCE_CREATE_SIGNALED_BIT
            };

            VK_CHECK(vkCreateSemaphore(device, &semaphore_info, nullptr, &context.image_available));
            VK_CHECK(vkCreateFence(device, &fence_info, nullptr, &context.fence));

            context.graphics = CommandContext::create(device, graphics);
            context.compute = CommandContext::create(device, compute);
            context.transfer = CommandContext::create(device, transfer);

            return context;
        }

        auto destroy(VkDevice device) -> void
        {
            vkDestroySemaphore(device, image_available, nullptr);
            vkDestroyFence(device, fence, nullptr);

            transfer.destroy(device);
            compute.destroy(device);
            graphics.destroy(device);
        }
    };

}

auto main() -> i32
{
    i32 window_width = 1280;
    i32 window_height = 720;

    glfwSetErrorCallback([](i32 code, const char* desc) {
        std::println(std::cerr, "glfw error ({}): {}", code, desc);
    });

    glfwInit();

    glfwWindowHint(GLFW_CLIENT_API, GLFW_NO_API);
    glfwWindowHint(GLFW_RESIZABLE, GLFW_FALSE);

    GLFWwindow* window = glfwCreateWindow(1280, 720, "RTX", nullptr, nullptr);
    {
        glfwGetFramebufferSize(window, &window_width, &window_height);
        std::println("created window \"{}\" ({}, {})", glfwGetWindowTitle(window), window_width, window_height);
    }

    VK_CHECK(volkInitialize());

    VkInstance instance;
    VkDebugUtilsMessengerEXT messenger;

    { // instance creation
        u32 version = volkGetInstanceVersion();

        std::println("vulkan instance : {}.{}.{}",
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

    VkPhysicalDevice physical_device;

    u32 graphics_queue_index = 0;
    u32 compute_queue_index  = 0;
    u32 transfer_queue_index = 0;

    VkPhysicalDeviceAccelerationStructurePropertiesKHR as_props {
        .sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_ACCELERATION_STRUCTURE_PROPERTIES_KHR
    };

    VkPhysicalDeviceRayTracingPipelinePropertiesKHR rt_props {
        .sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_RAY_TRACING_PIPELINE_PROPERTIES_KHR,
        .pNext = &as_props
    };

    VkPhysicalDeviceProperties2 device_props {
        .sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_PROPERTIES_2,
        .pNext = &rt_props
    };

    { // physical device selection
        u32 device_count = 0;
        vkEnumeratePhysicalDevices(instance, &device_count, nullptr);
        std::vector<VkPhysicalDevice> available_devices(device_count);
        vkEnumeratePhysicalDevices(instance, &device_count, available_devices.data());

        for (const auto& device : available_devices) {
            VkPhysicalDeviceProperties props;
            vkGetPhysicalDeviceProperties(device, &props);

            if (!(props.deviceType == VK_PHYSICAL_DEVICE_TYPE_DISCRETE_GPU)) {
                continue;
            }

            u32 queue_count = 0;
            vkGetPhysicalDeviceQueueFamilyProperties(device, &queue_count, nullptr);
            std::vector<VkQueueFamilyProperties> available_queues(queue_count);
            vkGetPhysicalDeviceQueueFamilyProperties(device, &queue_count, available_queues.data());

            std::optional<u32> graphics;
            std::optional<u32> compute;
            std::optional<u32> transfer;

            u32 queue_index = 0;
            for (const auto& queue : available_queues) {
                VkBool32 present = VK_FALSE;
                vkGetPhysicalDeviceSurfaceSupportKHR(device, queue_index, surface, &present);

                if ((queue.queueFlags & VK_QUEUE_GRAPHICS_BIT) && present == VK_TRUE) {
                    if (!graphics.has_value()) graphics = queue_index;
                }

                if ((queue.queueFlags & VK_QUEUE_COMPUTE_BIT) && !(queue.queueFlags & VK_QUEUE_GRAPHICS_BIT)) {
                    if (!compute.has_value()) compute = queue_index;
                }

                if ((queue.queueFlags & VK_QUEUE_TRANSFER_BIT) && !(queue.queueFlags & VK_QUEUE_COMPUTE_BIT) && !(queue.queueFlags & VK_QUEUE_GRAPHICS_BIT)) {
                    if (!transfer.has_value()) transfer = queue_index;
                }

                queue_index++;
            }

            if (graphics.has_value() && compute.has_value() && transfer.has_value()) {
                physical_device = device;

                graphics_queue_index = graphics.value();
                compute_queue_index = compute.value();
                transfer_queue_index = transfer.value();

                vkGetPhysicalDeviceProperties2(physical_device, &device_props);

                std::println("physical device : {}", device_props.properties.deviceName);
                std::println("graphics queue index : {}", graphics_queue_index);
                std::println("compute queue index  : {}", compute_queue_index);
                std::println("transfer queue index : {}", transfer_queue_index);

                break;
            }
        }
    }

    VkDevice device;
    VmaAllocator allocator;

    VkQueue graphics_queue;
    VkQueue compute_queue;
    VkQueue transfer_queue;

    { // device creation
        std::set<u32> indices { graphics_queue_index, compute_queue_index, transfer_queue_index };

        std::vector<VkDeviceQueueCreateInfo> queue_infos;
        queue_infos.reserve(indices.size());

        for (u32 index : indices) {
            f32 priority = 1.0f;
            queue_infos.push_back(VkDeviceQueueCreateInfo {
                .sType = VK_STRUCTURE_TYPE_DEVICE_QUEUE_CREATE_INFO,
                .pNext = nullptr,
                .flags = 0,
                .queueFamilyIndex = index,
                .queueCount = 1,
                .pQueuePriorities = &priority
            });
        }

        VkPhysicalDeviceSwapchainMaintenance1FeaturesKHR swapchain_maintenance {
            .sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_SWAPCHAIN_MAINTENANCE_1_FEATURES_KHR,
            .pNext = nullptr,
            .swapchainMaintenance1 = VK_TRUE
        };

        VkPhysicalDeviceVulkan14Features features14 {
            .sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_VULKAN_1_4_FEATURES,
            .pNext = &swapchain_maintenance,
            .pushDescriptor = VK_TRUE
        };

        VkPhysicalDeviceVulkan13Features features13 {
            .sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_VULKAN_1_3_FEATURES,
            .pNext = &features14,
            .synchronization2 = VK_TRUE,
            .dynamicRendering = VK_TRUE
        };

        VkPhysicalDeviceVulkan12Features features12 {
            .sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_VULKAN_1_2_FEATURES,
            .pNext = &features13,
            .scalarBlockLayout = VK_TRUE,
            .timelineSemaphore = VK_TRUE,
            .bufferDeviceAddress = VK_TRUE
        };

        VkPhysicalDeviceVulkan11Features features11 {
            .sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_VULKAN_1_1_FEATURES,
            .pNext = &features12
        };

        VkPhysicalDeviceFeatures2 features {
            .sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_FEATURES_2,
            .pNext = &features11,
            .features = {
                .samplerAnisotropy = VK_TRUE
            }
        };

        std::vector<const char*> extensions {
            VK_KHR_SWAPCHAIN_EXTENSION_NAME
        };

        VkDeviceCreateInfo device_info {
            .sType = VK_STRUCTURE_TYPE_DEVICE_CREATE_INFO,
            .pNext = &features,
            .flags = 0,
            .queueCreateInfoCount = static_cast<u32>(queue_infos.size()),
            .pQueueCreateInfos = queue_infos.data(),
            .enabledLayerCount = 0,
            .ppEnabledLayerNames = nullptr,
            .enabledExtensionCount = static_cast<u32>(extensions.size()),
            .ppEnabledExtensionNames = extensions.data(),
            .pEnabledFeatures = nullptr
        };

        VK_CHECK(vkCreateDevice(physical_device, &device_info, nullptr, &device));
        volkLoadDevice(device);

        VmaAllocatorCreateInfo allocator_info {
            .flags = VMA_ALLOCATOR_CREATE_BUFFER_DEVICE_ADDRESS_BIT,
            .physicalDevice = physical_device,
            .device = device,
            .preferredLargeHeapBlockSize = 0,
            .pAllocationCallbacks = nullptr,
            .pDeviceMemoryCallbacks = nullptr,
            .pHeapSizeLimit = nullptr,
            .pVulkanFunctions = nullptr,
            .instance = instance,
            .vulkanApiVersion = VK_API_VERSION_1_4
        };

        VmaVulkanFunctions vma_funcs;
        vmaImportVulkanFunctionsFromVolk(&allocator_info, &vma_funcs);

        allocator_info.pVulkanFunctions = &vma_funcs;

        VK_CHECK(vmaCreateAllocator(&allocator_info, &allocator));

        vkGetDeviceQueue(device, graphics_queue_index, 0, &graphics_queue);
        vkGetDeviceQueue(device, compute_queue_index, 0, &compute_queue);
        vkGetDeviceQueue(device, transfer_queue_index, 0, &transfer_queue);

        std::println("device extensions:");
        for (const auto& extension : extensions) {
            std::println(" - {}", extension);
        }
    }

    VkSurfaceCapabilitiesKHR surface_caps;
    VkSurfaceFormatKHR surface_format;
    VkPresentModeKHR present_mode;
    VkExtent2D extent;

    { // swapchaind details
        vkGetPhysicalDeviceSurfaceCapabilitiesKHR(physical_device, surface, &surface_caps);

        u32 format_count = 0;
        vkGetPhysicalDeviceSurfaceFormatsKHR(physical_device, surface, &format_count, nullptr);
        std::vector<VkSurfaceFormatKHR> available_formats(format_count);
        vkGetPhysicalDeviceSurfaceFormatsKHR(physical_device, surface, &format_count, available_formats.data());

        surface_format = available_formats[0];
        for (const auto& format : available_formats) {
            if (format.format == VK_FORMAT_R8G8B8A8_SRGB && format.colorSpace == VK_COLOR_SPACE_SRGB_NONLINEAR_KHR) {
                surface_format = format;
                break;
            }
        }

        u32 mode_count = 0;
        vkGetPhysicalDeviceSurfacePresentModesKHR(physical_device, surface, &mode_count, nullptr);
        std::vector<VkPresentModeKHR> available_modes(mode_count);
        vkGetPhysicalDeviceSurfacePresentModesKHR(physical_device, surface, &mode_count, available_modes.data());

        present_mode = VK_PRESENT_MODE_FIFO_KHR;
        for (const auto& mode : available_modes) {
            if (mode == VK_PRESENT_MODE_MAILBOX_KHR) {
                present_mode = VK_PRESENT_MODE_MAILBOX_KHR;
                break;
            }
        }

        if (surface_caps.currentExtent.width != std::numeric_limits<u32>::max()) {
            extent = surface_caps.currentExtent;
        } else {
            extent = {
                .width = std::clamp(static_cast<u32>(window_width), surface_caps.minImageExtent.width, surface_caps.maxImageExtent.width),
                .height = std::clamp(static_cast<u32>(window_height), surface_caps.minImageExtent.height, surface_caps.maxImageExtent.height)
            };
        }
    }

    VkSwapchainKHR swapchain;
    u32 swapchain_image_count;
    std::vector<VkImage> swapchain_images;
    std::vector<VkImageView> swapchain_image_views;

    { // create swapchain
        swapchain_image_count = surface_caps.minImageCount + 1;
        if (surface_caps.maxImageCount > 0) {
            swapchain_image_count = std::min(swapchain_image_count, surface_caps.maxImageCount);
        }

        VkSwapchainCreateInfoKHR swapchain_info {
            .sType = VK_STRUCTURE_TYPE_SWAPCHAIN_CREATE_INFO_KHR,
            .pNext = nullptr,
            .flags = 0,
            .surface = surface,
            .minImageCount = swapchain_image_count,
            .imageFormat = surface_format.format,
            .imageColorSpace = surface_format.colorSpace,
            .imageExtent = extent,
            .imageArrayLayers = 1,
            .imageUsage = VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT | VK_IMAGE_USAGE_TRANSFER_DST_BIT,
            .imageSharingMode = VK_SHARING_MODE_EXCLUSIVE,
            .queueFamilyIndexCount = 0,
            .pQueueFamilyIndices = nullptr,
            .preTransform = surface_caps.currentTransform,
            .compositeAlpha = VK_COMPOSITE_ALPHA_OPAQUE_BIT_KHR,
            .presentMode = present_mode,
            .clipped = VK_TRUE,
            .oldSwapchain = VK_NULL_HANDLE
        };

        VK_CHECK(vkCreateSwapchainKHR(device, &swapchain_info, nullptr, &swapchain));

        vkGetSwapchainImagesKHR(device, swapchain, &swapchain_image_count, nullptr);

        swapchain_images.resize(swapchain_image_count);
        swapchain_image_views.resize(swapchain_image_count);

        vkGetSwapchainImagesKHR(device, swapchain, &swapchain_image_count, swapchain_images.data());

        for (u32 i = 0; i < swapchain_image_count; ++i) {
            VkImageViewCreateInfo view_info {
                .sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO,
                .pNext = nullptr,
                .flags = 0,
                .image = swapchain_images[i],
                .viewType = VK_IMAGE_VIEW_TYPE_2D,
                .format = surface_format.format,
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

            VK_CHECK(vkCreateImageView(device, &view_info, nullptr, &swapchain_image_views[i]));
        }
    }

    std::array<FrameContext, FRAMES_IN_FLIGHT> frames;
    for (auto& frame : frames) {
        frame = FrameContext::create(device,
            graphics_queue_index,
            compute_queue_index,
            transfer_queue_index
        );
    }

    u64 frame_count = 0;
    while (!glfwWindowShouldClose(window)) {
        glfwPollEvents();
        
        FrameContext& frame = frames[frame_count % FRAMES_IN_FLIGHT];

        VK_CHECK(vkWaitForFences(device, 1, &frame.fence, VK_TRUE, std::numeric_limits<u64>::max()));
        VK_CHECK(vkResetFences(device, 1, &frame.fence));

        u32 swapchain_index;
        vkAcquireNextImageKHR(device, swapchain, std::numeric_limits<u64>::max(), frame.image_available, VK_NULL_HANDLE, &swapchain_index);

        frame.compute.record(device, [&](VkCommandBuffer cmd) {
        });

        frame.graphics.record(device, [&](VkCommandBuffer cmd) {
            VkImageMemoryBarrier2 barrier {
                .sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER_2,
                .pNext = nullptr,
                .srcStageMask = VK_PIPELINE_STAGE_2_COLOR_ATTACHMENT_OUTPUT_BIT,
                .srcAccessMask = VK_ACCESS_2_NONE,
                .dstStageMask = VK_PIPELINE_STAGE_2_BOTTOM_OF_PIPE_BIT,
                .dstAccessMask = VK_ACCESS_2_NONE,
                .oldLayout = VK_IMAGE_LAYOUT_UNDEFINED,
                .newLayout = VK_IMAGE_LAYOUT_PRESENT_SRC_KHR,
                .srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED,
                .dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED,
                .image = swapchain_images[swapchain_index],
                .subresourceRange = {
                    .aspectMask = VK_IMAGE_ASPECT_COLOR_BIT,
                    .baseMipLevel = 0,
                    .levelCount = 1,
                    .baseArrayLayer = 0,
                    .layerCount = 1
                }
            };

            VkDependencyInfo dependency {
                .sType = VK_STRUCTURE_TYPE_DEPENDENCY_INFO,
                .pNext = nullptr,
                .dependencyFlags = 0,
                .memoryBarrierCount = 0,
                .pMemoryBarriers = nullptr,
                .bufferMemoryBarrierCount = 0,
                .pBufferMemoryBarriers = nullptr,
                .imageMemoryBarrierCount = 1,
                .pImageMemoryBarriers = &barrier
            };

            vkCmdPipelineBarrier2(cmd, &dependency);
        });

        frame.compute.submit(device, compute_queue);
        frame.graphics.submit(device, graphics_queue,
            { frame.image_available, frame.compute.semaphore },
            { VK_PIPELINE_STAGE_2_COLOR_ATTACHMENT_OUTPUT_BIT, VK_PIPELINE_STAGE_2_ALL_COMMANDS_BIT }
        );

        VkSwapchainPresentFenceInfoKHR present_fence {
            .sType = VK_STRUCTURE_TYPE_SWAPCHAIN_PRESENT_FENCE_INFO_KHR,
            .pNext = nullptr,
            .swapchainCount = 1,
            .pFences = &frame.fence
        };

        VkPresentInfoKHR present_info {
            .sType = VK_STRUCTURE_TYPE_PRESENT_INFO_KHR,
            .pNext = &present_fence,
            .waitSemaphoreCount = 1,
            .pWaitSemaphores = &frame.graphics.semaphore,
            .swapchainCount = 1,
            .pSwapchains = &swapchain,
            .pImageIndices = &swapchain_index,
            .pResults = nullptr
        };

        vkQueuePresentKHR(graphics_queue, &present_info);

        frame_count++;
    }

    vkDeviceWaitIdle(device);

    for (auto& frame : frames) {
        frame.destroy(device);
    }

    for (u32 i = 0; i < swapchain_image_count; ++i) {
        vkDestroyImageView(device, swapchain_image_views[i], nullptr);
    }

    vkDestroySwapchainKHR(device, swapchain, nullptr);
    swapchain_image_views.clear();
    swapchain_images.clear();

    vmaDestroyAllocator(allocator);
    vkDestroyDevice(device, nullptr);
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
        case VK_DEBUG_UTILS_MESSAGE_SEVERITY_VERBOSE_BIT_EXT:   ss << "[VERBOSE]"; break;
        case VK_DEBUG_UTILS_MESSAGE_SEVERITY_INFO_BIT_EXT:      ss << "[INFO]";    break;
        case VK_DEBUG_UTILS_MESSAGE_SEVERITY_WARNING_BIT_EXT:   ss << "[WARNING]"; break;
        case VK_DEBUG_UTILS_MESSAGE_SEVERITY_ERROR_BIT_EXT:     ss << "[ERROR]";   break;
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
