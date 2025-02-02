#include "Vulkan.h"
#include "VulkanBuffer.h"
#include "VulkanShader.h"
#include "VulkanPipeline.h"
#include "VulkanVertexBuffer.h"

#include "Math/MyMath.h"

// Double-buffer is sufficient
#define FIF 2

static constexpr inline u32 c_MaxQuadsPerBatch = 1 << 8;
static constexpr inline u32 c_MaxQuadVertices = c_MaxQuadsPerBatch * 4;
static constexpr inline u32 c_MaxQuadIndices = c_MaxQuadsPerBatch * 6;
static constexpr inline u32 c_MaxTexturesPerDrawCall = 32; // TODO: Get this from the driver

// Each face has to have a normal vector, so unfortunately we cannot encode cube as 8 vertices
static constexpr inline v4 c_CubeVertexPositions[24] =
{
    // Front face (+Z)
    { -0.5f, -0.5f,  0.5f, 1.0f },
    {  0.5f, -0.5f,  0.5f, 1.0f },
    {  0.5f,  0.5f,  0.5f, 1.0f },
    { -0.5f,  0.5f,  0.5f, 1.0f },

    // Back face (-Z)
    {  0.5f, -0.5f, -0.5f, 1.0f },
    { -0.5f, -0.5f, -0.5f, 1.0f },
    { -0.5f,  0.5f, -0.5f, 1.0f },
    {  0.5f,  0.5f, -0.5f, 1.0f },

    // Left face (-X)
    { -0.5f, -0.5f, -0.5f, 1.0f },
    { -0.5f, -0.5f,  0.5f, 1.0f },
    { -0.5f,  0.5f,  0.5f, 1.0f },
    { -0.5f,  0.5f, -0.5f, 1.0f },

    // Right face (+X)
    {  0.5f, -0.5f,  0.5f, 1.0f },
    {  0.5f, -0.5f, -0.5f, 1.0f },
    {  0.5f,  0.5f, -0.5f, 1.0f },
    {  0.5f,  0.5f,  0.5f, 1.0f },

    // Top face (+Y)
    { -0.5f,  0.5f,  0.5f, 1.0f },
    {  0.5f,  0.5f,  0.5f, 1.0f },
    {  0.5f,  0.5f, -0.5f, 1.0f },
    { -0.5f,  0.5f, -0.5f, 1.0f },

    // Bottom face (-Y)
    { -0.5f, -0.5f, -0.5f, 1.0f },
    {  0.5f, -0.5f, -0.5f, 1.0f },
    {  0.5f, -0.5f,  0.5f, 1.0f },
    { -0.5f, -0.5f,  0.5f, 1.0f }
};

struct push_constant_buffer
{
    m4 CameraViewProjection;
};

struct quad_vertex
{
    v3 Position;
    v4 Color;
};

struct queue_family_indices
{
    u32 Graphics = -1;
    u32 Compute = -1;

    bool IsComplete()
    {
        return Graphics != -1 /* && m_Compute != -1*/;
    }
};

struct vulkan_game_renderer
{
    // Vulkan context stuff
    VkSurfaceFormatKHR SurfaceFormat;
    VkPresentModeKHR SurfacePresentMode;
    VkSurfaceKHR Surface = nullptr;

    VkFormat DepthFormat = VK_FORMAT_UNDEFINED;

    VkDebugUtilsMessengerEXT DebugMessenger = nullptr;
    VkInstance Instance = nullptr;

    queue_family_indices QueueFamilyIndices;

    VkPhysicalDeviceMemoryProperties MemoryProperties;
    VkPhysicalDeviceProperties Properties;
    VkPhysicalDeviceFeatures Features;
    VkPhysicalDevice PhysicalDevice = nullptr;

    // Logical device
    VkQueue GraphicsQueue = nullptr;
    VkQueue ComputeQueue = nullptr;
    VkCommandPool CommandPool = nullptr;
    VkDevice Device = nullptr;
    // --------------------------------------------------

    VkSemaphore RenderFinishedSemaphores[FIF];
    VkSemaphore PresentSemaphores[FIF];
    VkFence InFlightFences[FIF];

    // Color
    VkImageView ImageViews[FIF];
    VkImage Images[FIF];

    // Depth
    bool DepthTesting = true;
    VkImage DepthImages[FIF];
    VkDeviceMemory DepthImageMemories[FIF];
    VkImageView DepthImagesViews[FIF];

    VkFramebuffer FrameBuffers[FIF];
    VkCommandBuffer RenderCommandBuffers[FIF];

    bool ResizeSwapChain = false;

    VkExtent2D SwapChainSize = {};
    VkSwapchainKHR SwapChain = nullptr;
    VkRenderPass RenderPass = nullptr;

    u32 CurrentFrame = 0;
    u32 ImageIndex = 0;

    // Batch renderer
    graphics_pipeline QuadPipeline;
    shader QuadShader;

    push_constant_buffer PushConstant;

    vulkan_buffer QuadIndexBuffer;
    vulkan_vertex_buffer QuadVertexBuffers[FIF];

    quad_vertex QuadVertexDataBase[c_MaxQuadVertices];
    quad_vertex* QuadVertexDataPtr = QuadVertexDataBase;
    u32 QuadIndexCount = 0;

    //V4 ClearColor = { 0.2f, 0.3f, 0.8f, 1.0f };
    //Texture2D DefaultWhiteTexture;
    //Texture2D TextureStack[c_MaxTexturesPerDrawCall] = {};
    //u32 TextureStackIndex = 0;
    //
    //VkDescriptorSet DescriptorSets[FIF];
    //VkDescriptorSetLayout DescriptorSetLayout;
    //VkDescriptorPool DescriptorPool;
};

static VkBool32 VKAPI_CALL VulkanDebugCallback(VkDebugUtilsMessageSeverityFlagBitsEXT messageSeverity, VkDebugUtilsMessageTypeFlagsEXT messageType, const VkDebugUtilsMessengerCallbackDataEXT* pCallbackData, void* pUserData)
{
    switch (messageSeverity)
    {
        //case VK_DEBUG_UTILS_MESSAGE_SEVERITY_VERBOSE_BIT_EXT: FAA_TRACE("Validation layer: %s", pCallbackData->pMessage); break;
        case VK_DEBUG_UTILS_MESSAGE_SEVERITY_INFO_BIT_EXT:    Info("Validation layer: %s", pCallbackData->pMessage); break;
        case VK_DEBUG_UTILS_MESSAGE_SEVERITY_WARNING_BIT_EXT: Warn("Validation layer: %s", pCallbackData->pMessage); break;
        case VK_DEBUG_UTILS_MESSAGE_SEVERITY_ERROR_BIT_EXT:   Err("Validation layer: %s", pCallbackData->pMessage); break;
    }

    return VK_FALSE;
}

