#pragma once

#include "VulkanContext.hpp"
#include "VulkanUtils.hpp"

#include <vulkan/vulkan.h>

#include <cstdint>
#include <string>
#include <vector>

// A kernel's descriptor layout is fixed when its pipeline is created.
// Each binding can be updated with a different buffer between simulation cycles.
struct StorageBinding {
    std::uint32_t binding = 0;
    VkShaderStageFlags stages = VK_SHADER_STAGE_COMPUTE_BIT;
};

class ComputeKernel {
public:
    ComputeKernel(
        VulkanContext& context,
        const char* spirvPath,
        const std::vector<StorageBinding>& storageBindings,
        std::uint32_t pushConstantBytes = 0)
        : context_(context)
    {
        if (storageBindings.empty()) {
            throw std::invalid_argument("ComputeKernel requires at least one storage binding");
        }

        std::vector<VkDescriptorSetLayoutBinding> layoutBindings;
        layoutBindings.reserve(storageBindings.size());
        for (const auto& spec : storageBindings) {
            VkDescriptorSetLayoutBinding binding{};
            binding.binding = spec.binding;
            binding.descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
            binding.descriptorCount = 1;
            binding.stageFlags = spec.stages;
            layoutBindings.push_back(binding);
        }

        VkDescriptorSetLayoutCreateInfo setLayoutInfo{};
        setLayoutInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO;
        setLayoutInfo.bindingCount = static_cast<std::uint32_t>(layoutBindings.size());
        setLayoutInfo.pBindings = layoutBindings.data();
        vkCheck(vkCreateDescriptorSetLayout(
            context_.device(), &setLayoutInfo, nullptr, &descriptorSetLayout_),
            "vkCreateDescriptorSetLayout");

        try {
            VkPushConstantRange pushRange{};
            pushRange.stageFlags = VK_SHADER_STAGE_COMPUTE_BIT;
            pushRange.offset = 0;
            pushRange.size = pushConstantBytes;

            VkPipelineLayoutCreateInfo pipelineLayoutInfo{};
            pipelineLayoutInfo.sType = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO;
            pipelineLayoutInfo.setLayoutCount = 1;
            pipelineLayoutInfo.pSetLayouts = &descriptorSetLayout_;
            if (pushConstantBytes > 0) {
                pipelineLayoutInfo.pushConstantRangeCount = 1;
                pipelineLayoutInfo.pPushConstantRanges = &pushRange;
            }

            vkCheck(vkCreatePipelineLayout(
                context_.device(), &pipelineLayoutInfo, nullptr, &pipelineLayout_),
                "vkCreatePipelineLayout");

            const auto code = loadSpirv(spirvPath);
            VkShaderModuleCreateInfo shaderInfo{};
            shaderInfo.sType = VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO;
            shaderInfo.codeSize = code.size() * sizeof(std::uint32_t);
            shaderInfo.pCode = code.data();

            VkShaderModule shader = VK_NULL_HANDLE;
            vkCheck(vkCreateShaderModule(context_.device(), &shaderInfo, nullptr, &shader),
                    "vkCreateShaderModule");

            VkPipelineShaderStageCreateInfo stage{};
            stage.sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
            stage.stage = VK_SHADER_STAGE_COMPUTE_BIT;
            stage.module = shader;
            stage.pName = "main";

            VkComputePipelineCreateInfo pipelineInfo{};
            pipelineInfo.sType = VK_STRUCTURE_TYPE_COMPUTE_PIPELINE_CREATE_INFO;
            pipelineInfo.stage = stage;
            pipelineInfo.layout = pipelineLayout_;

            const VkResult result = vkCreateComputePipelines(
                context_.device(), VK_NULL_HANDLE, 1, &pipelineInfo, nullptr, &pipeline_);
            vkDestroyShaderModule(context_.device(), shader, nullptr);
            vkCheck(result, "vkCreateComputePipelines");
        } catch (...) {
            destroy();
            throw;
        }

        pushConstantBytes_ = pushConstantBytes;
        storageBindings_ = storageBindings;
        storageBindingCount_ = static_cast<std::uint32_t>(storageBindings.size());
        try {
            descriptorPool_ = createDescriptorPool(storageBindingCount_);
            descriptorSet_ = allocateDescriptorSet();
        } catch (...) {
            destroy();
            throw;
        }
    }

    ~ComputeKernel() { destroy(); }

    ComputeKernel(const ComputeKernel&) = delete;
    ComputeKernel& operator=(const ComputeKernel&) = delete;

    VkPipeline pipeline() const noexcept { return pipeline_; }
    VkPipelineLayout pipelineLayout() const noexcept { return pipelineLayout_; }
    VkDescriptorSet descriptorSet() const noexcept { return descriptorSet_; }
    VkDescriptorSetLayout descriptorSetLayout() const noexcept { return descriptorSetLayout_; }

