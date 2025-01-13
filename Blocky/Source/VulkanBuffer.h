#pragma once

// Simply strengthen the enums so I dont make such mistakes
enum class memory_property : u32
{
    None = 0,
    Local = VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT,
    HostVisible = VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT,
    Coherent = VK_MEMORY_PROPERTY_HOST_COHERENT_BIT,
    HostCached = VK_MEMORY_PROPERTY_HOST_CACHED_BIT
};
ENABLE_BITWISE_OPERATORS(memory_property, u32)

enum class buffer_usage : u32
{
    None = 0,
    TransferSource = VK_BUFFER_USAGE_TRANSFER_SRC_BIT,
    TransferDestination = VK_BUFFER_USAGE_TRANSFER_DST_BIT,
    IndexBuffer = VK_BUFFER_USAGE_INDEX_BUFFER_BIT,
    VertexBuffer = VK_BUFFER_USAGE_VERTEX_BUFFER_BIT
};
ENABLE_BITWISE_OPERATORS(buffer_usage, u32)

struct vulkan_buffer
{
    VkBuffer Handle;
    VkDeviceMemory MemoryHandle;
    void* MappedData = nullptr;
    u64 Size;
};

u32 VulkanFindMemoryType(VkPhysicalDevice PhysicalDevice, u32 TypeFilter, VkMemoryPropertyFlags Properties)
{
    VkPhysicalDeviceMemoryProperties memProperties;
    vkGetPhysicalDeviceMemoryProperties(PhysicalDevice, &memProperties);
    for (u32 i = 0; i < memProperties.memoryTypeCount; ++i)
    {
        if ((TypeFilter & (1 << i)) && (memProperties.memoryTypes[i].propertyFlags & Properties) == Properties)
        {
            return i;
        }
    }

    for (u32 i = 0; i < memProperties.memoryHeapCount; ++i)
    {
        if ((TypeFilter & (1 << i)) && (memProperties.memoryHeaps[i].flags & Properties) == Properties)
        {
            return i;
        }
    }

    Assert(false, "Could not find proper memory type.");
    return 0;
}

// Vulkan resources
static vulkan_buffer VulkanBufferCreate(VkDevice Device, VkPhysicalDevice PhysicalDevice, buffer_usage Usage, memory_property MemoryProperty, u64 Size, bool Map)
{
    vulkan_buffer Buffer;

    Buffer.Size = Size;
    VkBufferCreateInfo CreateInfo = {};
    CreateInfo.sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO;
    CreateInfo.pNext = nullptr;
    CreateInfo.size = Size;
    CreateInfo.usage = static_cast<VkBufferUsageFlags>(Usage);
    CreateInfo.sharingMode = VK_SHARING_MODE_EXCLUSIVE;

    VkAssert(vkCreateBuffer(Device, &CreateInfo, nullptr, &Buffer.Handle));

    // Query requirements
    VkMemoryRequirements MemRequirements;
    vkGetBufferMemoryRequirements(Device, Buffer.Handle, &MemRequirements);

    VkMemoryAllocateInfo AllocateInfo = {};
    AllocateInfo.sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO;
    AllocateInfo.pNext = nullptr;
    AllocateInfo.allocationSize = MemRequirements.size;
    AllocateInfo.memoryTypeIndex = VulkanFindMemoryType(PhysicalDevice, MemRequirements.memoryTypeBits, static_cast<VkMemoryPropertyFlags>(MemoryProperty));

    // Allocate memory
    VkAssert(vkAllocateMemory(Device, &AllocateInfo, nullptr, &Buffer.MemoryHandle));

    // Bind buffer with memory
    VkAssert(vkBindBufferMemory(Device, Buffer.Handle, Buffer.MemoryHandle, 0));

    // Access allocated buffer
    if (Map)
    {
        VkAssert(vkMapMemory(Device, Buffer.MemoryHandle, 0, Buffer.Size, 0, &Buffer.MappedData));
    }

    return Buffer;
}

static void VulkanBufferDestroy(VkDevice Device, vulkan_buffer& Buffer)
{
    if (Buffer.MappedData)
        vkUnmapMemory(Device, Buffer.MemoryHandle);
    vkDestroyBuffer(Device, Buffer.Handle, nullptr);
    vkFreeMemory(Device, Buffer.MemoryHandle, nullptr);

    Buffer = {};
}

static void VulkanBufferSubmitFullCopy(VkCommandBuffer CommandBuffer, vulkan_buffer Source, vulkan_buffer Destination)
{
    Assert(Source.Size <= Destination.Size, "Destination is smaller than source!");

    VkBufferCopy copyRegion = {};
    copyRegion.srcOffset = 0;
    copyRegion.dstOffset = 0;
    copyRegion.size = Source.Size;
    vkCmdCopyBuffer(CommandBuffer, Source.Handle, Destination.Handle, 1, &copyRegion);
}
