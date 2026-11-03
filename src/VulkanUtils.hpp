#pragma once

#include <vulkan/vulkan.h>

#include <cstdint>
#include <fstream>
#include <stdexcept>
#include <string>
#include <vector>

inline void vkCheck(VkResult result, const char* operation)
{
    if (result != VK_SUCCESS) {
        throw std::runtime_error(
            std::string(operation) + " failed with VkResult " +
            std::to_string(static_cast<int>(result)));
    }
}

inline std::vector<std::uint32_t> loadSpirv(const char* filename)
{
    std::ifstream file(filename, std::ios::binary | std::ios::ate);
    if (!file) {
        throw std::runtime_error(std::string("Cannot open SPIR-V file: ") + filename);
    }

    const std::streamsize byteCount = file.tellg();
    if (byteCount <= 0 || byteCount % sizeof(std::uint32_t) != 0) {
        throw std::runtime_error("Invalid SPIR-V file size");
    }

    std::vector<std::uint32_t> code(
        static_cast<std::size_t>(byteCount) / sizeof(std::uint32_t));

    file.seekg(0);
    if (!file.read(reinterpret_cast<char*>(code.data()), byteCount)) {
        throw std::runtime_error(std::string("Failed to read SPIR-V file: ") + filename);
    }

    return code;
}
