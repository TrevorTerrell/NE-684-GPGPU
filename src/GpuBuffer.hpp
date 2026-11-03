#pragma once

#include "VulkanContext.hpp"
#include "VulkanUtils.hpp"

#include <vk_mem_alloc.h>
#include <vulkan/vulkan.h>

#include <cstddef>
#include <cstring>
#include <stdexcept>
#include <utility>

class GpuBuffer {
public:
    GpuBuffer() = default;

    GpuBuffer(
        VulkanContext& context,
        VkDeviceSize size,
        VkBufferUsageFlags usage,
        bool hostVisible)
        : context_(&context), size_(size), hostVisible_(hostVisible)
    {
        if (size == 0) {
            throw std::invalid_argument("GpuBuffer size must be greater than zero");
        }

        VkBufferCreateInfo bufferInfo{};
        bufferInfo.sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO;
        bufferInfo.size = size;
        bufferInfo.usage = usage;
        bufferInfo.sharingMode = VK_SHARING_MODE_EXCLUSIVE;

        VmaAllocationCreateInfo allocationInfo{};
        allocationInfo.usage = VMA_MEMORY_USAGE_AUTO;
        if (hostVisible) {
            allocationInfo.flags =
                VMA_ALLOCATION_CREATE_HOST_ACCESS_SEQUENTIAL_WRITE_BIT |
                VMA_ALLOCATION_CREATE_MAPPED_BIT;
        }

        VmaAllocationInfo actualInfo{};
        vkCheck(
            vmaCreateBuffer(
                context.allocator(), &bufferInfo, &allocationInfo,
                &buffer_, &allocation_, hostVisible ? &actualInfo : nullptr),
            "vmaCreateBuffer");

        if (hostVisible) {
            mapped_ = actualInfo.pMappedData;
        }
    }

    ~GpuBuffer() { reset(); }

    GpuBuffer(const GpuBuffer&) = delete;
    GpuBuffer& operator=(const GpuBuffer&) = delete;

    GpuBuffer(GpuBuffer&& other) noexcept { moveFrom(other); }

    GpuBuffer& operator=(GpuBuffer&& other) noexcept
    {
        if (this != &other) {
            reset();
            moveFrom(other);
        }
        return *this;
    }

    VkBuffer handle() const noexcept { return buffer_; }
    VkDeviceSize size() const noexcept { return size_; }
    void* mappedData() const noexcept { return mapped_; }
    bool hostVisible() const noexcept { return hostVisible_; }

    void write(const void* source, VkDeviceSize bytes, VkDeviceSize offset = 0)
    {
        requireMapped();
        checkRange(bytes, offset);
        std::memcpy(static_cast<char*>(mapped_) + offset, source, static_cast<std::size_t>(bytes));
        vkCheck(vmaFlushAllocation(context_->allocator(), allocation_, offset, bytes),
                "vmaFlushAllocation");
    }

    void read(void* destination, VkDeviceSize bytes, VkDeviceSize offset = 0)
    {
        requireMapped();
        checkRange(bytes, offset);
        vkCheck(vmaInvalidateAllocation(context_->allocator(), allocation_, offset, bytes),
                "vmaInvalidateAllocation");
        std::memcpy(destination, static_cast<char*>(mapped_) + offset,
                    static_cast<std::size_t>(bytes));
    }

    void flush(VkDeviceSize offset = 0, VkDeviceSize bytes = VK_WHOLE_SIZE)
    {
        if (!hostVisible_) throw std::logic_error("Cannot flush a non-host-visible buffer");
        vkCheck(vmaFlushAllocation(context_->allocator(), allocation_, offset, bytes),
                "vmaFlushAllocation");
    }

    void invalidate(VkDeviceSize offset = 0, VkDeviceSize bytes = VK_WHOLE_SIZE)
    {
        if (!hostVisible_) throw std::logic_error("Cannot invalidate a non-host-visible buffer");
        vkCheck(vmaInvalidateAllocation(context_->allocator(), allocation_, offset, bytes),
                "vmaInvalidateAllocation");
    }

private:
    void requireMapped() const
    {
        if (!context_ || !mapped_) {
            throw std::logic_error("Buffer is not host mapped");
        }
    }

    void checkRange(VkDeviceSize bytes, VkDeviceSize offset) const
    {
        if (offset > size_ || bytes > size_ - offset) {
            throw std::out_of_range("GpuBuffer read/write exceeds allocation size");
        }
    }

    void reset() noexcept
    {
        if (context_ && buffer_ != VK_NULL_HANDLE) {
            vmaDestroyBuffer(context_->allocator(), buffer_, allocation_);
        }
        context_ = nullptr;
        buffer_ = VK_NULL_HANDLE;
        allocation_ = VK_NULL_HANDLE;
        mapped_ = nullptr;
        size_ = 0;
        hostVisible_ = false;
    }

    void moveFrom(GpuBuffer& other) noexcept
    {
        context_ = std::exchange(other.context_, nullptr);
        buffer_ = std::exchange(other.buffer_, VK_NULL_HANDLE);
        allocation_ = std::exchange(other.allocation_, VK_NULL_HANDLE);
        mapped_ = std::exchange(other.mapped_, nullptr);
        size_ = std::exchange(other.size_, 0);
        hostVisible_ = std::exchange(other.hostVisible_, false);
    }

    VulkanContext* context_ = nullptr; // Context must outlive every GpuBuffer.
    VkBuffer buffer_ = VK_NULL_HANDLE;
    VmaAllocation allocation_ = VK_NULL_HANDLE;
    void* mapped_ = nullptr;
    VkDeviceSize size_ = 0;
    bool hostVisible_ = false;
};