// From imgui_impl_vulkan.cpp
static VkSurfaceFormatKHR VulkanSelectSurfaceFormat(VkPhysicalDevice physicalDevice, VkSurfaceKHR surface)
{
    VkColorSpaceKHR request_color_space = VK_COLOR_SPACE_SRGB_NONLINEAR_KHR;

    // From top to bottom (top means its gonna be picked first)
    VkFormat request_formats[] = {
        VK_FORMAT_B8G8R8A8_UNORM,
        VK_FORMAT_B8G8R8A8_SRGB,
        VK_FORMAT_R8G8B8A8_SRGB,
        VK_FORMAT_R8G8B8A8_UNORM
    };

    // Per Spec Format and View Format are expected to be the same unless VK_IMAGE_CREATE_MUTABLE_BIT was set at image creation
    // Assuming that the default behavior is without setting this bit, there is no need for separate Swapchain image and image view format
    // Additionally several new color spaces were introduced with Vulkan Spec v1.0.40,
    // hence we must make sure that a format with the mostly available color space, VK_COLOR_SPACE_SRGB_NONLINEAR_KHR, is found and used.
    u32 availCount;
    vkGetPhysicalDeviceSurfaceFormatsKHR(physicalDevice, surface, &availCount, nullptr);

    VkSurfaceFormatKHR availFormats[16];

    if (availCount > 16)
        availCount = 16;

    vkGetPhysicalDeviceSurfaceFormatsKHR(physicalDevice, surface, &availCount, availFormats);

    // First check if only one format, VK_FORMAT_UNDEFINED, is available, which would imply that any format is available
    if (availCount == 1)
    {
        if (availFormats[0].format == VK_FORMAT_UNDEFINED)
        {
            VkSurfaceFormatKHR ret;
            ret.format = request_formats[0];
            ret.colorSpace = request_color_space;
            return ret;
        }
        else
        {
            // No point in searching another format
            return availFormats[0];
        }
    }
    else
    {
        // Request several formats, the first found will be used
        for (u64 request_i = 0; request_i < CountOf(request_formats); ++request_i)
            for (u32 avail_i = 0; avail_i < availCount; avail_i++)
                if (availFormats[avail_i].format == request_formats[request_i] && availFormats[avail_i].colorSpace == request_color_space)
                    return availFormats[avail_i];

        // If none of the requested image formats could be found, use the first available
        return availFormats[0];
    }
}

// NOTE: This is only for optimal tiling
static VkFormat VulkanSelectDepthFormat(VkPhysicalDevice physicalDevice)
{
    // From top to bottom (top means its gonna be picked first)
    VkFormat request_formats[] = {
        VK_FORMAT_D32_SFLOAT,
        VK_FORMAT_D32_SFLOAT_S8_UINT,
        VK_FORMAT_D24_UNORM_S8_UINT,
    };

    const VkImageTiling tiling = VK_IMAGE_TILING_OPTIMAL;
    VkFormatFeatureFlags features = VK_FORMAT_FEATURE_DEPTH_STENCIL_ATTACHMENT_BIT;

    for (VkFormat format : request_formats)
    {
        VkFormatProperties properties;
        vkGetPhysicalDeviceFormatProperties(physicalDevice, format, &properties);

        if (tiling == VK_IMAGE_TILING_OPTIMAL && (properties.optimalTilingFeatures & features) == features)
        {
            return format;
        }
    }

    Assert(false, "No supported depth buffer!");

    return VK_FORMAT_UNDEFINED;
}

static VkPresentModeKHR VulkanChooseSwapchainPresentMode(VkPhysicalDevice physicalDevice, VkSurfaceKHR surface)
{
    VkPresentModeKHR availablePresentModes[16]; // No way there will be more present modes

    // Query for swap chain available present modes
    u32 presentModeCount;
    VkAssert(vkGetPhysicalDeviceSurfacePresentModesKHR(physicalDevice, surface, &presentModeCount, nullptr));
    Assert(presentModeCount <= 16, "presentModeCount <= 16");
    Assert(presentModeCount > 0, "presentModeCount > 0");

    if (presentModeCount > 16)
        presentModeCount = 16;

    VkAssert(vkGetPhysicalDeviceSurfacePresentModesKHR(physicalDevice, surface, &presentModeCount, availablePresentModes));

    for (u32 i = 0; i < presentModeCount; ++i)
    {
        if (availablePresentModes[i] == VK_PRESENT_MODE_MAILBOX_KHR)
        {
            return availablePresentModes[i];
        }
    }

    return VK_PRESENT_MODE_FIFO_KHR;
}


