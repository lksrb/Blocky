#pragma once

#define IMAGE_FORMAT_RGBA 4

#define STB_IMAGE_IMPLEMENTATION
#define STB_IMAGE_STATIC
#include "../DX12/stb_image.h"

struct texture
{
    u32 width = 0;
    u32 height = 0;
    VkImage Handle = nullptr;
    VkImageView imageViewHandle = nullptr;
    VkDeviceMemory imageMemoryHandle = nullptr;
    VkSampler samplerHandle = nullptr;
    VkFilter filter = VK_FILTER_MAX_ENUM;
};

static u32 FindMemoryType(VkPhysicalDevice PhysicalDevice, u32 typeFilter, VkMemoryPropertyFlags properties)
{
    VkPhysicalDeviceMemoryProperties memProperties;
    vkGetPhysicalDeviceMemoryProperties(PhysicalDevice, &memProperties);
    for (u32 i = 0; i < memProperties.memoryTypeCount; ++i)
    {
        if ((typeFilter & (1 << i)) && (memProperties.memoryTypes[i].propertyFlags & properties) == properties)
        {
            return i;
        }
    }

    for (u32 i = 0; i < memProperties.memoryHeapCount; ++i)
    {
        if ((typeFilter & (1 << i)) && (memProperties.memoryHeaps[i].flags & properties) == properties)
        {
            return i;
        }
    }

    Assert(false, "Could not find proper memory type.");
    return 0;
}

// Helper functions
template<typename F>
static void SubmitImmediateCommandBuffer(VkDevice Device, VkCommandPool CommandPool, VkQueue GraphicsQueue, F&& func)
{
    VkCommandBuffer commandBuffer = nullptr;

    VkCommandBufferAllocateInfo cmdBufAllocateInfo = {};
    cmdBufAllocateInfo.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO;
    cmdBufAllocateInfo.commandPool = CommandPool;
    cmdBufAllocateInfo.level = VK_COMMAND_BUFFER_LEVEL_PRIMARY;
    cmdBufAllocateInfo.commandBufferCount = 1;

    VkAssert(vkAllocateCommandBuffers(Device, &cmdBufAllocateInfo, &commandBuffer));

    VkCommandBufferBeginInfo commandBufferBeginInfo = {};
    commandBufferBeginInfo.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO;
    VkAssert(vkBeginCommandBuffer(commandBuffer, &commandBufferBeginInfo));

    // Callback
    func(commandBuffer);

    VkAssert(vkEndCommandBuffer(commandBuffer));

    VkSubmitInfo submitInfo = {};
    submitInfo.sType = VK_STRUCTURE_TYPE_SUBMIT_INFO;
    submitInfo.commandBufferCount = 1;
    submitInfo.pCommandBuffers = &commandBuffer;

    // Create fence to ensure that the command buffer has finished executing
    VkFenceCreateInfo fenceCreateInfo = {};
    fenceCreateInfo.sType = VK_STRUCTURE_TYPE_FENCE_CREATE_INFO;
    fenceCreateInfo.flags = 0;
    VkFence fence;
    VkAssert(vkCreateFence(Device, &fenceCreateInfo, nullptr, &fence));

    // Submit to the queue
    VkAssert(vkQueueSubmit(GraphicsQueue, 1, &submitInfo, fence));

    // Wait for the fence to signal that command buffer has finished executing
    VkAssert(vkWaitForFences(Device, 1, &fence, VK_TRUE, UINT64_MAX));

    vkDestroyFence(Device, fence, nullptr);
    vkFreeCommandBuffers(Device, CommandPool, 1, &commandBuffer);
}

