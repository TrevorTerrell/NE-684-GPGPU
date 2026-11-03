# Vulkan compute starter refactor

This is a small refactor of the original vector-add sample into reusable components:

- `VulkanContext`: instance, physical/logical device, compute queue, VMA allocator.
- `GpuBuffer`: move-only VMA buffer wrapper, mapped host access, flush/invalidate.
- `ComputeKernel`: compute pipeline and descriptor set for a kernel's fixed binding layout.
- `CommandExecutor`: reusable command buffer and fence for simple submit-and-wait cycles.
- `VulkanUtils.hpp`: Vulkan error checking and SPIR-V loading.

## Build

Configure with CMake in CLion as before. The Vulkan SDK must provide `glslc`.
The sample compiles `shaders/add.comp` to `add.spv` and defines `SHADER_PATH`.

## Important design constraint: dynamic buffers and descriptors

A Vulkan descriptor-set layout is part of a pipeline's interface and is not arbitrary
per dispatch. This sample lets you replace which `VkBuffer` each binding refers to
between cycles, but the number and binding numbers are fixed when `ComputeKernel`
is created. The helper accepts explicit binding numbers; buffer arguments to
`updateStorageBuffers` must follow the same order as the `StorageBinding` list
passed to the constructor.

For a neutronics solver, define a stable interface per kernel/pass (for example,
particle state in/out, material/geometry data, tally buffers, RNG state, and queue
counters), then update descriptors between cycles as needed. If one shader truly needs
an unbounded number of independently bound buffers, investigate descriptor indexing
and runtime descriptor arrays, but feature support varies by device/driver; do not
assume it is available on every MoltenVK target.

Often the simpler and more portable option is a small number of large storage buffers
(or a buffer arena) with offsets/counts describing each region. For event-based transport,
particle queues can be represented by storage buffers plus counters; for history-based
transport, state arrays can be kept in stable buffers and ping-ponged between steps.

## Synchronization

The sample submits one command buffer and waits for a fence every cycle. That is easy
to reason about but serializes CPU/GPU work. Later, for performance, use multiple frames/
cycles in flight, fences or timeline semaphores, and explicit buffer barriers between
compute passes. Barriers between dispatches must describe the actual producer/consumer
accesses; a host-read barrier is not a universal inter-pass barrier.

`GpuBuffer::write` flushes mapped allocations and `GpuBuffer::read` invalidates them.
Host readback must occur after the relevant GPU work has completed. Device-local buffers
that are not host visible need staging buffers or a separate transfer path for CPU access.

## Lifetime rule

`VulkanContext` must outlive every `GpuBuffer`, `ComputeKernel`, and `CommandExecutor`
created from it. C++ declaration order in `main.cpp` follows that rule, so dependent
objects are destroyed before the context.