static void InitializeVulkan(vulkan_game_renderer* Renderer, game_window Window)
{
    // Load base functions from vulkan-1.dll
    VulkanLoadBaseFunctions();

    const char* ValidationLayers[] = {
        "VK_LAYER_KHRONOS_validation"
    };

    const char* DeviceExtensions[] = {
        VK_KHR_SWAPCHAIN_EXTENSION_NAME
    };

    const char* InstanceExtenions[] = {
        "VK_KHR_surface",
    #if ENABLE_VALIDATION_LAYERS
        VK_EXT_DEBUG_REPORT_EXTENSION_NAME,
        VK_EXT_DEBUG_UTILS_EXTENSION_NAME,
    #endif
        VK_KHR_WIN32_SURFACE_EXTENSION_NAME
    };

    // Create instance
    {
        // App info
        VkApplicationInfo appInfo = {};
        appInfo.sType = VK_STRUCTURE_TYPE_APPLICATION_INFO;
        appInfo.pApplicationName = "RunGun";
        appInfo.applicationVersion = VK_MAKE_VERSION(1, 0, 0);
        appInfo.pEngineName = "RunGun";
        appInfo.engineVersion = VK_MAKE_VERSION(1, 0, 0);

        // We use functions only up to 1.0, except for vkEnumerateInstanceVersion, if it exists, good, if not skip
        // However, some features like negative viewport height are a core feature in vulkan 1.1,
        // Another "however" is that vulkan is backward compatible, so we could just keep 1.3
        // TODO: This needs futher investigation, keeping this at 1.1
        appInfo.apiVersion = VK_API_VERSION_1_1;

        // Vulkan Instance
        VkInstanceCreateInfo createInfo = {};
        createInfo.sType = VK_STRUCTURE_TYPE_INSTANCE_CREATE_INFO;
        createInfo.pApplicationInfo = &appInfo;
        createInfo.enabledLayerCount = 0;
        createInfo.pNext = nullptr;

        VkValidationFeatureEnableEXT enables[] = { VK_VALIDATION_FEATURE_ENABLE_BEST_PRACTICES_EXT };
        VkValidationFeaturesEXT features = {};
        features.sType = VK_STRUCTURE_TYPE_VALIDATION_FEATURES_EXT;
        features.enabledValidationFeatureCount = 1;
        features.pEnabledValidationFeatures = enables;

        VkDebugUtilsMessengerCreateInfoEXT debugCreateInfo = {};

        // Validation layers
        if (ENABLE_VALIDATION_LAYERS)
        {
            // Validation layers
            createInfo.enabledLayerCount = CountOf(ValidationLayers);
            createInfo.ppEnabledLayerNames = ValidationLayers;

            // For VkCreateInstance and VkDestroyInstance debug
            debugCreateInfo.sType = VK_STRUCTURE_TYPE_DEBUG_UTILS_MESSENGER_CREATE_INFO_EXT;
            debugCreateInfo.messageSeverity = VK_DEBUG_UTILS_MESSAGE_SEVERITY_VERBOSE_BIT_EXT | VK_DEBUG_UTILS_MESSAGE_SEVERITY_WARNING_BIT_EXT | VK_DEBUG_UTILS_MESSAGE_SEVERITY_ERROR_BIT_EXT;
            debugCreateInfo.messageType = VK_DEBUG_UTILS_MESSAGE_TYPE_GENERAL_BIT_EXT | VK_DEBUG_UTILS_MESSAGE_TYPE_VALIDATION_BIT_EXT | VK_DEBUG_UTILS_MESSAGE_TYPE_PERFORMANCE_BIT_EXT;
            debugCreateInfo.pfnUserCallback = VulkanDebugCallback;
            debugCreateInfo.pNext = nullptr; // NOTE: These might be useful -> &features;

            createInfo.pNext = (VkDebugUtilsMessengerCreateInfoEXT*)&debugCreateInfo;
        }

        createInfo.enabledExtensionCount = CountOf(InstanceExtenions);
        createInfo.ppEnabledExtensionNames = InstanceExtenions;
        VkAssert(vkCreateInstance(&createInfo, nullptr, &Renderer->Instance));
    }

    // Load instance functions
    VulkanLoadInstanceFunctions(Renderer->Instance);

    // Create debugger
    if (ENABLE_VALIDATION_LAYERS)
    {
        VkDebugUtilsMessengerCreateInfoEXT createInfo = {};
        createInfo.sType = VK_STRUCTURE_TYPE_DEBUG_UTILS_MESSENGER_CREATE_INFO_EXT;
        createInfo.messageSeverity = VK_DEBUG_UTILS_MESSAGE_SEVERITY_VERBOSE_BIT_EXT | VK_DEBUG_UTILS_MESSAGE_SEVERITY_WARNING_BIT_EXT | VK_DEBUG_UTILS_MESSAGE_SEVERITY_ERROR_BIT_EXT;
        createInfo.messageType = VK_DEBUG_UTILS_MESSAGE_TYPE_GENERAL_BIT_EXT | VK_DEBUG_UTILS_MESSAGE_TYPE_VALIDATION_BIT_EXT | VK_DEBUG_UTILS_MESSAGE_TYPE_PERFORMANCE_BIT_EXT;
        createInfo.pfnUserCallback = VulkanDebugCallback;
        createInfo.pUserData = nullptr; // Optional

        // Debug messenger is an extension, meaning that it must be loaded manually
        auto createDebugUtilsMessenger = (PFN_vkCreateDebugUtilsMessengerEXT)vkGetInstanceProcAddr(Renderer->Instance, "vkCreateDebugUtilsMessengerEXT");
        Assert(createDebugUtilsMessenger, "Failed to load VkCreateDebugUtilsMessengerEXT");
        VkAssert(createDebugUtilsMessenger(Renderer->Instance, &createInfo, nullptr, &Renderer->DebugMessenger));
    }

    // Select physical device
    {
        auto isDeviceSuitable = [&](VkPhysicalDevice device, queue_family_indices* indices)
        {
            u32 queueFamilyCount = 0;
            vkGetPhysicalDeviceQueueFamilyProperties(device, &queueFamilyCount, nullptr);

            VkQueueFamilyProperties queueFamilies[30];
            Assert(queueFamilyCount <= 30, "queueFamilyCount <= 30");
            vkGetPhysicalDeviceQueueFamilyProperties(device, &queueFamilyCount, queueFamilies);

            for (u32 i = 0; i < queueFamilyCount; i++)
            {
                if (queueFamilies[i].queueFlags & VK_QUEUE_GRAPHICS_BIT)
                {
                    indices->Graphics = i;
                }

                if (indices->IsComplete())
                    break;

                i++;
            }

            return indices->IsComplete();
        };

        u32 deviceCount = 0;
        VkAssert(vkEnumeratePhysicalDevices(Renderer->Instance, &deviceCount, nullptr));
        Assert(deviceCount, "Failed to find GPUs with Vulkan support!");

        if (deviceCount > 5)
        {
            deviceCount = 5;
            Trace("[WARN] User has more than 5 GPUs!");
        }

        VkPhysicalDevice devices[5];
        VkAssert(vkEnumeratePhysicalDevices(Renderer->Instance, &deviceCount, devices));

        // Select device
        for (u32 i = 0; i < deviceCount; i++)
        {
            if (isDeviceSuitable(devices[i], &Renderer->QueueFamilyIndices))
            {
                Renderer->PhysicalDevice = devices[i];
                break;
            }
        }

        Assert(Renderer->PhysicalDevice, "Failed to find suitable GPU!");

        // Query device features
        vkGetPhysicalDeviceFeatures(Renderer->PhysicalDevice, &Renderer->Features);
        vkGetPhysicalDeviceProperties(Renderer->PhysicalDevice, &Renderer->Properties);
        vkGetPhysicalDeviceMemoryProperties(Renderer->PhysicalDevice, &Renderer->MemoryProperties);

        Assert(Renderer->Properties.limits.maxVertexInputAttributes >= 10, "Driver does not support more than 16 attributes");

        // Depth format
        // NOTE: This is only for optimal tiling
        auto selectDepthFormat = [&](VkPhysicalDevice physicalDevice)
        {
            // From top to bottom (top means its gonna be picked first)
            VkFormat request_formats[] = {
                VK_FORMAT_D32_SFLOAT,
                VK_FORMAT_D32_SFLOAT_S8_UINT,
                VK_FORMAT_D24_UNORM_S8_UINT,
            };

            const VkImageTiling tiling = VK_IMAGE_TILING_OPTIMAL;
            VkFormatFeatureFlags features = VK_FORMAT_FEATURE_DEPTH_STENCIL_ATTACHMENT_BIT;

            for (VkFormat format : request_formats)
            {
                VkFormatProperties properties;
                vkGetPhysicalDeviceFormatProperties(physicalDevice, format, &properties);

                if (tiling == VK_IMAGE_TILING_OPTIMAL && (properties.optimalTilingFeatures & features) == features)
                {
                    return format;
                }
            }

            Assert(false, "No supported depth buffer!");

            return VK_FORMAT_UNDEFINED;
        };

        Renderer->DepthFormat = selectDepthFormat(Renderer->PhysicalDevice);
    }

    // Create device
    {
        // Manage queues
        VkDeviceQueueCreateInfo queueCreateInfos[3] = {};

        u32 queueCreateInfoCount = 0;
        f32 queuePriority = 1.0f;

        // Only need graphics queue
        VkDeviceQueueCreateInfo& queueCreateInfo = queueCreateInfos[0];
        queueCreateInfo.sType = VK_STRUCTURE_TYPE_DEVICE_QUEUE_CREATE_INFO;
        queueCreateInfo.queueFamilyIndex = Renderer->QueueFamilyIndices.Graphics;
        queueCreateInfo.queueCount = 1;
        queueCreateInfo.pQueuePriorities = &queuePriority;
        queueCreateInfoCount = 1;

        VkPhysicalDeviceFeatures deviceFeatures = {};
        deviceFeatures.robustBufferAccess = false;
        //deviceFeatures.wideLines = VK_TRUE;
        //deviceFeatures.fillModeNonSolid = VK_TRUE;
        deviceFeatures.independentBlend = false;
        deviceFeatures.samplerAnisotropy = false;

        // Create device
        VkDeviceCreateInfo createInfo = {};
        createInfo.sType = VK_STRUCTURE_TYPE_DEVICE_CREATE_INFO;
        createInfo.queueCreateInfoCount = queueCreateInfoCount;
        createInfo.pQueueCreateInfos = queueCreateInfos;
        createInfo.pEnabledFeatures = &deviceFeatures;
        createInfo.enabledLayerCount = 0;

        createInfo.enabledExtensionCount = CountOf(DeviceExtensions);
        createInfo.ppEnabledExtensionNames = DeviceExtensions;

        if (ENABLE_VALIDATION_LAYERS)
        {
            createInfo.enabledLayerCount = CountOf(ValidationLayers);
            createInfo.ppEnabledLayerNames = ValidationLayers;
        }

        VkAssert(vkCreateDevice(Renderer->PhysicalDevice, &createInfo, nullptr, &Renderer->Device));
    }

    // Load device functions
    // NOTE: Loading using device is supposedly faster so we do that
    VulkanLoadDeviceFunctions(Renderer->Device);

    // Get graphics queue
    vkGetDeviceQueue(Renderer->Device, Renderer->QueueFamilyIndices.Graphics, 0, &Renderer->GraphicsQueue);

    // Create command pool
    {
        VkCommandPoolCreateInfo poolInfo = {};
        poolInfo.sType = VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO;
        poolInfo.flags = VK_COMMAND_POOL_CREATE_RESET_COMMAND_BUFFER_BIT;
        poolInfo.queueFamilyIndex = Renderer->QueueFamilyIndices.Graphics;
        VkAssert(vkCreateCommandPool(Renderer->Device, &poolInfo, nullptr, &Renderer->CommandPool));
    }

    // Create surface
    {
        VkWin32SurfaceCreateInfoKHR createInfo = {};
        createInfo.sType = VK_STRUCTURE_TYPE_WIN32_SURFACE_CREATE_INFO_KHR;
        createInfo.hwnd = Window.WindowHandle;
        createInfo.hinstance = Window.ModuleInstance;
        VkAssert(vkCreateWin32SurfaceKHR(Renderer->Instance, &createInfo, nullptr, &Renderer->Surface));

        Renderer->SurfaceFormat = VulkanSelectSurfaceFormat(Renderer->PhysicalDevice, Renderer->Surface);
        Renderer->SurfacePresentMode = VulkanChooseSwapchainPresentMode(Renderer->PhysicalDevice, Renderer->Surface);
    }
}

