#pragma once

#include <VkBootstrap.h>
#include <vk_mem_alloc.h>
#include <vulkan/vulkan.h>

#include <cstdint>

class VulkanContext {
public:
    VulkanContext();
    ~VulkanContext();

    VulkanContext(const VulkanContext&) = delete;
    VulkanContext& operator=(const VulkanContext&) = delete;
    VulkanContext(VulkanContext&&) = delete;
    VulkanContext& operator=(VulkanContext&&) = delete;

    VkInstance instance() const noexcept { return instance_; }
    VkPhysicalDevice physicalDevice() const noexcept { return physicalDevice_; }
    VkDevice device() const noexcept { return device_; }
    VkQueue computeQueue() const noexcept { return computeQueue_; }
    std::uint32_t computeQueueFamily() const noexcept { return computeQueueFamily_; }
    VmaAllocator allocator() const noexcept { return allocator_; }

    void waitIdle() const;

private:
    vkb::Instance bootstrapInstance_{};
    vkb::Device bootstrapDevice_{};

    VkInstance instance_ = VK_NULL_HANDLE;
    VkPhysicalDevice physicalDevice_ = VK_NULL_HANDLE;
    VkDevice device_ = VK_NULL_HANDLE;
    VkQueue computeQueue_ = VK_NULL_HANDLE;
    std::uint32_t computeQueueFamily_ = 0;
    VmaAllocator allocator_ = VK_NULL_HANDLE;
};
