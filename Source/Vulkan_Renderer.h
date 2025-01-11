#include "Vulkan.h"

// Double-buffer is sufficient
#define FIF 2

static constexpr inline u32 c_MaxQuadsPerBatch = 2048 * 1;
static constexpr inline u32 c_MaxQuadVertices = c_MaxQuadsPerBatch * 4;
static constexpr inline u32 c_MaxQuadIndices = c_MaxQuadsPerBatch * 6;

static constexpr inline u32 c_MaxTexturesPerDrawCall = 32; // TODO: Get this from the driver

//static constexpr inline V4 c_QuadVertexPositions[4]
//{
//    { -0.5f, -0.5f, 0.0f, 1.0f },
//    {  0.5f, -0.5f, 0.0f, 1.0f },
//    {  0.5f,  0.5f, 0.0f, 1.0f },
//    { -0.5f,  0.5f, 0.0f, 1.0f }
//};
//
//struct PushConstantBuffer
//{
//    M4 CameraViewProjection;
//};
//
//struct QuadVertex
//{
//    V3 Position;
//    V4 Color;
//    V2 TextureCoord;
//    u32 TextureIndex;
//    V2 TilingFactor;
//};

struct queue_family_indices
{
    u32 Graphics = -1;
    u32 Compute = -1;

    bool IsComplete()
    {
        return Graphics != -1 /* && m_Compute != -1*/;
    }
};

struct game_renderer
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

    bool ResizeSwapChain = true;

    VkSwapchainKHR SwapChain = nullptr;
    VkRenderPass RenderPass = nullptr;
    VkSurfaceCapabilitiesKHR SurfaceCapabilities;

    u32 CurrentFrame = 0;
    u32 ImageIndex = 0;

    // Batch renderer
    //V4 ClearColor = { 0.2f, 0.3f, 0.8f, 1.0f };
    //Texture2D DefaultWhiteTexture;
    //Texture2D TextureStack[c_MaxTexturesPerDrawCall] = {};
    u32 TextureStackIndex = 0;

    VkDescriptorSet DescriptorSets[FIF];
    VkDescriptorSetLayout DescriptorSetLayout;
    VkDescriptorPool DescriptorPool;

    //PushConstantBuffer PushConstant;

    // Solid Quads
    //IndexBuffer QuadIndexBuffer;
    //Shader QuadShader;
    //
    //QuadVertex QuadVertexDataBase[c_MaxQuadVertices];
    //QuadVertex* QuadVertexDataPtr = QuadVertexDataBase;
    //u32 QuadIndexCount = 0;
    //VertexBuffer QuadVertexBuffers[FIF];
    //GraphicsPipeline QuadPipeline;
    //
    //// Semi-transparent quads
    //QuadVertex SemiTransparentQuadVertexDataBase[c_MaxQuadVertices];
    //QuadVertex* SemiTransparentQuadVertexDataPtr = SemiTransparentQuadVertexDataBase;
    //u32 SemiTransparentQuadIndexCount = 0;
    //VertexBuffer SemiTransparentQuadVertexBuffers[FIF];
    //GraphicsPipeline SemiTransparentQuadPipeline;
};

static VkBool32 VKAPI_CALL VulkanDebugCallback(VkDebugUtilsMessageSeverityFlagBitsEXT messageSeverity, VkDebugUtilsMessageTypeFlagsEXT messageType, const VkDebugUtilsMessengerCallbackDataEXT* pCallbackData, void* pUserData)
{
    switch (messageSeverity)
    {
        //case VK_DEBUG_UTILS_MESSAGE_SEVERITY_VERBOSE_BIT_EXT: FAA_TRACE("Validation layer: %s", pCallbackData->pMessage); break;
        case VK_DEBUG_UTILS_MESSAGE_SEVERITY_INFO_BIT_EXT:    Log("Validation layer[INFO]: %s", pCallbackData->pMessage); break;
        case VK_DEBUG_UTILS_MESSAGE_SEVERITY_WARNING_BIT_EXT: Log("Validation layer[WARN]: %s", pCallbackData->pMessage); break;
        case VK_DEBUG_UTILS_MESSAGE_SEVERITY_ERROR_BIT_EXT:   Log("Validation layer[ERROR]: %s", pCallbackData->pMessage); break;
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


static void InitializeVulkan(game_renderer* Renderer, game_window Window)
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
            Log("[WARN] User has more than 5 GPUs!");
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

static void InitializeRendering(game_renderer* Renderer)
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

static void InitializeRenderingResources(game_renderer* Renderer)
{
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

    // Quad batch
    {
        // Index buffer
        {
            u32 quadIndices[c_MaxQuadIndices];
            u32 offset = 0;
            for (u32 i = 0; i < c_MaxQuadIndices; i += 6)
            {
                quadIndices[i + 0] = offset + 0;
                quadIndices[i + 1] = offset + 1;
                quadIndices[i + 2] = offset + 2;

                quadIndices[i + 3] = offset + 2;
                quadIndices[i + 4] = offset + 3;
                quadIndices[i + 5] = offset + 0;

                offset += 4;
            }

            r.QuadIndexBuffer.Create(quadIndices, c_MaxQuadIndices * sizeof(u32));
        }

        r.QuadShader.Create(Assets::GetAsset(ShaderCodeAsset::Quad));
        r.QuadPipeline.Create(
            r.QuadShader,
            r.RenderPass,
            VertexBufferLayout
            {
                { AttributeType::Vec3, "a_Position" },
                { AttributeType::Vec4, "a_Color" },
                { AttributeType::Vec2, "a_TextureCoord"},
                { AttributeType::UInt, "a_TextureIndex" },
                { AttributeType::Vec2, "a_TilingFactor" },
            },
            VertexBufferLayout{},
            sizeof(PushConstantBuffer),
            r.DescriptorSetLayout,
            0);

        for (auto& vertexBuffer : r.QuadVertexBuffers)
        {
            vertexBuffer.Create(c_MaxQuadVertices * sizeof(QuadVertex));
        }
    }
#endif

}

static game_renderer CreateGameRenderer(game_window Window)
{
    game_renderer Renderer;
    InitializeVulkan(&Renderer, Window);
    InitializeRendering(&Renderer);
    InitializeRenderingResources(&Renderer);

    Log("Game renderer initialized.");
    return Renderer;
}

static void BeginRender(game_renderer* Renderer)
{

}

static void EndRender(game_renderer* Renderer)
{

}