static void InitializeRendering(vulkan_game_renderer* Renderer)
{
    // Main renderpass
    {
        // Attachments
        VkAttachmentDescription colorAttachment = {};
        colorAttachment.format = Renderer->SurfaceFormat.format;
        colorAttachment.samples = VK_SAMPLE_COUNT_1_BIT;
        colorAttachment.loadOp = VK_ATTACHMENT_LOAD_OP_CLEAR;
        colorAttachment.storeOp = VK_ATTACHMENT_STORE_OP_STORE;
        colorAttachment.stencilLoadOp = VK_ATTACHMENT_LOAD_OP_DONT_CARE;
        colorAttachment.stencilStoreOp = VK_ATTACHMENT_STORE_OP_DONT_CARE;
        colorAttachment.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
        colorAttachment.finalLayout = VK_IMAGE_LAYOUT_PRESENT_SRC_KHR;

        VkAttachmentDescription depthAttachment = {};
        depthAttachment.format = Renderer->DepthFormat;
        depthAttachment.samples = VK_SAMPLE_COUNT_1_BIT;
        depthAttachment.loadOp = VK_ATTACHMENT_LOAD_OP_CLEAR;
        depthAttachment.storeOp = VK_ATTACHMENT_STORE_OP_STORE;
        depthAttachment.stencilLoadOp = VK_ATTACHMENT_LOAD_OP_DONT_CARE;
        depthAttachment.stencilStoreOp = VK_ATTACHMENT_STORE_OP_DONT_CARE;
        depthAttachment.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
        depthAttachment.finalLayout = VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL;

        // Attachment references
        VkAttachmentReference colorAttachmentRef = {};
        colorAttachmentRef.attachment = 0;
        colorAttachmentRef.layout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;

        VkAttachmentReference depthAttachmentRef = {};
        depthAttachmentRef.attachment = 1;
        depthAttachmentRef.layout = VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL;

        VkAttachmentReference colorAttachmentsRefs[] = { colorAttachmentRef };

        // Subpasses
        VkSubpassDescription defaultSubpass = {};
        defaultSubpass.pipelineBindPoint = VK_PIPELINE_BIND_POINT_GRAPHICS;
        defaultSubpass.colorAttachmentCount = CountOf(colorAttachmentsRefs);
        defaultSubpass.pColorAttachments = colorAttachmentsRefs;
        defaultSubpass.pDepthStencilAttachment = Renderer->DepthTesting ? &depthAttachmentRef : nullptr;

        VkSubpassDescription semiTransparentPass = {};
        semiTransparentPass.pipelineBindPoint = VK_PIPELINE_BIND_POINT_GRAPHICS;
        semiTransparentPass.colorAttachmentCount = CountOf(colorAttachmentsRefs);
        semiTransparentPass.pColorAttachments = colorAttachmentsRefs;
        semiTransparentPass.pDepthStencilAttachment = Renderer->DepthTesting ? &depthAttachmentRef : nullptr;

        // Quad to semitransparent dependency
        VkSubpassDependency semiTransparentSubpassDependency = {};
        {
            semiTransparentSubpassDependency.srcSubpass = 0; // Solid pass
            semiTransparentSubpassDependency.dstSubpass = 1; // Semi-transparent pass

            // Synchronization stages
            semiTransparentSubpassDependency.srcStageMask = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT;
            semiTransparentSubpassDependency.dstStageMask = VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT;

            // Access masks for proper synchronization
            semiTransparentSubpassDependency.srcAccessMask = VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT;
            semiTransparentSubpassDependency.dstAccessMask = VK_ACCESS_SHADER_READ_BIT;
            semiTransparentSubpassDependency.dependencyFlags = 0;
        }

        // Self dependencies
        VkSubpassDependency selfDependency = {};
        {
            selfDependency.srcSubpass = VK_SUBPASS_EXTERNAL;
            selfDependency.dstSubpass = 0;
            selfDependency.srcStageMask = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT;
            selfDependency.dstStageMask = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT;

            if (Renderer->DepthTesting)
            {
                selfDependency.srcStageMask |= VK_PIPELINE_STAGE_EARLY_FRAGMENT_TESTS_BIT;
                selfDependency.dstStageMask |= VK_PIPELINE_STAGE_EARLY_FRAGMENT_TESTS_BIT;
                selfDependency.dstAccessMask |= VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT;
            }

            selfDependency.srcAccessMask = 0;
            selfDependency.dstAccessMask = VK_ACCESS_COLOR_ATTACHMENT_READ_BIT | VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT;
            selfDependency.dependencyFlags = 0;
        }

        // Render pass
        VkAttachmentDescription attachments[] = { colorAttachment, depthAttachment };
        VkSubpassDescription subpassses[] = {
            defaultSubpass,
            //semiTransparentPass
        };
        VkSubpassDependency dependencies[] = {
            selfDependency,
            //semiTransparentSubpassDependency
        };

        VkRenderPassCreateInfo renderPassInfo = {};
        renderPassInfo.sType = VK_STRUCTURE_TYPE_RENDER_PASS_CREATE_INFO;
        renderPassInfo.attachmentCount = Renderer->DepthTesting ? CountOf(attachments) : (CountOf(attachments) - 1);
        renderPassInfo.pAttachments = attachments;
        renderPassInfo.subpassCount = CountOf(subpassses);
        renderPassInfo.pSubpasses = subpassses;
        renderPassInfo.dependencyCount = CountOf(dependencies);
        renderPassInfo.pDependencies = dependencies;

        VkAssert(vkCreateRenderPass(Renderer->Device, &renderPassInfo, nullptr, &Renderer->RenderPass));
    }

    // Sync objects
    {
        // Present and RenderFinished
        VkSemaphoreCreateInfo semaphoreInfo = {};
        semaphoreInfo.sType = VK_STRUCTURE_TYPE_SEMAPHORE_CREATE_INFO;
        semaphoreInfo.flags = 0;

        // Fences
        VkFenceCreateInfo fenceInfo = {};
        fenceInfo.sType = VK_STRUCTURE_TYPE_FENCE_CREATE_INFO;
        fenceInfo.flags = VK_FENCE_CREATE_SIGNALED_BIT;

        for (u32 i = 0; i < FIF; ++i)
        {
            VkAssert(vkCreateSemaphore(Renderer->Device, &semaphoreInfo, nullptr, &Renderer->PresentSemaphores[i]));
            VkAssert(vkCreateSemaphore(Renderer->Device, &semaphoreInfo, nullptr, &Renderer->RenderFinishedSemaphores[i]));
            VkAssert(vkCreateFence(Renderer->Device, &fenceInfo, nullptr, &Renderer->InFlightFences[i]));
        }
    }

    // Command buffers
    {
        VkCommandBufferAllocateInfo allocInfo = {};
        allocInfo.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO;
        allocInfo.commandPool = Renderer->CommandPool;
        allocInfo.level = VK_COMMAND_BUFFER_LEVEL_PRIMARY;
        allocInfo.commandBufferCount = FIF;

        VkAssert(vkAllocateCommandBuffers(Renderer->Device,
            &allocInfo, Renderer->RenderCommandBuffers));
    }
}

