#include "vk_types.hpp"

#include <GLFW/glfw3.h>
#include <GLFW/glfw3native.h>

#include <glm/glm.hpp>

#include "rhi/command.hpp"
#include "rhi/sync.hpp"
#include "rhi/buffer.hpp"

#include "scene/loader.hpp"

auto messenger_callback(VkDebugUtilsMessageSeverityFlagBitsEXT severity, VkDebugUtilsMessageTypeFlagsEXT type, const VkDebugUtilsMessengerCallbackDataEXT* data, void* user) -> VkBool32;

namespace {

    constexpr usize FRAMES_IN_FLIGHT = 3;

}

auto main() -> i32
{
    i32 window_width = 1280;
    i32 window_height = 720;

    glfwSetErrorCallback([](i32 code, const char* desc) {
        std::println(std::cerr, "glfw error ({}): {}", code, desc);
    });

    std::println("creating window");

    glfwInit();

    glfwWindowHint(GLFW_CLIENT_API, GLFW_NO_API);
    glfwWindowHint(GLFW_RESIZABLE, GLFW_FALSE);

    GLFWwindow* window = glfwCreateWindow(1280, 720, "RTX", nullptr, nullptr);
    {
        glfwGetFramebufferSize(window, &window_width, &window_height);
        std::println("created window \"{}\" ({}, {})", glfwGetWindowTitle(window), window_width, window_height);
    }

    std::println("creating vulkan instance");

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

    std::println("creating vulkan surface");

    VkSurfaceKHR surface;
    VK_CHECK(glfwCreateWindowSurface(instance, window, nullptr, &surface));

    std::println("choosing physical device");

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

    std::println("creating vulkan device");

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

        VkPhysicalDeviceRayTracingPipelineFeaturesKHR rt_features {
            .sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_RAY_TRACING_PIPELINE_FEATURES_KHR,
            .pNext = nullptr,
            .rayTracingPipeline = VK_TRUE,
            .rayTracingPipelineShaderGroupHandleCaptureReplay = VK_FALSE,
            .rayTracingPipelineShaderGroupHandleCaptureReplayMixed = VK_FALSE,
            .rayTracingPipelineTraceRaysIndirect = VK_FALSE,
            .rayTraversalPrimitiveCulling = VK_FALSE
        };

        VkPhysicalDeviceAccelerationStructureFeaturesKHR as_features {
            .sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_ACCELERATION_STRUCTURE_FEATURES_KHR,
            .pNext = &rt_features,
            .accelerationStructure = VK_TRUE,
            .accelerationStructureCaptureReplay = VK_FALSE,
            .accelerationStructureIndirectBuild = VK_FALSE,
            .accelerationStructureHostCommands = VK_FALSE,
            .descriptorBindingAccelerationStructureUpdateAfterBind = VK_FALSE
        };

        VkPhysicalDeviceVulkan14Features features14 {
            .sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_VULKAN_1_4_FEATURES,
            .pNext = &as_features,
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
            VK_KHR_SWAPCHAIN_EXTENSION_NAME,
            VK_KHR_ACCELERATION_STRUCTURE_EXTENSION_NAME,
            VK_KHR_DEFERRED_HOST_OPERATIONS_EXTENSION_NAME,
            VK_KHR_RAY_TRACING_PIPELINE_EXTENSION_NAME
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

    std::println("query swapchain details");

    VkSurfaceCapabilitiesKHR surface_caps;
    VkSurfaceFormatKHR surface_format;
    VkPresentModeKHR present_mode;
    VkExtent2D extent;

    { // swapchain details
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

    std::println("create swapchain");

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

    std::println("creating command and sync primitives");

    QueueSync graphics_sync = QueueSync::create(device, graphics_queue);
    QueueSync compute_sync = QueueSync::create(device, compute_queue);

    CommandContext compute_command = CommandContext::create(device, compute_queue_index);

    std::println("loading model");

    auto model = Loader::load_obj("assets/sponza/sponza.obj");

    std::println("Model loaded: {} vertices, {} indices", 
        model.mesh->positions.size(), 
        model.mesh->indices.size()
    );

    std::println("uploading model and building AS");

    usize vertex_size = model.mesh->positions.size() * sizeof(glm::vec3);
    usize index_size = model.mesh->indices.size() * sizeof(u32);

    VkBufferUsageFlags build_input_flags =
        VK_BUFFER_USAGE_ACCELERATION_STRUCTURE_BUILD_INPUT_READ_ONLY_BIT_KHR |
        VK_BUFFER_USAGE_TRANSFER_DST_BIT |
        VK_BUFFER_USAGE_SHADER_DEVICE_ADDRESS_BIT;

    Buffer vertex_buffer = Buffer::create(allocator, vertex_size, build_input_flags, VMA_MEMORY_USAGE_AUTO_PREFER_DEVICE);
    Buffer index_buffer = Buffer::create(allocator, index_size, build_input_flags | VK_BUFFER_USAGE_INDEX_BUFFER_BIT, VMA_MEMORY_USAGE_AUTO_PREFER_DEVICE);

    Buffer vertex_staging = Buffer::create_staging(allocator, vertex_size, model.mesh->positions.data());
    Buffer index_staging = Buffer::create_staging(allocator, index_size, model.mesh->indices.data());

    VkAccelerationStructureKHR blas;
    Buffer blas_buffer;
    Buffer blas_scratch;

    VkAccelerationStructureGeometryTrianglesDataKHR triangles {
        .sType = VK_STRUCTURE_TYPE_ACCELERATION_STRUCTURE_GEOMETRY_TRIANGLES_DATA_KHR,
        .pNext = nullptr,
        .vertexFormat = VK_FORMAT_R32G32B32_SFLOAT,
        .vertexData = { .deviceAddress = vertex_buffer.address },
        .vertexStride = sizeof(glm::vec3),
        .maxVertex = static_cast<u32>(model.mesh->positions.size() - 1),
        .indexType = VK_INDEX_TYPE_UINT32,
        .indexData = { .deviceAddress = index_buffer.address },
        .transformData = { .deviceAddress = 0 }
    };

    VkAccelerationStructureGeometryKHR blas_geometry {
        .sType = VK_STRUCTURE_TYPE_ACCELERATION_STRUCTURE_GEOMETRY_KHR,
        .pNext = nullptr,
        .geometryType = VK_GEOMETRY_TYPE_TRIANGLES_KHR,
        .geometry = { .triangles = triangles },
        .flags = VK_GEOMETRY_OPAQUE_BIT_KHR
    };

    VkAccelerationStructureBuildGeometryInfoKHR blas_build_info {
        .sType = VK_STRUCTURE_TYPE_ACCELERATION_STRUCTURE_BUILD_GEOMETRY_INFO_KHR,
        .pNext = nullptr,
        .type = VK_ACCELERATION_STRUCTURE_TYPE_BOTTOM_LEVEL_KHR,
        .flags = VK_BUILD_ACCELERATION_STRUCTURE_PREFER_FAST_TRACE_BIT_KHR,
        .mode = VK_BUILD_ACCELERATION_STRUCTURE_MODE_BUILD_KHR,
        .srcAccelerationStructure = VK_NULL_HANDLE,
        .dstAccelerationStructure = VK_NULL_HANDLE,
        .geometryCount = 1,
        .pGeometries = &blas_geometry,
        .ppGeometries = nullptr,
        .scratchData = {}
    };

    u32 primitive_count = static_cast<u32>(model.mesh->indices.size() / 3);

    VkAccelerationStructureBuildSizesInfoKHR blas_size_info {
        .sType = VK_STRUCTURE_TYPE_ACCELERATION_STRUCTURE_BUILD_SIZES_INFO_KHR,
        .pNext = nullptr
    };

    vkGetAccelerationStructureBuildSizesKHR(device, VK_ACCELERATION_STRUCTURE_BUILD_TYPE_DEVICE_KHR, &blas_build_info, &primitive_count, &blas_size_info);
    std::println("blas size: {}", blas_size_info.accelerationStructureSize);

    blas_buffer = Buffer::create(allocator, blas_size_info.accelerationStructureSize, VK_BUFFER_USAGE_ACCELERATION_STRUCTURE_STORAGE_BIT_KHR | VK_BUFFER_USAGE_SHADER_DEVICE_ADDRESS_BIT, VMA_MEMORY_USAGE_AUTO_PREFER_DEVICE);

    VkAccelerationStructureCreateInfoKHR blas_create_info {
        .sType = VK_STRUCTURE_TYPE_ACCELERATION_STRUCTURE_CREATE_INFO_KHR,
        .pNext = nullptr,
        .createFlags = 0,
        .buffer = blas_buffer.handle,
        .offset = 0,
        .size = blas_size_info.accelerationStructureSize,
        .type = VK_ACCELERATION_STRUCTURE_TYPE_BOTTOM_LEVEL_KHR,
        .deviceAddress = 0
    };

    VK_CHECK(vkCreateAccelerationStructureKHR(device, &blas_create_info, nullptr, &blas));

    blas_scratch = Buffer::create_aligned(allocator, blas_size_info.buildScratchSize, as_props.minAccelerationStructureScratchOffsetAlignment, VK_BUFFER_USAGE_STORAGE_BUFFER_BIT | VK_BUFFER_USAGE_SHADER_DEVICE_ADDRESS_BIT, VMA_MEMORY_USAGE_AUTO_PREFER_DEVICE);

    blas_build_info.dstAccelerationStructure = blas;
    blas_build_info.scratchData.deviceAddress = blas_scratch.address;

    VkAccelerationStructureBuildRangeInfoKHR blas_range_info {
        .primitiveCount = primitive_count,
        .primitiveOffset = 0,
        .firstVertex = 0,
        .transformOffset = 0
    };

    const VkAccelerationStructureBuildRangeInfoKHR* p_blas_range_info = &blas_range_info;

    VkAccelerationStructureKHR tlas;
    Buffer tlas_buffer;
    Buffer tlas_scratch;
    Buffer instance_buffer;

    VkAccelerationStructureDeviceAddressInfoKHR blas_address_info {
        .sType = VK_STRUCTURE_TYPE_ACCELERATION_STRUCTURE_DEVICE_ADDRESS_INFO_KHR,
        .pNext = nullptr,
        .accelerationStructure = blas
    };

    VkDeviceAddress blas_address = vkGetAccelerationStructureDeviceAddressKHR(device, &blas_address_info);

    VkAccelerationStructureInstanceKHR tlas_instance {
        .transform = {
            .matrix = {
                { 1.0f, 0.0f, 0.0f, 0.0f },
                { 0.0f, 1.0f, 0.0f, 0.0f },
                { 0.0f, 0.0f, 1.0f, 0.0f }
            }
        },
        .instanceCustomIndex = 0,
        .mask = 0xFF,
        .instanceShaderBindingTableRecordOffset = 0,
        .flags = VK_GEOMETRY_INSTANCE_TRIANGLE_FACING_CULL_DISABLE_BIT_KHR,
        .accelerationStructureReference = blas_address
    };

    instance_buffer = Buffer::create(allocator, sizeof(VkAccelerationStructureInstanceKHR), VK_BUFFER_USAGE_ACCELERATION_STRUCTURE_BUILD_INPUT_READ_ONLY_BIT_KHR | VK_BUFFER_USAGE_SHADER_DEVICE_ADDRESS_BIT, VMA_MEMORY_USAGE_CPU_TO_GPU);
    instance_buffer.upload(allocator, &tlas_instance, sizeof(VkAccelerationStructureInstanceKHR));

    VkDeviceAddress instance_buffer_address = instance_buffer.address;

    VkAccelerationStructureGeometryInstancesDataKHR instances_data {
        .sType = VK_STRUCTURE_TYPE_ACCELERATION_STRUCTURE_GEOMETRY_INSTANCES_DATA_KHR,
        .pNext = nullptr,
        .arrayOfPointers = VK_FALSE,
        .data = { .deviceAddress = instance_buffer_address }
    };

    VkAccelerationStructureGeometryKHR tlas_geometry {
        .sType = VK_STRUCTURE_TYPE_ACCELERATION_STRUCTURE_GEOMETRY_KHR,
        .pNext = nullptr,
        .geometryType = VK_GEOMETRY_TYPE_INSTANCES_KHR,
        .geometry = { .instances = instances_data },
        .flags = VK_GEOMETRY_OPAQUE_BIT_KHR
    };

    VkAccelerationStructureBuildGeometryInfoKHR tlas_build_info {
        .sType = VK_STRUCTURE_TYPE_ACCELERATION_STRUCTURE_BUILD_GEOMETRY_INFO_KHR,
        .pNext = nullptr,
        .type = VK_ACCELERATION_STRUCTURE_TYPE_TOP_LEVEL_KHR,
        .flags = VK_BUILD_ACCELERATION_STRUCTURE_PREFER_FAST_TRACE_BIT_KHR,
        .mode = VK_BUILD_ACCELERATION_STRUCTURE_MODE_BUILD_KHR,
        .srcAccelerationStructure = VK_NULL_HANDLE,
        .dstAccelerationStructure = VK_NULL_HANDLE,
        .geometryCount = 1,
        .pGeometries = &tlas_geometry,
        .ppGeometries = nullptr,
        .scratchData = {}
    };

    u32 instance_count = 1;

    VkAccelerationStructureBuildSizesInfoKHR tlas_size_info {
        .sType = VK_STRUCTURE_TYPE_ACCELERATION_STRUCTURE_BUILD_SIZES_INFO_KHR,
        .pNext = nullptr
    };

    vkGetAccelerationStructureBuildSizesKHR(device, VK_ACCELERATION_STRUCTURE_BUILD_TYPE_DEVICE_KHR, &tlas_build_info, &instance_count, &tlas_size_info);
    std::println("tlas size: {}", tlas_size_info.accelerationStructureSize);

    tlas_buffer = Buffer::create(allocator, tlas_size_info.accelerationStructureSize, VK_BUFFER_USAGE_ACCELERATION_STRUCTURE_STORAGE_BIT_KHR | VK_BUFFER_USAGE_SHADER_DEVICE_ADDRESS_BIT, VMA_MEMORY_USAGE_AUTO_PREFER_DEVICE);

    VkAccelerationStructureCreateInfoKHR tlas_create_info {
        .sType = VK_STRUCTURE_TYPE_ACCELERATION_STRUCTURE_CREATE_INFO_KHR,
        .pNext = nullptr,
        .createFlags = 0,
        .buffer = tlas_buffer.handle,
        .offset = 0,
        .size = tlas_size_info.accelerationStructureSize,
        .type = VK_ACCELERATION_STRUCTURE_TYPE_TOP_LEVEL_KHR,
        .deviceAddress = 0
    };

    VK_CHECK(vkCreateAccelerationStructureKHR(device, &tlas_create_info, nullptr, &tlas));

    tlas_scratch = Buffer::create_aligned(allocator, tlas_size_info.buildScratchSize, as_props.minAccelerationStructureScratchOffsetAlignment, VK_BUFFER_USAGE_STORAGE_BUFFER_BIT | VK_BUFFER_USAGE_SHADER_DEVICE_ADDRESS_BIT, VMA_MEMORY_USAGE_AUTO_PREFER_DEVICE);

    tlas_build_info.dstAccelerationStructure = tlas;
    tlas_build_info.scratchData.deviceAddress = tlas_scratch.address;

    VkAccelerationStructureBuildRangeInfoKHR tlas_range_info {
        .primitiveCount = instance_count,
        .primitiveOffset = 0,
        .firstVertex = 0,
        .transformOffset = 0
    };

    const VkAccelerationStructureBuildRangeInfoKHR* p_tlas_range_info = &tlas_range_info;

    compute_command.record(device, [&](VkCommandBuffer cmd) {
        Buffer::copy(cmd, vertex_staging, vertex_buffer, vertex_size);
        Buffer::copy(cmd, index_staging, index_buffer, index_size);

        VkMemoryBarrier2 upload_barrier {
            .sType = VK_STRUCTURE_TYPE_MEMORY_BARRIER_2,
            .pNext = nullptr,
            .srcStageMask = VK_PIPELINE_STAGE_2_TRANSFER_BIT,
            .srcAccessMask = VK_ACCESS_2_TRANSFER_WRITE_BIT,
            .dstStageMask = VK_PIPELINE_STAGE_2_ACCELERATION_STRUCTURE_BUILD_BIT_KHR,
            .dstAccessMask = VK_ACCESS_2_ACCELERATION_STRUCTURE_READ_BIT_KHR
        };

        VkDependencyInfo upload_dependency {
            .sType = VK_STRUCTURE_TYPE_DEPENDENCY_INFO,
            .pNext = nullptr,
            .dependencyFlags = 0,
            .memoryBarrierCount = 1,
            .pMemoryBarriers = &upload_barrier,
            .bufferMemoryBarrierCount = 0,
            .pBufferMemoryBarriers = nullptr,
            .imageMemoryBarrierCount = 0,
            .pImageMemoryBarriers = nullptr
        };

        vkCmdPipelineBarrier2(cmd, &upload_dependency);

        vkCmdBuildAccelerationStructuresKHR(cmd, 1, &blas_build_info, &p_blas_range_info);

        VkMemoryBarrier2 blas_barrier {
            .sType = VK_STRUCTURE_TYPE_MEMORY_BARRIER_2,
            .pNext = nullptr,
            .srcStageMask = VK_PIPELINE_STAGE_2_ACCELERATION_STRUCTURE_BUILD_BIT_KHR,
            .srcAccessMask = VK_ACCESS_2_ACCELERATION_STRUCTURE_WRITE_BIT_KHR,
            .dstStageMask = VK_PIPELINE_STAGE_2_ACCELERATION_STRUCTURE_BUILD_BIT_KHR,
            .dstAccessMask = VK_ACCESS_2_ACCELERATION_STRUCTURE_READ_BIT_KHR
        };

        VkDependencyInfo blas_dependency {
            .sType = VK_STRUCTURE_TYPE_DEPENDENCY_INFO,
            .pNext = nullptr,
            .dependencyFlags = 0,
            .memoryBarrierCount = 1,
            .pMemoryBarriers = &blas_barrier,
            .bufferMemoryBarrierCount = 0,
            .pBufferMemoryBarriers = nullptr,
            .imageMemoryBarrierCount = 0,
            .pImageMemoryBarriers = nullptr
        };

        vkCmdPipelineBarrier2(cmd, &blas_dependency);

        vkCmdBuildAccelerationStructuresKHR(cmd, 1, &tlas_build_info, &p_tlas_range_info);
    });

    std::vector<VkSemaphoreSubmitInfo> as_waits;
    std::vector<VkSemaphoreSubmitInfo> as_signals;
    u64 as_signal_value = compute_sync.submit(device, compute_command.buffer, as_waits, as_signals);

    std::println("creating frame resources");

    std::array<FrameContext, FRAMES_IN_FLIGHT> frames;
    for (auto& frame : frames) {
        frame = FrameContext::create(device,
            graphics_queue_index,
            compute_queue_index
        );
    }
    
    std::println("sync AS build");

    VkSemaphoreWaitInfo as_wait_info {
        .sType = VK_STRUCTURE_TYPE_SEMAPHORE_WAIT_INFO,
        .pNext = nullptr,
        .flags = 0,
        .semaphoreCount = 1,
        .pSemaphores = &compute_sync.timeline,
        .pValues = &as_signal_value
    };

    VK_CHECK(vkWaitSemaphores(device, &as_wait_info, std::numeric_limits<u64>::max()));

    tlas_scratch.destroy(allocator);
    instance_buffer.destroy(allocator);

    blas_scratch.destroy(allocator);

    vertex_staging.destroy(allocator);
    index_staging.destroy(allocator);

    vertex_buffer.destroy(allocator);
    index_buffer.destroy(allocator);

    compute_command.destroy(device);

    std::println("render loop start");

    u64 frame_count = 0;
    while (!glfwWindowShouldClose(window)) {
        glfwPollEvents();
        
        FrameContext& frame = frames[frame_count % FRAMES_IN_FLIGHT];
        if (frame_count >= FRAMES_IN_FLIGHT) {
            u64 wait_value = frame_count - FRAMES_IN_FLIGHT + 1;

            VkSemaphoreWaitInfo wait_info {
                .sType = VK_STRUCTURE_TYPE_SEMAPHORE_WAIT_INFO,
                .pNext = nullptr,
                .flags = 0,
                .semaphoreCount = 1,
                .pSemaphores = &graphics_sync.timeline,
                .pValues = &wait_value
            };

            VK_CHECK(vkWaitSemaphores(device, &wait_info, std::numeric_limits<u64>::max()));
        }

        u32 swapchain_index;
        vkAcquireNextImageKHR(device, swapchain, std::numeric_limits<u64>::max(), frame.image_available, VK_NULL_HANDLE, &swapchain_index);

        frame.compute.record(device, [&](VkCommandBuffer cmd) {
        });

        std::vector<VkSemaphoreSubmitInfo> compute_waits;
        std::vector<VkSemaphoreSubmitInfo> compute_signals;
        u64 compute_signal_value = compute_sync.submit(device, frame.compute.buffer, compute_waits, compute_signals);

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

        std::vector<VkSemaphoreSubmitInfo> graphics_waits;

        graphics_waits.push_back(VkSemaphoreSubmitInfo {
            .sType = VK_STRUCTURE_TYPE_SEMAPHORE_SUBMIT_INFO,
            .pNext = nullptr,
            .semaphore = frame.image_available,
            .value = 0,
            .stageMask = VK_PIPELINE_STAGE_2_COLOR_ATTACHMENT_OUTPUT_BIT,
            .deviceIndex = 0
        });

        graphics_waits.push_back(VkSemaphoreSubmitInfo {
            .sType = VK_STRUCTURE_TYPE_SEMAPHORE_SUBMIT_INFO,
            .pNext = nullptr,
            .semaphore = compute_sync.timeline,
            .value = compute_signal_value,
            .stageMask = VK_PIPELINE_STAGE_2_ALL_COMMANDS_BIT,
            .deviceIndex = 0
        });

        std::vector<VkSemaphoreSubmitInfo> graphics_signals;

        graphics_signals.push_back(VkSemaphoreSubmitInfo {
            .sType = VK_STRUCTURE_TYPE_SEMAPHORE_SUBMIT_INFO,
            .pNext = nullptr,
            .semaphore = frame.render_complete,
            .value = 0,
            .stageMask = VK_PIPELINE_STAGE_2_ALL_COMMANDS_BIT,
            .deviceIndex = 0
        });

        u64 graphics_signal_value = graphics_sync.submit(device, frame.graphics.buffer, graphics_waits, graphics_signals);

        VkPresentInfoKHR present_info {
            .sType = VK_STRUCTURE_TYPE_PRESENT_INFO_KHR,
            .pNext = nullptr,
            .waitSemaphoreCount = 1,
            .pWaitSemaphores = &frame.render_complete,
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

    graphics_sync.destroy(device);
    compute_sync.destroy(device);

    vkDestroyAccelerationStructureKHR(device, tlas, nullptr);
    tlas_buffer.destroy(allocator);

    vkDestroyAccelerationStructureKHR(device, blas, nullptr);
    blas_buffer.destroy(allocator);

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