static texture TextureCreate(VkDevice Device, VkPhysicalDevice PhysicalDevice, VkCommandPool CommandPool, VkQueue GraphicsQueue, u32 width, u32 height, void* data, u64 size, VkFilter filter)
{
    Assert(size == static_cast<u64>(width) * static_cast<u64>(height) * IMAGE_FORMAT_RGBA, "Data size and maximum size does not match!");

    const VkFormat format = VK_FORMAT_R8G8B8A8_UNORM;

    texture Texture;
    Texture.width = width;
    Texture.height = height;
    Texture.filter = filter;

    // Create vulkan image
    {
        VkImageCreateInfo imageCreateInfo = {};
        imageCreateInfo.sType = VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO;
        imageCreateInfo.pNext = nullptr;
        imageCreateInfo.imageType = VK_IMAGE_TYPE_2D;
        imageCreateInfo.extent = { static_cast<u32>(width), static_cast<u32>(height), 1 };
        imageCreateInfo.usage = VK_IMAGE_USAGE_SAMPLED_BIT | VK_IMAGE_USAGE_TRANSFER_DST_BIT; // We want to sample from it and send data to it
        imageCreateInfo.format = format;
        imageCreateInfo.arrayLayers = 1;
        imageCreateInfo.mipLevels = 1;
        imageCreateInfo.tiling = VK_IMAGE_TILING_OPTIMAL;
        imageCreateInfo.samples = VK_SAMPLE_COUNT_1_BIT;
        imageCreateInfo.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
        imageCreateInfo.sharingMode = VK_SHARING_MODE_EXCLUSIVE;
        VkAssert(vkCreateImage(Device, &imageCreateInfo, nullptr, &Texture.Handle));

        // Image memory
        VkMemoryRequirements memRequirements;
        vkGetImageMemoryRequirements(Device, Texture.Handle, &memRequirements);

        VkMemoryAllocateInfo allocInfo = {};
        allocInfo.sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO;
        allocInfo.allocationSize = memRequirements.size;
        allocInfo.memoryTypeIndex = FindMemoryType(PhysicalDevice, memRequirements.memoryTypeBits, VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT); // Data in VRAM
        VkAssert(vkAllocateMemory(Device, &allocInfo, nullptr, &Texture.imageMemoryHandle));
        VkAssert(vkBindImageMemory(Device, Texture.Handle, Texture.imageMemoryHandle, 0));

        // Image view
        VkImageViewCreateInfo createInfo = {};
        createInfo.sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO;
        createInfo.image = Texture.Handle;
        createInfo.viewType = VK_IMAGE_VIEW_TYPE_2D;
        createInfo.format = format;
        createInfo.components.r = VK_COMPONENT_SWIZZLE_IDENTITY;
        createInfo.components.g = VK_COMPONENT_SWIZZLE_IDENTITY;
        createInfo.components.b = VK_COMPONENT_SWIZZLE_IDENTITY;
        createInfo.components.a = VK_COMPONENT_SWIZZLE_IDENTITY;
        createInfo.subresourceRange = { VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1 };
        VkAssert(vkCreateImageView(Device, &createInfo, nullptr, &Texture.imageViewHandle));

        // Sampler
        VkSamplerCreateInfo samplerInfo = {};
        samplerInfo.sType = VK_STRUCTURE_TYPE_SAMPLER_CREATE_INFO;
        samplerInfo.magFilter = filter;
        samplerInfo.minFilter = filter;
        samplerInfo.addressModeU = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
        samplerInfo.addressModeV = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
        samplerInfo.addressModeW = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
        samplerInfo.anisotropyEnable = VK_FALSE;
        samplerInfo.maxAnisotropy = 1.0f;
        samplerInfo.borderColor = VK_BORDER_COLOR_FLOAT_OPAQUE_WHITE;
        samplerInfo.unnormalizedCoordinates = VK_FALSE;
        samplerInfo.compareEnable = VK_FALSE;
        samplerInfo.compareOp = VK_COMPARE_OP_NEVER;

        samplerInfo.mipmapMode = VK_SAMPLER_MIPMAP_MODE_NEAREST;
        samplerInfo.mipLodBias = 0.0f;
        samplerInfo.minLod = 0.0f;
        samplerInfo.maxLod = 0.0f;

        VkAssert(vkCreateSampler(Device, &samplerInfo, nullptr, &Texture.samplerHandle));
    }

    // Staging buffer
    VkBuffer stagingBufferHandle = nullptr;
    VkDeviceMemory stagingBufferMemoryHandle = nullptr;

    {
        VkBufferCreateInfo stagingCreateInfo = {};
        stagingCreateInfo.sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO;
        stagingCreateInfo.pNext = nullptr;
        stagingCreateInfo.size = width * height * IMAGE_FORMAT_RGBA;
        stagingCreateInfo.usage = VK_BUFFER_USAGE_TRANSFER_SRC_BIT;
        stagingCreateInfo.sharingMode = VK_SHARING_MODE_EXCLUSIVE;

        VkAssert(vkCreateBuffer(Device, &stagingCreateInfo, nullptr, &stagingBufferHandle));

        VkMemoryRequirements memRequirements;
        vkGetBufferMemoryRequirements(Device, stagingBufferHandle, &memRequirements);

        VkMemoryAllocateInfo allocateInfo = {};
        allocateInfo.sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO;
        allocateInfo.pNext = nullptr;
        allocateInfo.allocationSize = memRequirements.size;
        allocateInfo.memoryTypeIndex = FindMemoryType(PhysicalDevice, memRequirements.memoryTypeBits, VK_MEMORY_PROPERTY_HOST_COHERENT_BIT | VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT);

        VkAssert(vkAllocateMemory(Device, &allocateInfo, nullptr, &stagingBufferMemoryHandle));
        VkAssert(vkBindBufferMemory(Device, stagingBufferHandle, stagingBufferMemoryHandle, 0));

        // Set data into the staging buffer
        void* stagingBufferData;
        vkMapMemory(Device, stagingBufferMemoryHandle, 0, stagingCreateInfo.size, 0, &stagingBufferData);
        Assert(allocateInfo.allocationSize >= stagingCreateInfo.size, "Not enough VRAM!");
        memcpy(stagingBufferData, data, allocateInfo.allocationSize);
        vkUnmapMemory(Device, stagingBufferMemoryHandle);
    }

    // Send pixel data to gpu
    SubmitImmediateCommandBuffer(Device, CommandPool, GraphicsQueue, [imageHandle = Texture.Handle, width, height, stagingBufferHandle](VkCommandBuffer commandBuffer)
    {
        // Pipeline barrier, transition image layout 1
        {
            VkImageMemoryBarrier barrier = {};
            barrier.sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER;
            barrier.oldLayout = VK_IMAGE_LAYOUT_UNDEFINED;
            barrier.newLayout = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL;
            barrier.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
            barrier.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
            barrier.image = imageHandle;
            barrier.subresourceRange = { VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1 };
            barrier.srcAccessMask = 0;
            barrier.dstAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;

            vkCmdPipelineBarrier(
                commandBuffer,
                VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT, VK_PIPELINE_STAGE_TRANSFER_BIT,
                0,
                0, nullptr,
                0, nullptr,
                1, &barrier
            );
        }

        // Copy buffer to image barrier
        {
            VkBufferImageCopy region = {};
            region.bufferOffset = 0;
            region.bufferRowLength = 0;
            region.bufferImageHeight = 0;
            region.imageSubresource = { VK_IMAGE_ASPECT_COLOR_BIT, 0, 0, 1 };
            region.imageOffset = { 0, 0, 0 };
            region.imageExtent = { static_cast<u32>(width), static_cast<u32>(height), 1 };

            vkCmdCopyBufferToImage(
                commandBuffer,
                stagingBufferHandle,
                imageHandle,
                VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
                1,
                &region
            );
        }

        // Pipeline barrier, transition image layout 2
        {
            VkImageMemoryBarrier barrier = {};
            barrier.sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER;
            barrier.oldLayout = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL;
            barrier.newLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
            barrier.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
            barrier.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
            barrier.image = imageHandle;
            barrier.subresourceRange = { VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1 };
            barrier.srcAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;
            barrier.dstAccessMask = VK_ACCESS_SHADER_READ_BIT;

            vkCmdPipelineBarrier(
                commandBuffer,
                VK_PIPELINE_STAGE_TRANSFER_BIT, VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT,
                0,
                0, nullptr,
                0, nullptr,
                1, &barrier
            );
        }
    });

    vkFreeMemory(Device, stagingBufferMemoryHandle, nullptr);
    vkDestroyBuffer(Device, stagingBufferHandle, nullptr);

    return Texture;
}

static void TextureDestroy(VkDevice Device, texture* Texture)
{
    vkDestroySampler(Device, Texture->samplerHandle, nullptr);
    vkDestroyImage(Device, Texture->Handle, nullptr);
    vkFreeMemory(Device, Texture->imageMemoryHandle, nullptr);
    vkDestroyImageView(Device, Texture->imageViewHandle, nullptr);

    *Texture = {};
}
