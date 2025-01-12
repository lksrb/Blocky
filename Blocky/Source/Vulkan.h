#pragma once

#define VK_NO_PROTOTYPES
#define VK_USE_PLATFORM_WIN32_KHR
#define WIN32_VULKAN_LOADER_FILENAME "vulkan-1.dll"
#include <vulkan/vulkan.h>

#ifndef VK_API_VERSION_1_1
#error We require atleast VulkanSDK 1.1!
#endif

#define VulkanFunction(function) inline PFN_##function function
#define LoadVulkanFunction(function) function = (PFN_##function)vkGetInstanceProcAddr(instance, STRINGIFY(function))
#define LoadVulkanDeviceFunction(function) function = (PFN_##function)vkGetDeviceProcAddr(device, STRINGIFY(function))

// Loaded from the dll
VulkanFunction(vkEnumerateInstanceVersion);
VulkanFunction(vkCreateInstance);
VulkanFunction(vkGetInstanceProcAddr);

// Platform specific
VulkanFunction(vkCreateWin32SurfaceKHR);

// Loaded from instance loader
VulkanFunction(vkEnumeratePhysicalDevices);
VulkanFunction(vkGetPhysicalDeviceFeatures);
VulkanFunction(vkGetPhysicalDeviceProperties);
VulkanFunction(vkGetPhysicalDeviceMemoryProperties);
VulkanFunction(vkGetPhysicalDeviceFormatProperties);
VulkanFunction(vkEnumerateDeviceExtensionProperties);
VulkanFunction(vkGetDeviceProcAddr);
VulkanFunction(vkCreateDevice);
VulkanFunction(vkGetPhysicalDeviceSurfaceFormatsKHR);
VulkanFunction(vkGetPhysicalDeviceSurfacePresentModesKHR);
VulkanFunction(vkDestroySurfaceKHR);
VulkanFunction(vkDestroyInstance);
VulkanFunction(vkGetPhysicalDeviceQueueFamilyProperties);
VulkanFunction(vkGetPhysicalDeviceSurfaceCapabilitiesKHR);

// Loaded from device loader
VulkanFunction(vkGetDeviceQueue);
VulkanFunction(vkCreateCommandPool);
VulkanFunction(vkDestroyCommandPool);
VulkanFunction(vkDeviceWaitIdle);
VulkanFunction(vkDestroyDevice);

VulkanFunction(vkCreateRenderPass);
VulkanFunction(vkCreateSemaphore);
VulkanFunction(vkCreateFence);
VulkanFunction(vkAllocateCommandBuffers);
VulkanFunction(vkCreateImage);
VulkanFunction(vkAllocateMemory);
VulkanFunction(vkBindImageMemory);
VulkanFunction(vkCreateImageView);
VulkanFunction(vkCreateSampler);
VulkanFunction(vkCreateBuffer);
VulkanFunction(vkGetBufferMemoryRequirements);
VulkanFunction(vkBindBufferMemory);
VulkanFunction(vkMapMemory);
VulkanFunction(vkUnmapMemory);
VulkanFunction(vkCreateDescriptorPool);
VulkanFunction(vkCmdPipelineBarrier);
VulkanFunction(vkCmdCopyBufferToImage);
VulkanFunction(vkFreeMemory);
VulkanFunction(vkDestroyBuffer);
VulkanFunction(vkDestroySampler);
VulkanFunction(vkDestroyImage);
VulkanFunction(vkDestroyImageView);

VulkanFunction(vkCmdCopyBuffer);
VulkanFunction(vkCreateDescriptorSetLayout);
VulkanFunction(vkAllocateDescriptorSets);
VulkanFunction(vkUpdateDescriptorSets);
VulkanFunction(vkCreatePipelineLayout);
VulkanFunction(vkCreateGraphicsPipelines);
VulkanFunction(vkDestroyPipelineLayout);
VulkanFunction(vkDestroyPipeline);
VulkanFunction(vkCreateShaderModule);
VulkanFunction(vkDestroyShaderModule);
VulkanFunction(vkCmdCopyImageToBuffer);
VulkanFunction(vkCreateSwapchainKHR);
VulkanFunction(vkGetSwapchainImagesKHR);
VulkanFunction(vkCreateFramebuffer);
VulkanFunction(vkDestroyFramebuffer);
VulkanFunction(vkDestroySwapchainKHR);

