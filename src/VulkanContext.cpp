#include "VulkanContext.hpp"
#include "VulkanUtils.hpp"

#include <iostream>
#include <stdexcept>

VulkanContext::VulkanContext()
{
    uint32_t loaderVersion = VK_API_VERSION_1_0;
    vkCheck(vkEnumerateInstanceVersion(&loaderVersion), "vkEnumerateInstanceVersion");
    std::cout << "Vulkan loader API version: "
              << VK_API_VERSION_MAJOR(loaderVersion) << '.'
              << VK_API_VERSION_MINOR(loaderVersion) << '.'
              << VK_API_VERSION_PATCH(loaderVersion) << '\n';

    auto instanceResult = vkb::InstanceBuilder{}
        .set_app_name("NeutronCompute")
        .set_headless()
        .request_validation_layers()
        .use_default_debug_messenger()
        .build();

    if (!instanceResult) {
        throw std::runtime_error(
            "Failed to create Vulkan instance: " + instanceResult.error().message());
    }

    bootstrapInstance_ = instanceResult.value();
    instance_ = bootstrapInstance_;

    try {
        auto physicalDeviceResult = vkb::PhysicalDeviceSelector{bootstrapInstance_}
            .require_present(false)
            .select();

        if (!physicalDeviceResult) {
            throw std::runtime_error(
                "Failed to select a compute-capable GPU: " +
                physicalDeviceResult.error().message());
        }

        const vkb::PhysicalDevice bootstrapPhysicalDevice = physicalDeviceResult.value();
        physicalDevice_ = bootstrapPhysicalDevice.physical_device;

        auto deviceResult = vkb::DeviceBuilder{bootstrapPhysicalDevice}.build();
        if (!deviceResult) {
            throw std::runtime_error(
                "Failed to create logical device: " + deviceResult.error().message());
        }

        bootstrapDevice_ = deviceResult.value();
        device_ = bootstrapDevice_;

        auto queueResult = bootstrapDevice_.get_queue(vkb::QueueType::compute);
        if (!queueResult) {
            throw std::runtime_error(
                "Failed to get compute queue: " + queueResult.error().message());
        }
        computeQueue_ = queueResult.value();

        auto queueIndexResult = bootstrapDevice_.get_queue_index(vkb::QueueType::compute);
        if (!queueIndexResult) {
            throw std::runtime_error(
                "Failed to get compute queue index: " + queueIndexResult.error().message());
        }
        computeQueueFamily_ = queueIndexResult.value();

        VmaAllocatorCreateInfo allocatorInfo{};
        allocatorInfo.instance = instance_;
        allocatorInfo.physicalDevice = physicalDevice_;
        allocatorInfo.device = device_;
        allocatorInfo.vulkanApiVersion = VK_API_VERSION_1_0;

        vkCheck(vmaCreateAllocator(&allocatorInfo, &allocator_), "vmaCreateAllocator");
    } catch (...) {
        if (device_ != VK_NULL_HANDLE) {
            vkDestroyDevice(device_, nullptr);
            device_ = VK_NULL_HANDLE;
        }
        if (instance_ != VK_NULL_HANDLE) {
            vkb::destroy_instance(bootstrapInstance_);
            instance_ = VK_NULL_HANDLE;
        }
        throw;
    }
}

VulkanContext::~VulkanContext()
{
    if (device_ != VK_NULL_HANDLE) {
        vkDeviceWaitIdle(device_);

        if (allocator_ != VK_NULL_HANDLE) {
            vmaDestroyAllocator(allocator_);
            allocator_ = VK_NULL_HANDLE;
        }

        vkDestroyDevice(device_, nullptr);
        device_ = VK_NULL_HANDLE;
    }

    if (instance_ != VK_NULL_HANDLE) {
        vkb::destroy_instance(bootstrapInstance_);
        instance_ = VK_NULL_HANDLE;
    }
}

void VulkanContext::waitIdle() const
{
    vkCheck(vkDeviceWaitIdle(device_), "vkDeviceWaitIdle");
}
