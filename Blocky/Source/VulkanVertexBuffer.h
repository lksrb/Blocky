#pragma once

struct vulkan_vertex_buffer
{
    vulkan_buffer StagingBuffer;
    vulkan_buffer DeviceLocalBuffer;
};

static vulkan_vertex_buffer VulkanVertexBufferCreate(VkDevice Device, VkPhysicalDevice PhysicalDevice, u32 Size)
{
    vulkan_vertex_buffer Buffer;
    Buffer.StagingBuffer = VulkanBufferCreate(Device, PhysicalDevice, buffer_usage::TransferSource, memory_property::HostVisible | memory_property::Coherent, Size, true);
    Buffer.DeviceLocalBuffer = VulkanBufferCreate(Device, PhysicalDevice, buffer_usage::VertexBuffer | buffer_usage::TransferDestination, memory_property::Local | memory_property::Coherent, Size, false);
    return Buffer;
}

static void VulkanVertexBufferDestroy(VkDevice Device, vulkan_vertex_buffer& Buffer)
{
    VulkanBufferDestroy(Device, Buffer.StagingBuffer);
    VulkanBufferDestroy(Device, Buffer.DeviceLocalBuffer);
}

static void VulkanVertexBufferSetData(vulkan_vertex_buffer Buffer, VkCommandBuffer CommandBuffer, const void* vertices, u32 size)
{
    if (size == 0)
        return;

    Assert(size <= Buffer.StagingBuffer.Size, "size <= Buffer.StagingBuffer.Size");

    // Copy vertices to staging buffer
    memcpy(Buffer.StagingBuffer.MappedData, vertices, size);

    VkBufferCopy copyRegion = {};
    copyRegion.srcOffset = 0; // Optional
    copyRegion.dstOffset = 0; // Optional
    copyRegion.size = size;

    vkCmdCopyBuffer(CommandBuffer, Buffer.StagingBuffer.Handle, Buffer.DeviceLocalBuffer.Handle, 1, &copyRegion);
}
