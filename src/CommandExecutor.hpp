#pragma once

#include "VulkanContext.hpp"
#include "VulkanUtils.hpp"

#include <vulkan/vulkan.h>

class CommandExecutor {
public:
    explicit CommandExecutor(VulkanContext& context) : context_(context)
    {
        VkCommandPoolCreateInfo poolInfo{};
        poolInfo.sType = VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO;
        poolInfo.queueFamilyIndex = context_.computeQueueFamily();
        poolInfo.flags = VK_COMMAND_POOL_CREATE_RESET_COMMAND_BUFFER_BIT;
        vkCheck(vkCreateCommandPool(context_.device(), &poolInfo, nullptr, &pool_),
                "vkCreateCommandPool");

        VkCommandBufferAllocateInfo allocInfo{};
        allocInfo.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO;
        allocInfo.commandPool = pool_;
        allocInfo.level = VK_COMMAND_BUFFER_LEVEL_PRIMARY;
        allocInfo.commandBufferCount = 1;
        try {
            vkCheck(vkAllocateCommandBuffers(context_.device(), &allocInfo, &commandBuffer_),
                    "vkAllocateCommandBuffers");

            VkFenceCreateInfo fenceInfo{};
            fenceInfo.sType = VK_STRUCTURE_TYPE_FENCE_CREATE_INFO;
            fenceInfo.flags = VK_FENCE_CREATE_SIGNALED_BIT;
            vkCheck(vkCreateFence(context_.device(), &fenceInfo, nullptr, &fence_),
                    "vkCreateFence");
        } catch (...) {
            if (pool_ != VK_NULL_HANDLE)
                vkDestroyCommandPool(context_.device(), pool_, nullptr);
            pool_ = VK_NULL_HANDLE;
            throw;
        }
    }

    ~CommandExecutor()
    {
        if (context_.device() != VK_NULL_HANDLE) {
            if (fence_ != VK_NULL_HANDLE)
                vkDestroyFence(context_.device(), fence_, nullptr);
            if (pool_ != VK_NULL_HANDLE)
                vkDestroyCommandPool(context_.device(), pool_, nullptr);
        }
    }

    CommandExecutor(const CommandExecutor&) = delete;
    CommandExecutor& operator=(const CommandExecutor&) = delete;

    VkCommandBuffer begin()
    {
        VkResult result = vkWaitForFences(
            context_.device(),
            1,
            &fence_,
            VK_TRUE,
            60'000'000'000 // wait for max 1 min
            );

        if (result == VK_TIMEOUT) {
            throw std::runtime_error("Timed out waiting for command executor fence");
        }
        vkCheck(result, "vkWaitForFences");
        vkCheck(vkResetFences(context_.device(), 1, &fence_), "vkResetFences");
        vkCheck(vkResetCommandBuffer(commandBuffer_, 0), "vkResetCommandBuffer");

        VkCommandBufferBeginInfo beginInfo{};
        beginInfo.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO;
        vkCheck(vkBeginCommandBuffer(commandBuffer_, &beginInfo), "vkBeginCommandBuffer");
        return commandBuffer_;
    }

    void submitAndWait()
    {
        vkCheck(vkEndCommandBuffer(commandBuffer_), "vkEndCommandBuffer");

        VkSubmitInfo submitInfo{};
        submitInfo.sType = VK_STRUCTURE_TYPE_SUBMIT_INFO;
        submitInfo.commandBufferCount = 1;
        submitInfo.pCommandBuffers = &commandBuffer_;

        vkCheck(vkQueueSubmit(context_.computeQueue(), 1, &submitInfo, fence_),
                "vkQueueSubmit");
        vkCheck(vkWaitForFences(context_.device(), 1, &fence_, VK_TRUE, UINT64_MAX),
                "vkWaitForFences");
    }

private:
    VulkanContext& context_;
    VkCommandPool pool_ = VK_NULL_HANDLE;
    VkCommandBuffer commandBuffer_ = VK_NULL_HANDLE;
    VkFence fence_ = VK_NULL_HANDLE;
};