buffer ReadBinary(const char* path)
{
    buffer Result;

    HANDLE FileHandle = ::CreateFileA(path, GENERIC_READ, FILE_SHARE_READ, nullptr, OPEN_EXISTING, 0, nullptr);
    if (FileHandle != INVALID_HANDLE_VALUE)
    {
        DWORD dwBytesRead = 0;
        DWORD dwBytesWritten = 0;
        LARGE_INTEGER size;
        if (::GetFileSizeEx(FileHandle, &size))
        {
            // NOTE: We allocate more but that does not matter for now
            u8* data = static_cast<u8*>(::VirtualAlloc(nullptr, size.QuadPart, MEM_COMMIT, PAGE_READWRITE));

            if (::ReadFile(FileHandle, data, static_cast<DWORD>(size.QuadPart), &dwBytesRead, nullptr))
            {
                Result.Data = data;
                Result.Size = static_cast<u32>(size.QuadPart);
            }
            else
            {
                ::VirtualFree(data, 0, MEM_RELEASE);
            }
        }

        ::CloseHandle(FileHandle);
    }

    return Result;
}

static void InitializeRenderingResources(vulkan_game_renderer* Renderer)
{
    // Load shader
    {
        buffer Vertex = {};
        buffer Fragment = {};

        buffer Resources = ReadBinary("PackedResources.blp");
        u8* At = static_cast<u8*>(Resources.Data);

        // BLPF magic
        At += 4;

        // Load Shader Code
        {
            u32 AssetSize = *(u32*)(At);
            At += 4;

            Vertex = { At, AssetSize };
            At += AssetSize;

            AssetSize = *(u32*)(At);
            At += 4;

            Fragment = { At, AssetSize };
            At += AssetSize;
        }

        ShaderCreate(&Renderer->QuadShader, Renderer->Device, Vertex, Fragment);
    }

    // Quad
    {
        // Index buffer
        {
            u32 QuadIndices[c_MaxQuadIndices];
            u32 Offset = 0;
            for (u32 i = 0; i < c_MaxQuadIndices; i += 6)
            {
                QuadIndices[i + 0] = Offset + 0;
                QuadIndices[i + 1] = Offset + 1;
                QuadIndices[i + 2] = Offset + 2;

                QuadIndices[i + 3] = Offset + 2;
                QuadIndices[i + 4] = Offset + 3;
                QuadIndices[i + 5] = Offset + 0;

                Offset += 4;
            }

            // Create staging buffer
            vulkan_buffer StagingBuffer = VulkanBufferCreate(Renderer->Device, Renderer->PhysicalDevice, buffer_usage::TransferSource,
                memory_property::HostVisible | memory_property::Coherent, sizeof(QuadIndices), true);

            Renderer->QuadIndexBuffer = VulkanBufferCreate(Renderer->Device, Renderer->PhysicalDevice, buffer_usage::TransferDestination | buffer_usage::IndexBuffer,
                memory_property::Local | memory_property::Coherent, sizeof(QuadIndices), false);

            // Copy data to the staging buffer
            memcpy(StagingBuffer.MappedData, QuadIndices, sizeof(QuadIndices));

            // Submit immidiately
            {
                VkCommandBuffer CommandBuffer = nullptr;

                VkCommandBufferAllocateInfo cmdBufAllocateInfo = {};
                cmdBufAllocateInfo.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO;
                cmdBufAllocateInfo.commandPool = Renderer->CommandPool;
                cmdBufAllocateInfo.level = VK_COMMAND_BUFFER_LEVEL_PRIMARY;
                cmdBufAllocateInfo.commandBufferCount = 1;

                VkAssert(vkAllocateCommandBuffers(Renderer->Device, &cmdBufAllocateInfo, &CommandBuffer));

                VkCommandBufferBeginInfo commandBufferBeginInfo = {};
                commandBufferBeginInfo.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO;
                VkAssert(vkBeginCommandBuffer(CommandBuffer, &commandBufferBeginInfo));
                VulkanBufferSubmitFullCopy(CommandBuffer, StagingBuffer, Renderer->QuadIndexBuffer);
                VkAssert(vkEndCommandBuffer(CommandBuffer));

                VkSubmitInfo submitInfo = {};
                submitInfo.sType = VK_STRUCTURE_TYPE_SUBMIT_INFO;
                submitInfo.commandBufferCount = 1;
                submitInfo.pCommandBuffers = &CommandBuffer;

                // Create fence to ensure that the command buffer has finished executing
                VkFenceCreateInfo fenceCreateInfo = {};
                fenceCreateInfo.sType = VK_STRUCTURE_TYPE_FENCE_CREATE_INFO;
                fenceCreateInfo.flags = 0;
                VkFence fence;
                VkAssert(vkCreateFence(Renderer->Device, &fenceCreateInfo, nullptr, &fence));

                // Submit to the queue
                VkAssert(vkQueueSubmit(Renderer->GraphicsQueue, 1, &submitInfo, fence));

                // Wait for the fence to signal that command buffer has finished executing
                VkAssert(vkWaitForFences(Renderer->Device, 1, &fence, VK_TRUE, UINT64_MAX));

                vkDestroyFence(Renderer->Device, fence, nullptr);
                vkFreeCommandBuffers(Renderer->Device, Renderer->CommandPool, 1, &CommandBuffer);
            }

            VulkanBufferDestroy(Renderer->Device, StagingBuffer);
        }

        // Quad
        {
            // NOTE: For anything that is going to be read by the CPU
            //VK_MEMORY_PROPERTY_HOST_CACHED_BIT

            // Pipeline
            {
                Renderer->QuadPipeline = CreateGraphicsPipeline(Renderer->Device,
                    Renderer->QuadShader,
                    Renderer->RenderPass,
                    vertex_buffer_layout
                    {
                        { AttributeType::Vec3, "a_Position" },
                        { AttributeType::Vec4, "a_Color" },
                    },
                    vertex_buffer_layout{},
                    sizeof(push_constant_buffer),
                    nullptr,
                    0);
            }

            // Vertex buffers
            for (vulkan_vertex_buffer& QuadVertexBuffer : Renderer->QuadVertexBuffers)
            {
                QuadVertexBuffer = VulkanVertexBufferCreate(Renderer->Device, Renderer->PhysicalDevice, c_MaxQuadVertices * sizeof(quad_vertex));
            }
        }
    }

#if 0
    // White texture for texture-less quads
    {
        u32 white_color = 0xffffffff;
        r.DefaultWhiteTexture = CreateTexture2D(1, 1, &white_color, sizeof(u32), VK_FILTER_NEAREST);

        // Set first texture
        r.TextureStack[0] = r.DefaultWhiteTexture;
    }

    // GPU resources
    {
        VkDescriptorPoolSize poolSizes[] =
        {
            { VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, c_MaxTexturesPerDrawCall * FIF }, // resource count * frames in flight
        };

        VkDescriptorPoolCreateInfo poolCreateInfo = {};
        poolCreateInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO;
        poolCreateInfo.poolSizeCount = CountOf(poolSizes);
        poolCreateInfo.pPoolSizes = poolSizes;
        poolCreateInfo.maxSets = FIF;

        VkAssert(vkCreateDescriptorPool(Renderer->Device, &poolCreateInfo, nullptr, &Renderer->DescriptorPool));

        VkDescriptorSetLayoutBinding descriptorSetLayoutBindings[1];

        // sampler2D in fragment shader
        {
            auto& descriptorBinding = descriptorSetLayoutBindings[0] = {};
            descriptorBinding.binding = 0;
            descriptorBinding.descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
            descriptorBinding.stageFlags = VK_SHADER_STAGE_FRAGMENT_BIT;
            descriptorBinding.descriptorCount = c_MaxTexturesPerDrawCall;
            descriptorBinding.pImmutableSamplers = nullptr; // Optional
        }

        VkDescriptorSetLayoutCreateInfo layoutInfo = {};
        layoutInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO;
        layoutInfo.bindingCount = CountOf(descriptorSetLayoutBindings);
        layoutInfo.pBindings = descriptorSetLayoutBindings;

        VkAssert(vkCreateDescriptorSetLayout(Renderer->Device, &layoutInfo, nullptr, &Renderer->DescriptorSetLayout));

        // 3 is max probably
        VkDescriptorSetLayout layouts[3];
        for (u32 i = 0; i < FIF; i++)
        {
            layouts[i] = Renderer->DescriptorSetLayout;
        }

        VkDescriptorSetAllocateInfo allocInfo = {};
        allocInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO;
        allocInfo.descriptorPool = Renderer->DescriptorPool;
        allocInfo.descriptorSetCount = FIF * 1; // 'FIF' sets per 1 resource type
        allocInfo.pSetLayouts = layouts;

        VkAssert(vkAllocateDescriptorSets(Renderer->Device, &allocInfo, Renderer->DescriptorSets));
        // Update all texture slots to the white texture so Vulkan does not complain
        for (u32 i = 0; i < FIF; i++)
        {
            VkDescriptorImageInfo imageInfos[c_MaxTexturesPerDrawCall];

            for (u32 i = 0; i < c_MaxTexturesPerDrawCall; i++)
            {
                imageInfos[i].imageLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
                imageInfos[i].imageView = Renderer->DefaultWhiteTexture.imageViewHandle;
                imageInfos[i].sampler = Renderer->DefaultWhiteTexture.samplerHandle;
            }

            VkWriteDescriptorSet descriptorWrite = {};
            descriptorWrite.sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
            descriptorWrite.pNext = nullptr;
            descriptorWrite.dstSet = Renderer->DescriptorSets[i];
            descriptorWrite.dstBinding = 0;
            descriptorWrite.dstArrayElement = 0;
            descriptorWrite.descriptorCount = c_MaxTexturesPerDrawCall;
            descriptorWrite.descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
            descriptorWrite.pImageInfo = imageInfos;
            descriptorWrite.pBufferInfo = nullptr;
            descriptorWrite.pTexelBufferView = nullptr;
            vkUpdateDescriptorSets(Renderer->Device, 1, &descriptorWrite, 0, nullptr);
        }
    }
#endif
}

