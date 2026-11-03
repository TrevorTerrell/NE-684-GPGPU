#include "CommandExecutor.hpp"
#include "ComputeKernel.hpp"
#include "GpuBuffer.hpp"
#include "VulkanContext.hpp"

#include <vulkan/vulkan.h>

#include <cstdint>
#include <iostream>
#include <vector>

#ifndef SHADER_PATH
#error SHADER_PATH must be defined by CMake
#endif

int main()
{
    try {
        // Declare the context first so all objects that depend on it are
        // destroyed before the device, allocator, and instance.
        VulkanContext context;

        constexpr std::uint32_t elementCount = 1024;
        constexpr VkDeviceSize bufferBytes = elementCount * sizeof(float);

        GpuBuffer bufferA(context, bufferBytes, VK_BUFFER_USAGE_STORAGE_BUFFER_BIT, true);
        GpuBuffer bufferB(context, bufferBytes, VK_BUFFER_USAGE_STORAGE_BUFFER_BIT, true);
        GpuBuffer bufferC(context, bufferBytes, VK_BUFFER_USAGE_STORAGE_BUFFER_BIT, true);

        std::vector<float> a(elementCount), b(elementCount), c(elementCount);
        for (std::uint32_t i = 0; i < elementCount; ++i) {
            a[i] = static_cast<float>(i);
            b[i] = static_cast<float>(i * 10);
        }
        bufferA.write(a.data(), bufferBytes);
        bufferB.write(b.data(), bufferBytes);

        // This add shader uses bindings 0, 1, and 2 and one uint push constant.
        ComputeKernel addKernel(
            context, SHADER_PATH,
            {{0, VK_SHADER_STAGE_COMPUTE_BIT},
             {1, VK_SHADER_STAGE_COMPUTE_BIT},
             {2, VK_SHADER_STAGE_COMPUTE_BIT}},
            sizeof(std::uint32_t));

        addKernel.updateStorageBuffers(
            {bufferA.handle(), bufferB.handle(), bufferC.handle()},
            {bufferBytes, bufferBytes, bufferBytes});

        CommandExecutor executor(context);
        VkCommandBuffer cmd = executor.begin();

        addKernel.bind(cmd);
        addKernel.pushConstants(cmd, &elementCount, sizeof(elementCount));

        constexpr std::uint32_t localSize = 256;
        const std::uint32_t groups = (elementCount + localSize - 1) / localSize;
        vkCmdDispatch(cmd, groups, 1, 1);

        // Execution completion via the fence makes host reads safe. The
        // invalidate in GpuBuffer::read handles non-coherent host memory.
        executor.submitAndWait();
        bufferC.read(c.data(), bufferBytes);

        bool correct = true;
        for (std::uint32_t i = 0; i < elementCount; ++i) {
            if (c[i] != a[i] + b[i]) {
                std::cerr << "Mismatch at " << i << ": expected "
                          << a[i] + b[i] << ", got " << c[i] << '\n';
                correct = false;
                break;
            }
        }

        for (std::uint32_t i = 0; i < 5; ++i) {
            std::cout << "A[" << i << "]=" << a[i]
                      << ", B[" << i << "]=" << b[i]
                      << ", C[" << i << "]=" << c[i] << '\n';
        }
        std::cout << (correct ? "Validation: PASS\n" : "Validation: FAIL\n");
        return correct ? 0 : 1;
    } catch (const std::exception& e) {
        std::cerr << "Error: " << e.what() << '\n';
        return 1;
    }
}