    // Supply buffers in binding order (binding numbers may be sparse; the order
    // here must match the StorageBinding vector passed to the constructor).
    void updateStorageBuffers(const std::vector<VkBuffer>& buffers,
                              const std::vector<VkDeviceSize>& sizes)
    {
        if (buffers.size() != storageBindingCount_ || sizes.size() != storageBindingCount_) {
            throw std::invalid_argument("Buffer count does not match ComputeKernel descriptor layout");
        }

        std::vector<VkDescriptorBufferInfo> infos(buffers.size());
        std::vector<VkWriteDescriptorSet> writes(buffers.size());
        for (std::size_t i = 0; i < buffers.size(); ++i) {
            infos[i].buffer = buffers[i];
            infos[i].offset = 0;
            infos[i].range = sizes[i];

            writes[i].sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
            writes[i].dstSet = descriptorSet_;
            writes[i].dstBinding = storageBindings_[i].binding;
            writes[i].descriptorCount = 1;
            writes[i].descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
            writes[i].pBufferInfo = &infos[i];
        }
        vkUpdateDescriptorSets(context_.device(),
                               static_cast<std::uint32_t>(writes.size()), writes.data(),
                               0, nullptr);
    }

    void bind(VkCommandBuffer commandBuffer) const
    {
        vkCmdBindPipeline(commandBuffer, VK_PIPELINE_BIND_POINT_COMPUTE, pipeline_);
        vkCmdBindDescriptorSets(commandBuffer, VK_PIPELINE_BIND_POINT_COMPUTE,
                                pipelineLayout_, 0, 1, &descriptorSet_, 0, nullptr);
    }

    void pushConstants(VkCommandBuffer commandBuffer, const void* data,
                       std::uint32_t bytes) const
    {
        if (bytes != pushConstantBytes_) {
            throw std::invalid_argument("Push-constant byte count does not match pipeline layout");
        }
        if (bytes > 0) {
            vkCmdPushConstants(commandBuffer, pipelineLayout_, VK_SHADER_STAGE_COMPUTE_BIT,
                               0, bytes, data);
        }
    }

private:
    VkDescriptorPool createDescriptorPool(std::uint32_t descriptorCount)
    {
        VkDescriptorPoolSize size{};
        size.type = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
        size.descriptorCount = descriptorCount;

        VkDescriptorPoolCreateInfo info{};
        info.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO;
        info.maxSets = 1;
        info.poolSizeCount = 1;
        info.pPoolSizes = &size;

        VkDescriptorPool pool = VK_NULL_HANDLE;
        vkCheck(vkCreateDescriptorPool(context_.device(), &info, nullptr, &pool),
                "vkCreateDescriptorPool");
        return pool;
    }

    VkDescriptorSet allocateDescriptorSet()
    {
        VkDescriptorSetAllocateInfo info{};
        info.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO;
        info.descriptorPool = descriptorPool_;
        info.descriptorSetCount = 1;
        info.pSetLayouts = &descriptorSetLayout_;

        VkDescriptorSet set = VK_NULL_HANDLE;
        vkCheck(vkAllocateDescriptorSets(context_.device(), &info, &set),
                "vkAllocateDescriptorSets");
        return set;
    }

    void destroy() noexcept
    {
        if (context_.device() == VK_NULL_HANDLE) return;
        if (descriptorPool_ != VK_NULL_HANDLE)
            vkDestroyDescriptorPool(context_.device(), descriptorPool_, nullptr);
        if (pipeline_ != VK_NULL_HANDLE)
            vkDestroyPipeline(context_.device(), pipeline_, nullptr);
        if (pipelineLayout_ != VK_NULL_HANDLE)
            vkDestroyPipelineLayout(context_.device(), pipelineLayout_, nullptr);
        if (descriptorSetLayout_ != VK_NULL_HANDLE)
            vkDestroyDescriptorSetLayout(context_.device(), descriptorSetLayout_, nullptr);
        descriptorPool_ = VK_NULL_HANDLE;
        pipeline_ = VK_NULL_HANDLE;
        pipelineLayout_ = VK_NULL_HANDLE;
        descriptorSetLayout_ = VK_NULL_HANDLE;
        descriptorSet_ = VK_NULL_HANDLE;
    }

    VulkanContext& context_;
    VkDescriptorSetLayout descriptorSetLayout_ = VK_NULL_HANDLE;
    VkPipelineLayout pipelineLayout_ = VK_NULL_HANDLE;
    VkPipeline pipeline_ = VK_NULL_HANDLE;
    VkDescriptorPool descriptorPool_ = VK_NULL_HANDLE;
    VkDescriptorSet descriptorSet_ = VK_NULL_HANDLE;
    std::uint32_t pushConstantBytes_ = 0;
    std::uint32_t storageBindingCount_ = 0;
    std::vector<StorageBinding> storageBindings_;
};
