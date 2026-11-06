#include <complex>

#include "CommandExecutor.hpp"
#include "ComputeKernel.hpp"
#include "GpuBuffer.hpp"
#include "VulkanContext.hpp"

#include <vulkan/vulkan.h>

#include <cstdint>
#include <iostream>
#include <vector>
#include <fstream>
#include <sstream>
#include <chrono>

#ifndef SHADER_PATH
#error SHADER_PATH must be defined by CMake
#endif

struct SimulationParameters {
    uint32_t seed;
    uint32_t neutronCount;
    float rouletteThreshold;
    float rouletteKillRate;
};

int main()
{
    // Load Cross Section data from CSV
    constexpr uint32_t ENERGY_POINTS = 10'000;
    constexpr uint32_t COLUMNS = 9; // Energy, U fiss, U cap, U scat, same for Pu, C scat, total

    constexpr uint32_t NUM_PARTICLES = 1'000'000;
    constexpr uint32_t WORK_GROUP_SIZE = 256;
    constexpr float ROULETTE_THRESHOLD = 0.01f;
    constexpr float ROULETTE_LIVE_RATE = 0.01f;

    static_assert(sizeof(SimulationParameters) == 16);

    std::vector<float> crossSections; crossSections.reserve(ENERGY_POINTS * COLUMNS);

    std::ifstream file("../cross_sections_300K.csv");
    if (!file.is_open()) {
        std::cerr << "Error: Could not open cross section file\n";
        return 1;
    }
    { // This is in a block so that line, r, and c have limited scope
        std::string line;
        uint32_t r = 0;
        while (std::getline(file, line)) {

            std::stringstream ss(line);
            std::string field;
            uint32_t c = 0;
            while (std::getline(ss, field, ',')) {
                if (c == 0 || c == COLUMNS - 1) {++c; continue;}; // Removing Energy and Total from Cross sections to save 78 KiB memory
                if (c >= COLUMNS) throw std::runtime_error("Too many columns in cross section file.");
                crossSections.push_back(std::stof(field));
                ++c;
            }
            if (c != COLUMNS) throw std::runtime_error(
                std::format("Too few columns ({}) in cross section file.", c));
            ++r;
        }
        if (r != ENERGY_POINTS) throw std::runtime_error("Incorrect number of energies in cross section file.");
    }

    SimulationParameters params{
        1682603,
        NUM_PARTICLES,
        ROULETTE_THRESHOLD,
        ROULETTE_LIVE_RATE,
    };

    try {
        // Declare the context first so all objects that depend on it are
        // destroyed before the device, allocator, and instance.
        VulkanContext context;

        const VkDeviceSize buffXSecBytes = crossSections.size() * sizeof(float);
        GpuBuffer bufferXSec(context, buffXSecBytes, VK_BUFFER_USAGE_STORAGE_BUFFER_BIT, true);
        bufferXSec.write(crossSections.data(), buffXSecBytes);

        const VkDeviceSize bufftallyBytes = NUM_PARTICLES * sizeof(float);
        GpuBuffer bufferTally(context, bufftallyBytes, VK_BUFFER_USAGE_STORAGE_BUFFER_BIT, true);

        ComputeKernel historyKernel(
            context, SHADER_PATH,
            {{0, VK_SHADER_STAGE_COMPUTE_BIT},
            {1, VK_SHADER_STAGE_COMPUTE_BIT}},
            sizeof(SimulationParameters));

        historyKernel.updateStorageBuffers(
            {bufferXSec.handle(), bufferTally.handle()},
            {buffXSecBytes, bufftallyBytes});

        CommandExecutor executor(context);
        VkCommandBuffer cmd = executor.begin();

        historyKernel.bind(cmd);
        historyKernel.pushConstants(cmd, &params, sizeof(params));

        const std::uint32_t groups = (NUM_PARTICLES + WORK_GROUP_SIZE - 1) / WORK_GROUP_SIZE;

        const auto startTime = std::chrono::high_resolution_clock::now();

        vkCmdDispatch(cmd, groups, 1, 1);

        // Execution completion via the fence makes host reads safe. The
        // invalidate in GpuBuffer::read handles non-coherent host memory.
        executor.submitAndWait();
        float k_inf {};
        float k2_inf {};
        float k_var {};
        {
            std::vector<float> k_inf_tally(NUM_PARTICLES);
            bufferTally.read(k_inf_tally.data(), bufftallyBytes);
            for (uint32_t i = 0; i < NUM_PARTICLES; ++i) {
                k_inf += k_inf_tally[i];
                k2_inf += k_inf_tally[i] * k_inf_tally[i];
            }

            k_var = std::abs(std::pow(k_inf / NUM_PARTICLES, 2.0f) - k2_inf / NUM_PARTICLES) / static_cast<float>(NUM_PARTICLES - 1);
        }

        const auto endTime = std::chrono::high_resolution_clock::now();
        const auto deltaTime = endTime - startTime;
        const auto dt = static_cast<double>(deltaTime.count());

        std::cout << std::fixed << std::setprecision(5) << "k_inf = " << k_inf / NUM_PARTICLES << " +/- " << std::sqrt(k_var) << "\n";
        std::cout << "Elapsed time: " << dt / 1e6 << " ms \n";
        std::cout << "FOM: " << 1.0 / (dt / 1e9 * k_var) << "\n";

        return 0;
    } catch (const std::exception& e) {
        std::cerr << "Error: " << e.what() << '\n';
        return 1;
    }
}