static void RecreateSwapChain(vulkan_game_renderer* Renderer, u32 RequestWidth, u32 RequestHeight)
{
    Assert(RequestWidth != 0 && RequestHeight != 0, "Width or height are 0s!");

    // Wait for GPU to finish
    vkDeviceWaitIdle(Renderer->Device);

    // Query for swap chain capabilities

    VkSurfaceCapabilitiesKHR SurfaceCapabilities;
    VkAssert(vkGetPhysicalDeviceSurfaceCapabilitiesKHR(Renderer->PhysicalDevice, Renderer->Surface, &SurfaceCapabilities));

    VkExtent2D SwapchainExtent = {};

    // If width (and height) equals the special value 0xFFFFFFFF, the size of the surface will be set by the swapchain
    // Using Win + D crashes this application, because for some reason, this gets called at EndFrame with 0,0 as extent
    if (SurfaceCapabilities.currentExtent.width == 0xFFFFFFFF || SurfaceCapabilities.currentExtent.width == 0 || SurfaceCapabilities.currentExtent.height == 0)
    {
        // If the surface size is undefined, the size is set to
        // the size of the images requested.
        SwapchainExtent.width = RequestWidth;
        SwapchainExtent.height = RequestHeight;
    }
    else
    {
        // If the surface size is defined, the swap chain size must match
        SwapchainExtent = SurfaceCapabilities.currentExtent;
        RequestWidth = SurfaceCapabilities.currentExtent.width;
        RequestHeight = SurfaceCapabilities.currentExtent.height;
    }

    // Store it for dynamic viewport and scissors
    Renderer->SwapChainSize = { RequestWidth, RequestHeight };

    // Check support for features
    VkSurfaceTransformFlagBitsKHR surfaceTransform = SurfaceCapabilities.currentTransform;
    if (SurfaceCapabilities.supportedTransforms & VK_SURFACE_TRANSFORM_IDENTITY_BIT_KHR)
    {
        surfaceTransform = VK_SURFACE_TRANSFORM_IDENTITY_BIT_KHR;
    }

    // Find a supported composite alpha format (not all devices support alpha opaque)
    VkCompositeAlphaFlagBitsKHR compositeAlpha = VK_COMPOSITE_ALPHA_OPAQUE_BIT_KHR;

    // Simply select the first composite alpha format available
    VkCompositeAlphaFlagBitsKHR compositeAlphaFlags[] = {
        VK_COMPOSITE_ALPHA_OPAQUE_BIT_KHR,
        VK_COMPOSITE_ALPHA_PRE_MULTIPLIED_BIT_KHR,
        VK_COMPOSITE_ALPHA_POST_MULTIPLIED_BIT_KHR,
        VK_COMPOSITE_ALPHA_INHERIT_BIT_KHR,
    };
    for (auto compositeAlphaFlag : compositeAlphaFlags)
    {
        if (SurfaceCapabilities.supportedCompositeAlpha & compositeAlphaFlag)
        {
            compositeAlpha = compositeAlphaFlag;
            break;
        }
    }
    // TODO: For maximum device support this needs to be updated

    // Create swapchain
    {
        VkSwapchainCreateInfoKHR createInfo = {};
        createInfo.sType = VK_STRUCTURE_TYPE_SWAPCHAIN_CREATE_INFO_KHR;
        createInfo.surface = Renderer->Surface;
        createInfo.minImageCount = FIF;
        createInfo.imageFormat = Renderer->SurfaceFormat.format;
        createInfo.imageColorSpace = Renderer->SurfaceFormat.colorSpace;
        createInfo.imageExtent = SwapchainExtent;
        createInfo.imageArrayLayers = 1;
        createInfo.imageUsage = VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT;
        createInfo.preTransform = surfaceTransform;
        createInfo.compositeAlpha = compositeAlpha;
        //createInfo.presentMode = VK_PRESENT_MODE_MAILBOX_KHR ; // V-Sync
        createInfo.presentMode = VK_PRESENT_MODE_FIFO_KHR; // V-Sync
        // Setting clipped to true allows the implementation to discard rendering outside of the surface area
        createInfo.clipped = true;
        createInfo.oldSwapchain = Renderer->SwapChain; // Using old swapchain
        createInfo.imageSharingMode = VK_SHARING_MODE_EXCLUSIVE; // TODO: Investigate

        // Create new swapchain
        VkSwapchainKHR newSwapchain = nullptr;
        VkAssert(vkCreateSwapchainKHR(Renderer->Device, &createInfo, nullptr, &newSwapchain));

        // Get swapchain images
        u32 imageCount;
        VkAssert(vkGetSwapchainImagesKHR(Renderer->Device, newSwapchain, &imageCount, nullptr));
        Assert(imageCount <= FIF, "imageCount <= FIF");
        VkAssert(vkGetSwapchainImagesKHR(Renderer->Device, newSwapchain, &imageCount, Renderer->Images));

        // Destroy old stuff if exists
        if (Renderer->SwapChain)
        {
            // Color
            for (auto framebuffer : Renderer->FrameBuffers)
                vkDestroyFramebuffer(Renderer->Device, framebuffer, nullptr);
            for (auto imageView : Renderer->ImageViews)
                vkDestroyImageView(Renderer->Device, imageView, nullptr);

            // Depth
            if (Renderer->DepthTesting)
            {
                for (auto image : Renderer->DepthImages)
                    vkDestroyImage(Renderer->Device, image, nullptr);
                for (auto imageMemory : Renderer->DepthImageMemories)
                    vkFreeMemory(Renderer->Device, imageMemory, nullptr);
                for (auto imageView : Renderer->DepthImagesViews)
                    vkDestroyImageView(Renderer->Device, imageView, nullptr);
            }

            vkDestroySwapchainKHR(Renderer->Device, Renderer->SwapChain, nullptr);
        }

        // Assign new swapchain
        Renderer->SwapChain = newSwapchain;
    }

    // Color image views
    for (u32 i = 0; i < FIF; ++i)
    {
        VkImageViewCreateInfo imageViewInfo = {};
        imageViewInfo.sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO;
        imageViewInfo.image = Renderer->Images[i];
        imageViewInfo.viewType = VK_IMAGE_VIEW_TYPE_2D;
        imageViewInfo.format = Renderer->SurfaceFormat.format;
        imageViewInfo.components.r = VK_COMPONENT_SWIZZLE_IDENTITY;
        imageViewInfo.components.g = VK_COMPONENT_SWIZZLE_IDENTITY;
        imageViewInfo.components.b = VK_COMPONENT_SWIZZLE_IDENTITY;
        imageViewInfo.components.a = VK_COMPONENT_SWIZZLE_IDENTITY;
        imageViewInfo.subresourceRange = { VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1 };
        VkAssert(vkCreateImageView(Renderer->Device, &imageViewInfo, nullptr, &Renderer->ImageViews[i]));
    }

    // Depth resources
    if (Renderer->DepthTesting)
    {
        for (u32 i = 0; i < FIF; ++i)
        {
            VkImageCreateInfo imageCreateInfo = {};
            imageCreateInfo.sType = VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO;
            imageCreateInfo.pNext = nullptr;
            imageCreateInfo.imageType = VK_IMAGE_TYPE_2D;
            imageCreateInfo.extent = { SwapchainExtent.width, SwapchainExtent.height, 1 };
            imageCreateInfo.usage = VK_IMAGE_USAGE_DEPTH_STENCIL_ATTACHMENT_BIT;
            imageCreateInfo.format = Renderer->DepthFormat;
            imageCreateInfo.arrayLayers = 1;
            imageCreateInfo.mipLevels = 1;
            imageCreateInfo.tiling = VK_IMAGE_TILING_OPTIMAL;
            imageCreateInfo.samples = VK_SAMPLE_COUNT_1_BIT;
            imageCreateInfo.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
            imageCreateInfo.sharingMode = VK_SHARING_MODE_EXCLUSIVE;
            VkAssert(vkCreateImage(Renderer->Device, &imageCreateInfo, nullptr, &Renderer->DepthImages[i]));

            // Image memory
            VkMemoryRequirements memRequirements;
            vkGetImageMemoryRequirements(Renderer->Device, Renderer->DepthImages[i], &memRequirements);
            VkMemoryAllocateInfo allocInfo = {};
            allocInfo.sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO;
            allocInfo.allocationSize = memRequirements.size;
            allocInfo.memoryTypeIndex = VulkanFindMemoryType(Renderer->PhysicalDevice, memRequirements.memoryTypeBits, VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT);

            VkAssert(vkAllocateMemory(Renderer->Device, &allocInfo, nullptr, &Renderer->DepthImageMemories[i]));
            VkAssert(vkBindImageMemory(Renderer->Device, Renderer->DepthImages[i], Renderer->DepthImageMemories[i], 0));

            // Image view
            VkImageViewCreateInfo createInfo = {};
            createInfo.sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO;
            createInfo.image = Renderer->DepthImages[i];
            createInfo.viewType = VK_IMAGE_VIEW_TYPE_2D;
            createInfo.format = Renderer->DepthFormat;
            createInfo.components.r = VK_COMPONENT_SWIZZLE_IDENTITY;
            createInfo.components.g = VK_COMPONENT_SWIZZLE_IDENTITY;
            createInfo.components.b = VK_COMPONENT_SWIZZLE_IDENTITY;
            createInfo.components.a = VK_COMPONENT_SWIZZLE_IDENTITY;
            createInfo.subresourceRange = { VK_IMAGE_ASPECT_DEPTH_BIT, 0, 1, 0, 1 };
            VkAssert(vkCreateImageView(Renderer->Device, &createInfo, nullptr, &Renderer->DepthImagesViews[i]));
        }
    }

    // Framebuffers
    for (u32 i = 0; i < FIF; ++i)
    {
        VkImageView attachments[] = { Renderer->ImageViews[i], Renderer->DepthImagesViews[i] };
        VkFramebufferCreateInfo framebufferInfo = {};
        framebufferInfo.sType = VK_STRUCTURE_TYPE_FRAMEBUFFER_CREATE_INFO;
        framebufferInfo.renderPass = Renderer->RenderPass;
        framebufferInfo.attachmentCount = Renderer->DepthTesting ? CountOf(attachments) : (CountOf(attachments) - 1);
        framebufferInfo.pAttachments = attachments;
        framebufferInfo.width = SwapchainExtent.width;
        framebufferInfo.height = SwapchainExtent.height;
        framebufferInfo.layers = 1;
        VkAssert(vkCreateFramebuffer(Renderer->Device, &framebufferInfo, nullptr, &Renderer->FrameBuffers[i]));
    }

    Trace("Swapchain resized! %u, %u", SwapchainExtent.width, SwapchainExtent.height);
}