VulkanFunction(vkWaitForFences);
VulkanFunction(vkBeginCommandBuffer);
VulkanFunction(vkEndCommandBuffer);
VulkanFunction(vkResetFences);
VulkanFunction(vkQueueSubmit);
VulkanFunction(vkDestroyDescriptorSetLayout);
VulkanFunction(vkDestroyDescriptorPool);
VulkanFunction(vkDestroyRenderPass);
VulkanFunction(vkDestroySemaphore);
VulkanFunction(vkDestroyFence);
VulkanFunction(vkFreeCommandBuffers);
VulkanFunction(vkAcquireNextImageKHR);
VulkanFunction(vkCmdBeginRenderPass);
VulkanFunction(vkCmdSetViewport);
VulkanFunction(vkCmdSetScissor);
VulkanFunction(vkCmdBindDescriptorSets);
VulkanFunction(vkCmdPushConstants);
VulkanFunction(vkCmdDrawIndexed);
VulkanFunction(vkCmdNextSubpass);
VulkanFunction(vkCmdBindPipeline);
VulkanFunction(vkCmdBindVertexBuffers);
VulkanFunction(vkCmdBindIndexBuffer);
VulkanFunction(vkCmdEndRenderPass);
VulkanFunction(vkQueuePresentKHR);
VulkanFunction(vkGetImageMemoryRequirements);

// TODO: Error handling
static void VulkanLoadBaseFunctions()
{
    HMODULE vulkanDLL = LoadLibraryA(WIN32_VULKAN_LOADER_FILENAME);
    Assert(vulkanDLL != INVALID_HANDLE_VALUE, "");

    vkCreateInstance = (PFN_vkCreateInstance)GetProcAddress(vulkanDLL, "vkCreateInstance");
    vkGetInstanceProcAddr = (PFN_vkGetInstanceProcAddr)GetProcAddress(vulkanDLL, "vkGetInstanceProcAddr");
}

static void VulkanLoadInstanceFunctions(VkInstance instance)
{
    LoadVulkanFunction(vkGetDeviceProcAddr);
    LoadVulkanFunction(vkCreateDevice);
    LoadVulkanFunction(vkEnumeratePhysicalDevices);
    LoadVulkanFunction(vkGetPhysicalDeviceFeatures);
    LoadVulkanFunction(vkGetPhysicalDeviceProperties);
    LoadVulkanFunction(vkGetPhysicalDeviceMemoryProperties);
    LoadVulkanFunction(vkGetPhysicalDeviceFormatProperties);
    LoadVulkanFunction(vkEnumerateDeviceExtensionProperties);
    LoadVulkanFunction(vkCreateWin32SurfaceKHR);
    LoadVulkanFunction(vkGetPhysicalDeviceSurfaceFormatsKHR);
    LoadVulkanFunction(vkGetPhysicalDeviceSurfacePresentModesKHR);
    LoadVulkanFunction(vkDestroySurfaceKHR);
    LoadVulkanFunction(vkDestroyInstance);
    LoadVulkanFunction(vkGetPhysicalDeviceQueueFamilyProperties);
    LoadVulkanFunction(vkGetPhysicalDeviceSurfaceCapabilitiesKHR);
}

