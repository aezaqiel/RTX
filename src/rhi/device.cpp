#include "device.hpp"

namespace RHI {

    Device::Device(const std::shared_ptr<Context>& context)
        : m_context(context)
    {
        u32 device_count = 0;
        vkEnumeratePhysicalDevices(context->instance(), &device_count, nullptr);
        std::vector<VkPhysicalDevice> available_devices(device_count);
        vkEnumeratePhysicalDevices(context->instance(), &device_count, available_devices.data());

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
                vkGetPhysicalDeviceSurfaceSupportKHR(device, queue_index, context->surface(), &present);

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
                m_physical_device = device;

                m_queue_indices.graphics = graphics.value();
                m_queue_indices.compute  = compute.value();
                m_queue_indices.transfer = transfer.value();
                
                m_rt_props = VkPhysicalDeviceRayTracingPipelinePropertiesKHR {
                    .sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_RAY_TRACING_PIPELINE_PROPERTIES_KHR,
                    .pNext = nullptr
                };

                m_as_props = VkPhysicalDeviceAccelerationStructurePropertiesKHR {
                    .sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_ACCELERATION_STRUCTURE_PROPERTIES_KHR,
                    .pNext = &m_rt_props
                };

                m_props = VkPhysicalDeviceProperties2 {
                    .sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_PROPERTIES_2,
                    .pNext = &m_as_props
                };

                vkGetPhysicalDeviceProperties2(m_physical_device, &m_props);

                std::println("physical device : {}", m_props.properties.deviceName);
                std::println("graphics queue index : {}", m_queue_indices.graphics);
                std::println("compute queue index  : {}", m_queue_indices.compute);
                std::println("transfer queue index : {}", m_queue_indices.transfer);

                break;
            }
        }

        std::set<u32> indices {
            m_queue_indices.graphics,
            m_queue_indices.compute,
            m_queue_indices.transfer
        };

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

        VK_CHECK(vkCreateDevice(m_physical_device, &device_info, nullptr, &m_device));
        volkLoadDevice(m_device);

        VmaAllocatorCreateInfo allocator_info {
            .flags = VMA_ALLOCATOR_CREATE_BUFFER_DEVICE_ADDRESS_BIT,
            .physicalDevice = m_physical_device,
            .device = m_device,
            .preferredLargeHeapBlockSize = 0,
            .pAllocationCallbacks = nullptr,
            .pDeviceMemoryCallbacks = nullptr,
            .pHeapSizeLimit = nullptr,
            .pVulkanFunctions = nullptr,
            .instance = context->instance(),
            .vulkanApiVersion = VK_API_VERSION_1_4
        };

        VmaVulkanFunctions vma_funcs;
        vmaImportVulkanFunctionsFromVolk(&allocator_info, &vma_funcs);

        allocator_info.pVulkanFunctions = &vma_funcs;

        VK_CHECK(vmaCreateAllocator(&allocator_info, &m_allocator));

        std::println("device extensions:");
        for (const auto& extension : extensions) {
            std::println(" - {}", extension);
        }
    }

    Device::~Device()
    {
        vmaDestroyAllocator(m_allocator);
        vkDestroyDevice(m_device, nullptr);
    }

    auto Device::wait_idle() const -> void
    {
        vkDeviceWaitIdle(m_device);
    }

}