static vulkan_game_renderer CreateVulkanGameRenderer(game_window Window)
{
    vulkan_game_renderer Renderer;
    InitializeVulkan(&Renderer, Window);
    InitializeRendering(&Renderer);
    InitializeRenderingResources(&Renderer);

    Trace("Game renderer initialized.");
    return Renderer;
}

static void SubmitCube(vulkan_game_renderer* Renderer, v3 Translation, v3 Rotation, v3 Scale, v4 Color)
{
    m4 Transform = MyMath::Translate(m4(1.0f), Translation)
        * MyMath::ToM4(QTN(Rotation))
        * MyMath::Scale(m4(1.0f), Scale);

    for (u32 i = 0; i < CountOf(c_CubeVertexPositions); i++)
    {
        Renderer->QuadVertexDataPtr->Position = v3(Transform * c_CubeVertexPositions[i]);
        Renderer->QuadVertexDataPtr->Color = Color;
        Renderer->QuadVertexDataPtr++;
    }

    Renderer->QuadIndexCount += 36;
}

static void BeginRender(vulkan_game_renderer* Renderer, const m4& ViewProjection)
{
    bool JustResized = false;

    VkSemaphore CurrentSemaphore = Renderer->PresentSemaphores[Renderer->CurrentFrame];
    VkResult Result = vkAcquireNextImageKHR(Renderer->Device, Renderer->SwapChain, UINT64_MAX, CurrentSemaphore, nullptr, &Renderer->ImageIndex);

    if (Result != VK_SUCCESS)
    {
        if (Result == VK_SUBOPTIMAL_KHR || Result == VK_ERROR_OUT_OF_DATE_KHR)
        {
            Info("BeginRender: Requested resize!");
            Renderer->ResizeSwapChain = true;
        }
        else
        {
            Assert(false, "vkAcquireNextImageKHR was unsuccessful");
        }
    }

    if (Renderer->ResizeSwapChain)
    {
        Renderer->ResizeSwapChain = false;
        RecreateSwapChain(Renderer, Renderer->SwapChainSize.width, Renderer->SwapChainSize.height);
    }

    // Set view projection
    Renderer->PushConstant.CameraViewProjection = ViewProjection;

    // Reset batch renderer
    // Renderer->TextureStackIndex = 1;
    Renderer->QuadVertexDataPtr = Renderer->QuadVertexDataBase;
    Renderer->QuadIndexCount = 0;
}