static void VulkanLoadDeviceFunctions(VkDevice device)
{
    LoadVulkanDeviceFunction(vkGetDeviceQueue);
    LoadVulkanDeviceFunction(vkCreateCommandPool);
    LoadVulkanDeviceFunction(vkDestroyCommandPool);
    LoadVulkanDeviceFunction(vkDeviceWaitIdle);
    LoadVulkanDeviceFunction(vkDestroyDevice);

    LoadVulkanDeviceFunction(vkCreateRenderPass);
    LoadVulkanDeviceFunction(vkCreateSemaphore);
    LoadVulkanDeviceFunction(vkCreateFence);
    LoadVulkanDeviceFunction(vkAllocateCommandBuffers);
    LoadVulkanDeviceFunction(vkCreateImage);
    LoadVulkanDeviceFunction(vkAllocateMemory);
    LoadVulkanDeviceFunction(vkBindImageMemory);
    LoadVulkanDeviceFunction(vkCreateImageView);
    LoadVulkanDeviceFunction(vkCreateSampler);
    LoadVulkanDeviceFunction(vkCreateBuffer);
    LoadVulkanDeviceFunction(vkGetBufferMemoryRequirements);
    LoadVulkanDeviceFunction(vkAllocateMemory);
    LoadVulkanDeviceFunction(vkBindBufferMemory);
    LoadVulkanDeviceFunction(vkMapMemory);
    LoadVulkanDeviceFunction(vkUnmapMemory);
    LoadVulkanDeviceFunction(vkCmdPipelineBarrier);
    LoadVulkanDeviceFunction(vkCmdCopyBufferToImage);
    LoadVulkanDeviceFunction(vkCmdPipelineBarrier);
    LoadVulkanDeviceFunction(vkFreeMemory);
    LoadVulkanDeviceFunction(vkDestroyBuffer);
    LoadVulkanDeviceFunction(vkDestroySampler);
    LoadVulkanDeviceFunction(vkDestroyImage);
    LoadVulkanDeviceFunction(vkDestroyImageView);
    LoadVulkanDeviceFunction(vkCreateDescriptorPool);

    LoadVulkanDeviceFunction(vkCmdCopyBuffer);
    LoadVulkanDeviceFunction(vkCreateDescriptorSetLayout);
    LoadVulkanDeviceFunction(vkAllocateDescriptorSets);
    LoadVulkanDeviceFunction(vkUpdateDescriptorSets);
    LoadVulkanDeviceFunction(vkCreatePipelineLayout);
    LoadVulkanDeviceFunction(vkCreateGraphicsPipelines);
    LoadVulkanDeviceFunction(vkDestroyPipelineLayout);
    LoadVulkanDeviceFunction(vkDestroyPipeline);
    LoadVulkanDeviceFunction(vkCreateShaderModule);
    LoadVulkanDeviceFunction(vkDestroyShaderModule);
    LoadVulkanDeviceFunction(vkCmdCopyImageToBuffer);
    LoadVulkanDeviceFunction(vkCreateSwapchainKHR);
    LoadVulkanDeviceFunction(vkGetSwapchainImagesKHR);
    LoadVulkanDeviceFunction(vkCreateFramebuffer);
    LoadVulkanDeviceFunction(vkDestroyFramebuffer);
    LoadVulkanDeviceFunction(vkDestroyImageView);
    LoadVulkanDeviceFunction(vkDestroySwapchainKHR);

    LoadVulkanDeviceFunction(vkWaitForFences);
    LoadVulkanDeviceFunction(vkBeginCommandBuffer);
    LoadVulkanDeviceFunction(vkEndCommandBuffer);
    LoadVulkanDeviceFunction(vkResetFences);
    LoadVulkanDeviceFunction(vkQueueSubmit);
    LoadVulkanDeviceFunction(vkDestroyDescriptorSetLayout);
    LoadVulkanDeviceFunction(vkDestroyDescriptorSetLayout);
    LoadVulkanDeviceFunction(vkDestroyDescriptorPool);
    LoadVulkanDeviceFunction(vkDestroyRenderPass);
    LoadVulkanDeviceFunction(vkDestroySemaphore);
    LoadVulkanDeviceFunction(vkDestroySemaphore);
    LoadVulkanDeviceFunction(vkDestroyFence);
    LoadVulkanDeviceFunction(vkFreeCommandBuffers);
    LoadVulkanDeviceFunction(vkAcquireNextImageKHR);
    LoadVulkanDeviceFunction(vkCmdBeginRenderPass);
    LoadVulkanDeviceFunction(vkCmdSetViewport);
    LoadVulkanDeviceFunction(vkCmdSetScissor);
    LoadVulkanDeviceFunction(vkCmdBindDescriptorSets);
    LoadVulkanDeviceFunction(vkCmdPushConstants);
    LoadVulkanDeviceFunction(vkCmdDrawIndexed);
    LoadVulkanDeviceFunction(vkCmdNextSubpass);
    LoadVulkanDeviceFunction(vkCmdBindPipeline);
    LoadVulkanDeviceFunction(vkCmdBindVertexBuffers);
    LoadVulkanDeviceFunction(vkCmdBindIndexBuffer);
    LoadVulkanDeviceFunction(vkCmdEndRenderPass);
    LoadVulkanDeviceFunction(vkQueuePresentKHR);
    LoadVulkanDeviceFunction(vkGetImageMemoryRequirements);
}



