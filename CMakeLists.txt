cmake_minimum_required(VERSION 3.24)
project(NeutronCompute LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

include(FetchContent)
find_package(Vulkan REQUIRED)

FetchContent_Declare(
    vk-bootstrap
    GIT_REPOSITORY https://github.com/charles-lunarg/vk-bootstrap.git
    GIT_TAG        v1.4.366
)
FetchContent_MakeAvailable(vk-bootstrap)

FetchContent_Declare(
    VulkanMemoryAllocator
    GIT_REPOSITORY https://github.com/GPUOpen-LibrariesAndSDKs/VulkanMemoryAllocator.git
    GIT_TAG        v3.5.0
)
FetchContent_MakeAvailable(VulkanMemoryAllocator)

add_executable(NeutronCompute
    src/main.cpp
    src/VulkanContext.cpp
    src/VmaImplementation.cpp
)

target_include_directories(NeutronCompute PRIVATE src)
target_link_libraries(NeutronCompute PRIVATE
    Vulkan::Vulkan
    vk-bootstrap::vk-bootstrap
    GPUOpen::VulkanMemoryAllocator
)

find_program(GLSLC_EXECUTABLE
    NAMES glslc
    HINTS "$ENV{VULKAN_SDK}/Bin" "$ENV{VULKAN_SDK}/bin"
    REQUIRED
)

set(SHADER_SOURCE "${CMAKE_CURRENT_SOURCE_DIR}/shaders/history.comp")
set(SHADER_BINARY "${CMAKE_CURRENT_BINARY_DIR}/history.spv")

add_custom_command(
    OUTPUT "${SHADER_BINARY}"
    COMMAND "${GLSLC_EXECUTABLE}" "${SHADER_SOURCE}" -o "${SHADER_BINARY}"
    DEPENDS "${SHADER_SOURCE}"
    VERBATIM
)
add_custom_target(ComputeShaders DEPENDS "${SHADER_BINARY}")
add_dependencies(NeutronCompute ComputeShaders)

target_compile_definitions(NeutronCompute PRIVATE SHADER_PATH="${SHADER_BINARY}")
