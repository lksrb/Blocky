#pragma once

struct vulkan_buffer
{
    VkBuffer BufferHandle;
    VkDeviceMemory MemoryHandle;
    void* MappedData = nullptr;
    u64 Size;
};

// Vulkan resources
inline vulkan_buffer CreateVulkanBuffer(VkDevice Device, VkBufferUsageFlagBits Usage, u64 Size, bool Host, bool Map)
{
    vulkan_buffer Buffer;

    Buffer.Size = Size;
    VkBufferCreateInfo createInfo = {};
    createInfo.sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO;
    createInfo.pNext = nullptr;
    createInfo.size = Size;
    createInfo.usage = Usage;
    createInfo.sharingMode = VK_SHARING_MODE_EXCLUSIVE;

    VkAssert(vkCreateBuffer(Device, &createInfo, nullptr, &Buffer.BufferHandle));

    // Query requirements
    VkMemoryRequirements memRequirements;
    vkGetBufferMemoryRequirements(Device, Buffer.BufferHandle, &memRequirements);

    VkMemoryAllocateInfo allocateInfo = {};
    allocateInfo.sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO;
    allocateInfo.pNext = nullptr;
    allocateInfo.allocationSize = memRequirements.size;
    //allocateInfo.memoryTypeIndex = Vulkan::FindMemoryType(memRequirements.memoryTypeBits,
    //    (host ? VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT : VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT) | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT /* for debugging purposes*/);

    // Allocate memory
    VkAssert(vkAllocateMemory(Device, &allocateInfo, nullptr, &Buffer.MemoryHandle));

    // Bind buffer with memory
    VkAssert(vkBindBufferMemory(Device, Buffer.BufferHandle, Buffer.MemoryHandle, 0));

    // Access allocated buffer
    if (Map)
    {
        VkAssert(vkMapMemory(Device, Buffer.MemoryHandle, 0, Buffer.Size, 0, &Buffer.MappedData));
    }

    return Buffer;
}

inline void DestroyVulkanBuffer(VkDevice Device, vulkan_buffer& Buffer)
{
    vkUnmapMemory(Device, Buffer.MemoryHandle);
    vkDestroyBuffer(Device, Buffer.BufferHandle, nullptr);
    vkFreeMemory(Device, Buffer.MemoryHandle, nullptr);

    Buffer = {};
}
