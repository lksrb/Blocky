#pragma once

#pragma once

// Per vertex and per instance
#define FAA_VERTEX_BUFFER_LAYOUT_MAX_ATTRIBUTES 16

#include <initializer_list>

enum class AttributeType : u32
{
    Float = VK_FORMAT_R32_SFLOAT,
    Vec2 = VK_FORMAT_R32G32_SFLOAT,
    Vec3 = VK_FORMAT_R32G32B32_SFLOAT,
    Vec4 = VK_FORMAT_R32G32B32A32_SFLOAT,

    UInt = VK_FORMAT_R32_UINT,
    Int = VK_FORMAT_R32_SINT,
};

struct VertexAttribute
{
    AttributeType Type;
    const char* DebugName;
    u32 Offset = 0;

    VertexAttribute() = default;
    constexpr VertexAttribute(AttributeType type, const char* debugName)
        : Type(type), DebugName(debugName)
    {
    }
};

namespace Utils {

    static u32 GetSizeFromAttributeType(AttributeType type)
    {
        switch (type)
        {
            case AttributeType::Float: return 4 * 1;
            case AttributeType::Vec2:  return 4 * 2;
            case AttributeType::Vec3:  return 4 * 3;
            case AttributeType::Vec4:  return 4 * 4;
            case AttributeType::UInt:  return 4;
            case AttributeType::Int:   return 4;
        }

        Assert(false, "Invalid attribute type");
        return 0;
    }

}

struct vertex_buffer_layout
{
    u32 Stride = 0;
    u32 AttributeCount = 0;
    VertexAttribute Attributes[FAA_VERTEX_BUFFER_LAYOUT_MAX_ATTRIBUTES];

    vertex_buffer_layout() = default;

    // TODO: Replace initializer list
    constexpr vertex_buffer_layout(const std::initializer_list<VertexAttribute>& attributes)
    {
        // 16 attributes should be supported by every driver
        Assert(attributes.size() <= FAA_VERTEX_BUFFER_LAYOUT_MAX_ATTRIBUTES, "attributes.size() < FAA_VERTEX_BUFFER_LAYOUT_MAX_ATTRIBUTES");
        AttributeCount = static_cast<u32>(attributes.size());

        // Calculate offset and stride
        u32 i = 0;
        u32 offset = 0;
        for (auto& attribute : attributes)
        {
            u32 size = Utils::GetSizeFromAttributeType(attribute.Type);
            Attributes[i] = attribute;
            Attributes[i].Offset = offset;
            offset += size;
            Stride += size;
            ++i;
        }
    }

    constexpr auto begin() { return Attributes; }
    constexpr auto end() { return Attributes + AttributeCount; }
    constexpr auto begin() const { return Attributes; }
    constexpr auto end() const { return Attributes + AttributeCount; }
};
struct graphics_pipeline
{
    VkPipeline Handle;
    VkPipelineLayout PipelineLayoutHandle;
};