static void EndRender(vulkan_game_renderer* Renderer)
{
    // Record command buffer
    {
        VkCommandBufferBeginInfo beginInfo = {};
        beginInfo.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO;
        beginInfo.pNext = nullptr;
        beginInfo.flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT;
        beginInfo.pInheritanceInfo = nullptr;
        VkCommandBuffer commandBuffer = Renderer->RenderCommandBuffers[Renderer->CurrentFrame];
        VkAssert(vkBeginCommandBuffer(commandBuffer, &beginInfo));

        VkClearValue clearValue[2];
        clearValue[0].color = { 0.2f, 0.3f, 0.8f, 1.0f };
        clearValue[1].depthStencil = { 1.0f, 0 };

        // Prepass
        {
            if (Renderer->QuadIndexCount > 0)
            {
                u32 VertexCount = static_cast<u32>(Renderer->QuadVertexDataPtr - Renderer->QuadVertexDataBase);
                VulkanVertexBufferSetData(Renderer->QuadVertexBuffers[Renderer->CurrentFrame], commandBuffer, Renderer->QuadVertexDataBase, VertexCount * sizeof(quad_vertex));
            }
        }
        //CmdPrepass(commandBuffer);

        VkRenderPassBeginInfo renderPassBeginInfo = {};
        renderPassBeginInfo.sType = VK_STRUCTURE_TYPE_RENDER_PASS_BEGIN_INFO;
        renderPassBeginInfo.renderPass = Renderer->RenderPass;
        renderPassBeginInfo.renderArea.offset = { 0, 0 };
        renderPassBeginInfo.renderArea.extent = Renderer->SwapChainSize;
        renderPassBeginInfo.clearValueCount = Renderer->DepthTesting ? CountOf(clearValue) : (CountOf(clearValue) - 1);
        renderPassBeginInfo.pClearValues = clearValue;
        renderPassBeginInfo.framebuffer = Renderer->FrameBuffers[Renderer->ImageIndex];
        vkCmdBeginRenderPass(commandBuffer, &renderPassBeginInfo, VK_SUBPASS_CONTENTS_INLINE);

        VkViewport viewport = {};
        viewport.x = 0.0f;
        viewport.y = static_cast<f32>(Renderer->SwapChainSize.height);
        viewport.width = static_cast<f32>(Renderer->SwapChainSize.width);
        viewport.height = viewport.y * -1.0f;
        viewport.minDepth = 0.0f;
        viewport.maxDepth = 1.0f;
        vkCmdSetViewport(commandBuffer, 0, 1, &viewport);

        VkRect2D scissor = {};
        scissor.offset = { 0,0 };
        scissor.extent = Renderer->SwapChainSize;
        vkCmdSetScissor(commandBuffer, 0, 1, &scissor);

        // Quad pass
        if (Renderer->QuadIndexCount > 0)
        {
            //vkCmdBindDescriptorSets(commandBuffer, VK_PIPELINE_BIND_POINT_GRAPHICS, Renderer->QuadPipeline.m_PipelineLayoutHandle, 0, 1, &Renderer->DescriptorSets[Renderer->CurrentFrame], 0, nullptr);
            vkCmdPushConstants(commandBuffer, Renderer->QuadPipeline.PipelineLayoutHandle, VK_SHADER_STAGE_VERTEX_BIT, 0, sizeof(push_constant_buffer), &Renderer->PushConstant);
            vkCmdBindPipeline(commandBuffer, VK_PIPELINE_BIND_POINT_GRAPHICS, Renderer->QuadPipeline.Handle);

            VkDeviceSize offset[] = { 0 };
            vkCmdBindVertexBuffers(commandBuffer, 0, 1, &Renderer->QuadVertexBuffers[Renderer->CurrentFrame].DeviceLocalBuffer.Handle, offset);
            vkCmdBindIndexBuffer(commandBuffer, Renderer->QuadIndexBuffer.Handle, 0, VK_INDEX_TYPE_UINT32);
            vkCmdDrawIndexed(commandBuffer, Renderer->QuadIndexCount, 1, 0, 0, 0);
        }

        vkCmdEndRenderPass(commandBuffer);

        vkEndCommandBuffer(commandBuffer);
    }

    // Submit command buffers
    {
        VkPipelineStageFlags waitStage = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT;

        VkSubmitInfo submitInfo = {};
        submitInfo.sType = VK_STRUCTURE_TYPE_SUBMIT_INFO;
        submitInfo.commandBufferCount = 1;
        submitInfo.pCommandBuffers = &Renderer->RenderCommandBuffers[Renderer->CurrentFrame];
        submitInfo.waitSemaphoreCount = 1;
        submitInfo.pWaitSemaphores = &Renderer->PresentSemaphores[Renderer->CurrentFrame];
        submitInfo.signalSemaphoreCount = 1;
        submitInfo.pSignalSemaphores = &Renderer->RenderFinishedSemaphores[Renderer->CurrentFrame];
        submitInfo.pWaitDstStageMask = &waitStage;

        VkAssert(vkResetFences(Renderer->Device, 1, &Renderer->InFlightFences[Renderer->CurrentFrame]));
        VkAssert(vkQueueSubmit(Renderer->GraphicsQueue, 1, &submitInfo, Renderer->InFlightFences[Renderer->CurrentFrame]));
    }

    // Present rendered frame
    {
        VkPresentInfoKHR presentInfo = {};
        presentInfo.sType = VK_STRUCTURE_TYPE_PRESENT_INFO_KHR;
        presentInfo.waitSemaphoreCount = 1;
        presentInfo.pWaitSemaphores = &Renderer->RenderFinishedSemaphores[Renderer->CurrentFrame];  // Wait for the frame to render
        presentInfo.swapchainCount = 1;
        presentInfo.pSwapchains = &Renderer->SwapChain;
        presentInfo.pImageIndices = &Renderer->ImageIndex;

        // TODO: Distinguish between graphics queue and present queue
        VkResult result = vkQueuePresentKHR(Renderer->GraphicsQueue, &presentInfo);

        if (result != VK_SUCCESS)
        {
            if (result == VK_ERROR_OUT_OF_DATE_KHR|| result == VK_SUBOPTIMAL_KHR)
            {
                Info("EndRender: Requested resize!");
                Renderer->ResizeSwapChain = true;
            }
            else
            {
                Assert(false, "vkQueuePresentKHR was not successful!");
            }
        }
    }

    // Cycle frames in flights
    Renderer->CurrentFrame = (Renderer->CurrentFrame + 1) % FIF;

    // Wait for the previous frame to finish - blocks cpu until signaled
    VkAssert(vkWaitForFences(Renderer->Device, 1, &Renderer->InFlightFences[Renderer->CurrentFrame], VK_TRUE, UINT64_MAX));
}