static graphics_pipeline CreateGraphicsPipeline(VkDevice Device,
    const shader& Shader,
    VkRenderPass renderPass,
    const vertex_buffer_layout& vertexLayout,
    const vertex_buffer_layout& instanceLayout,
    u32 pushConstantSize,
    VkDescriptorSetLayout descriptorSetLayout,
    u32 subpass)
{
    graphics_pipeline Pipeline;

    // ###############################################################################################################
    // ##################                                    Stages                                 ##################
    // ###############################################################################################################

    VkPipelineShaderStageCreateInfo shaderStagesCreateInfo[2] = {};
    shaderStagesCreateInfo[0].sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
    shaderStagesCreateInfo[0].pNext = nullptr;
    shaderStagesCreateInfo[0].stage = VK_SHADER_STAGE_VERTEX_BIT;
    shaderStagesCreateInfo[0].pName = "main";
    shaderStagesCreateInfo[0].module = Shader.VertexShaderModule;

    shaderStagesCreateInfo[1].sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
    shaderStagesCreateInfo[1].pNext = nullptr;
    shaderStagesCreateInfo[1].stage = VK_SHADER_STAGE_FRAGMENT_BIT;
    shaderStagesCreateInfo[1].pName = "main";
    shaderStagesCreateInfo[1].module = Shader.FragmentShaderModule;
    // ###############################################################################################################
    // ##################                                   Vertex Input                            ##################
    // ###############################################################################################################

    VkVertexInputAttributeDescription descriptions[FAA_VERTEX_BUFFER_LAYOUT_MAX_ATTRIBUTES];

    u32 location = 0;
    u32 bindingIndex = 0;
    VkVertexInputBindingDescription bindingDescriptions[2];

    // Per vertex
    if (vertexLayout.AttributeCount)
    {
        for (const auto& description : vertexLayout)
        {
            auto& desc = descriptions[location];
            desc.binding = bindingIndex;
            desc.location = location;
            desc.format = static_cast<VkFormat>(description.Type);
            desc.offset = description.Offset;
            location++;
        }

        auto& bindingDescription = bindingDescriptions[bindingIndex];
        bindingDescription.binding = bindingIndex;
        bindingDescription.stride = vertexLayout.Stride;
        bindingDescription.inputRate = VK_VERTEX_INPUT_RATE_VERTEX;
        bindingIndex++;
    }

    // Per instance
    if (instanceLayout.AttributeCount)
    {
        for (const auto& description : instanceLayout)
        {
            auto& desc = descriptions[location];
            desc.binding = bindingIndex;
            desc.location = location;
            desc.format = static_cast<VkFormat>(description.Type);
            desc.offset = description.Offset;
            location++;
        }

        auto& bindingDescription = bindingDescriptions[bindingIndex];
        bindingDescription.binding = bindingIndex;
        bindingDescription.stride = instanceLayout.Stride;
        bindingDescription.inputRate = VK_VERTEX_INPUT_RATE_INSTANCE;
        bindingIndex++;
    }

    VkPipelineVertexInputStateCreateInfo vertexInputState = {};
    vertexInputState.sType = VK_STRUCTURE_TYPE_PIPELINE_VERTEX_INPUT_STATE_CREATE_INFO;
    vertexInputState.pNext = nullptr;
    vertexInputState.vertexAttributeDescriptionCount = vertexLayout.AttributeCount + instanceLayout.AttributeCount;
    vertexInputState.pVertexAttributeDescriptions = descriptions;
    vertexInputState.vertexBindingDescriptionCount = bindingIndex;
    vertexInputState.pVertexBindingDescriptions = bindingDescriptions;

    // ###############################################################################################################
    // ##################                              Index Buffer                                 ##################
    // ###############################################################################################################
    VkPipelineInputAssemblyStateCreateInfo inputAssembly = {};
    inputAssembly.sType = VK_STRUCTURE_TYPE_PIPELINE_INPUT_ASSEMBLY_STATE_CREATE_INFO;
    inputAssembly.pNext = nullptr;
    inputAssembly.topology = VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST;
    inputAssembly.primitiveRestartEnable = VK_FALSE;

    // ###############################################################################################################
    // ##################                               Viewport State                              ##################
    // ###############################################################################################################
    VkPipelineViewportStateCreateInfo viewportState = {};
    viewportState.sType = VK_STRUCTURE_TYPE_PIPELINE_VIEWPORT_STATE_CREATE_INFO;
    viewportState.viewportCount = 1;
    viewportState.pViewports = nullptr;
    viewportState.scissorCount = 1;
    viewportState.pScissors = nullptr;

    // ###############################################################################################################
    // ##################                              Rasterizer                                   ##################
    // ###############################################################################################################
    VkPipelineRasterizationStateCreateInfo rasterizer = {};
    rasterizer.sType = VK_STRUCTURE_TYPE_PIPELINE_RASTERIZATION_STATE_CREATE_INFO;
    rasterizer.depthClampEnable = VK_FALSE;
    rasterizer.rasterizerDiscardEnable = VK_FALSE;
    rasterizer.polygonMode = VK_POLYGON_MODE_FILL;
    rasterizer.cullMode = VK_CULL_MODE_NONE; // VK_CULL_MODE_FRONT_BIT
    rasterizer.frontFace = VK_FRONT_FACE_CLOCKWISE;
    rasterizer.depthBiasEnable = VK_FALSE;
    rasterizer.depthBiasConstantFactor = 0.0f;          // Optional
    rasterizer.depthBiasClamp = 0.0f;                   // Optional
    rasterizer.depthBiasSlopeFactor = 0.0f;             // Optional

    // ###############################################################################################################
    // ##################                              Multisampling                                ##################
    // ###############################################################################################################
    VkPipelineMultisampleStateCreateInfo multisampleState = {};
    multisampleState.sType = VK_STRUCTURE_TYPE_PIPELINE_MULTISAMPLE_STATE_CREATE_INFO;
    multisampleState.sampleShadingEnable = VK_FALSE;
    multisampleState.rasterizationSamples = VK_SAMPLE_COUNT_1_BIT;
    multisampleState.minSampleShading = 1.0f; // Optional
    multisampleState.pSampleMask = nullptr; // Optional
    multisampleState.alphaToCoverageEnable = VK_FALSE; // Optional
    multisampleState.alphaToOneEnable = VK_FALSE; // Optional

    // ###############################################################################################################
    // ##################                          Depth and stencil testing                        ##################
    // ###############################################################################################################
    VkPipelineDepthStencilStateCreateInfo depthAndStencilState = {};
    depthAndStencilState.sType = VK_STRUCTURE_TYPE_PIPELINE_DEPTH_STENCIL_STATE_CREATE_INFO;
    depthAndStencilState.depthTestEnable = VK_TRUE;
    depthAndStencilState.depthWriteEnable = VK_TRUE;
    depthAndStencilState.stencilTestEnable = VK_FALSE;
    depthAndStencilState.depthCompareOp = VK_COMPARE_OP_LESS;
    depthAndStencilState.depthBoundsTestEnable = VK_FALSE;
    depthAndStencilState.minDepthBounds = -1.0f;
    depthAndStencilState.maxDepthBounds = 1.0f;

    // ###############################################################################################################
    // ##################                              Color Blending                               ##################
    // ###############################################################################################################

    VkPipelineColorBlendAttachmentState blendAttachments[1] = {};
    blendAttachments[0].colorWriteMask = VK_COLOR_COMPONENT_R_BIT | VK_COLOR_COMPONENT_G_BIT | VK_COLOR_COMPONENT_B_BIT | VK_COLOR_COMPONENT_A_BIT;
    blendAttachments[0].blendEnable = VK_TRUE;
    blendAttachments[0].srcColorBlendFactor = VK_BLEND_FACTOR_SRC_ALPHA;
    blendAttachments[0].dstColorBlendFactor = VK_BLEND_FACTOR_ONE_MINUS_SRC_ALPHA;
    blendAttachments[0].colorBlendOp = VK_BLEND_OP_ADD; // Optional
    blendAttachments[0].srcAlphaBlendFactor = VK_BLEND_FACTOR_ONE;
    blendAttachments[0].dstAlphaBlendFactor = VK_BLEND_FACTOR_ZERO;
    blendAttachments[0].alphaBlendOp = VK_BLEND_OP_ADD; // Optional

    VkPipelineColorBlendStateCreateInfo colorBlendState = {};
    colorBlendState.sType = VK_STRUCTURE_TYPE_PIPELINE_COLOR_BLEND_STATE_CREATE_INFO;
    colorBlendState.attachmentCount = CountOf(blendAttachments);
    colorBlendState.pAttachments = blendAttachments;
    colorBlendState.logicOpEnable = VK_FALSE;
    colorBlendState.logicOp = VK_LOGIC_OP_COPY; // Optional
    colorBlendState.blendConstants[0] = 1.0f; // Optional
    colorBlendState.blendConstants[1] = 1.0f; // Optional
    colorBlendState.blendConstants[2] = 1.0f; // Optional
    colorBlendState.blendConstants[3] = 1.0f; // Optional

    // ###############################################################################################################
    // ##################                              Dynamic States                               ##################
    // ###############################################################################################################
    VkDynamicState states[3] = { VK_DYNAMIC_STATE_VIEWPORT, VK_DYNAMIC_STATE_SCISSOR , VK_DYNAMIC_STATE_LINE_WIDTH };

    VkPipelineDynamicStateCreateInfo dynamicStatesInfo = {};
    dynamicStatesInfo.sType = VK_STRUCTURE_TYPE_PIPELINE_DYNAMIC_STATE_CREATE_INFO;
    dynamicStatesInfo.pDynamicStates = states;
    dynamicStatesInfo.flags = 0;
    dynamicStatesInfo.dynamicStateCount = CountOf(states);

    // ###############################################################################################################
    // ##################                              Pipeline Layout                              ##################
    // ###############################################################################################################
    // Uniforms, samplers, etc.

    VkPushConstantRange pushConstant = {};
    pushConstant.size = pushConstantSize;
    pushConstant.offset = 0;
    pushConstant.stageFlags = VK_SHADER_STAGE_VERTEX_BIT;

    VkPipelineLayoutCreateInfo pipelineLayoutInfo = {};
    pipelineLayoutInfo.sType = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO;
    pipelineLayoutInfo.pNext = nullptr;
    pipelineLayoutInfo.setLayoutCount = descriptorSetLayout ? 1 : 0;
    pipelineLayoutInfo.pSetLayouts = &descriptorSetLayout;
    pipelineLayoutInfo.pushConstantRangeCount = pushConstantSize > 0 ? 1 : 0;
    pipelineLayoutInfo.pPushConstantRanges = &pushConstant;
    VkAssert(vkCreatePipelineLayout(Device, &pipelineLayoutInfo, nullptr, &Pipeline.PipelineLayoutHandle));

    // ###############################################################################################################
    // ##################                              Pipeline Creation                            ##################
    // ###############################################################################################################
    VkGraphicsPipelineCreateInfo pipelineCreateInfo = {};
    pipelineCreateInfo.sType = VK_STRUCTURE_TYPE_GRAPHICS_PIPELINE_CREATE_INFO;
    pipelineCreateInfo.pNext = nullptr;

    pipelineCreateInfo.pStages = shaderStagesCreateInfo;
    pipelineCreateInfo.stageCount = CountOf(shaderStagesCreateInfo);
    pipelineCreateInfo.pVertexInputState = &vertexInputState;
    pipelineCreateInfo.pInputAssemblyState = &inputAssembly;
    pipelineCreateInfo.pViewportState = &viewportState;
    pipelineCreateInfo.pRasterizationState = &rasterizer;
    pipelineCreateInfo.pMultisampleState = &multisampleState;
    pipelineCreateInfo.pDepthStencilState = &depthAndStencilState;
    pipelineCreateInfo.pColorBlendState = &colorBlendState;
    pipelineCreateInfo.pDynamicState = &dynamicStatesInfo;
    pipelineCreateInfo.layout = Pipeline.PipelineLayoutHandle;
    pipelineCreateInfo.renderPass = renderPass;
    pipelineCreateInfo.subpass = subpass;
    pipelineCreateInfo.basePipelineHandle = nullptr; // Optional
    pipelineCreateInfo.basePipelineIndex = -1; // Optional

    VkAssert(vkCreateGraphicsPipelines(Device, nullptr, 1, &pipelineCreateInfo, nullptr, &Pipeline.Handle));

    Trace("Pipeline created!");

    return Pipeline;
}

static void DestroyGraphicsPipeline(VkDevice Device, graphics_pipeline* Pipeline)
{
    vkDestroyPipelineLayout(Device, Pipeline->PipelineLayoutHandle, nullptr);
    vkDestroyPipeline(Device, Pipeline->Handle, nullptr);

    Pipeline->Handle = nullptr;
    Pipeline->PipelineLayoutHandle = nullptr;
}
